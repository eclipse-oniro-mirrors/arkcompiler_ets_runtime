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

#ifndef ECMASCRIPT_COMPILER_HEAP_CONSTANT_ENTRY_H
#define ECMASCRIPT_COMPILER_HEAP_CONSTANT_ENTRY_H

#include <limits>

#include "ecmascript/js_handle.h"
#include "ecmascript/js_tagged_value_wrapper.h"

namespace panda::ecmascript {
class JitCompilationEnv;
namespace arksteed {
class ArkSteedHeapBroker;
class ArkSteedHeapRef;
} // namespace arksteed

// A compilation-owned record. Its address and handle slot stay fixed when the
// constant table grows; moving GC only changes the object in the handle slot.
class HeapConstantEntry {
public:
    enum class Kind : uint8_t {
        UNCLASSIFIED,
        IMMEDIATE,
        HEAP_OBJECT,
        HCLASS,
        FUNCTION,
        STRING,
        SYMBOL,
        PROTO_CELL,
    };

    HeapConstantEntry(const JitCompilationEnv *owner, uint32_t index, JSHandle<JSTaggedValue> handle)
        : payload_(handle.GetAddress()), owner_(owner), index_(index)
    {
    }

    explicit HeapConstantEntry(JSTaggedValue immediate) : payload_(immediate.GetRawData()), kind_(Kind::IMMEDIATE)
    {
        ASSERT(!immediate.IsHeapObject());
    }

    NO_COPY_SEMANTIC(HeapConstantEntry);
    NO_MOVE_SEMANTIC(HeapConstantEntry);

    JSHandle<JSTaggedValue> GetHandle() const
    {
        ASSERT(kind_ != Kind::IMMEDIATE);
        return JSHandle<JSTaggedValue>(static_cast<uintptr_t>(payload_));
    }

private:
    void Classify(Kind kind, uint8_t hclassObjectType)
    {
        ASSERT(kind >= Kind::HEAP_OBJECT);
        ASSERT(kind_ == Kind::UNCLASSIFIED || (kind_ == kind && hclassObjectType_ == hclassObjectType));
        kind_ = kind;
        hclassObjectType_ = hclassObjectType;
    }

    // Heap entries store a handle-slot address; immediate entries store the
    // tagged value itself. Neither reading the metadata nor copying a ref
    // dereferences a handle or accesses the JavaScript heap.
    JSTaggedType payload_;
    const JitCompilationEnv *owner_{nullptr};
    uint32_t index_{std::numeric_limits<uint32_t>::max()};
    Kind kind_{Kind::UNCLASSIFIED};
    uint8_t hclassObjectType_{0};

    friend class arksteed::ArkSteedHeapBroker;
    friend class arksteed::ArkSteedHeapRef;
};
} // namespace panda::ecmascript

#endif // ECMASCRIPT_COMPILER_HEAP_CONSTANT_ENTRY_H
