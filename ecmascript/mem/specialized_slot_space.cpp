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

#include "ecmascript/mem/specialized_slot_space-inl.h"

#include "ecmascript/mem/cms_mem/slot_allocator-inl.h"
#include "ecmascript/mem/cms_mem/slot_space_config.h"
#include "ecmascript/mem/heap-inl.h"

namespace panda::ecmascript {
SpecializedSlotSpace::SpecializedSlotSpace(Heap *heap, MemSpaceType spaceType, size_t slotSize,
                                           size_t initialCapacity, size_t maximumCapacity)
    : Space(heap, heap->GetHeapRegionAllocator(), spaceType, initialCapacity, maximumCapacity),
      SweepableSpace(), localHeap_(heap), slotSize_(slotSize)
{
    ASSERT(slotSize_ >= sizeof(SlotFreeSegment));
    ASSERT(slotSize_ % SlotSpaceConfig::SLOT_STEP_SIZE == 0);
    ASSERT(initialCapacity <= maximumCapacity);
    allocator_.Initialize(slotSize_, &regionChainManager_);
    gcAllocator_.Initialize(slotSize_, &gcRegionChainManager_);
    regionChainManager_.Initialize(localHeap_, slotSize_, this);
    gcRegionChainManager_.Initialize(localHeap_, slotSize_, this);
}

SpecializedSlotSpace::~SpecializedSlotSpace()
{
    ASSERT(pendingReclaimFromRegions_.empty());
}

uintptr_t SpecializedSlotSpace::Allocate()
{
    auto recordAllocatedSize = [this](size_t allocatedSize) {
        allocateAfterLastGC_ += allocatedSize;
    };
    uintptr_t result = allocator_.TryAllocate(recordAllocatedSize);
    if (result > 0) {
        InvokeAllocationInspector(result, slotSize_, slotSize_);
        return result;
    }
    if (!TryExpandAllocator(DEFAULT_REGION_SIZE)) {
        return 0;
    }
    result = allocator_.TryAllocate(recordAllocatedSize);
    ASSERT(result > 0);
    InvokeAllocationInspector(result, slotSize_, slotSize_);
    return result;
}

std::pair<uintptr_t, size_t> SpecializedSlotSpace::AllocateBufferSync()
{
    size_t maxBufferSize =
        (SlotSpaceConfig::MAX_GC_TLAB_BUFFER_SIZE + slotSize_ - 1) / slotSize_ * slotSize_;
    LockHolder holder(gcAllocateMutex_);
    auto result = gcAllocator_.TryAllocateBuffer(maxBufferSize);
    if (result.first > 0) {
        return result;
    }
    ExpandInGC();
    result = gcAllocator_.TryAllocateBuffer(maxBufferSize);
    ASSERT(result.first > 0);
    return result;
}

bool SpecializedSlotSpace::TryExpandAllocator(size_t expandSize)
{
    if (!CheckExpandBudget(expandSize)) {
        return false;
    }
    ExpandAllocator(expandSize);
    return true;
}

void SpecializedSlotSpace::ExpandAllocator(size_t expandSize)
{
    Region *region = heapRegionAllocator_->AllocateAlignedRegion(
        this, expandSize, localHeap_->GetJSThread(), localHeap_, false, slotSize_);
    InitializeRegion(region);
    allocator_.Expand(region);
    IncreaseCommitted(region->GetCapacity());
}

void SpecializedSlotSpace::ExpandInGC()
{
    Region *region = heapRegionAllocator_->AllocateAlignedRegion(
        this, DEFAULT_REGION_SIZE, localHeap_->GetJSThread(), localHeap_, false, slotSize_);
    InitializeRegion(region);
    gcAllocator_.Expand(region);
    region->SetRegionTypeFlag(RegionTypeFlag::TO);
    region->CreateLocalToShareRememberedSet();
    region->CreateCompressedLocalToShareRememberedSet();
    region->SwapLocalToShareRSetForCS();
}

void SpecializedSlotSpace::SetRecordRegion()
{
    regionChainManager_.SetRecordRegion();
}

void SpecializedSlotSpace::PrepareEvacuation()
{
    ASSERT(gcRegionChainManager_.IsEmpty());
    committedSize_ = 0;
    survivalObjectSize_.store(0, std::memory_order_relaxed);
    allocator_.Discard();
    {
        LockHolder holder(pendingReclaimMutex_);
        ASSERT(pendingReclaimFromRegions_.empty());
        pendingReclaimFromRegions_.reserve(GetRegionCount());
        regionChainManager_.PrepareCompact(pendingReclaimFromRegions_);
        for (Region *region : pendingReclaimFromRegions_) {
            region->SetGCFlag(RegionGCFlags::IN_COLLECT_SET);
        }
    }
    allocateAfterLastGC_ = 0;
}

void SpecializedSlotSpace::FinishEvacuation()
{
    MergeToGCRegions();
    ReclaimFromRegions();
}

void SpecializedSlotSpace::MergeToGCRegions()
{
    gcRegionChainManager_.EnumerateRegions([this](Region *toRegion) {
        toRegion->ResetSwept();
        toRegion->ClearMarkGCBitset();
        toRegion->ResetAliveObject();
        toRegion->MergeLocalToShareRSetForCS();
        regionChainManager_.AddNewRegion(toRegion);
        IncreaseCommitted(toRegion->GetCapacity());
    });
    gcRegionChainManager_.ClearRegionList();
    gcAllocator_.Discard();
}

void SpecializedSlotSpace::IncreaseSurvivalObjectSize(size_t size)
{
    survivalObjectSize_.fetch_add(size, std::memory_order_relaxed);
}

uint32_t SpecializedSlotSpace::GetRegionCount() const
{
    return regionChainManager_.GetRegionCount();
}

void SpecializedSlotSpace::ReclaimFromRegions()
{
    std::vector<Region *> regions;
    {
        LockHolder holder(pendingReclaimMutex_);
        std::swap(regions, pendingReclaimFromRegions_);
    }
    size_t cachedSize = localHeap_->GetRegionCachedSize();
    for (Region *region : regions) {
        region->ClearGCFlag(RegionGCFlags::IN_COLLECT_SET);
        region->DeleteCrossRegionRSet();
        region->DeleteOldToNewRSet();
        region->DeleteLocalToShareRSet();
        region->DeleteSweepingOldToNewRSet();
        region->DeleteSweepingLocalToShareRSet();
        heapRegionAllocator_->FreeRegion(region, cachedSize);
    }
}

void SpecializedSlotSpace::ReclaimRegions(size_t cachedSize)
{
    std::vector<Region *> regions;
    {
        LockHolder holder(pendingReclaimMutex_);
        std::swap(regions, pendingReclaimFromRegions_);
    }
    for (Region *region : regions) {
        heapRegionAllocator_->FreeRegion(region, cachedSize);
    }
    EnumerateRegions([this, cachedSize](Region *region) {
        ClearAndFreeRegion(region, cachedSize);
    });
    regionChainManager_.ClearRegionList();
    gcRegionChainManager_.ClearRegionList();
    committedSize_ = 0;
}

void SpecializedSlotSpace::IterateOverObjects(const std::function<void(TaggedObject *object)> &objectVisitor)
{
    allocator_.FillInFlightSegment();
    EnumerateRegions([this, &objectVisitor](Region *region) {
        uintptr_t current = region->GetBegin();
        uintptr_t end = CMSRegion::GetAllocatableEnd(CMSRegion::FromRegion(region), slotSize_);
        while (current < end) {
            auto *freeSegment = SlotFreeSegment::Cast(current);
            size_t objectSize = slotSize_;
            if (!freeSegment->IsSlotFreeSegment()) {
                auto *freeObject = FreeObject::Cast(current);
                if (!freeObject->IsFreeObject()) {
                    objectVisitor(reinterpret_cast<TaggedObject *>(current));
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
    });
}

void SpecializedSlotSpace::InvokeAllocationInspector(Address object, size_t size, size_t alignedSize)
{
    ASSERT(size <= alignedSize);
    if (LIKELY(!allocationCounter_.IsActive())) {
        return;
    }
    if (alignedSize >= allocationCounter_.NextBytes()) {
        allocationCounter_.InvokeAllocationInspector(object, size, alignedSize);
    }
    allocationCounter_.AdvanceAllocationInspector(alignedSize);
}

}  // namespace panda::ecmascript
