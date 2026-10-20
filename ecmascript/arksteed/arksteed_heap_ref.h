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

#include "ecmascript/compiler/heap_constant_entry.h"

namespace panda::ecmascript::arksteed {

class ArkSteedHeapRef {
public:
    ArkSteedHeapRef() = default;

    ArkSteedHeapRef &operator=(JSTaggedValue value) = delete;

    JSTaggedValue Value() const
    {
        ASSERT(IsImmediate());
        return JSTaggedValue(data_->payload_);
    }

    bool HasHandle() const { return IsHeapObject(); }

    // These predicates inspect serialized metadata, never a handle or the heap.
    bool IsImmediate() const { return data_ != nullptr && data_->kind_ == Kind::IMMEDIATE; }
    bool IsHeapObject() const { return data_ != nullptr && data_->kind_ >= Kind::HEAP_OBJECT; }
    bool IsUndefined() const { return IsImmediate() && Value().IsUndefined(); }
    bool IsInt() const { return IsImmediate() && Value().IsInt(); }
    bool IsJSHClass() const { return data_ != nullptr && data_->kind_ == Kind::HCLASS; }
    uint8_t GetHClassObjectType() const
    {
        ASSERT(IsJSHClass());
        return data_->hclassObjectType_;
    }
    bool IsJSFunction() const { return data_ != nullptr && data_->kind_ == Kind::FUNCTION; }
    bool IsString() const { return data_ != nullptr && data_->kind_ == Kind::STRING; }
    bool IsSymbol() const { return data_ != nullptr && data_->kind_ == Kind::SYMBOL; }
    bool IsName() const { return IsString() || IsSymbol(); }
    bool IsProtoCell() const { return data_ != nullptr && data_->kind_ == Kind::PROTO_CELL; }

    bool IsStable() const { return IsSafeForCompile(); }

    bool IsSafeForCompile() const { return data_ != nullptr && data_->kind_ != Kind::UNCLASSIFIED; }

    // Object identity, not JavaScript value equality. Entries are local to a compilation.
    // Invalid refs are not identities; even two invalid refs do not compare equal.
    bool Equals(const ArkSteedHeapRef &other) const
    {
        if (!IsSafeForCompile() || !other.IsSafeForCompile()) {
            return false;
        }
        if (IsImmediate() || other.IsImmediate()) {
            return IsImmediate() && other.IsImmediate() && Value() == other.Value();
        }
        return data_ == other.data_;
    }

    bool IsSafeToEmbed() const = delete;

    uint64_t GetLargeUInt() const = delete;

    bool operator==(const ArkSteedHeapRef &other) const = delete;

    bool operator!=(const ArkSteedHeapRef &other) const = delete;

    operator JSTaggedValue() const = delete;

private:
    using Kind = HeapConstantEntry::Kind;

    // The broker roots heap objects and canonicalizes records before publishing refs.
    explicit ArkSteedHeapRef(const HeapConstantEntry *data) : data_(data) {}

    const HeapConstantEntry *data_{nullptr};

    friend class ArkSteedHeapBroker;
};

static_assert(sizeof(ArkSteedHeapRef) == sizeof(void *), "Heap refs must contain only one record pointer");

// Role names share a representation; broker factories validate their types.
using ArkSteedObjectRef = ArkSteedHeapRef;
using ArkSteedFunctionRef = ArkSteedHeapRef;
using ArkSteedHClassRef = ArkSteedHeapRef;
using ArkSteedProtoCellRef = ArkSteedHeapRef;
using ArkSteedHandlerRef = ArkSteedHeapRef;
using ArkSteedNameRef = ArkSteedHeapRef;

}  // namespace panda::ecmascript::arksteed

#endif  // ECMASCRIPT_ARKSTEED_HEAP_REF_H
