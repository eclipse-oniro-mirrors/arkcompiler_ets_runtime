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

#ifndef ECMASCRIPT_MEM_TLAB_ALLOCATOR_INL_H
#define ECMASCRIPT_MEM_TLAB_ALLOCATOR_INL_H

#include "ecmascript/mem/tlab_allocator.h"

#include "ecmascript/free_object.h"
#include "ecmascript/mem/cms_mem/slot_free_segment-inl.h"
#include "ecmascript/mem/full_gc.h"
#include "ecmascript/mem/heap-inl.h"
#include "ecmascript/mem/specialized_slot_space.h"

namespace panda::ecmascript {
static constexpr size_t MIN_BUFFER_SIZE = 31_KB;
static constexpr size_t SMALL_OBJECT_SIZE = 8_KB;

SpecializedSlotTlabAllocator::SpecializedSlotTlabAllocator(SpecializedSlotSpace *space) : space_(space)
{
    ASSERT(space_ != nullptr);
    slotSize_ = space_->GetSlotSize();
}

uintptr_t SpecializedSlotTlabAllocator::Allocate()
{
    ASSERT(space_ != nullptr);
    if (LIKELY(top_ < end_)) {
        uintptr_t result = top_;
        top_ += slotSize_;
        allocatedBytes_ += slotSize_;
        return result;
    }
    return Refill();
}

void SpecializedSlotTlabAllocator::Reset(SpecializedSlotSpace *space)
{
    ASSERT(space != nullptr);
    Discard();
    space_ = space;
    slotSize_ = space->GetSlotSize();
}

void SpecializedSlotTlabAllocator::Finalize()
{
    Discard();
    if (allocatedBytes_ != 0) {
        // The slots handed out here are module objects copied into module space during a full
        // GC evacuation, so they are exactly the space's surviving bytes for this work node.
        space_->AddSurvivalObjectSize(allocatedBytes_);
        allocatedBytes_ = 0;
    }
    space_ = nullptr;
    slotSize_ = 0;
}

void SpecializedSlotTlabAllocator::Discard()
{
    if (top_ < end_) {
        SlotFreeSegment::FillFreeSegment(top_, end_ - top_);
    }
    top_ = 0;
    end_ = 0;
}

uintptr_t SpecializedSlotTlabAllocator::Refill()
{
    auto [bufferBegin, bufferSize] = space_->AllocateBufferSync();
    ASSERT(bufferSize >= slotSize_);
    top_ = bufferBegin;
    end_ = bufferBegin + bufferSize;
    return Allocate();
}

TlabAllocator::TlabAllocator(Heap *heap)
    : heap_(heap), enableExpandYoung_(true), enableStealOldRegion_(true),
      moduleTlab_(heap->GetModuleSpace()), nonMovableSpace_(heap_->GetNonMovableSpace()),
      nativePointerAllocator_(heap_->GetNativePointerSpace())
{
    size_t maxOldSpaceCapacity = heap->GetOldSpace()->GetMaximumCapacity();
    localSpace_ = new LocalSpace(heap, maxOldSpaceCapacity, maxOldSpaceCapacity);
    youngAllocator_.Reset();
    nonMovableAllocator_.Reset();
}

inline void TlabAllocator::DrainBumpAllocator(BumpPointerAllocator &allocator)
{
    if (allocator.Available() != 0) {
        FreeObject::FillFreeObject(heap_, allocator.GetTop(), allocator.Available());
        allocator.Reset();
    }
}

inline void TlabAllocator::Finalize()
{
    DrainBumpAllocator(youngAllocator_);
    DrainBumpAllocator(nonMovableAllocator_);
    moduleTlab_.Finalize();
    nativePointerAllocator_.Finalize();
    heap_->MergeToOldSpaceSync(localSpace_);
}

uintptr_t TlabAllocator::Allocate(size_t size, MemSpaceType space)
{
    uintptr_t result = 0;
    switch (space) {
        case SEMI_SPACE:
            result = AllocateInYoungSpace(size);
            break;
        case OLD_SPACE:
            result = AllocateInOldSpace(size);
            break;
        case COMPRESS_SPACE:
            result = AllocateInCompressSpace(size);
            break;
        case NON_MOVABLE:
            result = AllocateInNonMovableSpace(size);
            break;
        case MODULE_SPACE:
            result = AllocateInModuleSpace(size);
            break;
        case NATIVE_POINTER_SPACE:
            result = AllocateInNativePointerSpace(size);
            break;
        default:
            LOG_ECMA(FATAL) << "this branch is unreachable";
            UNREACHABLE();
    }
    return result;
}

uintptr_t TlabAllocator::AllocateInYoungSpace(size_t size)
{
    ASSERT(AlignUp(size, static_cast<size_t>(MemAlignment::MEM_ALIGN_OBJECT)) == size);
    if (UNLIKELY(size > SMALL_OBJECT_SIZE)) {
        uintptr_t address = heap_->AllocateYoungSync(size);
        return address;
    }
    uintptr_t result = youngAllocator_.Allocate(size);
    if (result != 0) {
        return result;
    }
    if (!enableExpandYoung_ || !ExpandYoung()) {
        enableExpandYoung_ = false;
        return 0;
    }
    return youngAllocator_.Allocate(size);
}

uintptr_t TlabAllocator::AllocateInCompressSpace(size_t size)
{
    ASSERT(AlignUp(size, static_cast<size_t>(MemAlignment::MEM_ALIGN_OBJECT)) == size);
    size = AlignUp(size, static_cast<size_t>(MemAlignment::MEM_ALIGN_OBJECT));
    uintptr_t result = localSpace_->Allocate(size, true);
    ASSERT(result != 0);
    return result;
}

uintptr_t TlabAllocator::AllocateInNonMovableSpace(size_t size)
{
    ASSERT(AlignUp(size, static_cast<size_t>(MemAlignment::MEM_ALIGN_OBJECT)) == size);
    uintptr_t result = nonMovableAllocator_.Allocate(size);
    if (LIKELY(result != 0)) {
        return result;
    }
    if (UNLIKELY(size > SMALL_OBJECT_SIZE)) {
        result = nonMovableSpace_->AllocateSyncInGC(size);
        ASSERT(result != 0);
        return result;
    }
    ExpandNonMovable();
    result = nonMovableAllocator_.Allocate(size);
    ASSERT(result != 0);
    return result;
}

uintptr_t TlabAllocator::AllocateInModuleSpace(size_t size)
{
    ASSERT(size == heap_->GetModuleSpace()->GetSlotSize());
    return moduleTlab_.Allocate();
}

uintptr_t TlabAllocator::AllocateInNativePointerSpace(size_t size)
{
    ASSERT(size == nativePointerAllocator_.GetSlotSize());
    return nativePointerAllocator_.Allocate();
}

uintptr_t TlabAllocator::AllocateInOldSpace(size_t size)
{
    ASSERT(AlignUp(size, static_cast<size_t>(MemAlignment::MEM_ALIGN_OBJECT)) == size);
    size = AlignUp(size, static_cast<size_t>(MemAlignment::MEM_ALIGN_OBJECT));
    // 1. Allocate from freelist in compress allocator
    uintptr_t result = localSpace_->Allocate(size, false);
    if (result == 0) {
        // 2. Expand region from old space
        if (enableStealOldRegion_) {
            enableStealOldRegion_ = ExpandCompressFromOld(size);
        }
        result = localSpace_->Allocate(size, true);
    }
    ASSERT(result != 0);
    return result;
}

bool TlabAllocator::ExpandYoung()
{
    uintptr_t buffer = heap_->AllocateYoungSync(MIN_BUFFER_SIZE);
    if (buffer == 0) {
        if (youngAllocator_.Available() != 0) {
            FreeObject::FillFreeObject(heap_, youngAllocator_.GetTop(), youngAllocator_.Available());
        }
        return false;
    }
    uintptr_t end = buffer + MIN_BUFFER_SIZE;

    if (buffer == youngAllocator_.GetEnd()) {
        buffer = youngAllocator_.GetTop();
    } else {
        if (youngAllocator_.Available() != 0) {
            FreeObject::FillFreeObject(heap_, youngAllocator_.GetTop(), youngAllocator_.Available());
        }
    }
    youngAllocator_.Reset(buffer, end);
    return true;
}

bool TlabAllocator::ExpandCompressFromOld(size_t size)
{
    auto region = heap_->GetOldSpace()->TryToGetExclusiveRegion(size);
    if (region != nullptr) {
        localSpace_->AddRegionToList(region);
        return true;
    }
    return false;
}

void TlabAllocator::ExpandNonMovable()
{
    uintptr_t buffer = nonMovableSpace_->AllocateSyncInGC(MIN_BUFFER_SIZE);
    ASSERT(buffer != 0);
    uintptr_t end = buffer + MIN_BUFFER_SIZE;

    if (buffer == nonMovableAllocator_.GetEnd()) {
        buffer = nonMovableAllocator_.GetTop();
    } else {
        if (nonMovableAllocator_.Available() != 0) {
            FreeObject::FillFreeObject(heap_, nonMovableAllocator_.GetTop(), nonMovableAllocator_.Available());
        }
    }
    nonMovableAllocator_.Reset(buffer, end);
}

void CCTlabAllocator::Setup(Heap *heap)
{
    heap_ = heap;
    toSpace_ = heap->GetToSpace();
    if constexpr (!G_USE_CMS_GC) {
        nativePointerAllocator_.Reset(heap->GetNativePointerSpace());
    }
}

void CCTlabAllocator::Initialize()
{
    bpAllocator_.Reset();
    if constexpr (!G_USE_CMS_GC) {
        nativePointerAllocator_.Reset(heap_->GetNativePointerSpace());
    }
}

void CCTlabAllocator::Finalize()
{
    if (bpAllocator_.Available() != 0) {
        FreeObject::FillFreeObject(heap_, bpAllocator_.GetTop(), bpAllocator_.Available());
    }
    bpAllocator_.Reset();
    if constexpr (!G_USE_CMS_GC) {
        nativePointerAllocator_.Finalize();
    }
}

uintptr_t CCTlabAllocator::AllocateNativePointer()
{
    return nativePointerAllocator_.Allocate();
}

uintptr_t CCTlabAllocator::Allocate(size_t size)
{
    ASSERT(AlignUp(size, static_cast<size_t>(MemAlignment::MEM_ALIGN_OBJECT)) == size);
    size = AlignUp(size, static_cast<size_t>(MemAlignment::MEM_ALIGN_OBJECT));
    uintptr_t result = bpAllocator_.Allocate(size);
    if (result == 0) {
        Expand();
        result = bpAllocator_.Allocate(size);
    }
    tlabRegion_->IncreaseAliveObject(size);
    ASSERT(result != 0);
    return result;
}

void CCTlabAllocator::Expand()
{
    tlabRegion_= toSpace_->ForceExpandSync();
    FreeObject::FillFreeObject(heap_, bpAllocator_.GetTop(), bpAllocator_.Available());
    bpAllocator_.Reset(tlabRegion_->GetBegin(), tlabRegion_->GetEnd());
}

SharedTlabAllocator::SharedTlabAllocator(SharedHeap *sHeap)
    : sHeap_(sHeap)
{
    size_t maxOldSpaceCapacity = sHeap->GetOldSpace()->GetMaximumCapacity();
    sLocalSpace_ = new SharedLocalSpace(sHeap, maxOldSpaceCapacity, maxOldSpaceCapacity);
}

inline void SharedTlabAllocator::Finalize()
{
    sHeap_->MergeToOldSpaceSync(sLocalSpace_);
}

uintptr_t SharedTlabAllocator::Allocate(size_t size, MemSpaceType space)
{
    uintptr_t result = 0;
    switch (space) {
        case SHARED_COMPRESS_SPACE:
            result = AllocateInCompressSpace(size);
            break;
        default:
            LOG_ECMA(FATAL) << "this branch is unreachable";
            UNREACHABLE();
    }
    return result;
}

uintptr_t SharedTlabAllocator::AllocateInCompressSpace(size_t size)
{
    ASSERT(AlignUp(size, static_cast<size_t>(MemAlignment::MEM_ALIGN_OBJECT)) == size);
    size = AlignUp(size, static_cast<size_t>(MemAlignment::MEM_ALIGN_OBJECT));
    uintptr_t result = sLocalSpace_->Allocate(size, true);
    ASSERT(result != 0);
    return result;
}

}  // namespace panda::ecmascript
#endif  // ECMASCRIPT_MEM_TLAB_ALLOCATOR_INL_H
