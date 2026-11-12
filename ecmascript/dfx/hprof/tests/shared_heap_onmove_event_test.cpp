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

#include <cstring>
#include <thread>
#include <vector>

#include "ecmascript/dfx/hprof/heap_profiler.h"
#include "ecmascript/dfx/hprof/heap_profiler_interface.h"
#include "ecmascript/dfx/hprof/moved_object_scanner.h"
#include "ecmascript/dfx/hprof/shared_heap_profiler.h"
#include "ecmascript/js_tagged_value.h"
#include "ecmascript/mem/barriers-inl.h"
#include "ecmascript/mem/heap.h"
#include "ecmascript/mem/region.h"
#include "ecmascript/mem/shared_heap/shared_gc.h"
#include "ecmascript/tests/test_helper.h"

using namespace panda::ecmascript;

namespace panda::test {
class SharedHeapOnMoveEventTest : public testing::Test {
public:
    void SetUp() override
    {
        TestHelper::CreateEcmaVMWithScope(ecmaVm_, thread_, scope_);
        ecmaVm_->SetEnableForceGC(false);
        sHeap_ = SharedHeap::GetInstance();
        MovedObjectScanner::ResetCountersForTest();
    }

    void TearDown() override
    {
        TestHelper::DestroyEcmaVMWithScope(ecmaVm_, scope_);
    }

    EcmaVM *ecmaVm_ {nullptr};
    EcmaHandleScope *scope_ {nullptr};
    JSThread *thread_ {nullptr};
    SharedHeap *sHeap_ {nullptr};
};


class CountingTracker : public HeapProfilerInterface {
public:
    size_t GetIdCount() override
    {
        return 0;
    }
    void AllocationEvent(TaggedObject *address, size_t size) override {}
    void MoveEvent(uintptr_t address, TaggedObject *forwardAddress, size_t size) override
    {
        singleMoveEventCount_++;
    }
    void MoveEventLocked(uintptr_t address, TaggedObject *forwardAddress, size_t size) override
    {
        lockedEventCount_++;
        lastFrom_ = address;
        lastTo_ = reinterpret_cast<uintptr_t>(forwardAddress);
    }
    bool DumpHeapSnapshot(Stream *stream, const DumpSnapShotOption &dumpOption, Progress *progress = nullptr,
                          std::function<void(uint8_t)> callback = [](uint8_t) {}) override
    {
        return true;
    }
    void DumpHeapSnapshotForOOM(const DumpSnapShotOption &dumpOption, bool fromSharedGC = false) override {}
    bool StartHeapTracking(double timeInterval, bool isVmMode = true, Stream *stream = nullptr,
                           bool traceAllocation = false, bool newThread = true) override
    {
        return true;
    }
    bool StopHeapTracking(Stream *stream, Progress *progress = nullptr, bool newThread = true) override
    {
        return true;
    }
    bool UpdateHeapTracking(Stream *stream) override
    {
        return true;
    }
    bool StartHeapSampling(uint64_t samplingInterval, int stackDepth = 128) override
    {
        return true;
    }
    void StopHeapSampling() override {}
    const SamplingInfo *GetAllocationProfile() override
    {
        return nullptr;
    }
    std::unordered_map<uintptr_t, uint64_t> GetHandleNodeIdMap() override
    {
        return {};
    }

    size_t lockedEventCount_ {0};
    size_t singleMoveEventCount_ {0};
    uintptr_t lastFrom_ {0};
    uintptr_t lastTo_ {0};
};

class TrackingTestStream : public Stream {
public:
    void EndOfStream() override {}
    int GetSize() override
    {
        return 100_KB;
    }
    bool WriteChunk(char *data, int32_t size) override
    {
        return true;
    }
    bool WriteBinBlock(char *data, int32_t size) override
    {
        return true;
    }
    bool Good() override
    {
        return true;
    }
    void UpdateHeapStats(HeapStat *data, int32_t count) override {}
    void UpdateLastSeenObjectId(int32_t lastSeenObjectId, int64_t timeStampUs) override {}
};


class SharedOnMoveTestSpace {
public:
    static constexpr size_t cap = 1 * 1024 * 1024;
    explicit SharedOnMoveTestSpace(SharedHeap *sHeap, JSThread *thread) : thread_(thread)
    {
        region_ = sHeap->GetHeapRegionAllocator()->AllocateAlignedRegion(sHeap->GetOldSpace(),
            DEFAULT_REGION_SIZE, thread, sHeap);
        sHeap->GetOldSpace()->AddRegion(region_);
        allocator_.Reset(region_->GetBegin(), region_->GetEnd());
    }

    TaggedObject *AllocateTaggedArray(size_t arrayLen)
    {
        constexpr size_t taggedTypeSize = 8;
        size_t size = TaggedArray::ComputeSize(taggedTypeSize, arrayLen);
        TaggedObject *obj = reinterpret_cast<TaggedObject *>(allocator_.Allocate(size));
        if (obj == nullptr) {
            return nullptr;
        }
        JSHClass *arrayClass =
            JSHClass::Cast(thread_->GlobalConstants()->GetTaggedArrayClass().GetTaggedObject());
        obj->SetClassWithoutBarrier(arrayClass);
        TaggedArray::Cast(obj)->InitializeWithSpecialValue(JSTaggedValue::Undefined(), arrayLen);
        return obj;
    }

    void MarkObject(TaggedObject *obj)
    {
        region_->NonAtomicMark(obj);
    }

    TaggedObject *EvacuateObject(TaggedObject *obj)
    {
        size_t size = obj->GetSize();
        uintptr_t toAddress = allocator_.Allocate(size);
        if (toAddress == 0) {
            return nullptr;
        }
        if (memcpy_s(reinterpret_cast<void *>(toAddress), size, reinterpret_cast<void *>(obj), size) != EOK) {
            return nullptr;
        }
        Barriers::SetPrimitive(obj, 0, MarkWord::FromForwardingAddress(toAddress));
        return reinterpret_cast<TaggedObject *>(toAddress);
    }

    Region *GetRegion() const
    {
        return region_;
    }

private:
    JSThread *thread_ {nullptr};
    Region *region_ {nullptr};
    BumpPointerAllocator allocator_;
};

HWTEST_F_L0(SharedHeapOnMoveEventTest, SessionLifecycle)
{
    EXPECT_EQ(SharedHeapProfiler::GetInstance(), nullptr);
    EXPECT_FALSE(SharedHeapProfiler::IsMoveTrackingActive());

    CountingTracker tracker;
    SharedHeapProfiler::RegisterHeapTracker(&tracker);
    EXPECT_NE(SharedHeapProfiler::GetInstance(), nullptr);
    EXPECT_TRUE(SharedHeapProfiler::IsMoveTrackingActive());
    EXPECT_EQ(SharedHeapProfiler::GetActiveTrackerCount(), 1U);

    CountingTracker tracker2;
    SharedHeapProfiler::RegisterHeapTracker(&tracker2);
    EXPECT_EQ(SharedHeapProfiler::GetActiveTrackerCount(), 2U);
    SharedHeapProfiler::RegisterHeapTracker(&tracker);
    EXPECT_EQ(SharedHeapProfiler::GetActiveTrackerCount(), 2U);

    SharedHeapProfiler::UnregisterHeapTracker(&tracker);
    EXPECT_EQ(SharedHeapProfiler::GetActiveTrackerCount(), 1U);
    EXPECT_TRUE(SharedHeapProfiler::IsMoveTrackingActive());

    SharedHeapProfiler::UnregisterHeapTracker(&tracker2);
    EXPECT_EQ(SharedHeapProfiler::GetInstance(), nullptr);
    EXPECT_FALSE(SharedHeapProfiler::IsMoveTrackingActive());
    SharedHeapProfiler::UnregisterHeapTracker(&tracker);
    EXPECT_EQ(SharedHeapProfiler::GetActiveTrackerCount(), 0U);
    EXPECT_EQ(SharedHeapProfiler::GetSessionCreateCount(), SharedHeapProfiler::GetSessionDestroyCount());

    SharedHeapProfiler::RegisterHeapTracker(&tracker);
    EXPECT_TRUE(SharedHeapProfiler::IsMoveTrackingActive());
    SharedHeapProfiler::UnregisterHeapTracker(&tracker);
    EXPECT_EQ(SharedHeapProfiler::GetInstance(), nullptr);
}

HWTEST_F_L0(SharedHeapOnMoveEventTest, EntryIdMapSingleIdSpace)
{
    SharedOnMoveTestSpace space(sHeap_, thread_);
    TaggedObject *sharedObj = space.AllocateTaggedArray(4);
    ASSERT_NE(sharedObj, nullptr);
    TaggedObject *localObj = ecmaVm_->GetFactory()->NewTaggedArray(4).GetTaggedValue().GetTaggedObject();
    ASSERT_NE(localObj, nullptr);

    HeapProfiler *profiler = static_cast<HeapProfiler *>(HeapProfilerInterface::GetInstance(ecmaVm_));
    EntryIdMap *entryIdMap = profiler->GetEntryIdMap();
    uint64_t sharedId = entryIdMap->FindOrInsertNodeId(reinterpret_cast<JSTaggedType>(sharedObj));
    uint64_t localId = entryIdMap->FindOrInsertNodeId(reinterpret_cast<JSTaggedType>(localObj));
    EXPECT_EQ(sharedId % 2, 1U) << "shared object id must stay odd (single id space)";
    EXPECT_EQ(localId % 2, 1U) << "local object id must stay odd";
    EXPECT_NE(sharedId, localId);

    TaggedObject *toObj = space.EvacuateObject(sharedObj);
    ASSERT_NE(toObj, nullptr);
    EXPECT_TRUE(entryIdMap->Move(reinterpret_cast<JSTaggedType>(sharedObj),
                                 reinterpret_cast<JSTaggedType>(toObj)));
    auto [exist, foundId] = entryIdMap->FindId(reinterpret_cast<JSTaggedType>(toObj));
    EXPECT_TRUE(exist);
    EXPECT_EQ(foundId, sharedId);
    EXPECT_TRUE(entryIdMap->EraseId(reinterpret_cast<JSTaggedType>(toObj)));
    EXPECT_FALSE(entryIdMap->EraseId(reinterpret_cast<JSTaggedType>(toObj)));
}

HWTEST_F_L0(SharedHeapOnMoveEventTest, MoveEventLockedAppliesUnderHeldLock)
{
    SharedOnMoveTestSpace space(sHeap_, thread_);
    TaggedObject *fromObj = space.AllocateTaggedArray(4);
    ASSERT_NE(fromObj, nullptr);
    TaggedObject *toObj = space.EvacuateObject(fromObj);
    ASSERT_NE(toObj, nullptr);

    HeapProfiler *profiler = static_cast<HeapProfiler *>(HeapProfilerInterface::GetInstance(ecmaVm_));
    TrackingTestStream stream;
    ASSERT_TRUE(profiler->StartHeapTracking(50, true, &stream, false, false));
    uint64_t id = profiler->GetEntryIdMap()->FindOrInsertNodeId(
        reinterpret_cast<JSTaggedType>(fromObj));

    profiler->MoveEventLocked(reinterpret_cast<uintptr_t>(fromObj), toObj, 32);

    auto [exist, foundId] = profiler->GetEntryIdMap()->FindId(reinterpret_cast<JSTaggedType>(toObj));
    EXPECT_TRUE(exist);
    EXPECT_EQ(foundId, id);
    EXPECT_TRUE(profiler->StopHeapTracking(&stream, nullptr, false));
    EXPECT_EQ(SharedHeapProfiler::GetActiveTrackerCount(), 0U);
}

HWTEST_F_L0(SharedHeapOnMoveEventTest, ScannerCoversAllForwardedObjects)
{
    SharedOnMoveTestSpace space(sHeap_, thread_);
    constexpr size_t objectCount = 16;
    std::vector<TaggedObject *> objects;
    for (size_t i = 0; i < objectCount; i++) {
        TaggedObject *obj = space.AllocateTaggedArray(2);
        ASSERT_NE(obj, nullptr);
        space.MarkObject(obj);
        objects.push_back(obj);
    }
    size_t forwarded = 0;
    for (size_t i = 0; i < objectCount; i++) {
        if (space.EvacuateObject(objects[i]) != nullptr) {
            forwarded++;
        }
    }
    ASSERT_GT(forwarded, 0U);

    CountingTracker tracker;
    SharedHeapProfiler::RegisterHeapTracker(&tracker);
    MovedObjectScanner::ResetCountersForTest();

    MoveScanStats stats = MovedObjectScanner::ScanSharedFullGC(sHeap_);

    EXPECT_EQ(stats.trackers, 1U);
    EXPECT_GE(stats.moved, forwarded);
    EXPECT_EQ(tracker.lockedEventCount_, static_cast<size_t>(stats.moved));
    EXPECT_EQ(tracker.singleMoveEventCount_, 0U);
    EXPECT_EQ(MovedObjectScanner::GetScanInvocationCount(), 1U);

    SharedHeapProfiler::UnregisterHeapTracker(&tracker);
}

HWTEST_F_L0(SharedHeapOnMoveEventTest, ScannerSilentWhenNotRecording)
{
    SharedOnMoveTestSpace space(sHeap_, thread_);
    TaggedObject *obj = space.AllocateTaggedArray(2);
    ASSERT_NE(obj, nullptr);
    space.MarkObject(obj);
    ASSERT_NE(space.EvacuateObject(obj), nullptr);

    uint32_t before = MovedObjectScanner::GetScanInvocationCount();
    MoveScanStats stats = MovedObjectScanner::ScanSharedFullGC(sHeap_);
    EXPECT_EQ(stats.regions, 0U);
    EXPECT_EQ(stats.moved, 0U);
    EXPECT_EQ(stats.trackers, 0U);
    EXPECT_EQ(MovedObjectScanner::GetScanInvocationCount(), before);
    Heap *heap = const_cast<Heap *>(ecmaVm_->GetHeap());
    MoveScanStats localStats = MovedObjectScanner::ScanLocalFullGC(heap);
    EXPECT_EQ(localStats.regions, 0U);
    EXPECT_EQ(MovedObjectScanner::GetScanInvocationCount(), before);
}

HWTEST_F_L0(SharedHeapOnMoveEventTest, AnchorsFireOnRealGCPaths)
{
    CountingTracker tracker;
    SharedHeapProfiler::RegisterHeapTracker(&tracker);
    MovedObjectScanner::ResetCountersForTest();
    Heap *heap = const_cast<Heap *>(ecmaVm_->GetHeap());

    uint32_t before = MovedObjectScanner::GetScanInvocationCount();
    sHeap_->CollectGarbage<TriggerGCType::SHARED_FULL_GC, GCReason::OTHER>(thread_);
    EXPECT_GT(MovedObjectScanner::GetScanInvocationCount(), before);

    before = MovedObjectScanner::GetScanInvocationCount();
    sHeap_->CollectGarbage<TriggerGCType::SHARED_GC, GCReason::OTHER>(thread_);
    EXPECT_GE(MovedObjectScanner::GetScanInvocationCount(), before);

    before = MovedObjectScanner::GetScanInvocationCount();
    heap->CollectGarbage(TriggerGCType::FULL_GC);
    EXPECT_EQ(MovedObjectScanner::GetScanInvocationCount(), before);

    before = MovedObjectScanner::GetScanInvocationCount();
    heap->CollectGarbage(TriggerGCType::YOUNG_GC);
    EXPECT_EQ(MovedObjectScanner::GetScanInvocationCount(), before);

    EXPECT_EQ(tracker.singleMoveEventCount_, 0U);

    SharedHeapProfiler::UnregisterHeapTracker(&tracker);
    EXPECT_EQ(SharedHeapProfiler::GetInstance(), nullptr);
}

HWTEST_F_L0(SharedHeapOnMoveEventTest, LocalAnchorFiresWhenRecording)
{
    TrackingTestStream stream;
    HeapProfiler *profiler = static_cast<HeapProfiler *>(HeapProfilerInterface::GetInstance(ecmaVm_));
    ASSERT_TRUE(profiler->StartHeapTracking(50, true, &stream, false, false));
    MovedObjectScanner::ResetCountersForTest();
    Heap *heap = const_cast<Heap *>(ecmaVm_->GetHeap());

    heap->CollectGarbage(TriggerGCType::FULL_GC);
    uint32_t fullCount = MovedObjectScanner::GetScanInvocationCount();
    EXPECT_GE(fullCount, 1U);
    EXPECT_EQ(MovedObjectScanner::GetLastStats().trackers, 1U);

    heap->CollectGarbage(TriggerGCType::YOUNG_GC);
    EXPECT_GT(MovedObjectScanner::GetScanInvocationCount(), fullCount);

    EXPECT_TRUE(profiler->StopHeapTracking(&stream, nullptr, false));
    uint32_t before = MovedObjectScanner::GetScanInvocationCount();
    heap->CollectGarbage(TriggerGCType::FULL_GC);
    EXPECT_EQ(MovedObjectScanner::GetScanInvocationCount(), before);
}

HWTEST_F_L0(SharedHeapOnMoveEventTest, LocalCCAnchorFiresWhenRecording)
{
    DumpSnapShotOption dumpOption;
    dumpOption.dumpFormat = DumpFormat::JSON;
    Heap *heap = const_cast<Heap *>(ecmaVm_->GetHeap());
    HeapProfiler *profiler = static_cast<HeapProfiler *>(HeapProfilerInterface::GetInstance(ecmaVm_));
    TrackingTestStream stream;
    ASSERT_TRUE(profiler->DumpHeapSnapshot(&stream, dumpOption));
    ASSERT_TRUE(profiler->IsRecording());

    JSHandle<TaggedArray> keep = ecmaVm_->GetFactory()->NewTaggedArray(8);
    TaggedObject *obj = keep.GetTaggedValue().GetTaggedObject();
    uint64_t id = profiler->GetEntryIdMap()->FindOrInsertNodeId(reinterpret_cast<JSTaggedType>(obj));

    ASSERT_TRUE(heap->TryTriggerCCMarking(MarkReason::OTHER));
    heap->CollectGarbageFromCCMark(GCReason::OTHER);
    ASSERT_TRUE(thread_->IsConcurrentCopying());
    uint32_t before = MovedObjectScanner::GetScanInvocationCount();
    heap->WaitAndHandleCCFinished();

    EXPECT_EQ(MovedObjectScanner::GetScanInvocationCount(), before + 1U);
    MoveScanStats stats = MovedObjectScanner::GetLastStats();
    EXPECT_GT(stats.moved, 0U);

    bool idAlive = false;
    JSTaggedType newAddr = 0;
    for (const auto &entry : *profiler->GetEntryIdMap()->GetIdMap()) {
        if (entry.second == id) {
            idAlive = true;
            newAddr = entry.first;
        }
    }
    EXPECT_TRUE(idAlive);
    EXPECT_NE(newAddr, reinterpret_cast<JSTaggedType>(obj));
}

HWTEST_F_L0(SharedHeapOnMoveEventTest, RealProfilerNodeIdStableAcrossGC)
{
    SharedOnMoveTestSpace space(sHeap_, thread_);
    TaggedObject *sharedObj = space.AllocateTaggedArray(4);
    ASSERT_NE(sharedObj, nullptr);
    JSTaggedType addr = reinterpret_cast<JSTaggedType>(sharedObj);

    TrackingTestStream stream;
    HeapProfiler *profiler = static_cast<HeapProfiler *>(HeapProfilerInterface::GetInstance(ecmaVm_));
    ASSERT_TRUE(profiler->StartHeapTracking(50, true, &stream, false, false));
    uint64_t nodeId = profiler->GetEntryIdMap()->FindOrInsertNodeId(addr);
    EXPECT_EQ(nodeId % 2, 1U) << "single odd id space (BR-2)";

    sHeap_->CollectGarbage<TriggerGCType::SHARED_FULL_GC, GCReason::OTHER>(thread_);
    sHeap_->CollectGarbage<TriggerGCType::SHARED_GC, GCReason::OTHER>(thread_);
    sHeap_->CollectGarbage<TriggerGCType::SHARED_FULL_GC, GCReason::OTHER>(thread_);

    EXPECT_TRUE(profiler->StopHeapTracking(&stream, nullptr, false));
}

HWTEST_F_L0(SharedHeapOnMoveEventTest, CrossVmOnlyActiveRecordersReceive)
{
    CountingTracker active;
    CountingTracker stopped;
    SharedHeapProfiler::RegisterHeapTracker(&active);
    SharedHeapProfiler::RegisterHeapTracker(&stopped);
    EXPECT_EQ(SharedHeapProfiler::GetActiveTrackerCount(), 2U);

    MovedObjectScanner::ResetCountersForTest();
    sHeap_->CollectGarbage<TriggerGCType::SHARED_FULL_GC, GCReason::OTHER>(thread_);
    size_t round1 = MovedObjectScanner::GetScanInvocationCount();
    EXPECT_EQ(stopped.lockedEventCount_, active.lockedEventCount_);

    SharedHeapProfiler::UnregisterHeapTracker(&stopped);
    size_t stoppedAt = stopped.lockedEventCount_;
    sHeap_->CollectGarbage<TriggerGCType::SHARED_FULL_GC, GCReason::OTHER>(thread_);
    EXPECT_GT(MovedObjectScanner::GetScanInvocationCount(), round1);
    EXPECT_EQ(stopped.lockedEventCount_, stoppedAt);

    SharedHeapProfiler::UnregisterHeapTracker(&active);
    EXPECT_EQ(SharedHeapProfiler::GetInstance(), nullptr);
}

HWTEST_F_L0(SharedHeapOnMoveEventTest, StartStopLifecycleStress)
{
    std::atomic<bool> stopFlag {false};
    std::atomic<size_t> registerCycles {0};

    std::thread churn([&stopFlag, &registerCycles]() {
        CountingTracker tracker;
        while (!stopFlag.load(std::memory_order_relaxed)) {
            SharedHeapProfiler::RegisterHeapTracker(&tracker);
            SharedHeapProfiler::UnregisterHeapTracker(&tracker);
            registerCycles.fetch_add(1, std::memory_order_relaxed);
        }
    });

    CountingTracker mainTracker;
    for (int i = 0; i < 50; i++) {
        SharedHeapProfiler::RegisterHeapTracker(&mainTracker);
        sHeap_->CollectGarbage<TriggerGCType::SHARED_FULL_GC, GCReason::OTHER>(thread_);
        sHeap_->CollectGarbage<TriggerGCType::SHARED_GC, GCReason::OTHER>(thread_);
        SharedHeapProfiler::UnregisterHeapTracker(&mainTracker);
    }

    stopFlag.store(true, std::memory_order_relaxed);
    churn.join();

    EXPECT_GT(registerCycles.load(), 0U);
    EXPECT_EQ(SharedHeapProfiler::GetInstance(), nullptr);
    EXPECT_EQ(SharedHeapProfiler::GetSessionCreateCount(), SharedHeapProfiler::GetSessionDestroyCount());
    EXPECT_EQ(mainTracker.singleMoveEventCount_, 0U);
    EXPECT_GT(MovedObjectScanner::GetScanInvocationCount(), 0U);
}

HWTEST_F_L0(SharedHeapOnMoveEventTest, BatchPathObservable)
{
    MovedObjectScanner::ResetCountersForTest();
    sHeap_->CollectGarbage<TriggerGCType::SHARED_FULL_GC, GCReason::OTHER>(thread_);
    EXPECT_EQ(MovedObjectScanner::GetScanInvocationCount(), 0U);

    CountingTracker tracker;
    SharedHeapProfiler::RegisterHeapTracker(&tracker);
    uint32_t before = MovedObjectScanner::GetScanInvocationCount();
    sHeap_->CollectGarbage<TriggerGCType::SHARED_FULL_GC, GCReason::OTHER>(thread_);
    EXPECT_EQ(MovedObjectScanner::GetScanInvocationCount(), before + 1);
    MoveScanStats stats = MovedObjectScanner::GetLastStats();
    EXPECT_EQ(stats.trackers, 1U);
    EXPECT_GT(stats.regions, 0U);

    SharedHeapProfiler::UnregisterHeapTracker(&tracker);
}

class LocalOnMoveTestSpace {
public:
    explicit LocalOnMoveTestSpace(Heap *heap, JSThread *thread) : thread_(thread)
    {
        region_ = heap->GetHeapRegionAllocator()->AllocateAlignedRegion(heap->GetOldSpace(),
            DEFAULT_REGION_SIZE, thread, heap);
        heap->GetOldSpace()->AddRegion(region_);
        allocator_.Reset(region_->GetBegin(), region_->GetEnd());
    }

    TaggedObject *AllocateTaggedArray(size_t arrayLen)
    {
        constexpr size_t taggedTypeSize = 8;
        size_t size = TaggedArray::ComputeSize(taggedTypeSize, arrayLen);
        TaggedObject *obj = reinterpret_cast<TaggedObject *>(allocator_.Allocate(size));
        if (obj == nullptr) {
            return nullptr;
        }
        JSHClass *arrayClass =
            JSHClass::Cast(thread_->GlobalConstants()->GetTaggedArrayClass().GetTaggedObject());
        obj->SetClassWithoutBarrier(arrayClass);
        TaggedArray::Cast(obj)->InitializeWithSpecialValue(JSTaggedValue::Undefined(), arrayLen);
        return obj;
    }

    void MarkObject(TaggedObject *obj)
    {
        region_->NonAtomicMark(obj);
    }

    TaggedObject *EvacuateObject(TaggedObject *obj)
    {
        size_t size = obj->GetSize();
        uintptr_t toAddress = allocator_.Allocate(size);
        if (toAddress == 0) {
            return nullptr;
        }
        if (memcpy_s(reinterpret_cast<void *>(toAddress), size, reinterpret_cast<void *>(obj), size) != EOK) {
            return nullptr;
        }
        Barriers::SetPrimitive(obj, 0, MarkWord::FromForwardingAddress(toAddress));
        return reinterpret_cast<TaggedObject *>(toAddress);
    }

    Region *GetRegion() const
    {
        return region_;
    }

private:
    JSThread *thread_ {nullptr};
    Region *region_ {nullptr};
    BumpPointerAllocator allocator_;
};
\
namespace {
// Helpers for ConcurrentLocalAnchorsPerVmIsolated (G.FUD.05: keep the test body under 50 lines).

TaggedObject *PrepareForwardedSpace(LocalOnMoveTestSpace &space, size_t objectCount, TaggedObject *&firstFrom)
{
    std::vector<TaggedObject *> objects;
    for (size_t i = 0; i < objectCount; i++) {
        TaggedObject *obj = space.AllocateTaggedArray(2);
        if (obj == nullptr) {
            return nullptr;
        }
        space.MarkObject(obj);
        objects.push_back(obj);
    }
    firstFrom = objects[0];
    TaggedObject *firstTo = space.EvacuateObject(objects[0]);
    if (firstTo == nullptr) {
        return nullptr;
    }
    for (size_t i = 1; i < objectCount; i++) {
        if (space.EvacuateObject(objects[i]) == nullptr) {
            return nullptr;
        }
    }
    return firstTo;
}

void RunAnchorLoops(Heap *heap, size_t &moved, bool &trackersOk, uint32_t scanLoops)
{
    for (uint32_t i = 0; i < scanLoops; i++) {
        MoveScanStats stats = MovedObjectScanner::ScanLocalFullGC(heap);
        moved += stats.moved;
        trackersOk = trackersOk && stats.trackers == 1U;
        std::this_thread::yield();
    }
}

bool LedgerFollowsMove(HeapProfiler *profiler, TaggedObject *to, uint64_t id)
{
    auto [exist, found] = profiler->GetEntryIdMap()->FindId(reinterpret_cast<JSTaggedType>(to));
    return exist && found == id;
}

bool LedgerNotPolluted(HeapProfiler *profiler, Region *foreignRegion)
{
    for (const auto &entry : *profiler->GetEntryIdMap()->GetIdMap()) {
        if (foreignRegion->InRange(entry.first)) {
            return false;
        }
    }
    return true;
}

struct SecondVm {
    EcmaVM *vm {nullptr};
    JSThread *thread {nullptr};
    Heap *heap {nullptr};
    HeapProfiler *profiler {nullptr};
    Region *region {nullptr};
    TaggedObject *from {nullptr};
    TaggedObject *to {nullptr};
    uint64_t id {0};
};

bool PrepareSecondVm(SecondVm &v, size_t objectCount)
{
    JSRuntimeOptions options;
    options.SetEnableForceGC(true);
    v.vm = JSNApi::CreateEcmaVM(options);
    if (v.vm == nullptr) {
        return false;
    }
    v.thread = v.vm->GetJSThread();
    v.thread->ManagedCodeBegin();
    v.heap = const_cast<Heap *>(v.vm->GetHeap());
    v.profiler = static_cast<HeapProfiler *>(HeapProfilerInterface::GetInstance(v.vm));
    TrackingTestStream stream;
    DumpSnapShotOption dumpOption;
    dumpOption.dumpFormat = DumpFormat::JSON;
    {
        EcmaHandleScope scope(v.thread);
        if (!v.profiler->DumpHeapSnapshot(&stream, dumpOption)) {
            return false;
        }
        LocalOnMoveTestSpace space(v.heap, v.thread);
        v.region = space.GetRegion();
        v.to = PrepareForwardedSpace(space, objectCount, v.from);
        if (v.to == nullptr) {
            return false;
        }
        v.id = v.profiler->GetEntryIdMap()->FindOrInsertNodeId(reinterpret_cast<JSTaggedType>(v.from));
    }
    v.thread->ManagedCodeEnd();
    return true;
}
}  // namespace

HWTEST_F_L0(SharedHeapOnMoveEventTest, ConcurrentLocalAnchorsPerVmIsolated)
{
    constexpr size_t objectCount = 16;
    constexpr uint32_t scanLoops = 2000;

    Heap *heap1 = const_cast<Heap *>(ecmaVm_->GetHeap());
    HeapProfiler *profiler1 = static_cast<HeapProfiler *>(HeapProfilerInterface::GetInstance(ecmaVm_));
    TrackingTestStream stream1;
    DumpSnapShotOption dumpOption;
    dumpOption.dumpFormat = DumpFormat::JSON;
    ASSERT_TRUE(profiler1->DumpHeapSnapshot(&stream1, dumpOption));
    LocalOnMoveTestSpace space1(heap1, thread_);
    TaggedObject *from1 = nullptr;
    TaggedObject *to1 = PrepareForwardedSpace(space1, objectCount, from1);
    ASSERT_NE(to1, nullptr);
    uint64_t id1 = profiler1->GetEntryIdMap()->FindOrInsertNodeId(reinterpret_cast<JSTaggedType>(from1));

    thread_->ManagedCodeEnd();
    SecondVm vm2;
    ASSERT_TRUE(PrepareSecondVm(vm2, objectCount));
    thread_->ManagedCodeBegin();

    uint32_t countBefore = MovedObjectScanner::GetScanInvocationCount();
    size_t moved1 = 0, moved2 = 0;
    bool trackersOk1 = true, trackersOk2 = true;
    std::thread scanner2(RunAnchorLoops, vm2.heap, std::ref(moved2), std::ref(trackersOk2), scanLoops);
    RunAnchorLoops(heap1, moved1, trackersOk1, scanLoops);
    scanner2.join();

    bool ledger1Ok = LedgerFollowsMove(profiler1, to1, id1);
    bool ledger1Clean = LedgerNotPolluted(profiler1, vm2.region);
    thread_->ManagedCodeEnd();
    vm2.thread->ManagedCodeBegin();
    bool ledger2Ok = LedgerFollowsMove(vm2.profiler, vm2.to, vm2.id);
    bool ledger2Clean = LedgerNotPolluted(vm2.profiler, space1.GetRegion());
    vm2.thread->ManagedCodeEnd();
    vm2.vm->SetEnableForceGC(false);
    JSNApi::DestroyJSVM(vm2.vm);
    thread_->ManagedCodeBegin();

    EXPECT_TRUE(trackersOk1);
    EXPECT_TRUE(trackersOk2);
    EXPECT_EQ(moved1, objectCount * scanLoops);
    EXPECT_EQ(moved2, objectCount * scanLoops);
    EXPECT_EQ(MovedObjectScanner::GetScanInvocationCount(), countBefore + 2 * scanLoops);
    EXPECT_TRUE(ledger1Ok);
    EXPECT_TRUE(ledger2Ok);
    EXPECT_TRUE(ledger1Clean);
    EXPECT_TRUE(ledger2Clean);
}
}  // namespace panda::test