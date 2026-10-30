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

#ifndef ECMASCRIPT_MEM_SPECIALIZED_SLOT_SPACE_INL_H
#define ECMASCRIPT_MEM_SPECIALIZED_SLOT_SPACE_INL_H

#include "ecmascript/mem/specialized_slot_space.h"

namespace panda::ecmascript {
template <typename Callback>
void SpecializedSlotSpace::EnumerateRegions(Callback &&regionCallback) const
{
    regionChainManager_.EnumerateRegions(std::forward<Callback>(regionCallback));
}

template <typename Callback>
void SpecializedSlotSpace::EnumerateRegionsWithRecord(Callback &&regionCallback) const
{
    regionChainManager_.EnumerateRegionsWithRecord(std::forward<Callback>(regionCallback));
}

template <typename Callback>
void SpecializedSlotSpace::EnumerateFromRegions(Callback &&regionCallback) const
{
    LockHolder holder(pendingReclaimMutex_);
    for (Region *region : pendingReclaimFromRegions_) {
        regionCallback(region);
    }
}

template <typename Callback>
void SpecializedSlotSpace::IterateSlotObjectsInRegion(Region *region, const Callback &objectCallback) const
{
    uintptr_t current = region->GetBegin();
    uintptr_t end = CMSRegion::GetAllocatableEnd(CMSRegion::FromRegion(region), slotSize_);
    while (current < end) {
        auto *freeSegment = SlotFreeSegment::Cast(current);
        size_t objectSize = slotSize_;
        if (!freeSegment->IsSlotFreeSegment()) {
            auto *freeObject = FreeObject::Cast(current);
            if (!freeObject->IsFreeObject()) {
                objectCallback(reinterpret_cast<TaggedObject *>(current));
            } else {
                freeObject->AsanUnPoisonFreeObject();
                ASSERT(freeObject->Available() <= slotSize_);
                freeObject->AsanPoisonFreeObject();
            }
        } else {
            freeSegment->AsanUnPoisonFreeSegment();
            objectSize = freeSegment->GetSize();
            freeSegment->AsanPoisonFreeSegment();
        }
        current += objectSize;
        ASSERT(objectSize % slotSize_ == 0);
        CHECK_OBJECT_SIZE(objectSize);
    }
    CHECK_REGION_END(current, end);
}
}  // namespace panda::ecmascript

#endif  // ECMASCRIPT_MEM_SPECIALIZED_SLOT_SPACE_INL_H
