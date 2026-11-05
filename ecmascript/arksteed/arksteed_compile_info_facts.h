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

#ifndef ECMASCRIPT_ARKSTEED_COMPILE_INFO_FACTS_H
#define ECMASCRIPT_ARKSTEED_COMPILE_INFO_FACTS_H

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <tuple>
#include <vector>

#include "ecmascript/arksteed/arksteed_cse_types.h"
#include "ecmascript/arksteed/arksteed_vertex.h"
#include "ecmascript/arksteed/arksteed_heap_ref.h"
#include "ecmascript/base_env.h"
#include "ecmascript/js_hclass.h"
#include "ecmascript/js_tagged_value.h"
#include "ecmascript/lexical_env.h"
#include "ecmascript/mem/chunk_containers.h"
#include "libpandabase/macros.h"

namespace panda::ecmascript::arksteed {

class ValueVertex;
class LoopEffects;
enum class VertexOpcode : uint16_t;

class AlternativeNodes {
public:
    enum class Kind : uint8_t {
        TAGGED,
        INT32,
        UINT32,
        INT64,
        FLOAT64,
        HOLEY_FLOAT64,
        CHECKED_VALUE,
        ELEMENT_INDEX_I32,  // Index-only conversion; may canonicalize -0.
        COUNT,
    };

    AlternativeNodes();

    ValueVertex *Get(Kind kind) const;
    void Set(Kind kind, ValueVertex *node);
    bool Empty() const;
    void MergeWith(const AlternativeNodes &other);

    bool operator==(const AlternativeNodes &other) const
    {
        return store_ == other.store_;
    }

private:
    static constexpr size_t STORE_SIZE = static_cast<size_t>(Kind::COUNT);
    std::array<ValueVertex *, STORE_SIZE> store_;
};

class NodeInfo {
public:
    struct PossibleHClassInfo {
        ArkSteedHClassRef hclass {};
        bool isStable {false};
    };

    // Interface types only: exchanged across API boundaries as stack-scoped
    // temporaries. They must never become members of chunk-placed objects.
    using PossibleHClasses = std::vector<ArkSteedHClassRef>;
    using PossibleHClassInfos = std::vector<PossibleHClassInfo>;

    using NodeType = arksteed::NodeType;

    explicit NodeInfo(Chunk *chunk) : chunk_(chunk), possibleHClasses_(chunk) {}
    NodeInfo(Chunk *chunk, const NodeInfo &other);
    ~NodeInfo() = default;

    // Preserve borrowed immutable HClass storage; copy only an owned list.
    NodeInfo(const NodeInfo &other) : NodeInfo(other.chunk_, other) {}

    NodeInfo &operator=(const NodeInfo &other)
    {
        if (this != &other) {
            ASSERT(chunk_ == other.chunk_);
            type_ = other.type_;
            nonHole_ = other.nonHole_;
            possibleHClassesAreStale_ = other.possibleHClassesAreStale_;
            alternatives_ = other.alternatives_;
            truthinessResults_ = other.truthinessResults_;
            if (other.sharedHClasses_ == &possibleHClasses_) {
                sharedHClasses_ = nullptr;
            } else if (other.sharedHClasses_ != nullptr) {
                possibleHClasses_.clear();
                sharedHClasses_ = other.sharedHClasses_;
            } else {
                possibleHClasses_.assign(other.possibleHClasses_.begin(), other.possibleHClasses_.end());
                sharedHClasses_ = nullptr;
            }
        }
        return *this;
    }

    DEFAULT_MOVE_SEMANTIC(NodeInfo);

    static constexpr uint16_t ToNodeTypeBits(NodeType type)
    {
        return static_cast<uint16_t>(type);
    }

    static constexpr NodeType ToNodeType(uint16_t bits)
    {
        return static_cast<NodeType>(bits);
    }

    static constexpr NodeType UnionNodeType(NodeType lhs, NodeType rhs)
    {
        return ToNodeType(ToNodeTypeBits(lhs) | ToNodeTypeBits(rhs));
    }

    static constexpr NodeType IntersectNodeType(NodeType lhs, NodeType rhs)
    {
        return ToNodeType(ToNodeTypeBits(lhs) & ToNodeTypeBits(rhs));
    }

    static constexpr NodeType RemoveNodeType(NodeType type, NodeType remove)
    {
        return ToNodeType(ToNodeTypeBits(type) & ~ToNodeTypeBits(remove));
    }

    static constexpr bool NodeTypeIs(NodeType type, NodeType expected)
    {
        return (ToNodeTypeBits(type) & ~ToNodeTypeBits(expected)) == 0;
    }

    static constexpr bool NodeTypeCanBe(NodeType type, NodeType expected)
    {
        return ToNodeTypeBits(IntersectNodeType(type, expected)) != ToNodeTypeBits(NodeType::NONE);
    }

    static constexpr bool IsEmptyNodeType(NodeType type)
    {
        return ToNodeTypeBits(type) == ToNodeTypeBits(NodeType::NONE);
    }

    NodeType GetType() const
    {
        return type_;
    }

    NodeType SetType(NodeType type)
    {
        type_ = type;
        return type_;
    }

    NodeType IntersectType(NodeType type)
    {
        type_ = IntersectNodeType(type_, type);
        return type_;
    }

    NodeType UnionType(NodeType type)
    {
        type_ = UnionNodeType(type_, type);
        return type_;
    }

    bool CheckType(NodeType type) const
    {
        return NodeTypeIs(type_, type);
    }

    bool IsNonHole() const
    {
        return nonHole_;
    }

    void MarkNonHole()
    {
        nonHole_ = true;
    }

    bool HasFreshKnownHClass() const
    {
        return GetHClassStorage().size() == 1 && !possibleHClassesAreStale_;
    }

    ArkSteedHClassRef GetFreshKnownHClass() const
    {
        return HasFreshKnownHClass() ? GetHClassStorage().front().hclass : ArkSteedHClassRef {};
    }

    bool HasFreshPossibleHClasses() const
    {
        return !GetHClassStorage().empty() && !possibleHClassesAreStale_;
    }

    PossibleHClasses GetFreshPossibleHClasses() const;
    PossibleHClasses GetPossibleHClassesForRevalidation() const;

    bool PossibleHClassesAreStale() const
    {
        return possibleHClassesAreStale_;
    }

    bool HasUnstablePossibleHClass() const;
    bool ContainsPossibleHClass(ArkSteedHClassRef hclass) const;

    size_t PossibleHClassCount() const
    {
        return GetHClassStorage().size();
    }

    bool HClassIsStable() const
    {
        return HasFreshKnownHClass() && GetHClassStorage().front().isStable;
    }

    void RecordHClass(ArkSteedHClassRef hclass, bool isStable);
    void RecordPossibleHClasses(const PossibleHClasses &hclasses, bool isStable);
    void RecordPossibleHClasses(const PossibleHClassInfos &hclasses);
    bool NarrowPossibleHClasses(const PossibleHClasses &hclasses);
    void ClearPossibleHClasses();
    void MarkUnstableHClassesStale();
    void MarkHClassesStaleForElementsKindTransition(const PossibleHClasses &sourceHClasses);
    void MarkPossibleHClassesFresh();
    bool MarkPossibleHClassStable(ArkSteedHClassRef hclass);
    bool MergeWith(const NodeInfo &other);
    bool IsCompatibleWith(const NodeInfo &expected) const;

    AlternativeNodes &GetAlternatives()
    {
        return alternatives_;
    }

    const AlternativeNodes &GetAlternatives() const
    {
        return alternatives_;
    }

    ValueVertex *GetTruthinessResult(bool trueIfTruthy) const
    {
        return truthinessResults_[trueIfTruthy ? 1 : 0];
    }

    void SetTruthinessResult(bool trueIfTruthy, ValueVertex *result)
    {
        truthinessResults_[trueIfTruthy ? 1 : 0] = result;
    }

    bool NoInfoAvailable() const
    {
        return type_ == NodeType::UNKNOWN && !nonHole_ && alternatives_.Empty() && GetHClassStorage().empty() &&
               truthinessResults_[0] == nullptr && truthinessResults_[1] == nullptr;
    }

private:
    friend class NodeInfoView;
    using HClassStorage = ChunkVector<PossibleHClassInfo>;

    // Only immutable Chunk-stored values may lend their payload to an update.
    static NodeInfo CopyForUpdate(const NodeInfo &stored);
    bool IsSameAs(const NodeInfo &other, bool effectiveStale) const;
    const HClassStorage &GetHClassStorage() const
    {
        return sharedHClasses_ == nullptr ? possibleHClasses_ : *sharedHClasses_;
    }
    void EnsureWritableHClasses();
    void AddPossibleHClass(ArkSteedHClassRef hclass, bool isStable);
    void IntersectTypeWithPossibleHClasses();
    void UnionPossibleHClasses(const NodeInfo &other);
    void CheckPossibleHClassInvariants() const;

    NodeType type_ {NodeType::UNKNOWN};
    bool nonHole_ {false};
    bool possibleHClassesAreStale_ {false};
    AlternativeNodes alternatives_;
    std::array<ValueVertex *, 2> truthinessResults_ {nullptr, nullptr};  // 2: false and true result polarities.
    Chunk *chunk_ {nullptr};
    HClassStorage possibleHClasses_;
    const HClassStorage *sharedHClasses_ {nullptr};
};

struct HClassGeneration {
    uint8_t unused {0};
};

struct StoredNodeInfo {
    StoredNodeInfo(Chunk *chunk, const NodeInfo &value, const HClassGeneration *generation)
        : info(chunk, value), hclassGeneration(generation)
    {}

    NodeInfo info;
    const HClassGeneration *hclassGeneration;
};

struct NodeInfoHamtTraits {
    static uint32_t MinEpoch([[maybe_unused]] const StoredNodeInfo *value)
    {
        return std::numeric_limits<uint32_t>::max();
    }

    static bool HasEmptyInfo(const StoredNodeInfo *value)
    {
        return value->info.NoInfoAvailable();
    }
};

// The same immutable value can be fresh in one path and stale in another.
class NodeInfoView {
public:
    NodeInfoView(const StoredNodeInfo *stored, const HClassGeneration *generation)
        : stored_(stored), generation_(generation)
    {}

    bool operator==(std::nullptr_t) const
    {
        return stored_ == nullptr;
    }
    bool operator!=(std::nullptr_t) const
    {
        return stored_ != nullptr;
    }
    const NodeInfoView *operator->() const
    {
        ASSERT(stored_ != nullptr);
        return this;
    }

    NodeInfo::NodeType GetType() const
    {
        return stored_->info.GetType();
    }
    bool NoInfoAvailable() const
    {
        return stored_->info.NoInfoAvailable();
    }
    bool IsNonHole() const
    {
        return stored_->info.IsNonHole();
    }
    const AlternativeNodes &GetAlternatives() const
    {
        return stored_->info.GetAlternatives();
    }
    ValueVertex *GetTruthinessResult(bool trueIfTruthy) const
    {
        return stored_->info.GetTruthinessResult(trueIfTruthy);
    }
    bool HasUnstablePossibleHClass() const
    {
        return stored_->info.HasUnstablePossibleHClass();
    }
    bool ContainsPossibleHClass(ArkSteedHClassRef hclass) const
    {
        return stored_->info.ContainsPossibleHClass(hclass);
    }
    size_t PossibleHClassCount() const
    {
        return stored_->info.PossibleHClassCount();
    }
    bool PossibleHClassesAreStale() const
    {
        return stored_->info.PossibleHClassesAreStale() ||
               (stored_->hclassGeneration != generation_ && stored_->info.HasUnstablePossibleHClass());
    }
    bool HasFreshPossibleHClasses() const
    {
        return PossibleHClassCount() != 0 && !PossibleHClassesAreStale();
    }
    bool HasFreshKnownHClass() const
    {
        return PossibleHClassCount() == 1 && !PossibleHClassesAreStale();
    }
    ArkSteedHClassRef GetFreshKnownHClass() const
    {
        return HasFreshKnownHClass() ? stored_->info.GetFreshKnownHClass() : ArkSteedHClassRef {};
    }
    NodeInfo::PossibleHClasses GetFreshPossibleHClasses() const
    {
        return HasFreshPossibleHClasses() ? stored_->info.GetPossibleHClassesForRevalidation()
                                          : NodeInfo::PossibleHClasses {};
    }
    NodeInfo::PossibleHClasses GetPossibleHClassesForRevalidation() const
    {
        return stored_->info.GetPossibleHClassesForRevalidation();
    }
    NodeInfo Materialize() const
    {
        NodeInfo info = NodeInfo::CopyForUpdate(stored_->info);
        if (PossibleHClassesAreStale()) {
            info.MarkUnstableHClassesStale();
        }
        return info;
    }

    bool IsSameAs(const NodeInfo &info) const
    {
        ASSERT(stored_ != nullptr);
        return stored_->info.IsSameAs(info, PossibleHClassesAreStale());
    }

private:
    const StoredNodeInfo *stored_;
    const HClassGeneration *generation_;
};

NodeInfo::NodeType NodeTypeFromJSTaggedValue(JSTaggedValue value);
NodeInfo::NodeType NodeTypeFromJSType(JSType type);
NodeInfo::NodeType NodeTypeFromHClass(const JSHClass *hclass);
NodeInfo::NodeType NodeTypeFromHClass(ArkSteedHClassRef hclass);

class PropertyKey {
public:
    enum class Kind : uint8_t {
        NAMED,
        INDEX,
        ELEMENTS,
        LENGTH,
        TYPED_ARRAY_LENGTH,
        MODULE_ENVIRONMENT,
        EXTERNAL_MODULE_DICTIONARY,
        EXTERNAL_MODULE_INDEX,
        CONST_DATA_ID,
        HEAP_CONSTANT,
        UNKNOWN,
    };

    static PropertyKey Named(uint32_t id)
    {
        return PropertyKey(Kind::NAMED, id);
    }

    static PropertyKey Index(uint32_t index)
    {
        return PropertyKey(Kind::INDEX, index);
    }

    static PropertyKey Elements()
    {
        return PropertyKey(Kind::ELEMENTS, 0);
    }

    static PropertyKey Length()
    {
        return PropertyKey(Kind::LENGTH, 0);
    }

    // Internal array length, not the overridable JavaScript "length" property.
    static PropertyKey TypedArrayLength()
    {
        return PropertyKey(Kind::TYPED_ARRAY_LENGTH, 0);
    }

    static PropertyKey ModuleEnvironment()
    {
        return PropertyKey(Kind::MODULE_ENVIRONMENT, 0);
    }

    static PropertyKey ExternalModuleDictionary(uint32_t index)
    {
        return PropertyKey(Kind::EXTERNAL_MODULE_DICTIONARY, index);
    }

    static PropertyKey ExternalModuleIndex(uint32_t index)
    {
        return PropertyKey(Kind::EXTERNAL_MODULE_INDEX, index);
    }

    static PropertyKey ConstDataId(uint32_t constDataId, uint32_t constantPoolScope = 0)
    {
        return PropertyKey(Kind::CONST_DATA_ID,
                           (static_cast<uint64_t>(constantPoolScope) << CONST_DATA_SCOPE_SHIFT) | constDataId);
    }

    bool MayAliasConstDataId(PropertyKey other) const
    {
        // Indices are unique within a pool scope. Different scopes may use different indices for the same name.
        return kind_ == Kind::CONST_DATA_ID && other.kind_ == Kind::CONST_DATA_ID &&
               (payload_ == other.payload_ ||
                (payload_ >> CONST_DATA_SCOPE_SHIFT) != (other.payload_ >> CONST_DATA_SCOPE_SHIFT));
    }

    static PropertyKey HeapConstant(uint32_t handleIndex)
    {
        return PropertyKey(Kind::HEAP_CONSTANT, handleIndex);
    }

    static PropertyKey Unknown()
    {
        return PropertyKey(Kind::UNKNOWN, 0);
    }

    Kind GetKind() const
    {
        return kind_;
    }

    uint64_t GetPayload() const
    {
        return payload_;
    }

    bool operator<(const PropertyKey &other) const
    {
        return std::tie(kind_, payload_) < std::tie(other.kind_, other.payload_);
    }

    bool operator==(const PropertyKey &other) const
    {
        return kind_ == other.kind_ && payload_ == other.payload_;
    }

private:
    static constexpr uint32_t CONST_DATA_SCOPE_SHIFT = 32;

    PropertyKey(Kind kind, uint64_t payload) : kind_(kind), payload_(payload) {}

    Kind kind_;
    uint64_t payload_;
};

class LoadedPropertyKey {
public:
    static LoadedPropertyKey FromPropertyKey(ValueVertex *receiver, PropertyKey propertyKey)
    {
        return LoadedPropertyKey(receiver, propertyKey, 0);
    }

    static LoadedPropertyKey ConstDataId(ValueVertex *receiver, uint32_t constDataId, PropertyLookupResult plr,
                                         uint32_t constantPoolScope = 0)
    {
        return LoadedPropertyKey(receiver, PropertyKey::ConstDataId(constDataId, constantPoolScope), plr.GetData());
    }

    static LoadedPropertyKey HeapConstant(ValueVertex *receiver, uint32_t handleIndex, PropertyLookupResult plr)
    {
        return LoadedPropertyKey(receiver, PropertyKey::HeapConstant(handleIndex), plr.GetData());
    }

    ValueVertex *GetReceiver() const
    {
        return receiver_;
    }

    PropertyKey GetPropertyKey() const
    {
        return propertyKey_;
    }

    uint32_t GetPropertyLookupResult() const
    {
        return propertyLookupResult_;
    }

    bool MatchesReceiver(ValueVertex *receiver) const
    {
        return receiver_ == receiver;
    }

    bool MatchesPropertyKey(PropertyKey key) const
    {
        return propertyKey_ == key;
    }

private:
    LoadedPropertyKey(ValueVertex *receiver, PropertyKey propertyKey, uint32_t propertyLookupResult)
        : receiver_(receiver), propertyKey_(propertyKey), propertyLookupResult_(propertyLookupResult)
    {}

    ValueVertex *receiver_ {nullptr};
    PropertyKey propertyKey_ {PropertyKey::Unknown()};
    uint32_t propertyLookupResult_ {0};
};

struct LoadedPropertyKeyHash {
    uint64_t operator()(const LoadedPropertyKey &key) const
    {
        uint64_t hash = MixFactsHash(reinterpret_cast<uintptr_t>(key.GetReceiver()));
        hash = MixFactsHash(hash ^ static_cast<uint64_t>(key.GetPropertyKey().GetKind()));
        hash = MixFactsHash(hash ^ key.GetPropertyKey().GetPayload());
        return MixFactsHash(hash ^ key.GetPropertyLookupResult());
    }
};

struct LoadedPropertyKeyEqual {
    bool operator()(const LoadedPropertyKey &lhs, const LoadedPropertyKey &rhs) const
    {
        return lhs.GetReceiver() == rhs.GetReceiver() && lhs.GetPropertyKey() == rhs.GetPropertyKey() &&
               lhs.GetPropertyLookupResult() == rhs.GetPropertyLookupResult();
    }
};

class EnvSlotKey {
public:
    EnvSlotKey(ValueVertex *env, int32_t slot) : env_(env), slot_(slot) {}

    ValueVertex *GetEnv() const
    {
        return env_;
    }

    int32_t GetSlot() const
    {
        return slot_;
    }

    bool operator<(const EnvSlotKey &other) const
    {
        if (std::less<ValueVertex *>()(env_, other.env_)) {
            return true;
        }
        if (std::less<ValueVertex *>()(other.env_, env_)) {
            return false;
        }
        return slot_ < other.slot_;
    }

private:
    ValueVertex *env_;
    int32_t slot_;
};

struct EnvSlotKeyHash {
    uint64_t operator()(const EnvSlotKey &key) const
    {
        return MixFactsHash(MixFactsHash(reinterpret_cast<uintptr_t>(key.GetEnv())) ^
                            static_cast<uint32_t>(key.GetSlot()));
    }
};

struct EnvSlotKeyEqual {
    bool operator()(const EnvSlotKey &lhs, const EnvSlotKey &rhs) const
    {
        return lhs.GetEnv() == rhs.GetEnv() && lhs.GetSlot() == rhs.GetSlot();
    }
};

inline bool IsEnvConstantFieldOffset(int32_t offset)
{
    constexpr int32_t taggedSize = static_cast<int32_t>(JSTaggedValue::TaggedTypeSize());
    constexpr int32_t dataOffset = static_cast<int32_t>(TaggedArray::DATA_OFFSET);
    return offset == dataOffset + static_cast<int32_t>(BaseEnv::GLOBAL_ENV_INDEX) * taggedSize ||
           offset == dataOffset + static_cast<int32_t>(LexicalEnv::PARENT_ENV_INDEX) * taggedSize;
}

enum class EnvSlotAliasMode : uint8_t {
    NONE,
    CURRENT_ENV_ONLY,
    CONSTANT_ENV_ONLY,
    MAY_ALIAS,
};

struct SideEffectDescriptor {
    SideEffectKind kind {SideEffectKind::NO_SIDE_EFFECT};
    bool advancesEffectEpoch {true};
    ValueVertex *receiver {nullptr};
    PropertyKey propertyKey {PropertyKey::Unknown()};
    ValueVertex *env {nullptr};
    int32_t envSlot {-1};
    ValueVertex *envSlotValue {nullptr};
    // Borrowed only for the synchronous facts/effects update during graph building.
    const NodeInfo::PossibleHClasses *transitionSources {nullptr};
};

class CompileInfoFacts {
public:
    using ClearedEnvSlotKeys = ChunkVector<EnvSlotKey>;

    explicit CompileInfoFacts(Chunk *chunk);

    NO_COPY_SEMANTIC(CompileInfoFacts);
    NO_MOVE_SEMANTIC(CompileInfoFacts);

    CompileInfoFacts *Clone() const;
    CompileInfoFacts *CloneForLoopHeader() const;
    CompileInfoFacts *CloneForOptimisticLoopHeader(const LoopEffects &effects) const;
    bool IsCompatibleWithLoopHeader(const CompileInfoFacts &headerFacts, const BB *loopHeader) const;
    void Merge(const CompileInfoFacts &other);

    NodeInfoView TryGetInfoFor(ValueVertex *node) const
    {
        return NodeInfoView(nodeInfos_.Find(node), hclassGeneration_);
    }

    // The callback operates on a private temporary; no writable stored value escapes.
    template <class Callback>
    void UpdateInfoFor(ValueVertex *node, Callback callback)
    {
        ASSERT(node != nullptr);
        // Keep the recursive HAMT updater out of vertex-specific instantiations.
        UpdateInfoForImpl(node, &callback,
                          [](void *context, NodeInfo &info) { (*static_cast<Callback *>(context))(info); });
    }

    NodeInfo::NodeType GetKnownType(ValueVertex *node) const;
    bool MayBeNullOrUndefined(ValueVertex *node) const;
    bool CheckType(ValueVertex *node, NodeInfo::NodeType type) const;
    template <size_t N>
    std::optional<NodeInfo::NodeType> CheckTypes(ValueVertex *node,
                                                 const std::array<NodeInfo::NodeType, N> &types) const
    {
        NodeInfo::NodeType knownType = GetKnownType(node);
        if (NodeInfo::IsEmptyNodeType(knownType)) {
            return std::nullopt;
        }

        for (NodeInfo::NodeType type : types) {
            if (!NodeInfo::NodeTypeIs(knownType, type)) {
                continue;
            }
            return type;
        }
        return std::nullopt;
    }

    NodeInfo::NodeType EnsureType(ValueVertex *node, NodeInfo::NodeType type);
    void RecordNonHole(ValueVertex *node);
    bool IsKnownNonHole(ValueVertex *node) const;
    void RecordHClass(ValueVertex *node, ArkSteedHClassRef hclass, bool isStable);
    ArkSteedHClassRef TryGetHClass(ValueVertex *node) const;
    void RecordPossibleHClasses(ValueVertex *node, const NodeInfo::PossibleHClasses &hclasses, bool isStable);
    void RecordPossibleHClasses(ValueVertex *node, const NodeInfo::PossibleHClassInfos &hclasses);
    bool NarrowPossibleHClasses(ValueVertex *node, const NodeInfo::PossibleHClasses &hclasses);
    void SetPossibleHClasses(ValueVertex *node, const ChunkVector<ArkSteedHClassRef> &hclasses,
                             NodeInfo::NodeType possibleType);
    std::optional<NodeInfo::PossibleHClasses> TryGetPossibleHClasses(ValueVertex *node) const;
    std::optional<NodeInfo::PossibleHClasses> TryGetPossibleHClassesForRevalidation(ValueVertex *node) const;
    bool PossibleHClassesAreStale(ValueVertex *node) const;
    void MarkPossibleHClassesFresh(ValueVertex *node);
    bool MarkPossibleHClassStable(ValueVertex *node, ArkSteedHClassRef hclass);
    void SetAlternative(ValueVertex *node, AlternativeNodes::Kind kind, ValueVertex *alternative);
    ValueVertex *TryGetAlternative(ValueVertex *node, AlternativeNodes::Kind kind) const;
    ValueVertex *LookupTruthinessResult(ValueVertex *node, bool trueIfTruthy) const;
    void RecordTruthinessResult(ValueVertex *node, bool trueIfTruthy, ValueVertex *result);

    void RecordLoadedProperty(const LoadedPropertyKey &key, ValueVertex *value);
    ValueVertex *LookupLoadedProperty(const LoadedPropertyKey &key) const;
    void RecordLoadedProperty(ValueVertex *object, PropertyKey key, ValueVertex *value);
    ValueVertex *LookupLoadedProperty(ValueVertex *object, PropertyKey key) const;
    void RecordLoadedConstantProperty(const LoadedPropertyKey &key, ValueVertex *value);
    ValueVertex *LookupLoadedConstantProperty(const LoadedPropertyKey &key) const;
    void RecordLoadedConstantProperty(ValueVertex *object, PropertyKey key, ValueVertex *value);
    ValueVertex *LookupLoadedConstantProperty(ValueVertex *object, PropertyKey key) const;
    void ClearLoadedProperties();
    void ClearLoadedPropertiesForReceiver(ValueVertex *receiver, LoopEffects *effects = nullptr);
    void ClearLoadedPropertiesForKey(PropertyKey key, LoopEffects *effects = nullptr);

    void RecordEnvSlot(ValueVertex *env, int32_t slot, ValueVertex *value);
    ValueVertex *LookupEnvSlot(ValueVertex *env, int32_t slot) const;
    void RecordEnvConstant(ValueVertex *env, int32_t slot, ValueVertex *value);
    ValueVertex *LookupEnvConstant(ValueVertex *env, int32_t slot) const;
    ClearedEnvSlotKeys ClearAliasedEnvSlotsFor(ValueVertex *env, int32_t slot, ValueVertex *newValue);
    void ClearEnvSlotsFor(ValueVertex *env);

    CseAvailableExpressions &GetCse()
    {
        return cse_;
    }

    const CseAvailableExpressions &GetCse() const
    {
        return cse_;
    }

    void MarkPossibleSideEffect(const SideEffectDescriptor &effect, LoopEffects *loopEffects = nullptr);
    void MarkHClassesStaleForElementsKindTransition(const NodeInfo::PossibleHClasses &sourceHClasses);
    void ClearAll();
    void OnSideEffect();
    void IncrementEffectEpoch();

    EnvSlotAliasMode GetEnvSlotAliasMode() const
    {
        return envSlotAliasMode_;
    }

    void SetEnvSlotAliasMode(EnvSlotAliasMode mode)
    {
        envSlotAliasMode_ = mode;
    }

    uint32_t GetEffectEpoch() const
    {
        return effectEpoch_;
    }

    bool Empty() const
    {
        return nodeInfos_.Empty() && loadedProperties_.Empty() && loadedConstantProperties_.Empty() &&
               loadedEnvSlots_.Empty() && loadedEnvConstants_.Empty() && cse_.Empty();
    }

private:
    static constexpr uint32_t EFFECT_EPOCH_OVERFLOW = CSE_EPOCH_OVERFLOW;

    using NodeInfos = FactsHamt<ValueVertex *, const StoredNodeInfo *, FactsHamtHash<ValueVertex *>,
                                std::equal_to<ValueVertex *>, NodeInfoHamtTraits>;
    using LoadedPropertyMap =
        FactsHamt<LoadedPropertyKey, ValueVertex *, LoadedPropertyKeyHash, LoadedPropertyKeyEqual>;
    using LoadedEnvSlots = FactsHamt<EnvSlotKey, ValueVertex *, EnvSlotKeyHash, EnvSlotKeyEqual>;

    // A guarded update may reuse its lookup only while the facts and generation are unchanged.
    template <class Callback>
    void UpdateInfoFromView(ValueVertex *node, NodeInfoView existing, Callback &&callback)
    {
        ASSERT(node != nullptr);
        NodeInfo info = existing == nullptr ? NodeInfo(chunk_) : existing.Materialize();
        callback(info);
        if (existing != nullptr && existing.IsSameAs(info)) {
            return;
        }
        StoreNodeInfo(node, info);
        mayHaveFreshUnstableHClasses_ |= info.HasFreshPossibleHClasses() && info.HasUnstablePossibleHClass();
    }

    void UpdateInfoForImpl(ValueVertex *node, void *context, void (*callback)(void *, NodeInfo &));

    void StoreNodeInfo(ValueVertex *node, const NodeInfo &info)
    {
        nodeInfos_.InsertOrAssign(node, chunk_->New<StoredNodeInfo>(chunk_, info, hclassGeneration_));
    }
    void AdvanceEpochAfterMerge(uint32_t otherEpoch);
    ValueVertex *LookupLoadedProperty(const LoadedPropertyMap &map, const LoadedPropertyKey &key) const;
    void RecordLoadedProperty(LoadedPropertyMap &map, const LoadedPropertyKey &key, ValueVertex *value);
    void MergeLoadedProperties(LoadedPropertyMap &target, const LoadedPropertyMap &other);
    void MergeEnvSlots(LoadedEnvSlots &target, const LoadedEnvSlots &other);
    void UpdateEnvSlotAliasMode(ValueVertex *env);
    void RecomputeEnvSlotAliasMode();
    void MarkAllFreshUnstableHClassesStale();
    void MarkHClassesStaleForTransition(ValueVertex *receiver);
    bool EnvMayAlias(ValueVertex *lhs, ValueVertex *rhs) const;

    static EnvSlotAliasMode MergeEnvSlotAliasMode(EnvSlotAliasMode lhs, EnvSlotAliasMode rhs);

    Chunk *chunk_;
    NodeInfos nodeInfos_;
    LoadedPropertyMap loadedProperties_;
    LoadedPropertyMap loadedConstantProperties_;
    LoadedEnvSlots loadedEnvSlots_;
    LoadedEnvSlots loadedEnvConstants_;
    CseAvailableExpressions cse_;
    uint32_t effectEpoch_ {0};
    EnvSlotAliasMode envSlotAliasMode_ {EnvSlotAliasMode::NONE};
    const HClassGeneration *hclassGeneration_ {nullptr};
    // Conservative: true can outlive individual entries; false guarantees none are fresh.
    bool mayHaveFreshUnstableHClasses_ {false};
};

}  // namespace panda::ecmascript::arksteed

#endif  // ECMASCRIPT_ARKSTEED_COMPILE_INFO_FACTS_H
