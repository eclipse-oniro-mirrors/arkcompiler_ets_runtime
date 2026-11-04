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

#ifndef ECMASCRIPT_ARKSTEED_LOOP_EFFECTS_H
#define ECMASCRIPT_ARKSTEED_LOOP_EFFECTS_H

#include "ecmascript/arksteed/arksteed_compile_info_facts.h"
#include "ecmascript/mem/chunk_containers.h"

namespace panda::ecmascript::arksteed {

class LoopEffects {
public:
    explicit LoopEffects(Chunk *chunk);

    NO_COPY_SEMANTIC(LoopEffects);
    NO_MOVE_SEMANTIC(LoopEffects);

    void Record(const SideEffectDescriptor &effect);
    void RecordPropertyWrite(PropertyKey key);
    void RecordReceiverWrite(ValueVertex *receiver);
    void RecordAliasedEnvSlots(const CompileInfoFacts::ClearedEnvSlotKeys &slots);
    void Merge(const LoopEffects &other);

    bool InvalidatesAllLoadedProperties() const
    {
        return invalidatesAllLoadedProperties_;
    }

    bool InvalidatesPropertyKey(PropertyKey key) const
    {
        return invalidatesAllLoadedProperties_ || invalidatedPropertyKeys_.find(key) != invalidatedPropertyKeys_.end();
    }

    bool WritesReceiver(const ValueVertex *receiver) const
    {
        return writtenReceivers_.find(const_cast<ValueVertex *>(receiver)) != writtenReceivers_.end();
    }

    bool WritesEnvSlot(const ValueVertex *env, int32_t slot) const
    {
        return writtenEnvSlots_.find(EnvSlotKey(const_cast<ValueVertex *>(env), slot)) != writtenEnvSlots_.end();
    }

    bool InvalidatesAllEnvSlots() const
    {
        return invalidatesAllEnvSlots_;
    }

    bool ClearsUnstableNodeInfo() const
    {
        return clearsUnstableNodeInfo_;
    }

    bool MayHaveAliasingEnvs() const
    {
        return mayHaveAliasingEnvs_;
    }

    bool HasUnknownCall() const
    {
        return hasUnknownCall_;
    }

    bool HasMapTransition() const
    {
        return hasMapTransition_;
    }

    const ChunkVector<JSHClass *> &GetElementsKindTransitionSources() const
    {
        return elementsKindTransitionSources_;
    }

    size_t InvalidatedPropertyKeyCount() const
    {
        return invalidatedPropertyKeys_.size();
    }

    size_t WrittenReceiverCount() const
    {
        return writtenReceivers_.size();
    }

    size_t WrittenEnvSlotCount() const
    {
        return writtenEnvSlots_.size();
    }

private:
    void RecordElementsKindTransitionSource(JSHClass *source);

    ChunkSet<PropertyKey> invalidatedPropertyKeys_;
    ChunkSet<ValueVertex *> writtenReceivers_;
    ChunkSet<EnvSlotKey> writtenEnvSlots_;
    ChunkVector<JSHClass *> elementsKindTransitionSources_;
    bool invalidatesAllLoadedProperties_ {false};
    bool invalidatesAllEnvSlots_ {false};
    bool clearsUnstableNodeInfo_ {false};
    bool mayHaveAliasingEnvs_ {false};
    bool hasUnknownCall_ {false};
    bool hasMapTransition_ {false};
};

}  // namespace panda::ecmascript::arksteed

#endif  // ECMASCRIPT_ARKSTEED_LOOP_EFFECTS_H
