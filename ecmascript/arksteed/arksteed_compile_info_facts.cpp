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

#include "ecmascript/arksteed/arksteed_compile_info_facts.h"

#include <algorithm>

#include "ecmascript/arksteed/arksteed_opcode.h"
#include "ecmascript/arksteed/arksteed_loop_effects.h"
#include "ecmascript/mem/tagged_object.h"

namespace panda::ecmascript::arksteed {
namespace {
bool InJSTypeRange(JSType type, JSType first, JSType last)
{
    return static_cast<uint8_t>(type) >= static_cast<uint8_t>(first) &&
           static_cast<uint8_t>(type) <= static_cast<uint8_t>(last);
}

bool IsJSArrayLikeJSType(JSType type)
{
    return type == JSType::JS_ARRAY || InJSTypeRange(type, JSType::JS_API_ARRAY_LIST, JSType::JS_API_QUEUE);
}

bool IsJSTypedArrayJSType(JSType type)
{
    return InJSTypeRange(type, JSType::JS_TYPED_ARRAY_FIRST, JSType::JS_TYPED_ARRAY_LAST) ||
           InJSTypeRange(type, JSType::JS_SHARED_TYPED_ARRAY_FIRST, JSType::JS_SHARED_TYPED_ARRAY_LAST);
}

bool IsEcmaObjectJSType(JSType type)
{
    return static_cast<uint8_t>(type) > static_cast<uint8_t>(JSType::STRING_LAST);
}
}  // namespace

NodeInfo::NodeType NodeTypeFromJSTaggedValue(JSTaggedValue value)
{
    if (value.IsInt()) {
        return NodeInfo::NodeType::INT;
    }
    if (value.IsNull()) {
        return NodeInfo::NodeType::NULL_TYPE;
    }
    if (value.IsUndefined()) {
        return NodeInfo::NodeType::UNDEFINED;
    }
    if (value.IsTrue() || value.IsFalse()) {
        return NodeInfo::NodeType::BOOLEAN;
    }
    if (value.IsDouble()) {
        return NodeInfo::NodeType::DOUBLE;
    }
    if (value.IsHeapObject()) {
        return NodeTypeFromHClass(value.GetTaggedObject()->GetClass());
    }
    return NodeInfo::NodeType::UNKNOWN;
}

NodeInfo::NodeType NodeTypeFromJSType(JSType type)
{
    if (InJSTypeRange(type, JSType::STRING_FIRST, JSType::STRING_LAST)) {
        return NodeInfo::NodeType::STRING;
    }
    if (type == JSType::SYMBOL) {
        return NodeInfo::NodeType::SYMBOL;
    }
    if (type == JSType::BIGINT) {
        return NodeInfo::NodeType::BIGINT;
    }
    if (IsJSArrayLikeJSType(type)) {
        return NodeInfo::NodeType::JS_ARRAY;
    }
    if (InJSTypeRange(type, JSType::JS_FUNCTION_FIRST, JSType::JS_FUNCTION_LAST)) {
        return NodeInfo::NodeType::JS_FUNCTION;
    }
    if (IsJSTypedArrayJSType(type)) {
        return NodeInfo::NodeType::JS_TYPED_ARRAY;
    }
    if (IsEcmaObjectJSType(type)) {
        return NodeInfo::NodeType::OTHER_JS_RECEIVER;
    }
    return NodeInfo::NodeType::UNKNOWN;
}

NodeInfo::NodeType NodeTypeFromHClass(const JSHClass *hclass)
{
    if (hclass == nullptr) {
        return NodeInfo::NodeType::UNKNOWN;
    }
    return NodeTypeFromJSType(hclass->GetObjectType());
}

NodeInfo::NodeType NodeTypeFromHClass(ArkSteedHClassRef hclass)
{
    return hclass.IsJSHClass() ? NodeTypeFromJSType(static_cast<JSType>(hclass.GetHClassObjectType()))
                             : NodeInfo::NodeType::UNKNOWN;
}

AlternativeNodes::AlternativeNodes()
{
    store_.fill(nullptr);
}

ValueVertex *AlternativeNodes::Get(Kind kind) const
{
    return store_[static_cast<size_t>(kind)];
}

void AlternativeNodes::Set(Kind kind, ValueVertex *node)
{
    ASSERT(node != nullptr);
    store_[static_cast<size_t>(kind)] = node;
}

bool AlternativeNodes::Empty() const
{
    for (ValueVertex *node : store_) {
        if (node != nullptr) {
            return false;
        }
    }
    return true;
}

void AlternativeNodes::MergeWith(const AlternativeNodes &other)
{
    for (size_t index = 0; index < STORE_SIZE; ++index) {
        if (store_[index] != nullptr && store_[index] != other.store_[index]) {
            store_[index] = nullptr;
        }
    }
}

NodeInfo::NodeInfo(Chunk *chunk, const NodeInfo &other)
    : type_(other.type_),
      nonHole_(other.nonHole_),
      possibleHClassesAreStale_(other.possibleHClassesAreStale_),
      alternatives_(other.alternatives_),
      truthinessResults_(other.truthinessResults_),
      chunk_(chunk),
      possibleHClasses_(chunk)
{
    if (other.sharedHClasses_ != nullptr && other.chunk_ == chunk) {
        sharedHClasses_ = other.sharedHClasses_;
    } else {
        const auto &classes = other.GetHClassStorage();
        possibleHClasses_.assign(classes.begin(), classes.end());
    }
}

NodeInfo NodeInfo::CopyForUpdate(const NodeInfo &stored)
{
    NodeInfo info(stored.chunk_);
    info.type_ = stored.type_;
    info.nonHole_ = stored.nonHole_;
    info.alternatives_ = stored.alternatives_;
    info.truthinessResults_ = stored.truthinessResults_;
    info.sharedHClasses_ = &stored.GetHClassStorage();
    info.possibleHClassesAreStale_ = stored.possibleHClassesAreStale_;
    return info;
}

void NodeInfo::EnsureWritableHClasses()
{
    if (sharedHClasses_ == nullptr) {
        return;
    }
    possibleHClasses_.assign(sharedHClasses_->begin(), sharedHClasses_->end());
    sharedHClasses_ = nullptr;
}

bool NodeInfo::IsSameAs(const NodeInfo &other, bool effectiveStale) const
{
    if (type_ != other.type_ || nonHole_ != other.nonHole_ || !(alternatives_ == other.alternatives_) ||
        truthinessResults_ != other.truthinessResults_ || effectiveStale != other.possibleHClassesAreStale_) {
        return false;
    }
    const auto &left = GetHClassStorage();
    const auto &right = other.GetHClassStorage();
    return &left == &right || (left.size() == right.size() &&
                               std::equal(left.begin(), left.end(), right.begin(), [](const auto &a, const auto &b) {
                                   return a.hclass.Equals(b.hclass) && a.isStable == b.isStable;
                               }));
}

NodeInfo::PossibleHClasses NodeInfo::GetFreshPossibleHClasses() const
{
    if (!HasFreshPossibleHClasses()) {
        return {};
    }
    return GetPossibleHClassesForRevalidation();
}

NodeInfo::PossibleHClasses NodeInfo::GetPossibleHClassesForRevalidation() const
{
    PossibleHClasses result;
    result.reserve(GetHClassStorage().size());
    for (const auto &info : GetHClassStorage()) {
        result.push_back(info.hclass);
    }
    return result;
}

bool NodeInfo::HasUnstablePossibleHClass() const
{
    const auto &classes = GetHClassStorage();
    return std::any_of(classes.begin(), classes.end(), [](const PossibleHClassInfo &info) { return !info.isStable; });
}

bool NodeInfo::ContainsPossibleHClass(ArkSteedHClassRef hclass) const
{
    const auto &classes = GetHClassStorage();
    return std::any_of(classes.begin(), classes.end(),
                       [hclass](const PossibleHClassInfo &info) { return info.hclass.Equals(hclass); });
}

void NodeInfo::RecordHClass(ArkSteedHClassRef hclass, bool isStable)
{
    ClearPossibleHClasses();
    AddPossibleHClass(hclass, isStable);
    IntersectTypeWithPossibleHClasses();
    CheckPossibleHClassInvariants();
}

void NodeInfo::RecordPossibleHClasses(const PossibleHClasses &hclasses, bool isStable)
{
    ClearPossibleHClasses();
    for (ArkSteedHClassRef hclass : hclasses) {
        AddPossibleHClass(hclass, isStable);
    }
    IntersectTypeWithPossibleHClasses();
    CheckPossibleHClassInvariants();
}

void NodeInfo::RecordPossibleHClasses(const PossibleHClassInfos &hclasses)
{
    ClearPossibleHClasses();
    for (const PossibleHClassInfo &info : hclasses) {
        AddPossibleHClass(info.hclass, info.isStable);
    }
    IntersectTypeWithPossibleHClasses();
    CheckPossibleHClassInvariants();
}

bool NodeInfo::NarrowPossibleHClasses(const PossibleHClasses &hclasses)
{
    ASSERT(HasFreshPossibleHClasses());
    auto isRemoved = [&hclasses](const auto &info) {
        return std::none_of(hclasses.begin(), hclasses.end(),
                            [&info](ArkSteedHClassRef hclass) { return info.hclass.Equals(hclass); });
    };
    const auto &classes = GetHClassStorage();
    if (std::any_of(classes.begin(), classes.end(), isRemoved)) {
        EnsureWritableHClasses();
        possibleHClasses_.erase(std::remove_if(possibleHClasses_.begin(), possibleHClasses_.end(), isRemoved),
                                possibleHClasses_.end());
    }
    if (GetHClassStorage().empty()) {
        possibleHClassesAreStale_ = false;
        CheckPossibleHClassInvariants();
        return false;
    }
    IntersectTypeWithPossibleHClasses();
    CheckPossibleHClassInvariants();
    return true;
}

void NodeInfo::ClearPossibleHClasses()
{
    sharedHClasses_ = nullptr;
    possibleHClasses_.clear();
    possibleHClassesAreStale_ = false;
    CheckPossibleHClassInvariants();
}

void NodeInfo::MarkUnstableHClassesStale()
{
    if (HasUnstablePossibleHClass()) {
        possibleHClassesAreStale_ = true;
    }
    CheckPossibleHClassInvariants();
}

void NodeInfo::MarkHClassesStaleForElementsKindTransition(const PossibleHClasses &sourceHClasses)
{
    if (!HasFreshPossibleHClasses()) {
        return;
    }
    auto matches = [&sourceHClasses](const auto &info) {
        return std::any_of(sourceHClasses.begin(), sourceHClasses.end(),
                           [&info](ArkSteedHClassRef source) { return info.hclass.Equals(source); });
    };
    bool containsMatchingHClass = false;
    bool changesStability = false;
    for (const auto &info : GetHClassStorage()) {
        if (matches(info)) {
            containsMatchingHClass = true;
            changesStability |= info.isStable;
        }
    }
    if (changesStability) {
        EnsureWritableHClasses();
        for (auto &info : possibleHClasses_) {
            if (matches(info)) {
                info.isStable = false;
            }
        }
    }
    if (containsMatchingHClass) {
        possibleHClassesAreStale_ = true;
    }
    CheckPossibleHClassInvariants();
}

void NodeInfo::MarkPossibleHClassesFresh()
{
    ASSERT(!GetHClassStorage().empty());
    possibleHClassesAreStale_ = false;
    CheckPossibleHClassInvariants();
}

bool NodeInfo::MarkPossibleHClassStable(ArkSteedHClassRef hclass)
{
    ASSERT(!possibleHClassesAreStale_);
    const auto &classes = GetHClassStorage();
    auto it = std::find_if(classes.begin(), classes.end(),
                           [hclass](const PossibleHClassInfo &info) { return info.hclass.Equals(hclass); });
    if (it == classes.end()) {
        return false;
    }
    if (!it->isStable) {
        size_t index = static_cast<size_t>(it - classes.begin());
        EnsureWritableHClasses();
        possibleHClasses_[index].isStable = true;
    }
    CheckPossibleHClassInvariants();
    return true;
}

bool NodeInfo::MergeWith(const NodeInfo &other)
{
    UnionType(other.type_);
    nonHole_ = nonHole_ && other.nonHole_;
    alternatives_.MergeWith(other.alternatives_);
    for (size_t index = 0; index < truthinessResults_.size(); ++index) {
        if (truthinessResults_[index] != other.truthinessResults_[index]) {
            truthinessResults_[index] = nullptr;
        }
    }
    bool mergedHClassesAreStale = possibleHClassesAreStale_ || other.possibleHClassesAreStale_;
    UnionPossibleHClasses(other);
    possibleHClassesAreStale_ = GetHClassStorage().empty() ? false : mergedHClassesAreStale;
    CheckPossibleHClassInvariants();
    return !NoInfoAvailable();
}

bool NodeInfo::IsCompatibleWith(const NodeInfo &expected) const
{
    if (!NodeTypeIs(type_, expected.type_)) {
        return false;
    }
    for (size_t index = 0; index < truthinessResults_.size(); ++index) {
        if (expected.truthinessResults_[index] != nullptr &&
            truthinessResults_[index] != expected.truthinessResults_[index]) {
            return false;
        }
    }
    if (expected.possibleHClassesAreStale_) {
        return true;
    }
    if (!expected.HasUnstablePossibleHClass()) {
        return true;
    }
    if (GetHClassStorage().empty() || possibleHClassesAreStale_) {
        return false;
    }
    const auto &expectedClasses = expected.GetHClassStorage();
    for (const PossibleHClassInfo &actual : GetHClassStorage()) {
        auto expectedIt = std::find_if(expectedClasses.begin(), expectedClasses.end(),
                                       [&actual](const PossibleHClassInfo &candidate) {
                                           return candidate.hclass.Equals(actual.hclass);
                                       });
        if (expectedIt == expectedClasses.end() || (expectedIt->isStable && !actual.isStable)) {
            return false;
        }
    }
    return true;
}

void NodeInfo::AddPossibleHClass(ArkSteedHClassRef hclass, bool isStable)
{
    if (!hclass.IsJSHClass()) {
        return;
    }
    const auto &classes = GetHClassStorage();
    auto it = std::find_if(classes.begin(), classes.end(),
                           [hclass](const PossibleHClassInfo &info) { return info.hclass.Equals(hclass); });
    if (it != classes.end()) {
        if (it->isStable && !isStable) {
            size_t index = static_cast<size_t>(it - classes.begin());
            EnsureWritableHClasses();
            possibleHClasses_[index].isStable = false;
        }
        return;
    }
    EnsureWritableHClasses();
    possibleHClasses_.push_back(PossibleHClassInfo {
        .hclass = hclass,
        .isStable = isStable,
    });
}

void NodeInfo::IntersectTypeWithPossibleHClasses()
{
    if (GetHClassStorage().empty()) {
        return;
    }
    // Strings, symbols and bigints have HClasses too, but are not JS receivers.
    IntersectType(NodeType::ANY_HEAP_OBJECT);
    NodeType possibleType = NodeType::NONE;
    for (const PossibleHClassInfo &info : GetHClassStorage()) {
        possibleType = UnionNodeType(possibleType, NodeTypeFromHClass(info.hclass));
    }
    possibleType = IntersectNodeType(possibleType, NodeType::ANY_HEAP_OBJECT);
    if (!IsEmptyNodeType(possibleType)) {
        IntersectType(possibleType);
    }
}

void NodeInfo::UnionPossibleHClasses(const NodeInfo &other)
{
    if (GetHClassStorage().empty() || other.GetHClassStorage().empty()) {
        sharedHClasses_ = nullptr;
        possibleHClasses_.clear();
        return;
    }
    if (&GetHClassStorage() == &other.GetHClassStorage()) {
        return;
    }
    for (const auto &otherInfo : other.GetHClassStorage()) {
        AddPossibleHClass(otherInfo.hclass, otherInfo.isStable);
    }
}

void NodeInfo::CheckPossibleHClassInvariants() const
{
    ASSERT(!GetHClassStorage().empty() || !possibleHClassesAreStale_);
    ASSERT(!possibleHClassesAreStale_ || HasUnstablePossibleHClass());
}

CompileInfoFacts::CompileInfoFacts(Chunk *chunk)
    : chunk_(chunk),
      nodeInfos_(chunk),
      loadedProperties_(chunk),
      loadedConstantProperties_(chunk),
      loadedEnvSlots_(chunk),
      loadedEnvConstants_(chunk),
      cse_(chunk)
{
    ASSERT(chunk_ != nullptr);
}

CompileInfoFacts *CompileInfoFacts::Clone() const
{
    auto *copy = chunk_->New<CompileInfoFacts>(chunk_);
    copy->nodeInfos_.CopyFrom(nodeInfos_);
    copy->loadedProperties_.CopyFrom(loadedProperties_);
    copy->loadedConstantProperties_.CopyFrom(loadedConstantProperties_);
    copy->loadedEnvSlots_.CopyFrom(loadedEnvSlots_);
    copy->loadedEnvConstants_.CopyFrom(loadedEnvConstants_);
    copy->cse_.CopyFrom(cse_);
    copy->effectEpoch_ = effectEpoch_;
    copy->envSlotAliasMode_ = envSlotAliasMode_;
    copy->hclassGeneration_ = hclassGeneration_;
    copy->mayHaveFreshUnstableHClasses_ = mayHaveFreshUnstableHClasses_;
    return copy;
}

CompileInfoFacts *CompileInfoFacts::CloneForLoopHeader() const
{
    auto *copy = Clone();
    copy->MarkAllFreshUnstableHClassesStale();
    copy->nodeInfos_.TransformWithReuse(
        [](ValueVertex *, const StoredNodeInfo *info) { return info->info.NoInfoAvailable() ? nullptr : info; },
        [](const auto &metadata) { return !metadata.hasEmptyInfo; });
    copy->loadedProperties_.Clear();
    copy->loadedEnvSlots_.Clear();
    copy->IncrementEffectEpoch();
    copy->cse_.IntersectWith(copy->cse_, copy->effectEpoch_);
    copy->envSlotAliasMode_ = EnvSlotAliasMode::NONE;
    return copy;
}

CompileInfoFacts *CompileInfoFacts::CloneForOptimisticLoopHeader(const LoopEffects &effects) const
{
    auto *copy = Clone();
    if (effects.ClearsUnstableNodeInfo()) {
        copy->MarkAllFreshUnstableHClassesStale();
        copy->nodeInfos_.TransformWithReuse(
            [](ValueVertex *, const StoredNodeInfo *info) { return info->info.NoInfoAvailable() ? nullptr : info; },
            [](const auto &metadata) { return !metadata.hasEmptyInfo; });
    }

    copy->loadedProperties_.Transform([&effects](const LoadedPropertyKey &key, ValueVertex *value) {
        return !effects.InvalidatesPropertyKey(key.GetPropertyKey()) && !effects.WritesReceiver(key.GetReceiver())
                   ? value
                   : nullptr;
    });
    if (!effects.InvalidatesAllEnvSlots() && !effects.ClearsUnstableNodeInfo()) {
        copy->loadedEnvSlots_.Transform([&effects](const EnvSlotKey &key, ValueVertex *value) {
            return effects.WritesEnvSlot(key.GetEnv(), key.GetSlot()) ? nullptr : value;
        });
        if (!copy->loadedEnvSlots_.Empty()) {
            copy->envSlotAliasMode_ = effects.MayHaveAliasingEnvs() ? EnvSlotAliasMode::MAY_ALIAS : envSlotAliasMode_;
        } else {
            copy->envSlotAliasMode_ = EnvSlotAliasMode::NONE;
        }
    } else {
        copy->loadedEnvSlots_.Clear();
        copy->envSlotAliasMode_ = EnvSlotAliasMode::NONE;
    }

    const auto &transitionSources = effects.GetElementsKindTransitionSources();
    if (!transitionSources.empty()) {
        copy->MarkHClassesStaleForElementsKindTransition(
            NodeInfo::PossibleHClasses(transitionSources.begin(), transitionSources.end()));
    }
    copy->IncrementEffectEpoch();
    copy->cse_.IntersectWith(copy->cse_, copy->effectEpoch_);
    return copy;
}

bool CompileInfoFacts::IsCompatibleWithLoopHeader(const CompileInfoFacts &headerFacts, const BB *loopHeader) const
{
    ASSERT(chunk_ == headerFacts.chunk_);
    if (!headerFacts.loadedEnvSlots_.Empty() && headerFacts.envSlotAliasMode_ != EnvSlotAliasMode::MAY_ALIAS &&
        headerFacts.envSlotAliasMode_ != envSlotAliasMode_ && envSlotAliasMode_ != EnvSlotAliasMode::NONE) {
        return false;
    }
    bool hadEffects = effectEpoch_ != headerFacts.effectEpoch_;
    bool compatible = true;
    headerFacts.nodeInfos_.ForEach([&](ValueVertex *node, const StoredNodeInfo *expectedStored) {
        auto *phi = node->TryCast<PhiVertex>();
        if (phi != nullptr && static_cast<ValueVertex *>(phi)->GetOwner() == loopHeader) {
            return;
        }
        NodeInfoView actual = TryGetInfoFor(node);
        if (actual == nullptr) {
            compatible = false;
            return;
        }
        NodeInfoView expected(expectedStored, headerFacts.hclassGeneration_);
        compatible &= hadEffects ? actual.Materialize().IsCompatibleWith(expected.Materialize())
                                 : NodeInfo::NodeTypeIs(actual.GetType(), expected.GetType());
    });
    if (!compatible) {
        return false;
    }
    if (!hadEffects) {
        return true;
    }
    auto containsSameEntries = [](const auto &actual, const auto &expected) {
        bool same = true;
        expected.ForEach([&](const auto &key, auto value) { same &= actual.Find(key) == value; });
        return same;
    };
    if (!containsSameEntries(loadedProperties_, headerFacts.loadedProperties_) ||
        !containsSameEntries(loadedEnvSlots_, headerFacts.loadedEnvSlots_)) {
        return false;
    }
    return true;
}

void CompileInfoFacts::Merge(const CompileInfoFacts &other)
{
    ASSERT(chunk_ == other.chunk_);
    const HClassGeneration *leftGeneration = hclassGeneration_;
    const HClassGeneration *rightGeneration = other.hclassGeneration_;
    const HClassGeneration *mergedGeneration =
        leftGeneration == rightGeneration ? leftGeneration : chunk_->New<HClassGeneration>();
    // In a shared subtree with different generations, an unstable HClass cannot
    // be fresh on both paths. The new generation makes that same value stale.
    nodeInfos_.IntersectWith(
        other.nodeInfos_,
        [this, leftGeneration, rightGeneration, mergedGeneration](
            ValueVertex *, const StoredNodeInfo *left, const StoredNodeInfo *right) -> const StoredNodeInfo* {
            NodeInfoView leftView(left, leftGeneration);
            NodeInfo merged = leftView.Materialize();
            NodeInfo otherInfo = NodeInfoView(right, rightGeneration).Materialize();
            if (!merged.MergeWith(otherInfo)) {
                return nullptr;
            }
            if (mergedGeneration == leftGeneration && leftView.IsSameAs(merged)) {
                return left;
            }
            return chunk_->New<StoredNodeInfo>(chunk_, merged, mergedGeneration);
        },
        [](const auto &metadata) { return !metadata.hasEmptyInfo; });
    hclassGeneration_ = mergedGeneration;

    MergeLoadedProperties(loadedProperties_, other.loadedProperties_);
    MergeLoadedProperties(loadedConstantProperties_, other.loadedConstantProperties_);
    if (effectEpoch_ != other.effectEpoch_) {
        AdvanceEpochAfterMerge(other.effectEpoch_);
    }
    MergeEnvSlots(loadedEnvSlots_, other.loadedEnvSlots_);
    MergeEnvSlots(loadedEnvConstants_, other.loadedEnvConstants_);
    cse_.IntersectWith(other.cse_, effectEpoch_);
    envSlotAliasMode_ = MergeEnvSlotAliasMode(envSlotAliasMode_, other.envSlotAliasMode_);
    mayHaveFreshUnstableHClasses_ |= other.mayHaveFreshUnstableHClasses_;
}

void CompileInfoFacts::UpdateInfoForImpl(ValueVertex *node, void *context, void (*callback)(void *, NodeInfo &))
{
    nodeInfos_.Update(node, [this, context, callback](const StoredNodeInfo *stored) -> const StoredNodeInfo* {
        NodeInfoView existing(stored, hclassGeneration_);
        NodeInfo info = stored == nullptr ? NodeInfo(chunk_) : existing.Materialize();
        callback(context, info);
        if (stored != nullptr && existing.IsSameAs(info)) {
            return stored;
        }
        mayHaveFreshUnstableHClasses_ |= info.HasFreshPossibleHClasses() && info.HasUnstablePossibleHClass();
        return chunk_->New<StoredNodeInfo>(chunk_, info, hclassGeneration_);
    });
}

NodeInfo::NodeType CompileInfoFacts::GetKnownType(ValueVertex *node) const
{
    NodeInfo::NodeType staticType = node == nullptr ? NodeInfo::NodeType::UNKNOWN : node->GetStaticType();
    NodeInfoView info = TryGetInfoFor(node);
    if (info == nullptr) {
        return staticType;
    }
    return NodeInfo::IntersectNodeType(staticType, info->GetType());
}

bool CompileInfoFacts::MayBeNullOrUndefined(ValueVertex *node) const
{
    return NodeInfo::NodeTypeCanBe(GetKnownType(node), NodeInfo::NodeType::NULL_OR_UNDEFINED);
}

bool CompileInfoFacts::CheckType(ValueVertex *node, NodeInfo::NodeType type) const
{
    NodeInfo::NodeType knownType = GetKnownType(node);
    return !NodeInfo::IsEmptyNodeType(knownType) && NodeInfo::NodeTypeIs(knownType, type);
}

NodeInfo::NodeType CompileInfoFacts::EnsureType(ValueVertex *node, NodeInfo::NodeType type)
{
    ASSERT(node != nullptr);
    NodeInfoView existing = TryGetInfoFor(node);
    if (existing != nullptr) {
        NodeInfo::NodeType result = NodeInfo::IntersectNodeType(existing->GetType(), type);
        if (result == existing->GetType()) {
            return result;
        }
    }
    NodeInfo::NodeType result = NodeInfo::NodeType::UNKNOWN;
    UpdateInfoFromView(node, existing, [&](NodeInfo &info) { result = info.IntersectType(type); });
    return result;
}

void CompileInfoFacts::RecordNonHole(ValueVertex *node)
{
    ASSERT(node != nullptr);
    NodeInfoView existing = TryGetInfoFor(node);
    if (existing != nullptr && existing->IsNonHole()) {
        return;
    }
    UpdateInfoFromView(node, existing, [](NodeInfo &info) { info.MarkNonHole(); });
}

bool CompileInfoFacts::IsKnownNonHole(ValueVertex *node) const
{
    ASSERT(node != nullptr);
    NodeInfoView info = TryGetInfoFor(node);
    return info != nullptr && info->IsNonHole();
}

void CompileInfoFacts::RecordHClass(ValueVertex *node, ArkSteedHClassRef hclass, bool isStable)
{
    UpdateInfoFor(node, [&](NodeInfo &info) { info.RecordHClass(hclass, isStable); });
}

ArkSteedHClassRef CompileInfoFacts::TryGetHClass(ValueVertex *node) const
{
    NodeInfoView info = TryGetInfoFor(node);
    if (info == nullptr || !info->HasFreshKnownHClass()) {
        return {};
    }
    return info->GetFreshKnownHClass();
}

void CompileInfoFacts::RecordPossibleHClasses(ValueVertex *node, const NodeInfo::PossibleHClasses &hclasses,
                                              bool isStable)
{
    UpdateInfoFor(node, [&](NodeInfo &info) { info.RecordPossibleHClasses(hclasses, isStable); });
}

void CompileInfoFacts::RecordPossibleHClasses(ValueVertex *node, const NodeInfo::PossibleHClassInfos &hclasses)
{
    UpdateInfoFor(node, [&](NodeInfo &info) { info.RecordPossibleHClasses(hclasses); });
}

bool CompileInfoFacts::NarrowPossibleHClasses(ValueVertex *node, const NodeInfo::PossibleHClasses &hclasses)
{
    NodeInfoView info = TryGetInfoFor(node);
    if (info == nullptr || !info->HasFreshPossibleHClasses()) {
        return false;
    }
    bool narrowed = false;
    UpdateInfoFromView(node, info, [&](NodeInfo &current) { narrowed = current.NarrowPossibleHClasses(hclasses); });
    return narrowed;
}

void CompileInfoFacts::SetPossibleHClasses(ValueVertex *node, const ChunkVector<ArkSteedHClassRef> &hclasses,
                                           NodeInfo::NodeType possibleType)
{
    NodeInfo::PossibleHClasses copy;
    copy.reserve(hclasses.size());
    copy.insert(copy.end(), hclasses.begin(), hclasses.end());
    UpdateInfoFor(node, [&](NodeInfo &info) {
        info.RecordPossibleHClasses(copy, false);
        info.IntersectType(possibleType);
    });
}

std::optional<NodeInfo::PossibleHClasses> CompileInfoFacts::TryGetPossibleHClasses(ValueVertex *node) const
{
    NodeInfoView info = TryGetInfoFor(node);
    if (info == nullptr || !info->HasFreshPossibleHClasses()) {
        return std::nullopt;
    }
    return info->GetFreshPossibleHClasses();
}

std::optional<NodeInfo::PossibleHClasses> CompileInfoFacts::TryGetPossibleHClassesForRevalidation(
    ValueVertex *node) const
{
    NodeInfoView info = TryGetInfoFor(node);
    if (info == nullptr || info->PossibleHClassCount() == 0) {
        return std::nullopt;
    }
    return info->GetPossibleHClassesForRevalidation();
}

bool CompileInfoFacts::PossibleHClassesAreStale(ValueVertex *node) const
{
    NodeInfoView info = TryGetInfoFor(node);
    return info != nullptr && info->PossibleHClassesAreStale();
}

void CompileInfoFacts::MarkPossibleHClassesFresh(ValueVertex *node)
{
    NodeInfoView info = TryGetInfoFor(node);
    ASSERT(info != nullptr && info->PossibleHClassesAreStale());
    UpdateInfoFromView(node, info, [](NodeInfo &current) { current.MarkPossibleHClassesFresh(); });
}

bool CompileInfoFacts::MarkPossibleHClassStable(ValueVertex *node, ArkSteedHClassRef hclass)
{
    NodeInfoView existing = TryGetInfoFor(node);
    if (existing == nullptr) {
        return false;
    }
    bool result = false;
    UpdateInfoFromView(node, existing, [&](NodeInfo &info) { result = info.MarkPossibleHClassStable(hclass); });
    return result;
}

void CompileInfoFacts::SetAlternative(ValueVertex *node, AlternativeNodes::Kind kind, ValueVertex *alternative)
{
    ASSERT(node != nullptr && alternative != nullptr);
    NodeInfoView existing = TryGetInfoFor(node);
    if (existing != nullptr && existing->GetAlternatives().Get(kind) == alternative) {
        return;
    }
    UpdateInfoFromView(node, existing, [&](NodeInfo &info) { info.GetAlternatives().Set(kind, alternative); });
}

ValueVertex *CompileInfoFacts::TryGetAlternative(ValueVertex *node, AlternativeNodes::Kind kind) const
{
    NodeInfoView info = TryGetInfoFor(node);
    return info == nullptr ? nullptr : info->GetAlternatives().Get(kind);
}

ValueVertex *CompileInfoFacts::LookupTruthinessResult(ValueVertex *node, bool trueIfTruthy) const
{
    NodeInfoView info = TryGetInfoFor(node);
    return info == nullptr ? nullptr : info->GetTruthinessResult(trueIfTruthy);
}

void CompileInfoFacts::RecordTruthinessResult(ValueVertex *node, bool trueIfTruthy, ValueVertex *result)
{
    ASSERT(node != nullptr && result != nullptr);
    NodeInfoView existing = TryGetInfoFor(node);
    if (existing != nullptr && existing->GetTruthinessResult(trueIfTruthy) == result) {
        return;
    }
    UpdateInfoFromView(node, existing, [&](NodeInfo &info) { info.SetTruthinessResult(trueIfTruthy, result); });
}

void CompileInfoFacts::RecordLoadedProperty(const LoadedPropertyKey &key, ValueVertex *value)
{
    RecordLoadedProperty(loadedProperties_, key, value);
}

ValueVertex *CompileInfoFacts::LookupLoadedProperty(const LoadedPropertyKey &key) const
{
    return LookupLoadedProperty(loadedProperties_, key);
}

void CompileInfoFacts::RecordLoadedProperty(ValueVertex *object, PropertyKey key, ValueVertex *value)
{
    RecordLoadedProperty(LoadedPropertyKey::FromPropertyKey(object, key), value);
}

ValueVertex *CompileInfoFacts::LookupLoadedProperty(ValueVertex *object, PropertyKey key) const
{
    return LookupLoadedProperty(LoadedPropertyKey::FromPropertyKey(object, key));
}

void CompileInfoFacts::RecordLoadedConstantProperty(const LoadedPropertyKey &key, ValueVertex *value)
{
    RecordLoadedProperty(loadedConstantProperties_, key, value);
}

ValueVertex *CompileInfoFacts::LookupLoadedConstantProperty(const LoadedPropertyKey &key) const
{
    return LookupLoadedProperty(loadedConstantProperties_, key);
}

void CompileInfoFacts::RecordLoadedConstantProperty(ValueVertex *object, PropertyKey key, ValueVertex *value)
{
    RecordLoadedConstantProperty(LoadedPropertyKey::FromPropertyKey(object, key), value);
}

ValueVertex *CompileInfoFacts::LookupLoadedConstantProperty(ValueVertex *object, PropertyKey key) const
{
    return LookupLoadedConstantProperty(LoadedPropertyKey::FromPropertyKey(object, key));
}

void CompileInfoFacts::ClearLoadedProperties()
{
    loadedProperties_.Clear();
}

void CompileInfoFacts::ClearLoadedPropertiesForReceiver(ValueVertex *receiver, LoopEffects *effects)
{
    if (effects != nullptr) {
        effects->RecordReceiverWrite(receiver);
    }
    loadedProperties_.Transform([receiver](const LoadedPropertyKey &key, ValueVertex *value) {
        return key.MatchesReceiver(receiver) ? nullptr : value;
    });
}

void CompileInfoFacts::ClearLoadedPropertiesForKey(PropertyKey key, LoopEffects *effects)
{
    if (effects != nullptr) {
        effects->RecordPropertyWrite(key);
    }
    if (key.GetKind() == PropertyKey::Kind::UNKNOWN) {
        ClearLoadedProperties();
        return;
    }
    loadedProperties_.Transform([key, effects](const LoadedPropertyKey &entry, ValueVertex *value) {
        PropertyKey cachedKey = entry.GetPropertyKey();
        // Named keys from different representations may refer to the same property.
        bool mayAliasNamedKey = key.GetKind() == PropertyKey::Kind::CONST_DATA_ID &&
            (cachedKey.GetKind() == PropertyKey::Kind::HEAP_CONSTANT ||
             cachedKey.GetKind() == PropertyKey::Kind::NAMED || cachedKey.GetKind() == PropertyKey::Kind::INDEX);
        if (entry.MatchesPropertyKey(key) || key.MayAliasConstDataId(cachedKey) || mayAliasNamedKey) {
            if (effects != nullptr) {
                effects->RecordPropertyWrite(cachedKey);
            }
            return static_cast<ValueVertex *>(nullptr);
        }
        return value;
    });
}

void CompileInfoFacts::RecordEnvSlot(ValueVertex *env, int32_t slot, ValueVertex *value)
{
    ASSERT(env != nullptr);
    ASSERT(value != nullptr);
    loadedEnvSlots_.InsertOrAssign(EnvSlotKey(env, slot), value);
    UpdateEnvSlotAliasMode(env);
}

ValueVertex *CompileInfoFacts::LookupEnvSlot(ValueVertex *env, int32_t slot) const
{
    return loadedEnvSlots_.Find(EnvSlotKey(env, slot));
}

void CompileInfoFacts::RecordEnvConstant(ValueVertex *env, int32_t slot, ValueVertex *value)
{
    ASSERT(env != nullptr);
    ASSERT(value != nullptr);
    loadedEnvConstants_.InsertOrAssign(EnvSlotKey(env, slot), value);
}

ValueVertex *CompileInfoFacts::LookupEnvConstant(ValueVertex *env, int32_t slot) const
{
    return loadedEnvConstants_.Find(EnvSlotKey(env, slot));
}

CompileInfoFacts::ClearedEnvSlotKeys CompileInfoFacts::ClearAliasedEnvSlotsFor(ValueVertex *env, int32_t slot,
                                                                               ValueVertex *newValue)
{
    ClearedEnvSlotKeys cleared(chunk_);
    if (env == nullptr || slot < 0) {
        OnSideEffect();
        return cleared;
    }

    // Lexical-env bytecodes write user variable slots. The parent/global-env
    // fields are tracked as constants and do not invalidate cached user slots.
    if (IsEnvConstantFieldOffset(slot)) {
        return cleared;
    }

    UpdateEnvSlotAliasMode(env);
    if (envSlotAliasMode_ != EnvSlotAliasMode::MAY_ALIAS) {
        return cleared;
    }

    loadedEnvSlots_.Transform([&](const EnvSlotKey &key, ValueVertex *value) {
        if (key.GetSlot() == slot && key.GetEnv() != env && EnvMayAlias(key.GetEnv(), env) && value != newValue) {
            cleared.emplace_back(key);
            return static_cast<ValueVertex *>(nullptr);
        }
        return value;
    });
    return cleared;
}

void CompileInfoFacts::ClearEnvSlotsFor(ValueVertex *env)
{
    if (env == nullptr) {
        return;
    }
    bool erased = false;
    loadedEnvSlots_.Transform([&](const EnvSlotKey &key, ValueVertex *value) {
        if (key.GetEnv() == env) {
            erased = true;
            return static_cast<ValueVertex *>(nullptr);
        }
        return value;
    });
    if (erased) {
        RecomputeEnvSlotAliasMode();
    }
}

void CompileInfoFacts::MarkPossibleSideEffect(const SideEffectDescriptor &effect, LoopEffects *loopEffects)
{
    if (loopEffects != nullptr && effect.kind != SideEffectKind::FIELD_WRITE &&
        effect.kind != SideEffectKind::ELEMENTS_WRITE) {
        loopEffects->Record(effect);
    }
    switch (effect.kind) {
        case SideEffectKind::NO_SIDE_EFFECT:
            return;
        case SideEffectKind::FIELD_WRITE:
            ClearLoadedPropertiesForKey(effect.propertyKey, loopEffects);
            IncrementEffectEpoch();
            return;
        case SideEffectKind::ELEMENTS_WRITE:
            // A backing-store slot write does not replace elements or change array length.
            // Element value loads still depend on the memory effect epoch.
            IncrementEffectEpoch();
            return;
        case SideEffectKind::ENV_SLOT_WRITE: {
            ClearedEnvSlotKeys aliased = ClearAliasedEnvSlotsFor(effect.env, effect.envSlot, effect.envSlotValue);
            if (loopEffects != nullptr) {
                loopEffects->RecordAliasedEnvSlots(aliased);
            }
            IncrementEffectEpoch();
            return;
        }
        case SideEffectKind::MAP_TRANSITION:
            MarkHClassesStaleForTransition(effect.receiver);
            ClearLoadedProperties();
            IncrementEffectEpoch();
            return;
        case SideEffectKind::ELEMENTS_KIND_TRANSITION:
            ASSERT(effect.transitionSources != nullptr);
            MarkHClassesStaleForElementsKindTransition(*effect.transitionSources);
            IncrementEffectEpoch();
            return;
        case SideEffectKind::UNKNOWN_CALL:
            OnSideEffect();
            return;
        case SideEffectKind::TYPED_ARRAY_WRITE:
            IncrementEffectEpoch();
            return;
        case SideEffectKind::SAFE_CALL:
            if (effect.advancesEffectEpoch) {
                IncrementEffectEpoch();
            }
            return;
    }
}

void CompileInfoFacts::MarkAllFreshUnstableHClassesStale()
{
    if (!mayHaveFreshUnstableHClasses_) {
        return;
    }
    // Generations are never reused, including across independently built paths.
    hclassGeneration_ = chunk_->New<HClassGeneration>();
    mayHaveFreshUnstableHClasses_ = false;
}

void CompileInfoFacts::MarkHClassesStaleForTransition(ValueVertex *receiver)
{
    if (!mayHaveFreshUnstableHClasses_) {
        return;
    }
    NodeInfoView receiverInfo = TryGetInfoFor(receiver);
    ArkSteedHClassRef oldHClass = receiverInfo == nullptr ? ArkSteedHClassRef {} : receiverInfo->GetFreshKnownHClass();
    if (!oldHClass.IsJSHClass()) {
        MarkAllFreshUnstableHClassesStale();
        return;
    }
    nodeInfos_.Transform([&](ValueVertex *, const StoredNodeInfo *stored) -> const StoredNodeInfo* {
        NodeInfoView info(stored, hclassGeneration_);
        if (!info.HasFreshPossibleHClasses() || !info.HasUnstablePossibleHClass() ||
            !info.ContainsPossibleHClass(oldHClass)) {
            return stored;
        }
        NodeInfo updated = info.Materialize();
        updated.MarkUnstableHClassesStale();
        return chunk_->New<StoredNodeInfo>(chunk_, updated, hclassGeneration_);
    });
}

void CompileInfoFacts::MarkHClassesStaleForElementsKindTransition(const NodeInfo::PossibleHClasses &sourceHClasses)
{
    ASSERT(!sourceHClasses.empty());
    loadedProperties_.Transform([&](const LoadedPropertyKey &key, ValueVertex *value) {
        if (!(key.GetPropertyKey() == PropertyKey::Elements())) {
            return value;
        }
        NodeInfoView info = TryGetInfoFor(key.GetReceiver());
        bool mayAlias = info == nullptr || !info->HasFreshPossibleHClasses() ||
                        std::any_of(sourceHClasses.begin(), sourceHClasses.end(),
                                    [info](ArkSteedHClassRef source) { return info.ContainsPossibleHClass(source); });
        return mayAlias ? nullptr : value;
    });
    nodeInfos_.Transform([&](ValueVertex *, const StoredNodeInfo *stored) -> const StoredNodeInfo* {
        NodeInfoView info(stored, hclassGeneration_);
        bool affected = info.HasFreshPossibleHClasses() &&
                        std::any_of(sourceHClasses.begin(), sourceHClasses.end(),
                                    [info](ArkSteedHClassRef source) { return info.ContainsPossibleHClass(source); });
        if (!affected) {
            return stored;
        }
        NodeInfo updated = info.Materialize();
        updated.MarkHClassesStaleForElementsKindTransition(sourceHClasses);
        return chunk_->New<StoredNodeInfo>(chunk_, updated, hclassGeneration_);
    });
}

void CompileInfoFacts::ClearAll()
{
    nodeInfos_.Clear();
    loadedProperties_.Clear();
    loadedConstantProperties_.Clear();
    loadedEnvSlots_.Clear();
    loadedEnvConstants_.Clear();
    cse_.Clear();
    envSlotAliasMode_ = EnvSlotAliasMode::NONE;
    mayHaveFreshUnstableHClasses_ = false;
}

void CompileInfoFacts::OnSideEffect()
{
    MarkAllFreshUnstableHClassesStale();
    ClearLoadedProperties();
    loadedEnvSlots_.Clear();
    envSlotAliasMode_ = EnvSlotAliasMode::NONE;
    IncrementEffectEpoch();
}

void CompileInfoFacts::IncrementEffectEpoch()
{
    cse_.ClearMemoryExpressions();
    if (effectEpoch_ < EFFECT_EPOCH_OVERFLOW) {
        ++effectEpoch_;
    }
}

void CompileInfoFacts::AdvanceEpochAfterMerge(uint32_t otherEpoch)
{
    cse_.ClearMemoryExpressions();
    uint32_t mergedEpoch = std::max(effectEpoch_, otherEpoch);
    if (mergedEpoch < EFFECT_EPOCH_OVERFLOW) {
        effectEpoch_ = mergedEpoch + 1;
        return;
    }
    effectEpoch_ = EFFECT_EPOCH_OVERFLOW;
}

ValueVertex *CompileInfoFacts::LookupLoadedProperty(const LoadedPropertyMap &map, const LoadedPropertyKey &key) const
{
    return map.Find(key);
}

void CompileInfoFacts::RecordLoadedProperty(LoadedPropertyMap &map, const LoadedPropertyKey &key, ValueVertex *value)
{
    ASSERT(key.GetReceiver() != nullptr);
    ASSERT(value != nullptr);
    map.InsertOrAssign(key, value);
}

void CompileInfoFacts::MergeLoadedProperties(LoadedPropertyMap &target, const LoadedPropertyMap &other)
{
    target.IntersectWith(
        other,
        [](const LoadedPropertyKey &, ValueVertex *left, ValueVertex *right) { return left == right ? left : nullptr; },
        [](const auto &) { return true; });
}

void CompileInfoFacts::MergeEnvSlots(LoadedEnvSlots &target, const LoadedEnvSlots &other)
{
    target.IntersectWith(
        other, [](const EnvSlotKey &, ValueVertex *left, ValueVertex *right) { return left == right ? left : nullptr; },
        [](const auto &) { return true; });
}

void CompileInfoFacts::UpdateEnvSlotAliasMode(ValueVertex *env)
{
    if (envSlotAliasMode_ == EnvSlotAliasMode::MAY_ALIAS) {
        return;
    }
    EnvSlotAliasMode mode = EnvSlotAliasMode::MAY_ALIAS;
    if (env != nullptr && env->Is<InitialValueVertex>()) {
        mode = EnvSlotAliasMode::CURRENT_ENV_ONLY;
    } else if (env != nullptr && (env->Is<TaggedConstantVertex>() || env->Is<HeapConstantVertex>())) {
        mode = EnvSlotAliasMode::CONSTANT_ENV_ONLY;
    }
    envSlotAliasMode_ = MergeEnvSlotAliasMode(envSlotAliasMode_, mode);
}

void CompileInfoFacts::RecomputeEnvSlotAliasMode()
{
    envSlotAliasMode_ = EnvSlotAliasMode::NONE;
    loadedEnvSlots_.ForEach([this](const EnvSlotKey &key, ValueVertex *) { UpdateEnvSlotAliasMode(key.GetEnv()); });
}

bool CompileInfoFacts::EnvMayAlias(ValueVertex *lhs, ValueVertex *rhs) const
{
    return lhs != rhs;
}

EnvSlotAliasMode CompileInfoFacts::MergeEnvSlotAliasMode(EnvSlotAliasMode lhs, EnvSlotAliasMode rhs)
{
    if (lhs == rhs) {
        return lhs;
    }
    if (lhs == EnvSlotAliasMode::NONE) {
        return rhs;
    }
    if (rhs == EnvSlotAliasMode::NONE) {
        return lhs;
    }
    return EnvSlotAliasMode::MAY_ALIAS;
}

}  // namespace panda::ecmascript::arksteed
