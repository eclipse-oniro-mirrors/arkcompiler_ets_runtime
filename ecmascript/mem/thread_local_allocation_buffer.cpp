/*
 * Copyright (c) 2024 Huawei Device Co., Ltd.
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

#include "ecmascript/free_object.h"
#include "ecmascript/mem/thread_local_allocation_buffer.h"

namespace panda::ecmascript {
    void ThreadLocalAllocationBuffer::Reset(uintptr_t begin, uintptr_t end, uintptr_t top)
    {
        FillBumpPointer();
        tlabWasteLimit_ = INIT_WASTE_LIMIT;
        bpAllocator_.Reset(begin, end, top);
    }

    void ThreadLocalAllocationBuffer::FillBumpPointer()
    {
        size_t remainSize = Available();
        if (remainSize != 0) {
            FreeObject::FillFreeObject(heap_, GetTop(), remainSize);
        }
    }
}