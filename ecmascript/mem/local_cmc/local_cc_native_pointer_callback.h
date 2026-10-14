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

#ifndef ECMASCRIPT_MEM_LOCAL_CMC_LOCAL_CC_NATIVE_POINTER_CALLBACK_H
#define ECMASCRIPT_MEM_LOCAL_CMC_LOCAL_CC_NATIVE_POINTER_CALLBACK_H

#include "ecmascript/base/config.h"

#include <cstddef>
#include <memory>

#include "ecmascript/napi/include/jsnapi_expo.h"

namespace panda::ecmascript {
struct LocalCCNativePointerCallbackData {
    NativePointerCallback callback;
    void *pointer;
    void *data;
};

struct LocalCCNativePointerCallbackBucket {
    std::unique_ptr<LocalCCNativePointerCallbackData[]> callbacks {};
    size_t size {0};

    LocalCCNativePointerCallbackBucket() = default;
    ~LocalCCNativePointerCallbackBucket() = default;
    LocalCCNativePointerCallbackBucket(LocalCCNativePointerCallbackBucket &&) noexcept = default;
    LocalCCNativePointerCallbackBucket &operator=(LocalCCNativePointerCallbackBucket &&) noexcept = default;
    LocalCCNativePointerCallbackBucket(const LocalCCNativePointerCallbackBucket &) = delete;
    LocalCCNativePointerCallbackBucket &operator=(const LocalCCNativePointerCallbackBucket &) = delete;

    bool Empty() const
    {
        return size == 0;
    }

    void Reset()
    {
        callbacks.reset();
        size = 0;
    }
};
}  // namespace panda::ecmascript


#endif  // ECMASCRIPT_MEM_LOCAL_CMC_LOCAL_CC_NATIVE_POINTER_CALLBACK_H
