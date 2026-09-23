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

#ifndef ECMASCRIPT_ARKSTEED_HEAP_REF_H
#define ECMASCRIPT_ARKSTEED_HEAP_REF_H

#include "ecmascript/js_handle.h"
#include "ecmascript/js_tagged_value_wrapper.h"

namespace panda::ecmascript {
class JitCompilationEnv;
}

namespace panda::ecmascript::arksteed {

class ArkSteedHeapRef {
public:
    ArkSteedHeapRef() = default;

    // Heap refs can only be created by the broker, after rooting and canonicalization.
    static ArkSteedHeapRef FromImmediate(JSTaggedValue value)
    {
        if (value.IsHeapObject()) {
            return {};
        }
        ArkSteedHeapRef ref;
        ref.value_ = value;
        ref.kind_ = Kind::IMMEDIATE;
        return ref;
    }

    ArkSteedHeapRef &operator=(JSTaggedValue value) = delete;

    JSTaggedValue Value() const
    {
        ASSERT(IsImmediate());
        return value_;
    }

    bool HasHandle() const
    {
        return IsHeapObject();
    }

    // These predicates inspect serialized metadata, never a handle or the heap.
    bool IsImmediate() const { return kind_ == Kind::IMMEDIATE; }
    bool IsHeapObject() const { return kind_ >= Kind::HEAP_OBJECT; }
    bool IsUndefined() const { return IsImmediate() && value_.IsUndefined(); }
    bool IsInt() const { return IsImmediate() && value_.IsInt(); }
    bool IsJSHClass() const { return kind_ == Kind::HCLASS; }
    bool IsJSFunction() const { return kind_ == Kind::FUNCTION; }
    bool IsString() const { return kind_ == Kind::STRING; }
    bool IsSymbol() const { return kind_ == Kind::SYMBOL; }
    bool IsName() const { return IsString() || IsSymbol(); }
    bool IsProtoCell() const { return kind_ == Kind::PROTO_CELL; }

    bool IsStable() const
    {
        return IsSafeForCompile();
    }

    bool IsSafeForCompile() const
    {
        return kind_ != Kind::INVALID;
    }

    // Object identity, not JavaScript value equality. Indices are local to a compilation.
    // Invalid refs are not identities; even two invalid refs do not compare equal.
    bool Equals(const ArkSteedHeapRef &other) const
    {
        if (!IsSafeForCompile() || !other.IsSafeForCompile()) {
            return false;
        }
        if (IsImmediate() || other.IsImmediate()) {
            return IsImmediate() && other.IsImmediate() && value_ == other.value_;
        }
        return owner_ == other.owner_ && index_ == other.index_;
    }

    bool IsSafeToEmbed() const = delete;

    uint64_t GetLargeUInt() const = delete;

    bool operator==(const ArkSteedHeapRef &other) const = delete;

    bool operator!=(const ArkSteedHeapRef &other) const = delete;

    operator JSTaggedValue() const = delete;

private:
    enum class Kind : uint8_t {
        INVALID,
        IMMEDIATE,
        HEAP_OBJECT,
        HCLASS,
        FUNCTION,
        STRING,
        SYMBOL,
        PROTO_CELL,
    };

    ArkSteedHeapRef(const JitCompilationEnv *owner, uint32_t index, Kind kind)
        : owner_(owner), index_(index), kind_(kind)
    {}

    JSTaggedValue value_ {JSTaggedValue::Undefined()};
    const JitCompilationEnv *owner_ {nullptr};
    uint32_t index_ {0};
    Kind kind_ {Kind::INVALID};

    friend class ArkSteedHeapBroker;
};

// Role names share a representation; broker factories validate their types.
using ArkSteedObjectRef = ArkSteedHeapRef;
using ArkSteedFunctionRef = ArkSteedHeapRef;
using ArkSteedHClassRef = ArkSteedHeapRef;
using ArkSteedProtoCellRef = ArkSteedHeapRef;
using ArkSteedHandlerRef = ArkSteedHeapRef;
using ArkSteedNameRef = ArkSteedHeapRef;

}  // namespace panda::ecmascript::arksteed

#endif  // ECMASCRIPT_ARKSTEED_HEAP_REF_H
