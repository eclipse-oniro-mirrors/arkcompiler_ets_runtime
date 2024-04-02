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

#ifndef ECMASCRIPT_MEM_HEAP_INL_H
#define ECMASCRIPT_MEM_HEAP_INL_H

#include "ecmascript/mem/heap.h"

#include "ecmascript/dfx/hprof/heap_tracker.h"
#include "ecmascript/ecma_vm.h"
#include "ecmascript/mem/allocator-inl.h"
#include "ecmascript/mem/concurrent_sweeper.h"
#include "ecmascript/mem/linear_space.h"
#include "ecmascript/mem/mem_controller.h"
#include "ecmascript/mem/sparse_space.h"
#include "ecmascript/mem/tagged_object.h"
#include "ecmascript/mem/barriers-inl.h"
#include "ecmascript/mem/mem_map_allocator.h"

namespace panda::ecmascript {
#define CHECK_OBJ_AND_THROW_OOM_ERROR(object, size, space, message)                                         \
    if (UNLIKELY((object) == nullptr)) {                                                                    \
        EcmaVM *vm = GetEcmaVM();                                                                           \
        size_t oomOvershootSize = vm->GetEcmaParamConfiguration().GetOutOfMemoryOvershootSize();            \
        (space)->IncreaseOutOfMemoryOvershootSize(oomOvershootSize);                                        \
        if ((space)->IsOOMDumpSpace()) {                                                                    \
            DumpHeapSnapshotBeforeOOM();                                                                    \
        }                                                                                                   \
        StatisticHeapDetail();                                                                              \
        ThrowOutOfMemoryError(GetJSThread(), size, message);                                                \
        (object) = reinterpret_cast<TaggedObject *>((space)->Allocate(size));                               \
    }

#define CHECK_SOBJ_AND_THROW_OOM_ERROR(thread, object, size, space, message)                                \
    if (UNLIKELY((object) == nullptr)) {                                                                    \
        size_t oomOvershootSize = GetEcmaParamConfiguration().GetOutOfMemoryOvershootSize();                \
        (space)->IncreaseOutOfMemoryOvershootSize(oomOvershootSize);                                        \
        ThrowOutOfMemoryError(thread, size, message);                                                       \
        (object) = reinterpret_cast<TaggedObject *>((space)->Allocate(thread, size));                       \
    }

template<class Callback>
void SharedHeap::EnumerateOldSpaceRegions(const Callback &cb) const
{
    sOldSpace_->EnumerateRegions(cb);
    sNonMovableSpace_->EnumerateRegions(cb);
    sHugeObjectSpace_->EnumerateRegions(cb);
}

template<class Callback>
void SharedHeap::EnumerateOldSpaceRegionsWithRecord(const Callback &cb) const
{
    sOldSpace_->EnumerateRegionsWithRecord(cb);
    sNonMovableSpace_->EnumerateRegionsWithRecord(cb);
    sHugeObjectSpace_->EnumerateRegionsWithRecord(cb);
}

template<class Callback>
void SharedHeap::IterateOverObjects(const Callback &cb) const
{
    sOldSpace_->IterateOverObjects(cb);
    sNonMovableSpace_->IterateOverObjects(cb);
    sHugeObjectSpace_->IterateOverObjects(cb);
}

template<class Callback>
void Heap::EnumerateOldSpaceRegions(const Callback &cb, Region *region) const
{
    oldSpace_->EnumerateRegions(cb, region);
    appSpawnSpace_->EnumerateRegions(cb);
    nonMovableSpace_->EnumerateRegions(cb);
    hugeObjectSpace_->EnumerateRegions(cb);
    machineCodeSpace_->EnumerateRegions(cb);
    hugeMachineCodeSpace_->EnumerateRegions(cb);
}

template<class Callback>
void Heap::EnumerateSnapshotSpaceRegions(const Callback &cb) const
{
    snapshotSpace_->EnumerateRegions(cb);
}

template<class Callback>
void Heap::EnumerateNonNewSpaceRegions(const Callback &cb) const
{
    oldSpace_->EnumerateRegions(cb);
    oldSpace_->EnumerateCollectRegionSet(cb);
    appSpawnSpace_->EnumerateRegions(cb);
    snapshotSpace_->EnumerateRegions(cb);
    nonMovableSpace_->EnumerateRegions(cb);
    hugeObjectSpace_->EnumerateRegions(cb);
    machineCodeSpace_->EnumerateRegions(cb);
    hugeMachineCodeSpace_->EnumerateRegions(cb);
}

template<class Callback>
void Heap::EnumerateNonNewSpaceRegionsWithRecord(const Callback &cb) const
{
    oldSpace_->EnumerateRegionsWithRecord(cb);
    snapshotSpace_->EnumerateRegionsWithRecord(cb);
    nonMovableSpace_->EnumerateRegionsWithRecord(cb);
    hugeObjectSpace_->EnumerateRegionsWithRecord(cb);
    machineCodeSpace_->EnumerateRegionsWithRecord(cb);
    hugeMachineCodeSpace_->EnumerateRegionsWithRecord(cb);
}

template<class Callback>
void Heap::EnumerateNewSpaceRegions(const Callback &cb) const
{
    activeSemiSpace_->EnumerateRegions(cb);
}

template<class Callback>
void Heap::EnumerateNonMovableRegions(const Callback &cb) const
{
    snapshotSpace_->EnumerateRegions(cb);
    appSpawnSpace_->EnumerateRegions(cb);
    nonMovableSpace_->EnumerateRegions(cb);
    hugeObjectSpace_->EnumerateRegions(cb);
    machineCodeSpace_->EnumerateRegions(cb);
    hugeMachineCodeSpace_->EnumerateRegions(cb);
}

template<class Callback>
void Heap::EnumerateRegions(const Callback &cb) const
{
    activeSemiSpace_->EnumerateRegions(cb);
    oldSpace_->EnumerateRegions(cb);
    oldSpace_->EnumerateCollectRegionSet(cb);
    appSpawnSpace_->EnumerateRegions(cb);
    snapshotSpace_->EnumerateRegions(cb);
    nonMovableSpace_->EnumerateRegions(cb);
    hugeObjectSpace_->EnumerateRegions(cb);
    machineCodeSpace_->EnumerateRegions(cb);
    hugeMachineCodeSpace_->EnumerateRegions(cb);
}

template<class Callback>
void Heap::IterateOverObjects(const Callback &cb) const
{
    activeSemiSpace_->IterateOverObjects(cb);
    oldSpace_->IterateOverObjects(cb);
    readOnlySpace_->IterateOverObjects(cb);
    appSpawnSpace_->IterateOverMarkedObjects(cb);
    nonMovableSpace_->IterateOverObjects(cb);
    hugeObjectSpace_->IterateOverObjects(cb);
    hugeMachineCodeSpace_->IterateOverObjects(cb);
}

TaggedObject *Heap::AllocateYoungOrHugeObject(JSHClass *hclass)
{
    size_t size = hclass->GetObjectSize();
    return AllocateYoungOrHugeObject(hclass, size);
}

TaggedObject *Heap::AllocateYoungOrHugeObject(size_t size)
{
    size = AlignUp(size, static_cast<size_t>(MemAlignment::MEM_ALIGN_OBJECT));
    TaggedObject *object = nullptr;
    if (size > MAX_REGULAR_HEAP_OBJECT_SIZE) {
        object = AllocateHugeObject(size);
    } else {
        object = reinterpret_cast<TaggedObject *>(activeSemiSpace_->Allocate(size));
        if (object == nullptr) {
            CollectGarbage(SelectGCType(), GCReason::ALLOCATION_LIMIT);
            object = reinterpret_cast<TaggedObject *>(activeSemiSpace_->Allocate(size));
            if (object == nullptr) {
                CollectGarbage(SelectGCType(), GCReason::ALLOCATION_FAILED);
                object = reinterpret_cast<TaggedObject *>(activeSemiSpace_->Allocate(size));
                CHECK_OBJ_AND_THROW_OOM_ERROR(object, size, activeSemiSpace_, "Heap::AllocateYoungOrHugeObject");
            }
        }
    }
    return object;
}

TaggedObject *Heap::AllocateYoungOrHugeObject(JSHClass *hclass, size_t size)
{
    auto object = AllocateYoungOrHugeObject(size);
    object->SetClass(thread_, hclass);
    OnAllocateEvent(GetEcmaVM(), object, size);
    return object;
}

void BaseHeap::SetHClassAndDoAllocateEvent(JSThread *thread, TaggedObject *object, JSHClass *hclass, size_t size)
{
    object->SetClass(thread, hclass);
    OnAllocateEvent(thread->GetEcmaVM(), object, size);
}

uintptr_t Heap::AllocateYoungSync(size_t size)
{
    return activeSemiSpace_->AllocateSync(size);
}

bool Heap::MoveYoungRegionSync(Region *region)
{
    return activeSemiSpace_->SwapRegion(region, inactiveSemiSpace_);
}

void Heap::MergeToOldSpaceSync(LocalSpace *localSpace)
{
    oldSpace_->Merge(localSpace);
}

TaggedObject *Heap::TryAllocateYoungGeneration(JSHClass *hclass, size_t size)
{
    size = AlignUp(size, static_cast<size_t>(MemAlignment::MEM_ALIGN_OBJECT));
    if (size > MAX_REGULAR_HEAP_OBJECT_SIZE) {
        return nullptr;
    }
    auto object = reinterpret_cast<TaggedObject *>(activeSemiSpace_->Allocate(size));
    if (object != nullptr) {
        object->SetClass(thread_, hclass);
    }
    OnAllocateEvent(GetEcmaVM(), object, size);
    return object;
}

TaggedObject *Heap::AllocateOldOrHugeObject(JSHClass *hclass)
{
    size_t size = hclass->GetObjectSize();
    TaggedObject *object = AllocateOldOrHugeObject(hclass, size);
    OnAllocateEvent(GetEcmaVM(), object, size);
    return object;
}

TaggedObject *Heap::AllocateOldOrHugeObject(JSHClass *hclass, size_t size)
{
    size = AlignUp(size, static_cast<size_t>(MemAlignment::MEM_ALIGN_OBJECT));
    TaggedObject *object = nullptr;
    if (size > MAX_REGULAR_HEAP_OBJECT_SIZE) {
        object = AllocateHugeObject(hclass, size);
    } else {
        object = reinterpret_cast<TaggedObject *>(oldSpace_->Allocate(size));
        CHECK_OBJ_AND_THROW_OOM_ERROR(object, size, oldSpace_, "Heap::AllocateOldOrHugeObject");
        object->SetClass(thread_, hclass);
    }
    OnAllocateEvent(GetEcmaVM(), reinterpret_cast<TaggedObject*>(object), size);
    return object;
}

TaggedObject *Heap::AllocateReadOnlyOrHugeObject(JSHClass *hclass)
{
    size_t size = hclass->GetObjectSize();
    TaggedObject *object = AllocateReadOnlyOrHugeObject(hclass, size);
    OnAllocateEvent(GetEcmaVM(), object, size);
    return object;
}

TaggedObject *Heap::AllocateReadOnlyOrHugeObject(JSHClass *hclass, size_t size)
{
    size = AlignUp(size, static_cast<size_t>(MemAlignment::MEM_ALIGN_OBJECT));
    TaggedObject *object = nullptr;
    if (size > MAX_REGULAR_HEAP_OBJECT_SIZE) {
        object = AllocateHugeObject(hclass, size);
    } else {
        object = reinterpret_cast<TaggedObject *>(readOnlySpace_->Allocate(size));
        CHECK_OBJ_AND_THROW_OOM_ERROR(object, size, readOnlySpace_, "Heap::AllocateReadOnlyOrHugeObject");
        object->SetClass(thread_, hclass);
    }
    OnAllocateEvent(GetEcmaVM(), object, size);
    return object;
}

TaggedObject *Heap::AllocateNonMovableOrHugeObject(JSHClass *hclass)
{
    size_t size = hclass->GetObjectSize();
    TaggedObject *object = AllocateNonMovableOrHugeObject(hclass, size);
    OnAllocateEvent(GetEcmaVM(), object, size);
    return object;
}

TaggedObject *Heap::AllocateNonMovableOrHugeObject(JSHClass *hclass, size_t size)
{
    size = AlignUp(size, static_cast<size_t>(MemAlignment::MEM_ALIGN_OBJECT));
    TaggedObject *object = nullptr;
    if (size > MAX_REGULAR_HEAP_OBJECT_SIZE) {
        object = AllocateHugeObject(hclass, size);
    } else {
        object = reinterpret_cast<TaggedObject *>(nonMovableSpace_->CheckAndAllocate(size));
        CHECK_OBJ_AND_THROW_OOM_ERROR(object, size, nonMovableSpace_, "Heap::AllocateNonMovableOrHugeObject");
        object->SetClass(thread_, hclass);
    }
    OnAllocateEvent(GetEcmaVM(), object, size);
    return object;
}

TaggedObject *Heap::AllocateClassClass(JSHClass *hclass, size_t size)
{
    size = AlignUp(size, static_cast<size_t>(MemAlignment::MEM_ALIGN_OBJECT));
    auto object = reinterpret_cast<TaggedObject *>(nonMovableSpace_->Allocate(size));
    if (UNLIKELY(object == nullptr)) {
        LOG_ECMA_MEM(FATAL) << "Heap::AllocateClassClass can not allocate any space";
        UNREACHABLE();
    }
    *reinterpret_cast<MarkWordType *>(ToUintPtr(object)) = reinterpret_cast<MarkWordType>(hclass);
    OnAllocateEvent(GetEcmaVM(), object, size);
    return object;
}

TaggedObject *SharedHeap::AllocateClassClass(JSThread *thread, JSHClass *hclass, size_t size)
{
    size = AlignUp(size, static_cast<size_t>(MemAlignment::MEM_ALIGN_OBJECT));
    auto object = reinterpret_cast<TaggedObject *>(sNonMovableSpace_->AllocateWithoutGC(thread, size));
    if (UNLIKELY(object == nullptr)) {
        LOG_ECMA_MEM(FATAL) << "Heap::AllocateClassClass can not allocate any space";
        UNREACHABLE();
    }
    *reinterpret_cast<MarkWordType *>(ToUintPtr(object)) = reinterpret_cast<MarkWordType>(hclass);
    // todo(Gymee) OnAllocateEvent
    return object;
}

TaggedObject *Heap::AllocateHugeObject(size_t size)
{
    // Check whether it is necessary to trigger Old GC before expanding to avoid OOM risk.
    CheckAndTriggerOldGC(size);

    auto *object = reinterpret_cast<TaggedObject *>(hugeObjectSpace_->Allocate(size, thread_));
    if (UNLIKELY(object == nullptr)) {
        CollectGarbage(TriggerGCType::OLD_GC, GCReason::ALLOCATION_LIMIT);
        object = reinterpret_cast<TaggedObject *>(hugeObjectSpace_->Allocate(size, thread_));
        if (UNLIKELY(object == nullptr)) {
            // if allocate huge object OOM, temporarily increase space size to avoid vm crash
            size_t oomOvershootSize = config_.GetOutOfMemoryOvershootSize();
            oldSpace_->IncreaseOutOfMemoryOvershootSize(oomOvershootSize);
            DumpHeapSnapshotBeforeOOM();
            StatisticHeapDetail();
            object = reinterpret_cast<TaggedObject *>(hugeObjectSpace_->Allocate(size, thread_));
            ThrowOutOfMemoryError(thread_, size, "Heap::AllocateHugeObject");
            object = reinterpret_cast<TaggedObject *>(hugeObjectSpace_->Allocate(size, thread_));
            if (UNLIKELY(object == nullptr)) {
                FatalOutOfMemoryError(size, "Heap::AllocateHugeObject");
            }
        }
    }
    return object;
}

TaggedObject *Heap::AllocateHugeObject(JSHClass *hclass, size_t size)
{
    // Check whether it is necessary to trigger Old GC before expanding to avoid OOM risk.
    CheckAndTriggerOldGC(size);
    auto object = AllocateHugeObject(size);
    object->SetClass(thread_, hclass);
    OnAllocateEvent(GetEcmaVM(), object, size);
    return object;
}

TaggedObject *Heap::AllocateHugeMachineCodeObject(size_t size)
{
    auto *object = reinterpret_cast<TaggedObject *>(hugeMachineCodeSpace_->Allocate(size, thread_));
    return object;
}

TaggedObject *Heap::AllocateMachineCodeObject(JSHClass *hclass, size_t size)
{
    size = AlignUp(size, static_cast<size_t>(MemAlignment::MEM_ALIGN_OBJECT));
    auto object = (size > MAX_REGULAR_HEAP_OBJECT_SIZE) ?
        reinterpret_cast<TaggedObject *>(AllocateHugeMachineCodeObject(size)) :
        reinterpret_cast<TaggedObject *>(machineCodeSpace_->Allocate(size));
    CHECK_OBJ_AND_THROW_OOM_ERROR(object, size, machineCodeSpace_, "Heap::AllocateMachineCodeObject");
    object->SetClass(thread_, hclass);
    OnAllocateEvent(GetEcmaVM(), object, size);
    return object;
}

uintptr_t Heap::AllocateSnapshotSpace(size_t size)
{
    size = AlignUp(size, static_cast<size_t>(MemAlignment::MEM_ALIGN_OBJECT));
    uintptr_t object = snapshotSpace_->Allocate(size);
    if (UNLIKELY(object == 0)) {
        FatalOutOfMemoryError(size, "Heap::AllocateSnapshotSpaceObject");
    }
    OnAllocateEvent(GetEcmaVM(), reinterpret_cast<TaggedObject *>(object), size);
    return object;
}

void Heap::SwapNewSpace()
{
    activeSemiSpace_->Stop();
    inactiveSemiSpace_->Restart();

    SemiSpace *newSpace = inactiveSemiSpace_;
    inactiveSemiSpace_ = activeSemiSpace_;
    activeSemiSpace_ = newSpace;
    if (UNLIKELY(ShouldVerifyHeap())) {
        inactiveSemiSpace_->EnumerateRegions([](Region *region) {
            region->SetInactiveSemiSpace();
        });
    }
#ifdef ECMASCRIPT_SUPPORT_HEAPSAMPLING
    activeSemiSpace_->SwapAllocationCounter(inactiveSemiSpace_);
#endif
    auto topAddress = activeSemiSpace_->GetAllocationTopAddress();
    auto endAddress = activeSemiSpace_->GetAllocationEndAddress();
    thread_->ReSetNewSpaceAllocationAddress(topAddress, endAddress);
}

void Heap::SwapOldSpace()
{
    compressSpace_->SetInitialCapacity(oldSpace_->GetInitialCapacity());
    auto *oldSpace = compressSpace_;
    compressSpace_ = oldSpace_;
    oldSpace_ = oldSpace;
#ifdef ECMASCRIPT_SUPPORT_HEAPSAMPLING
    oldSpace_->SwapAllocationCounter(compressSpace_);
#endif
}

void Heap::ReclaimRegions(TriggerGCType gcType)
{
    activeSemiSpace_->EnumerateRegionsWithRecord([] (Region *region) {
        region->ClearMarkGCBitset();
        region->ClearCrossRegionRSet();
        region->ResetAliveObject();
        region->ClearGCFlag(RegionGCFlags::IN_NEW_TO_NEW_SET);
    });
    size_t cachedSize = inactiveSemiSpace_->GetInitialCapacity();
    if (gcType == TriggerGCType::FULL_GC) {
        compressSpace_->Reset();
        cachedSize = 0;
    } else if (gcType == TriggerGCType::OLD_GC) {
        oldSpace_->ReclaimCSet();
    }

    inactiveSemiSpace_->ReclaimRegions(cachedSize);

    sweeper_->WaitAllTaskFinished();
    EnumerateNonNewSpaceRegionsWithRecord([] (Region *region) {
        region->ClearMarkGCBitset();
        region->ClearCrossRegionRSet();
    });
    if (!clearTaskFinished_) {
        LockHolder holder(waitClearTaskFinishedMutex_);
        clearTaskFinished_ = true;
        waitClearTaskFinishedCV_.SignalAll();
    }
}

// only call in js-thread
void Heap::ClearSlotsRange(Region *current, uintptr_t freeStart, uintptr_t freeEnd)
{
    if (!current->InYoungSpace()) {
        current->AtomicClearSweepingRSetInRange(freeStart, freeEnd);
        current->ClearOldToNewRSetInRange(freeStart, freeEnd);
        current->AtomicClearCrossRegionRSetInRange(freeStart, freeEnd);
    }
    current->AtomicClearLocalToShareRSetInRange(freeStart, freeEnd);
}

size_t Heap::GetCommittedSize() const
{
    size_t result = activeSemiSpace_->GetCommittedSize() +
                    oldSpace_->GetCommittedSize() +
                    hugeObjectSpace_->GetCommittedSize() +
                    nonMovableSpace_->GetCommittedSize() +
                    machineCodeSpace_->GetCommittedSize() +
                    hugeMachineCodeSpace_->GetCommittedSize() +
                    snapshotSpace_->GetCommittedSize();
    return result;
}

size_t Heap::GetHeapObjectSize() const
{
    size_t result = activeSemiSpace_->GetHeapObjectSize() +
                    oldSpace_->GetHeapObjectSize() +
                    hugeObjectSpace_->GetHeapObjectSize() +
                    nonMovableSpace_->GetHeapObjectSize() +
                    machineCodeSpace_->GetCommittedSize() +
                    hugeMachineCodeSpace_->GetCommittedSize() +
                    snapshotSpace_->GetHeapObjectSize();
    return result;
}

uint32_t Heap::GetHeapObjectCount() const
{
    uint32_t count = 0;
    sweeper_->EnsureAllTaskFinished();
    this->IterateOverObjects([&count]([[maybe_unused]] TaggedObject *obj) {
        ++count;
    });
    return count;
}

void Heap::InitializeIdleStatusControl(std::function<void(bool)> callback)
{
    notifyIdleStatusCallback = callback;
    if (callback != nullptr) {
        OPTIONAL_LOG(ecmaVm_, INFO) << "Received idle status control call back";
        enableIdleGC_ = ecmaVm_->GetJSOptions().EnableIdleGC();
    }
}

TaggedObject *SharedHeap::AllocateNonMovableOrHugeObject(JSThread *thread, JSHClass *hclass)
{
    size_t size = hclass->GetObjectSize();
    return AllocateNonMovableOrHugeObject(thread, hclass, size);
}

TaggedObject *SharedHeap::AllocateNonMovableOrHugeObject(JSThread *thread, JSHClass *hclass, size_t size)
{
    size = AlignUp(size, static_cast<size_t>(MemAlignment::MEM_ALIGN_OBJECT));
    if (size > MAX_REGULAR_HEAP_OBJECT_SIZE) {
        return AllocateHugeObject(thread, hclass, size);
    }
    auto object = reinterpret_cast<TaggedObject *>(sNonMovableSpace_->Allocate(thread, size));
    CHECK_SOBJ_AND_THROW_OOM_ERROR(thread, object, size, sNonMovableSpace_,
        "SharedHeap::AllocateNonMovableOrHugeObject");
    object->SetClass(thread, hclass);
    // todo(lukai) OnAllocateEvent
    return object;
}

TaggedObject *SharedHeap::AllocateOldOrHugeObject(JSThread *thread, JSHClass *hclass)
{
    size_t size = hclass->GetObjectSize();
    return AllocateOldOrHugeObject(thread, hclass, size);
}

TaggedObject *SharedHeap::AllocateOldOrHugeObject(JSThread *thread, JSHClass *hclass, size_t size)
{
    size = AlignUp(size, static_cast<size_t>(MemAlignment::MEM_ALIGN_OBJECT));
    if (size > MAX_REGULAR_HEAP_OBJECT_SIZE) {
        return AllocateHugeObject(thread, hclass, size);
    }
    auto object = reinterpret_cast<TaggedObject *>(sOldSpace_->Allocate(thread, size));
    CHECK_SOBJ_AND_THROW_OOM_ERROR(thread, object, size, sOldSpace_, "SharedHeap::AllocateOldOrHugeObject");
    object->SetClass(thread, hclass);
    // todo(lukai) OnAllocateEvent
    return object;
}

TaggedObject *SharedHeap::AllocateOldOrHugeObject(JSThread *thread, size_t size)
{
    size = AlignUp(size, static_cast<size_t>(MemAlignment::MEM_ALIGN_OBJECT));
    if (size > MAX_REGULAR_HEAP_OBJECT_SIZE) {
        return AllocateHugeObject(thread, size);
    }

    auto object = reinterpret_cast<TaggedObject *>(sOldSpace_->Allocate(thread, size));
    CHECK_SOBJ_AND_THROW_OOM_ERROR(thread, object, size, sOldSpace_, "SharedHeap::AllocateOldOrHugeObject");
    return object;
}

TaggedObject *SharedHeap::AllocateHugeObject(JSThread *thread, JSHClass *hclass, size_t size)
{
    auto object = AllocateHugeObject(thread, size);
    object->SetClass(thread, hclass);
    // todo(lukai) OnAllocateEvent
    return object;
}

TaggedObject *SharedHeap::AllocateHugeObject(JSThread *thread, size_t size)
{
    // Check whether it is necessary to trigger Shared GC before expanding to avoid OOM risk.
    CheckHugeAndTriggerGC(thread, size);
    auto *object = reinterpret_cast<TaggedObject *>(sHugeObjectSpace_->Allocate(thread, size));
    if (UNLIKELY(object == nullptr)) {
        CollectGarbage(thread, TriggerGCType::SHARED_GC, GCReason::ALLOCATION_LIMIT);
        object = reinterpret_cast<TaggedObject *>(sHugeObjectSpace_->Allocate(thread, size));
        if (UNLIKELY(object == nullptr)) {
            // if allocate huge object OOM, temporarily increase space size to avoid vm crash
            size_t oomOvershootSize = config_.GetOutOfMemoryOvershootSize();
            sHugeObjectSpace_->IncreaseOutOfMemoryOvershootSize(oomOvershootSize);
            // todo(lukai) DumpHeapSnapshotBeforeOOM
            ThrowOutOfMemoryError(thread, size, "SharedHeap::AllocateHugeObject");
            object = reinterpret_cast<TaggedObject *>(sHugeObjectSpace_->Allocate(thread, size));
            if (UNLIKELY(object == nullptr)) {
                FatalOutOfMemoryError(size, "SharedHeap::AllocateHugeObject");
            }
        }
    }
    return object;
}

TaggedObject *SharedHeap::AllocateReadOnlyOrHugeObject(JSThread *thread, JSHClass *hclass)
{
    size_t size = hclass->GetObjectSize();
    return AllocateReadOnlyOrHugeObject(thread, hclass, size);
}

TaggedObject *SharedHeap::AllocateReadOnlyOrHugeObject(JSThread *thread, JSHClass *hclass, size_t size)
{
    size = AlignUp(size, static_cast<size_t>(MemAlignment::MEM_ALIGN_OBJECT));
    if (size > MAX_REGULAR_HEAP_OBJECT_SIZE) {
        return AllocateHugeObject(thread, hclass, size);
    }
    auto object = reinterpret_cast<TaggedObject *>(sReadOnlySpace_->Allocate(thread, size));
    CHECK_SOBJ_AND_THROW_OOM_ERROR(thread, object, size, sReadOnlySpace_, "SharedHeap::AllocateReadOnlyOrHugeObject");
    object->SetClass(thread, hclass);
    return object;
}
}  // namespace panda::ecmascript

#endif  // ECMASCRIPT_MEM_HEAP_INL_H
