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
 * Anchor scan for moved objects: at each GC finish anchor (before FROM is
 * reclaimed), iterate FROM regions, restore (from -> to) pairs from markword
 * forwarding, and apply them to active recorders via lock-free streaming.
 */

#ifndef ECMASCRIPT_DFX_HPROF_MOVED_OBJECT_SCANNER_H
#define ECMASCRIPT_DFX_HPROF_MOVED_OBJECT_SCANNER_H

#include <cstdint>

namespace panda::ecmascript {

class Heap;
class HeapProfiler;
class SharedHeap;
class Region;
class TaggedObject;

struct MoveScanStats {
    uint32_t regions {0};
    uint32_t moved {0};
    uint32_t trackers {0};
    uint64_t costUs {0};
};

class MovedObjectScanner {
public:
    // Shared-path anchors (called from each GC's finish/reclaim point).
#if defined(ECMASCRIPT_SUPPORT_HEAPPROFILER)
    static MoveScanStats ScanSharedFullGC(SharedHeap *heap);
#else
    static MoveScanStats ScanSharedFullGC(SharedHeap *) { return MoveScanStats {}; }
#endif

#if defined(ECMASCRIPT_SUPPORT_HEAPPROFILER)
    static MoveScanStats ScanSharedCC(SharedHeap *heap);
#else
    static MoveScanStats ScanSharedCC(SharedHeap *) { return MoveScanStats {}; }
#endif

#if defined(ECMASCRIPT_SUPPORT_HEAPPROFILER)
    static MoveScanStats ScanSharedPartialGC(SharedHeap *heap);
#else
    static MoveScanStats ScanSharedPartialGC(SharedHeap *) { return MoveScanStats {}; }
#endif

    // Local-path anchors (dispatch to the owning VM's recorder only).
#if defined(ECMASCRIPT_SUPPORT_HEAPPROFILER)
    static MoveScanStats ScanLocalFullGC(Heap *heap);
#else
    static MoveScanStats ScanLocalFullGC(Heap *) { return MoveScanStats {}; }
#endif

#if defined(ECMASCRIPT_SUPPORT_HEAPPROFILER)
    static MoveScanStats ScanLocalPartialGC(Heap *heap);
#else
    static MoveScanStats ScanLocalPartialGC(Heap *) { return MoveScanStats {}; }
#endif

#if defined(ECMASCRIPT_SUPPORT_HEAPPROFILER)
    static MoveScanStats ScanLocalCC(Heap *heap);
#else
    static MoveScanStats ScanLocalCC(Heap *) { return MoveScanStats {}; }
#endif

    // Test/observability hooks.
#if defined(ECMASCRIPT_SUPPORT_HEAPPROFILER)
    static void SetLastStats(const MoveScanStats &stats);
#else
    static void SetLastStats(const MoveScanStats &) {}
#endif

#if defined(ECMASCRIPT_SUPPORT_HEAPPROFILER)
    static uint32_t GetScanInvocationCount();
#else
    static uint32_t GetScanInvocationCount() { return 0; }
#endif

#if defined(ECMASCRIPT_SUPPORT_HEAPPROFILER)
    static MoveScanStats GetLastStats();
#else
    static MoveScanStats GetLastStats() { return MoveScanStats {}; }
#endif

#if defined(ECMASCRIPT_SUPPORT_HEAPPROFILER)
    static void ResetCountersForTest();
#else
    static void ResetCountersForTest() {}
#endif

private:
    static void ScanRegionShared(Region *region, MoveScanStats &stats);
    static void ScanRegionLocal(Region *region, MoveScanStats &stats, HeapProfiler *profiler);
    static uint32_t scanInvocationCount_;
    static MoveScanStats lastStats_;
};
}  // namespace panda::ecmascript

#endif  // ECMASCRIPT_DFX_HPROF_MOVED_OBJECT_SCANNER_H
