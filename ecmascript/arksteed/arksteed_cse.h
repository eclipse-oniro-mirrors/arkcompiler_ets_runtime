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

#ifndef ECMASCRIPT_ARKSTEED_CSE_H
#define ECMASCRIPT_ARKSTEED_CSE_H

#include <algorithm>
#include <array>
#include <initializer_list>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "ecmascript/arksteed/arksteed_cse_types.h"
#include "ecmascript/arksteed/arksteed_opcode.h"

namespace panda::ecmascript::arksteed {

constexpr uint64_t CSE_FNV_OFFSET_BASIS = 14695981039346656037ULL;
constexpr uint64_t CSE_FNV_PRIME = 1099511628211ULL;

constexpr uint64_t CseHashCombine(uint64_t hash, uint64_t value)
{
    return (hash ^ value) * CSE_FNV_PRIME;
}

uint64_t CseHashInputs(VertexOpcode opcode, CseInputView inputs);
bool ShouldSwapCseOperands(ValueVertex *left, ValueVertex *right);

// Keep the dynamic hash's exact mixing order while exposing fixed arity to the compiler.
template <size_t N>
uint64_t CseHashInputs(VertexOpcode opcode, CseInputView inputs)
{
    ASSERT(inputs.count == N);
    uint64_t hash = CseHashCombine(CSE_FNV_OFFSET_BASIS, static_cast<uint64_t>(opcode));
    hash = CseHashCombine(hash, N);
    for (size_t index = 0; index < N; ++index) {
        hash = CseHashCombine(hash, reinterpret_cast<uintptr_t>(inputs[index]));
    }
    return hash;
}

inline CseInputView MakeCseInputView(const std::initializer_list<ValueVertex *> &inputs)
{
    return {inputs.begin(), inputs.size()};
}

template <class InputRange>
CseInputView MakeCseInputView(const InputRange &inputs)
{
    return {inputs.data(), inputs.size()};
}

constexpr bool CseIsExcludedOpcode(VertexOpcode opcode)
{
    switch (opcode) {
        case VertexOpcode::Int32Constant:
        case VertexOpcode::Int64Constant:
        case VertexOpcode::Float64Constant:
        case VertexOpcode::TaggedConstant:
        case VertexOpcode::HeapConstant:
        case VertexOpcode::InitialValue:
        case VertexOpcode::ActualArgc:
        case VertexOpcode::Call:
        case VertexOpcode::NumberToString:
        case VertexOpcode::CallRuntime:
        case VertexOpcode::CallCommonStub:
        case VertexOpcode::Deopt:
        case VertexOpcode::Phi:
            return true;
        default:
            return false;
    }
}

template <class VertexT>
constexpr CsePolicy CseDefaultPolicy()
{
    if constexpr (!std::is_base_of_v<ValueVertex, VertexT>) {
        return CsePolicy::NONE;
    } else {
        return !CseIsExcludedOpcode(OpcodeOf<VertexT>) && CanParticipateInCSE(VertexT::PROPERTIES) ? CsePolicy::VALUE
                                                                                                   : CsePolicy::NONE;
    }
}

template <class VertexT>
struct CsePolicyFor {
    static constexpr CsePolicy VALUE = CseDefaultPolicy<VertexT>();
};

#define ARKSTEED_CHECKED_ARITHMETIC_CSE(VertexT) \
    template <>                                   \
    struct CsePolicyFor<VertexT> {               \
        static constexpr CsePolicy VALUE = CsePolicy::VALUE; \
    };

ARKSTEED_CHECKED_ARITHMETIC_CSE(I32AddWithOverflowVertex)
ARKSTEED_CHECKED_ARITHMETIC_CSE(I32SubWithOverflowVertex)
ARKSTEED_CHECKED_ARITHMETIC_CSE(I32MulWithOverflowVertex)
ARKSTEED_CHECKED_ARITHMETIC_CSE(I32DivWithOverflowVertex)
ARKSTEED_CHECKED_ARITHMETIC_CSE(CheckedI32ModVertex)
ARKSTEED_CHECKED_ARITHMETIC_CSE(I32DivByConstWithCheckVertex)
ARKSTEED_CHECKED_ARITHMETIC_CSE(I32IncWithOverflowVertex)
ARKSTEED_CHECKED_ARITHMETIC_CSE(I32DecWithOverflowVertex)
ARKSTEED_CHECKED_ARITHMETIC_CSE(I32NegWithOverflowVertex)
#undef ARKSTEED_CHECKED_ARITHMETIC_CSE

#define ARKSTEED_SUCCESS_CHECK_CSE(VertexT)                  \
    template <>                                              \
    struct CsePolicyFor<VertexT> {                          \
        static constexpr CsePolicy VALUE = CsePolicy::SUCCESS_CHECK; \
    };

ARKSTEED_SUCCESS_CHECK_CSE(DeoptIfInt32ConditionVertex)
ARKSTEED_SUCCESS_CHECK_CSE(DeoptIfFloat64ConditionVertex)
ARKSTEED_SUCCESS_CHECK_CSE(DeoptIfTaggedConditionVertex)
ARKSTEED_SUCCESS_CHECK_CSE(DeoptIfReferenceNotEqualVertex)
ARKSTEED_SUCCESS_CHECK_CSE(DeoptIfNotNumberVertex)
ARKSTEED_SUCCESS_CHECK_CSE(DeoptIfNotBooleanVertex)
ARKSTEED_SUCCESS_CHECK_CSE(DeoptIfNotBigIntVertex)
ARKSTEED_SUCCESS_CHECK_CSE(DeoptIfNotUndefinedOrNullVertex)
ARKSTEED_SUCCESS_CHECK_CSE(DeoptIfNotHeapObjectVertex)
ARKSTEED_SUCCESS_CHECK_CSE(DeoptIfHClassMismatchVertex)
ARKSTEED_SUCCESS_CHECK_CSE(DeoptIfHClassNotInVertex)
ARKSTEED_SUCCESS_CHECK_CSE(DeoptIfPrototypeChangedVertex)
ARKSTEED_SUCCESS_CHECK_CSE(DeoptIfArrayBufferDetachedVertex)
ARKSTEED_SUCCESS_CHECK_CSE(DeoptIfCOWElementsVertex)
ARKSTEED_SUCCESS_CHECK_CSE(DeoptIfElementsUnstableVertex)
#undef ARKSTEED_SUCCESS_CHECK_CSE

#define ARKSTEED_CHECKED_VALUE_CSE(VertexT)               \
    template <>                                           \
    struct CsePolicyFor<VertexT> {                       \
        static constexpr CsePolicy VALUE = CsePolicy::VALUE; \
    };

ARKSTEED_CHECKED_VALUE_CSE(CheckedTaggedIntToI32Vertex)
ARKSTEED_CHECKED_VALUE_CSE(CheckedNumberToF64Vertex)
ARKSTEED_CHECKED_VALUE_CSE(CheckedTaggedStringVertex)
ARKSTEED_CHECKED_VALUE_CSE(CheckedNonNegativeI32ToTaggedIntVertex)
ARKSTEED_CHECKED_VALUE_CSE(TaggedNumberToBooleanVertex)
ARKSTEED_CHECKED_VALUE_CSE(TaggedDoubleToBooleanVertex)
#undef ARKSTEED_CHECKED_VALUE_CSE

template <class VertexT>
struct CseSemanticKey {
    using Key = std::tuple<>;

    template <class... Args>
    static Key MakeKey(Args &&...)
    {
        static_assert(sizeof...(Args) == 0, "CSE node with constructor parameters needs a semantic key");
        return {};
    }

    static Key GetKey(const VertexT &)
    {
        return {};
    }
};

#define ARKSTEED_CSE_KEY_1(VertexT, KeyType, Getter)       \
    template <>                                             \
    struct CseSemanticKey<VertexT> {                        \
        using Key = std::tuple<KeyType>;                    \
        static Key MakeKey(KeyType value) { return {value}; } \
        static Key GetKey(const VertexT &node) { return {node.Getter()}; } \
    };

ARKSTEED_CSE_KEY_1(LoadGlueTaggedFieldVertex, int32_t, GetOffset)
ARKSTEED_CSE_KEY_1(LoadGlueI32FieldVertex, int32_t, GetOffset)
ARKSTEED_CSE_KEY_1(LoadTaggedFromAddressVertex, int32_t, GetOffset)
ARKSTEED_CSE_KEY_1(LoadI32FromAddressVertex, int32_t, GetOffset)
ARKSTEED_CSE_KEY_1(LoadI64FromAddressVertex, int32_t, GetOffset)
ARKSTEED_CSE_KEY_1(LoadF64FromAddressVertex, int32_t, GetOffset)
ARKSTEED_CSE_KEY_1(LoadTaggedFieldVertex, int32_t, GetOffset)
ARKSTEED_CSE_KEY_1(LoadInt32FieldVertex, int32_t, GetOffset)
ARKSTEED_CSE_KEY_1(I32ConditionCheckVertex, Condition, GetCondition)
ARKSTEED_CSE_KEY_1(I64ConditionCheckVertex, Condition, GetCondition)
ARKSTEED_CSE_KEY_1(F64ConditionCheckVertex, Condition, GetCondition)
ARKSTEED_CSE_KEY_1(Float64ToBooleanVertex, bool, IsTrueIfNonZero)
ARKSTEED_CSE_KEY_1(NumberToBooleanVertex, bool, IsTrueIfNonZero)
ARKSTEED_CSE_KEY_1(DoubleToBooleanVertex, bool, IsTrueIfNonZero)
ARKSTEED_CSE_KEY_1(StringToBooleanVertex, bool, IsTrueIfNonZero)
ARKSTEED_CSE_KEY_1(I32BitwiseBinaryVertex, IntBitwiseKind, GetKind)
ARKSTEED_CSE_KEY_1(I64BitwiseBinaryVertex, IntBitwiseKind, GetKind)
ARKSTEED_CSE_KEY_1(I64ShiftLeftVertex, uint32_t, GetShift)
ARKSTEED_CSE_KEY_1(I64ShiftRightLogicalVertex, uint32_t, GetShift)
ARKSTEED_CSE_KEY_1(DeoptIfReferenceNotEqualVertex, kungfu::DeoptType, GetDeoptType)
ARKSTEED_CSE_KEY_1(DeoptIfHClassMismatchVertex, uint32_t, GetExpectedHClassHandleIndex)
ARKSTEED_CSE_KEY_1(DeoptIfArrayBufferDetachedVertex, OnHeapMode, GetOnHeapMode)
ARKSTEED_CSE_KEY_1(TaggedNumberToBooleanVertex, bool, IsTrueIfNonZero)
ARKSTEED_CSE_KEY_1(TaggedDoubleToBooleanVertex, bool, IsTrueIfNonZero)
#undef ARKSTEED_CSE_KEY_1

#define ARKSTEED_CSE_CONDITION_KEY(VertexT)                                    \
    template <>                                                                \
    struct CseSemanticKey<VertexT> {                                           \
        using Key = std::tuple<Condition, kungfu::DeoptType>;                   \
        static Key MakeKey(Condition condition, kungfu::DeoptType deoptType)   \
        {                                                                       \
            return {condition, deoptType};                                     \
        }                                                                       \
        static Key GetKey(const VertexT &node)                                  \
        {                                                                       \
            return {node.GetCondition(), node.GetDeoptType()};                  \
        }                                                                       \
    };

ARKSTEED_CSE_CONDITION_KEY(DeoptIfInt32ConditionVertex)
ARKSTEED_CSE_CONDITION_KEY(DeoptIfFloat64ConditionVertex)
ARKSTEED_CSE_CONDITION_KEY(DeoptIfTaggedConditionVertex)
#undef ARKSTEED_CSE_CONDITION_KEY

template <>
struct CseSemanticKey<DeoptIfPrototypeChangedVertex> {
    using Key = std::tuple<bool, bool>;
    static Key MakeKey(bool checkProtoChangeMarker, bool checkNotPrototype)
    {
        return {checkProtoChangeMarker, checkNotPrototype};
    }
    static Key GetKey(const DeoptIfPrototypeChangedVertex &node)
    {
        return {node.ShouldCheckProtoChangeMarker(), node.ShouldCheckNotPrototype()};
    }
};

template <>
struct CseSemanticKey<DeoptIfHClassNotInVertex> {
    using Key = std::tuple<CseUInt32View>;
    static Key MakeKey(const std::vector<uint32_t> &indices)
    {
        return {CseUInt32View {indices.data(), indices.size()}};
    }
    static Key GetKey(const DeoptIfHClassNotInVertex &node)
    {
        const auto &indices = node.GetExpectedHClassHandleIndices();
        return {CseUInt32View {indices.data(), indices.size()}};
    }
};

template <>
struct CseSemanticKey<TypedArrayIntLoadElementVertex> {
    using Key = std::tuple<JSType, bool>;
    static Key MakeKey(JSType elementType, bool isOnHeap)
    {
        return {elementType, isOnHeap};
    }
    static Key GetKey(const TypedArrayIntLoadElementVertex &node)
    {
        return {node.GetElementType(), node.IsOnHeap()};
    }
};

template <>
struct CseSemanticKey<TypedArrayDoubleLoadElementVertex> {
    using Key = std::tuple<JSType, bool>;
    static Key MakeKey(JSType elementType, bool isOnHeap)
    {
        return {elementType, isOnHeap};
    }
    static Key GetKey(const TypedArrayDoubleLoadElementVertex &node)
    {
        return {node.GetElementType(), node.IsOnHeap()};
    }
};

template <>
struct CseSemanticKey<I32DivByConstWithCheckVertex> {
    using Key = std::tuple<int32_t, int32_t, uint32_t>;
    static Key MakeKey(int32_t divisor, int32_t magic, uint32_t shift)
    {
        return {divisor, magic, shift};
    }
    static Key GetKey(const I32DivByConstWithCheckVertex &node)
    {
        return {node.GetDivisor(), node.GetMagic(), node.GetShift()};
    }
};

template <class VertexT>
struct CseCommutativity {
    static constexpr bool ALWAYS =
        std::is_same_v<VertexT, I32AddVertex> || std::is_same_v<VertexT, I32MulVertex> ||
        std::is_same_v<VertexT, I32AddWithOverflowVertex> || std::is_same_v<VertexT, I32MulWithOverflowVertex> ||
        std::is_same_v<VertexT, F64AddVertex> || std::is_same_v<VertexT, F64MulVertex> ||
        std::is_same_v<VertexT, TaggedEqualVertex> || std::is_same_v<VertexT, TaggedNotEqualVertex> ||
        std::is_same_v<VertexT, DeoptIfReferenceNotEqualVertex>;
    static constexpr bool BITWISE =
        std::is_same_v<VertexT, I32BitwiseBinaryVertex> || std::is_same_v<VertexT, I64BitwiseBinaryVertex>;
    static constexpr bool CONDITION =
        std::is_same_v<VertexT, I32ConditionCheckVertex> || std::is_same_v<VertexT, I64ConditionCheckVertex> ||
        std::is_same_v<VertexT, F64ConditionCheckVertex> ||
        std::is_same_v<VertexT, DeoptIfInt32ConditionVertex> ||
        std::is_same_v<VertexT, DeoptIfFloat64ConditionVertex> || std::is_same_v<VertexT, DeoptIfTaggedConditionVertex>;
    static constexpr bool HAS_COMMUTATIVE_INPUTS = ALWAYS || BITWISE || CONDITION;

    static std::optional<CommutativeInputs> GetCommutativeInputs(
        [[maybe_unused]] const typename CseSemanticKey<VertexT>::Key &key)
    {
        if constexpr (ALWAYS) {
            return CommutativeInputs {VertexT::LEFT_INDEX, VertexT::RIGHT_INDEX};
        } else if constexpr (BITWISE) {
            IntBitwiseKind kind = std::get<0>(key);
            if (kind == IntBitwiseKind::BITWISE_AND || kind == IntBitwiseKind::BITWISE_OR ||
                kind == IntBitwiseKind::BITWISE_XOR) {
                return CommutativeInputs {VertexT::LEFT_INDEX, VertexT::RIGHT_INDEX};
            }
            return std::nullopt;
        } else if constexpr (CONDITION) {
            Condition condition = std::get<0>(key);
            if (condition == Condition::EQUAL || condition == Condition::NOT_EQUAL) {
                return CommutativeInputs {VertexT::LEFT_INDEX, VertexT::RIGHT_INDEX};
            }
            return std::nullopt;
        } else {
            return std::nullopt;
        }
    }
};

template <class VertexT>
struct CseTraits : CseSemanticKey<VertexT>, CseCommutativity<VertexT> {
    static constexpr CsePolicy POLICY = CsePolicyFor<VertexT>::VALUE;
    static constexpr bool NEEDS_EPOCH_CHECK = CanRead(VertexT::PROPERTIES);
};

template <class VertexT>
std::optional<CommutativeInputs> GetCommutativeInputs(const typename CseTraits<VertexT>::Key &key)
{
    return CseTraits<VertexT>::GetCommutativeInputs(key);
}

template <class VertexT, size_t N>
void NormalizeCseQueryInputs(std::array<ValueVertex *, N> &inputs, const typename CseTraits<VertexT>::Key &key)
{
    static_assert(CseTraits<VertexT>::HAS_COMMUTATIVE_INPUTS);
    static_assert(VertexT::LEFT_INDEX < N && VertexT::RIGHT_INDEX < N);
    auto indices = GetCommutativeInputs<VertexT>(key);
    if (indices && ShouldSwapCseOperands(inputs[indices->left], inputs[indices->right])) {
        std::swap(inputs[indices->left], inputs[indices->right]);
    }
}

template <class VertexT>
struct CseQuery {
    CseInputView inputs;
    typename CseTraits<VertexT>::Key key;
};

template <class VertexT, class InputRange, class... Args>
CseQuery<VertexT> MakeCseQuery(const InputRange &inputs, Args &&...args)
{
    return {MakeCseInputView(inputs), CseTraits<VertexT>::MakeKey(std::forward<Args>(args)...)};
}

template <class T>
uint64_t CseHashScalar(T value)
{
    if constexpr (std::is_enum_v<T>) {
        return static_cast<uint64_t>(value);
    } else if constexpr (std::is_integral_v<T>) {
        return static_cast<uint64_t>(value);
    } else if constexpr (std::is_pointer_v<T>) {
        return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(value));
    } else {
        static_assert(std::is_integral_v<T> || std::is_enum_v<T> || std::is_pointer_v<T>,
                      "Add a hash rule for this CSE key field");
    }
}

inline uint64_t CseHashScalar(CseUInt32View values)
{
    uint64_t hash = values.count;
    for (size_t index = 0; index < values.count; ++index) {
        hash = CseHashCombine(hash, values.data[index]);
    }
    return hash;
}

template <class VertexT>
uint64_t CseHashQuery(const CseQuery<VertexT> &query)
{
    uint64_t hash = 0;
    if constexpr (CseTraits<VertexT>::HAS_COMMUTATIVE_INPUTS) {
        ASSERT(query.inputs.count == VertexT::NUM_INPUTS);
        // Canonicalize the lookup identity while preserving the node's original input order.
        std::array<ValueVertex *, VertexT::NUM_INPUTS> normalizedInputs {};
        std::copy_n(query.inputs.begin(), VertexT::NUM_INPUTS, normalizedInputs.begin());
        NormalizeCseQueryInputs<VertexT>(normalizedInputs, query.key);
        hash = CseHashInputs<VertexT::NUM_INPUTS>(OpcodeOf<VertexT>, MakeCseInputView(normalizedInputs));
    } else if constexpr (HasFixedInputCount<VertexT>::value) {
        hash = CseHashInputs<VertexT::NUM_INPUTS>(OpcodeOf<VertexT>, query.inputs);
    } else {
        hash = CseHashInputs(OpcodeOf<VertexT>, query.inputs);
    }
    std::apply([&hash](const auto &...values) { ((hash = CseHashCombine(hash, CseHashScalar(values))), ...); },
               query.key);
    return hash;
}

template <class VertexT>
VertexT *CseMatchNode(NonControlVertex *candidate, const CseQuery<VertexT> &query)
{
    VertexT *node = candidate->TryCast<VertexT>();
    if (node == nullptr || node->GetInputCount() != query.inputs.count ||
        CseTraits<VertexT>::GetKey(*node) != query.key) {
        return nullptr;
    }
    auto indices = GetCommutativeInputs<VertexT>(query.key);
    for (size_t index = 0; index < query.inputs.count; ++index) {
        if (indices && (index == indices->left || index == indices->right)) {
            continue;
        }
        if (node->GetInput(index) != query.inputs[index]) {
            return nullptr;
        }
    }
    if (indices) {
        auto *left = node->GetInput(indices->left);
        auto *right = node->GetInput(indices->right);
        bool sameOrder = left == query.inputs[indices->left] && right == query.inputs[indices->right];
        bool reverseOrder = left == query.inputs[indices->right] && right == query.inputs[indices->left];
        if (!sameOrder && !reverseOrder) {
            return nullptr;
        }
    }
    return node;
}

template <class VertexT>
VertexT *CseAvailableExpressions::Find(const CseQuery<VertexT> &query, uint64_t hash, uint32_t effectEpoch) const
{
    static_assert(CseTraits<VertexT>::POLICY != CsePolicy::NONE);
    const auto &buckets = CseTraits<VertexT>::NEEDS_EPOCH_CHECK ? memoryBuckets_ : pureBuckets_;
    for (const ExpressionLink *link = buckets.Find(hash); link != nullptr; link = link->next) {
        const AvailableExpressionRecord &record = *link->record;
        if (CseTraits<VertexT>::NEEDS_EPOCH_CHECK && effectEpoch > record.effectEpoch) {
            continue;
        }
        if (VertexT *node = CseMatchNode(record.node, query)) {
            return node;
        }
    }
    return nullptr;
}

template <class VertexT>
void CseAvailableExpressions::AddExpression(const CseQuery<VertexT> &query, VertexT *node, uint64_t hash,
                                            uint32_t effectEpoch)
{
    static_assert(CseTraits<VertexT>::POLICY != CsePolicy::NONE);
    ASSERT(node != nullptr && node->GetInputCount() == query.inputs.count);
    ASSERT(CseTraits<VertexT>::GetKey(*node) == query.key);
    ASSERT(CseMatchNode(node, query) == node);
    uint32_t recordEpoch = CseTraits<VertexT>::NEEDS_EPOCH_CHECK ? effectEpoch : CSE_PURE_EPOCH;
    if (recordEpoch == CSE_EPOCH_OVERFLOW) {
        return;
    }
    AddRecord(node, hash, recordEpoch);
}

}  // namespace panda::ecmascript::arksteed

#endif  // ECMASCRIPT_ARKSTEED_CSE_H
