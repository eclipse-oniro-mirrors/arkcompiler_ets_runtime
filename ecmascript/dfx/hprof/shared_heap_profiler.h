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

/*
 * HeapProfiler registry for active recorders: short-circuit flag, tuning-session
 * lifecycle, and shared move-event fan-out. Scans hold registryMutex_ so
 * unregister (VM teardown) is serialized with the anchor scan.
 */

#ifndef ECMASCRIPT_DFX_HPROF_SHARED_HEAP_PROFILER_H
#define ECMASCRIPT_DFX_HPROF_SHARED_HEAP_PROFILER_H

#include <atomic>
#include <vector>

#include "ecmascript/js_tagged_value_internals.h"
#include "ecmascript/platform/mutex.h"

namespace panda::ecmascript {

class HeapProfilerInterface;
class TaggedObject;

class SharedHeapProfiler {
public:
    static SharedHeapProfiler *GetInstance()
    {
        return activeTrackerCount_.load(std::memory_order_relaxed) != 0 ? Instance() : nullptr;
    }

    // Anchor gate: single relaxed read, zero cost when idle.
    static bool IsMoveTrackingActive()
    {
#if defined(ECMASCRIPT_SUPPORT_HEAPPROFILER)
        return activeTrackerCount_.load(std::memory_order_relaxed) != 0;
#else
        return false;
#endif
    }

    static uint32_t GetActiveTrackerCount()
    {
        return static_cast<uint32_t>(activeTrackerCount_.load(std::memory_order_relaxed));
    }

    static void RegisterHeapTracker(HeapProfilerInterface *profiler);
    static void UnregisterHeapTracker(HeapProfilerInterface *profiler);

    // Held across the whole anchor scan: serializes with register/unregister
    // (one acquisition per recording-state GC round, independent of N and K).
    static void LockActiveTrackers();
    static void UnlockActiveTrackers();

    // Fan out one move event to all active recorders (scan must hold the lock).
    static void DispatchMoveEvent(uintptr_t fromAddress, TaggedObject *toAddress, size_t size);

    static uint32_t GetSessionCreateCount();
    static uint32_t GetSessionDestroyCount();

private:
    SharedHeapProfiler() = default;

    // Never deleted: the mutex/vector must outlive any static-destruction-time unregister.
    static SharedHeapProfiler *Instance();

    Mutex mutex_;  // guards trackers_
    std::vector<HeapProfilerInterface *> trackers_;
    uint32_t sessionCreateCount_ {0};  // session beacons, tests assert they pair
    uint32_t sessionDestroyCount_ {0};

    // > 0 means a tuning session is on; the lockless gate for GC paths and GetInstance.
    static std::atomic<size_t> activeTrackerCount_;
};
}  // namespace panda::ecmascript

#endif  // ECMASCRIPT_DFX_HPROF_SHARED_HEAP_PROFILER_H
