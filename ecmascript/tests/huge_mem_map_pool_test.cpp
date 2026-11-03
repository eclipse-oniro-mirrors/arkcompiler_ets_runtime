/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <array>
#include <atomic>
#include <cerrno>
#include <fstream>
#include <functional>
#include <limits>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "ecmascript/log_wrapper.h"

namespace panda::ecmascript {
// Keep this white-box test independent of mem.h and tagged-object headers from
// runtime_core. The value must stay aligned with mem.h::DEFAULT_REGION_SIZE.
[[maybe_unused]] static constexpr size_t DEFAULT_REGION_SIZE = 1U << 18U;

namespace base {
template<class T>
uintptr_t ToUintPtr(T *ptr)
{
    return reinterpret_cast<uintptr_t>(ptr);
}
}  // namespace base
}  // namespace panda::ecmascript

#define ECMASCRIPT_ECMA_MACROS_H
#define ECMASCRIPT_MEM_C_STRING_H
#define HUGE_MEM_MAP_POOL_STANDALONE_TEST
// This standalone white-box target compiles the pool implementation directly.
// Enable optional DFX code here so its branches remain covered even when the
// host runtime library is built with huge-page DFX disabled.
#undef ENABLE_HUGEPAGE_DFX_STATS
#define ENABLE_HUGEPAGE_DFX_STATS 1
#include "ecmascript/mem/huge_mem_map_pool.h"
#undef HUGE_MEM_MAP_POOL_STANDALONE_TEST

#if ENABLE_HUGE_PAGE_MEM
#include <sys/mman.h>

namespace panda::test {
enum class HugeMemMapPoolTestInputMode {
    OPEN_FAILURE,
    READ_FAILURE,
    ZERO,
    SUB_PAGE,
    TWO_PAGES,
};

inline HugeMemMapPoolTestInputMode g_inputMode = HugeMemMapPoolTestInputMode::OPEN_FAILURE;
inline bool g_failPageMap = false;
inline void *g_pageMapOverride = nullptr;
inline std::function<void(void *)> g_pageTagCallback;
inline std::function<void(void *)> g_pageReleaseCallback;
inline int g_madviseResult = 0;
inline int g_madviseErrno = 0;
inline size_t g_madviseCallCount = 0;
inline std::function<void(void *)> g_madviseCallback;

int HugeMemMapPoolTestMadvise([[maybe_unused]] void *addr, [[maybe_unused]] size_t size,
                              [[maybe_unused]] int advice)
{
    g_madviseCallCount++;
    if (g_madviseCallback) {
        auto callback = g_madviseCallback;
        callback(addr);
    }
    errno = g_madviseErrno;
    return g_madviseResult;
}

class HugeMemMapPoolTestLog {
public:
    template<class T>
    HugeMemMapPoolTestLog &operator<<(const T &)
    {
        return *this;
    }
};
}  // namespace panda::test

namespace std {
class HugeMemMapPoolTestInputFile {
public:
    explicit HugeMemMapPoolTestInputFile([[maybe_unused]] const char *path) {}

    bool is_open() const
    {
        return panda::test::g_inputMode != panda::test::HugeMemMapPoolTestInputMode::OPEN_FAILURE;
    }

    HugeMemMapPoolTestInputFile &operator>>(size_t &value)
    {
        switch (panda::test::g_inputMode) {
            case panda::test::HugeMemMapPoolTestInputMode::ZERO:
                value = 0;
                break;
            case panda::test::HugeMemMapPoolTestInputMode::SUB_PAGE:
                value = 1;
                break;
            case panda::test::HugeMemMapPoolTestInputMode::TWO_PAGES:
                value = 2 * 1024 * 1024;
                break;
            case panda::test::HugeMemMapPoolTestInputMode::OPEN_FAILURE:
            case panda::test::HugeMemMapPoolTestInputMode::READ_FAILURE:
                break;
        }
        return *this;
    }

    bool fail() const
    {
        return panda::test::g_inputMode == panda::test::HugeMemMapPoolTestInputMode::READ_FAILURE;
    }
};
}  // namespace std

namespace panda::ecmascript {
MemMap PageMapForHugeMemMapPoolTest(size_t size, int prot, size_t alignment, void *addr = nullptr,
                                    int flags = 0, [[maybe_unused]] bool jitfort = false)
{
    if (panda::test::g_failPageMap) {
        return MemMap();
    }
    if (panda::test::g_pageMapOverride != nullptr) {
        return MemMap(panda::test::g_pageMapOverride, size);
    }
    size_t allocSize = size + alignment;
    int mmapFlags = MAP_PRIVATE | MAP_ANONYMOUS | flags;
    void *result = mmap(addr, allocSize, prot, mmapFlags, -1, 0);
    if (result == MAP_FAILED) {
        return MemMap();
    }
    if (alignment == 0) {
        return MemMap(result, size);
    }

    uintptr_t resultAddr = reinterpret_cast<uintptr_t>(result);
    uintptr_t alignedAddr = (resultAddr + alignment - 1) & ~(alignment - 1);
    size_t leftSize = alignedAddr - resultAddr;
    size_t rightSize = allocSize - leftSize - size;
    if (leftSize != 0) {
        munmap(result, leftSize);
    }
    if (rightSize != 0) {
        munmap(reinterpret_cast<void *>(alignedAddr + size), rightSize);
    }
    return MemMap(reinterpret_cast<void *>(alignedAddr), size);
}

template<class T>
bool IsAligned(T value, size_t alignment)
{
    return alignment != 0 && static_cast<uintptr_t>(value) % alignment == 0;
}
}  // namespace panda::ecmascript

#undef LOG_GC
#define LOG_GC(level) panda::test::HugeMemMapPoolTestLog()
#define ifstream HugeMemMapPoolTestInputFile
#define madvise panda::test::HugeMemMapPoolTestMadvise
#define PageMap PageMapForHugeMemMapPoolTest
#ifndef ECMA_BYTRACE_NAME
#define ECMA_BYTRACE_NAME(level, tag, name, customArgs)
#endif
#define ECMASCRIPT_MEM_REGION_H
#include "ecmascript/mem/huge_mem_map_pool.cpp"
#undef ECMASCRIPT_MEM_REGION_H
#undef PageMap
#undef madvise
#undef ifstream
#undef ECMASCRIPT_MEM_C_STRING_H
#undef ECMASCRIPT_ECMA_MACROS_H
#undef LOG_GC

using namespace panda::ecmascript;
using namespace panda::ecmascript::base;

namespace panda::ecmascript {
int PrctlSetVMA([[maybe_unused]] const void *p, [[maybe_unused]] size_t size,
                [[maybe_unused]] const char *tag)
{
    return 0;
}

void PageUnmap(MemMap memMap, [[maybe_unused]] bool jitfort)
{
    if (memMap.GetMem() != nullptr) {
        munmap(memMap.GetMem(), memMap.GetSize());
    }
}

int PageProtect(void *mem, size_t size, int prot)
{
    return mprotect(mem, size, prot);
}

void PageTag([[maybe_unused]] void *mem, [[maybe_unused]] size_t size,
             [[maybe_unused]] PageTagType type, [[maybe_unused]] const std::string &spaceName,
             [[maybe_unused]] uint32_t threadId)
{
    if (panda::test::g_pageTagCallback) {
        panda::test::g_pageTagCallback(mem);
    }
}

void PageRelease(void *mem, size_t size)
{
    madvise(mem, size, MADV_DONTNEED);
    if (panda::test::g_pageReleaseCallback) {
        panda::test::g_pageReleaseCallback(mem);
    }
}
}  // namespace panda::ecmascript

namespace panda::test {

class HugeMemMapPoolTest : public testing::Test {
protected:
    static MemMap MapHugeChunk()
    {
        return panda::ecmascript::PageMapForHugeMemMapPoolTest(
            HugeMemMapPool::HUGE_REGION_SIZE, PAGE_PROT_READWRITE, HugeMemMapPool::HUGE_REGION_SIZE);
    }

    static MemMap AllocateHugeChunk(HugeMemMapPool &pool, size_t alignment)
    {
        return pool.AllocateHugeChunk(alignment);
    }

    static void ReleaseHugeChunkMemory(HugeMemMapPool &pool, const MemMap &memMap)
    {
        pool.ReleaseHugeChunkMemory(memMap);
    }

#if USE_COMPRESSED_HUGE_PAGE_POOL
    static MemMap MapCompressedBacking(size_t chunkCount)
    {
        return panda::ecmascript::PageMapForHugeMemMapPoolTest(
            chunkCount * HugeMemMapPool::HUGE_REGION_SIZE, PAGE_PROT_NONE,
            HugeMemMapPool::HUGE_REGION_SIZE);
    }

    static void InitializeCompressedBacking(HugeMemMapPool &pool, CompressedHugePagePool &backing,
                                            const MemMap &reserved)
    {
        backing.Initialize(reserved);
        pool.SetCompressedBackingPool(&backing);
    }

    static void ClearCompressedFreeChunksForTest(CompressedHugePagePool &backing)
    {
        LockHolder lock(backing.lock_);
        backing.freeChunks_.clear();
    }
#endif

    static bool InsertHugeChunk(HugeMemMapPool &pool, const MemMap &memMap,
                                bool ownsQuotaReservation = false)
    {
        if (!pool.InsertHugeChunk(memMap)) {
            return false;
        }
        auto it = pool.chunkMap_.find(ToUintPtr(memMap.GetMem()));
        if (it == pool.chunkMap_.end()) {
            return false;
        }
        it->second.ownsQuotaReservation_ = ownsQuotaReservation;
        return true;
    }

    static const char *GetSpaceName(HugePageTag tag)
    {
        switch (tag) {
            case HugePageTag::SEMI:
                return "semi space";
            case HugePageTag::OLD:
                return "old space";
            case HugePageTag::SHARED_OLD:
                return "shared old space";
            default:
                return "unknown space";
        }
    }

    static MemMap GetHugeSlot(HugeMemMapPool &pool, HugePageTag tag,
                              HugeSlotAllocPolicy policy = HugeSlotAllocPolicy::DEFAULT)
    {
        return pool.GetHugeSlot(tag, GetSpaceName(tag), false, nullptr, policy);
    }

    static MemMap GetHugeSlotWithDelta(HugeMemMapPool &pool, HugePageTag tag, size_t *memMapSizeDelta,
                                       HugeSlotAllocPolicy policy = HugeSlotAllocPolicy::DEFAULT)
    {
        return pool.GetHugeSlot(tag, GetSpaceName(tag), false, memMapSizeDelta, policy);
    }

    static HugeSlotAllocResult TryAllocateSlot(
        HugeMemMapPool &pool, HugePageTag tag,
        HugeSlotAllocPolicy policy = HugeSlotAllocPolicy::DEFAULT)
    {
        return pool.TryAllocateSlot(tag, GetSpaceName(tag), DEFAULT_REGION_SIZE, policy);
    }

    static bool ReleaseHugeSlot(HugeMemMapPool &pool, void *mem, size_t *memMapSizeDelta = nullptr)
    {
        return pool.ReleaseSlotIfOwned(mem, DEFAULT_REGION_SIZE, memMapSizeDelta);
    }

    static size_t SoftReleaseEmptyChunks(HugeMemMapPool &pool, size_t *memMapSizeDelta = nullptr)
    {
        return pool.SoftReleaseEmptyChunks(memMapSizeDelta);
    }

    static size_t GetReleasedEmptyChunkCount(const HugeMemMapPool &pool)
    {
        size_t releasedCount = 0;
        for (const auto *chunk : pool.emptyChunks_) {
            if (chunk->isPhysicalReleased_) {
                releasedCount++;
            }
        }
        return releasedCount;
    }

    struct SummaryStats {
        size_t totalChunks;
        size_t activeChunks;
        size_t emptyChunks;
        size_t releasedEmptyChunks;
        size_t usedSlots;
        size_t activeSlotCapacity;
    };

    static SummaryStats CollectSummaryDfx(HugeMemMapPool &pool)
    {
        LockHolder lock(pool.lock_);
        auto stats = pool.CollectSummaryDfxLocked();
        return {stats.totalChunks, stats.activeChunks, stats.emptyChunks,
                stats.releasedEmptyChunks, stats.usedSlots, stats.activeSlotCapacity};
    }

    static size_t GetHugeChunkCount(const HugeMemMapPool &pool)
    {
        return pool.chunkMap_.size();
    }

    static size_t GetFreeSlotRefillLimit(size_t quotaLimit)
    {
        constexpr size_t MAX_FREE_SLOT_PERCENT_FOR_REFILL = 20;
        constexpr size_t PERCENT_FACTOR = 100;
        return quotaLimit * HugeMemMapPool::SLOTS_PER_CHUNK *
               MAX_FREE_SLOT_PERCENT_FOR_REFILL / PERCENT_FACTOR;
    }

    static bool CanRefillHugeChunk(HugeMemMapPool &pool, size_t quotaLimit, bool &quotaExceeded)
    {
        return pool.CanRefillHugeChunk(quotaLimit, GetFreeSlotRefillLimit(quotaLimit), quotaExceeded);
    }

    static bool CanRefillHugeChunk(HugeMemMapPool &pool, size_t quotaLimit,
                                   size_t freeSlotRefillLimit, bool &quotaExceeded)
    {
        return pool.CanRefillHugeChunk(quotaLimit, freeSlotRefillLimit, quotaExceeded);
    }

    static bool CanRefillHugeChunk(HugeMemMapPool &pool, size_t quotaLimit,
                                   size_t freeSlotRefillLimit, HugePageTag tag,
                                   bool &quotaExceeded)
    {
        return pool.CanRefillHugeChunk(quotaLimit, freeSlotRefillLimit, quotaExceeded,
                                       tag, HugeSlotAllocPolicy::DEFAULT);
    }

    static bool TryReserveQuotaForSlot(HugeMemMapPool &pool, HugePageTag tag,
                                       HugeSlotAllocPolicy policy, bool &quotaExceeded)
    {
        return pool.TryReserveQuotaForSlot(tag, policy, quotaExceeded);
    }

    static auto GetChunk(HugeMemMapPool &pool, void *mem)
    {
        uintptr_t baseAddr = ToUintPtr(mem) & ~(HugeMemMapPool::HUGE_REGION_SIZE - 1);
        auto it = pool.chunkMap_.find(baseAddr);
        return it == pool.chunkMap_.end() ? nullptr : &it->second;
    }

    static bool HasActiveSlotWithTag(HugeMemMapPool &pool, void *mem, HugePageTag tag)
    {
        auto *chunk = GetChunk(pool, mem);
        return chunk != nullptr && pool.HasActiveSlotWithTag(*chunk, tag);
    }

    static size_t CountActiveSlotsWithTag(HugeMemMapPool &pool, void *mem, HugePageTag tag)
    {
        auto *chunk = GetChunk(pool, mem);
        return chunk == nullptr ? 0 : pool.CountActiveSlotsWithTag(*chunk, tag);
    }

    static bool HasPromotedSlot(HugeMemMapPool &pool, void *mem)
    {
        auto *chunk = GetChunk(pool, mem);
        if (chunk == nullptr) {
            return false;
        }
        for (size_t i = 0; i < HugeMemMapPool::SLOTS_PER_CHUNK; ++i) {
            if ((chunk->usageMask_ & (1U << i)) != 0 &&
                chunk->slotTags_[i] == static_cast<char>(HugePageTag::PROMOTED)) {
                return true;
            }
        }
        return false;
    }

    static bool IsHugeChunkWithSingleActiveSlot(HugeMemMapPool &pool, void *mem, HugePageTag tag)
    {
        return IsHugeChunkWithActiveSlotCountAtMost(pool, mem, tag, 1);
    }

    static bool IsHugeChunkWithActiveSlotCountAtMost(HugeMemMapPool &pool, void *mem, HugePageTag tag,
                                                     size_t maxActiveSlots)
    {
        if (mem == nullptr || tag == HugePageTag::INVALID ||
            maxActiveSlots == 0 || maxActiveSlots > HugeMemMapPool::SLOTS_PER_CHUNK) {
            return false;
        }
        uintptr_t addr = ToUintPtr(mem);
        uintptr_t baseAddr = addr & ~(HugeMemMapPool::HUGE_REGION_SIZE - 1);
        auto it = pool.chunkMap_.find(baseAddr);
        if (it == pool.chunkMap_.end()) {
            return false;
        }
        size_t activeSlotCount = pool.CountActiveSlotsWithTag(it->second, tag);
        if (activeSlotCount == 0 || activeSlotCount > maxActiveSlots) {
            return false;
        }
        size_t offset = addr - baseAddr;
        if (offset >= HugeMemMapPool::HUGE_REGION_SIZE) {
            return false;
        }
        size_t index = offset / DEFAULT_REGION_SIZE;
        char actualTag = it->second.slotTags_[index];
        return (it->second.usageMask_ & (1U << index)) != 0 &&
               (actualTag == static_cast<char>(tag) ||
                (tag == HugePageTag::OLD &&
                 actualTag == static_cast<char>(HugePageTag::PROMOTED)));
    }

#if ENABLE_HUGEPAGE_DFX_STATS
    static void NotifyHugeSlotMoved(HugeMemMapPool &pool, void *mem, HugePageTag tag)
    {
        pool.NotifySlotMoved(mem, tag);
    }
#endif

    static void SetBackgroundState(HugeMemMapPool &pool, bool isInBackground)
    {
        pool.isInBackground_.store(isInBackground, std::memory_order_release);
    }

    static void SetBackgroundStartTime(HugeMemMapPool &pool, uint64_t backgroundStartTimeUs)
    {
        pool.backgroundStartTimeUs_.store(backgroundStartTimeUs, std::memory_order_release);
    }

    static bool IsInBackground(const HugeMemMapPool &pool)
    {
        return pool.isInBackground_.load(std::memory_order_acquire);
    }

    static uint64_t GetBackgroundStartTime(const HugeMemMapPool &pool)
    {
        return pool.backgroundStartTimeUs_.load(std::memory_order_acquire);
    }

    static void SetHasAdvisedAllCold(HugeMemMapPool &pool, bool hasAdvised)
    {
        pool.hasAdvisedAllColdInBackground_.store(hasAdvised, std::memory_order_release);
    }

    static bool HasAdvisedAllCold(const HugeMemMapPool &pool)
    {
        return pool.hasAdvisedAllColdInBackground_.load(std::memory_order_acquire);
    }

    static void TryMadviseColdChunksInBackground(HugeMemMapPool &pool)
    {
        pool.TryMadviseColdChunksInBackground(GetBackgroundStartTime(pool));
    }

    static char GetChunkOwnerDomain(const HugeMemMapPool &pool, void *mem)
    {
        uintptr_t baseAddr = ToUintPtr(mem) & ~(HugeMemMapPool::HUGE_REGION_SIZE - 1);
        switch (pool.chunkMap_.at(baseAddr).ownerDomain_) {
            case HugeChunkDomain::EMPTY:
                return '_';
            case HugeChunkDomain::LOCAL:
                return 'L';
            case HugeChunkDomain::SHARED_OLD:
                return 'D';
            default:
                return '?';
        }
    }

    static char GetSlotTag(const HugeMemMapPool &pool, void *mem)
    {
        uintptr_t addr = ToUintPtr(mem);
        uintptr_t baseAddr = addr & ~(HugeMemMapPool::HUGE_REGION_SIZE - 1);
        size_t index = (addr - baseAddr) / DEFAULT_REGION_SIZE;
        return pool.chunkMap_.at(baseAddr).slotTags_[index];
    }

    static bool RemoveHugeChunk(HugeMemMapPool &pool, const MemMap &memMap)
    {
        return pool.RemoveHugeChunk(memMap);
    }

    static auto TakeEmptyChunk(HugeMemMapPool &pool)
    {
        return pool.TakeEmptyChunkLocked();
    }

    template<class ChunkPtr>
    static void InsertPartialChunk(HugeMemMapPool &pool, ChunkPtr chunk)
    {
        pool.InsertPartialChunkLocked(chunk);
    }

    template<class ChunkPtr>
    static void RemovePartialChunk(HugeMemMapPool &pool, ChunkPtr chunk)
    {
        pool.RemovePartialChunkLocked(chunk);
    }

    template<class ChunkPtr>
    static MemMap OccupyFreeSlot(HugeMemMapPool &pool, ChunkPtr chunk,
                                 HugePageTag tag, size_t *memMapSizeDelta = nullptr)
    {
        return pool.OccupyFreeSlotLocked(chunk, tag, memMapSizeDelta);
    }

    template<class ChunkPtr>
    static bool FreeHugeSlot(HugeMemMapPool &pool, void *mem, ChunkPtr chunk,
                             uintptr_t baseAddr, bool &isChunkEmpty)
    {
        return pool.FreeHugeSlotLocked(mem, chunk, baseAddr, isChunkEmpty);
    }

    template<class ChunkPtr>
    static void UpdateChunkAfterSlotFree(HugeMemMapPool &pool, ChunkPtr chunk)
    {
        pool.UpdateChunkAfterSlotFreeLocked(chunk);
    }

    template<class ChunkPtr>
    static void QuarantineChunkForRelease(HugeMemMapPool &pool, ChunkPtr chunk)
    {
        pool.QuarantineChunkForReleaseLocked(chunk);
    }

    static bool RestoreQuarantinedChunks(HugeMemMapPool &pool)
    {
        return pool.RestoreQuarantinedChunksLocked();
    }

    template<class ChunkPtr>
    static void ResetEmptyChunk(HugeMemMapPool &pool, ChunkPtr chunk)
    {
        pool.ResetEmptyChunkLocked(chunk);
    }

#if ENABLE_HUGEPAGE_DFX_STATS
    template<class ChunkPtr>
    static void UpdateSlotTagAfterMove(HugeMemMapPool &pool, ChunkPtr chunk,
                                       size_t index, HugePageTag tag)
    {
        pool.UpdateSlotTagAfterMoveLocked(chunk, index, tag);
    }
#endif

    static bool ReleaseSingleEmptyChunk(HugeMemMapPool &pool, uintptr_t addr, size_t &releasedMemMapSize)
    {
        return pool.ReleaseSingleEmptyChunkLocked(addr, releasedMemMapSize);
    }

    template<class ChunkType>
    static bool IsCurrentGcTargetChunk(HugeMemMapPool &pool, const ChunkType &chunk,
                                       HugePageTag tag, HugeSlotAllocPolicy policy)
    {
        LockHolder lock(pool.lock_);
        return pool.IsCurrentGcTargetChunkLocked(chunk, tag, policy);
    }

    static bool ShouldMarkGcTarget(HugeMemMapPool &pool, HugePageTag tag, HugeSlotAllocPolicy policy)
    {
        return pool.ShouldMarkGcTarget(tag, policy);
    }

    static auto FindReusableChunkInBuckets(HugeMemMapPool &pool, HugePageTag tag,
                                           HugeSlotAllocPolicy policy, bool acceptDomain)
    {
        std::function<bool(const HugeMemMapPool::Chunk &, HugeChunkDomain)> predicate =
            [acceptDomain]([[maybe_unused]] const HugeMemMapPool::Chunk &chunk,
                           [[maybe_unused]] HugeChunkDomain targetDomain) {
            return acceptDomain;
        };
        LockHolder lock(pool.lock_);
        return pool.FindReusableChunkInBucketsLocked(tag, policy, predicate);
    }

    static auto FindReusableChunk(HugeMemMapPool &pool, HugePageTag tag,
                                  HugeSlotAllocPolicy policy)
    {
        LockHolder lock(pool.lock_);
        return pool.FindReusableChunkLocked(tag, policy);
    }

    static bool IsFreeSlotRefillLimitReached(HugeMemMapPool &pool, size_t freeSlotRefillLimit)
    {
        LockHolder lock(pool.lock_);
        return pool.IsFreeSlotRefillLimitReachedLocked(freeSlotRefillLimit);
    }

    template<class ChunkPtr>
    static void MarkGcTargetChunk(HugeMemMapPool &pool, ChunkPtr chunk,
                                  HugePageTag tag, HugeSlotAllocPolicy policy)
    {
        LockHolder lock(pool.lock_);
        pool.MarkGcTargetChunkLocked(chunk, tag, policy);
    }

    template<class ChunkType>
    static bool ShouldMadviseChunk(HugeMemMapPool &pool, const ChunkType &chunk,
                                   int advice, char ownerTag)
    {
        return pool.ShouldMadviseChunkLocked(chunk, advice, ownerTag);
    }

    static std::vector<uintptr_t> CollectMadviseTargets(HugeMemMapPool &pool, size_t maxChunks,
                                                        int advice, char ownerTag)
    {
        return pool.CollectMadviseTargets(maxChunks, advice, ownerTag);
    }

    static void UpdateMadviseState(HugeMemMapPool &pool, const std::vector<uintptr_t> &targets,
                                   int advice, char ownerTag)
    {
        pool.UpdateMadviseState(targets, advice, ownerTag);
    }

    static void MadviseActiveResidentChunks(HugeMemMapPool &pool, size_t maxChunks, int advice,
                                            const char *actionName, char ownerTag = '\0')
    {
        pool.MadviseActiveResidentChunks(maxChunks, advice, actionName, ownerTag);
    }

    static void NotifyBackgroundColdTimeout(HugeMemMapPool &pool, uint64_t backgroundStartTimeUs)
    {
        pool.NotifyBackgroundColdTimeout(backgroundStartTimeUs);
    }

    static void PostBackgroundColdTask(HugeMemMapPool &pool, uint64_t backgroundStartTimeUs)
    {
        pool.PostBackgroundColdTask(backgroundStartTimeUs);
    }

    static size_t GetPartialChunkCount(const HugeMemMapPool &pool)
    {
        size_t count = 0;
        for (const auto &bucket : pool.partialChunks_) {
            count += bucket.size();
        }
        return count;
    }

    struct ChunkState {
        uint8_t usageMask;
        HugeChunkDomain domain;
        bool physicalReleased {false};
        bool advising {false};
        bool coldAdvised {false};
        bool reuseQuarantined {false};
    };

    template<class ChunkPtr>
    static void SetChunkState(ChunkPtr chunk, const ChunkState &state)
    {
        chunk->usageMask_ = state.usageMask;
        chunk->ownerDomain_ = state.domain;
        chunk->isPhysicalReleased_ = state.physicalReleased;
        chunk->isAdvising_ = state.advising;
        chunk->isColdAdvised_ = state.coldAdvised;
        chunk->isReuseQuarantined_ = state.reuseQuarantined;
    }

    template<class ChunkPtr>
    static void SetSlotTag(ChunkPtr chunk, size_t index, HugePageTag tag)
    {
        chunk->slotTags_[index] = static_cast<char>(tag);
    }

    template<class ChunkPtr>
    static void SetMemMapCounted(ChunkPtr chunk, bool counted)
    {
        chunk->isMemMapCounted_ = counted;
    }

    template<class ChunkType>
    static bool IsPhysicalReleased(const ChunkType &chunk)
    {
        return chunk.isPhysicalReleased_;
    }

    template<class ChunkType>
    static bool IsColdAdvised(const ChunkType &chunk)
    {
        return chunk.isColdAdvised_;
    }

    template<class ChunkType>
    static bool IsReuseQuarantined(const ChunkType &chunk)
    {
        return chunk.isReuseQuarantined_;
    }

    template<class ChunkType>
    static bool IsAdvising(const ChunkType &chunk)
    {
        return chunk.isAdvising_;
    }

    template<class ChunkType>
    static HugeChunkDomain GetOwnerDomain(const ChunkType &chunk)
    {
        return chunk.ownerDomain_;
    }

    template<class ChunkPtr>
    static void SetOwnerDomain(ChunkPtr chunk, HugeChunkDomain domain)
    {
        chunk->ownerDomain_ = domain;
    }

    template<class ChunkPtr>
    static void SetPhysicalReleased(ChunkPtr chunk, bool physicalReleased)
    {
        chunk->isPhysicalReleased_ = physicalReleased;
    }

    template<class ChunkPtr>
    static void SetUsageMask(ChunkPtr chunk, uint8_t usageMask)
    {
        chunk->usageMask_ = usageMask;
    }

    template<class ChunkPtr>
    static void SetUsageMaskWithLock(HugeMemMapPool &pool, ChunkPtr chunk, uint8_t usageMask)
    {
        LockHolder lock(pool.lock_);
        chunk->usageMask_ = usageMask;
    }

    template<class ChunkType>
    static uint64_t GetChunkLocalTargetEpoch(const ChunkType &chunk)
    {
        return chunk.localFullGcTargetEpoch_;
    }

    template<class ChunkType>
    static uint64_t GetChunkSharedTargetEpoch(const ChunkType &chunk)
    {
        return chunk.sharedFullGcTargetEpoch_;
    }

    static uint64_t GetLocalTargetEpoch(HugeMemMapPool &pool)
    {
        LockHolder lock(pool.lock_);
        return pool.localFullGcTargetEpoch_;
    }

    static uint64_t GetSharedTargetEpoch(HugeMemMapPool &pool)
    {
        LockHolder lock(pool.lock_);
        return pool.sharedFullGcTargetEpoch_;
    }

    template<class ChunkPtr>
    static void SetChunkTargetEpochs(ChunkPtr chunk, uint64_t localEpoch, uint64_t sharedEpoch)
    {
        chunk->localFullGcTargetEpoch_ = localEpoch;
        chunk->sharedFullGcTargetEpoch_ = sharedEpoch;
    }

    struct ProcessState {
        bool initialized;
        bool canTry;
        bool enabled;
        size_t quotaLimit;
        size_t freeSlotRefillLimit;
        size_t allocatedHugePages;
    };

    static void SetProcessState(const ProcessState &state)
    {
        ecmascript::g_systemHugePageLimit.store(state.quotaLimit, std::memory_order_release);
        ecmascript::g_freeSlotRefillLimit.store(state.freeSlotRefillLimit, std::memory_order_release);
        ecmascript::g_canTryHugePage.store(state.canTry, std::memory_order_release);
        ecmascript::g_isEnableHugePage.store(state.enabled, std::memory_order_release);
        ecmascript::g_allocatedHugePages.store(state.allocatedHugePages, std::memory_order_release);
        ecmascript::g_hugePageLimitInitialized.store(state.initialized, std::memory_order_release);
    }

    static bool CanTryHugePage()
    {
        return ecmascript::CanTryHugePage();
    }

    static bool IsHugePageEnabled()
    {
        return ecmascript::IsHugePageEnabled();
    }

    static void MarkHugePageEnabled()
    {
        ecmascript::MarkHugePageEnabledForCurrentProcess("unit test");
    }

    static void DisableHugePage(int err)
    {
        ecmascript::DisableHugePageForCurrentProcess("unit test", err);
    }

    static bool TryReserveHugePageQuota(size_t quotaLimit)
    {
        return ecmascript::TryReserveHugePageQuota(quotaLimit);
    }

    static void ReleaseReservedHugePageQuota(size_t count)
    {
        ecmascript::ReleaseReservedHugePageQuota(count);
    }

    static size_t GetAllocatedHugePageCount()
    {
        return ecmascript::GetAllocatedHugePageCount();
    }

    static HugeChunkDomain ToChunkDomain(HugePageTag tag)
    {
        return ecmascript::ToChunkDomain(tag);
    }

    static bool CanReuseChunkDomain(HugeChunkDomain ownerDomain, HugePageTag tag,
                                    HugeSlotAllocPolicy policy)
    {
        return ecmascript::CanReuseChunkDomain(ownerDomain, tag, policy);
    }

    static bool DoesSlotTagMatch(HugePageTag slotTag, HugePageTag targetTag)
    {
        return ecmascript::DoesSlotTagMatch(static_cast<char>(slotTag), targetTag);
    }

    static size_t CalculateFreeSlotRefillLimit(size_t quotaLimit)
    {
        return ecmascript::CalculateFreeSlotRefillLimit(quotaLimit);
    }

    static bool RunBackgroundColdTask(HugeMemMapPool *pool, uint64_t backgroundStartTimeUs)
    {
        HugeMemMapPool::BackgroundColdTask task(pool, backgroundStartTimeUs);
        return task.Run(0);
    }

    void TearDown() override
    {
        g_inputMode = HugeMemMapPoolTestInputMode::OPEN_FAILURE;
        g_failPageMap = false;
        g_pageMapOverride = nullptr;
        g_pageTagCallback = {};
        g_pageReleaseCallback = {};
        g_madviseResult = 0;
        g_madviseErrno = 0;
        g_madviseCallCount = 0;
        g_madviseCallback = {};
        SetProcessState({false, false, false, 0, 0, 0});
        testing::Test::TearDown();
    }
};

TEST_F(HugeMemMapPoolTest, LocalHugeSlotsCanReuseSemiOldAndPromotedChunks)
{
    HugeMemMapPool pool;
    MemMap semiChunk = MapHugeChunk();
    ASSERT_NE(semiChunk.GetMem(), nullptr);
    uintptr_t semiBase = ToUintPtr(semiChunk.GetMem());
    ASSERT_TRUE(InsertHugeChunk(pool, semiChunk));

    MemMap semiSlot = GetHugeSlot(pool, HugePageTag::SEMI);
    ASSERT_NE(semiSlot.GetMem(), nullptr);
    EXPECT_EQ(ToUintPtr(semiSlot.GetMem()) & ~(HugeMemMapPool::HUGE_REGION_SIZE - 1), semiBase);

    MemMap oldInSemiChunk = GetHugeSlot(pool, HugePageTag::OLD);
    ASSERT_NE(oldInSemiChunk.GetMem(), nullptr);
    EXPECT_EQ(ToUintPtr(oldInSemiChunk.GetMem()) & ~(HugeMemMapPool::HUGE_REGION_SIZE - 1), semiBase);
    EXPECT_EQ(GetChunkOwnerDomain(pool, semiSlot.GetMem()), 'L');
    EXPECT_EQ(GetSlotTag(pool, semiSlot.GetMem()), 'S');
    EXPECT_EQ(GetSlotTag(pool, oldInSemiChunk.GetMem()), 'O');

    MemMap oldChunk = MapHugeChunk();
    ASSERT_NE(oldChunk.GetMem(), nullptr);
    uintptr_t oldBase = ToUintPtr(oldChunk.GetMem());
    ASSERT_TRUE(InsertHugeChunk(pool, oldChunk));
    for (size_t i = 0; i < HugeMemMapPool::SLOTS_PER_CHUNK - 1; i++) {
        ASSERT_NE(GetHugeSlot(pool, HugePageTag::OLD).GetMem(), nullptr);
    }

    MemMap semiInOldChunk = GetHugeSlot(pool, HugePageTag::SEMI);
    ASSERT_NE(semiInOldChunk.GetMem(), nullptr);
    EXPECT_EQ(ToUintPtr(semiInOldChunk.GetMem()) & ~(HugeMemMapPool::HUGE_REGION_SIZE - 1), oldBase);
    EXPECT_EQ(GetChunkOwnerDomain(pool, semiInOldChunk.GetMem()), 'L');
    EXPECT_EQ(GetSlotTag(pool, semiInOldChunk.GetMem()), 'S');

#if ENABLE_HUGEPAGE_DFX_STATS
    HugeMemMapPool promotedPool;
    MemMap promotedChunk = MapHugeChunk();
    ASSERT_NE(promotedChunk.GetMem(), nullptr);
    uintptr_t promotedBase = ToUintPtr(promotedChunk.GetMem());
    ASSERT_TRUE(InsertHugeChunk(promotedPool, promotedChunk));

    std::array<MemMap, 2> semiSlots;
    for (auto &slot : semiSlots) {
        slot = GetHugeSlot(promotedPool, HugePageTag::SEMI);
        ASSERT_NE(slot.GetMem(), nullptr);
        EXPECT_EQ(ToUintPtr(slot.GetMem()) & ~(HugeMemMapPool::HUGE_REGION_SIZE - 1), promotedBase);
    }

    NotifyHugeSlotMoved(promotedPool, semiSlots[0].GetMem(), HugePageTag::OLD);
    EXPECT_EQ(GetSlotTag(promotedPool, semiSlots[0].GetMem()), 'p');
    EXPECT_EQ(GetSlotTag(promotedPool, semiSlots[1].GetMem()), 'S');
    ASSERT_TRUE(ReleaseHugeSlot(promotedPool, semiSlots[0].GetMem()));

    MemMap oldInPromotedChunk = GetHugeSlot(promotedPool, HugePageTag::OLD);
    ASSERT_NE(oldInPromotedChunk.GetMem(), nullptr);
    EXPECT_EQ(oldInPromotedChunk.GetMem(), semiSlots[0].GetMem());
    EXPECT_EQ(GetChunkOwnerDomain(promotedPool, oldInPromotedChunk.GetMem()), 'L');
    EXPECT_EQ(GetSlotTag(promotedPool, oldInPromotedChunk.GetMem()), 'O');
    EXPECT_EQ(GetSlotTag(promotedPool, semiSlots[1].GetMem()), 'S');
#endif
}

TEST_F(HugeMemMapPoolTest, SharedOldHugeSlotsOnlyReuseSharedOldChunks)
{
    HugeMemMapPool pool;
    MemMap localChunk = MapHugeChunk();
    ASSERT_NE(localChunk.GetMem(), nullptr);
    uintptr_t localBase = ToUintPtr(localChunk.GetMem());
    ASSERT_TRUE(InsertHugeChunk(pool, localChunk));

    MemMap localSlot = GetHugeSlot(pool, HugePageTag::OLD);
    ASSERT_NE(localSlot.GetMem(), nullptr);
    EXPECT_EQ(ToUintPtr(localSlot.GetMem()) & ~(HugeMemMapPool::HUGE_REGION_SIZE - 1), localBase);
    EXPECT_EQ(GetChunkOwnerDomain(pool, localSlot.GetMem()), 'L');

    MemMap sharedChunk = MapHugeChunk();
    ASSERT_NE(sharedChunk.GetMem(), nullptr);
    uintptr_t sharedBase = ToUintPtr(sharedChunk.GetMem());
    ASSERT_TRUE(InsertHugeChunk(pool, sharedChunk));

    MemMap sharedSlot = GetHugeSlot(pool, HugePageTag::SHARED_OLD);
    ASSERT_NE(sharedSlot.GetMem(), nullptr);
    EXPECT_EQ(ToUintPtr(sharedSlot.GetMem()) & ~(HugeMemMapPool::HUGE_REGION_SIZE - 1), sharedBase);
    EXPECT_EQ(GetChunkOwnerDomain(pool, sharedSlot.GetMem()), 'D');

    MemMap secondSharedSlot = GetHugeSlot(pool, HugePageTag::SHARED_OLD);
    ASSERT_NE(secondSharedSlot.GetMem(), nullptr);
    EXPECT_EQ(ToUintPtr(secondSharedSlot.GetMem()) & ~(HugeMemMapPool::HUGE_REGION_SIZE - 1), sharedBase);

    HugeMemMapPool secondPool;
    MemMap sharedChunkForSecondPool = MapHugeChunk();
    ASSERT_NE(sharedChunkForSecondPool.GetMem(), nullptr);
    uintptr_t sharedBaseForSecondPool = ToUintPtr(sharedChunkForSecondPool.GetMem());
    ASSERT_TRUE(InsertHugeChunk(secondPool, sharedChunkForSecondPool));
    ASSERT_NE(GetHugeSlot(secondPool, HugePageTag::SHARED_OLD).GetMem(), nullptr);

    MemMap localChunkForSecondPool = MapHugeChunk();
    ASSERT_NE(localChunkForSecondPool.GetMem(), nullptr);
    ASSERT_TRUE(InsertHugeChunk(secondPool, localChunkForSecondPool));

    MemMap localOldSlot = GetHugeSlot(secondPool, HugePageTag::OLD);
    ASSERT_NE(localOldSlot.GetMem(), nullptr);
    EXPECT_NE(ToUintPtr(localOldSlot.GetMem()) & ~(HugeMemMapPool::HUGE_REGION_SIZE - 1),
              sharedBaseForSecondPool);
}

TEST_F(HugeMemMapPoolTest, FullGCTargetSkipsPreviousPartialAndReusesCurrentTargetChunk)
{
    HugeMemMapPool pool;
    MemMap sourceChunk = MapHugeChunk();
    ASSERT_NE(sourceChunk.GetMem(), nullptr);
    ASSERT_TRUE(InsertHugeChunk(pool, sourceChunk));
    MemMap sourceSlot = GetHugeSlot(pool, HugePageTag::OLD);
    ASSERT_NE(sourceSlot.GetMem(), nullptr);
    uintptr_t sourceBase = ToUintPtr(sourceSlot.GetMem()) & ~(HugeMemMapPool::HUGE_REGION_SIZE - 1);

    MemMap targetChunk = MapHugeChunk();
    ASSERT_NE(targetChunk.GetMem(), nullptr);
    uintptr_t targetChunkBase = ToUintPtr(targetChunk.GetMem());
    ASSERT_TRUE(InsertHugeChunk(pool, targetChunk));

    MemMap targetSlot = GetHugeSlot(pool, HugePageTag::OLD, HugeSlotAllocPolicy::FULL_GC_TARGET);
    ASSERT_NE(targetSlot.GetMem(), nullptr);
    uintptr_t targetBase = ToUintPtr(targetSlot.GetMem()) & ~(HugeMemMapPool::HUGE_REGION_SIZE - 1);
    EXPECT_EQ(targetBase, targetChunkBase);
    EXPECT_NE(targetBase, sourceBase);

    MemMap nextTargetSlot = GetHugeSlot(pool, HugePageTag::OLD, HugeSlotAllocPolicy::FULL_GC_TARGET);
    ASSERT_NE(nextTargetSlot.GetMem(), nullptr);
    EXPECT_EQ(ToUintPtr(nextTargetSlot.GetMem()) & ~(HugeMemMapPool::HUGE_REGION_SIZE - 1), targetBase);

    pool.NotifyGCReclaimFinished(TriggerGCType::FULL_GC);
    EXPECT_EQ(GetHugeSlot(pool, HugePageTag::OLD, HugeSlotAllocPolicy::FULL_GC_TARGET).GetMem(), nullptr);
}

TEST_F(HugeMemMapPoolTest, FullGCTargetEpochsAreSplitByLocalAndSharedDomains)
{
    HugeMemMapPool pool;
    uint64_t initialLocalEpoch = GetLocalTargetEpoch(pool);
    uint64_t initialSharedEpoch = GetSharedTargetEpoch(pool);
    pool.NotifyGCReclaimFinished(TriggerGCType::FULL_GC);
    pool.NotifyGCReclaimFinished(TriggerGCType::SHARED_FULL_GC);
    EXPECT_EQ(GetLocalTargetEpoch(pool), initialLocalEpoch);
    EXPECT_EQ(GetSharedTargetEpoch(pool), initialSharedEpoch);

    MemMap localChunk = MapHugeChunk();
    ASSERT_NE(localChunk.GetMem(), nullptr);
    ASSERT_TRUE(InsertHugeChunk(pool, localChunk));

    MemMap localSlot = GetHugeSlot(pool, HugePageTag::OLD, HugeSlotAllocPolicy::FULL_GC_TARGET);
    ASSERT_NE(localSlot.GetMem(), nullptr);
    auto *localTarget = GetChunk(pool, localSlot.GetMem());
    ASSERT_NE(localTarget, nullptr);
    EXPECT_EQ(GetChunkLocalTargetEpoch(*localTarget), GetLocalTargetEpoch(pool));

    MemMap sharedChunk = MapHugeChunk();
    ASSERT_NE(sharedChunk.GetMem(), nullptr);
    ASSERT_TRUE(InsertHugeChunk(pool, sharedChunk));

    MemMap sharedSlot = GetHugeSlot(pool, HugePageTag::SHARED_OLD, HugeSlotAllocPolicy::FULL_GC_TARGET);
    ASSERT_NE(sharedSlot.GetMem(), nullptr);
    auto *sharedTarget = GetChunk(pool, sharedSlot.GetMem());
    ASSERT_NE(sharedTarget, nullptr);
    EXPECT_EQ(GetChunkSharedTargetEpoch(*sharedTarget), GetSharedTargetEpoch(pool));

    uint64_t localEpoch = GetLocalTargetEpoch(pool);
    uint64_t sharedEpoch = GetSharedTargetEpoch(pool);
    pool.NotifyGCReclaimFinished(TriggerGCType::FULL_GC);
    EXPECT_GT(GetLocalTargetEpoch(pool), localEpoch);
    EXPECT_EQ(GetSharedTargetEpoch(pool), sharedEpoch);

    MemMap localAfterLocalFullGC =
        GetHugeSlot(pool, HugePageTag::OLD, HugeSlotAllocPolicy::FULL_GC_TARGET);
    ASSERT_NE(localAfterLocalFullGC.GetMem(), nullptr);
    localTarget = GetChunk(pool, localAfterLocalFullGC.GetMem());
    ASSERT_NE(localTarget, nullptr);
    EXPECT_EQ(GetChunkLocalTargetEpoch(*localTarget), GetLocalTargetEpoch(pool));

    MemMap sharedAfterLocalFullGC =
        GetHugeSlot(pool, HugePageTag::SHARED_OLD, HugeSlotAllocPolicy::FULL_GC_TARGET);
    ASSERT_NE(sharedAfterLocalFullGC.GetMem(), nullptr);
    sharedTarget = GetChunk(pool, sharedAfterLocalFullGC.GetMem());
    ASSERT_NE(sharedTarget, nullptr);
    EXPECT_EQ(GetChunkSharedTargetEpoch(*sharedTarget), sharedEpoch);

    localEpoch = GetLocalTargetEpoch(pool);
    pool.NotifyGCReclaimFinished(TriggerGCType::SHARED_FULL_GC);
    EXPECT_EQ(GetLocalTargetEpoch(pool), localEpoch);
    EXPECT_GT(GetSharedTargetEpoch(pool), sharedEpoch);

    MemMap sharedAfterSharedFullGC =
        GetHugeSlot(pool, HugePageTag::SHARED_OLD, HugeSlotAllocPolicy::FULL_GC_TARGET);
    ASSERT_NE(sharedAfterSharedFullGC.GetMem(), nullptr);
    sharedTarget = GetChunk(pool, sharedAfterSharedFullGC.GetMem());
    ASSERT_NE(sharedTarget, nullptr);
    EXPECT_EQ(GetChunkSharedTargetEpoch(*sharedTarget), GetSharedTargetEpoch(pool));
}

TEST_F(HugeMemMapPoolTest, DefaultPolicyReusesExistingPartialChunkAcrossYoungGC)
{
    HugeMemMapPool pool;
    MemMap sourceChunk = MapHugeChunk();
    ASSERT_NE(sourceChunk.GetMem(), nullptr);
    ASSERT_TRUE(InsertHugeChunk(pool, sourceChunk));

    MemMap sourceSlot = GetHugeSlot(pool, HugePageTag::SEMI);
    ASSERT_NE(sourceSlot.GetMem(), nullptr);
    uintptr_t sourceBase = ToUintPtr(sourceSlot.GetMem()) & ~(HugeMemMapPool::HUGE_REGION_SIZE - 1);

    MemMap targetChunk = MapHugeChunk();
    ASSERT_NE(targetChunk.GetMem(), nullptr);
    uintptr_t targetChunkBase = ToUintPtr(targetChunk.GetMem());
    ASSERT_TRUE(InsertHugeChunk(pool, targetChunk));

    MemMap targetSlot = GetHugeSlot(pool, HugePageTag::SEMI, HugeSlotAllocPolicy::DEFAULT);
    ASSERT_NE(targetSlot.GetMem(), nullptr);
    uintptr_t targetBase = ToUintPtr(targetSlot.GetMem()) & ~(HugeMemMapPool::HUGE_REGION_SIZE - 1);
    EXPECT_EQ(targetBase, sourceBase);
    EXPECT_NE(targetBase, targetChunkBase);

    MemMap nextTargetSlot = GetHugeSlot(pool, HugePageTag::SEMI, HugeSlotAllocPolicy::DEFAULT);
    ASSERT_NE(nextTargetSlot.GetMem(), nullptr);
    EXPECT_EQ(ToUintPtr(nextTargetSlot.GetMem()) & ~(HugeMemMapPool::HUGE_REGION_SIZE - 1), sourceBase);

    pool.NotifyGCReclaimFinished(TriggerGCType::YOUNG_GC);
    MemMap nextYoungGCTargetSlot =
        GetHugeSlot(pool, HugePageTag::SEMI, HugeSlotAllocPolicy::DEFAULT);
    ASSERT_NE(nextYoungGCTargetSlot.GetMem(), nullptr);
    EXPECT_EQ(ToUintPtr(nextYoungGCTargetSlot.GetMem()) &
              ~(HugeMemMapPool::HUGE_REGION_SIZE - 1), sourceBase);
}

TEST_F(HugeMemMapPoolTest, ReleaseSlotIfOwnedReturnsSlotToHugePool)
{
    HugeMemMapPool pool;
    MemMap chunk = MapHugeChunk();
    ASSERT_NE(chunk.GetMem(), nullptr);
    ASSERT_TRUE(InsertHugeChunk(pool, chunk));

    MemMap slot = GetHugeSlot(pool, HugePageTag::OLD);
    ASSERT_NE(slot.GetMem(), nullptr);
    EXPECT_FALSE(pool.ReleaseSlotIfOwned(nullptr, DEFAULT_REGION_SIZE));
    ASSERT_TRUE(pool.ReleaseSlotIfOwned(slot.GetMem(), slot.GetSize()));
    EXPECT_EQ(GetReleasedEmptyChunkCount(pool), 1U);

    MemMap reused = GetHugeSlot(pool, HugePageTag::OLD);
    ASSERT_NE(reused.GetMem(), nullptr);
    EXPECT_EQ(reused.GetMem(), slot.GetMem());
}

TEST_F(HugeMemMapPoolTest, HugeChunkMemMapAccountingUsesChunkLifecycle)
{
    HugeMemMapPool pool;
    MemMap chunk = MapHugeChunk();
    ASSERT_NE(chunk.GetMem(), nullptr);
    ASSERT_TRUE(InsertHugeChunk(pool, chunk));

    size_t memMapSizeDelta = 0;
    MemMap firstSlot = GetHugeSlotWithDelta(pool, HugePageTag::OLD, &memMapSizeDelta);
    ASSERT_NE(firstSlot.GetMem(), nullptr);
    EXPECT_EQ(memMapSizeDelta, HugeMemMapPool::HUGE_REGION_SIZE);

    memMapSizeDelta = HugeMemMapPool::HUGE_REGION_SIZE;
    MemMap secondSlot = GetHugeSlotWithDelta(pool, HugePageTag::OLD, &memMapSizeDelta);
    ASSERT_NE(secondSlot.GetMem(), nullptr);
    EXPECT_EQ(memMapSizeDelta, 0U);

    memMapSizeDelta = HugeMemMapPool::HUGE_REGION_SIZE;
    ASSERT_TRUE(ReleaseHugeSlot(pool, firstSlot.GetMem(), &memMapSizeDelta));
    EXPECT_EQ(memMapSizeDelta, 0U);

    memMapSizeDelta = 0;
    ASSERT_TRUE(ReleaseHugeSlot(pool, secondSlot.GetMem(), &memMapSizeDelta));
    EXPECT_EQ(memMapSizeDelta, HugeMemMapPool::HUGE_REGION_SIZE);

    memMapSizeDelta = 0;
    MemMap reusedSlot = GetHugeSlotWithDelta(pool, HugePageTag::OLD, &memMapSizeDelta);
    ASSERT_NE(reusedSlot.GetMem(), nullptr);
    EXPECT_EQ(reusedSlot.GetMem(), firstSlot.GetMem());
    EXPECT_EQ(memMapSizeDelta, HugeMemMapPool::HUGE_REGION_SIZE);
}

TEST_F(HugeMemMapPoolTest, HugeChunkActiveSlotQueriesUseSlotTags)
{
    HugeMemMapPool pool;
    MemMap chunk = MapHugeChunk();
    ASSERT_NE(chunk.GetMem(), nullptr);
    ASSERT_TRUE(InsertHugeChunk(pool, chunk));

    MemMap oldSlot = GetHugeSlot(pool, HugePageTag::OLD);
    ASSERT_NE(oldSlot.GetMem(), nullptr);
    EXPECT_TRUE(IsHugeChunkWithSingleActiveSlot(pool, oldSlot.GetMem(), HugePageTag::OLD));
    EXPECT_TRUE(IsHugeChunkWithActiveSlotCountAtMost(pool, oldSlot.GetMem(), HugePageTag::OLD, 2));

    void *activeSlotInnerAddr = reinterpret_cast<void *>(ToUintPtr(oldSlot.GetMem()) + 1);
    EXPECT_TRUE(IsHugeChunkWithSingleActiveSlot(pool, activeSlotInnerAddr, HugePageTag::OLD));

    void *freeSlot = reinterpret_cast<void *>(ToUintPtr(oldSlot.GetMem()) + DEFAULT_REGION_SIZE);
    EXPECT_FALSE(IsHugeChunkWithSingleActiveSlot(pool, freeSlot, HugePageTag::OLD));
    EXPECT_FALSE(IsHugeChunkWithActiveSlotCountAtMost(pool, freeSlot, HugePageTag::OLD, 2));

    MemMap secondOldSlot = GetHugeSlot(pool, HugePageTag::OLD);
    ASSERT_NE(secondOldSlot.GetMem(), nullptr);
    EXPECT_FALSE(IsHugeChunkWithSingleActiveSlot(pool, oldSlot.GetMem(), HugePageTag::OLD));
    EXPECT_TRUE(IsHugeChunkWithActiveSlotCountAtMost(pool, oldSlot.GetMem(), HugePageTag::OLD, 2));

    MemMap semiInOldChunk = GetHugeSlot(pool, HugePageTag::SEMI);
    ASSERT_NE(semiInOldChunk.GetMem(), nullptr);
    EXPECT_TRUE(IsHugeChunkWithActiveSlotCountAtMost(pool, oldSlot.GetMem(), HugePageTag::OLD, 2));
    EXPECT_TRUE(IsHugeChunkWithSingleActiveSlot(pool, semiInOldChunk.GetMem(), HugePageTag::SEMI));

    HugeMemMapPool semiPool;
    MemMap semiChunk = MapHugeChunk();
    ASSERT_NE(semiChunk.GetMem(), nullptr);
    ASSERT_TRUE(InsertHugeChunk(semiPool, semiChunk));

    MemMap semiSlot = GetHugeSlot(semiPool, HugePageTag::SEMI);
    ASSERT_NE(semiSlot.GetMem(), nullptr);
    EXPECT_FALSE(IsHugeChunkWithSingleActiveSlot(semiPool, semiSlot.GetMem(), HugePageTag::OLD));
    EXPECT_FALSE(IsHugeChunkWithActiveSlotCountAtMost(semiPool, semiSlot.GetMem(), HugePageTag::OLD, 2));
    EXPECT_TRUE(IsHugeChunkWithSingleActiveSlot(semiPool, semiSlot.GetMem(), HugePageTag::SEMI));

    HugeMemMapPool sharedOldPool;
    MemMap sharedOldChunk = MapHugeChunk();
    ASSERT_NE(sharedOldChunk.GetMem(), nullptr);
    ASSERT_TRUE(InsertHugeChunk(sharedOldPool, sharedOldChunk));

    MemMap sharedOldSlot = GetHugeSlot(sharedOldPool, HugePageTag::SHARED_OLD);
    ASSERT_NE(sharedOldSlot.GetMem(), nullptr);
    EXPECT_FALSE(IsHugeChunkWithSingleActiveSlot(sharedOldPool, sharedOldSlot.GetMem(), HugePageTag::OLD));
    EXPECT_TRUE(IsHugeChunkWithSingleActiveSlot(sharedOldPool, sharedOldSlot.GetMem(),
                                                HugePageTag::SHARED_OLD));
}

#if ENABLE_HUGEPAGE_DFX_STATS
TEST_F(HugeMemMapPoolTest, NotifyHugeSlotMovedUpdatesSlotTagsWithoutChangingLocalDomain)
{
    HugeMemMapPool pool;
    MemMap chunk = MapHugeChunk();
    ASSERT_NE(chunk.GetMem(), nullptr);
    ASSERT_TRUE(InsertHugeChunk(pool, chunk));

    MemMap firstSemiSlot = GetHugeSlot(pool, HugePageTag::SEMI);
    MemMap secondSemiSlot = GetHugeSlot(pool, HugePageTag::SEMI);
    ASSERT_NE(firstSemiSlot.GetMem(), nullptr);
    ASSERT_NE(secondSemiSlot.GetMem(), nullptr);
    EXPECT_EQ(GetChunkOwnerDomain(pool, firstSemiSlot.GetMem()), 'L');

    NotifyHugeSlotMoved(pool, firstSemiSlot.GetMem(), HugePageTag::SEMI);
    EXPECT_EQ(GetChunkOwnerDomain(pool, firstSemiSlot.GetMem()), 'L');
    EXPECT_EQ(GetSlotTag(pool, firstSemiSlot.GetMem()), 'S');

    NotifyHugeSlotMoved(pool, firstSemiSlot.GetMem(), HugePageTag::OLD);
    EXPECT_EQ(GetChunkOwnerDomain(pool, firstSemiSlot.GetMem()), 'L');
    EXPECT_EQ(GetSlotTag(pool, firstSemiSlot.GetMem()), 'p');
    EXPECT_EQ(GetSlotTag(pool, secondSemiSlot.GetMem()), 'S');

    NotifyHugeSlotMoved(pool, secondSemiSlot.GetMem(), HugePageTag::OLD);
    EXPECT_EQ(GetChunkOwnerDomain(pool, secondSemiSlot.GetMem()), 'L');
    EXPECT_EQ(GetSlotTag(pool, firstSemiSlot.GetMem()), 'p');
    EXPECT_EQ(GetSlotTag(pool, secondSemiSlot.GetMem()), 'p');
}
#endif

TEST_F(HugeMemMapPoolTest, ReleasingMixedLocalSlotsPreservesRemainingSlotTags)
{
    HugeMemMapPool pool;
    MemMap chunk = MapHugeChunk();
    ASSERT_NE(chunk.GetMem(), nullptr);
    ASSERT_TRUE(InsertHugeChunk(pool, chunk));

    MemMap semiSlot = GetHugeSlot(pool, HugePageTag::SEMI);
    MemMap oldSlot = GetHugeSlot(pool, HugePageTag::OLD);
    ASSERT_NE(semiSlot.GetMem(), nullptr);
    ASSERT_NE(oldSlot.GetMem(), nullptr);
    EXPECT_EQ(GetChunkOwnerDomain(pool, semiSlot.GetMem()), 'L');
    EXPECT_EQ(GetSlotTag(pool, semiSlot.GetMem()), 'S');
    EXPECT_EQ(GetSlotTag(pool, oldSlot.GetMem()), 'O');

    ASSERT_TRUE(ReleaseHugeSlot(pool, oldSlot.GetMem()));
    EXPECT_EQ(GetChunkOwnerDomain(pool, semiSlot.GetMem()), 'L');
    EXPECT_EQ(GetSlotTag(pool, semiSlot.GetMem()), 'S');
    EXPECT_TRUE(IsHugeChunkWithSingleActiveSlot(pool, semiSlot.GetMem(), HugePageTag::SEMI));
    EXPECT_FALSE(IsHugeChunkWithSingleActiveSlot(pool, semiSlot.GetMem(), HugePageTag::OLD));

    ASSERT_TRUE(ReleaseHugeSlot(pool, semiSlot.GetMem()));
    EXPECT_EQ(GetChunkOwnerDomain(pool, semiSlot.GetMem()), '_');
    EXPECT_EQ(GetSlotTag(pool, semiSlot.GetMem()), '_');
}

TEST_F(HugeMemMapPoolTest, ReleaseBatchSkipsChunkReuseUntilBatchEnds)
{
    HugeMemMapPool pool;
    MemMap chunk = MapHugeChunk();
    ASSERT_NE(chunk.GetMem(), nullptr);
    ASSERT_TRUE(InsertHugeChunk(pool, chunk));

    std::array<MemMap, HugeMemMapPool::SLOTS_PER_CHUNK> slots;
    for (auto &slot : slots) {
        slot = GetHugeSlot(pool, HugePageTag::OLD);
        ASSERT_NE(slot.GetMem(), nullptr);
    }

    pool.BeginSlotReleaseBatch();
    ASSERT_TRUE(ReleaseHugeSlot(pool, slots[0].GetMem()));
    EXPECT_EQ(GetHugeSlot(pool, HugePageTag::OLD).GetMem(), nullptr);
    for (size_t i = 1; i < HugeMemMapPool::SLOTS_PER_CHUNK; i++) {
        ASSERT_TRUE(ReleaseHugeSlot(pool, slots[i].GetMem()));
    }
    auto *releasedChunk = GetChunk(pool, slots[0].GetMem());
    ASSERT_NE(releasedChunk, nullptr);
    EXPECT_TRUE(IsPhysicalReleased(*releasedChunk));
    EXPECT_TRUE(IsReuseQuarantined(*releasedChunk));
    pool.EndSlotReleaseBatch();
    EXPECT_FALSE(IsReuseQuarantined(*releasedChunk));
}

TEST_F(HugeMemMapPoolTest, HugeRefillDeniedWhenFreeSlotsExceedQuotaLimit)
{
    HugeMemMapPool pool;
    constexpr size_t CHUNK_COUNT = 3;
    for (size_t i = 0; i < CHUNK_COUNT; i++) {
        MemMap chunk = MapHugeChunk();
        ASSERT_NE(chunk.GetMem(), nullptr);
        ASSERT_TRUE(InsertHugeChunk(pool, chunk));
    }

    constexpr size_t USED_SLOT_COUNT = 17;
    std::array<MemMap, USED_SLOT_COUNT> slots;
    for (auto &slot : slots) {
        slot = GetHugeSlot(pool, HugePageTag::OLD);
        ASSERT_NE(slot.GetMem(), nullptr);
    }

    bool quotaExceeded = false;
    EXPECT_FALSE(CanRefillHugeChunk(pool, CHUNK_COUNT + 1, quotaExceeded));
    EXPECT_FALSE(quotaExceeded);
}

TEST_F(HugeMemMapPoolTest, HugeRefillAllowedAtQuotaFreeSlotLimit)
{
    HugeMemMapPool pool;
    constexpr size_t CHUNK_COUNT = 4;
    for (size_t i = 0; i < CHUNK_COUNT; i++) {
        MemMap chunk = MapHugeChunk();
        ASSERT_NE(chunk.GetMem(), nullptr);
        ASSERT_TRUE(InsertHugeChunk(pool, chunk));
    }

    std::array<MemMap, HugeMemMapPool::SLOTS_PER_CHUNK * CHUNK_COUNT> slots;
    for (auto &slot : slots) {
        slot = GetHugeSlot(pool, HugePageTag::OLD);
        ASSERT_NE(slot.GetMem(), nullptr);
    }

    for (size_t chunkIndex = 0; chunkIndex < CHUNK_COUNT; chunkIndex++) {
        size_t firstSlotIndex = chunkIndex * HugeMemMapPool::SLOTS_PER_CHUNK;
        ASSERT_TRUE(ReleaseHugeSlot(pool, slots[firstSlotIndex].GetMem()));
        ASSERT_TRUE(ReleaseHugeSlot(pool, slots[firstSlotIndex + 1].GetMem()));
    }

    bool quotaExceeded = false;
    EXPECT_TRUE(CanRefillHugeChunk(pool, CHUNK_COUNT + 1, quotaExceeded));
    EXPECT_FALSE(quotaExceeded);
}

TEST_F(HugeMemMapPoolTest, HugeRefillCountsOnlyReusableSlotsInRequestedDomain)
{
    HugeMemMapPool pool;
    MemMap chunk = MapHugeChunk();
    ASSERT_NE(chunk.GetMem(), nullptr);
    ASSERT_TRUE(InsertHugeChunk(pool, chunk));

    MemMap sharedSlot = GetHugeSlot(pool, HugePageTag::SHARED_OLD);
    ASSERT_NE(sharedSlot.GetMem(), nullptr);
    auto *sharedChunk = GetChunk(pool, sharedSlot.GetMem());
    ASSERT_NE(sharedChunk, nullptr);

    bool quotaExceeded = false;
    EXPECT_TRUE(CanRefillHugeChunk(pool, 2, 0, HugePageTag::OLD, quotaExceeded));
    EXPECT_FALSE(quotaExceeded);
    EXPECT_FALSE(CanRefillHugeChunk(pool, 2, 0, HugePageTag::SHARED_OLD, quotaExceeded));
    EXPECT_FALSE(quotaExceeded);

    sharedChunk->isAdvising_ = true;
    EXPECT_TRUE(CanRefillHugeChunk(pool, 2, 0, HugePageTag::SHARED_OLD, quotaExceeded));
    sharedChunk->isAdvising_ = false;
    sharedChunk->isReuseQuarantined_ = true;
    EXPECT_TRUE(CanRefillHugeChunk(pool, 2, 0, HugePageTag::SHARED_OLD, quotaExceeded));
}

TEST_F(HugeMemMapPoolTest, LocalHugeSlotReusesPartialChunkWhenFreeSlotsAreHigh)
{
    HugeMemMapPool pool;
    constexpr size_t CHUNK_COUNT = 3;
    uintptr_t lastChunkBase = 0;
    for (size_t i = 0; i < CHUNK_COUNT; i++) {
        MemMap chunk = MapHugeChunk();
        ASSERT_NE(chunk.GetMem(), nullptr);
        lastChunkBase = ToUintPtr(chunk.GetMem());
        ASSERT_TRUE(InsertHugeChunk(pool, chunk));
    }

    constexpr size_t USED_SLOT_COUNT = 17;
    std::array<MemMap, USED_SLOT_COUNT> slots;
    for (auto &slot : slots) {
        slot = GetHugeSlot(pool, HugePageTag::OLD);
        ASSERT_NE(slot.GetMem(), nullptr);
    }

    bool quotaExceeded = false;
    EXPECT_FALSE(CanRefillHugeChunk(pool, CHUNK_COUNT + 1, quotaExceeded));
    EXPECT_FALSE(quotaExceeded);

    MemMap emptyChunk = MapHugeChunk();
    ASSERT_NE(emptyChunk.GetMem(), nullptr);
    uintptr_t emptyBase = ToUintPtr(emptyChunk.GetMem());
    ASSERT_TRUE(InsertHugeChunk(pool, emptyChunk));

    MemMap reused = GetHugeSlot(pool, HugePageTag::SEMI);
    ASSERT_NE(reused.GetMem(), nullptr);
    uintptr_t reusedBase = ToUintPtr(reused.GetMem()) & ~(HugeMemMapPool::HUGE_REGION_SIZE - 1);
    EXPECT_EQ(reusedBase, lastChunkBase);
    EXPECT_NE(reusedBase, emptyBase);
}

TEST_F(HugeMemMapPoolTest, HugePageSummaryDfxCountsActiveEmptyAndReleasedChunks)
{
    HugeMemMapPool pool;
    std::array<MemMap, 3> chunks;
    for (auto &chunk : chunks) {
        chunk = MapHugeChunk();
        ASSERT_NE(chunk.GetMem(), nullptr);
        ASSERT_TRUE(InsertHugeChunk(pool, chunk));
    }

    auto *activeChunk = TakeEmptyChunk(pool);
    auto *releasedChunk = GetChunk(pool, chunks[2].GetMem());
    ASSERT_NE(activeChunk, nullptr);
    ASSERT_NE(releasedChunk, nullptr);
    SetChunkState(activeChunk, {0b11U, HugeChunkDomain::LOCAL});
    SetSlotTag(activeChunk, 0, HugePageTag::OLD);
    SetSlotTag(activeChunk, 1, HugePageTag::OLD);
    InsertPartialChunk(pool, activeChunk);
    SetPhysicalReleased(releasedChunk, true);

    SummaryStats stats = CollectSummaryDfx(pool);
    EXPECT_EQ(stats.totalChunks, 3U);
    EXPECT_EQ(stats.activeChunks, 1U);
    EXPECT_EQ(stats.emptyChunks, 2U);
    EXPECT_EQ(stats.releasedEmptyChunks, 1U);
    EXPECT_EQ(stats.usedSlots, 2U);
    EXPECT_EQ(stats.activeSlotCapacity, HugeMemMapPool::SLOTS_PER_CHUNK);
    pool.DumpUsageSummaryDfx();
}

TEST_F(HugeMemMapPoolTest, SoftReleaseReleasesAllEmptyChunks)
{
    HugeMemMapPool pool;
    constexpr size_t CHUNK_COUNT = 3;
    for (size_t i = 0; i < CHUNK_COUNT; i++) {
        MemMap chunk = MapHugeChunk();
        ASSERT_NE(chunk.GetMem(), nullptr);
        ASSERT_TRUE(InsertHugeChunk(pool, chunk));
    }

    EXPECT_EQ(SoftReleaseEmptyChunks(pool), CHUNK_COUNT);
    EXPECT_EQ(GetReleasedEmptyChunkCount(pool), CHUNK_COUNT);
}

TEST_F(HugeMemMapPoolTest, InsertAndRemoveHugeChunkDefensivePaths)
{
    HugeMemMapPool pool;
    EXPECT_FALSE(InsertHugeChunk(pool, MemMap()));
    EXPECT_FALSE(RemoveHugeChunk(pool, MemMap()));
    EXPECT_EQ(GetChunk(pool, nullptr), nullptr);

    MemMap chunk = MapHugeChunk();
    MemMap unknown = MapHugeChunk();
    ASSERT_NE(chunk.GetMem(), nullptr);
    ASSERT_NE(unknown.GetMem(), nullptr);
    ASSERT_TRUE(InsertHugeChunk(pool, chunk));
    EXPECT_FALSE(InsertHugeChunk(pool, chunk));
    EXPECT_NE(GetChunk(pool, chunk.GetMem()), nullptr);
    EXPECT_NE(GetChunk(pool, reinterpret_cast<void *>(ToUintPtr(chunk.GetMem()) + 1)), nullptr);
    EXPECT_EQ(GetChunk(pool, unknown.GetMem()), nullptr);
    EXPECT_FALSE(RemoveHugeChunk(pool, unknown));

    auto *poolChunk = GetChunk(pool, chunk.GetMem());
    ASSERT_NE(poolChunk, nullptr);
    SetChunkState(poolChunk, {0, HugeChunkDomain::EMPTY, false, true});
    EXPECT_FALSE(RemoveHugeChunk(pool, chunk));
    SetChunkState(poolChunk, {0, HugeChunkDomain::EMPTY});

    MemMap slot = GetHugeSlot(pool, HugePageTag::OLD);
    ASSERT_NE(slot.GetMem(), nullptr);
    EXPECT_EQ(CountActiveSlotsWithTag(pool, slot.GetMem(), HugePageTag::OLD), 1U);
    EXPECT_FALSE(RemoveHugeChunk(pool, chunk));
    ASSERT_TRUE(ReleaseHugeSlot(pool, slot.GetMem()));
    EXPECT_TRUE(RemoveHugeChunk(pool, chunk));
    EXPECT_EQ(GetChunk(pool, chunk.GetMem()), nullptr);
    PageUnmap(unknown);
}

TEST_F(HugeMemMapPoolTest, RemoveHugeChunkReleasesOnlyOwnedQuota)
{
    SetProcessState({true, true, true, 2, 3, 1});
    HugeMemMapPool pool;
    MemMap unreservedChunk = MapHugeChunk();
    MemMap reservedChunk = MapHugeChunk();
    ASSERT_NE(unreservedChunk.GetMem(), nullptr);
    ASSERT_NE(reservedChunk.GetMem(), nullptr);
    ASSERT_TRUE(InsertHugeChunk(pool, unreservedChunk));
    ASSERT_TRUE(InsertHugeChunk(pool, reservedChunk, true));

    EXPECT_TRUE(RemoveHugeChunk(pool, unreservedChunk));
    EXPECT_EQ(GetAllocatedHugePageCount(), 1U);
    EXPECT_TRUE(RemoveHugeChunk(pool, reservedChunk));
    EXPECT_EQ(GetAllocatedHugePageCount(), 0U);
}

TEST_F(HugeMemMapPoolTest, EmptyChunkSelectionSkipsUnavailableChunksAndFallsBackToReleasedChunk)
{
    HugeMemMapPool pool;
    std::array<MemMap, 5> chunks;
    for (auto &chunk : chunks) {
        chunk = MapHugeChunk();
        ASSERT_NE(chunk.GetMem(), nullptr);
        ASSERT_TRUE(InsertHugeChunk(pool, chunk));
    }

    auto *quarantined = GetChunk(pool, chunks[0].GetMem());
    auto *advising = GetChunk(pool, chunks[1].GetMem());
    auto *advisingAndReleased = GetChunk(pool, chunks[2].GetMem());
    auto *released = GetChunk(pool, chunks[3].GetMem());
    auto *resident = GetChunk(pool, chunks[4].GetMem());
    ASSERT_NE(quarantined, nullptr);
    ASSERT_NE(advising, nullptr);
    ASSERT_NE(advisingAndReleased, nullptr);
    ASSERT_NE(released, nullptr);
    ASSERT_NE(resident, nullptr);
    SetChunkState(quarantined, {0, HugeChunkDomain::EMPTY, false, false, false, true});
    SetChunkState(advising, {0, HugeChunkDomain::EMPTY, false, true});
    SetChunkState(advisingAndReleased, {0, HugeChunkDomain::EMPTY, true, true});
    SetChunkState(released, {0, HugeChunkDomain::EMPTY, true});

    EXPECT_EQ(TakeEmptyChunk(pool), resident);
    EXPECT_EQ(TakeEmptyChunk(pool), released);
    EXPECT_EQ(TakeEmptyChunk(pool), nullptr);
}

TEST_F(HugeMemMapPoolTest, PartialChunkBookkeepingHandlesBoundaryAndQuarantineStates)
{
    HugeMemMapPool pool;
    MemMap chunk = MapHugeChunk();
    ASSERT_NE(chunk.GetMem(), nullptr);
    ASSERT_TRUE(InsertHugeChunk(pool, chunk));
    auto *poolChunk = TakeEmptyChunk(pool);
    ASSERT_NE(poolChunk, nullptr);

    InsertPartialChunk(pool, nullptr);
    RemovePartialChunk(pool, nullptr);
    SetChunkState(poolChunk, {0, HugeChunkDomain::EMPTY});
    InsertPartialChunk(pool, poolChunk);
    RemovePartialChunk(pool, poolChunk);
    EXPECT_EQ(GetPartialChunkCount(pool), 0U);

    SetChunkState(poolChunk, {std::numeric_limits<uint8_t>::max(), HugeChunkDomain::LOCAL});
    InsertPartialChunk(pool, poolChunk);
    RemovePartialChunk(pool, poolChunk);
    EXPECT_EQ(GetPartialChunkCount(pool), 0U);

    SetChunkState(poolChunk, {1U, HugeChunkDomain::LOCAL});
    InsertPartialChunk(pool, poolChunk);
    EXPECT_EQ(GetPartialChunkCount(pool), 1U);
    RemovePartialChunk(pool, poolChunk);
    EXPECT_EQ(GetPartialChunkCount(pool), 0U);

    SetChunkState(poolChunk, {1U, HugeChunkDomain::LOCAL, false, false, false, true});
    InsertPartialChunk(pool, poolChunk);
    RemovePartialChunk(pool, poolChunk);
    EXPECT_EQ(GetPartialChunkCount(pool), 0U);
}

TEST_F(HugeMemMapPoolTest, ReusableChunkBucketSearchCoversEveryFilter)
{
    HugeMemMapPool pool;
    MemMap chunk = MapHugeChunk();
    ASSERT_NE(chunk.GetMem(), nullptr);
    ASSERT_TRUE(InsertHugeChunk(pool, chunk));
    auto *poolChunk = TakeEmptyChunk(pool);
    ASSERT_NE(poolChunk, nullptr);
    SetChunkState(poolChunk, {1U, HugeChunkDomain::LOCAL});
    SetSlotTag(poolChunk, 0, HugePageTag::OLD);
    InsertPartialChunk(pool, poolChunk);

    SetChunkState(poolChunk, {1U, HugeChunkDomain::LOCAL, false, false, false, true});
    EXPECT_EQ(FindReusableChunkInBuckets(pool, HugePageTag::OLD,
        HugeSlotAllocPolicy::DEFAULT, true), nullptr);

    SetChunkState(poolChunk, {1U, HugeChunkDomain::LOCAL, false, true, false, true});
    EXPECT_EQ(FindReusableChunkInBuckets(pool, HugePageTag::OLD,
        HugeSlotAllocPolicy::DEFAULT, true), nullptr);

    SetChunkState(poolChunk, {1U, HugeChunkDomain::LOCAL, false, true});
    EXPECT_EQ(FindReusableChunkInBuckets(pool, HugePageTag::OLD,
        HugeSlotAllocPolicy::DEFAULT, true), nullptr);

    SetChunkState(poolChunk, {1U, HugeChunkDomain::LOCAL});
    EXPECT_EQ(FindReusableChunkInBuckets(pool, HugePageTag::OLD,
        HugeSlotAllocPolicy::DEFAULT, false), nullptr);
    EXPECT_EQ(FindReusableChunkInBuckets(pool, HugePageTag::OLD,
        HugeSlotAllocPolicy::DEFAULT, true), poolChunk);

    EXPECT_EQ(FindReusableChunkInBuckets(pool, HugePageTag::OLD,
        HugeSlotAllocPolicy::FULL_GC_TARGET, true), nullptr);
    SetChunkTargetEpochs(poolChunk, GetLocalTargetEpoch(pool), 0);
    EXPECT_EQ(FindReusableChunkInBuckets(pool, HugePageTag::OLD,
        HugeSlotAllocPolicy::FULL_GC_TARGET, true), poolChunk);
}

TEST_F(HugeMemMapPoolTest, OccupySlotTracksDomainAndMemMapAccounting)
{
    HugeMemMapPool pool;
    MemMap chunk = MapHugeChunk();
    ASSERT_NE(chunk.GetMem(), nullptr);
    ASSERT_TRUE(InsertHugeChunk(pool, chunk));
    auto *poolChunk = TakeEmptyChunk(pool);
    ASSERT_NE(poolChunk, nullptr);

    size_t memMapSizeDelta = 0;
    MemMap oldSlot = OccupyFreeSlot(pool, poolChunk, HugePageTag::OLD, &memMapSizeDelta);
    ASSERT_NE(oldSlot.GetMem(), nullptr);
    EXPECT_EQ(memMapSizeDelta, HugeMemMapPool::HUGE_REGION_SIZE);
    EXPECT_EQ(GetChunkOwnerDomain(pool, oldSlot.GetMem()), 'L');

    memMapSizeDelta = 1;
    MemMap sharedSlot = OccupyFreeSlot(pool, poolChunk, HugePageTag::SHARED_OLD, &memMapSizeDelta);
    ASSERT_NE(sharedSlot.GetMem(), nullptr);
    EXPECT_EQ(memMapSizeDelta, 1U);
    EXPECT_EQ(GetOwnerDomain(*poolChunk), HugeChunkDomain::MIXED);
    EXPECT_EQ(GetSlotTag(pool, sharedSlot.GetMem()), 'D');
}

TEST_F(HugeMemMapPoolTest, FreeSlotValidationAndChunkStateTransitions)
{
    HugeMemMapPool pool;
    MemMap chunk = MapHugeChunk();
    ASSERT_NE(chunk.GetMem(), nullptr);
    ASSERT_TRUE(InsertHugeChunk(pool, chunk));
    MemMap first = GetHugeSlot(pool, HugePageTag::OLD);
    MemMap second = GetHugeSlot(pool, HugePageTag::OLD);
    ASSERT_NE(first.GetMem(), nullptr);
    ASSERT_NE(second.GetMem(), nullptr);
    auto *poolChunk = GetChunk(pool, first.GetMem());
    ASSERT_NE(poolChunk, nullptr);
    uintptr_t baseAddr = ToUintPtr(chunk.GetMem());
    bool isChunkEmpty = false;

    EXPECT_FALSE(FreeHugeSlot(pool, reinterpret_cast<void *>(ToUintPtr(first.GetMem()) + 1),
                              poolChunk, baseAddr, isChunkEmpty));
    EXPECT_FALSE(FreeHugeSlot(pool, reinterpret_cast<void *>(baseAddr + HugeMemMapPool::HUGE_REGION_SIZE),
                              poolChunk, baseAddr, isChunkEmpty));
    void *freeSlot = reinterpret_cast<void *>(baseAddr + 2 * DEFAULT_REGION_SIZE);
    EXPECT_FALSE(FreeHugeSlot(pool, freeSlot, poolChunk, baseAddr, isChunkEmpty));

    EXPECT_TRUE(FreeHugeSlot(pool, first.GetMem(), poolChunk, baseAddr, isChunkEmpty));
    EXPECT_FALSE(isChunkEmpty);
    EXPECT_EQ(GetPartialChunkCount(pool), 1U);
    EXPECT_TRUE(FreeHugeSlot(pool, second.GetMem(), poolChunk, baseAddr, isChunkEmpty));
    EXPECT_TRUE(isChunkEmpty);
    EXPECT_EQ(GetChunkOwnerDomain(pool, second.GetMem()), '_');
}

TEST_F(HugeMemMapPoolTest, NestedReleaseBatchRestoresOnlyAtOutermostBoundary)
{
    HugeMemMapPool pool;
    MemMap firstChunk = MapHugeChunk();
    MemMap secondChunk = MapHugeChunk();
    ASSERT_NE(firstChunk.GetMem(), nullptr);
    ASSERT_NE(secondChunk.GetMem(), nullptr);
    ASSERT_TRUE(InsertHugeChunk(pool, firstChunk));
    ASSERT_TRUE(InsertHugeChunk(pool, secondChunk));

    MemMap first = GetHugeSlot(pool, HugePageTag::OLD);
    MemMap second = GetHugeSlot(pool, HugePageTag::OLD);
    ASSERT_NE(first.GetMem(), nullptr);
    ASSERT_NE(second.GetMem(), nullptr);
    pool.EndSlotReleaseBatch();
    pool.BeginSlotReleaseBatch();
    pool.BeginSlotReleaseBatch();
    ASSERT_TRUE(ReleaseHugeSlot(pool, first.GetMem()));
    auto *chunk = GetChunk(pool, second.GetMem());
    ASSERT_NE(chunk, nullptr);
    EXPECT_TRUE(IsReuseQuarantined(*chunk));
    QuarantineChunkForRelease(pool, chunk);

    pool.EndSlotReleaseBatch();
    EXPECT_TRUE(IsReuseQuarantined(*chunk));
    pool.EndSlotReleaseBatch();
    EXPECT_FALSE(IsReuseQuarantined(*chunk));
    EXPECT_NE(GetHugeSlot(pool, HugePageTag::OLD).GetMem(), nullptr);
}

#if ENABLE_HUGEPAGE_DFX_STATS
TEST_F(HugeMemMapPoolTest, SlotMoveValidationAndDomainRules)
{
    HugeMemMapPool emptyPool;
    NotifyHugeSlotMoved(emptyPool, reinterpret_cast<void *>(1), HugePageTag::OLD);
    EXPECT_FALSE(emptyPool.HasChunks());

    HugeMemMapPool pool;
    MemMap chunk = MapHugeChunk();
    MemMap unknown = MapHugeChunk();
    ASSERT_NE(chunk.GetMem(), nullptr);
    ASSERT_NE(unknown.GetMem(), nullptr);
    ASSERT_TRUE(InsertHugeChunk(pool, chunk));
    MemMap slot = GetHugeSlot(pool, HugePageTag::OLD);
    ASSERT_NE(slot.GetMem(), nullptr);
    auto *poolChunk = GetChunk(pool, slot.GetMem());
    ASSERT_NE(poolChunk, nullptr);

    NotifyHugeSlotMoved(pool, nullptr, HugePageTag::OLD);
    NotifyHugeSlotMoved(pool, slot.GetMem(), HugePageTag::INVALID);
    NotifyHugeSlotMoved(pool, unknown.GetMem(), HugePageTag::OLD);
    NotifyHugeSlotMoved(pool, reinterpret_cast<void *>(ToUintPtr(slot.GetMem()) + DEFAULT_REGION_SIZE),
                        HugePageTag::OLD);
    EXPECT_EQ(GetSlotTag(pool, slot.GetMem()), 'O');

    UpdateSlotTagAfterMove(pool, nullptr, 0, HugePageTag::OLD);
    UpdateSlotTagAfterMove(pool, poolChunk, HugeMemMapPool::SLOTS_PER_CHUNK, HugePageTag::OLD);
    UpdateSlotTagAfterMove(pool, poolChunk, 0, HugePageTag::SHARED_OLD);
    EXPECT_EQ(GetSlotTag(pool, slot.GetMem()), 'O');

    SetOwnerDomain(poolChunk, HugeChunkDomain::MIXED);
    UpdateSlotTagAfterMove(pool, poolChunk, 0, HugePageTag::SHARED_OLD);
    EXPECT_EQ(GetSlotTag(pool, slot.GetMem()), 'D');
    SetOwnerDomain(poolChunk, HugeChunkDomain::EMPTY);
    UpdateSlotTagAfterMove(pool, poolChunk, 0, HugePageTag::SEMI);
    EXPECT_EQ(GetSlotTag(pool, slot.GetMem()), 'S');
    UpdateSlotTagAfterMove(pool, poolChunk, 0, HugePageTag::OLD);
    EXPECT_EQ(GetSlotTag(pool, slot.GetMem()), 'p');
    UpdateSlotTagAfterMove(pool, poolChunk, 0, HugePageTag::OLD);
    EXPECT_EQ(GetSlotTag(pool, slot.GetMem()), 'p');
    PageUnmap(unknown);
}
#endif

TEST_F(HugeMemMapPoolTest, ReleaseSingleEmptyChunkCoversMissingActiveAndAccountingStates)
{
    HugeMemMapPool pool;
    MemMap chunk = MapHugeChunk();
    ASSERT_NE(chunk.GetMem(), nullptr);
    ASSERT_TRUE(InsertHugeChunk(pool, chunk));
    auto *poolChunk = GetChunk(pool, chunk.GetMem());
    ASSERT_NE(poolChunk, nullptr);
    size_t releasedMemMapSize = 0;

    EXPECT_FALSE(ReleaseSingleEmptyChunk(pool, 1, releasedMemMapSize));
    SetChunkState(poolChunk, {1U, HugeChunkDomain::LOCAL, false, true});
    EXPECT_FALSE(ReleaseSingleEmptyChunk(pool, ToUintPtr(chunk.GetMem()), releasedMemMapSize));
    SetChunkState(poolChunk, {0, HugeChunkDomain::EMPTY});
    SetMemMapCounted(poolChunk, true);
    EXPECT_TRUE(ReleaseSingleEmptyChunk(pool, ToUintPtr(chunk.GetMem()), releasedMemMapSize));
    EXPECT_EQ(releasedMemMapSize, HugeMemMapPool::HUGE_REGION_SIZE);
    EXPECT_TRUE(IsPhysicalReleased(*poolChunk));
    EXPECT_TRUE(ReleaseSingleEmptyChunk(pool, ToUintPtr(chunk.GetMem()), releasedMemMapSize));
    EXPECT_EQ(releasedMemMapSize, HugeMemMapPool::HUGE_REGION_SIZE);
}

TEST_F(HugeMemMapPoolTest, SoftReleaseRevalidatesChunkAndReportsMappedMemoryDelta)
{
    HugeMemMapPool pool;
    MemMap countedChunk = MapHugeChunk();
    MemMap racedChunk = MapHugeChunk();
    ASSERT_NE(countedChunk.GetMem(), nullptr);
    ASSERT_NE(racedChunk.GetMem(), nullptr);
    ASSERT_TRUE(InsertHugeChunk(pool, countedChunk));
    ASSERT_TRUE(InsertHugeChunk(pool, racedChunk));
    auto *countedPoolChunk = GetChunk(pool, countedChunk.GetMem());
    auto *racedPoolChunk = GetChunk(pool, racedChunk.GetMem());
    ASSERT_NE(countedPoolChunk, nullptr);
    ASSERT_NE(racedPoolChunk, nullptr);
    SetMemMapCounted(countedPoolChunk, true);

    g_pageReleaseCallback = [&pool, racedAddress = racedChunk.GetMem(), racedPoolChunk](void *mem) {
        if (mem == racedAddress) {
            SetUsageMaskWithLock(pool, racedPoolChunk, 1U);
        }
    };
    size_t memMapSizeDelta = std::numeric_limits<size_t>::max();
    EXPECT_EQ(SoftReleaseEmptyChunks(pool, &memMapSizeDelta), 1U);
    g_pageReleaseCallback = {};

    EXPECT_EQ(memMapSizeDelta, HugeMemMapPool::HUGE_REGION_SIZE);
    EXPECT_TRUE(IsPhysicalReleased(*countedPoolChunk));
    EXPECT_FALSE(IsPhysicalReleased(*racedPoolChunk));
    EXPECT_FALSE(IsAdvising(*racedPoolChunk));
    SetUsageMaskWithLock(pool, racedPoolChunk, 0);
}

TEST_F(HugeMemMapPoolTest, MadviseSelectionAndStateUpdateRevalidateChunks)
{
    constexpr int TEST_VIP_FLAG = 0x100000;
    HugeMemMapPool pool;
    MemMap chunk = MapHugeChunk();
    ASSERT_NE(chunk.GetMem(), nullptr);
    ASSERT_TRUE(InsertHugeChunk(pool, chunk));
    MemMap slot = GetHugeSlot(pool, HugePageTag::OLD);
    ASSERT_NE(slot.GetMem(), nullptr);
    auto *poolChunk = GetChunk(pool, slot.GetMem());
    ASSERT_NE(poolChunk, nullptr);

    EXPECT_TRUE(ShouldMadviseChunk(pool, *poolChunk, MADV_COLD, '\0'));
    EXPECT_TRUE(ShouldMadviseChunk(pool, *poolChunk, MADV_COLD, 'O'));
    EXPECT_FALSE(ShouldMadviseChunk(pool, *poolChunk, MADV_COLD, 'D'));
    EXPECT_FALSE(ShouldMadviseChunk(pool, *poolChunk, MADV_COLD, 'X'));
    EXPECT_TRUE(CollectMadviseTargets(pool, 0, MADV_COLD, '\0').empty());
    EXPECT_EQ(CollectMadviseTargets(pool, 1, MADV_COLD, 'O').size(), 1U);
    MadviseActiveResidentChunks(pool, 0, MADV_COLD, "skip");
    MadviseActiveResidentChunks(pool, 1, MADV_COLD, "cold");
    MadviseActiveResidentChunks(pool, 1, TEST_VIP_FLAG, "vip");
    SetChunkState(poolChunk, {1U, HugeChunkDomain::LOCAL});
    SetSlotTag(poolChunk, 0, HugePageTag::OLD);

    UpdateMadviseState(pool, {}, MADV_COLD, '\0');
    UpdateMadviseState(pool, {1}, MADV_COLD, '\0');
    UpdateMadviseState(pool, {ToUintPtr(chunk.GetMem())}, MADV_COLD, '\0');
    EXPECT_TRUE(IsColdAdvised(*poolChunk));
    UpdateMadviseState(pool, {ToUintPtr(chunk.GetMem())}, MADV_COLD, '\0');
    EXPECT_TRUE(IsColdAdvised(*poolChunk));
    UpdateMadviseState(pool, {ToUintPtr(chunk.GetMem())}, TEST_VIP_FLAG, '\0');
    EXPECT_FALSE(IsColdAdvised(*poolChunk));
    UpdateMadviseState(pool, {ToUintPtr(chunk.GetMem())}, 0, '\0');
    EXPECT_FALSE(IsColdAdvised(*poolChunk));

    SetPhysicalReleased(poolChunk, true);
    EXPECT_FALSE(ShouldMadviseChunk(pool, *poolChunk, MADV_COLD, '\0'));
    SetPhysicalReleased(poolChunk, false);
    SetUsageMask(poolChunk, 0);
    EXPECT_FALSE(ShouldMadviseChunk(pool, *poolChunk, MADV_COLD, '\0'));
    SetProcessState({false, false, false, 0, 0, 0});
}

TEST_F(HugeMemMapPoolTest, GcTargetEpochBranches)
{
    HugeMemMapPool pool;
    MemMap firstChunk = MapHugeChunk();
    MemMap secondChunk = MapHugeChunk();
    ASSERT_NE(firstChunk.GetMem(), nullptr);
    ASSERT_NE(secondChunk.GetMem(), nullptr);
    ASSERT_TRUE(InsertHugeChunk(pool, firstChunk));
    ASSERT_TRUE(InsertHugeChunk(pool, secondChunk));
    auto *first = TakeEmptyChunk(pool);
    auto *second = TakeEmptyChunk(pool);
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);

    EXPECT_TRUE(IsCurrentGcTargetChunk(pool, *first, HugePageTag::OLD, HugeSlotAllocPolicy::DEFAULT));
    EXPECT_TRUE(IsCurrentGcTargetChunk(pool, *first, HugePageTag::OLD, HugeSlotAllocPolicy::FULL_GC_TARGET));
    SetChunkState(first, {1U, HugeChunkDomain::LOCAL});
    SetSlotTag(first, 0, HugePageTag::OLD);
    EXPECT_TRUE(IsCurrentGcTargetChunk(pool, *first, HugePageTag::SHARED_OLD,
                                       HugeSlotAllocPolicy::FULL_GC_TARGET));
    EXPECT_FALSE(IsCurrentGcTargetChunk(pool, *first, HugePageTag::OLD,
                                        HugeSlotAllocPolicy::FULL_GC_TARGET));
    SetChunkTargetEpochs(first, GetLocalTargetEpoch(pool), 0);
    EXPECT_TRUE(IsCurrentGcTargetChunk(pool, *first, HugePageTag::OLD,
                                       HugeSlotAllocPolicy::FULL_GC_TARGET));

    EXPECT_FALSE(ShouldMarkGcTarget(pool, HugePageTag::OLD, HugeSlotAllocPolicy::DEFAULT));
    EXPECT_FALSE(ShouldMarkGcTarget(pool, HugePageTag::INVALID, HugeSlotAllocPolicy::FULL_GC_TARGET));
    EXPECT_TRUE(ShouldMarkGcTarget(pool, HugePageTag::SEMI, HugeSlotAllocPolicy::FULL_GC_TARGET));
    EXPECT_TRUE(ShouldMarkGcTarget(pool, HugePageTag::SHARED_OLD, HugeSlotAllocPolicy::FULL_GC_TARGET));
    MarkGcTargetChunk(pool, nullptr, HugePageTag::OLD, HugeSlotAllocPolicy::FULL_GC_TARGET);
    MarkGcTargetChunk(pool, first, HugePageTag::OLD, HugeSlotAllocPolicy::DEFAULT);
    MarkGcTargetChunk(pool, first, HugePageTag::SEMI, HugeSlotAllocPolicy::FULL_GC_TARGET);
    MarkGcTargetChunk(pool, second, HugePageTag::SHARED_OLD, HugeSlotAllocPolicy::FULL_GC_TARGET);
    EXPECT_EQ(GetChunkLocalTargetEpoch(*first), GetLocalTargetEpoch(pool));
    EXPECT_EQ(GetChunkSharedTargetEpoch(*second), GetSharedTargetEpoch(pool));
}

TEST_F(HugeMemMapPoolTest, RefillSummaryFinalizeAndBackgroundDefensivePaths)
{
    HugeMemMapPool pool;
    bool quotaExceeded = false;
    EXPECT_FALSE(CanRefillHugeChunk(pool, 0, 0, quotaExceeded));
    EXPECT_TRUE(quotaExceeded);
    EXPECT_TRUE(CanRefillHugeChunk(pool, 1, 0, quotaExceeded));
    EXPECT_FALSE(quotaExceeded);
    pool.DumpUsageSummaryDfx();
    NotifyBackgroundColdTimeout(pool, 0);
    NotifyBackgroundColdTimeout(pool, 1);
    PostBackgroundColdTask(pool, 0);

    MemMap chunk = MapHugeChunk();
    ASSERT_NE(chunk.GetMem(), nullptr);
    ASSERT_TRUE(InsertHugeChunk(pool, chunk));
    EXPECT_FALSE(CanRefillHugeChunk(pool, 1, std::numeric_limits<size_t>::max(), quotaExceeded));
    EXPECT_TRUE(quotaExceeded);
    MemMap slot = GetHugeSlot(pool, HugePageTag::OLD);
    ASSERT_NE(slot.GetMem(), nullptr);
    pool.DumpUsageSummaryDfx();
    ASSERT_TRUE(ReleaseHugeSlot(pool, slot.GetMem()));
    pool.DumpUsageSummaryDfx();

    EXPECT_FALSE(pool.ConfigureAfterFork());
    EXPECT_FALSE(pool.ConfigureAfterFork());
    EXPECT_EQ(GetLocalTargetEpoch(pool), 1U);
    EXPECT_EQ(GetSharedTargetEpoch(pool), 1U);
    EXPECT_FALSE(pool.IsEnabled());
    pool.Finalize();
    pool.Finalize();
}

TEST_F(HugeMemMapPoolTest, TryAllocateSlotAndQuotaPolicyBranches)
{
    HugeMemMapPool pool;
    EXPECT_EQ(TryAllocateSlot(pool, HugePageTag::INVALID).slot.GetMem(), nullptr);
    SetProcessState({true, false, false, 2, 3, 0});
    EXPECT_EQ(TryAllocateSlot(pool, HugePageTag::OLD).slot.GetMem(), nullptr);

    MemMap chunk = MapHugeChunk();
    ASSERT_NE(chunk.GetMem(), nullptr);
    ASSERT_TRUE(InsertHugeChunk(pool, chunk));
    SetProcessState({true, true, false, 2, 3, 0});
    HugeSlotAllocResult reused = TryAllocateSlot(pool, HugePageTag::OLD);
    ASSERT_NE(reused.slot.GetMem(), nullptr);
    EXPECT_EQ(reused.memMapSizeDelta, HugeMemMapPool::HUGE_REGION_SIZE);
    EXPECT_FALSE(reused.quotaExceeded);

    EXPECT_TRUE(HasActiveSlotWithTag(pool, reused.slot.GetMem(), HugePageTag::OLD));
    EXPECT_FALSE(HasActiveSlotWithTag(pool, reused.slot.GetMem(), HugePageTag::SEMI));
    EXPECT_EQ(CountActiveSlotsWithTag(pool, reused.slot.GetMem(), HugePageTag::OLD), 1U);
    EXPECT_EQ(CountActiveSlotsWithTag(pool, reused.slot.GetMem(), HugePageTag::SEMI), 0U);
#if ENABLE_HUGEPAGE_DFX_STATS
    EXPECT_FALSE(HasPromotedSlot(pool, reused.slot.GetMem()));
    NotifyHugeSlotMoved(pool, reused.slot.GetMem(), HugePageTag::SEMI);
    EXPECT_FALSE(HasPromotedSlot(pool, reused.slot.GetMem()));
    NotifyHugeSlotMoved(pool, reused.slot.GetMem(), HugePageTag::OLD);
    EXPECT_TRUE(HasPromotedSlot(pool, reused.slot.GetMem()));
#endif

    HugeMemMapPool quotaPool;
    bool quotaExceeded = false;
    SetProcessState({true, true, false, 0, 0, 0});
    EXPECT_FALSE(TryReserveQuotaForSlot(quotaPool, HugePageTag::OLD,
                                        HugeSlotAllocPolicy::DEFAULT, quotaExceeded));
    EXPECT_TRUE(quotaExceeded);
    HugeSlotAllocResult denied = TryAllocateSlot(quotaPool, HugePageTag::OLD);
    EXPECT_EQ(denied.slot.GetMem(), nullptr);
    EXPECT_TRUE(denied.quotaExceeded);

    SetProcessState({true, true, false, 8, 0, 0});
    EXPECT_TRUE(TryReserveQuotaForSlot(quotaPool, HugePageTag::OLD,
                                       HugeSlotAllocPolicy::DEFAULT, quotaExceeded));
    EXPECT_FALSE(quotaExceeded);
    ReleaseReservedHugePageQuota(1);
    EXPECT_TRUE(TryReserveQuotaForSlot(quotaPool, HugePageTag::OLD,
                                       HugeSlotAllocPolicy::FULL_GC_TARGET, quotaExceeded));
    EXPECT_FALSE(quotaExceeded);
    ReleaseReservedHugePageQuota(1);
    EXPECT_TRUE(TryReserveQuotaForSlot(quotaPool, HugePageTag::SHARED_OLD,
                                       HugeSlotAllocPolicy::FULL_GC_TARGET, quotaExceeded));
    EXPECT_FALSE(quotaExceeded);
    ReleaseReservedHugePageQuota(1);
    SetProcessState({false, false, false, 0, 0, 0});
}

TEST_F(HugeMemMapPoolTest, BackgroundForegroundAndGcNotificationBranches)
{
    HugeMemMapPool pool;
    SetProcessState({true, true, false, 0, 0, 0});
    TryMadviseColdChunksInBackground(pool);
    EXPECT_FALSE(HasAdvisedAllCold(pool));

    SetBackgroundState(pool, true);
    SetBackgroundStartTime(pool, 1);
    TryMadviseColdChunksInBackground(pool);
    EXPECT_FALSE(HasAdvisedAllCold(pool));

    SetProcessState({true, true, true, 0, 0, 0});
    TryMadviseColdChunksInBackground(pool);
    EXPECT_TRUE(HasAdvisedAllCold(pool));
    TryMadviseColdChunksInBackground(pool);
    EXPECT_TRUE(HasAdvisedAllCold(pool));

    SetBackgroundStartTime(pool, 7);
    NotifyBackgroundColdTimeout(pool, 6);
    NotifyBackgroundColdTimeout(pool, 7);
    SetHasAdvisedAllCold(pool, false);
    NotifyBackgroundColdTimeout(pool, 7);
    EXPECT_TRUE(HasAdvisedAllCold(pool));

    MemMap chunk = MapHugeChunk();
    ASSERT_NE(chunk.GetMem(), nullptr);
    ASSERT_TRUE(InsertHugeChunk(pool, chunk));

    SetBackgroundState(pool, false);
    pool.NotifyGCReclaimFinished(TriggerGCType::OLD_GC);
    SetBackgroundState(pool, true);
    SetProcessState({true, true, false, 0, 0, 0});
    pool.NotifyGCReclaimFinished(TriggerGCType::FULL_GC);
    SetProcessState({true, true, true, 0, 0, 0});
    pool.NotifyGCReclaimFinished(TriggerGCType::FULL_GC);
    pool.NotifyGCReclaimFinished(TriggerGCType::SHARED_FULL_GC);
    pool.NotifyGCReclaimFinished(TriggerGCType::SHARED_GC);
    pool.NotifyGCReclaimFinished(TriggerGCType::YOUNG_GC);

    SetProcessState({true, true, false, 0, 0, 0});
    pool.NotifyApplicationForeground();
    SetProcessState({true, true, true, 0, 0, 0});
    pool.NotifyApplicationForeground();
    SetBackgroundState(pool, true);
    pool.NotifyApplicationForeground();
    pool.Finalize();
    SetProcessState({false, false, false, 0, 0, 0});
}

TEST_F(HugeMemMapPoolTest, ProcessHugePageEnableDisableTransitions)
{
    SetProcessState({true, false, false, 4, 6, 0});
    EXPECT_FALSE(CanTryHugePage());
    EXPECT_FALSE(IsHugePageEnabled());
    MarkHugePageEnabled();
    EXPECT_TRUE(CanTryHugePage());
    EXPECT_TRUE(IsHugePageEnabled());
    MarkHugePageEnabled();

    SetProcessState({true, true, false, 4, 6, 0});
    DisableHugePage(EAGAIN);
    EXPECT_FALSE(CanTryHugePage());
    EXPECT_FALSE(IsHugePageEnabled());

    SetProcessState({true, true, true, 4, 6, 0});
    DisableHugePage(EAGAIN);
    EXPECT_FALSE(CanTryHugePage());
    EXPECT_FALSE(IsHugePageEnabled());

    SetProcessState({true, true, false, 4, 6, 0});
    DisableHugePage(EINVAL);
    EXPECT_FALSE(CanTryHugePage());
    EXPECT_FALSE(IsHugePageEnabled());

    SetProcessState({true, true, true, 4, 6, 0});
    DisableHugePage(38);
    EXPECT_FALSE(CanTryHugePage());
    EXPECT_FALSE(IsHugePageEnabled());
}

TEST_F(HugeMemMapPoolTest, HugePageQuotaReservationIsBoundedAndSaturating)
{
    SetProcessState({true, true, false, 2, 6, 0});
    EXPECT_FALSE(TryReserveHugePageQuota(0));
    EXPECT_TRUE(TryReserveHugePageQuota(2));
    EXPECT_TRUE(TryReserveHugePageQuota(2));
    EXPECT_FALSE(TryReserveHugePageQuota(2));
    EXPECT_EQ(GetAllocatedHugePageCount(), 2U);
    ReleaseReservedHugePageQuota(0);
    EXPECT_EQ(GetAllocatedHugePageCount(), 2U);
    ReleaseReservedHugePageQuota(1);
    EXPECT_EQ(GetAllocatedHugePageCount(), 1U);
    ReleaseReservedHugePageQuota(4);
    EXPECT_EQ(GetAllocatedHugePageCount(), 0U);
    ReleaseReservedHugePageQuota(1);
    EXPECT_EQ(GetAllocatedHugePageCount(), 0U);
}

TEST_F(HugeMemMapPoolTest, HugePageDomainConversions)
{
    EXPECT_EQ(ToChunkDomain(HugePageTag::SEMI), HugeChunkDomain::LOCAL);
    EXPECT_EQ(ToChunkDomain(HugePageTag::OLD), HugeChunkDomain::LOCAL);
    EXPECT_EQ(ToChunkDomain(HugePageTag::SHARED_OLD), HugeChunkDomain::SHARED_OLD);
    EXPECT_EQ(ToChunkDomain(HugePageTag::INVALID), HugeChunkDomain::EMPTY);
}

TEST_F(HugeMemMapPoolTest, HugePageReuseAndPolicyConversions)
{
    EXPECT_TRUE(CanReuseChunkDomain(HugeChunkDomain::EMPTY, HugePageTag::OLD,
                                    HugeSlotAllocPolicy::DEFAULT));
    EXPECT_TRUE(CanReuseChunkDomain(HugeChunkDomain::MIXED, HugePageTag::SHARED_OLD,
                                    HugeSlotAllocPolicy::DEFAULT));
    EXPECT_TRUE(CanReuseChunkDomain(HugeChunkDomain::LOCAL, HugePageTag::SHARED_OLD,
                                    HugeSlotAllocPolicy::FULL_GC_TARGET));
    EXPECT_FALSE(CanReuseChunkDomain(HugeChunkDomain::LOCAL, HugePageTag::SHARED_OLD,
                                     HugeSlotAllocPolicy::DEFAULT));
    EXPECT_TRUE(CanReuseChunkDomain(HugeChunkDomain::LOCAL, HugePageTag::SEMI,
                                    HugeSlotAllocPolicy::DEFAULT));

    EXPECT_EQ(CalculateFreeSlotRefillLimit(0), 0U);
    EXPECT_EQ(CalculateFreeSlotRefillLimit(5), 8U);
}

TEST_F(HugeMemMapPoolTest, SlotTagBranchMatrix)
{
    EXPECT_TRUE(DoesSlotTagMatch(HugePageTag::OLD, HugePageTag::OLD));
    EXPECT_TRUE(DoesSlotTagMatch(HugePageTag::PROMOTED, HugePageTag::OLD));
    EXPECT_FALSE(DoesSlotTagMatch(HugePageTag::SEMI, HugePageTag::OLD));
    EXPECT_TRUE(DoesSlotTagMatch(HugePageTag::SEMI, HugePageTag::SEMI));
    EXPECT_FALSE(DoesSlotTagMatch(HugePageTag::OLD, HugePageTag::SEMI));
}

TEST_F(HugeMemMapPoolTest, GcTargetPredicateBranchMatrix)
{
    HugeMemMapPool pool;
    MemMap chunk = MapHugeChunk();
    ASSERT_NE(chunk.GetMem(), nullptr);
    ASSERT_TRUE(InsertHugeChunk(pool, chunk));
    auto *poolChunk = TakeEmptyChunk(pool);
    ASSERT_NE(poolChunk, nullptr);

    EXPECT_TRUE(ShouldMarkGcTarget(pool, HugePageTag::OLD,
                                   HugeSlotAllocPolicy::FULL_GC_TARGET));
    EXPECT_TRUE(ShouldMarkGcTarget(pool, HugePageTag::SEMI,
                                   HugeSlotAllocPolicy::FULL_GC_TARGET));
    EXPECT_TRUE(ShouldMarkGcTarget(pool, HugePageTag::SHARED_OLD,
                                   HugeSlotAllocPolicy::FULL_GC_TARGET));
    EXPECT_FALSE(ShouldMarkGcTarget(pool, HugePageTag::INVALID,
                                    HugeSlotAllocPolicy::FULL_GC_TARGET));

    SetChunkState(poolChunk, {1U, HugeChunkDomain::SHARED_OLD});
    SetSlotTag(poolChunk, 0, HugePageTag::SHARED_OLD);
    SetChunkTargetEpochs(poolChunk, 0, 0);
    EXPECT_FALSE(IsCurrentGcTargetChunk(pool, *poolChunk, HugePageTag::SHARED_OLD,
                                        HugeSlotAllocPolicy::FULL_GC_TARGET));
    SetChunkTargetEpochs(poolChunk, GetLocalTargetEpoch(pool), 0);
    EXPECT_TRUE(IsCurrentGcTargetChunk(pool, *poolChunk, HugePageTag::SHARED_OLD,
                                       HugeSlotAllocPolicy::FULL_GC_TARGET));
    SetChunkTargetEpochs(poolChunk, 0, GetSharedTargetEpoch(pool));
    EXPECT_TRUE(IsCurrentGcTargetChunk(pool, *poolChunk, HugePageTag::SHARED_OLD,
                                       HugeSlotAllocPolicy::FULL_GC_TARGET));

    MarkGcTargetChunk(pool, poolChunk, HugePageTag::OLD,
                      HugeSlotAllocPolicy::FULL_GC_TARGET);
    EXPECT_EQ(GetChunkLocalTargetEpoch(*poolChunk), GetLocalTargetEpoch(pool));
    MarkGcTargetChunk(pool, poolChunk, HugePageTag::INVALID,
                      HugeSlotAllocPolicy::FULL_GC_TARGET);
    EXPECT_EQ(GetChunkSharedTargetEpoch(*poolChunk), GetSharedTargetEpoch(pool));
}

TEST_F(HugeMemMapPoolTest, ReusableChunkDomainBranchMatrix)
{
    HugeMemMapPool pool;
    MemMap chunk = MapHugeChunk();
    ASSERT_NE(chunk.GetMem(), nullptr);
    ASSERT_TRUE(InsertHugeChunk(pool, chunk));
    auto *poolChunk = TakeEmptyChunk(pool);
    ASSERT_NE(poolChunk, nullptr);
    SetChunkState(poolChunk, {1U, HugeChunkDomain::LOCAL});
    SetSlotTag(poolChunk, 0, HugePageTag::OLD);
    InsertPartialChunk(pool, poolChunk);

    EXPECT_EQ(FindReusableChunk(pool, HugePageTag::OLD,
        HugeSlotAllocPolicy::DEFAULT), poolChunk);
    SetOwnerDomain(poolChunk, HugeChunkDomain::SHARED_OLD);
    EXPECT_EQ(FindReusableChunk(pool, HugePageTag::OLD,
        HugeSlotAllocPolicy::DEFAULT), nullptr);
    SetOwnerDomain(poolChunk, HugeChunkDomain::MIXED);
    EXPECT_EQ(FindReusableChunk(pool, HugePageTag::OLD,
        HugeSlotAllocPolicy::DEFAULT), poolChunk);
    SetOwnerDomain(poolChunk, HugeChunkDomain::EMPTY);
    EXPECT_EQ(FindReusableChunk(pool, HugePageTag::OLD,
        HugeSlotAllocPolicy::DEFAULT), poolChunk);

    SetOwnerDomain(poolChunk, HugeChunkDomain::SHARED_OLD);
    EXPECT_EQ(FindReusableChunk(pool, HugePageTag::SHARED_OLD,
        HugeSlotAllocPolicy::DEFAULT), poolChunk);
    SetOwnerDomain(poolChunk, HugeChunkDomain::LOCAL);
    EXPECT_EQ(FindReusableChunk(pool, HugePageTag::SHARED_OLD,
        HugeSlotAllocPolicy::DEFAULT), nullptr);
    SetOwnerDomain(poolChunk, HugeChunkDomain::MIXED);
    EXPECT_EQ(FindReusableChunk(pool, HugePageTag::SHARED_OLD,
        HugeSlotAllocPolicy::DEFAULT), poolChunk);
}

TEST_F(HugeMemMapPoolTest, FullGcReusableChunkBranchMatrix)
{
    HugeMemMapPool pool;
    MemMap chunk = MapHugeChunk();
    ASSERT_NE(chunk.GetMem(), nullptr);
    ASSERT_TRUE(InsertHugeChunk(pool, chunk));
    auto *poolChunk = TakeEmptyChunk(pool);
    ASSERT_NE(poolChunk, nullptr);
    SetChunkState(poolChunk, {1U, HugeChunkDomain::LOCAL});
    SetSlotTag(poolChunk, 0, HugePageTag::OLD);
    InsertPartialChunk(pool, poolChunk);

    EXPECT_EQ(FindReusableChunk(pool, HugePageTag::SHARED_OLD,
        HugeSlotAllocPolicy::FULL_GC_TARGET), poolChunk);
    SetOwnerDomain(poolChunk, HugeChunkDomain::MIXED);
    EXPECT_EQ(FindReusableChunk(pool, HugePageTag::SHARED_OLD,
        HugeSlotAllocPolicy::FULL_GC_TARGET), poolChunk);
    SetOwnerDomain(poolChunk, HugeChunkDomain::EMPTY);
    EXPECT_EQ(FindReusableChunk(pool, HugePageTag::SHARED_OLD,
        HugeSlotAllocPolicy::FULL_GC_TARGET), poolChunk);

    SetOwnerDomain(poolChunk, HugeChunkDomain::SHARED_OLD);
    SetChunkTargetEpochs(poolChunk, 0, 0);
    EXPECT_EQ(FindReusableChunk(pool, HugePageTag::SHARED_OLD,
        HugeSlotAllocPolicy::FULL_GC_TARGET), nullptr);
    SetChunkTargetEpochs(poolChunk, 0, GetSharedTargetEpoch(pool));
    EXPECT_EQ(FindReusableChunk(pool, HugePageTag::SHARED_OLD,
        HugeSlotAllocPolicy::FULL_GC_TARGET), poolChunk);

    SetChunkState(poolChunk, {1U, HugeChunkDomain::SHARED_OLD, false, false, false, true});
    EXPECT_EQ(FindReusableChunk(pool, HugePageTag::SHARED_OLD,
        HugeSlotAllocPolicy::FULL_GC_TARGET), nullptr);
    SetChunkState(poolChunk, {1U, HugeChunkDomain::SHARED_OLD, false, true});
    EXPECT_EQ(FindReusableChunk(pool, HugePageTag::SHARED_OLD,
        HugeSlotAllocPolicy::FULL_GC_TARGET), nullptr);
}

TEST_F(HugeMemMapPoolTest, RefillAndQuotaReservationBranchMatrix)
{
    HugeMemMapPool pool;
    MemMap chunk = MapHugeChunk();
    ASSERT_NE(chunk.GetMem(), nullptr);
    ASSERT_TRUE(InsertHugeChunk(pool, chunk));
    auto *poolChunk = GetChunk(pool, chunk.GetMem());
    ASSERT_NE(poolChunk, nullptr);

    EXPECT_FALSE(IsFreeSlotRefillLimitReached(pool, 0));
    SetUsageMask(poolChunk, std::numeric_limits<uint8_t>::max());
    EXPECT_FALSE(IsFreeSlotRefillLimitReached(pool, 0));
    SetUsageMask(poolChunk, 0x7FU);
    EXPECT_TRUE(IsFreeSlotRefillLimitReached(pool, 0));
    EXPECT_FALSE(IsFreeSlotRefillLimitReached(pool, 1));

    bool quotaExceeded = true;
    SetProcessState({true, true, true, 2, 0, 0});
    EXPECT_FALSE(TryReserveQuotaForSlot(pool, HugePageTag::OLD,
                                        HugeSlotAllocPolicy::DEFAULT, quotaExceeded));
    EXPECT_FALSE(quotaExceeded);

    SetProcessState({true, true, true, 2, 1, 0});
    EXPECT_TRUE(TryReserveQuotaForSlot(pool, HugePageTag::OLD,
                                       HugeSlotAllocPolicy::DEFAULT, quotaExceeded));
    EXPECT_FALSE(quotaExceeded);
    ReleaseReservedHugePageQuota(1);

    SetProcessState({true, true, true, 2, 1, 2});
    EXPECT_FALSE(TryReserveQuotaForSlot(pool, HugePageTag::OLD,
                                        HugeSlotAllocPolicy::DEFAULT, quotaExceeded));
    EXPECT_TRUE(quotaExceeded);
}

TEST_F(HugeMemMapPoolTest, FullGcQuotaReservationBranchMatrix)
{
    HugeMemMapPool pool;
    MemMap chunk = MapHugeChunk();
    ASSERT_NE(chunk.GetMem(), nullptr);
    ASSERT_TRUE(InsertHugeChunk(pool, chunk));
    auto *poolChunk = GetChunk(pool, chunk.GetMem());
    ASSERT_NE(poolChunk, nullptr);
    SetUsageMask(poolChunk, std::numeric_limits<uint8_t>::max());

    bool quotaExceeded = true;
    SetProcessState({true, true, true, 2, 0, 0});
    EXPECT_TRUE(TryReserveQuotaForSlot(pool, HugePageTag::OLD,
                                       HugeSlotAllocPolicy::FULL_GC_TARGET, quotaExceeded));
    EXPECT_FALSE(quotaExceeded);
    ReleaseReservedHugePageQuota(1);

    SetProcessState({true, true, true, 2, 0, 2});
    EXPECT_FALSE(TryReserveQuotaForSlot(pool, HugePageTag::OLD,
                                        HugeSlotAllocPolicy::FULL_GC_TARGET, quotaExceeded));
    EXPECT_TRUE(quotaExceeded);
}

TEST_F(HugeMemMapPoolTest, OccupySlotDomainTransitionBranchMatrix)
{
    HugeMemMapPool emptyPool;
    MemMap emptyChunk = MapHugeChunk();
    ASSERT_NE(emptyChunk.GetMem(), nullptr);
    ASSERT_TRUE(InsertHugeChunk(emptyPool, emptyChunk));
    auto *empty = TakeEmptyChunk(emptyPool);
    ASSERT_NE(empty, nullptr);
    ASSERT_NE(OccupyFreeSlot(emptyPool, empty, HugePageTag::OLD).GetMem(), nullptr);
    EXPECT_EQ(GetOwnerDomain(*empty), HugeChunkDomain::LOCAL);

    HugeMemMapPool sameDomainPool;
    MemMap sameDomainChunk = MapHugeChunk();
    ASSERT_NE(sameDomainChunk.GetMem(), nullptr);
    ASSERT_TRUE(InsertHugeChunk(sameDomainPool, sameDomainChunk));
    auto *sameDomain = TakeEmptyChunk(sameDomainPool);
    ASSERT_NE(sameDomain, nullptr);
    SetChunkState(sameDomain, {1U, HugeChunkDomain::LOCAL});
    SetSlotTag(sameDomain, 0, HugePageTag::OLD);
    InsertPartialChunk(sameDomainPool, sameDomain);
    ASSERT_NE(OccupyFreeSlot(sameDomainPool, sameDomain, HugePageTag::OLD).GetMem(), nullptr);
    EXPECT_EQ(GetOwnerDomain(*sameDomain), HugeChunkDomain::LOCAL);

    HugeMemMapPool mixedPool;
    MemMap mixedChunk = MapHugeChunk();
    ASSERT_NE(mixedChunk.GetMem(), nullptr);
    ASSERT_TRUE(InsertHugeChunk(mixedPool, mixedChunk));
    auto *mixed = TakeEmptyChunk(mixedPool);
    ASSERT_NE(mixed, nullptr);
    SetChunkState(mixed, {1U, HugeChunkDomain::MIXED});
    SetSlotTag(mixed, 0, HugePageTag::OLD);
    InsertPartialChunk(mixedPool, mixed);
    ASSERT_NE(OccupyFreeSlot(mixedPool, mixed, HugePageTag::SEMI).GetMem(), nullptr);
    EXPECT_EQ(GetOwnerDomain(*mixed), HugeChunkDomain::MIXED);

    HugeMemMapPool crossDomainPool;
    MemMap crossDomainChunk = MapHugeChunk();
    ASSERT_NE(crossDomainChunk.GetMem(), nullptr);
    ASSERT_TRUE(InsertHugeChunk(crossDomainPool, crossDomainChunk));
    auto *crossDomain = TakeEmptyChunk(crossDomainPool);
    ASSERT_NE(crossDomain, nullptr);
    SetChunkState(crossDomain, {1U, HugeChunkDomain::SHARED_OLD});
    SetSlotTag(crossDomain, 0, HugePageTag::SHARED_OLD);
    InsertPartialChunk(crossDomainPool, crossDomain);
    ASSERT_NE(OccupyFreeSlot(crossDomainPool, crossDomain, HugePageTag::OLD).GetMem(), nullptr);
    EXPECT_EQ(GetOwnerDomain(*crossDomain), HugeChunkDomain::MIXED);
}

TEST_F(HugeMemMapPoolTest, RestoreQuarantineAndFinalizeBranchMatrix)
{
    SetProcessState({true, true, true, 4, 4, 1});
    HugeMemMapPool pool;
    std::array<MemMap, 3> chunks;
    for (size_t i = 0; i < chunks.size(); i++) {
        chunks[i] = MapHugeChunk();
        ASSERT_NE(chunks[i].GetMem(), nullptr);
        ASSERT_TRUE(InsertHugeChunk(pool, chunks[i], i == 0));
    }

    auto *notQuarantined = TakeEmptyChunk(pool);
    auto *emptyQuarantined = TakeEmptyChunk(pool);
    auto *activeQuarantined = TakeEmptyChunk(pool);
    ASSERT_NE(notQuarantined, nullptr);
    ASSERT_NE(emptyQuarantined, nullptr);
    ASSERT_NE(activeQuarantined, nullptr);
    SetChunkState(notQuarantined, {1U, HugeChunkDomain::LOCAL});
    SetChunkState(emptyQuarantined, {0, HugeChunkDomain::EMPTY, false, false, false, true});
    SetChunkState(activeQuarantined, {1U, HugeChunkDomain::LOCAL, false, false, false, true});

    EXPECT_TRUE(RestoreQuarantinedChunks(pool));
    EXPECT_FALSE(IsReuseQuarantined(*notQuarantined));
    EXPECT_TRUE(IsReuseQuarantined(*emptyQuarantined));
    EXPECT_FALSE(IsReuseQuarantined(*activeQuarantined));
    EXPECT_EQ(GetPartialChunkCount(pool), 1U);

    pool.Finalize();
    EXPECT_EQ(GetHugeChunkCount(pool), 0U);
    EXPECT_EQ(GetAllocatedHugePageCount(), 0U);
}

TEST_F(HugeMemMapPoolTest, MadviseColdStateBranchMatrix)
{
    constexpr int TEST_VIP_FLAG = 0x100000;
    HugeMemMapPool pool;
    MemMap chunk = MapHugeChunk();
    ASSERT_NE(chunk.GetMem(), nullptr);
    ASSERT_TRUE(InsertHugeChunk(pool, chunk));
    auto *poolChunk = TakeEmptyChunk(pool);
    ASSERT_NE(poolChunk, nullptr);
    SetChunkState(poolChunk, {1U, HugeChunkDomain::LOCAL});
    SetSlotTag(poolChunk, 0, HugePageTag::PROMOTED);

    EXPECT_TRUE(ShouldMadviseChunk(pool, *poolChunk, MADV_COLD, 'O'));
    SetChunkState(poolChunk, {1U, HugeChunkDomain::LOCAL, false, false, true});
    SetSlotTag(poolChunk, 0, HugePageTag::PROMOTED);
    EXPECT_FALSE(ShouldMadviseChunk(pool, *poolChunk, MADV_COLD, 'O'));
    EXPECT_TRUE(ShouldMadviseChunk(pool, *poolChunk, TEST_VIP_FLAG, 'O'));
    EXPECT_TRUE(ShouldMadviseChunk(pool, *poolChunk, TEST_VIP_FLAG, '\0'));
    EXPECT_FALSE(ShouldMadviseChunk(pool, *poolChunk, TEST_VIP_FLAG, 'D'));
}

TEST_F(HugeMemMapPoolTest, BackgroundColdTaskHandlesNullAndPoolTargets)
{
    HugeMemMapPool pool;
    EXPECT_TRUE(RunBackgroundColdTask(nullptr, 0));
    EXPECT_TRUE(RunBackgroundColdTask(&pool, 0));
}

TEST_F(HugeMemMapPoolTest, ReadSystemHugePageCountHandlesBoundaryInputs)
{
    g_inputMode = HugeMemMapPoolTestInputMode::OPEN_FAILURE;
    EXPECT_EQ(ReadSystemHugePageCount(), 0U);
    g_inputMode = HugeMemMapPoolTestInputMode::READ_FAILURE;
    EXPECT_EQ(ReadSystemHugePageCount(), 0U);
    g_inputMode = HugeMemMapPoolTestInputMode::ZERO;
    EXPECT_EQ(ReadSystemHugePageCount(), 0U);
    g_inputMode = HugeMemMapPoolTestInputMode::SUB_PAGE;
    EXPECT_EQ(ReadSystemHugePageCount(), 0U);
    g_inputMode = HugeMemMapPoolTestInputMode::TWO_PAGES;
    EXPECT_EQ(ReadSystemHugePageCount(), 1U);
    SetProcessState({false, false, false, 0, 0, 0});
    InitHugePageLimit("unit test");
    EXPECT_EQ(GetMaxProcessHugePages(), 1U);
    EXPECT_EQ(panda::ecmascript::GetFreeSlotRefillLimit(), 1U);

    HugeMemMapPool pool;
    MarkHugePageEnabled();
    ASSERT_TRUE(TryReserveHugePageQuota(1));
    g_inputMode = HugeMemMapPoolTestInputMode::ZERO;
    ResetHugePageProcessStateAfterForkInternal();
    EXPECT_EQ(GetMaxProcessHugePages(), 1U);
    EXPECT_TRUE(CanTryHugePage());
    EXPECT_FALSE(IsHugePageEnabled());
    EXPECT_EQ(GetAllocatedHugePageCount(), 0U);
}

TEST_F(HugeMemMapPoolTest, PrepareBeforeForkInitializesKernelQuotaOnlyOnce)
{
    HugeMemMapPool pool;
    SetProcessState({false, false, false, 0, 0, 0});
    g_inputMode = HugeMemMapPoolTestInputMode::TWO_PAGES;
    pool.PrepareBeforeFork();
    EXPECT_EQ(GetMaxProcessHugePages(), 1U);
    EXPECT_TRUE(CanTryHugePage());

    g_inputMode = HugeMemMapPoolTestInputMode::ZERO;
    pool.PrepareBeforeFork();
    EXPECT_EQ(GetMaxProcessHugePages(), 1U);
    EXPECT_TRUE(CanTryHugePage());
}

TEST_F(HugeMemMapPoolTest, HugePageTagAndMadviseSystemBoundaries)
{
    constexpr int TEST_VIP_FLAG = 0x100000;
    EXPECT_EQ(ecmascript::ToHugePageTag(static_cast<char>(HugePageTag::SEMI)),
              HugePageTag::SEMI);
    EXPECT_EQ(ecmascript::ToHugePageTag(static_cast<char>(HugePageTag::OLD)),
              HugePageTag::OLD);
    EXPECT_EQ(ecmascript::ToHugePageTag(static_cast<char>(HugePageTag::SHARED_OLD)),
              HugePageTag::SHARED_OLD);
    EXPECT_EQ(ecmascript::ToHugePageTag('?'), HugePageTag::INVALID);
    EXPECT_GT(GetMonotonicTimeUs(), 0U);

    SetProcessState({true, true, false, 8, 8, 0});
    g_madviseResult = -1;
    g_madviseErrno = EINVAL;
    EXPECT_FALSE(TryMadviseTarget(1, TEST_VIP_FLAG, "unit test"));
    EXPECT_FALSE(CanTryHugePage());
    EXPECT_FALSE(IsHugePageEnabled());
    size_t callCountAfterSingleFailure = g_madviseCallCount;
    EXPECT_TRUE(AdviseTargets({1, 2}, TEST_VIP_FLAG, "unit test").empty());
    EXPECT_EQ(g_madviseCallCount, callCountAfterSingleFailure + 1);
    g_madviseErrno = EAGAIN;
    EXPECT_FALSE(TryMadviseTarget(1, MADV_COLD, "unit test"));
    EXPECT_TRUE(AdviseTargets({1, 2}, MADV_COLD, "unit test").empty());
    g_madviseResult = 0;
    g_madviseErrno = 0;
    EXPECT_EQ(AdviseTargets({1, 2}, MADV_COLD, "unit test").size(), 2U);
}

TEST_F(HugeMemMapPoolTest, AllocateHugeChunkHandlesPlatformFailures)
{
    HugeMemMapPool pool;
#if USE_COMPRESSED_HUGE_PAGE_POOL
    EXPECT_EQ(AllocateHugeChunk(pool, 0).GetMem(), nullptr);

    CompressedHugePagePool backing;
    MemMap reserved = MapCompressedBacking(2);
    ASSERT_NE(reserved.GetMem(), nullptr);
    InitializeCompressedBacking(pool, backing, reserved);

    g_failPageMap = true;
    EXPECT_EQ(AllocateHugeChunk(pool, 0).GetMem(), nullptr);
    g_failPageMap = false;
#else
    g_failPageMap = true;
    EXPECT_EQ(AllocateHugeChunk(pool, 0).GetMem(), nullptr);
    g_failPageMap = false;
#endif

    SetProcessState({true, true, false, 8, 8, 0});
    g_madviseResult = 0;
    MemMap allocated = AllocateHugeChunk(pool, 0);
    ASSERT_NE(allocated.GetMem(), nullptr);
    EXPECT_TRUE(IsHugePageEnabled());
#if USE_COMPRESSED_HUGE_PAGE_POOL
    ReleaseHugeChunkMemory(pool, allocated);
#else
    PageUnmap(allocated);
#endif

    SetProcessState({true, true, false, 8, 8, 0});
    g_madviseResult = -1;
    g_madviseErrno = EINVAL;
    EXPECT_EQ(AllocateHugeChunk(pool, 0).GetMem(), nullptr);
    EXPECT_FALSE(CanTryHugePage());
#if USE_COMPRESSED_HUGE_PAGE_POOL
    pool.Finalize();
    backing.Finalize();
#endif
}

#if USE_COMPRESSED_HUGE_PAGE_POOL
TEST_F(HugeMemMapPoolTest, CompressedBackingAcquireReturnAndRelease)
{
    CompressedHugePagePool backing;
    MemMap reserved = MapCompressedBacking(2);
    ASSERT_NE(reserved.GetMem(), nullptr);
    backing.Initialize(reserved);
    EXPECT_TRUE(backing.HasReservedMemory());
    EXPECT_EQ(backing.GetChunkCapacity(), 2U);

    MemMap first = backing.AcquireChunk();
    MemMap second = backing.AcquireChunk();
    ASSERT_NE(first.GetMem(), nullptr);
    ASSERT_NE(second.GetMem(), nullptr);
    EXPECT_NE(first.GetMem(), second.GetMem());
    EXPECT_EQ(backing.AcquireChunk().GetMem(), nullptr);
    EXPECT_EQ(backing.ReleaseReservedMemory().GetMem(), nullptr);

    backing.ReturnChunk(first);
    backing.ReturnChunk(second);
    MemMap detached = backing.ReleaseReservedMemory();
    EXPECT_EQ(detached.GetMem(), reserved.GetMem());
    EXPECT_EQ(detached.GetSize(), reserved.GetSize());
    EXPECT_FALSE(backing.HasReservedMemory());
    PageUnmap(detached);
}

TEST_F(HugeMemMapPoolTest, CompressedBackingValidatesReturnAndContainmentInputs)
{
    CompressedHugePagePool backing;
    MemMap reserved = MapCompressedBacking(2);
    ASSERT_NE(reserved.GetMem(), nullptr);

    EXPECT_FALSE(backing.Contains(reserved));
    backing.Initialize(reserved);
    EXPECT_FALSE(backing.Contains(MemMap()));

    uintptr_t base = ToUintPtr(reserved.GetMem());
    MemMap oversized(reserved.GetMem(), reserved.GetSize() + 1);
    MemMap belowBase(reinterpret_cast<void *>(base - HugeMemMapPool::HUGE_REGION_SIZE),
                     HugeMemMapPool::HUGE_REGION_SIZE);
    MemMap beyondEnd(reinterpret_cast<void *>(base + reserved.GetSize()),
                     HugeMemMapPool::HUGE_REGION_SIZE);
    EXPECT_FALSE(backing.Contains(oversized));
    EXPECT_FALSE(backing.Contains(belowBase));
    EXPECT_FALSE(backing.Contains(beyondEnd));
    EXPECT_TRUE(backing.Contains(MemMap(reserved.GetMem(), HugeMemMapPool::HUGE_REGION_SIZE)));

    // The standalone test replaces fatal logging with a sink, so invalid returns
    // can verify the guards without terminating the test process.
    backing.ReturnChunk(MemMap(reserved.GetMem(), DEFAULT_REGION_SIZE));
    backing.ReturnChunk(MemMap(reserved.GetMem(), HugeMemMapPool::HUGE_REGION_SIZE));
    backing.ReturnChunk(MemMap(reinterpret_cast<void *>(base + DEFAULT_REGION_SIZE),
                               HugeMemMapPool::HUGE_REGION_SIZE));

    MemMap first = backing.AcquireChunk();
    MemMap second = backing.AcquireChunk();
    ASSERT_NE(first.GetMem(), nullptr);
    ASSERT_NE(second.GetMem(), nullptr);
    EXPECT_EQ(backing.AcquireChunk().GetMem(), nullptr);
    backing.ReturnChunk(first);
    backing.ReturnChunk(second);
    ClearCompressedFreeChunksForTest(backing);
    backing.ReturnChunk(MemMap(reserved.GetMem(), HugeMemMapPool::HUGE_REGION_SIZE));
    MemMap detached = backing.ReleaseReservedMemory();
    ASSERT_EQ(detached.GetMem(), reserved.GetMem());
    PageUnmap(detached);
}

TEST_F(HugeMemMapPoolTest, ConfigureAfterForkRejectsMissingCompressedBacking)
{
    g_inputMode = HugeMemMapPoolTestInputMode::TWO_PAGES;

    HugeMemMapPool missingBackingPool;
    EXPECT_FALSE(missingBackingPool.ConfigureAfterFork());
    EXPECT_FALSE(CanTryHugePage());

    CompressedHugePagePool emptyBacking;
    HugeMemMapPool emptyBackingPool;
    emptyBackingPool.SetCompressedBackingPool(&emptyBacking);
    EXPECT_FALSE(emptyBackingPool.ConfigureAfterFork());
    EXPECT_FALSE(CanTryHugePage());
}

TEST_F(HugeMemMapPoolTest, ConfigureAfterForkRejectsZeroCompressedQuota)
{
    CompressedHugePagePool backing;
    HugeMemMapPool pool;
    MemMap reserved = panda::ecmascript::PageMapForHugeMemMapPoolTest(
        DEFAULT_REGION_SIZE, PAGE_PROT_NONE, HugeMemMapPool::HUGE_REGION_SIZE);
    ASSERT_NE(reserved.GetMem(), nullptr);
    InitializeCompressedBacking(pool, backing, reserved);
    g_inputMode = HugeMemMapPoolTestInputMode::TWO_PAGES;

    EXPECT_FALSE(pool.ConfigureAfterFork());
    EXPECT_FALSE(CanTryHugePage());
    EXPECT_EQ(g_madviseCallCount, 0U);

    MemMap detached = backing.ReleaseReservedMemory();
    ASSERT_EQ(detached.GetMem(), reserved.GetMem());
    pool.Finalize();
    PageUnmap(detached);
}

TEST_F(HugeMemMapPoolTest, ConfigureAfterForkHandlesProbeRegistrationFailure)
{
    CompressedHugePagePool backing;
    HugeMemMapPool pool;
    MemMap reserved = MapCompressedBacking(1);
    ASSERT_NE(reserved.GetMem(), nullptr);
    InitializeCompressedBacking(pool, backing, reserved);
    g_inputMode = HugeMemMapPoolTestInputMode::TWO_PAGES;
    g_madviseCallback = [&pool](void *addr) {
        g_madviseCallback = {};
        ASSERT_TRUE(InsertHugeChunk(
            pool, MemMap(addr, HugeMemMapPool::HUGE_REGION_SIZE)));
    };

    EXPECT_FALSE(pool.ConfigureAfterFork());
    EXPECT_FALSE(CanTryHugePage());
    EXPECT_TRUE(pool.HasChunks());
    EXPECT_EQ(GetAllocatedHugePageCount(), 0U);

    g_madviseCallback = {};
    pool.Finalize();
    MemMap detached = backing.ReleaseReservedMemory();
    ASSERT_EQ(detached.GetMem(), reserved.GetMem());
    PageUnmap(detached);
}

TEST_F(HugeMemMapPoolTest, ConfigureAfterForkKeepsCompressedBackingAfterVipSuccess)
{
    CompressedHugePagePool backing;
    HugeMemMapPool pool;
    MemMap reserved = MapCompressedBacking(2);
    ASSERT_NE(reserved.GetMem(), nullptr);
    InitializeCompressedBacking(pool, backing, reserved);
    g_inputMode = HugeMemMapPoolTestInputMode::TWO_PAGES;
    g_madviseResult = 0;

    EXPECT_TRUE(pool.ConfigureAfterFork());
    EXPECT_TRUE(pool.ConfigureAfterFork());
    EXPECT_TRUE(IsHugePageEnabled());
    EXPECT_TRUE(CanTryHugePage());
    EXPECT_TRUE(pool.HasChunks());
    EXPECT_EQ(GetAllocatedHugePageCount(), 1U);
    EXPECT_EQ(g_madviseCallCount, 1U);

    HugeSlotAllocResult result = TryAllocateSlot(pool, HugePageTag::OLD);
    ASSERT_NE(result.slot.GetMem(), nullptr);
    EXPECT_EQ(result.memMapSizeDelta, HugeMemMapPool::HUGE_REGION_SIZE);
    EXPECT_EQ(g_madviseCallCount, 1U);
    EXPECT_EQ(ToUintPtr(result.slot.GetMem()) & ~(HugeMemMapPool::HUGE_REGION_SIZE - 1),
              ToUintPtr(reserved.GetMem()));

    pool.Finalize();
    EXPECT_EQ(GetAllocatedHugePageCount(), 0U);
    backing.Finalize();
}

TEST_F(HugeMemMapPoolTest, ConfigureAfterForkRecyclesProbeAfterVipFailure)
{
    CompressedHugePagePool backing;
    HugeMemMapPool pool;
    MemMap reserved = MapCompressedBacking(2);
    ASSERT_NE(reserved.GetMem(), nullptr);
    InitializeCompressedBacking(pool, backing, reserved);
    g_inputMode = HugeMemMapPoolTestInputMode::TWO_PAGES;
    g_madviseResult = -1;
    g_madviseErrno = EINVAL;

    EXPECT_FALSE(pool.ConfigureAfterFork());
    EXPECT_FALSE(pool.ConfigureAfterFork());
    EXPECT_FALSE(IsHugePageEnabled());
    EXPECT_FALSE(CanTryHugePage());
    EXPECT_FALSE(pool.HasChunks());
    EXPECT_EQ(GetAllocatedHugePageCount(), 0U);
    EXPECT_EQ(g_madviseCallCount, 1U);

    MemMap detached = backing.ReleaseReservedMemory();
    EXPECT_EQ(detached.GetMem(), reserved.GetMem());
    EXPECT_EQ(detached.GetSize(), reserved.GetSize());
    pool.Finalize();
    PageUnmap(detached);
}
#else
TEST_F(HugeMemMapPoolTest, ConfigureAfterForkProbesNonCompressedHugePagePool)
{
    HugeMemMapPool pool;
    g_inputMode = HugeMemMapPoolTestInputMode::TWO_PAGES;
    g_madviseResult = 0;

    EXPECT_TRUE(pool.ConfigureAfterFork());
    EXPECT_TRUE(IsHugePageEnabled());
    EXPECT_TRUE(CanTryHugePage());
    EXPECT_TRUE(pool.HasChunks());
    EXPECT_EQ(GetAllocatedHugePageCount(), 1U);

    pool.Finalize();
    EXPECT_EQ(GetAllocatedHugePageCount(), 0U);
}
#endif

TEST_F(HugeMemMapPoolTest, TryAllocateSlotCreatesHugeChunk)
{
    HugeMemMapPool pool;
#if USE_COMPRESSED_HUGE_PAGE_POOL
    CompressedHugePagePool backing;
    MemMap reserved = MapCompressedBacking(2);
    ASSERT_NE(reserved.GetMem(), nullptr);
    InitializeCompressedBacking(pool, backing, reserved);
#endif
    SetProcessState({true, true, false, 8, 8, 0});
    g_madviseResult = 0;
    HugeSlotAllocResult newChunkResult =
        TryAllocateSlot(pool, HugePageTag::OLD);
    ASSERT_NE(newChunkResult.slot.GetMem(), nullptr);
    EXPECT_FALSE(newChunkResult.quotaExceeded);
    EXPECT_EQ(newChunkResult.memMapSizeDelta, HugeMemMapPool::HUGE_REGION_SIZE);
#if USE_COMPRESSED_HUGE_PAGE_POOL
    pool.Finalize();
    backing.Finalize();
#endif
}

TEST_F(HugeMemMapPoolTest, FullGcReuseRejectsIncompatibleChunkDomain)
{
    EXPECT_FALSE(CanReuseChunkDomain(HugeChunkDomain::SHARED_OLD, HugePageTag::OLD,
                                     HugeSlotAllocPolicy::FULL_GC_TARGET));
    EXPECT_TRUE(CanReuseChunkDomain(HugeChunkDomain::SHARED_OLD, HugePageTag::SHARED_OLD,
                                    HugeSlotAllocPolicy::FULL_GC_TARGET));
}

#if USE_COMPRESSED_HUGE_PAGE_POOL
TEST_F(HugeMemMapPoolTest, MissingCompressedBackingRollsBackReservedQuota)
{
    SetProcessState({true, true, false, 4, 4, 0});
    HugeMemMapPool pool;
    HugeSlotAllocResult result =
        TryAllocateSlot(pool, HugePageTag::OLD);
    EXPECT_EQ(result.slot.GetMem(), nullptr);
    EXPECT_FALSE(result.quotaExceeded);
    EXPECT_EQ(GetAllocatedHugePageCount(), 0U);
}
#else
TEST_F(HugeMemMapPoolTest, PageMapFailureRollsBackReservedQuota)
{
    SetProcessState({true, true, false, 4, 4, 0});
    HugeMemMapPool pool;
    g_failPageMap = true;
    HugeSlotAllocResult result =
        TryAllocateSlot(pool, HugePageTag::OLD);
    EXPECT_EQ(result.slot.GetMem(), nullptr);
    EXPECT_FALSE(result.quotaExceeded);
    EXPECT_EQ(GetAllocatedHugePageCount(), 0U);
}
#endif

#if !USE_COMPRESSED_HUGE_PAGE_POOL
TEST_F(HugeMemMapPoolTest, DuplicateChunkInsertionRollsBackReservedQuota)
{
    SetProcessState({true, true, false, 4, 4, 0});
    HugeMemMapPool pool;
    MemMap chunk = MapHugeChunk();
    ASSERT_NE(chunk.GetMem(), nullptr);
    ASSERT_TRUE(InsertHugeChunk(pool, chunk));
    for (size_t i = 0; i < HugeMemMapPool::SLOTS_PER_CHUNK; i++) {
        ASSERT_NE(GetHugeSlot(pool, HugePageTag::OLD).GetMem(), nullptr);
    }

    g_pageMapOverride = chunk.GetMem();
    HugeSlotAllocResult result =
        TryAllocateSlot(pool, HugePageTag::OLD);
    g_pageMapOverride = nullptr;
    EXPECT_EQ(result.slot.GetMem(), nullptr);
    EXPECT_FALSE(result.quotaExceeded);
    EXPECT_EQ(GetAllocatedHugePageCount(), 0U);
}
#endif

TEST_F(HugeMemMapPoolTest, MissingSlotAfterChunkInsertionRemovesChunkAndQuota)
{
    SetProcessState({true, true, false, 4, 4, 0});
    HugeMemMapPool pool;
#if USE_COMPRESSED_HUGE_PAGE_POOL
    CompressedHugePagePool backing;
    MemMap reserved = MapCompressedBacking(2);
    ASSERT_NE(reserved.GetMem(), nullptr);
    InitializeCompressedBacking(pool, backing, reserved);
#endif
    void *insertedAddress = nullptr;
    g_pageTagCallback = [&pool, &insertedAddress](void *mem) {
        insertedAddress = mem;
        auto *chunk = TakeEmptyChunk(pool);
        ASSERT_NE(chunk, nullptr);
        SetChunkState(chunk, {std::numeric_limits<uint8_t>::max(), HugeChunkDomain::LOCAL});
    };
    HugeSlotAllocResult result =
        TryAllocateSlot(pool, HugePageTag::OLD);
    g_pageTagCallback = {};
    EXPECT_EQ(result.slot.GetMem(), nullptr);
    ASSERT_NE(insertedAddress, nullptr);
    auto *chunk = GetChunk(pool, insertedAddress);
    ASSERT_NE(chunk, nullptr);
    SetChunkState(chunk, {0, HugeChunkDomain::EMPTY});
    ResetEmptyChunk(pool, chunk);
    EXPECT_TRUE(RemoveHugeChunk(
        pool, MemMap(insertedAddress, HugeMemMapPool::HUGE_REGION_SIZE)));
    EXPECT_EQ(GetAllocatedHugePageCount(), 0U);
#if USE_COMPRESSED_HUGE_PAGE_POOL
    pool.Finalize();
    backing.Finalize();
#endif
}

TEST_F(HugeMemMapPoolTest, DefensiveReleaseRefillAndSelectionBranches)
{
    HugeMemMapPool pool;
    MemMap chunk = MapHugeChunk();
    ASSERT_NE(chunk.GetMem(), nullptr);
    ASSERT_TRUE(InsertHugeChunk(pool, chunk));
    auto *poolChunk = TakeEmptyChunk(pool);
    ASSERT_NE(poolChunk, nullptr);

    SetChunkState(poolChunk, {std::numeric_limits<uint8_t>::max(), HugeChunkDomain::LOCAL});
    EXPECT_EQ(OccupyFreeSlot(pool, poolChunk, HugePageTag::OLD).GetMem(), nullptr);
    SetChunkState(poolChunk, {0, HugeChunkDomain::EMPTY});
    ResetEmptyChunk(pool, poolChunk);

    MemMap slot = GetHugeSlot(pool, HugePageTag::OLD);
    ASSERT_NE(slot.GetMem(), nullptr);
    EXPECT_TRUE(pool.ReleaseSlotIfOwned(slot.GetMem(), DEFAULT_REGION_SIZE / 2));
    EXPECT_TRUE(ReleaseHugeSlot(pool, slot.GetMem()));

    SetChunkState(poolChunk, {1U, HugeChunkDomain::LOCAL});
    EXPECT_EQ(SoftReleaseEmptyChunks(pool), 0U);
    SetChunkState(poolChunk, {0, HugeChunkDomain::EMPTY, true, true});
    EXPECT_EQ(SoftReleaseEmptyChunks(pool), 0U);
    SetChunkState(poolChunk, {0, HugeChunkDomain::EMPTY, false, true});
    EXPECT_EQ(SoftReleaseEmptyChunks(pool), 0U);
    SetChunkState(poolChunk, {0, HugeChunkDomain::EMPTY, true});
    EXPECT_EQ(SoftReleaseEmptyChunks(pool), 0U);
    SetChunkState(poolChunk, {0, HugeChunkDomain::EMPTY});
    EXPECT_EQ(SoftReleaseEmptyChunks(pool), 1U);

    SetProcessState({true, true, false, 1, 0, 0});
    bool quotaExceeded = false;
    EXPECT_FALSE(TryReserveQuotaForSlot(pool, HugePageTag::OLD,
                                        HugeSlotAllocPolicy::FULL_GC_TARGET, quotaExceeded));
    EXPECT_TRUE(quotaExceeded);

    HugeMemMapPool quotaPool;
    SetProcessState({true, true, false, 1, 0, 1});
    HugeSlotAllocResult fullGcResult = TryAllocateSlot(
        quotaPool, HugePageTag::OLD, HugeSlotAllocPolicy::FULL_GC_TARGET);
    EXPECT_EQ(fullGcResult.slot.GetMem(), nullptr);
    EXPECT_TRUE(fullGcResult.quotaExceeded);
    SetProcessState({false, false, false, 0, 0, 0});
}

TEST_F(HugeMemMapPoolTest, NotifyApplicationBackgroundPostsOnlyOnTransition)
{
    common::Taskpool *taskpool = common::Taskpool::GetCurrentTaskpool();
    taskpool->Initialize(1);
    HugeMemMapPool pool;
    // NotifyApplicationBackground short-circuits unless huge pages are enabled.
    SetProcessState({true, true, true, 0, 0, 0});
    pool.NotifyApplicationBackground();
    EXPECT_TRUE(IsInBackground(pool));
    uint64_t firstBackgroundStart = GetBackgroundStartTime(pool);
    EXPECT_NE(firstBackgroundStart, 0U);
    pool.NotifyApplicationBackground();
    EXPECT_EQ(GetBackgroundStartTime(pool), firstBackgroundStart);
    pool.NotifyApplicationForeground();
    EXPECT_FALSE(IsInBackground(pool));
    taskpool->Destroy(common::GLOBAL_TASK_ID);
}

TEST_F(HugeMemMapPoolTest, ConcurrentQuotaReservationAndReleaseKeepAccountingConsistent)
{
    constexpr size_t QUOTA_LIMIT = 64;
    constexpr size_t THREAD_COUNT = 8;
    SetProcessState({true, true, false, QUOTA_LIMIT, QUOTA_LIMIT, 0});
    std::atomic<size_t> reservedCount {0};
    std::array<std::thread, THREAD_COUNT> reserveThreads;
    for (auto &thread : reserveThreads) {
        thread = std::thread([&reservedCount]() {
            while (TryReserveHugePageQuota(QUOTA_LIMIT)) {
                reservedCount.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }
    for (auto &thread : reserveThreads) {
        thread.join();
    }
    EXPECT_EQ(reservedCount.load(std::memory_order_relaxed), QUOTA_LIMIT);
    EXPECT_EQ(GetAllocatedHugePageCount(), QUOTA_LIMIT);

    std::array<std::thread, THREAD_COUNT> releaseQuotaThreads;
    for (auto &thread : releaseQuotaThreads) {
        thread = std::thread([]() {
            for (size_t i = 0; i < QUOTA_LIMIT / THREAD_COUNT; i++) {
                ReleaseReservedHugePageQuota(1);
            }
        });
    }
    for (auto &thread : releaseQuotaThreads) {
        thread.join();
    }
    EXPECT_EQ(GetAllocatedHugePageCount(), 0U);
    SetProcessState({false, false, false, 0, 0, 0});
}

TEST_F(HugeMemMapPoolTest, ConcurrentSlotReleaseKeepsChunkAccountingConsistent)
{
    constexpr size_t THREAD_COUNT = 8;
    HugeMemMapPool pool;
    constexpr size_t CHUNK_COUNT = 4;
    constexpr size_t SLOT_COUNT = CHUNK_COUNT * HugeMemMapPool::SLOTS_PER_CHUNK;
    for (size_t i = 0; i < CHUNK_COUNT; i++) {
        MemMap chunk = MapHugeChunk();
        ASSERT_NE(chunk.GetMem(), nullptr);
        ASSERT_TRUE(InsertHugeChunk(pool, chunk));
    }
    std::array<MemMap, SLOT_COUNT> slots;
    for (auto &slot : slots) {
        slot = GetHugeSlot(pool, HugePageTag::OLD);
        ASSERT_NE(slot.GetMem(), nullptr);
    }

    std::atomic<size_t> releaseFailures {0};
    std::array<std::thread, THREAD_COUNT> releaseSlotThreads;
    for (size_t threadIndex = 0; threadIndex < THREAD_COUNT; threadIndex++) {
        releaseSlotThreads[threadIndex] = std::thread([&pool, &slots, &releaseFailures, threadIndex]() {
            for (size_t slotIndex = threadIndex; slotIndex < SLOT_COUNT; slotIndex += THREAD_COUNT) {
                if (!ReleaseHugeSlot(pool, slots[slotIndex].GetMem())) {
                    releaseFailures.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }
    for (auto &thread : releaseSlotThreads) {
        thread.join();
    }
    EXPECT_EQ(releaseFailures.load(std::memory_order_relaxed), 0U);
    EXPECT_EQ(GetReleasedEmptyChunkCount(pool), CHUNK_COUNT);
    EXPECT_NE(GetHugeSlot(pool, HugePageTag::OLD).GetMem(), nullptr);
}

} // namespace panda::test
#endif // ENABLE_HUGE_PAGE_MEM