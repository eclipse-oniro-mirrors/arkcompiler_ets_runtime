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

#ifndef ECMASCRIPT_UTILS_COLLECTIONS_ATOMIC_HELPER_H
#define ECMASCRIPT_UTILS_COLLECTIONS_ATOMIC_HELPER_H

#include <atomic>
#include <type_traits>
#include "ecmascript/base/atomic_helper.h"

namespace panda::ecmascript::collections {
class CollectionsAtomicHelper final {
public:
    struct AtomicTypedArrayWitness {
        JSTaggedValue buffer {JSTaggedValue::Undefined()};
        DataViewType type {DataViewType::INT8};
        uint32_t length {0};
        uint32_t byteOffset {0};
        bool isSendable {true};
    };
    static bool ValidateArguments(EcmaRuntimeCallInfo *argv, uint32_t requiredCount, uint32_t maximumCount,
        bool waitable = false, bool hasArray = true);
    static JSTaggedValue ValidateIntegerTypedArray(JSThread *thread, JSHandle<JSTaggedValue> typedArray,
        bool waitable = false);
    static bool ValidateIntegerTypedArray(JSThread *thread, const JSHandle<JSTaggedValue> &typedArray,
        AtomicTypedArrayWitness &witness, bool waitable = false);
    static uint32_t ValidateAtomicAccess(JSThread *thread, const JSHandle<JSTaggedValue> typedArray,
        JSHandle<JSTaggedValue> requestIndex);
    static uint32_t ValidateAtomicAccess(JSThread *thread, const AtomicTypedArrayWitness &witness,
        JSHandle<JSTaggedValue> requestIndex);
    static JSTaggedValue AtomicStore(JSThread *thread, const JSHandle<JSTaggedValue> &typedArray,
        JSHandle<JSTaggedValue> index, JSHandle<JSTaggedValue> &value);
    static JSTaggedValue AtomicLoad(JSThread *thread, const JSHandle<JSTaggedValue> &typedArray,
        JSHandle<JSTaggedValue> index);
    template<typename T>
    static T AtomicLoadValue(T *ptr)
    {
        return reinterpret_cast<std::atomic<T> *>(ptr)->load(std::memory_order_seq_cst);
    }
    template<typename T>
    static void AtomicStoreValue(T *ptr, T value)
    {
        reinterpret_cast<std::atomic<T> *>(ptr)->store(value, std::memory_order_seq_cst);
    }
};
}  // namespace panda::ecmascript::collections
#endif  // ECMASCRIPT_UTILS_COLLECTIONS_ATOMIC_HELPER_H
