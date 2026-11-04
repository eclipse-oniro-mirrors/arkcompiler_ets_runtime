/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "ecmascript/arksteed/arksteed_loop_effects.h"

#include <algorithm>

namespace panda::ecmascript::arksteed {

LoopEffects::LoopEffects(Chunk *chunk)
    : invalidatedPropertyKeys_(chunk),
      writtenReceivers_(chunk),
      writtenEnvSlots_(chunk),
      elementsKindTransitionSources_(chunk)
{
}

void LoopEffects::Record(const SideEffectDescriptor &effect)
{
    switch (effect.kind) {
        case SideEffectKind::NO_SIDE_EFFECT:
            return;
        case SideEffectKind::FIELD_WRITE:
            RecordPropertyWrite(effect.propertyKey);
            return;
        case SideEffectKind::ELEMENTS_WRITE:
            return;
        case SideEffectKind::ENV_SLOT_WRITE:
            if (effect.env == nullptr || effect.envSlot < 0) {
                invalidatesAllEnvSlots_ = true;
                mayHaveAliasingEnvs_ = true;
            } else {
                writtenEnvSlots_.insert(EnvSlotKey(effect.env, effect.envSlot));
            }
            return;
        case SideEffectKind::MAP_TRANSITION:
            invalidatesAllLoadedProperties_ = true;
            clearsUnstableNodeInfo_ = true;
            hasMapTransition_ = true;
            return;
        case SideEffectKind::ELEMENTS_KIND_TRANSITION:
            ASSERT(effect.transitionSources != nullptr && !effect.transitionSources->empty());
            for (JSHClass *source : *effect.transitionSources) {
                RecordElementsKindTransitionSource(source);
            }
            hasMapTransition_ = true;
            return;
        case SideEffectKind::UNKNOWN_CALL:
            invalidatesAllLoadedProperties_ = true;
            invalidatesAllEnvSlots_ = true;
            clearsUnstableNodeInfo_ = true;
            mayHaveAliasingEnvs_ = true;
            hasUnknownCall_ = true;
            return;
        case SideEffectKind::TYPED_ARRAY_WRITE:
        case SideEffectKind::SAFE_CALL:
            return;
    }
}

void LoopEffects::RecordElementsKindTransitionSource(JSHClass *source)
{
    if (std::find(elementsKindTransitionSources_.begin(), elementsKindTransitionSources_.end(), source) ==
        elementsKindTransitionSources_.end()) {
        elementsKindTransitionSources_.push_back(source);
    }
}

void LoopEffects::RecordPropertyWrite(PropertyKey key)
{
    if (key.GetKind() == PropertyKey::Kind::UNKNOWN) {
        invalidatesAllLoadedProperties_ = true;
    } else {
        invalidatedPropertyKeys_.insert(key);
    }
}

void LoopEffects::RecordReceiverWrite(ValueVertex *receiver)
{
    writtenReceivers_.insert(receiver);
}

void LoopEffects::RecordAliasedEnvSlots(const CompileInfoFacts::ClearedEnvSlotKeys &slots)
{
    writtenEnvSlots_.insert(slots.begin(), slots.end());
    mayHaveAliasingEnvs_ |= !slots.empty();
}

void LoopEffects::Merge(const LoopEffects &other)
{
    invalidatedPropertyKeys_.insert(other.invalidatedPropertyKeys_.begin(), other.invalidatedPropertyKeys_.end());
    writtenReceivers_.insert(other.writtenReceivers_.begin(), other.writtenReceivers_.end());
    writtenEnvSlots_.insert(other.writtenEnvSlots_.begin(), other.writtenEnvSlots_.end());
    for (JSHClass *source : other.elementsKindTransitionSources_) {
        RecordElementsKindTransitionSource(source);
    }
    invalidatesAllLoadedProperties_ |= other.invalidatesAllLoadedProperties_;
    invalidatesAllEnvSlots_ |= other.invalidatesAllEnvSlots_;
    clearsUnstableNodeInfo_ |= other.clearsUnstableNodeInfo_;
    mayHaveAliasingEnvs_ |= other.mayHaveAliasingEnvs_;
    hasUnknownCall_ |= other.hasUnknownCall_;
    hasMapTransition_ |= other.hasMapTransition_;
}
}  // namespace panda::ecmascript::arksteed
