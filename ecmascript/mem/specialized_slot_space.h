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

#ifndef ECMASCRIPT_MEM_SPECIALIZED_SLOT_SPACE_H
#define ECMASCRIPT_MEM_SPECIALIZED_SLOT_SPACE_H

#include <atomic>
#include <functional>
#include <utility>

#include "ecmascript/free_object.h"
#include "ecmascript/mem/cms_mem/cms_region_chain_manager.h"
#include "ecmascript/mem/cms_mem/slot_allocator.h"
#include "ecmascript/mem/space-inl.h"

namespace panda::ecmascript {
class Heap;

class SpecializedSlotSpace : public Space, public SweepableSpace {
public:
    SpecializedSlotSpace(Heap *heap, MemSpaceType spaceType, size_t slotSize,
                         size_t initialCapacity, size_t maximumCapacity);
    ~SpecializedSlotSpace() override;

    NO_COPY_SEMANTIC(SpecializedSlotSpace);
    NO_MOVE_SEMANTIC(SpecializedSlotSpace);

    size_t GetSlotSize() const
    {
        return slotSize_;
    }

    size_t GetHeapObjectSize() const
    {
        return survivalObjectSize_.load(std::memory_order_relaxed) + allocateAfterLastGC_;
    }

    void AddSurvivalObjectSize(size_t size)
    {
        survivalObjectSize_.fetch_add(size, std::memory_order_relaxed);
    }

    uintptr_t Allocate();
    std::pair<uintptr_t, size_t> AllocateBufferSync();

    template <typename Callback>
    void EnumerateRegions(Callback &&regionCallback) const;

    template <typename Callback>
    void EnumerateRegionsWithRecord(Callback &&regionCallback) const;

    template <typename Callback>
    void EnumerateFromRegions(Callback &&regionCallback) const;

    template <typename Callback>
    void IterateSlotObjectsInRegion(Region *region, const Callback &objectCallback) const;

    template <typename Visitor>
    inline void IterateOldToNewOverObjects(Visitor &visitor) const;

    void SetRecordRegion();
    void IterateOverObjects(const std::function<void(TaggedObject *object)> &objectVisitor);
    void PrepareEvacuation();
    // Merges the gc regions (evacuation targets) back into the main chain without
    // releasing the from-regions parked by PrepareEvacuation: Heap::ReclaimRegions
    // (via Resume) releases them after a normal full GC, and Heap::ResumeForAppSpawn
    // does it in appspawn mode — the same split SlotSpace uses for CMS full GCs.
    void MergeToGCRegions();
    void SetSurvivalObjectSize(size_t size);

    // Called only via the SweptChainManagerCallback registered in the constructor.
    void AddSweptRegionChainManager(CMSRegionChainManager *sweptRegionChainManager);

    void PrepareSweeping() override;
    void Sweep() override;
    void AsyncSweep(bool isMain, bool releaseMemory = false) override;
    bool TryFillSweptRegion() override;
    bool FinishFillSweptRegion() override;
    uint32_t GetRegionCount() const override;
    void ReclaimRegions(size_t cachedSize = 0) override;
    void ReclaimFromRegions();

protected:
    virtual bool CheckExpandBudget(size_t expandSize) = 0;

    Heap *localHeap_ {nullptr};

private:
    bool TryExpandAllocator(size_t expandSize);
    void ExpandAllocator(size_t expandSize);
    void ExpandInGC();
    void DrainSweptChainManagers();
    void InvokeAllocationInspector(Address object, size_t size, size_t alignedSize);

    size_t slotSize_ {0};
    SlotAllocator allocator_ {};
    CMSRegionChainManager regionChainManager_ {};
    SlotAllocator gcAllocator_ {};
    CMSRegionChainManager gcRegionChainManager_ {};
    std::vector<Region *> pendingReclaimFromRegions_ {};
    Mutex gcAllocateMutex_ {};
    mutable Mutex pendingReclaimMutex_ {};
    std::atomic<size_t> survivalObjectSize_ {0};
    size_t allocateAfterLastGC_ {0};
    std::vector<CMSRegionChainManager *> sweptChainManagers_ {};
    Mutex sweptChainManagerMutex_ {};
};
}  // namespace panda::ecmascript

#include "ecmascript/mem/specialized_slot_space-inl.h"

#endif  // ECMASCRIPT_MEM_SPECIALIZED_SLOT_SPACE_H
