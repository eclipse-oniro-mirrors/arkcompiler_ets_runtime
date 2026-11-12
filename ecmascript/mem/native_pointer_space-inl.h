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

#ifndef ECMASCRIPT_MEM_NATIVE_POINTER_SPACE_INL_H
#define ECMASCRIPT_MEM_NATIVE_POINTER_SPACE_INL_H

#include "ecmascript/mem/native_pointer_space.h"
#include "ecmascript/mem/specialized_slot_space-inl.h"

namespace panda::ecmascript {
template <typename Callback>
void NativePointerSpace::CollectDeadCallbacksFromRegion(Region *region, const Callback &liveObjectCallback)
{
    void *env = localHeap_->GetJSThread()->GetEnv();
    std::vector<NativePointerCallbackData> concurrentCallbacks;
    std::vector<NativePointerCallbackData> asyncCallbacks;
    size_t totalBindingSize = 0;
    size_t asyncBindingSize = 0;
    IterateSlotObjectsInRegion(region, [this, region, &concurrentCallbacks, &asyncCallbacks, &totalBindingSize,
                                        &asyncBindingSize, env, &liveObjectCallback](TaggedObject *taggedObject) {
        MarkWord markWord(taggedObject, RELAXED_LOAD);
        if (markWord.IsForwardingAddress() || region->Test(taggedObject)) {
            liveObjectCallback(taggedObject);
            return;
        }
        auto *nativePointer = JSNativePointer::Cast(taggedObject);
        NativePointerCallbackData callback = std::make_pair(nativePointer->GetDeleter(),
            std::make_tuple(env, nativePointer->GetExternalPointer(), nativePointer->GetData()));
        size_t bindingSize = nativePointer->GetBindingSize();
        totalBindingSize += bindingSize;
        if (callback.first != nullptr && nativePointer->GetIsConcurrent()) {
            concurrentCallbacks.emplace_back(callback);
        } else if (callback.first != nullptr) {
            asyncCallbacks.emplace_back(callback);
            asyncBindingSize += bindingSize;
        }
        localHeap_->GetNativeAreaAllocator()->DecreaseNativeSizeStats(bindingSize, nativePointer->GetNativeFlag());
    });
    if (totalBindingSize != 0) {
        localHeap_->DecreaseNativeBindingSize(totalBindingSize);
    }
    localHeap_->AddNativePointerCallbacks(concurrentCallbacks, asyncCallbacks, asyncBindingSize);
}
}  // namespace panda::ecmascript

#endif  // ECMASCRIPT_MEM_NATIVE_POINTER_SPACE_INL_H
