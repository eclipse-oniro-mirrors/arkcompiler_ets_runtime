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

#ifndef ECMASCRIPT_ARKSTEED_CONDITION_CODE_H
#define ECMASCRIPT_ARKSTEED_CONDITION_CODE_H

#include <cstdint>
#include <type_traits>
#include "libpandabase/macros.h"

namespace panda::ecmascript::arksteed {

// Note: The "OVERFLOWS" spelling is intentional since "OVERFLOW" is a macro defined by Both MinGW and Apple SDK.
#define ARKSTEED_CC_LIST(V)             \
    V(EQUAL, NOT_EQUAL)                 \
    V(LESS_THAN, GREATER_THAN_OR_EQUAL) \
    V(GREATER_THAN, LESS_THAN_OR_EQUAL) \
    V(BELOW, ABOVE_OR_EQUAL)            \
    V(ABOVE, BELOW_OR_EQUAL)            \
    V(ZERO, NOT_ZERO)                   \
    V(OVERFLOWS, NOT_OVERFLOW)          \
    V(PARITY, NOT_PARITY)

#define ARKSTEED_JS_CC_LIST(V)          \
    V(EQUAL, NOT_EQUAL)                 \
    V(STRICT_EQUAL, STRICT_NOT_EQUAL)   \
    V(LESS_THAN, GREATER_THAN_OR_EQUAL) \
    V(GREATER_THAN, LESS_THAN_OR_EQUAL)

#define DEFINE_CC(C, N) C, N,

enum class Condition : uint8_t {
    ARKSTEED_CC_LIST(DEFINE_CC)
};

// JS-level comparison operators.
enum class JSCondition : uint8_t {
    ARKSTEED_JS_CC_LIST(DEFINE_CC)
};

#undef DEFINE_CC

constexpr bool IsEqualityCompare(JSCondition kind)
{
    return kind == JSCondition::EQUAL ||
           kind == JSCondition::NOT_EQUAL ||
           kind == JSCondition::STRICT_EQUAL ||
           kind == JSCondition::STRICT_NOT_EQUAL;
}

#define ARKSTEED_JS_CC_EQ_MATRIX(V)                 \
    V(Strict,   STRICT_EQUAL, STRICT_NOT_EQUAL)     \
    V(Loose,    EQUAL, NOT_EQUAL)                   \
    V(Positive, STRICT_EQUAL, EQUAL)                \
    V(Negative, STRICT_NOT_EQUAL, NOT_EQUAL)

#define DEFINE_JS_CC_EQ_PREDICATE(Name, C1, C2)                 \
    constexpr bool Is##Name##EqualityCompare(JSCondition cc)    \
    {                                                           \
        return cc == JSCondition::C1 || cc == JSCondition::C2;  \
    }
ARKSTEED_JS_CC_EQ_MATRIX(DEFINE_JS_CC_EQ_PREDICATE)
#undef DEFINE_JS_CC_EQ_PREDICATE

template <class T, class = std::enable_if_t<std::is_arithmetic_v<T>>>
constexpr bool EvaluateCompare(JSCondition cc, T left, T right)
{
    switch (cc) {
        case JSCondition::EQUAL:
        case JSCondition::STRICT_EQUAL:
            return left == right;
        case JSCondition::NOT_EQUAL:
        case JSCondition::STRICT_NOT_EQUAL:
            return left != right;
        case JSCondition::LESS_THAN:
            return left < right;
        case JSCondition::LESS_THAN_OR_EQUAL:
            return left <= right;
        case JSCondition::GREATER_THAN:
            return left > right;
        case JSCondition::GREATER_THAN_OR_EQUAL:
            return left >= right;
        default:
            UNREACHABLE();
    }
}

// Note: Works for primitive type comparison only (integer and floating-points)
constexpr Condition ToMachineCondition(JSCondition kind)
{
    switch (kind) {
        case JSCondition::EQUAL:
        case JSCondition::STRICT_EQUAL:
            return Condition::EQUAL;
        case JSCondition::NOT_EQUAL:
        case JSCondition::STRICT_NOT_EQUAL:
            return Condition::NOT_EQUAL;
        case JSCondition::LESS_THAN:
            return Condition::LESS_THAN;
        case JSCondition::LESS_THAN_OR_EQUAL:
            return Condition::LESS_THAN_OR_EQUAL;
        case JSCondition::GREATER_THAN:
            return Condition::GREATER_THAN;
        case JSCondition::GREATER_THAN_OR_EQUAL:
            return Condition::GREATER_THAN_OR_EQUAL;
        default:
            UNREACHABLE();
    }
}

constexpr Condition NegateCondition(Condition cond)
{
    switch (cond) {
#define CASES(C, N)                             \
        case Condition::C: return Condition::N; \
        case Condition::N: return Condition::C;
        ARKSTEED_CC_LIST(CASES)
#undef CASES
        default:
            UNREACHABLE();
    }
}

constexpr JSCondition NegateJSCondition(JSCondition cond)
{
    switch (cond) {
#define CASES(C, N)                                 \
        case JSCondition::C: return JSCondition::N; \
        case JSCondition::N: return JSCondition::C;
        ARKSTEED_JS_CC_LIST(CASES)
#undef CASES
        default:
            UNREACHABLE();
    }
}

constexpr const char *ConditionName(Condition cond)
{
    switch (cond) {
#define CASES(C, N)                     \
        case Condition::C: return #C;   \
        case Condition::N: return #N;
        ARKSTEED_CC_LIST(CASES)
#undef CASES
        default:
            return "<unknown>";
    }
}

constexpr const char *JSConditionName(JSCondition kind)
{
    switch (kind) {
#define CASES(C, N)                     \
        case JSCondition::C: return #C; \
        case JSCondition::N: return #N;
        ARKSTEED_JS_CC_LIST(CASES)
#undef CASES
        default:
            return "<unknown>";
    }
}

}  // namespace panda::ecmascript::arksteed

#endif  // ECMASCRIPT_ARKSTEED_CONDITION_CODE_H
