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

#include <array>
#include <cerrno>

#include "ecmascript/ecma_vm.h"

#include "ecmascript/mem/mem_map_allocator.h"
#include "ecmascript/mem/mem_common.h"
#include "ecmascript/jit/jit.h"
#include "ecmascript/tests/huge_page_test_helper.h"
#include "ecmascript/tests/test_helper.h"

using namespace panda::ecmascript;
using namespace panda::ecmascript::base;

namespace panda::test {

constexpr size_t HUGE_OBJECT_CAPACITY = 1024_MB;

class MemMapAllocatorTest : public BaseTestWithScope<false> {
#if ENABLE_HUGE_PAGE_MEM
protected:
    void EnableHugePageForTest()
    {
        ConfigureHugePageStateForTest(true);
        allocator_.capacity_ = HUGE_OBJECT_CAPACITY;
    }

    MemMap MapAndInsertHugeChunkForTest()
    {
        MemMap chunk = PageMap(HugeMemMapPool::HUGE_REGION_SIZE, PAGE_PROT_READWRITE,
                               HugeMemMapPool::HUGE_REGION_SIZE);
        if (chunk.GetMem() == nullptr) {
            return MemMap();
        }
        if (!allocator_.hugeMemMapPool_.InsertHugeChunk(chunk)) {
            PageUnmap(chunk);
            return MemMap();
        }
        auto it = allocator_.hugeMemMapPool_.chunkMap_.find(ToUintPtr(chunk.GetMem()));
        if (it == allocator_.hugeMemMapPool_.chunkMap_.end()) {
            return MemMap();
        }
        it->second.ownsQuotaReservation_ = false;
        return chunk;
    }

    bool MapAndInsertHugeChunk()
    {
        return MapAndInsertHugeChunkForTest().GetMem() != nullptr;
    }

    MemMap InsertRegularRegionForTest()
    {
        MemMap region = PageMap(DEFAULT_REGION_SIZE, PAGE_PROT_NONE, DEFAULT_REGION_SIZE);
        if (region.GetMem() == nullptr) {
            return MemMap();
        }
        allocator_.memMapPool_.AdoptMemMap(region);
        return region;
    }

    MemMap AllocateRegularRegion(const std::string &spaceName, bool shouldPageTag = false,
                                 bool isHugePageEligible = true)
    {
        HugePageTag tag = HugePageTag::INVALID;
        if (isHugePageEligible) {
            tag = spaceName == "semi space" ? HugePageTag::SEMI : HugePageTag::OLD;
        }
        return allocator_.Allocate(thread->GetThreadId(), DEFAULT_REGION_SIZE, DEFAULT_REGION_SIZE,
                                   spaceName, true, false, false, false, shouldPageTag, false,
                                   tag);
    }

    bool TryReleaseHugeSlot(void *mem, bool isRegular)
    {
        return allocator_.TryReleaseHugeSlot(mem, DEFAULT_REGION_SIZE, isRegular);
    }

    bool IsHugePageSlotForTest(void *mem)
    {
        HugeMemMapPool &pool = allocator_.hugeMemMapPool_;
        LockHolder lock(pool.lock_);
        uintptr_t addr = reinterpret_cast<uintptr_t>(mem);
        uintptr_t baseAddr = addr & ~(HugeMemMapPool::HUGE_REGION_SIZE - 1);
        return pool.chunkMap_.find(baseAddr) != pool.chunkMap_.end();
    }

    bool SetHugeChunkStateForTest(void *mem, uint8_t usageMask, HugeChunkDomain domain,
                                  bool physicalReleased, bool advising, bool reuseQuarantined)
    {
        HugeMemMapPool &pool = allocator_.hugeMemMapPool_;
        LockHolder lock(pool.lock_);
        uintptr_t addr = reinterpret_cast<uintptr_t>(mem);
        uintptr_t baseAddr = addr & ~(HugeMemMapPool::HUGE_REGION_SIZE - 1);
        auto iter = pool.chunkMap_.find(baseAddr);
        if (iter == pool.chunkMap_.end()) {
            return false;
        }
        auto &chunk = iter->second;
        chunk.usageMask_ = usageMask;
        chunk.ownerDomain_ = domain;
        chunk.isPhysicalReleased_ = physicalReleased;
        chunk.isAdvising_ = advising;
        chunk.isReuseQuarantined_ = reuseQuarantined;
        return true;
    }

    void *FindReusableChunkInBucketsForTest()
    {
        HugeMemMapPool &pool = allocator_.hugeMemMapPool_;
        auto acceptDomain = []([[maybe_unused]] const HugeMemMapPool::Chunk &chunk,
                               [[maybe_unused]] HugeChunkDomain targetDomain) {
            return true;
        };
        LockHolder lock(pool.lock_);
        auto *chunk = pool.FindReusableChunkInBucketsLocked(
            HugePageTag::OLD, HugeSlotAllocPolicy::DEFAULT, acceptDomain);
        return chunk == nullptr ? nullptr : reinterpret_cast<void *>(chunk->baseAddr_);
    }

    void *TakeEmptyChunkForTest()
    {
        HugeMemMapPool &pool = allocator_.hugeMemMapPool_;
        LockHolder lock(pool.lock_);
        auto *chunk = pool.TakeEmptyChunkLocked();
        return chunk == nullptr ? nullptr : reinterpret_cast<void *>(chunk->baseAddr_);
    }

    size_t SoftReleaseEmptyChunksForTest()
    {
        return allocator_.hugeMemMapPool_.SoftReleaseEmptyChunks();
    }

    struct HugeSummaryForTest {
        size_t totalChunks;
        size_t activeChunks;
        size_t emptyChunks;
        size_t releasedEmptyChunks;
        size_t usedSlots;
    };

    HugeSummaryForTest CollectHugeSummaryForTest()
    {
        HugeMemMapPool &pool = allocator_.hugeMemMapPool_;
        LockHolder lock(pool.lock_);
        auto stats = pool.CollectSummaryDfxLocked();
        return {
            stats.totalChunks,
            stats.activeChunks,
            stats.emptyChunks,
            stats.releasedEmptyChunks,
            stats.usedSlots,
        };
    }

#if USE_COMPRESSED_HUGE_PAGE_POOL
    CompressedHugePagePool &GetCompressedHugePagePoolForTest()
    {
        return allocator_.compressedHugePagePool_;
    }

    HugeMemMapPool &GetHugeMemMapPoolForTest()
    {
        return allocator_.hugeMemMapPool_;
    }

    MemMapPool &GetRegularMemMapPoolForTest()
    {
        return allocator_.memMapPool_;
    }

    void ReturnCompressedHugePagePoolToRegularPoolForTest()
    {
        allocator_.ReturnCompressedHugePagePoolToRegularPool();
    }
#endif

    void TearDown() override
    {
        allocator_.Finalize();
        ConfigureHugePageStateForTest(false);
        BaseTestWithScope<false>::TearDown();
    }

    MemMapAllocator allocator_;
#endif
};

HWTEST_F_L0(MemMapAllocatorTest, GetMemFromList)
{
    MemMap memMap = PageMap(HUGE_OBJECT_CAPACITY, PAGE_PROT_NONE, DEFAULT_REGION_SIZE);
    PageRelease(memMap.GetMem(), memMap.GetSize());

    size_t cap = MemMapAllocator::GetInstance()->GetCapacity();
    ASSERT_NE(cap, 0U);
    // Mirror initialHugeObjectCapacity in InitializeHugeRegionMap: min(capacity_/3, INITIAL_HUGE_OBJECT_CAPACITY)
    size_t initialHuge = (cap / 3 < INITIAL_HUGE_OBJECT_CAPACITY) ? cap / 3 : INITIAL_HUGE_OBJECT_CAPACITY;
    size_t allocSize = AlignUp(initialHuge / 2, PageSize());

    constexpr size_t LOOP_TIMES = 10;
    for (size_t i = 0; i < LOOP_TIMES; ++i) {
        // From FreeList
        auto mem1 = MemMapAllocator::GetInstance()->
            Allocate(0, allocSize, DEFAULT_REGION_SIZE, "", false, false, false, false, false, false);
        EXPECT_EQ(mem1.GetSize(), allocSize);

        // From FreeList
        auto mem2 = MemMapAllocator::GetInstance()->
            Allocate(0, allocSize, DEFAULT_REGION_SIZE, "", false, false, false, false, false, false);
        EXPECT_EQ(mem2.GetSize(), allocSize);

        // From PageMap
        auto mem3 = MemMapAllocator::GetInstance()->
            Allocate(0, allocSize, DEFAULT_REGION_SIZE, "", false, false, false, false, false, false);
        EXPECT_EQ(mem3.GetSize(), allocSize);

        thread->GetEcmaVM()->CollectGarbage(TriggerGCType::FULL_GC);
        MemMapAllocator::GetInstance()->CacheOrFree(mem1.GetMem(), mem1.GetSize(), false, false, 0, false, false);
        MemMapAllocator::GetInstance()->CacheOrFree(mem2.GetMem(), mem2.GetSize(), false, false, 0, false, false);
        MemMapAllocator::GetInstance()->CacheOrFree(mem3.GetMem(), mem3.GetSize(), false, false, 0, false, false);
        thread->GetEcmaVM()->CollectGarbage(TriggerGCType::FULL_GC);
    }
}

HWTEST_F_L0(MemMapAllocatorTest, GetMemOverflow)
{
    Heap *heap = const_cast<Heap *>(thread->GetEcmaVM()->GetHeap());
    SemiSpace *space = heap->GetNewSpace();
    MemMap mem = MemMapAllocator::GetInstance()->Allocate(thread->GetThreadId(), 2048_MB, DEFAULT_REGION_SIZE,
                                                         ToSpaceTypeName(space->GetSpaceType()),
                                                         false, false, true,
                                                         Jit::GetInstance()->IsEnableJitFort(), true, false);
    EXPECT_EQ(mem.GetSize(), 0_MB);
}

HWTEST_F_L0(MemMapAllocatorTest, AsyncFreeTest)
{
    Heap *heap = const_cast<Heap *>(thread->GetEcmaVM()->GetHeap());
    HugeObjectSpace *space = heap->GetHugeObjectSpace();
    constexpr size_t TEST_SIZE = 2_MB;
    MemMap mem = MemMapAllocator::GetInstance()->Allocate(thread->GetThreadId(), TEST_SIZE, DEFAULT_REGION_SIZE,
                                                         ToSpaceTypeName(space->GetSpaceType()),
                                                         false, false, false, false, false, false);
    EXPECT_EQ(mem.GetSize(), TEST_SIZE);
    MemMapAllocator::GetInstance()->DecreaseMemMapTotalSize(TEST_SIZE);
    MemMapAllocator::GetInstance()->AsyncFree(mem.GetMem(), TEST_SIZE, false, false, false);
    MemMap mem2 = MemMapAllocator::GetInstance()->Allocate(thread->GetThreadId(), TEST_SIZE, DEFAULT_REGION_SIZE,
                                                         ToSpaceTypeName(space->GetSpaceType()),
                                                         false, false, false, false, false, false);
    EXPECT_EQ(mem2.GetSize(), TEST_SIZE);
}

HWTEST_F_L0(MemMapAllocatorTest, InitialMemPool_RegularWithPageTag)
{
    Heap *heap = const_cast<Heap *>(thread->GetEcmaVM()->GetHeap());
    SemiSpace *space = heap->GetNewSpace();
    constexpr size_t TEST_SIZE = DEFAULT_REGION_SIZE;
    MemMap mem = MemMapAllocator::GetInstance()->Allocate(
        thread->GetThreadId(), TEST_SIZE, DEFAULT_REGION_SIZE,
        ToSpaceTypeName(space->GetSpaceType()),
        true, false, false, false, true, false);
    EXPECT_NE(mem.GetMem(), nullptr);
    EXPECT_EQ(mem.GetSize(), TEST_SIZE);
}

HWTEST_F_L0(MemMapAllocatorTest, InitialMemPool_RegularWithoutPageTag)
{
    Heap *heap = const_cast<Heap *>(thread->GetEcmaVM()->GetHeap());
    SemiSpace *space = heap->GetNewSpace();
    constexpr size_t TEST_SIZE = DEFAULT_REGION_SIZE;
    MemMap mem = MemMapAllocator::GetInstance()->Allocate(
        thread->GetThreadId(), TEST_SIZE, DEFAULT_REGION_SIZE,
        ToSpaceTypeName(space->GetSpaceType()),
        true, false, false, false, false, false);
    EXPECT_NE(mem.GetMem(), nullptr);
    EXPECT_EQ(mem.GetSize(), TEST_SIZE);
}

HWTEST_F_L0(MemMapAllocatorTest, InitialMemPool_CompressWithPageTag)
{
    Heap *heap = const_cast<Heap *>(thread->GetEcmaVM()->GetHeap());
    NonMovableSpace *space = heap->GetNonMovableSpace();
    constexpr size_t TEST_SIZE = DEFAULT_REGION_SIZE;
    MemMap mem = MemMapAllocator::GetInstance()->Allocate(
        thread->GetThreadId(), TEST_SIZE, DEFAULT_REGION_SIZE,
        ToSpaceTypeName(space->GetSpaceType()),
        true, true, false, false, true, false);
    EXPECT_NE(mem.GetMem(), nullptr);
    EXPECT_EQ(mem.GetSize(), TEST_SIZE);
}

HWTEST_F_L0(MemMapAllocatorTest, InitialMemPool_CompressWithoutPageTag)
{
    Heap *heap = const_cast<Heap *>(thread->GetEcmaVM()->GetHeap());
    NonMovableSpace *space = heap->GetNonMovableSpace();
    constexpr size_t TEST_SIZE = DEFAULT_REGION_SIZE;
    MemMap mem = MemMapAllocator::GetInstance()->Allocate(
        thread->GetThreadId(), TEST_SIZE, DEFAULT_REGION_SIZE,
        ToSpaceTypeName(space->GetSpaceType()),
        true, true, false, false, false, false);
    EXPECT_NE(mem.GetMem(), nullptr);
    EXPECT_EQ(mem.GetSize(), TEST_SIZE);
}

HWTEST_F_L0(MemMapAllocatorTest, InitialMemPool_MachineCodeWithPageTag)
{
    Heap *heap = const_cast<Heap *>(thread->GetEcmaVM()->GetHeap());
    MachineCodeSpace *space = heap->GetMachineCodeSpace();
    constexpr size_t TEST_SIZE = DEFAULT_REGION_SIZE;
    MemMap mem = MemMapAllocator::GetInstance()->Allocate(
        thread->GetThreadId(), TEST_SIZE, DEFAULT_REGION_SIZE,
        ToSpaceTypeName(space->GetSpaceType()),
        true, false, true,
        Jit::GetInstance()->IsEnableJitFort(), true, false);
    EXPECT_NE(mem.GetMem(), nullptr);
    EXPECT_EQ(mem.GetSize(), TEST_SIZE);
}

HWTEST_F_L0(MemMapAllocatorTest, InitialMemPool_MachineCodeWithoutPageTag)
{
    Heap *heap = const_cast<Heap *>(thread->GetEcmaVM()->GetHeap());
    MachineCodeSpace *space = heap->GetMachineCodeSpace();
    constexpr size_t TEST_SIZE = DEFAULT_REGION_SIZE;
    MemMap mem = MemMapAllocator::GetInstance()->Allocate(
        thread->GetThreadId(), TEST_SIZE, DEFAULT_REGION_SIZE,
        ToSpaceTypeName(space->GetSpaceType()),
        true, false, true,
        Jit::GetInstance()->IsEnableJitFort(), false, false);
    EXPECT_NE(mem.GetMem(), nullptr);
    EXPECT_EQ(mem.GetSize(), TEST_SIZE);
}

HWTEST_F_L0(MemMapAllocatorTest, InitialMemPool_HugeObjectWithPageTag)
{
    Heap *heap = const_cast<Heap *>(thread->GetEcmaVM()->GetHeap());
    HugeObjectSpace *space = heap->GetHugeObjectSpace();
    constexpr size_t TEST_SIZE = 2_MB;
    MemMap mem = MemMapAllocator::GetInstance()->Allocate(
        thread->GetThreadId(), TEST_SIZE, DEFAULT_REGION_SIZE,
        ToSpaceTypeName(space->GetSpaceType()),
        false, false, false, false, true, false);
    EXPECT_EQ(mem.GetSize(), TEST_SIZE);
}

HWTEST_F_L0(MemMapAllocatorTest, InitialMemPool_HugeObjectWithoutPageTag)
{
    Heap *heap = const_cast<Heap *>(thread->GetEcmaVM()->GetHeap());
    HugeObjectSpace *space = heap->GetHugeObjectSpace();
    constexpr size_t TEST_SIZE = 2_MB;
    MemMap mem = MemMapAllocator::GetInstance()->Allocate(
        thread->GetThreadId(), TEST_SIZE, DEFAULT_REGION_SIZE,
        ToSpaceTypeName(space->GetSpaceType()),
        false, false, false, false, false, false);
    EXPECT_EQ(mem.GetSize(), TEST_SIZE);
}

HWTEST_F_L0(MemMapAllocatorTest, InitialMemPool_HugeMachineCodeWithPageTag)
{
    Heap *heap = const_cast<Heap *>(thread->GetEcmaVM()->GetHeap());
    HugeMachineCodeSpace *space = heap->GetHugeMachineCodeSpace();
    constexpr size_t TEST_SIZE = 2_MB;
    MemMap mem = MemMapAllocator::GetInstance()->Allocate(
        thread->GetThreadId(), TEST_SIZE, DEFAULT_REGION_SIZE,
        ToSpaceTypeName(space->GetSpaceType()),
        false, false, true,
        Jit::GetInstance()->IsEnableJitFort(), true, false);
    EXPECT_EQ(mem.GetSize(), TEST_SIZE);
}

HWTEST_F_L0(MemMapAllocatorTest, InitialMemPool_HugeMachineCodeWithoutPageTag)
{
    Heap *heap = const_cast<Heap *>(thread->GetEcmaVM()->GetHeap());
    HugeMachineCodeSpace *space = heap->GetHugeMachineCodeSpace();
    constexpr size_t TEST_SIZE = 2_MB;
    MemMap mem = MemMapAllocator::GetInstance()->Allocate(
        thread->GetThreadId(), TEST_SIZE, DEFAULT_REGION_SIZE,
        ToSpaceTypeName(space->GetSpaceType()),
        false, false, true,
        Jit::GetInstance()->IsEnableJitFort(), false, false);
    EXPECT_EQ(mem.GetSize(), TEST_SIZE);
}

#if ENABLE_HUGE_PAGE_MEM
HWTEST_F_L0(MemMapAllocatorTest, DisableHugePageNeverRetriesAnyVipFailure)
{
    auto state = DisableHugePageAndGetStateForTest(false, true, EAGAIN);
    EXPECT_FALSE(state.canTry);
    EXPECT_FALSE(state.enabled);

    state = DisableHugePageAndGetStateForTest(true, true, EAGAIN);
    EXPECT_FALSE(state.canTry);
    EXPECT_FALSE(state.enabled);

    state = DisableHugePageAndGetStateForTest(false, true, EINVAL);
    EXPECT_FALSE(state.canTry);
    EXPECT_FALSE(state.enabled);

    constexpr int VIP_NOT_SUPPORTED_ERRNO = 38;
    state = DisableHugePageAndGetStateForTest(true, true, VIP_NOT_SUPPORTED_ERRNO);
    EXPECT_FALSE(state.canTry);
    EXPECT_FALSE(state.enabled);
}

#if USE_COMPRESSED_HUGE_PAGE_POOL
HWTEST_F_L0(MemMapAllocatorTest, CompressedHugePagePoolTransfersWholeReservationToRegularPool)
{
    static_assert(COMPRESSED_HUGE_PAGE_POOL_CAPACITY == 200_MB);
    static_assert(COMPRESSED_HUGE_PAGE_POOL_CAPACITY / HugeMemMapPool::HUGE_REGION_SIZE == 100);
    constexpr size_t testPoolSize = 2 * HugeMemMapPool::HUGE_REGION_SIZE;
    MemMap reserved = PageMap(testPoolSize, PAGE_PROT_NONE, HugeMemMapPool::HUGE_REGION_SIZE);
    ASSERT_NE(reserved.GetMem(), nullptr);

    CompressedHugePagePool &compressedPool = GetCompressedHugePagePoolForTest();
    compressedPool.Initialize(reserved);
    GetHugeMemMapPoolForTest().SetCompressedBackingPool(&compressedPool);
    EXPECT_EQ(compressedPool.GetChunkCapacity(), 2U);

    MemMap checkedOut = compressedPool.AcquireChunk();
    ASSERT_NE(checkedOut.GetMem(), nullptr);
    EXPECT_EQ(compressedPool.ReleaseReservedMemory().GetMem(), nullptr);
    compressedPool.ReturnChunk(checkedOut);

    ReturnCompressedHugePagePoolToRegularPoolForTest();
    EXPECT_FALSE(compressedPool.HasReservedMemory());
    EXPECT_FALSE(allocator_.IsHugePageEnabled());

    MemMapPool &regularPool = GetRegularMemMapPoolForTest();
    constexpr size_t expectedRegionCount = testPoolSize / DEFAULT_REGION_SIZE;
    for (size_t index = 0; index < expectedRegionCount; ++index) {
        EXPECT_NE(regularPool.GetMemFromCache(DEFAULT_REGION_SIZE).GetMem(), nullptr);
    }
    EXPECT_EQ(regularPool.GetMemFromCache(DEFAULT_REGION_SIZE).GetMem(), nullptr);
}
#endif

HWTEST_F_L0(MemMapAllocatorTest, LinkedHugePoolReusableChunkFiltersTransientStates)
{
    EnableHugePageForTest();
    ASSERT_TRUE(MapAndInsertHugeChunk());
    MemMap slot = AllocateRegularRegion("old space");
    ASSERT_NE(slot.GetMem(), nullptr);
    uintptr_t slotAddr = reinterpret_cast<uintptr_t>(slot.GetMem());
    void *chunkBase = reinterpret_cast<void *>(
        slotAddr & ~(HugeMemMapPool::HUGE_REGION_SIZE - 1));

    ASSERT_TRUE(SetHugeChunkStateForTest(
        slot.GetMem(), 1U, HugeChunkDomain::LOCAL, false, false, true));
    EXPECT_EQ(FindReusableChunkInBucketsForTest(), nullptr);

    ASSERT_TRUE(SetHugeChunkStateForTest(
        slot.GetMem(), 1U, HugeChunkDomain::LOCAL, false, true, true));
    EXPECT_EQ(FindReusableChunkInBucketsForTest(), nullptr);

    ASSERT_TRUE(SetHugeChunkStateForTest(
        slot.GetMem(), 1U, HugeChunkDomain::LOCAL, false, true, false));
    EXPECT_EQ(FindReusableChunkInBucketsForTest(), nullptr);

    ASSERT_TRUE(SetHugeChunkStateForTest(
        slot.GetMem(), 1U, HugeChunkDomain::LOCAL, false, false, false));
    EXPECT_EQ(FindReusableChunkInBucketsForTest(), chunkBase);
}

HWTEST_F_L0(MemMapAllocatorTest, LinkedHugePoolTakeEmptyChunkSkipsTransientStates)
{
    EnableHugePageForTest();
    std::array<MemMap, 4> chunks;
    for (auto &chunk : chunks) {
        chunk = MapAndInsertHugeChunkForTest();
        ASSERT_NE(chunk.GetMem(), nullptr);
    }

    ASSERT_TRUE(SetHugeChunkStateForTest(
        chunks[0].GetMem(), 0, HugeChunkDomain::EMPTY, false, false, true));
    ASSERT_TRUE(SetHugeChunkStateForTest(
        chunks[1].GetMem(), 0, HugeChunkDomain::EMPTY, false, true, false));
    ASSERT_TRUE(SetHugeChunkStateForTest(
        chunks[2].GetMem(), 0, HugeChunkDomain::EMPTY, true, false, false));

    EXPECT_EQ(TakeEmptyChunkForTest(), chunks[3].GetMem());
    EXPECT_EQ(TakeEmptyChunkForTest(), chunks[2].GetMem());
    EXPECT_EQ(TakeEmptyChunkForTest(), nullptr);
}

HWTEST_F_L0(MemMapAllocatorTest, LinkedHugePoolSoftReleaseSkipsAdvisingAndReleasedChunks)
{
    EnableHugePageForTest();
    std::array<MemMap, 3> releaseChunks;
    for (auto &chunk : releaseChunks) {
        chunk = MapAndInsertHugeChunkForTest();
        ASSERT_NE(chunk.GetMem(), nullptr);
    }
    ASSERT_TRUE(SetHugeChunkStateForTest(
        releaseChunks[0].GetMem(), 0, HugeChunkDomain::EMPTY, false, true, false));
    ASSERT_TRUE(SetHugeChunkStateForTest(
        releaseChunks[1].GetMem(), 0, HugeChunkDomain::EMPTY, true, false, false));
    EXPECT_EQ(SoftReleaseEmptyChunksForTest(), 1U);
}

HWTEST_F_L0(MemMapAllocatorTest, LinkedHugePoolSummaryCountsReleasedAndResidentChunks)
{
    EnableHugePageForTest();
    std::array<MemMap, 3> summaryChunks;
    for (auto &chunk : summaryChunks) {
        chunk = MapAndInsertHugeChunkForTest();
        ASSERT_NE(chunk.GetMem(), nullptr);
    }
    MemMap activeSlot = AllocateRegularRegion("old space");
    ASSERT_NE(activeSlot.GetMem(), nullptr);
    ASSERT_TRUE(SetHugeChunkStateForTest(
        summaryChunks[2].GetMem(), 0, HugeChunkDomain::EMPTY, true, false, false));

    HugeSummaryForTest summary = CollectHugeSummaryForTest();
    EXPECT_EQ(summary.totalChunks, 3U);
    EXPECT_EQ(summary.activeChunks, 1U);
    EXPECT_EQ(summary.emptyChunks, 2U);
    EXPECT_EQ(summary.releasedEmptyChunks, 1U);
    EXPECT_EQ(summary.usedSlots, 1U);
}

HWTEST_F_L0(MemMapAllocatorTest, HugeSlotAllocationAndReleaseUseChunkAccounting)
{
    EnableHugePageForTest();
    ASSERT_TRUE(MapAndInsertHugeChunk());

    MemMap first = AllocateRegularRegion("old space", true);
    MemMap second = AllocateRegularRegion("old space");
    ASSERT_NE(first.GetMem(), nullptr);
    ASSERT_NE(second.GetMem(), nullptr);
    EXPECT_TRUE(IsHugePageSlotForTest(first.GetMem()));
    EXPECT_TRUE(IsHugePageSlotForTest(second.GetMem()));
    EXPECT_EQ(allocator_.GetTotalSize(), HugeMemMapPool::HUGE_REGION_SIZE);

    allocator_.CacheOrFree(first.GetMem(), first.GetSize(), true, false, 0, false, true);
    EXPECT_EQ(allocator_.GetTotalSize(), HugeMemMapPool::HUGE_REGION_SIZE);

    allocator_.CacheOrFree(second.GetMem(), second.GetSize(), true, false, 0, false, true);
    EXPECT_EQ(allocator_.GetTotalSize(), 0U);
}

HWTEST_F_L0(MemMapAllocatorTest, TryReleaseHugeSlotRejectsIneligibleMemory)
{
    EnableHugePageForTest();
    EXPECT_FALSE(TryReleaseHugeSlot(nullptr, false));
    EXPECT_FALSE(TryReleaseHugeSlot(nullptr, true));
}

HWTEST_F_L0(MemMapAllocatorTest, HugePageQuotaExceededFallsBackToRegularPool)
{
    EnableHugePageForTest();
    ConfigureHugePageStateForTest(true, 0);

    ASSERT_NE(InsertRegularRegionForTest().GetMem(), nullptr);
    ASSERT_NE(InsertRegularRegionForTest().GetMem(), nullptr);
    MemMap first = AllocateRegularRegion("old space");
    MemMap second = AllocateRegularRegion("old space");
    ASSERT_NE(first.GetMem(), nullptr);
    ASSERT_NE(second.GetMem(), nullptr);
    EXPECT_FALSE(IsHugePageSlotForTest(first.GetMem()));
    EXPECT_FALSE(IsHugePageSlotForTest(second.GetMem()));

    allocator_.CacheOrFree(first.GetMem(), first.GetSize(), true, false, 0, false, true);
    allocator_.CacheOrFree(second.GetMem(), second.GetSize(), true, false, 0, false, true);
}

HWTEST_F_L0(MemMapAllocatorTest, SemiAllocationCanSkipHugePage)
{
    EnableHugePageForTest();

    ASSERT_NE(InsertRegularRegionForTest().GetMem(), nullptr);
    MemMap mem = AllocateRegularRegion("semi space", false, false);
    ASSERT_NE(mem.GetMem(), nullptr);
    EXPECT_FALSE(IsHugePageSlotForTest(mem.GetMem()));

    allocator_.CacheOrFree(mem.GetMem(), mem.GetSize(), true, false, 0, false, true);
}

HWTEST_F_L0(MemMapAllocatorTest, HugePageSlotReleaseBatchOnlyForFullLikeGC)
{
    EXPECT_FALSE(allocator_.ShouldBatchHugePageSlotRelease(TriggerGCType::FULL_GC, false));
    EnableHugePageForTest();
    ASSERT_TRUE(MapAndInsertHugeChunk());

    EXPECT_FALSE(allocator_.ShouldBatchHugePageSlotRelease(TriggerGCType::YOUNG_GC, false));
    EXPECT_FALSE(allocator_.ShouldBatchHugePageSlotRelease(TriggerGCType::OLD_GC, false));
    EXPECT_TRUE(allocator_.ShouldBatchHugePageSlotRelease(TriggerGCType::FULL_GC, false));
    EXPECT_FALSE(allocator_.ShouldBatchHugePageSlotRelease(TriggerGCType::LOCAL_CC, false));

    EXPECT_FALSE(allocator_.ShouldBatchHugePageSlotRelease(TriggerGCType::SHARED_GC, true));
    EXPECT_FALSE(allocator_.ShouldBatchHugePageSlotRelease(TriggerGCType::SHARED_PARTIAL_GC, true));
    EXPECT_TRUE(allocator_.ShouldBatchHugePageSlotRelease(TriggerGCType::SHARED_FULL_GC, true));
    EXPECT_FALSE(allocator_.ShouldBatchHugePageSlotRelease(TriggerGCType::SHARED_CC, true));
}

HWTEST_F_L0(MemMapAllocatorTest, HugePageFacadeForwardsPoolOperations)
{
    EnableHugePageForTest();
    ASSERT_TRUE(MapAndInsertHugeChunk());
    MemMap slot = AllocateRegularRegion("semi space");
    ASSERT_NE(slot.GetMem(), nullptr);

#if ENABLE_HUGEPAGE_DFX_STATS
    allocator_.NotifyHugeSlotMoved(slot.GetMem(), HugePageTag::OLD);
#endif
    allocator_.BeginHugePageSlotReleaseBatch();
    allocator_.CacheOrFree(slot.GetMem(), slot.GetSize(), true, false, 0, false, true);
    allocator_.EndHugePageSlotReleaseBatch();
    EXPECT_EQ(allocator_.GetTotalSize(), 0U);
    allocator_.NotifyGCReclaimFinished(TriggerGCType::FULL_GC);
    allocator_.NotifyApplicationForeground();
    allocator_.DumpGlobalMemUsageSummaryDfx();

    MemMapAllocator::GetInstance()->NotifyApplicationBackground();
    MemMapAllocator::GetInstance()->NotifyApplicationForeground();
}
#endif
}  // namespace panda::test
