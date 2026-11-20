/*
 * Copyright (c) 2021 Huawei Device Co., Ltd.
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

#include "ecmascript/mem/local_cmc/cc_evacuator-inl.h"

#include "ecmascript/js_hclass-inl.h"
#include "ecmascript/mem/tlab_allocator-inl.h"
#include "ecmascript/mem/work_manager-inl.h"
#include "ecmascript/free_object.h"

namespace panda::ecmascript {
TaggedObject *CCEvacuator::Copy(TaggedObject *fromObj, const MarkWord &markWord)
{
    JSHClass *klass = markWord.GetJSHClass();
    size_t size = klass->SizeFromJSHClass(fromObj);
#if USE_CMS_GC
    uintptr_t forwardAddress = tlab_->Allocate(size);
#else
    Region *fromRegion = Region::ObjectAddressToRange(fromObj);
    MemSpaceType targetSpace = fromRegion->InModuleSpace() ? MemSpaceType::MODULE_SPACE : MemSpaceType::OLD_SPACE;
    uintptr_t forwardAddress = fromRegion->InNativePointerSpace()
        ? tlab_->AllocateNativePointer() : tlab_->Allocate(size, targetSpace);
#endif
    if (UNLIKELY(forwardAddress == 0)) {
        LOG_ECMA_MEM(FATAL) << "EvacuateObject alloc failed: " << " size: " << size;
        UNREACHABLE();
    }
    errno_t res = memcpy_s(ToVoidPtr(forwardAddress), size, fromObj, size);
    if (res != EOK) {
        LOG_FULL(FATAL) << "memcpy_s failed " << res;
    }
    std::atomic_thread_fence(std::memory_order_seq_cst);
    MarkWordType oldValue = markWord.GetValue();
    MarkWordType result = Barriers::AtomicSetPrimitive(fromObj, 0, oldValue,
                                                       MarkWord::FromForwardingAddress(forwardAddress));
    if (result == oldValue) {
        TaggedObject *toObject = reinterpret_cast<TaggedObject*>(forwardAddress);
        heap_->OnMoveEvent(reinterpret_cast<uintptr_t>(fromObj), toObject, size);
        return toObject;
    }
    ASSERT(MarkWord::IsForwardingAddress(result));
#if !USE_CMS_GC
    if (fromRegion->InNativePointerSpace()) {
        tlab_->ReleaseNativePointer(forwardAddress);
    } else if (targetSpace == MemSpaceType::MODULE_SPACE) {
        tlab_->ReleaseModule(forwardAddress);
    } else {
        FreeObject::FillFreeObject(heap_, forwardAddress, size);
    }
#else
    FreeObject::FillFreeObject(heap_, forwardAddress, size);
#endif
    return MarkWord::ToForwardingAddress(result);
}
}  // namespace panda::ecmascript
