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

#ifndef ECMASCRIPT_MEM_GLOBAL_CC_GLOBAL_CC_H
#define ECMASCRIPT_MEM_GLOBAL_CC_GLOBAL_CC_H

#include <atomic>
#include <memory>
#include <vector>

#include "common_components/taskpool/taskpool.h"
#include "ecmascript/mem/clock_scope.h"
#include "ecmascript/mem/mem_common.h"
#include "ecmascript/platform/mutex.h"

namespace panda::ecmascript {
class SharedHeap;
class Heap;
class Region;
class DaemonThread;
class JSThread;
class GlobalCCMarker;
class GlobalCCWorkManager;
class SharedCCEvacuator;
class SharedTlabAllocator;

struct GlobalCCRegionWorkload {
    explicit GlobalCCRegionWorkload(Heap *heap) : localHeap(heap) {}
    ~GlobalCCRegionWorkload() = default;

    NO_COPY_SEMANTIC(GlobalCCRegionWorkload);
    NO_MOVE_SEMANTIC(GlobalCCRegionWorkload);

    void AddRegion(Region *region)
    {
        regions.emplace_back(region);
    }

    Region *GetNextRegion()
    {
        size_t index = regionIter.fetch_add(1U, std::memory_order_relaxed);
        return index < regions.size() ? regions[index] : nullptr;
    }

    size_t RegionCount() const
    {
        return regions.size();
    }

    bool AllRegionsClaimed() const
    {
        return regionIter.load(std::memory_order_relaxed) >= regions.size();
    }

    Heap *localHeap {nullptr};  // nullptr denotes the shared heap.
    std::vector<Region*> regions {};
    std::atomic<size_t> regionIter {0};
    // Workers sum live bindings here; the final STW updates the owner's baseline.
    std::atomic<size_t> liveNativeBindingSize {0};
};

class GlobalCC {
public:
    explicit GlobalCC(SharedHeap *sHeap);
    ~GlobalCC() = default;

    NO_COPY_SEMANTIC(GlobalCC);
    NO_MOVE_SEMANTIC(GlobalCC);

    SharedHeap *GetHeap() const
    {
        return sHeap_;
    }

    bool IsRunning() const
    {
        return ccRunning_;
    }

    // Called when a worker VM becomes ready while GlobalCC is running.
    void PrepareNewThread(JSThread *thread);

    // Blocks (in NATIVE state) until the current GlobalCC cycle finishes; used
    // by callers that must not run local GC/mark work while GlobalCC owns the
    // heaps. Called with an active round on the target thread.
    void WaitGlobalCCFinished(JSThread *thread);

    // Waits out an active copy window. Restricted to callers that
    // may run concurrently with marking and are protected while they work
    // (self-held STW, RUNNING execution blocking new rounds, or the
    // cross-window safety rules). It does not protect against a copy window
    // opening after the call returns.
    void WaitGlobalCCCopyFinished(JSThread *thread);

private:
    class ReclaimTask;

    GlobalCCWorkManager *GetWorkManager() const
    {
        return workManager_;
    }

    void SetMarker(GlobalCCMarker *marker)
    {
        marker_ = marker;
    }

    void SetWorkManager(GlobalCCWorkManager *workManager)
    {
        workManager_ = workManager;
    }

    SharedTlabAllocator *GetTlabAllocator(uint32_t threadIndex)
    {
        return tlabAllocators_[threadIndex].get();
    }

    void RunConcurrentMarkPhase(GCReason gcReason);
    void RunPhases(GCReason gcReason);
    void NotifyTasksFinished();

    void PrepareMainThread();
    void WaitMainThreadReady();
    void ConcurrentMark();

    // Remark STW prepares and publishes the concurrent copy phase.
    void ReMarkAndPrepare(GCReason gcReason);
    void NotifyProfilersOfGlobalCCStart();
    void ConfigureConcurrentCopyParticipants();
    void InitializeEvacuationPhase();
    void PublishConcurrentCopyPhase();

    // Evacuation/update preparation under the Remark STW.
    void DrainLocalReclamationAndResetTlabs();
    void StartSharedNonEvacuatedSpaceSweep();
    void StartLocalNonEvacuatedSpaceSweep();
    void PrepareSharedHeapForEvacuation();
    void PrepareLocalHeapsForEvacuation();
    void BuildNonEvacuatedUpdateWorkloads();
    void InstallGlobalCCEvacuators();

    void ParallelCopy();
    void UpdateReferences();
    void AppendEvacuatedRegionsToUpdateWorkloads();
    void FinalizeAndReclaim();
    void MergeNonEvacuatedLocalToShareRSets();
    void FinalizeSharedHeapForReclaim();
    void FinalizeSharedEvacuationResources();
    void FinalizeLocalHeapAndThread(JSThread *thread, Heap *heap);
    void FinalizeLocalHeap(Heap *heap);
    void RestoreLocalThreadAfterGlobalCC(JSThread *thread, Heap *heap);
    static void ReclaimLocalHeapsThenShared(SharedHeap *sHeap, const std::vector<Heap *> &localHeaps);
    void VerifyHeapsAfterGlobalCC();
    void FinishGCStatistics();

    void PostStringTableSweepTask();
    void WaitStringTableSweep();
    void FinishConcurrentStringTableSweep();

    void FinalizeGlobalCCEvacuators();
    void ProcessWeakReference();
    void UpdateRecordWeakReference();
    void UpdateRoot();
    void EstimatePostCCSize();
    int CalculateWorkerThreadNum(size_t regionCount);
    void WaitWorkloadFinished();

    friend class SharedHeap;
    friend class GlobalCCMarker;
    friend class GlobalCCCopyTask;
    friend class GlobalCCUpdateTask;

    SharedHeap *sHeap_ {nullptr};
    DaemonThread *dThread_ {nullptr};
    GlobalCCMarker *marker_ {nullptr};
    GlobalCCWorkManager *workManager_ {nullptr};

    std::vector<std::unique_ptr<SharedTlabAllocator>> tlabAllocators_;
    ClockScope totalGcTimer_;
    float remarkStwDuration_ {0.0F};
    float finalizeSuspendDuration_ {0.0F};
    // Written only in STW; non-STW readers are ordered by thread-state/STW boundaries.
    bool ccRunning_ {false};

    std::unique_ptr<GlobalCCRegionWorkload> sharedCopyWorkload_ {};
    std::vector<std::unique_ptr<GlobalCCRegionWorkload>> localCopyWorkloads_ {};
    std::atomic<size_t> localCopyHeapIter_ {0};
    size_t copyRegionCount_ {0};
    std::unique_ptr<GlobalCCRegionWorkload> sharedUpdateWorkload_ {};
    std::vector<std::unique_ptr<GlobalCCRegionWorkload>> localUpdateWorkloads_ {};
    std::atomic<size_t> localUpdateHeapIter_ {0};
    size_t updateRegionCount_ {0};
    std::vector<SharedCCEvacuator*> evacuators_ {};
    Mutex evacuatorsMutex_;

    std::atomic<size_t> runningTaskCount_ {0};
    Mutex waitMutex_;
    ConditionVariable waitCV_;

    bool concurrentProcessStringTable_ {false};
};

class GlobalCCCopyTask : public common::Task {
public:
    GlobalCCCopyTask(std::vector<std::unique_ptr<GlobalCCRegionWorkload>> &localWorkloads,
                     std::atomic<size_t> &localHeapIter, std::atomic<size_t> &runningTaskCount,
                     GlobalCCRegionWorkload &sharedWorkload, GlobalCC *cc)
        : Task(0), localWorkloads_(localWorkloads), localHeapIter_(localHeapIter),
          runningTaskCount_(runningTaskCount), sharedWorkload_(sharedWorkload), cc_(cc) {}
    ~GlobalCCCopyTask() override = default;

    bool Run(uint32_t threadIndex) override;

    NO_COPY_SEMANTIC(GlobalCCCopyTask);
    NO_MOVE_SEMANTIC(GlobalCCCopyTask);

private:
    friend class GlobalCC;

    bool RunSharedWorkload(uint32_t threadIndex);
    void ProcessLocalWorkloads(uint32_t threadIndex);
    GlobalCCRegionWorkload *GetNextLocalWorkload();
    void ProcessLocalWorkload(GlobalCCRegionWorkload &workload, uint32_t threadIndex);
    void ProcessSharedWorkload(Region *region, SharedCCEvacuator &sharedEvacuator);
    template<typename Evacuator>
    void ProcessRegion(Region *region, Evacuator &evacuator);
    void HelpRemainingLocalWorkloads(uint32_t threadIndex);
    void NotifyFinished();

    std::vector<std::unique_ptr<GlobalCCRegionWorkload>> &localWorkloads_;
    std::atomic<size_t> &localHeapIter_;
    std::atomic<size_t> &runningTaskCount_;
    GlobalCCRegionWorkload &sharedWorkload_;
    GlobalCC *cc_ {nullptr};
};

class GlobalCCUpdateTask : public common::Task {
public:
    GlobalCCUpdateTask(std::vector<std::unique_ptr<GlobalCCRegionWorkload>> &localWorkloads,
                       std::atomic<size_t> &localHeapIter, std::atomic<size_t> &runningTaskCount,
                       GlobalCCRegionWorkload &sharedWorkload, GlobalCC *cc)
        : Task(0), localWorkloads_(localWorkloads), localHeapIter_(localHeapIter),
          runningTaskCount_(runningTaskCount), sharedWorkload_(sharedWorkload), cc_(cc) {}
    ~GlobalCCUpdateTask() override = default;

    bool Run(uint32_t threadIndex) override;

    NO_COPY_SEMANTIC(GlobalCCUpdateTask);
    NO_MOVE_SEMANTIC(GlobalCCUpdateTask);

private:
    friend class GlobalCC;

    bool RunSharedWorkload(uint32_t threadIndex);
    void ProcessLocalWorkloads(uint32_t threadIndex);
    GlobalCCRegionWorkload *GetNextLocalWorkload();
    void ProcessLocalWorkload(GlobalCCRegionWorkload &workload);
    void ProcessSharedWorkload(Region *region);
    void HelpRemainingLocalWorkloads(uint32_t threadIndex);
    void NotifyFinished();

    std::vector<std::unique_ptr<GlobalCCRegionWorkload>> &localWorkloads_;
    std::atomic<size_t> &localHeapIter_;
    std::atomic<size_t> &runningTaskCount_;
    GlobalCCRegionWorkload &sharedWorkload_;
    GlobalCC *cc_ {nullptr};
};

}  // namespace panda::ecmascript
#endif  // ECMASCRIPT_MEM_GLOBAL_CC_GLOBAL_CC_H
