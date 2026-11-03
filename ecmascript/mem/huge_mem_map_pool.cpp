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

#include "ecmascript/mem/huge_mem_map_pool.h"

#if defined(OHOS_UNIT_TEST)
#include "ecmascript/tests/huge_page_test_helper.h"
#endif

#if ENABLE_HUGE_PAGE_MEM
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <sstream>
#include <sys/mman.h>
#include <unistd.h>
#include <vector>

#include "common_components/taskpool/taskpool.h"
#include "ecmascript/log_wrapper.h"

#include "ecmascript/mem/region.h"

namespace panda::ecmascript {

namespace {
constexpr int VIP_FLAG = 0x100000;
constexpr const char *VIPZONE_SIZE_PATH = "/sys/kernel/mm/transparent_hugepage/vipzone_size";
constexpr uint64_t BACKGROUND_COLD_FALLBACK_DELAY_MS = 1000; // before app background frozen
constexpr size_t MAX_FREE_SLOT_PERCENT_FOR_REFILL = 20;
constexpr size_t PERCENT_FACTOR = 100;
Mutex g_hugePageLimitInitLock;
std::atomic<bool> g_hugePageLimitInitialized {false};
std::atomic<bool> g_canTryHugePage {false};
std::atomic<bool> g_isEnableHugePage {false};
std::atomic<size_t> g_systemHugePageLimit {0};
std::atomic<size_t> g_freeSlotRefillLimit {0};
std::atomic<size_t> g_allocatedHugePages {0};

constexpr char ToTagChar(HugePageTag tag)
{
    return static_cast<char>(tag);
}

HugePageTag ToHugePageTag(char tag)
{
    switch (tag) {
        case ToTagChar(HugePageTag::SEMI):
            return HugePageTag::SEMI;
        case ToTagChar(HugePageTag::OLD):
            return HugePageTag::OLD;
        case ToTagChar(HugePageTag::SHARED_OLD):
            return HugePageTag::SHARED_OLD;
        default:
            return HugePageTag::INVALID;
    }
}

bool IsLocalHugePageTag(HugePageTag tag)
{
    return tag == HugePageTag::SEMI || tag == HugePageTag::OLD;
}

HugeChunkDomain ToChunkDomain(HugePageTag tag)
{
    if (IsLocalHugePageTag(tag)) {
        return HugeChunkDomain::LOCAL;
    }
    if (tag == HugePageTag::SHARED_OLD) {
        return HugeChunkDomain::SHARED_OLD;
    }
    return HugeChunkDomain::EMPTY;
}

#if ENABLE_HUGEPAGE_DFX_STATS
char ToDomainChar(HugeChunkDomain domain)
{
    switch (domain) {
        case HugeChunkDomain::EMPTY:
            return '_';
        case HugeChunkDomain::LOCAL:
            return 'L';
        case HugeChunkDomain::SHARED_OLD:
            return ToTagChar(HugePageTag::SHARED_OLD);
        case HugeChunkDomain::MIXED:
            return 'M';
        default:
            return '?';
    }
}
#endif

bool CanReuseChunkDomain(HugeChunkDomain ownerDomain, HugePageTag tag, HugeSlotAllocPolicy policy)
{
    if (ownerDomain == HugeChunkDomain::EMPTY) {
        return true;
    }
    if (ownerDomain == HugeChunkDomain::MIXED) {
        return true;
    }
    if (tag == HugePageTag::SHARED_OLD &&
        ownerDomain == HugeChunkDomain::LOCAL &&
        policy == HugeSlotAllocPolicy::FULL_GC_TARGET) {
        return true;
    }
    return ownerDomain == ToChunkDomain(tag);
}

uint8_t CountActiveSlots(uint8_t usageMask)
{
    return static_cast<uint8_t>(__builtin_popcount(static_cast<unsigned int>(usageMask)));
}

bool IsSlotActive(uint8_t usageMask, size_t index)
{
    return (usageMask & (1U << index)) != 0;
}

bool DoesSlotTagMatch(char slotTag, HugePageTag tag)
{
    if (tag == HugePageTag::OLD) {
        return slotTag == ToTagChar(HugePageTag::OLD) || slotTag == ToTagChar(HugePageTag::PROMOTED);
    }
    return slotTag == ToTagChar(tag);
}

#if ENABLE_HUGEPAGE_DFX_STATS
const char *GetPartialSlotSource(HugeSlotAllocPolicy policy)
{
    switch (policy) {
        case HugeSlotAllocPolicy::FULL_GC_TARGET:
            return "fullGcTarget";
        case HugeSlotAllocPolicy::DEFAULT:
        default:
            return "partial";
    }
}

const char *GetEmptySlotSource(bool fromNewChunk, HugeSlotAllocPolicy policy)
{
    if (fromNewChunk) {
        return "newChunk";
    }
    switch (policy) {
        case HugeSlotAllocPolicy::FULL_GC_TARGET:
            return "skipFullGcSourcePartial";
        case HugeSlotAllocPolicy::DEFAULT:
        default:
            return "empty";
    }
}
#endif

size_t CalculateFreeSlotRefillLimit(size_t quotaLimit)
{
    return quotaLimit * HugeMemMapPool::SLOTS_PER_CHUNK * MAX_FREE_SLOT_PERCENT_FOR_REFILL / PERCENT_FACTOR;
}

#if ENABLE_HUGEPAGE_DFX_STATS
bool DoesDfxTagMatch(char slotTag, char targetTag)
{
    return slotTag == targetTag ||
        (targetTag == ToTagChar(HugePageTag::OLD) && slotTag == ToTagChar(HugePageTag::PROMOTED));
}
#endif

size_t ReadSystemHugePageCount()
{
    std::ifstream file(VIPZONE_SIZE_PATH);
    if (!file.is_open()) {
        LOG_GC(WARN) << "Huge page disabled, failed to open kernel quota source";
        return 0;
    }

    size_t limitBytes = 0;
    file >> limitBytes;
    if (file.fail()) {
        LOG_GC(WARN) << "Huge page disabled, failed to read kernel quota";
        return 0;
    }
    size_t limit = limitBytes / HugeMemMapPool::HUGE_REGION_SIZE;
    if (limit == 0 && limitBytes != 0) {
        LOG_GC(WARN) << "Huge page disabled, kernel quota is smaller than one huge page, bytes:" << limitBytes;
    }
    return limit;
}

void InitHugePageLimit(const char *reason)
{
    size_t limit = ReadSystemHugePageCount();
    size_t freeSlotRefillLimit = CalculateFreeSlotRefillLimit(limit);
    g_systemHugePageLimit.store(limit, std::memory_order_release);
    g_freeSlotRefillLimit.store(freeSlotRefillLimit, std::memory_order_release);
    g_canTryHugePage.store(limit > 0, std::memory_order_release);
    g_isEnableHugePage.store(false, std::memory_order_release);
    g_hugePageLimitInitialized.store(true, std::memory_order_release);
    LOG_GC(INFO) << "Huge page limit state, pid:" << getpid()
                 << ", reason:" << reason
                 << ", canTry:" << (limit > 0)
                 << ", enabled:false"
                 << ", systemLimit:" << limit
                 << ", freeSlotRefillLimit:" << freeSlotRefillLimit
                 << ", source:kernel quota";
}

void EnsureHugePageLimitInitialized(const char *reason = "lazy")
{
    if (g_hugePageLimitInitialized.load(std::memory_order_acquire)) {
        return;
    }
    LockHolder lock(g_hugePageLimitInitLock);
    if (g_hugePageLimitInitialized.load(std::memory_order_acquire)) {
        return;
    }
    InitHugePageLimit(reason);
}

bool IsHugePageEnabled()
{
    return g_isEnableHugePage.load(std::memory_order_acquire);
}

bool CanTryHugePage()
{
    EnsureHugePageLimitInitialized();
    return g_canTryHugePage.load(std::memory_order_acquire);
}

void MarkHugePageEnabledForCurrentProcess(const char *reason)
{
    if (g_isEnableHugePage.load(std::memory_order_acquire)) {
        return;
    }
    g_canTryHugePage.store(true, std::memory_order_release);
    bool expected = false;
    if (g_isEnableHugePage.compare_exchange_strong(expected, true,
        std::memory_order_acq_rel, std::memory_order_acquire)) {
        LOG_GC(INFO) << "Huge page enabled, reason:" << reason;
    }
}

void DisableHugePageForCurrentProcess(const char *reason, int err)
{
    g_canTryHugePage.store(false, std::memory_order_release);
    bool wasEnabled = g_isEnableHugePage.exchange(false, std::memory_order_acq_rel);
    if (wasEnabled || err != 0) {
        LOG_GC(WARN) << "Huge page disabled, reason:" << reason << ", errno:" << err;
    }
}

bool TryMadviseTarget(uintptr_t addrValue, int advice, const char *actionName)
{
    void *addr = reinterpret_cast<void *>(addrValue);
    if (madvise(addr, HugeMemMapPool::HUGE_REGION_SIZE, advice) == 0) {
        return true;
    }

    int err = errno;
    LOG_GC(WARN) << actionName << " madvise failed, addr:" << addr
                 << " errno:" << err;
    if (advice == VIP_FLAG) {
        DisableHugePageForCurrentProcess("VIP_FLAG restore failed", err);
    }
    return false;
}

std::vector<uintptr_t> AdviseTargets(const std::vector<uintptr_t> &targets, int advice, const char *actionName)
{
    std::vector<uintptr_t> advisedTargets;
    advisedTargets.reserve(targets.size());
    for (uintptr_t addrValue : targets) {
        if (TryMadviseTarget(addrValue, advice, actionName)) {
            advisedTargets.emplace_back(addrValue);
        } else if (advice == VIP_FLAG) {
            break;
        }
    }
    return advisedTargets;
}

size_t GetMaxProcessHugePages()
{
    EnsureHugePageLimitInitialized();
    return g_systemHugePageLimit.load(std::memory_order_acquire);
}

size_t GetFreeSlotRefillLimit()
{
    EnsureHugePageLimitInitialized();
    return g_freeSlotRefillLimit.load(std::memory_order_acquire);
}

size_t GetAllocatedHugePageCount()
{
    return g_allocatedHugePages.load(std::memory_order_relaxed);
}

bool TryReserveHugePageQuota(size_t quotaLimit)
{
    if (quotaLimit == 0) {
        return false;
    }
    size_t expected = g_allocatedHugePages.load(std::memory_order_relaxed);
    while (expected < quotaLimit) {
        if (g_allocatedHugePages.compare_exchange_weak(expected, expected + 1,
            std::memory_order_acq_rel, std::memory_order_relaxed)) {
            return true;
        }
    }
    return false;
}

void ReleaseReservedHugePageQuota(size_t count = 1)
{
    if (count == 0) {
        return;
    }
    size_t current = g_allocatedHugePages.load(std::memory_order_relaxed);
    while (current > 0) {
        size_t next = current > count ? current - count : 0;
        if (g_allocatedHugePages.compare_exchange_weak(current, next,
            std::memory_order_acq_rel, std::memory_order_relaxed)) {
            return;
        }
    }
}

uint64_t GetMonotonicTimeUs()
{
    auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(now).count());
}

void ResetHugePageProcessStateAfterForkInternal()
{
    EnsureHugePageLimitInitialized("post fork fallback");
    size_t limit = g_systemHugePageLimit.load(std::memory_order_acquire);
    size_t freeSlotRefillLimit = g_freeSlotRefillLimit.load(std::memory_order_acquire);
    bool canTry = g_canTryHugePage.load(std::memory_order_acquire);
    g_allocatedHugePages.store(0, std::memory_order_release);
    g_isEnableHugePage.store(false, std::memory_order_release);
    LOG_GC(INFO) << "Huge page process state reset after fork, pid:" << getpid()
                 << ", canTry:" << canTry
                 << ", enabled:false"
                 << ", systemLimit:" << limit
                 << ", freeSlotRefillLimit:" << freeSlotRefillLimit;
}
}  // namespace

#if USE_COMPRESSED_HUGE_PAGE_POOL
void CompressedHugePagePool::Initialize(MemMap reservedMem)
{
    ASSERT(reservedMem.GetMem() != nullptr);
    ASSERT(IsAligned(reinterpret_cast<uintptr_t>(reservedMem.GetMem()), HugeMemMapPool::HUGE_REGION_SIZE));
    ASSERT(IsAligned(reservedMem.GetSize(), HugeMemMapPool::HUGE_REGION_SIZE));

    LockHolder lock(lock_);
    ASSERT(reservedMem_.GetMem() == nullptr);
    reservedMem_ = reservedMem;
    checkedOutChunkCount_ = 0;
    freeChunks_.clear();
    size_t chunkCount = reservedMem.GetSize() / HugeMemMapPool::HUGE_REGION_SIZE;
    freeChunks_.reserve(chunkCount);
    uintptr_t base = reinterpret_cast<uintptr_t>(reservedMem.GetMem());
    for (size_t index = chunkCount; index > 0; --index) {
        uintptr_t chunkAddr = base + (index - 1) * HugeMemMapPool::HUGE_REGION_SIZE;
        freeChunks_.emplace_back(reinterpret_cast<void *>(chunkAddr), HugeMemMapPool::HUGE_REGION_SIZE);
    }
}

void CompressedHugePagePool::Finalize()
{
    MemMap reserved;
    {
        LockHolder lock(lock_);
        reserved = reservedMem_;
        reservedMem_ = MemMap();
        freeChunks_.clear();
        checkedOutChunkCount_ = 0;
    }
    if (reserved.GetMem() != nullptr) {
        PageUnmap(reserved);
    }
}

MemMap CompressedHugePagePool::AcquireChunk()
{
    LockHolder lock(lock_);
    if (freeChunks_.empty()) {
        return MemMap();
    }
    MemMap chunk = freeChunks_.back();
    freeChunks_.pop_back();
    checkedOutChunkCount_++;
    return chunk;
}

void CompressedHugePagePool::ReturnChunk(MemMap chunk)
{
    if (chunk.GetMem() == nullptr) {
        return;
    }
    LockHolder lock(lock_);
    uintptr_t base = reinterpret_cast<uintptr_t>(reservedMem_.GetMem());
    uintptr_t addr = reinterpret_cast<uintptr_t>(chunk.GetMem());
    bool invalidChunk = reservedMem_.GetMem() == nullptr ||
        chunk.GetSize() != HugeMemMapPool::HUGE_REGION_SIZE ||
        chunk.GetSize() > reservedMem_.GetSize() || addr < base ||
        (addr - base) % HugeMemMapPool::HUGE_REGION_SIZE != 0 ||
        addr - base > reservedMem_.GetSize() - chunk.GetSize();
    bool alreadyFree = std::any_of(freeChunks_.begin(), freeChunks_.end(), [addr](const MemMap &freeChunk) {
        return reinterpret_cast<uintptr_t>(freeChunk.GetMem()) == addr;
    });
    if (invalidChunk || alreadyFree || checkedOutChunkCount_ == 0) {
        LOG_GC(FATAL) << "Invalid compressed huge chunk return, addr:" << chunk.GetMem()
                      << ", size:" << chunk.GetSize();
        return;
    }
    freeChunks_.emplace_back(chunk);
    checkedOutChunkCount_--;
}

MemMap CompressedHugePagePool::ReleaseReservedMemory()
{
    LockHolder lock(lock_);
    if (checkedOutChunkCount_ != 0) {
        return MemMap();
    }
    MemMap reserved = reservedMem_;
    reservedMem_ = MemMap();
    freeChunks_.clear();
    return reserved;
}

bool CompressedHugePagePool::Contains(const MemMap &chunk) const
{
    LockHolder lock(lock_);
    if (reservedMem_.GetMem() == nullptr || chunk.GetMem() == nullptr) {
        return false;
    }
    uintptr_t base = reinterpret_cast<uintptr_t>(reservedMem_.GetMem());
    uintptr_t addr = reinterpret_cast<uintptr_t>(chunk.GetMem());
    if (chunk.GetSize() > reservedMem_.GetSize() || addr < base) {
        return false;
    }
    return addr - base <= reservedMem_.GetSize() - chunk.GetSize();
}

bool CompressedHugePagePool::HasReservedMemory() const
{
    LockHolder lock(lock_);
    return reservedMem_.GetMem() != nullptr;
}

size_t CompressedHugePagePool::GetChunkCapacity() const
{
    LockHolder lock(lock_);
    return reservedMem_.GetSize() / HugeMemMapPool::HUGE_REGION_SIZE;
}
#endif

#if defined(OHOS_UNIT_TEST)
PUBLIC_API void ConfigureHugePageStateForTest(bool enabled, size_t quotaLimit)
{
    size_t effectiveQuotaLimit = enabled ? quotaLimit : 0;
    g_systemHugePageLimit.store(effectiveQuotaLimit, std::memory_order_release);
    g_freeSlotRefillLimit.store(CalculateFreeSlotRefillLimit(effectiveQuotaLimit), std::memory_order_release);
    g_allocatedHugePages.store(0, std::memory_order_release);
    g_canTryHugePage.store(enabled, std::memory_order_release);
    g_isEnableHugePage.store(enabled, std::memory_order_release);
    g_hugePageLimitInitialized.store(true, std::memory_order_release);
}

PUBLIC_API HugePageProcessStateForTest DisableHugePageAndGetStateForTest(
    bool enabled, bool canTry, int err)
{
    ConfigureHugePageStateForTest(enabled);
    g_canTryHugePage.store(canTry, std::memory_order_release);
    DisableHugePageForCurrentProcess("unit test", err);
    return {
        g_canTryHugePage.load(std::memory_order_acquire),
        g_isEnableHugePage.load(std::memory_order_acquire),
    };
}
#endif

class HugeMemMapPool::BackgroundColdTask : public common::Task {
public:
    BackgroundColdTask(HugeMemMapPool *pool, uint64_t backgroundStartTimeUs)
        : common::Task(common::GLOBAL_TASK_ID), pool_(pool), backgroundStartTimeUs_(backgroundStartTimeUs) {}
    ~BackgroundColdTask() override = default;

    bool Run([[maybe_unused]] uint32_t threadIndex) override
    {
        if (pool_ != nullptr) {
            pool_->NotifyBackgroundColdTimeout(backgroundStartTimeUs_);
        }
        return true;
    }

    BackgroundColdTask(const BackgroundColdTask &) = delete;
    BackgroundColdTask &operator=(const BackgroundColdTask &) = delete;
    BackgroundColdTask(BackgroundColdTask &&) = delete;
    BackgroundColdTask &operator=(BackgroundColdTask &&) = delete;

private:
    HugeMemMapPool *pool_ {nullptr};
    uint64_t backgroundStartTimeUs_ {0};
};

void HugeMemMapPool::PostBackgroundColdTask(uint64_t backgroundStartTimeUs)
{
    if (backgroundStartTimeUs == 0) {
        return;
    }
    common::Taskpool::GetCurrentTaskpool()->PostDelayedTask(
        std::make_unique<BackgroundColdTask>(this, backgroundStartTimeUs), BACKGROUND_COLD_FALLBACK_DELAY_MS);
}

bool HugeMemMapPool::HasActiveSlotWithTag(const Chunk &chunk, HugePageTag tag)
{
    for (size_t i = 0; i < SLOTS_PER_CHUNK; i++) {
        if (IsSlotActive(chunk.usageMask_, i) && DoesSlotTagMatch(chunk.slotTags_[i], tag)) {
            return true;
        }
    }
    return false;
}

size_t HugeMemMapPool::CountActiveSlotsWithTag(const Chunk &chunk, HugePageTag tag)
{
    size_t count = 0;
    for (size_t i = 0; i < SLOTS_PER_CHUNK; i++) {
        if (IsSlotActive(chunk.usageMask_, i) && DoesSlotTagMatch(chunk.slotTags_[i], tag)) {
            count++;
        }
    }
    return count;
}

#if ENABLE_HUGEPAGE_DFX_STATS
bool HugeMemMapPool::HasPromotedSlot(const Chunk &chunk)
{
    for (size_t i = 0; i < SLOTS_PER_CHUNK; i++) {
        if (IsSlotActive(chunk.usageMask_, i) && chunk.slotTags_[i] == ToTagChar(HugePageTag::PROMOTED)) {
            return true;
        }
    }
    return false;
}
#endif

MemMap HugeMemMapPool::AllocateHugeChunk([[maybe_unused]] size_t alignment)
{
#if USE_COMPRESSED_HUGE_PAGE_POOL
    MemMap hugeMem = compressedBackingPool_ == nullptr ? MemMap() : compressedBackingPool_->AcquireChunk();
    hugeMem = PageMap(HUGE_REGION_SIZE, PAGE_PROT_READWRITE, 0, hugeMem.GetMem(), MAP_FIXED);
#else
    size_t finalAlignment = std::max(alignment, HUGE_REGION_SIZE);
    MemMap hugeMem = PageMap(HUGE_REGION_SIZE, PAGE_PROT_READWRITE, finalAlignment);
#endif
    if (hugeMem.GetMem() == nullptr) {
        LOG_GC(WARN) << "Huge chunk mmap failed";
        return MemMap();
    }

    if (madvise(hugeMem.GetMem(), hugeMem.GetSize(), VIP_FLAG) == 0) {
        MarkHugePageEnabledForCurrentProcess("VIP_FLAG allocation success");
        return hugeMem;
    }

    int err = errno;
    LOG_GC(WARN) << "Huge chunk madvise VIP_FLAG failed:" << hugeMem.GetMem()
                 << ", size:" << hugeMem.GetSize()
                 << ", errno:" << err;
    DisableHugePageForCurrentProcess("VIP_FLAG allocation failed", err);
    ReleaseHugeChunkMemory(hugeMem);
    return MemMap();
}

bool HugeMemMapPool::TryReserveQuotaForSlot(HugePageTag hugePageTag, HugeSlotAllocPolicy policy,
                                            bool &quotaExceeded)
{
    size_t quotaLimit = GetMaxProcessHugePages();
    constexpr size_t NO_FREE_SLOT_REFILL_LIMIT = std::numeric_limits<size_t>::max();
    size_t freeSlotRefillLimit = policy == HugeSlotAllocPolicy::FULL_GC_TARGET ?
        NO_FREE_SLOT_REFILL_LIMIT : GetFreeSlotRefillLimit();
    if (!CanRefillHugeChunk(quotaLimit, freeSlotRefillLimit, quotaExceeded, hugePageTag, policy)) {
        return false;
    }
    bool reserved = TryReserveHugePageQuota(quotaLimit);
    quotaExceeded = !reserved;
    return reserved;
}

HugeSlotAllocResult HugeMemMapPool::TryAllocateSlot(HugePageTag hugePageTag, const std::string &spaceName,
                                                    size_t alignment,
                                                    HugeSlotAllocPolicy policy)
{
    HugeSlotAllocResult result;
#if HUGE_MEM_MAP_POOL_STANDALONE_TEST_BUILD
    bool allocationEnabled = true;
#else
    bool allocationEnabled = IsHugePageEnabled();
#endif
    if (hugePageTag == HugePageTag::INVALID || !allocationEnabled || !CanTryHugePage()) {
        return result;
    }

    result.slot = GetHugeSlot(hugePageTag, spaceName, false, &result.memMapSizeDelta, policy);
    if (result.slot.GetMem() != nullptr) {
        return result;
    }

    bool quotaReserved = TryReserveQuotaForSlot(hugePageTag, policy, result.quotaExceeded);
    if (!quotaReserved) {
        return result;
    }

    MemMap hugeMem = AllocateHugeChunk(alignment);
    if (hugeMem.GetMem() == nullptr) {
        ReleaseReservedHugePageQuota();
        return result;
    }

    if (!InsertHugeChunk(hugeMem)) {
        ReleaseReservedHugePageQuota();
        ReleaseHugeChunkMemory(hugeMem);
        return result;
    }
#if ENABLE_HUGEPAGE_DFX_STATS
    DumpUsageDfx("increase chunk");
#endif

    result.memMapSizeDelta = 0;
    result.slot = GetHugeSlot(hugePageTag, spaceName, true, &result.memMapSizeDelta, policy);
    if (result.slot.GetMem() == nullptr) {
        RemoveHugeChunk(hugeMem);
    }
    return result;
}

HugeMemMapPool::~HugeMemMapPool()
{
    Finalize();
}

void HugeMemMapPool::Finalize()
{
    std::vector<uintptr_t> chunks;
    size_t quotaReservedChunks = 0;
    {
        LockHolder lock(lock_);
        chunks.reserve(chunkMap_.size());
        for (const auto &pair : chunkMap_) {
            chunks.emplace_back(pair.first);
            if (pair.second.ownsQuotaReservation_) {
                quotaReservedChunks++;
            }
        }
        chunkMap_.clear();
        chunkCount_.store(0, std::memory_order_release);
        emptyChunks_.clear();
        for (auto &partialList : partialChunks_) {
            partialList.clear();
        }
        backgroundStartTimeUs_.store(0, std::memory_order_release);
        hasAdvisedAllColdInBackground_.store(false, std::memory_order_release);
        localFullGcTargetEpoch_ = 1;
        sharedFullGcTargetEpoch_ = 1;
        postForkConfigured_.store(false, std::memory_order_release);
    }

    for (uintptr_t addr : chunks) {
        MemMap chunk(reinterpret_cast<void *>(addr), HUGE_REGION_SIZE);
#if USE_COMPRESSED_HUGE_PAGE_POOL
        if (compressedBackingPool_ != nullptr && compressedBackingPool_->Contains(chunk)) {
            continue;
        }
#endif
        PageUnmap(chunk);
    }
    ReleaseReservedHugePageQuota(quotaReservedChunks);
}

bool HugeMemMapPool::InsertHugeChunk(MemMap memMap)
{
    if (memMap.GetMem() == nullptr) {
        return false;
    }
    {
        LockHolder lock(lock_);
        auto baseAddr = reinterpret_cast<uintptr_t>(memMap.GetMem());
        auto [it, inserted] = chunkMap_.try_emplace(baseAddr);
        if (!inserted) {
            return false;
        }
        Chunk *chunk = &it->second;
        chunkCount_.fetch_add(1, std::memory_order_release);
        chunk->baseAddr_ = baseAddr;
        chunk->usageMask_ = 0;
        chunk->slotTags_.fill('_');
        chunk->ownerDomain_ = HugeChunkDomain::EMPTY;
        chunk->isPhysicalReleased_ = false;
        chunk->isColdAdvised_ = false;
        chunk->localFullGcTargetEpoch_ = 0;
        chunk->sharedFullGcTargetEpoch_ = 0;
        chunk->ownsQuotaReservation_ = true;
        chunk->isMemMapCounted_ = false;
        emptyChunks_.push_back(chunk);
    }
    PageTag(memMap.GetMem(), memMap.GetSize(), PageTagType::HEAP, "HugePage");
    return true;
}

bool HugeMemMapPool::RemoveHugeChunk(MemMap memMap)
{
    if (memMap.GetMem() == nullptr) {
        return false;
    }
    bool quotaReserved = false;
    {
        LockHolder lock(lock_);
        auto baseAddr = reinterpret_cast<uintptr_t>(memMap.GetMem());
        auto it = chunkMap_.find(baseAddr);
        if (it == chunkMap_.end()) {
            return false;
        }
        Chunk *chunk = &it->second;
        if (chunk->usageMask_ != 0 || chunk->isAdvising_) {
            return false;
        }
        quotaReserved = chunk->ownsQuotaReservation_;
        emptyChunks_.remove(chunk);
        chunkMap_.erase(it);
        chunkCount_.fetch_sub(1, std::memory_order_release);
    }
    ReleaseHugeChunkMemory(memMap);
    if (quotaReserved) {
        ReleaseReservedHugePageQuota();
    }
    return true;
}

void HugeMemMapPool::ReleaseHugeChunkMemory(MemMap memMap)
{
#if USE_COMPRESSED_HUGE_PAGE_POOL
    if (compressedBackingPool_ != nullptr && compressedBackingPool_->Contains(memMap)) {
        if (PageProtect(memMap.GetMem(), memMap.GetSize(), PAGE_PROT_NONE) != 0) {
            LOG_GC(FATAL) << "Huge chunk PageProtect NONE failed:" << memMap.GetMem()
                          << ", size:" << memMap.GetSize();
            return;
        }
        PageRelease(memMap.GetMem(), memMap.GetSize());
        compressedBackingPool_->ReturnChunk(memMap);
        return;
    }
#endif
    PageUnmap(memMap);
}

bool HugeMemMapPool::IsCurrentGcTargetChunkLocked(
    const Chunk &chunk, HugePageTag tag, HugeSlotAllocPolicy policy) const
{
    switch (policy) {
        case HugeSlotAllocPolicy::FULL_GC_TARGET: {
            HugeChunkDomain chunkDomain = chunk.ownerDomain_;
            HugeChunkDomain targetDomain = ToChunkDomain(tag);
            // EMPTY chunk: no slots at all, cannot be a source chunk
            if (chunkDomain == HugeChunkDomain::EMPTY) {
                return true;
            }
            if (chunkDomain != targetDomain) {
                return true;
            }
            bool isLocalTarget = chunk.localFullGcTargetEpoch_ == localFullGcTargetEpoch_;
            bool isSharedTarget = chunk.sharedFullGcTargetEpoch_ == sharedFullGcTargetEpoch_;
            return isLocalTarget || isSharedTarget;
        }
        case HugeSlotAllocPolicy::DEFAULT:
        default:
            return true;
    }
}

bool HugeMemMapPool::ShouldMarkGcTarget(HugePageTag tag, HugeSlotAllocPolicy policy) const
{
    switch (policy) {
        case HugeSlotAllocPolicy::FULL_GC_TARGET:
            // SEMI tag in Full GC is an evacuation "to" space — same LOCAL domain
            // as OLD, must be epoch-marked to avoid landing on source chunks.
            return tag == HugePageTag::OLD || tag == HugePageTag::SEMI ||
                   tag == HugePageTag::SHARED_OLD;
        case HugeSlotAllocPolicy::DEFAULT:
        default:
            return false;
    }
}

void HugeMemMapPool::MarkGcTargetChunkLocked(Chunk *chunk, HugePageTag tag, HugeSlotAllocPolicy policy)
{
    if (chunk == nullptr || !ShouldMarkGcTarget(tag, policy)) {
        return;
    }
    if (tag == HugePageTag::OLD || tag == HugePageTag::SEMI) {
        chunk->localFullGcTargetEpoch_ = localFullGcTargetEpoch_;
        return;
    }
    if (tag == HugePageTag::SHARED_OLD) {
        chunk->sharedFullGcTargetEpoch_ = sharedFullGcTargetEpoch_;
    }
}

HugeMemMapPool::Chunk *HugeMemMapPool::FindReusableChunkInBucketsLocked(
    HugePageTag tag, HugeSlotAllocPolicy policy,
    const std::function<bool(const Chunk &, HugeChunkDomain targetDomain)> &acceptDomain) const
{
    HugeChunkDomain targetDomain = ToChunkDomain(tag);
    for (size_t activeCount = SLOTS_PER_CHUNK - 1; activeCount > 0; --activeCount) {
        const auto &partialList = partialChunks_[activeCount - 1];
        for (Chunk *chunk : partialList) {
            if (chunk->isReuseQuarantined_ || chunk->isAdvising_) {
                continue;
            }
            if (!acceptDomain(*chunk, targetDomain)) {
                continue;
            }
            if (!IsCurrentGcTargetChunkLocked(*chunk, tag, policy)) {
                continue;
            }
            return chunk;
        }
    }
    return nullptr;
}

HugeMemMapPool::Chunk *HugeMemMapPool::FindReusableChunkLocked(HugePageTag tag, HugeSlotAllocPolicy policy) const
{
    // Priority pass for SHARED_OLD + FULL_GC_TARGET: cross-domain non-MIXED (LOCAL) chunks
    if (tag == HugePageTag::SHARED_OLD && policy == HugeSlotAllocPolicy::FULL_GC_TARGET) {
        auto crossDomainNonMixed = [](const Chunk &chunk, HugeChunkDomain targetDomain) -> bool {
            HugeChunkDomain domain = chunk.ownerDomain_;
            return domain != targetDomain && domain != HugeChunkDomain::EMPTY &&
                   domain != HugeChunkDomain::MIXED;
        };
        Chunk *result = FindReusableChunkInBucketsLocked(tag, policy, crossDomainNonMixed);
        if (result != nullptr) {
            return result;
        }
    }

    // First pass: prefer same-domain + EMPTY chunks
    auto sameDomainOrEmpty = [](const Chunk &chunk, HugeChunkDomain targetDomain) -> bool {
        return chunk.ownerDomain_ == HugeChunkDomain::EMPTY || chunk.ownerDomain_ == targetDomain;
    };
    Chunk *result = FindReusableChunkInBucketsLocked(tag, policy, sameDomainOrEmpty);
    if (result != nullptr) {
        return result;
    }

    // Second pass: try MIXED chunks, then cross-domain chunks (if policy allows)
    HugeChunkDomain targetDomain = ToChunkDomain(tag);
    auto mixedOrCrossDomain = [tag, policy, targetDomain](const Chunk &chunk,
                                                          HugeChunkDomain) -> bool {
        if (chunk.ownerDomain_ == targetDomain || chunk.ownerDomain_ == HugeChunkDomain::EMPTY) {
            return false;  // Already tried in first pass
        }
        if (!CanReuseChunkDomain(chunk.ownerDomain_, tag, policy)) {
            return false;
        }
        // Cross-domain non-MIXED already tried in priority pass for SHARED_OLD+FULL_GC_TARGET
        if (tag == HugePageTag::SHARED_OLD && policy == HugeSlotAllocPolicy::FULL_GC_TARGET &&
            chunk.ownerDomain_ != HugeChunkDomain::MIXED) {
            return false;
        }
        return true;
    };
    return FindReusableChunkInBucketsLocked(tag, policy, mixedOrCrossDomain);
}

void HugeMemMapPool::InsertPartialChunkLocked(Chunk *chunk)
{
    if (chunk == nullptr) {
        return;
    }
    if (chunk->isReuseQuarantined_) {
        return;
    }
    uint8_t activeCount = CountActiveSlots(chunk->usageMask_);
    if (activeCount == 0 || activeCount >= SLOTS_PER_CHUNK) {
        return;
    }
    auto &partialList = partialChunks_[activeCount - 1];
    partialList.push_back(chunk);
    chunk->partialIterator_ = std::prev(partialList.end());
}

void HugeMemMapPool::RemovePartialChunkLocked(Chunk *chunk)
{
    if (chunk == nullptr) {
        return;
    }
    if (chunk->isReuseQuarantined_) {
        return;
    }
    uint8_t activeCount = CountActiveSlots(chunk->usageMask_);
    if (activeCount == 0 || activeCount >= SLOTS_PER_CHUNK) {
        return;
    }
    partialChunks_[activeCount - 1].erase(chunk->partialIterator_);
}

bool HugeMemMapPool::IsFreeSlotRefillLimitReachedLocked(
    size_t freeSlotRefillLimit, HugePageTag tag, HugeSlotAllocPolicy policy) const
{
    size_t usedSlots = 0;
    size_t activeSlotCapacity = 0;
    for (const auto &pair : chunkMap_) {
        const Chunk &chunk = pair.second;
        uint8_t activeCount = CountActiveSlots(chunk.usageMask_);
        if (activeCount == 0) {
            continue;
        }
        if (tag != HugePageTag::INVALID &&
            (chunk.isReuseQuarantined_ || chunk.isAdvising_ ||
            !CanReuseChunkDomain(chunk.ownerDomain_, tag, policy) ||
            !IsCurrentGcTargetChunkLocked(chunk, tag, policy))) {
            continue;
        }
        usedSlots += activeCount;
        activeSlotCapacity += SLOTS_PER_CHUNK;
    }
    if (activeSlotCapacity == 0) {
        return false;
    }
    size_t freeSlots = activeSlotCapacity - usedSlots;
    return freeSlots > freeSlotRefillLimit;
}

bool HugeMemMapPool::CanRefillHugeChunk(size_t quotaLimit, size_t freeSlotRefillLimit, bool &quotaExceeded,
                                        HugePageTag tag, HugeSlotAllocPolicy policy)
{
    LockHolder lock(lock_);
    quotaExceeded = quotaLimit == 0 || chunkMap_.size() >= quotaLimit;
    if (quotaExceeded) {
        return false;
    }
    return !IsFreeSlotRefillLimitReachedLocked(freeSlotRefillLimit, tag, policy);
}

MemMap HugeMemMapPool::OccupyFreeSlotLocked(Chunk *targetChunk, HugePageTag hugePageTag, size_t *memMapSizeDelta)
{
    char tag = ToTagChar(hugePageTag);
    uint32_t freeBits = (~targetChunk->usageMask_) & ((1U << SLOTS_PER_CHUNK) - 1);
    if (freeBits == 0) {
        LOG_GC(FATAL) << "Reusable huge chunk has no free slot, base:"
                      << reinterpret_cast<void *>(targetChunk->baseAddr_)
                      << ", activeCount:" << static_cast<int>(CountActiveSlots(targetChunk->usageMask_))
                      << ", usageMask:" << static_cast<int>(targetChunk->usageMask_);
        return MemMap();
    }

    RemovePartialChunkLocked(targetChunk);
    bool shouldCountChunk = !targetChunk->isMemMapCounted_;
    if (targetChunk->ownerDomain_ == HugeChunkDomain::EMPTY) {
        targetChunk->ownerDomain_ = ToChunkDomain(hugePageTag);
    } else if (targetChunk->ownerDomain_ != HugeChunkDomain::MIXED &&
               targetChunk->ownerDomain_ != ToChunkDomain(hugePageTag)) {
        targetChunk->ownerDomain_ = HugeChunkDomain::MIXED;
    }

    targetChunk->isPhysicalReleased_ = false;
    targetChunk->isColdAdvised_ = false;

    int freeIndex = __builtin_ctz(freeBits);
    targetChunk->usageMask_ |= (1U << freeIndex);
    targetChunk->slotTags_[freeIndex] = tag;
    if (shouldCountChunk) {
        targetChunk->isMemMapCounted_ = true;
        if (memMapSizeDelta != nullptr) {
            *memMapSizeDelta = HUGE_REGION_SIZE;
        }
    }
    InsertPartialChunkLocked(targetChunk);

    void *mem = reinterpret_cast<void *>(targetChunk->baseAddr_ + freeIndex * DEFAULT_REGION_SIZE);
    return MemMap(mem, DEFAULT_REGION_SIZE);
}

MemMap HugeMemMapPool::GetHugeSlot(HugePageTag hugePageTag,
                                   [[maybe_unused]] const std::string &spaceName,
                                   [[maybe_unused]] bool fromNewChunk,
                                   size_t *memMapSizeDelta,
                                   [[maybe_unused]] HugeSlotAllocPolicy policy)
{
    if (memMapSizeDelta != nullptr) {
        *memMapSizeDelta = 0;
    }
    LockHolder lock(lock_);
    ASSERT(hugePageTag != HugePageTag::INVALID);

#if ENABLE_HUGEPAGE_DFX_STATS
    const char *slotSource = GetPartialSlotSource(policy);
#endif
    Chunk *targetChunk = FindReusableChunkLocked(hugePageTag, policy);
    if (targetChunk == nullptr) {
#if ENABLE_HUGEPAGE_DFX_STATS
        slotSource = GetEmptySlotSource(fromNewChunk, policy);
#endif
        targetChunk = TakeEmptyChunkLocked();
        if (targetChunk == nullptr) {
            return MemMap();
        }
    }

#if ENABLE_HUGEPAGE_DFX_STATS
    uint8_t activeBefore = CountActiveSlots(targetChunk->usageMask_);
    uint8_t usageMaskBefore = targetChunk->usageMask_;
    char ownerBefore = ToDomainChar(targetChunk->ownerDomain_);
    bool promotedBefore = HasPromotedSlot(*targetChunk);
    bool releasedBefore = targetChunk->isPhysicalReleased_;
    bool coldBefore = targetChunk->isColdAdvised_;
#endif

    MarkGcTargetChunkLocked(targetChunk, hugePageTag, policy);
    MemMap slot = OccupyFreeSlotLocked(targetChunk, hugePageTag, memMapSizeDelta);
#if ENABLE_HUGEPAGE_DFX_STATS
    if (slot.GetMem() != nullptr) {
        size_t freeIndex = (reinterpret_cast<uintptr_t>(slot.GetMem()) - targetChunk->baseAddr_) / DEFAULT_REGION_SIZE;
        uintptr_t slotAddr = reinterpret_cast<uintptr_t>(slot.GetMem());
        DumpSlotAllocDfxLocked(*targetChunk, spaceName, slotSource, activeBefore, usageMaskBefore, ownerBefore,
                               promotedBefore, releasedBefore, coldBefore, freeIndex, slotAddr);
    }
#endif
    return slot;
}

#if ENABLE_HUGEPAGE_DFX_STATS
void HugeMemMapPool::NotifySlotMoved(void *mem, HugePageTag tag)
{
    if (mem == nullptr || tag == HugePageTag::INVALID || !HasChunks()) {
        return;
    }

    LockHolder lock(lock_);
    uintptr_t addr = reinterpret_cast<uintptr_t>(mem);
    uintptr_t baseAddr = addr & ~(HUGE_REGION_SIZE - 1);
    auto it = chunkMap_.find(baseAddr);
    if (it == chunkMap_.end()) {
        return;
    }
    size_t offset = addr - baseAddr;
    size_t index = offset / DEFAULT_REGION_SIZE;
    Chunk *chunk = &it->second;
    if (!IsSlotActive(chunk->usageMask_, index)) {
        return;
    }

    UpdateSlotTagAfterMoveLocked(chunk, index, tag);
}
#endif

void HugeMemMapPool::BeginSlotReleaseBatch()
{
    LockHolder lock(lock_);
    slotReleaseBatchDepth_++;
}

void HugeMemMapPool::EndSlotReleaseBatch()
{
    LockHolder lock(lock_);
    if (slotReleaseBatchDepth_ == 0) {
        return;
    }
    slotReleaseBatchDepth_--;
    if (slotReleaseBatchDepth_ != 0) {
        return;
    }

    // ReleaseSlotIfOwned physically releases an empty Chunk before returning. Only restore
    // allocation visibility here so another Batch cannot start during an unlocked cleanup gap.
    RestoreQuarantinedChunksLocked();
    for (auto &pair : chunkMap_) {
        Chunk &chunk = pair.second;
        if (chunk.isReuseQuarantined_ && chunk.usageMask_ == 0) {
            chunk.isReuseQuarantined_ = false;
        }
    }
}

HugeMemMapPool::Chunk *HugeMemMapPool::TakeEmptyChunkLocked()
{
    auto releasedIt = emptyChunks_.end();
    for (auto it = emptyChunks_.begin(); it != emptyChunks_.end(); ++it) {
        Chunk *chunk = *it;
        if (chunk->isReuseQuarantined_) {
            continue;
        }
        if (chunk->isAdvising_) {
            continue;
        }
        if (chunk->isPhysicalReleased_) {
            if (releasedIt == emptyChunks_.end()) {
                releasedIt = it;
            }
            continue;
        }
        emptyChunks_.erase(it);
        return chunk;
    }
    if (releasedIt == emptyChunks_.end()) {
        return nullptr;
    }
    Chunk *chunk = *releasedIt;
    emptyChunks_.erase(releasedIt);
    return chunk;
}

bool HugeMemMapPool::ReleaseSlotIfOwned(void *mem, size_t size, size_t *memMapSizeDelta)
{
    if (memMapSizeDelta != nullptr) {
        *memMapSizeDelta = 0;
    }
    bool isChunkEmpty = false;
    {
        LockHolder lock(lock_);
        uintptr_t addr = reinterpret_cast<uintptr_t>(mem);
        uintptr_t baseAddr = addr & ~(HUGE_REGION_SIZE - 1);

        auto it = chunkMap_.find(baseAddr);
        if (it == chunkMap_.end()) {
            return false;
        }
        if (size != DEFAULT_REGION_SIZE) {
            LOG_GC(FATAL) << "Invalid huge slot release size, addr:" << mem
                          << ", size:" << size
                          << ", expected:" << DEFAULT_REGION_SIZE;
            return true;
        }
        if (!FreeHugeSlotLocked(mem, &it->second, baseAddr, isChunkEmpty)) {
            LOG_GC(FATAL) << "Invalid huge slot release address, addr:" << mem
                          << ", size:" << size;
            return false;
        }
#if ENABLE_HUGEPAGE_DFX_STATS
        LOG_GC(INFO) << "ReleaseSlot, addr:" << mem << ", chunkusage:" << (int)((it->second).usageMask_);
#endif
    }
    if (isChunkEmpty) {
        SoftReleaseEmptyChunks(memMapSizeDelta);
    }
    return true;
}

bool HugeMemMapPool::FreeHugeSlotLocked(void *mem, Chunk *targetChunk, uintptr_t baseAddr, bool &isChunkEmpty)
{
    ASSERT(targetChunk != nullptr);
    isChunkEmpty = false;
    uintptr_t addr = reinterpret_cast<uintptr_t>(mem);
    size_t offset = addr - baseAddr;
    if (offset >= HUGE_REGION_SIZE || offset % DEFAULT_REGION_SIZE != 0) {
        LOG_GC(ERROR) << "Invalid huge slot address, addr:" << mem
                      << ", base:" << reinterpret_cast<void *>(baseAddr)
                      << ", offset:" << offset;
        return false;
    }
    size_t index = offset / DEFAULT_REGION_SIZE;
    if (index >= SLOTS_PER_CHUNK) {
        LOG_GC(ERROR) << "Invalid huge slot index, addr:" << mem
                      << ", base:" << reinterpret_cast<void *>(baseAddr)
                      << ", index:" << index;
        return false;
    }

    if ((targetChunk->usageMask_ & (1U << index)) == 0) {
        LOG_GC(ERROR) << "Huge slot is already free, addr:" << mem
                      << ", base:" << reinterpret_cast<void *>(baseAddr)
                      << ", index:" << index
                      << ", usageMask:" << static_cast<int>(targetChunk->usageMask_);
        return false;
    }

    if (slotReleaseBatchDepth_ != 0) {
        QuarantineChunkForReleaseLocked(targetChunk);
    }
    RemovePartialChunkLocked(targetChunk);

    targetChunk->usageMask_ &= ~(1U << index);
    targetChunk->slotTags_[index] = '_';
    isChunkEmpty = targetChunk->usageMask_ == 0;
    UpdateChunkAfterSlotFreeLocked(targetChunk);
    return true;
}

void HugeMemMapPool::UpdateChunkAfterSlotFreeLocked(Chunk *targetChunk)
{
    if (targetChunk->usageMask_ == 0) {
        ResetEmptyChunkLocked(targetChunk);
        return;
    }

    InsertPartialChunkLocked(targetChunk);
}

void HugeMemMapPool::QuarantineChunkForReleaseLocked(Chunk *targetChunk)
{
    ASSERT(targetChunk != nullptr);
    if (targetChunk->isReuseQuarantined_) {
        return;
    }
    RemovePartialChunkLocked(targetChunk);
    targetChunk->isReuseQuarantined_ = true;
}

bool HugeMemMapPool::RestoreQuarantinedChunksLocked()
{
    bool hasEmptyQuarantinedChunk = false;
    for (auto &pair : chunkMap_) {
        Chunk &chunk = pair.second;
        if (!chunk.isReuseQuarantined_) {
            continue;
        }
        if (chunk.usageMask_ == 0) {
            hasEmptyQuarantinedChunk = true;
            continue;
        }
        chunk.isReuseQuarantined_ = false;
        InsertPartialChunkLocked(&chunk);
    }
    return hasEmptyQuarantinedChunk;
}

void HugeMemMapPool::ResetEmptyChunkLocked(Chunk *targetChunk)
{
    targetChunk->ownerDomain_ = HugeChunkDomain::EMPTY;
    targetChunk->slotTags_.fill('_');
    targetChunk->localFullGcTargetEpoch_ = 0;
    targetChunk->sharedFullGcTargetEpoch_ = 0;
    targetChunk->isPhysicalReleased_ = false;
    targetChunk->isColdAdvised_ = false;
    emptyChunks_.push_back(targetChunk);
}

#if ENABLE_HUGEPAGE_DFX_STATS
void HugeMemMapPool::UpdateSlotTagAfterMoveLocked(Chunk *targetChunk, size_t index, HugePageTag tag)
{
    if (targetChunk == nullptr || index >= SLOTS_PER_CHUNK) {
        return;
    }
    if (targetChunk->ownerDomain_ != HugeChunkDomain::MIXED &&
        targetChunk->ownerDomain_ != HugeChunkDomain::EMPTY &&
        !CanReuseChunkDomain(targetChunk->ownerDomain_, tag, HugeSlotAllocPolicy::DEFAULT)) {
        return;
    }
    if (tag == HugePageTag::OLD &&
        (targetChunk->slotTags_[index] == ToTagChar(HugePageTag::SEMI) ||
         targetChunk->slotTags_[index] == ToTagChar(HugePageTag::PROMOTED))) {
        targetChunk->slotTags_[index] = ToTagChar(HugePageTag::PROMOTED);
        return;
    }
    targetChunk->slotTags_[index] = ToTagChar(tag);
}
#endif

bool HugeMemMapPool::ReleaseSingleEmptyChunkLocked(uintptr_t addrValue, size_t &releasedMemMapSize)
{
    auto it = chunkMap_.find(addrValue);
    if (it == chunkMap_.end()) {
        return false;
    }
    Chunk *chunk = &it->second;
    chunk->isAdvising_ = false;
    if (chunk->usageMask_ != 0) {
        return false;
    }
    if (chunk->isMemMapCounted_) {
        releasedMemMapSize += HUGE_REGION_SIZE;
        chunk->isMemMapCounted_ = false;
    }
    chunk->isPhysicalReleased_ = true;
    return true;
}

size_t HugeMemMapPool::SoftReleaseEmptyChunks(size_t *memMapSizeDelta)
{
    if (memMapSizeDelta != nullptr) {
        *memMapSizeDelta = 0;
    }
    std::vector<uintptr_t> targets;
    {
        LockHolder lock(lock_);
        targets.reserve(emptyChunks_.size());
        for (Chunk *chunk : emptyChunks_) {
            if (chunk->usageMask_ != 0) {
                LOG_GC(FATAL) << "Non-empty chunk in emptyChunks_, base:"
                              << reinterpret_cast<void *>(chunk->baseAddr_)
                              << " activeCount:" << static_cast<int>(CountActiveSlots(chunk->usageMask_))
                              << " usageMask:" << static_cast<int>(chunk->usageMask_);
                continue;
            }
            if (chunk->isAdvising_ || chunk->isPhysicalReleased_) {
                continue;
            }
            chunk->isAdvising_ = true;
            targets.emplace_back(chunk->baseAddr_);
        }
    }

    size_t releasedCount = 0;
    size_t releasedMemMapSize = 0;
    for (uintptr_t addrValue : targets) {
        PageRelease(reinterpret_cast<void *>(addrValue), HUGE_REGION_SIZE);
        LockHolder lock(lock_);
        if (ReleaseSingleEmptyChunkLocked(addrValue, releasedMemMapSize)) {
            releasedCount++;
        }
    }
    if (memMapSizeDelta != nullptr) {
        *memMapSizeDelta = releasedMemMapSize;
    }
    if (releasedCount != 0) {
#if ENABLE_HUGEPAGE_DFX_STATS
        DumpUsageDfx("release");
#endif
    }

    return releasedCount;
}

void HugeMemMapPool::MadviseActiveResidentChunks(size_t maxChunks, int advice, const char *actionName, char ownerTag)
{
    if (maxChunks == 0) {
        return;
    }

    ECMA_BYTRACE_NAME(HITRACE_LEVEL_COMMERCIAL, HITRACE_TAG_ARK, actionName, "");
    std::vector<uintptr_t> targets = CollectMadviseTargets(maxChunks, advice, ownerTag);
    std::vector<uintptr_t> advisedTargets = AdviseTargets(targets, advice, actionName);
    UpdateMadviseState(advisedTargets, advice, ownerTag);
}

std::vector<uintptr_t> HugeMemMapPool::CollectMadviseTargets(size_t maxChunks, int advice, char ownerTag)
{
    std::vector<uintptr_t> targets;
    LockHolder lock(lock_);
    targets.reserve(std::min(maxChunks, chunkMap_.size()));
    for (const auto &pair : chunkMap_) {
        if (targets.size() >= maxChunks) {
            break;
        }
        const Chunk &chunk = pair.second;
        if (ShouldMadviseChunkLocked(chunk, advice, ownerTag)) {
            targets.emplace_back(chunk.baseAddr_);
        }
    }
    return targets;
}

bool HugeMemMapPool::ShouldMadviseChunkLocked(const Chunk &chunk, int advice, char ownerTag) const
{
    if (chunk.usageMask_ == 0 || chunk.isPhysicalReleased_) {
        return false;
    }
    if (advice == MADV_COLD && chunk.isColdAdvised_) {
        return false;
    }
    if (ownerTag != '\0') {
        HugePageTag tag = ToHugePageTag(ownerTag);
        if (tag == HugePageTag::INVALID || !HasActiveSlotWithTag(chunk, tag)) {
            return false;
        }
    }
    return true;
}

void HugeMemMapPool::UpdateMadviseState(const std::vector<uintptr_t> &advisedTargets, int advice, char ownerTag)
{
    if (advisedTargets.empty()) {
        return;
    }
    LockHolder lock(lock_);
    for (uintptr_t addrValue : advisedTargets) {
        auto it = chunkMap_.find(addrValue);
        if (it == chunkMap_.end()) {
            continue;
        }
        Chunk *chunk = &it->second;
        if (!ShouldMadviseChunkLocked(*chunk, advice, ownerTag)) {
            continue;
        }
        if (advice == MADV_COLD) {
            chunk->isColdAdvised_ = true;
        } else if (advice == VIP_FLAG) {
            chunk->isColdAdvised_ = false;
        }
    }
}

void HugeMemMapPool::NotifyApplicationBackground()
{
    if (!IsHugePageEnabled()) {
        return;
    }
    bool wasBackground = isInBackground_.exchange(true, std::memory_order_acq_rel);
    if (!wasBackground) {
        uint64_t backgroundStartTimeUs = GetMonotonicTimeUs();
        backgroundStartTimeUs_.store(backgroundStartTimeUs, std::memory_order_release);
        hasAdvisedAllColdInBackground_.store(false, std::memory_order_release);
        PostBackgroundColdTask(backgroundStartTimeUs);
    }
}

void HugeMemMapPool::NotifyBackgroundColdTimeout(uint64_t backgroundStartTimeUs)
{
    if (backgroundStartTimeUs == 0 ||
        backgroundStartTimeUs_.load(std::memory_order_acquire) != backgroundStartTimeUs) {
        return;
    }
    TryMadviseColdChunksInBackground(backgroundStartTimeUs);
}

void HugeMemMapPool::TryMadviseColdChunksInBackground(uint64_t backgroundStartTimeUs)
{
    LockHolder transitionLock(backgroundAdviceLock_);
    if (backgroundStartTimeUs == 0 ||
        backgroundStartTimeUs_.load(std::memory_order_acquire) != backgroundStartTimeUs ||
        !isInBackground_.load(std::memory_order_acquire)) {
        return;
    }
    if (!IsHugePageEnabled()) {
        return;
    }
    bool expected = false;
    if (!hasAdvisedAllColdInBackground_.compare_exchange_strong(expected, true,
        std::memory_order_acq_rel, std::memory_order_acquire)) {
        return;
    }
    MadviseActiveResidentChunks(GetMaxProcessHugePages(), MADV_COLD, "HugePage::COLD_ALL_AFTER_BACKGROUND_DELAY");
}

void HugeMemMapPool::NotifyGCReclaimFinished(TriggerGCType gcType)
{
    if (!HasChunks()) {
        return;
    }
    bool isLocalFullGC = gcType == TriggerGCType::FULL_GC;
    bool isSharedFullGC = gcType == TriggerGCType::SHARED_FULL_GC;
    if (!isLocalFullGC && !isSharedFullGC) {
        return;
    }
    ECMA_BYTRACE_NAME(HITRACE_LEVEL_COMMERCIAL, HITRACE_TAG_ARK,
                      "HugePage::NotifyGCReclaimFinished", "");
    {
        LockHolder lock(lock_);
        if (chunkMap_.empty()) {
            return;
        }
        if (isLocalFullGC) {
            localFullGcTargetEpoch_++;
        } else {
            sharedFullGcTargetEpoch_++;
        }
    }
    LockHolder transitionLock(backgroundAdviceLock_);
    if (!isInBackground_.load(std::memory_order_acquire) || !IsHugePageEnabled()) {
        return;
    }
    if (isLocalFullGC) {
        MadviseActiveResidentChunks(GetMaxProcessHugePages(), MADV_COLD, "HugePage::COLD_ALL_AFTER_FULL_GC");
        return;
    }
    MadviseActiveResidentChunks(GetMaxProcessHugePages(), MADV_COLD, "HugePage::COLD_SHARED_OLD_AFTER_SHARED_GC",
        ToTagChar(HugePageTag::SHARED_OLD));
}

void HugeMemMapPool::NotifyApplicationForeground()
{
    bool wasBackground = isInBackground_.exchange(false, std::memory_order_acq_rel);
    backgroundStartTimeUs_.store(0, std::memory_order_release);
    hasAdvisedAllColdInBackground_.store(false, std::memory_order_release);
    LockHolder transitionLock(backgroundAdviceLock_);
    if (!IsHugePageEnabled()) {
        return;
    }
    if (wasBackground) {
        MadviseActiveResidentChunks(GetMaxProcessHugePages(), VIP_FLAG, "HugePage::RestoreVipForActiveChunks");
    }
}

void HugeMemMapPool::PrepareBeforeFork()
{
    EnsureHugePageLimitInitialized("pre fork");
}

bool HugeMemMapPool::ConfigureAfterFork()
{
    bool expected = false;
    if (!postForkConfigured_.compare_exchange_strong(expected, true,
        std::memory_order_acq_rel, std::memory_order_acquire)) {
        return CanTryHugePage();
    }

    {
        LockHolder lock(lock_);
        for (auto &pair : chunkMap_) {
            pair.second.localFullGcTargetEpoch_ = 0;
            pair.second.sharedFullGcTargetEpoch_ = 0;
        }
        localFullGcTargetEpoch_ = 1;
        sharedFullGcTargetEpoch_ = 1;
    }

    ResetHugePageProcessStateAfterForkInternal();
    if (!CanTryHugePage()) {
        return false;
    }

#if USE_COMPRESSED_HUGE_PAGE_POOL
    if (compressedBackingPool_ == nullptr || !compressedBackingPool_->HasReservedMemory()) {
        DisableHugePageForCurrentProcess("compressed huge page pool is unavailable", 0);
        return false;
    }
    size_t quotaLimit = std::min(GetMaxProcessHugePages(), compressedBackingPool_->GetChunkCapacity());
    g_systemHugePageLimit.store(quotaLimit, std::memory_order_release);
    g_freeSlotRefillLimit.store(CalculateFreeSlotRefillLimit(quotaLimit), std::memory_order_release);
#else
    size_t quotaLimit = GetMaxProcessHugePages();
#endif
    if (!TryReserveHugePageQuota(quotaLimit)) {
        DisableHugePageForCurrentProcess("huge page probe quota unavailable", 0);
        return false;
    }
    MemMap probeChunk = AllocateHugeChunk(HUGE_REGION_SIZE);
    if (probeChunk.GetMem() == nullptr) {
        ReleaseReservedHugePageQuota();
        DisableHugePageForCurrentProcess("huge page probe failed", 0);
        return false;
    }
    if (!InsertHugeChunk(probeChunk)) {
        ReleaseHugeChunkMemory(probeChunk);
        ReleaseReservedHugePageQuota();
        DisableHugePageForCurrentProcess("failed to register huge page probe chunk", 0);
        return false;
    }

    return CanTryHugePage();
}

bool HugeMemMapPool::HasChunks() const
{
    return chunkCount_.load(std::memory_order_acquire) != 0;
}

bool HugeMemMapPool::IsEnabled() const
{
    return IsHugePageEnabled();
}

HugeMemMapPool::HugeSummaryStatsDfx HugeMemMapPool::CollectSummaryDfxLocked() const
{
    HugeSummaryStatsDfx stats;
    stats.totalChunks = chunkMap_.size();
    stats.emptyChunks = emptyChunks_.size();
    stats.activeChunks = stats.totalChunks - stats.emptyChunks;
    stats.activeSlotCapacity = stats.activeChunks * SLOTS_PER_CHUNK;

    for (const auto &pair : chunkMap_) {
        const Chunk &chunk = pair.second;
        uint8_t activeCount = CountActiveSlots(chunk.usageMask_);
        if (activeCount > 0) {
            stats.usedSlots += activeCount;
        } else if (chunk.isPhysicalReleased_) {
            stats.releasedEmptyChunks++;
        }
    }
    return stats;
}

void HugeMemMapPool::DumpSummaryDfxLocked(const HugeSummaryStatsDfx &stats) const
{
    double density = stats.activeSlotCapacity == 0 ? 0.0 :
        static_cast<double>(stats.usedSlots) * 100.0 / stats.activeSlotCapacity;
    LOG_GC(INFO) << "HP_DFX_SUMMARY totalChunks:" << stats.totalChunks
                 << " activeChunks:" << stats.activeChunks
                 << " emptyChunks:" << stats.emptyChunks
                 << " releasedEmptyChunks:" << stats.releasedEmptyChunks
                 << " activeSlots:" << stats.usedSlots
                 << " freeSlots:" << (stats.activeSlotCapacity - stats.usedSlots)
                 << " quotaFreeSlotLimit:"
                 << GetFreeSlotRefillLimit()
                 << " density:" << density
                 << "% quotaUsed:" << GetAllocatedHugePageCount();
}

void HugeMemMapPool::DumpUsageSummaryDfx()
{
    LockHolder lock(lock_);
    HugeSummaryStatsDfx summary = CollectSummaryDfxLocked();
    if (summary.totalChunks == 0) {
        return;
    }

    DumpSummaryDfxLocked(summary);
}

#if ENABLE_HUGEPAGE_DFX_STATS
void HugeMemMapPool::DumpSlotAllocDfxLocked(const Chunk &chunk, const std::string &spaceName, const char *source,
                                            uint8_t activeBefore, uint8_t usageMaskBefore, char ownerBefore,
                                            bool promotedBefore, bool releasedBefore, bool coldBefore,
                                            size_t freeIndex, uintptr_t slotAddr) const
{
    LOG_GC(INFO) << "HP_DFX_GET_SLOT addr:0x" << std::hex << slotAddr
                 << " chunk:0x" << chunk.baseAddr_ << std::dec
                 << " name:" << spaceName
                 << " source:" << source
                 << " freeIndex:" << freeIndex
                 << " activeBefore:" << static_cast<int>(activeBefore)
                 << " activeAfter:" << static_cast<int>(CountActiveSlots(chunk.usageMask_))
                 << " usageMaskBefore:" << static_cast<int>(usageMaskBefore)
                 << " usageMaskAfter:" << static_cast<int>(chunk.usageMask_)
                 << " ownerBefore:" << ownerBefore
                 << " ownerAfter:" << ToDomainChar(chunk.ownerDomain_)
                 << " promotedBefore:" << promotedBefore
                 << " promotedAfter:" << HasPromotedSlot(chunk)
                 << " releasedBefore:" << releasedBefore
                 << " releasedAfter:" << chunk.isPhysicalReleased_
                 << " coldBefore:" << coldBefore
                 << " coldAfter:" << chunk.isColdAdvised_
                 << " advising:" << chunk.isAdvising_
                 << " map:" << BuildChunkMapDfx(chunk);
}

HugeMemMapPool::HugeSpaceStatsDfx HugeMemMapPool::CollectStatsDfxLocked(char tag) const
{
    HugeSpaceStatsDfx stats;
    for (const auto &pair : chunkMap_) {
        const Chunk *chunk = &pair.second;
        uint8_t activeCount = CountActiveSlots(chunk->usageMask_);
        if (activeCount == 0) {
            continue;
        }
        size_t matchedSlots = 0;
        for (size_t i = 0; i < SLOTS_PER_CHUNK; i++) {
            if (!IsSlotActive(chunk->usageMask_, i)) {
                continue;
            }
            char slotTag = chunk->slotTags_[i];
            if (!DoesDfxTagMatch(slotTag, tag)) {
                continue;
            }
            matchedSlots++;
        }
        if (matchedSlots == 0) {
            continue;
        }
        stats.activeChunkCount++;
        stats.activeSlots += matchedSlots;
        stats.totalSlots += SLOTS_PER_CHUNK;
        if (activeCount < SLOTS_PER_CHUNK) {
            stats.fragmentedChunks++;
        }
    }
    return stats;
}

void HugeMemMapPool::CollectActiveChunksDfxLocked(
    std::array<size_t, SLOTS_PER_CHUNK + 1> &histogram,
    std::vector<const Chunk *> &activeSortedChunks) const
{
    for (const auto &pair : chunkMap_) {
        const Chunk *chunk = &pair.second;
        uint8_t activeCount = CountActiveSlots(chunk->usageMask_);
        histogram[activeCount]++;
        if (activeCount > 0) {
            activeSortedChunks.push_back(chunk);
        }
    }
}

void HugeMemMapPool::DumpHistogramDfx(const std::array<size_t, SLOTS_PER_CHUNK + 1> &histogram) const
{
    std::ostringstream histogramLog;
    histogramLog << "HP_DFX_HIST_ALL";
    for (size_t activeCount = 0; activeCount < histogram.size(); ++activeCount) {
        histogramLog << " " << activeCount << "/" << SLOTS_PER_CHUNK
                     << ":" << histogram[activeCount];
    }
    LOG_GC(INFO) << histogramLog.str();
}

void HugeMemMapPool::DumpSpaceStatsDfxLocked() const
{
    static constexpr char TAGS[] = {'S', 'O', 'p', 'D'};
    for (char tag : TAGS) {
        HugeSpaceStatsDfx stats = CollectStatsDfxLocked(tag);
        if (stats.activeChunkCount == 0) {
            continue;
        }
        double density = stats.totalSlots == 0 ? 0.0 :
            static_cast<double>(stats.activeSlots) * 100.0 / stats.totalSlots;
        LOG_GC(INFO) << "HP_DFX_SPACE tag:" << tag
                     << " activeChunks:" << stats.activeChunkCount
                     << " activeSlots:" << stats.activeSlots
                     << " density:" << density
                     << "% fragmented:" << stats.fragmentedChunks;
    }
}

std::string HugeMemMapPool::BuildChunkMapDfx(const Chunk &chunk) const
{
    std::string maskStr;
    for (size_t i = 0; i < SLOTS_PER_CHUNK; ++i) {
        maskStr += "[";
        maskStr += IsSlotActive(chunk.usageMask_, i) ? chunk.slotTags_[i] : '_';
        maskStr += "]";
    }
    return maskStr;
}

void HugeMemMapPool::DumpActiveChunksDfx(std::vector<const Chunk *> &activeSortedChunks) const
{
    if (activeSortedChunks.empty()) {
        return;
    }

    LOG_GC(INFO) << "Legend: [S]emi, [O]ld, [p]Promoted, [D]SharedOld, [_]Free";
    std::sort(activeSortedChunks.begin(), activeSortedChunks.end(), [](const Chunk *first, const Chunk *second) {
        return first->baseAddr_ < second->baseAddr_;
    });

    for (size_t i = 0; i < activeSortedChunks.size(); ++i) {
        const Chunk *chunk = activeSortedChunks[i];
        uint8_t activeCount = CountActiveSlots(chunk->usageMask_);
        LOG_GC(INFO) << "Active Chunk " << i
                     << " | Addr: 0x" << std::hex << chunk->baseAddr_ << std::dec
                     << " | Active: " << static_cast<int>(activeCount) << "/" << SLOTS_PER_CHUNK
                     << " | Map: " << BuildChunkMapDfx(*chunk)
                     << " released:" << chunk->isPhysicalReleased_
                     << " advising:" << chunk->isAdvising_
                     << " promotedFromSemi:" << HasPromotedSlot(*chunk)
                     << " owner:" << ToDomainChar(chunk->ownerDomain_);
    }
    LOG_GC(INFO) << "-----------------------------------------------";
}

void HugeMemMapPool::DumpUsageDfx(const std::string &reason)
{
    LockHolder lock(lock_);
    LOG_GC(INFO) << "========== HugeMemMapPool DFX Report ==========" << reason;

    HugeSummaryStatsDfx summary = CollectSummaryDfxLocked();
    if (summary.totalChunks == 0) {
        LOG_GC(INFO) << "No huge chunks allocated yet.";
        LOG_GC(INFO) << "===============================================";
        return;
    }

    std::array<size_t, SLOTS_PER_CHUNK + 1> histogram {};
    std::vector<const Chunk *> activeSortedChunks;
    activeSortedChunks.reserve(summary.activeChunks);
    CollectActiveChunksDfxLocked(histogram, activeSortedChunks);

    DumpSummaryDfxLocked(summary);
    DumpHistogramDfx(histogram);
    DumpSpaceStatsDfxLocked();
    DumpActiveChunksDfx(activeSortedChunks);

    if (summary.emptyChunks > 0) {
        LOG_GC(INFO) << "Empty Chunks (0/8): " << summary.emptyChunks << " pages ready for demand.";
    }
    LOG_GC(INFO) << "Total Virtual Chunks: " << summary.totalChunks << " / " << GetMaxProcessHugePages();
    LOG_GC(INFO) << "===============================================";
}
#endif
}  // namespace panda::ecmascript
#endif // ENABLE_HUGE_PAGE_MEM
