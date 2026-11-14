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

#ifndef ECMASCRIPT_MEM_HUGE_MEM_MAP_POOL_H
#define ECMASCRIPT_MEM_HUGE_MEM_MAP_POOL_H

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <list>
#include <string>
#include <unordered_map>
#include <vector>

#include "ecmascript/base/config.h"
#include "ecmascript/common_enum.h"
#include "ecmascript/ecma_macros.h"
#include "ecmascript/mem/mem_common.h"
#include "ecmascript/platform/map.h"
#include "ecmascript/platform/mutex.h"

#ifndef ENABLE_HUGE_PAGE_MEM
#define ENABLE_HUGE_PAGE_MEM 0
#endif

#ifdef HUGE_MEM_MAP_POOL_STANDALONE_TEST
#undef ENABLE_HUGE_PAGE_MEM
#define ENABLE_HUGE_PAGE_MEM 1
#endif

#if ENABLE_HUGE_PAGE_MEM && (!defined(WIN_OR_MAC_OR_IOS_PLATFORM) || !WIN_OR_MAC_OR_IOS_PLATFORM)
#include <sys/mman.h>
#endif

#ifndef ENABLE_HUGEPAGE_DFX_STATS
#define ENABLE_HUGEPAGE_DFX_STATS 0
#endif

#if !ENABLE_HUGE_PAGE_MEM
#undef ENABLE_HUGEPAGE_DFX_STATS
#define ENABLE_HUGEPAGE_DFX_STATS 0
#endif

#if ENABLE_HUGE_PAGE_MEM && \
    ((defined(WIN_OR_MAC_OR_IOS_PLATFORM) && WIN_OR_MAC_OR_IOS_PLATFORM) || !defined(MADV_COLD))
#error "ENABLE_HUGE_PAGE_MEM requires MADV_COLD on a supported platform"
#endif

#if ENABLE_HUGE_PAGE_MEM && defined(USE_COMPRESSED_POINTER)
#define USE_COMPRESSED_HUGE_PAGE_POOL 1
#else
#define USE_COMPRESSED_HUGE_PAGE_POOL 0
#endif

#ifdef HUGE_MEM_MAP_POOL_STANDALONE_TEST
#define HUGE_MEM_MAP_POOL_STANDALONE_TEST_BUILD 1
#else
#define HUGE_MEM_MAP_POOL_STANDALONE_TEST_BUILD 0
#endif

namespace panda::test {
class HeapTest;
class HugeMemMapPoolTest;
class MemMapAllocatorTest;
}  // namespace panda::test

namespace panda::ecmascript {

#if ENABLE_HUGE_PAGE_MEM
enum class HugeSlotAllocPolicy : uint8_t {
    DEFAULT,
    FULL_GC_TARGET,
};

enum class HugePageTag : uint8_t {
    INVALID = 0,
    SEMI = 'S',
    OLD = 'O',
    PROMOTED = 'p',  // SEMI slot promoted to OLD by GC
    SHARED_OLD = 'D',
};

enum class HugeChunkDomain : uint8_t {
    EMPTY,
    LOCAL,
    SHARED_OLD,
    MIXED,  // chunk has both local and shared slots
};

struct HugeSlotAllocResult {
    MemMap slot;
    size_t memMapSizeDelta {0};
    bool quotaExceeded {false};
};

#if USE_COMPRESSED_HUGE_PAGE_POOL
class CompressedHugePagePool {
public:
    void Initialize(MemMap reservedMem);
    void Finalize();
    MemMap AcquireChunk();
    void ReturnChunk(MemMap chunk);
    MemMap ReleaseReservedMemory();
    bool Contains(const MemMap &chunk) const;
    bool HasReservedMemory() const;
    size_t GetChunkCapacity() const;

private:
    mutable Mutex lock_;
    MemMap reservedMem_;
    std::vector<MemMap> freeChunks_;
    size_t checkedOutChunkCount_ {0};

    friend class panda::test::HugeMemMapPoolTest;
};
#endif

class HugeMemMapPool {
public:
    static constexpr size_t HUGE_REGION_SIZE = 2 * 1024 * 1024; // 2MB
    static constexpr size_t SLOTS_PER_CHUNK = HUGE_REGION_SIZE / DEFAULT_REGION_SIZE; // 8

    HugeMemMapPool() = default;
    ~HugeMemMapPool();

    void Finalize();
    HugeSlotAllocResult TryAllocateSlot(HugePageTag tag, const std::string &spaceName, size_t alignment,
                                        HugeSlotAllocPolicy policy = HugeSlotAllocPolicy::DEFAULT);
    bool ReleaseSlotIfOwned(void *mem, size_t size, size_t *memMapSizeDelta = nullptr);
#if ENABLE_HUGEPAGE_DFX_STATS
    void NotifySlotMoved(void *mem, HugePageTag tag);
#endif
    void BeginSlotReleaseBatch();
    void EndSlotReleaseBatch();
    bool HasChunks() const;
    void NotifyApplicationBackground();
    void NotifyGCReclaimFinished(TriggerGCType gcType);
    void NotifyApplicationForeground();
    void PrepareBeforeFork();
    bool ConfigureAfterFork();
    bool IsEnabled() const;
    void DumpUsageSummaryDfx();
#if ENABLE_HUGEPAGE_DFX_STATS
    void DumpUsageDfx(const std::string &reason = "");
#endif

#if USE_COMPRESSED_HUGE_PAGE_POOL
    void SetCompressedBackingPool(CompressedHugePagePool *pool)
    {
        compressedBackingPool_ = pool;
    }
#endif

private:
    class BackgroundColdTask;

    static constexpr size_t PARTIAL_BUCKET_COUNT = SLOTS_PER_CHUNK - 1;

    struct Chunk {
        uintptr_t baseAddr_;
        uint8_t usageMask_;
        HugeChunkDomain ownerDomain_ {HugeChunkDomain::EMPTY};
        bool isPhysicalReleased_ = false;
        bool isAdvising_ = false;
        bool isColdAdvised_ = false;
        bool isReuseQuarantined_ = false;
        bool ownsQuotaReservation_ = false;
        bool isMemMapCounted_ = false;
        uint64_t localFullGcTargetEpoch_ = 0;
        uint64_t sharedFullGcTargetEpoch_ = 0;
        std::list<Chunk *>::iterator partialIterator_ {};
        std::array<char, SLOTS_PER_CHUNK> slotTags_ {};
    };

    struct HugeSummaryStatsDfx {
        size_t totalChunks {0};
        size_t activeChunks {0};
        size_t emptyChunks {0};
        size_t releasedEmptyChunks {0};
        size_t usedSlots {0};
        size_t activeSlotCapacity {0};
    };
    static bool HasActiveSlotWithTag(const Chunk &chunk, HugePageTag tag);
    static size_t CountActiveSlotsWithTag(const Chunk &chunk, HugePageTag tag);
#if ENABLE_HUGEPAGE_DFX_STATS
    static bool HasPromotedSlot(const Chunk &chunk);
#endif
    Mutex lock_;

    std::unordered_map<uintptr_t, Chunk> chunkMap_;
    // Lock-free empty fast path for regular Region allocation and notifications.
    // Every update is performed while lock_ protects chunkMap_.
    std::atomic<size_t> chunkCount_ {0};
    std::list<Chunk *> emptyChunks_;
    std::array<std::list<Chunk *>, PARTIAL_BUCKET_COUNT> partialChunks_;
    std::atomic<bool> isInBackground_ {false};
    std::atomic<uint64_t> backgroundStartTimeUs_ {0};
    std::atomic<bool> hasAdvisedAllColdInBackground_ {false};
    Mutex backgroundAdviceLock_;
    uint64_t localFullGcTargetEpoch_ {1};
    uint64_t sharedFullGcTargetEpoch_ {1};
    size_t slotReleaseBatchDepth_ {0};
    // Idempotence guard for the one-shot child-process probe; it does not
    // authorize allocation, which is controlled by the process-global result.
    std::atomic<bool> postForkConfigured_ {false};
#if USE_COMPRESSED_HUGE_PAGE_POOL
    CompressedHugePagePool *compressedBackingPool_ {nullptr};
#endif

    bool CanRefillHugeChunk(size_t quotaLimit, size_t freeSlotRefillLimit, bool &quotaExceeded,
                            HugePageTag tag = HugePageTag::INVALID,
                            HugeSlotAllocPolicy policy = HugeSlotAllocPolicy::DEFAULT);
    bool TryReserveQuotaForSlot(HugePageTag hugePageTag, HugeSlotAllocPolicy policy, bool &quotaExceeded);
    MemMap AllocateHugeChunk(size_t alignment);
    MemMap GetHugeSlot(HugePageTag tag, const std::string &spaceName, bool fromNewChunk = false,
                       size_t *memMapSizeDelta = nullptr,
                       HugeSlotAllocPolicy policy = HugeSlotAllocPolicy::DEFAULT);
    MemMap OccupyFreeSlotLocked(Chunk *targetChunk, HugePageTag hugePageTag, size_t *memMapSizeDelta);
    bool InsertHugeChunk(MemMap memMap);
    bool RemoveHugeChunk(MemMap memMap);
    void ReleaseHugeChunkMemory(MemMap memMap);
    Chunk *TakeEmptyChunkLocked();
    bool FreeHugeSlotLocked(void *mem, Chunk *targetChunk, uintptr_t baseAddr, bool &isChunkEmpty);
    void UpdateChunkAfterSlotFreeLocked(Chunk *targetChunk);
    void QuarantineChunkForReleaseLocked(Chunk *targetChunk);
    bool RestoreQuarantinedChunksLocked();
    void ResetEmptyChunkLocked(Chunk *targetChunk);
#if ENABLE_HUGEPAGE_DFX_STATS
    void UpdateSlotTagAfterMoveLocked(Chunk *targetChunk, size_t index, HugePageTag tag);
#endif
    bool ReleaseSingleEmptyChunkLocked(uintptr_t addrValue, size_t &releasedMemMapSize);
    size_t SoftReleaseEmptyChunks(size_t *memMapSizeDelta = nullptr);
    void MadviseActiveResidentChunks(size_t maxChunks, int advice, const char *actionName,
                                     char ownerTag = '\0');
    std::vector<uintptr_t> CollectMadviseTargets(size_t maxChunks, int advice, char ownerTag);
    bool ShouldMadviseChunkLocked(const Chunk &chunk, int advice, char ownerTag) const;
    void UpdateMadviseState(const std::vector<uintptr_t> &advisedTargets, int advice, char ownerTag);
    void NotifyBackgroundColdTimeout(uint64_t backgroundStartTimeUs);
    void PostBackgroundColdTask(uint64_t backgroundStartTimeUs);
    void TryMadviseColdChunksInBackground(uint64_t backgroundStartTimeUs);
    Chunk *FindReusableChunkLocked(HugePageTag tag, HugeSlotAllocPolicy policy = HugeSlotAllocPolicy::DEFAULT) const;
    Chunk *FindReusableChunkInBucketsLocked(
        HugePageTag tag, HugeSlotAllocPolicy policy,
        const std::function<bool(const Chunk &, HugeChunkDomain targetDomain)> &acceptDomain) const;
    bool IsCurrentGcTargetChunkLocked(const Chunk &chunk, HugePageTag tag, HugeSlotAllocPolicy policy) const;
    bool ShouldMarkGcTarget(HugePageTag tag, HugeSlotAllocPolicy policy) const;
    void MarkGcTargetChunkLocked(Chunk *chunk, HugePageTag tag, HugeSlotAllocPolicy policy);
    void InsertPartialChunkLocked(Chunk *chunk);
    void RemovePartialChunkLocked(Chunk *chunk);
    bool IsFreeSlotRefillLimitReachedLocked(
        size_t freeSlotRefillLimit, HugePageTag tag = HugePageTag::INVALID,
        HugeSlotAllocPolicy policy = HugeSlotAllocPolicy::DEFAULT) const;
    HugeSummaryStatsDfx CollectSummaryDfxLocked() const;
    void DumpSummaryDfxLocked(const HugeSummaryStatsDfx &stats) const;

    friend class panda::test::HugeMemMapPoolTest;
    friend class panda::test::HeapTest;
    friend class panda::test::MemMapAllocatorTest;

#if ENABLE_HUGEPAGE_DFX_STATS
    struct HugeSpaceStatsDfx {
        size_t activeChunkCount {0};
        size_t activeSlots {0};
        size_t totalSlots {0};
        size_t fragmentedChunks {0};
    };

    HugeSpaceStatsDfx CollectStatsDfxLocked(char tag) const;
    void DumpSlotAllocDfxLocked(const Chunk &chunk, const std::string &spaceName, const char *source,
                                uint8_t activeBefore, uint8_t usageMaskBefore, char ownerBefore,
                                bool promotedBefore, bool releasedBefore, bool coldBefore,
                                size_t freeIndex, uintptr_t slotAddr) const;
    void CollectActiveChunksDfxLocked(std::array<size_t, SLOTS_PER_CHUNK + 1> &histogram,
                                      std::vector<const Chunk *> &activeSortedChunks) const;
    void DumpHistogramDfx(const std::array<size_t, SLOTS_PER_CHUNK + 1> &histogram) const;
    void DumpSpaceStatsDfxLocked() const;
    void DumpActiveChunksDfx(std::vector<const Chunk *> &activeSortedChunks) const;
    std::string BuildChunkMapDfx(const Chunk &chunk) const;
#endif
};

#endif // ENABLE_HUGE_PAGE_MEM
}  // namespace panda::ecmascript
#endif // ECMASCRIPT_MEM_HUGE_MEM_MAP_POOL_H
