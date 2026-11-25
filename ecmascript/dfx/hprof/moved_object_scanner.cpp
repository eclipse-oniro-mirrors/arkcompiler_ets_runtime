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

#include "ecmascript/dfx/hprof/moved_object_scanner.h"

#include <chrono>

#include "ecmascript/base/block_hook_scope.h"
#include "ecmascript/dfx/hprof/heap_profiler.h"
#include "ecmascript/dfx/hprof/heap_profiler_interface.h"
#include "ecmascript/dfx/hprof/shared_heap_profiler.h"
#include "ecmascript/ecma_vm.h"
#include "ecmascript/log_wrapper.h"
#include "ecmascript/mem/heap.h"
#include "ecmascript/mem/heap-inl.h"
#include "ecmascript/mem/mark_word.h"
#include "ecmascript/mem/region.h"
#include "ecmascript/mem/specialized_slot_space-inl.h"
#include "ecmascript/platform/mutex.h"

namespace panda::ecmascript {

uint32_t MovedObjectScanner::scanInvocationCount_ {0};
MoveScanStats MovedObjectScanner::lastStats_ {};

namespace {
Mutex &StatsMutex()
{
    static Mutex instance;
    return instance;
}

uint64_t NowUs()
{
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
}

// Core primitive: bitset-iterate live objects, keep those with forwarding.
template <typename Dispatch>
inline void ScanRegionForForwarded(Region *region, MoveScanStats &stats, Dispatch &&dispatch)
{
    stats.regions++;
    region->IterateAllMarkedBits([&stats, &dispatch](void *mem) {
        TaggedObject *object = static_cast<TaggedObject *>(mem);
        MarkWord markWord(object, RELAXED_LOAD);
        if (markWord.IsForwardingAddress()) {  // TO objects never carry one
            TaggedObject *forward = markWord.ToForwardingAddress();
            stats.moved++;
            dispatch(ToUintPtr(object), forward, forward->GetSize());
        }
    });
}

void FinishAnchor(const char *gcName, MoveScanStats &stats)
{
    stats.costUs = NowUs() - stats.costUs;
    MovedObjectScanner::SetLastStats(stats);
    LOG_ECMA(INFO) << "[MoveEvents] gc=" << gcName << " regions=" << stats.regions
                   << " moved=" << stats.moved << " trackers=" << stats.trackers
                   << " cost=" << stats.costUs << "us";
}

// Shared-anchor skeleton: gate on global tracking, freeze the registry across the enumeration.
template <typename Enumerate>
MoveScanStats RunSharedAnchor(const char *gcName, SharedHeap *heap, Enumerate &&enumerate)
{
    MoveScanStats stats;
    if (!SharedHeapProfiler::IsMoveTrackingActive()) {  // idle: zero cost
        return stats;
    }
    stats.costUs = NowUs();
    ECMA_BYTRACE_NAME(HITRACE_LEVEL_COMMERCIAL, HITRACE_TAG_ARK, "HeapProfiler::ScanAndApplyMoveEvents", "");
    SharedHeapProfiler::LockActiveTrackers();
    stats.trackers = SharedHeapProfiler::GetActiveTrackerCount();
    enumerate(heap, stats);
    SharedHeapProfiler::UnlockActiveTrackers();
    FinishAnchor(gcName, stats);
    return stats;
}

// Local-anchor skeleton: gate on this VM's recorder; the profiler is passed down, never shared.
template <typename Enumerate>
MoveScanStats RunLocalAnchor(const char *gcName, Heap *heap, Enumerate &&enumerate)
{
    MoveScanStats stats;
    HeapProfiler *profiler = static_cast<HeapProfiler *>(heap->GetEcmaVM()->GetHeapProfile());
    if (profiler == nullptr || !profiler->IsRecording()) {
        return stats;
    }
    stats.costUs = NowUs();
    ECMA_BYTRACE_NAME(HITRACE_LEVEL_COMMERCIAL, HITRACE_TAG_ARK, "HeapProfiler::ScanAndApplyMoveEvents", "");
    stats.trackers = 1;
    enumerate(heap, stats, profiler);
    FinishAnchor(gcName, stats);
    return stats;
}
}  // namespace

// ---------------- Shared paths ----------------

void MovedObjectScanner::ScanRegionShared(Region *region, MoveScanStats &stats)
{
    // Suppress malloc hooks across the ledger updates, as the old per-object notify did.
    base::BlockHookScope blockScope;
    ScanRegionForForwarded(region, stats, [](uintptr_t from, TaggedObject *to, size_t size) {
        SharedHeapProfiler::DispatchMoveEvent(from, to, size);
    });
}

MoveScanStats MovedObjectScanner::ScanSharedFullGC(SharedHeap *heap)
{
    return RunSharedAnchor("SharedFullGC", heap, [](SharedHeap *h, MoveScanStats &s) {
        h->EnumerateFullGCFromRegions([&s](Region *region) { ScanRegionShared(region, s); });
    });
}

MoveScanStats MovedObjectScanner::ScanSharedCC(SharedHeap *heap)
{
    return RunSharedAnchor("SharedCC", heap, [](SharedHeap *h, MoveScanStats &s) {
        h->EnumerateCCFromRegions([&s](Region *region) { ScanRegionShared(region, s); });
    });
}

MoveScanStats MovedObjectScanner::ScanSharedPartialGC(SharedHeap *heap)
{
    return RunSharedAnchor("SharedPartialGC", heap, [](SharedHeap *h, MoveScanStats &s) {
        h->EnumeratePartialGCFromRegions([&s](Region *region) { ScanRegionShared(region, s); });
    });
}

// ---------------- Local paths ----------------

void MovedObjectScanner::ScanRegionLocal(Region *region, MoveScanStats &stats, HeapProfiler *profiler)
{
    // Suppress malloc hooks across the ledger updates, as the old per-object notify did.
    base::BlockHookScope blockScope;
    ScanRegionForForwarded(region, stats, [profiler](uintptr_t from, TaggedObject *to, size_t size) {
        profiler->MoveEventLocked(from, to, size);
    });
}

MoveScanStats MovedObjectScanner::ScanLocalFullGC(Heap *heap)
{
    return RunLocalAnchor("LocalFullGC", heap, [](Heap *h, MoveScanStats &s, HeapProfiler *p) {
        h->EnumerateFullGCFromRegions([&s, p](Region *region) { ScanRegionLocal(region, s, p); });
    });
}

MoveScanStats MovedObjectScanner::ScanLocalPartialGC(Heap *heap)
{
    return RunLocalAnchor("LocalPartialGC", heap, [](Heap *h, MoveScanStats &s, HeapProfiler *p) {
        h->EnumeratePartialGCFromRegions([&s, p](Region *region) { ScanRegionLocal(region, s, p); });
    });
}

MoveScanStats MovedObjectScanner::ScanLocalCC(Heap *heap)
{
    return RunLocalAnchor("LocalCC", heap, [](Heap *h, MoveScanStats &s, HeapProfiler *p) {
        h->EnumerateLocalCCFromRegions([&s, p](Region *region) { ScanRegionLocal(region, s, p); });
    });
}

MoveScanStats MovedObjectScanner::ScanLocalGlobalCC(Heap *heap)
{
    return RunLocalAnchor("GlobalCC", heap, [](Heap *h, MoveScanStats &s, HeapProfiler *p) {
        auto scanRegion = [&s, p](Region *region) { ScanRegionLocal(region, s, p); };
        h->EnumerateLocalCCFromRegions(scanRegion);
        // GlobalCC also evacuates the specialized local spaces.
        h->GetModuleSpace()->EnumerateFromRegions(scanRegion);
        if constexpr (!G_USE_CMS_GC) {
            h->GetNativePointerSpace()->EnumerateFromRegions(scanRegion);
        }
    });
}

// ---------------- Test/observability hooks ----------------

void MovedObjectScanner::SetLastStats(const MoveScanStats &stats)
{
    LockHolder lock(StatsMutex());
    lastStats_ = stats;
    scanInvocationCount_++;
}

uint32_t MovedObjectScanner::GetScanInvocationCount()
{
    LockHolder lock(StatsMutex());
    return scanInvocationCount_;
}

MoveScanStats MovedObjectScanner::GetLastStats()
{
    LockHolder lock(StatsMutex());
    return lastStats_;
}

void MovedObjectScanner::ResetCountersForTest()
{
    LockHolder lock(StatsMutex());
    scanInvocationCount_ = 0;
    lastStats_ = MoveScanStats {};
}
}  // namespace panda::ecmascript
