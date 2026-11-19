/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef ECMASCRIPT_ARKSTEED_ARKSTEED_NODE_TYPE_H
#define ECMASCRIPT_ARKSTEED_ARKSTEED_NODE_TYPE_H

#include <cstdint>

namespace panda::ecmascript::arksteed {

enum class NodeType : uint16_t {
    NONE = 0,
    INT = 1U << 0U,
    DOUBLE = 1U << 1U,
    NULL_TYPE = 1U << 2U,
    UNDEFINED = 1U << 3U,
    BOOLEAN = 1U << 4U,
    STRING = 1U << 5U,
    SYMBOL = 1U << 6U,
    BIGINT = 1U << 7U,
    JS_ARRAY = 1U << 8U,
    JS_FUNCTION = 1U << 9U,
    JS_TYPED_ARRAY = 1U << 10U,
    OTHER_JS_RECEIVER = 1U << 11U,

    NUMBER = static_cast<uint16_t>(INT) | static_cast<uint16_t>(DOUBLE),
    NULL_OR_UNDEFINED = static_cast<uint16_t>(NULL_TYPE) | static_cast<uint16_t>(UNDEFINED),
    ODDBALL = static_cast<uint16_t>(NULL_TYPE) | static_cast<uint16_t>(UNDEFINED) | static_cast<uint16_t>(BOOLEAN),
    JS_RECEIVER = static_cast<uint16_t>(JS_ARRAY) | static_cast<uint16_t>(JS_FUNCTION) |
                  static_cast<uint16_t>(JS_TYPED_ARRAY) | static_cast<uint16_t>(OTHER_JS_RECEIVER),
    ANY_HEAP_OBJECT = static_cast<uint16_t>(STRING) | static_cast<uint16_t>(SYMBOL) |
                      static_cast<uint16_t>(BIGINT) | static_cast<uint16_t>(JS_RECEIVER),
    UNKNOWN = static_cast<uint16_t>(INT) | static_cast<uint16_t>(DOUBLE) | static_cast<uint16_t>(NULL_TYPE) |
              static_cast<uint16_t>(UNDEFINED) | static_cast<uint16_t>(BOOLEAN) | static_cast<uint16_t>(STRING) |
              static_cast<uint16_t>(SYMBOL) | static_cast<uint16_t>(BIGINT) | static_cast<uint16_t>(JS_ARRAY) |
              static_cast<uint16_t>(JS_FUNCTION) | static_cast<uint16_t>(JS_TYPED_ARRAY) |
              static_cast<uint16_t>(OTHER_JS_RECEIVER),
};

constexpr NodeType UnionNodeType(NodeType lhs, NodeType rhs)
{
    return static_cast<NodeType>(static_cast<uint16_t>(lhs) | static_cast<uint16_t>(rhs));
}

}  // namespace panda::ecmascript::arksteed

#endif  // ECMASCRIPT_ARKSTEED_ARKSTEED_NODE_TYPE_H
