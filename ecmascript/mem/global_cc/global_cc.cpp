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

#include "ecmascript/mem/global_cc/global_cc.h"

#include <algorithm>

#include "common_components/taskpool/taskpool.h"
#include "ecmascript/checkpoint/thread_state_transition.h"
#include "ecmascript/daemon/daemon_thread.h"
#include "ecmascript/dfx/hprof/moved_object_scanner.h"
#include "ecmascript/ecma_string_table.h"
#include "ecmascript/js_hclass.h"
#include "ecmascript/js_thread.h"
#include "ecmascript/layout_info.h"
#include "ecmascript/linked_hash_table.h"
#include "ecmascript/mem/gc_stats.h"
#include "ecmascript/mem/global_cc/global_concurrent_marker.h"
#include "ecmascript/mem/global_cc/global_mark_work_manager.h"
#include "ecmascript/mem/heap-inl.h"
#include "ecmascript/mem/local_cmc/cc_evacuator-inl.h"
#include "ecmascript/mem/local_cmc/cc_evacuator.h"
#include "ecmascript/mem/local_cmc/concurrent_copy_gc.h"
#include "ecmascript/mem/native_pointer_space-inl.h"
#include "ecmascript/mem/object_xray.h"
#include "ecmascript/mem/shared_heap/shared_cc.h"
#include "ecmascript/mem/shared_heap/shared_cc_evacuator-inl.h"
#include "ecmascript/mem/shared_heap/shared_cc_evacuator.h"
#include "ecmascript/mem/shared_heap/shared_concurrent_sweeper.h"
#include "ecmascript/mem/tlab_allocator-inl.h"
#include "ecmascript/mem/tlab_allocator.h"
#include "ecmascript/mem/verification.h"
#include "ecmascript/mem/visitor.h"
#include "ecmascript/pgo_profiler/pgo_profiler.h"
#include "ecmascript/runtime.h"

namespace panda::ecmascript {
class GlobalCC::ReclaimTask final : public common::Task {
public:
    ReclaimTask(int32_t id, SharedHeap *sHeap, std::vector<Heap *> localHeaps)
        : common::Task(id), sHeap_(sHeap), localHeaps_(std::move(localHeaps)) {}
    ~ReclaimTask() override = default;

    NO_COPY_SEMANTIC(ReclaimTask);
    NO_MOVE_SEMANTIC(ReclaimTask);

    bool Run([[maybe_unused]] uint32_t threadIndex) override
    {
        ECMA_BYTRACE_NAME(HITRACE_LEVEL_COMMERCIAL, HITRACE_TAG_ARK,
                          "GlobalCC::ReclaimTask::Run", "");
        GlobalCC::ReclaimLocalHeapsThenShared(sHeap_, localHeaps_);
        return true;
    }

private:
    SharedHeap *sHeap_ {nullptr};
    std::vector<Heap *> localHeaps_;
};

namespace {
class GlobalCCThreadRootVisitor final : public RootVisitor {
public:
    GlobalCCThreadRootVisitor(SharedCCEvacuator &sharedEvacuator, CCEvacuator &localEvacuator)
        : sharedEvacuator_(sharedEvacuator), localEvacuator_(localEvacuator) {}
    ~GlobalCCThreadRootVisitor() override = default;

    void VisitRoot(Root type, ObjectSlot slot) override;
    void VisitRangeRoot(Root type, ObjectSlot start, ObjectSlot end) override;
    void VisitBaseAndDerivedRoot(Root type, ObjectSlot base, ObjectSlot derived,
                                 uintptr_t baseOldObject) override;

private:
    void UpdateObjectSlotRoot(ObjectSlot slot);

    SharedCCEvacuator &sharedEvacuator_;
    CCEvacuator &localEvacuator_;
};

class GlobalCCLocalUpdateVisitor final : public BaseObjectVisitor<GlobalCCLocalUpdateVisitor> {
public:
    GlobalCCLocalUpdateVisitor() = default;
    ~GlobalCCLocalUpdateVisitor() override = default;

    void VisitObjectRangeImpl(BaseObject *root, ObjectSlot start, ObjectSlot end,
        VisitObjectArea area) override;
    void VisitCompressedObjectRangeImpl(BaseObject *root, CompressedObjectSlot start,
        CompressedObjectSlot end) override;

    template<ReferenceType refType>
    void HandleSlot(ObjectSlotBase<refType> slot, Region *rootRegion);

private:
    void HandleInObjectArea(TaggedObject *rootObject, ObjectSlot startSlot, ObjectSlot endSlot);
};

// Runs under the CC status lock: threads that cannot run concurrently with the
// string-table sweep are CC-suspended; others keep (or acquire) the read
// barrier and become READY.
void SuspendThreadForRound(JSThread *thread, SharedCCStatus &status, bool concurrentProcessStringTable)
{
    if (!concurrentProcessStringTable) {
        thread->SetCCSuspend();
        status = SharedCCStatus::SUSPENDED;
        return;
    }
    if (status != SharedCCStatus::READY && status != SharedCCStatus::SUSPENDED) {
        if (thread->GetLastLeaveFrame() == nullptr) {
            thread->HoldReadBarrier(ReadBarrierOwner::SHARED_CC);
            status = SharedCCStatus::READY;
        } else {
            thread->SetCCSuspend();
            status = SharedCCStatus::SUSPENDED;
        }
    }
}
}  // namespace

GlobalCC::GlobalCC(SharedHeap *sHeap)
    : sHeap_(sHeap), dThread_(DaemonThread::GetInstance())
{
    uint32_t totalThreads = common::MAX_TASKPOOL_THREAD_NUM + 1;
    tlabAllocators_.reserve(totalThreads);
    for (uint32_t i = 0; i < totalThreads; i++) {
        tlabAllocators_.emplace_back(std::make_unique<SharedTlabAllocator>(sHeap_));
    }
}

void GlobalCC::RunConcurrentMarkPhase(GCReason gcReason)
{
    {
        ECMA_BYTRACE_NAME(HITRACE_LEVEL_COMMERCIAL, HITRACE_TAG_ARK,
                          "GlobalCC::WaitSensitiveStatusFinished", "");
        // Avoid a sensitive-state deadlock during preparation.
        sHeap_->WaitSensitiveStatusFinished();
    }
    ECMA_BYTRACE_NAME(HITRACE_LEVEL_COMMERCIAL, HITRACE_TAG_ARK,
        ("GlobalCC::RunConcurrentMarkPhase;GCReason" +
         std::to_string(static_cast<int>(gcReason))).c_str(), "");
    // TotalGC is finalized in FinalizeAndReclaim (see the TRACE_GC note in gc_stats.h).
    totalGcTimer_.Reset();

    PrepareMainThread();
    ConcurrentMark();
}

void GlobalCC::RunPhases(GCReason gcReason)
{
    ECMA_BYTRACE_NAME(HITRACE_LEVEL_COMMERCIAL, HITRACE_TAG_ARK,
        ("GlobalCC::RunPhases;GCReason" +
         std::to_string(static_cast<int>(gcReason))).c_str(), "");
    ReMarkAndPrepare(gcReason);
    ParallelCopy();
    PostStringTableSweepTask();
    UpdateReferences();
    WaitStringTableSweep();
    FinalizeAndReclaim();
}

void GlobalCC::PrepareMainThread()
{
    ECMA_BYTRACE_NAME(HITRACE_LEVEL_COMMERCIAL, HITRACE_TAG_ARK, "GlobalCC::PrepareMainThread", "");
    TRACE_GC(GCStats::Scope::ScopeId::Initialize, sHeap_->GetEcmaGCStats());
    // Only the main thread needs this handshake (it may be parked in the
    // trigger wait); the other threads' states are set directly under the
    // marker's initial-mark STW.
    Runtime::GetInstance()->GCIterateThreadList([](JSThread *thread) {
        if (thread != Runtime::GetInstance()->GetMainThread() ||
            !thread->HasPostTaskToThreadCallback()) {
            return;
        }
        if (thread->TryMarkCCTaskPending()) {
            thread->PostRuntimeThreadTask();
        }
    });
    WaitMainThreadReady();
}

void GlobalCC::WaitMainThreadReady()
{
    JSThread *mainThread = Runtime::GetInstance()->GetMainThread();
    if (mainThread == nullptr) {
        return;
    }
    if (mainThread->GetSharedCCStatus() == SharedCCStatus::READY) {
        return;
    }

    Mutex &mtx = sHeap_->GetSharedGCSyncMutex();
    ConditionVariable &cv = sHeap_->GetSharedGCSyncCV();
    LockHolder lock(mtx);
    bool done = false;
    while (!done) {
        mainThread->WithCCStatusLock([&](SharedCCStatus &status) {
            if (status == SharedCCStatus::READY) {
                done = true;
                return;
            }
            if (mainThread->IsWaitingSharedGCFinished()) {
                mainThread->SetCCSuspend();
                status = SharedCCStatus::SUSPENDED;
                LOG_GC(DEBUG) << "GlobalCC: main thread blocked in WaitGCFinished, set SUSPENDED";
                done = true;
            }
        });
        if (!done) {
            cv.Wait(&mtx);
        }
    }
}

void GlobalCC::ConcurrentMark()
{
    ECMA_BYTRACE_NAME(HITRACE_LEVEL_COMMERCIAL, HITRACE_TAG_ARK, "GlobalCC::ConcurrentMark", "");
    TRACE_GC(GCStats::Scope::ScopeId::ConcurrentMark, sHeap_->GetEcmaGCStats());
    marker_->Mark();
}

void GlobalCC::ReMarkAndPrepare(GCReason gcReason)
{
    ECMA_BYTRACE_NAME(HITRACE_LEVEL_COMMERCIAL, HITRACE_TAG_ARK, "GlobalCC::ReMarkAndPrepare", "");
    TRACE_GC(GCStats::Scope::ScopeId::ReMark, sHeap_->GetEcmaGCStats());
    SharedHeap::LocalCCDrainScope localCCDrainScope(dThread_);
    ThreadManagedScope runningScope(dThread_);
    ClockScope stwTimer;
    {
        SuspendAllScope scope(dThread_);
        NotifyProfilersOfGlobalCCStart();
        sHeap_->CheckProfilerEnabled();
        sHeap_->GetEcmaGCStats()->RecordStatisticBeforeGC(TriggerGCType::GLOBAL_CC, gcReason);
        marker_->ReMark();
        ConfigureConcurrentCopyParticipants();
        InitializeEvacuationPhase();
        ProcessWeakReference();
        UpdateRoot();
        // Finish mark work only after its weak queues and roots are processed.
        dThread_->SetSharedMarkStatus(SharedMarkStatus::READY_TO_CONCURRENT_MARK);
        workManager_->Finish();
        StartLocalNonEvacuatedSpaceSweep();
        PublishConcurrentCopyPhase();
    }
    remarkStwDuration_ = stwTimer.TotalSpentTime();
}

void GlobalCC::ConfigureConcurrentCopyParticipants()
{
    ECMA_BYTRACE_NAME(HITRACE_LEVEL_COMMERCIAL, HITRACE_TAG_ARK,
                      "GlobalCC::ConfigureConcurrentCopyParticipants", "");
    auto stringTableCleaner = Runtime::GetInstance()->GetEcmaStringTable()->GetCleaner();
    concurrentProcessStringTable_ = stringTableCleaner->IsEnableConcurrentSweep();
    Runtime::GetInstance()->GCIterateThreadList(
        [concurrentSweep = concurrentProcessStringTable_](JSThread *thread) {
            thread->WithCCStatusLock([thread, concurrentSweep](SharedCCStatus &status) {
                SuspendThreadForRound(thread, status, concurrentSweep);
            });
        });
    Runtime::GetInstance()->IterateAllThreadList([](JSThread *thread) {
        if (thread->IsJitThread()) {
            thread->SetCCSuspend();
        }
    });
}

void GlobalCC::InitializeEvacuationPhase()
{
    ECMA_BYTRACE_NAME(HITRACE_LEVEL_COMMERCIAL, HITRACE_TAG_ARK, "GlobalCC::InitializeEvacuationPhase", "");
    DrainLocalReclamationAndResetTlabs();
    StartSharedNonEvacuatedSpaceSweep();
    PrepareSharedHeapForEvacuation();
    PrepareLocalHeapsForEvacuation();
    // Large local heaps first: avoid a long serial tail before region-level help.
    std::sort(localCopyWorkloads_.begin(), localCopyWorkloads_.end(),
        [](const auto &lhs, const auto &rhs) { return lhs->RegionCount() > rhs->RegionCount(); });
    BuildNonEvacuatedUpdateWorkloads();
    if (concurrentProcessStringTable_) {
        Runtime::GetInstance()->GetEcmaStringTable()->GetChainedHashMap()->StartSweeping();
    }
    InstallGlobalCCEvacuators();
}

void GlobalCC::PublishConcurrentCopyPhase()
{
    ECMA_BYTRACE_NAME(HITRACE_LEVEL_COMMERCIAL, HITRACE_TAG_ARK,
                      "GlobalCC::PublishConcurrentCopyPhase", "");
    EstimatePostCCSize();
    ccRunning_ = true;
    sHeap_->UpdateGCThresholds(TriggerGCType::GLOBAL_CC);
    sHeap_->FinishGCTask();
    sHeap_->NotifyGCCompleted();
}

void GlobalCC::NotifyProfilersOfGlobalCCStart()
{
    Runtime::GetInstance()->GCIterateThreadList([](JSThread *thread) {
#ifndef EXCLUDE_PGO
        std::shared_ptr<PGOProfiler> pgoProfiler = thread->GetEcmaVM()->GetPGOProfiler();
        if (pgoProfiler != nullptr) {
            pgoProfiler->SuspendByGC();
        }
#endif
#if defined(ECMASCRIPT_SUPPORT_CPUPROFILER)
        thread->SetGcState(true);
#endif
    });
}

void GlobalCC::DrainLocalReclamationAndResetTlabs()
{
    // Shared sweep may build free lists over memory reachable from these TLABs.
    Runtime::GetInstance()->GCIterateThreadList([](JSThread *thread) {
        Heap *heap = thread->GetEcmaVM()->GetHeap();
        heap->GetSweeper()->EnsureAllTaskFinished();
        heap->WaitClearTaskFinished();
        heap->ResetTlab();
    });
}

void GlobalCC::StartSharedNonEvacuatedSpaceSweep()
{
    ECMA_BYTRACE_NAME(HITRACE_LEVEL_COMMERCIAL, HITRACE_TAG_ARK,
                      "GlobalCC::StartSharedNonEvacuatedSpaceSweep", "");
    // Sweep shared huge regions before collecting update workloads.
    sHeap_->GetSweeper()->Sweep(true);
    sHeap_->GetSweeper()->PostTask(true);
}

void GlobalCC::StartLocalNonEvacuatedSpaceSweep()
{
    ECMA_BYTRACE_NAME(HITRACE_LEVEL_COMMERCIAL, HITRACE_TAG_ARK,
                      "GlobalCC::StartLocalNonEvacuatedSpaceSweep", "");
    Runtime::GetInstance()->GCIterateThreadList([](JSThread *thread) {
        Heap *heap = thread->GetEcmaVM()->GetHeap();
        if (heap->GetCmsGC()) {
            return;
        }
        heap->GetSweeper()->Sweep(TriggerGCType::GLOBAL_CC);
        heap->PrepareRecordNonmovableRegions();
        heap->GetSweeper()->PostTask(TriggerGCType::GLOBAL_CC);
    });
}

void GlobalCC::PrepareSharedHeapForEvacuation()
{
    sHeap_->SwapOldSpace();
    sHeap_->GetCompressSpace()->EnumerateRegions([](Region *region) {
        region->SetRegionTypeFlag(RegionTypeFlag::FROM);
    });
    sharedCopyWorkload_ = std::make_unique<GlobalCCRegionWorkload>(nullptr);
    localCopyWorkloads_.clear();
    localCopyHeapIter_ = 0;
    copyRegionCount_ = 0;
    sHeap_->GetCompressSpace()->EnumerateRegions([this](Region *region) {
        sharedCopyWorkload_->AddRegion(region);
    });
    copyRegionCount_ += sharedCopyWorkload_->RegionCount();
}

void GlobalCC::PrepareLocalHeapsForEvacuation()
{
    Runtime::GetInstance()->GCIterateThreadList([this](JSThread *thread) {
        Heap *heap = thread->GetEcmaVM()->GetHeap();
        heap->SetGCType(TriggerGCType::GLOBAL_CC);
        // Keep native allocations made during copy in the next GC's increment.
        heap->ResetNativeSizeAfterLastGC();
        // Full promotion makes every old-to-new entry stale.
        heap->EnumerateNonNewSpaceRegions([](Region *current) {
            current->DeleteOldToNewRSet();
        });
        // Distinguish GlobalCC ownership while preserving copying semantics.
        thread->SetCCStatus(CCStatus::GLOBAL_COPYING);
        heap->SelectFromSpace();
        // LocalCC sweeps ModuleSpace in place, while GlobalCC compacts it.
        heap->GetModuleSpace()->PrepareEvacuation();
        if constexpr (!G_USE_CMS_GC) {
            heap->GetNativePointerSpace()->PrepareEvacuation();
        }
        heap->GetCCGC()->GetTlabAllocator(MAIN_THREAD_INDEX)->Initialize();
        if (thread->GetSharedCCStatus() == SharedCCStatus::READY) {
            thread->AcquireReadBarrier(ReadBarrierOwner::SHARED_CC);
        }
        auto localWorkload = std::make_unique<GlobalCCRegionWorkload>(heap);
        heap->GetFromSpaceDuringEvacuation()->EnumerateRegions([&localWorkload](Region *region) {
            localWorkload->AddRegion(region);
        });
        heap->GetCompressSpace()->EnumerateRegions([&localWorkload](Region *region) {
            localWorkload->AddRegion(region);
        });
        heap->GetModuleSpace()->EnumerateFromRegions([&localWorkload](Region *region) {
            localWorkload->AddRegion(region);
        });
        if constexpr (!G_USE_CMS_GC) {
            heap->GetNativePointerSpace()->EnumerateFromRegions([&localWorkload](Region *region) {
                localWorkload->AddRegion(region);
            });
        }
        copyRegionCount_ += localWorkload->RegionCount();
        localCopyWorkloads_.emplace_back(std::move(localWorkload));
        // GLOBAL_COPYING keeps waiters blocked while marking routes are closed.
        thread->SetGlobalConcurrentMarking(false);
        thread->SetMarkStatus(MarkStatus::READY_TO_MARK);
    });
    // Stop new threads from inheriting the marking route after every existing
    // thread is protected by GLOBAL_COPYING.
    dThread_->SetGlobalConcurrentMarking(false);
}

namespace {
// Local regions that stay in place. FROM regions are discarded; TO regions
// prepare their rebuild sets at creation and are appended after copying.
template <typename Visitor>
void EnumerateLocalNonEvacuatedRegions(Heap *heap, const Visitor &visitor)
{
    heap->GetNonMovableSpace()->EnumerateRegions(visitor);
    heap->GetHugeObjectSpace()->EnumerateRegions(visitor);
    heap->GetMachineCodeSpace()->EnumerateRegions(visitor);
    heap->GetHugeMachineCodeSpace()->EnumerateRegions(visitor);
    heap->GetAppSpawnSpace()->EnumerateRegions(visitor);
    heap->GetSnapshotSpace()->EnumerateRegions(visitor);
}
}  // namespace

void GlobalCC::BuildNonEvacuatedUpdateWorkloads()
{
    sharedUpdateWorkload_ = std::make_unique<GlobalCCRegionWorkload>(nullptr);
    localUpdateWorkloads_.clear();
    localUpdateHeapIter_ = 0;
    updateRegionCount_ = 0;

    auto collectShared = [this](Region *region) {
        sharedUpdateWorkload_->AddRegion(region);
        updateRegionCount_++;
    };
    sHeap_->GetNonMovableSpace()->EnumerateRegions(collectShared);
    sHeap_->GetHugeObjectSpace()->EnumerateRegions(collectShared);
    sHeap_->GetAppSpawnSpace()->EnumerateRegions(collectShared);

    Runtime::GetInstance()->GCIterateThreadList([this](JSThread *thread) {
        Heap *heap = thread->GetEcmaVM()->GetHeap();
        auto localWorkload = std::make_unique<GlobalCCRegionWorkload>(heap);
        auto collectLocal = [this, &localWorkload](Region *region) {
            if ((region->InHugeObjectSpace() || region->InHugeMachineCodeSpace()) &&
                !region->Test(region->GetBegin())) {
                return;
            }
            region->CreateLocalToShareRememberedSet()->ClearAll();
#ifdef USE_COMPRESSED_POINTER
            region->CreateCompressedLocalToShareRememberedSet()->ClearAll();
#endif
            region->SwapLocalToShareRSetForCC();
            localWorkload->AddRegion(region);
            updateRegionCount_++;
        };
        EnumerateLocalNonEvacuatedRegions(heap, collectLocal);
        localUpdateWorkloads_.emplace_back(std::move(localWorkload));
    });
}

void GlobalCC::InstallGlobalCCEvacuators()
{
    // Local evacuators are JSThread-owned and reused through the read barrier.
    LockHolder lock(evacuatorsMutex_);
    evacuators_.clear();
    Runtime::GetInstance()->GCIterateThreadList([this](JSThread *thread) {
        auto *evacuator = new SharedCCEvacuator(sHeap_);
        evacuators_.push_back(evacuator);
        thread->InstallSharedCCEvacuator(evacuator);
    });
}

void GlobalCC::FinalizeGlobalCCEvacuators()
{
    // The final STW clears every thread's pointer before these evacuators are deleted.
    LockHolder lock(evacuatorsMutex_);
    for (auto *evacuator : evacuators_) {
        delete evacuator;
    }
    evacuators_.clear();
}

void GlobalCC::EstimatePostCCSize()
{
    // SelectFromSpace already updated every local heap's preserved size.
    size_t totalAliveSize = 0;
    sHeap_->GetCompressSpace()->EnumerateRegions([&totalAliveSize](Region *r) {
        totalAliveSize += r->AliveObject();
    });
    sHeap_->GetOldSpace()->SetPreservedSize(totalAliveSize);
}

namespace {
// Recorded weak fields must be cleared before an external weak root can copy its target.
template<class Evacuator>
TaggedObject *ForwardWeakFromRegion(TaggedObject *object, Region *objectRegion, Evacuator &evacuator)
{
    if (!objectRegion->Test(object)) {
        return nullptr;
    }
    MarkWord markWord(object, RELAXED_LOAD);
    if (markWord.IsForwardingAddress()) {
        return markWord.ToForwardingAddress();
    }
    return evacuator.Copy(object, markWord);
}

TaggedObject *ForwardSharedWeakRoot(TaggedObject *object, SharedCCEvacuator &evacuator)
{
    Region *objectRegion = Region::ObjectAddressToRange(object);
    if (objectRegion->IsFromRegion()) {
        return ForwardWeakFromRegion(object, objectRegion, evacuator);
    }
    return GlobalCCMarker::IsAlive(object) ? object : nullptr;
}

TaggedObject *ForwardThreadWeakRoot(TaggedObject *object, SharedCCEvacuator &sharedEvacuator,
    CCEvacuator &localEvacuator)
{
    Region *objectRegion = Region::ObjectAddressToRange(object);
    if (objectRegion->IsFromRegion()) {
        return objectRegion->InSharedHeap() ? ForwardWeakFromRegion(object, objectRegion, sharedEvacuator)
                                            : ForwardWeakFromRegion(object, objectRegion, localEvacuator);
    }
    return GlobalCCMarker::IsAlive(object) ? object : nullptr;
}

void RemoveClearedEntriesFromWeakLinkedHashMap(WeakLinkedHashMap *map, JSThread *thread)
{
    int entries = map->NumberOfAllUsedElements();
    for (int i = 0; i < entries; i++) {
        JSTaggedValue maybeKey = map->GetKey(thread, i);
        if (maybeKey.IsUndefined()) {
            map->RemoveEntryFromGCThread(i);
        }
    }
}
}  // namespace

void GlobalCC::ProcessWeakReference()
{
    ECMA_BYTRACE_NAME(HITRACE_LEVEL_COMMERCIAL, HITRACE_TAG_ARK, "GlobalCC::ProcessWeakReference", "");
    TRACE_GC(GCStats::Scope::ScopeId::UpdateWeekRef, sHeap_->GetEcmaGCStats());
    // The recorded queues must be fully drained before the external weak roots
    // below may copy, so that no dead weak field survives into a copy.
    UpdateRecordWeakReference();

    SharedCCEvacuator sharedEvacuator(sHeap_, GetTlabAllocator(DAEMON_THREAD_INDEX));
    Runtime::GetInstance()->ProcessSharedDelete([&sharedEvacuator](TaggedObject *object) {
        return ForwardSharedWeakRoot(object, sharedEvacuator);
    });

    Runtime::GetInstance()->GCIterateThreadList([](JSThread *thread) {
        SharedCCEvacuator &sharedEvacuator = *thread->GetSharedCCEvacuator();
        CCEvacuator &localEvacuator = *thread->GetLocalCCEvacuator();
        WeakRootVisitor weakVisitor = [&sharedEvacuator, &localEvacuator](TaggedObject *object) {
            return ForwardThreadWeakRoot(object, sharedEvacuator, localEvacuator);
        };
        thread->GetEcmaVM()->ProcessReferences(weakVisitor);
        thread->GetEcmaVM()->ProcessPendingRemovalLocalModules(weakVisitor);
        thread->IterateWeakRoots(weakVisitor);
        thread->IterateWeakEcmaGlobalStorage(weakVisitor, GCKind::SHARED_GC);
        thread->GetEcmaVM()->ProcessSnapShotEnv(weakVisitor);
        thread->GetEcmaVM()->IterateWeakGlobalEnvList(weakVisitor);
        thread->UpdateJitCodeMapReference(weakVisitor);
        thread->ClearVMCachedConstantPool();
    });
}

namespace {
void ProcessGlobalCCWeakReferenceSlot(ObjectSlot &slot)
{
    JSTaggedValue value(slot.GetTaggedType());
    if (!value.IsWeak()) {
        return;
    }
    if (!GlobalCCMarker::IsAlive(value.GetTaggedWeakRef())) {
        slot.Clear();
    }
}
}  // namespace

void GlobalCC::UpdateRecordWeakReference()
{
    ECMA_BYTRACE_NAME(HITRACE_LEVEL_COMMERCIAL, HITRACE_TAG_ARK,
                      "GlobalCC::UpdateRecordWeakReference", "");
    auto *workManager = workManager_;
    auto drainWeakQueue = [](ProcessQueue *queue) {
        auto *obj = queue->PopBack();
        while (obj != nullptr) {
            ObjectSlot slot(ToUintPtr(obj));
            ProcessGlobalCCWeakReferenceSlot(slot);
            obj = queue->PopBack();
        }
    };
    auto drainWeakLinkedHashMapQueue = [this](WeakLinkedHashMapProcessQueue *queue) {
        TaggedObject *obj = queue->PopBack();
        while (obj != nullptr) {
            WeakLinkedHashMap *map = WeakLinkedHashMap::Cast(obj);
            ASSERT(map->VerifyLayout());
            RemoveClearedEntriesFromWeakLinkedHashMap(map, dThread_);
            obj = queue->PopBack();
        }
    };
    uint32_t totalThreadCount = common::Taskpool::GetCurrentTaskpool()->GetTotalThreadNum() + 1;
    for (uint32_t i = 0; i < totalThreadCount; i++) {
        GlobalCCWorkNodeHolder *holder = workManager->GetHolder(i);
        drainWeakQueue(holder->GetWeakReferenceQueue());
        drainWeakLinkedHashMapQueue(holder->GetWeakLinkedHashMapQueue());
    }
}

void GlobalCC::UpdateRoot()
{
    ECMA_BYTRACE_NAME(HITRACE_LEVEL_COMMERCIAL, HITRACE_TAG_ARK, "GlobalCC::UpdateRoot", "");
    TRACE_GC(GCStats::Scope::ScopeId::UpdateRoot, sHeap_->GetEcmaGCStats());

    Runtime::GetInstance()->GCIterateThreadList([](JSThread *thread) {
        GlobalCCThreadRootVisitor rootVisitor(*thread->GetSharedCCEvacuator(), *thread->GetLocalCCEvacuator());
        ObjectXRay::VisitVMRoots(thread->GetEcmaVM(), rootVisitor);
        thread->GetEcmaVM()->IterateGlobalEnvField(rootVisitor);
#if ECMASCRIPT_ENABLE_ARK_STEED
        auto *refs = thread->GetEcmaVM()->GetHeap()->GetEmbeddedCodeRefSet();
        if (!refs->IsEmpty()) {
            refs->VisitMarkedGlobalTargets(rootVisitor);
            // The shared path initializes the daemon's JitFort access before patching local targets.
            if (!refs->UpdateSharedTargets() || !refs->UpdateMarkedLocalTargets()) {
                LOG_GC(FATAL) << "Failed to update ArkSteed embedded heap references during GlobalCC";
            }
        }
#endif
        // Refresh native pointers derived from handles before mutators resume.
        thread->GetEcmaVM()->GetHeap()->ProcessGCListeners();
    });
    SharedCCEvacuator sharedEvacuator(sHeap_, GetTlabAllocator(DAEMON_THREAD_INDEX));
    SharedCCRootVisitor rootVisitor(&sharedEvacuator);
    Runtime::GetInstance()->IterateSharedRoot(rootVisitor);
}

namespace {
void GlobalCCThreadRootVisitor::VisitRoot([[maybe_unused]] Root type, ObjectSlot slot)
{
    UpdateObjectSlotRoot(slot);
}

void GlobalCCThreadRootVisitor::VisitRangeRoot([[maybe_unused]] Root type, ObjectSlot start, ObjectSlot end)
{
    for (ObjectSlot slot = start; slot < end; slot++) {
        UpdateObjectSlotRoot(slot);
    }
}

void GlobalCCThreadRootVisitor::VisitBaseAndDerivedRoot([[maybe_unused]] Root type, ObjectSlot base,
                                                        ObjectSlot derived, uintptr_t baseOldObject)
{
    UpdateObjectSlotRoot(base);
    if (JSTaggedValue(base.GetTaggedType()).IsHeapObject()) {
        derived.Update(base.GetTaggedType() + derived.GetTaggedType() - baseOldObject);
    }
}

void GlobalCCThreadRootVisitor::UpdateObjectSlotRoot(ObjectSlot slot)
{
    JSTaggedValue value(slot.GetTaggedType());
    if (!value.IsHeapObject()) {
        return;
    }
    TaggedObject *object = value.GetHeapObject();
    Region *objectRegion = Region::ObjectAddressToRange(object);
    if (!objectRegion->IsFromRegion()) {
        return;
    }
    MarkWord markWord(object, RELAXED_LOAD);
    TaggedObject *toObject = nullptr;
    if (markWord.IsForwardingAddress()) {
        toObject = markWord.ToForwardingAddress();
    } else if (objectRegion->InSharedHeap()) {
        toObject = sharedEvacuator_.Copy(object, markWord);
    } else {
        toObject = localEvacuator_.Copy(object, markWord);
    }
    slot.Update(toObject);
}

}  // namespace

void GlobalCC::ParallelCopy()
{
    ECMA_BYTRACE_NAME(HITRACE_LEVEL_COMMERCIAL, HITRACE_TAG_ARK, "GlobalCC::ParallelCopy", "");
    TRACE_GC(GCStats::Scope::ScopeId::Evacuate, sHeap_->GetEcmaGCStats());
    ASSERT(runningTaskCount_ == 0);
    int workerCount = CalculateWorkerThreadNum(copyRegionCount_);
    ASSERT(workerCount > 0);

    GlobalCCRegionWorkload &sharedWorkload = *sharedCopyWorkload_;

    // Taskpool worker indices start at one, so their local allocators never
    // alias the mutator/read-barrier allocator at index zero. The daemon joins
    // bulk copy only for shared-heap work and therefore never accesses a local
    // allocator with index zero.
    runningTaskCount_ = workerCount + 1;
    for (int i = 0; i < workerCount; i++) {
        common::Taskpool::GetCurrentTaskpool()->PostTask(
            std::make_unique<GlobalCCCopyTask>(localCopyWorkloads_, localCopyHeapIter_, runningTaskCount_,
                                               sharedWorkload, this));
    }
    GlobalCCCopyTask daemonTask(localCopyWorkloads_, localCopyHeapIter_, runningTaskCount_,
                                sharedWorkload, this);
    daemonTask.RunSharedWorkload(DAEMON_THREAD_INDEX);
    WaitWorkloadFinished();
    ASSERT(localCopyHeapIter_.load(std::memory_order_relaxed) >= localCopyWorkloads_.size());
    ASSERT(sharedWorkload.AllRegionsClaimed());
    for (const auto &workload : localCopyWorkloads_) {
        ASSERT(workload->AllRegionsClaimed());
    }
}

int GlobalCC::CalculateWorkerThreadNum(size_t regionCount)
{
    constexpr uint32_t regionPerThread = 8;
    size_t maxThreadNum = common::Taskpool::GetCurrentTaskpool()->GetTotalThreadNum();
    size_t workerCount = std::max<size_t>(1, regionCount / regionPerThread);
    return static_cast<int>(std::min(workerCount, maxThreadNum));
}

void GlobalCC::NotifyTasksFinished()
{
    LockHolder lock(waitMutex_);
    waitCV_.SignalAll();
}

void GlobalCC::PrepareNewThread(JSThread *thread)
{
    // Lock order: CC status -> evacuators.
    thread->WithCCStatusLock([&](SharedCCStatus &status) {
        LockHolder evacuatorLock(evacuatorsMutex_);
        if (!ccRunning_) {
            return;
        }
        if (thread->GetSharedCCEvacuator() == nullptr) {
            auto *evacuator = new SharedCCEvacuator(sHeap_);
            evacuators_.push_back(evacuator);
            thread->InstallSharedCCEvacuator(evacuator);
        }
        // Trigger suppression for the whole round is carried by the
        // per-thread states: MarkStatus during the marking window,
        // GLOBAL_COPYING (set below) during the copy window.
        if (concurrentProcessStringTable_) {
            thread->AcquireReadBarrier(ReadBarrierOwner::SHARED_CC);
            thread->SetCCStatus(CCStatus::GLOBAL_COPYING);
            status = SharedCCStatus::READY;
        } else {
            thread->SetCCSuspend();
            status = SharedCCStatus::SUSPENDED;
        }
    });
}

void GlobalCC::WaitGlobalCCFinished(JSThread *thread)
{
    // Never wait in RUNNING state: a STW may need the waiting thread to
    // reach a safepoint before the wait can end.
    ThreadNativeScope nativeScope(thread);
    // The marking and copying flags cover the full round; ccRunning_ is
    // cleared before the final STW finishes restoring thread state.
    LockHolder lock(waitMutex_);
    while (thread->IsGlobalConcurrentCopying() || thread->IsGlobalConcurrentMarking()) {
        waitCV_.Wait(&waitMutex_);
    }
}

void GlobalCC::WaitGlobalCCCopyFinished(JSThread *thread)
{
    ThreadNativeScope nativeScope(thread);
    LockHolder lock(waitMutex_);
    while (thread->IsGlobalConcurrentCopying()) {
        waitCV_.Wait(&waitMutex_);
    }
}

void GlobalCC::WaitWorkloadFinished()
{
    LockHolder lock(waitMutex_);
    while (runningTaskCount_ > 0) {
        waitCV_.Wait(&waitMutex_);
    }
}

bool GlobalCCCopyTask::Run(uint32_t threadIndex)
{
    // Taskpool worker indices start at 1 (index 0 is the mutator main thread),
    // so local-heap allocators never collide with the main-thread read barrier.
    ASSERT(threadIndex != DAEMON_THREAD_INDEX);
    auto *allocator = cc_->GetTlabAllocator(threadIndex);
    SharedCCEvacuator sharedEvacuator(cc_->GetHeap(), allocator);

    ProcessLocalWorkloads(threadIndex);
    ProcessSharedWorkload(sharedWorkload_.GetNextRegion(), sharedEvacuator);

    NotifyFinished();
    return true;
}

bool GlobalCCCopyTask::RunSharedWorkload(uint32_t threadIndex)
{
    ASSERT(threadIndex == DAEMON_THREAD_INDEX);
    SharedCCEvacuator sharedEvacuator(cc_->GetHeap(), cc_->GetTlabAllocator(threadIndex));
    ProcessSharedWorkload(sharedWorkload_.GetNextRegion(), sharedEvacuator);
    NotifyFinished();
    return true;
}

void GlobalCCCopyTask::ProcessLocalWorkloads(uint32_t threadIndex)
{
    GlobalCCRegionWorkload *workload = GetNextLocalWorkload();
    while (workload != nullptr) {
        ProcessLocalWorkload(*workload, threadIndex);
        workload = GetNextLocalWorkload();
    }
    HelpRemainingLocalWorkloads(threadIndex);
}

void GlobalCCCopyTask::NotifyFinished()
{
    if (runningTaskCount_.fetch_sub(1, std::memory_order_seq_cst) == 1) {
        cc_->NotifyTasksFinished();
    }
}

GlobalCCRegionWorkload *GlobalCCCopyTask::GetNextLocalWorkload()
{
    size_t idx = localHeapIter_.fetch_add(1U, std::memory_order_relaxed);
    return idx < localWorkloads_.size() ? localWorkloads_[idx].get() : nullptr;
}

void GlobalCCCopyTask::ProcessLocalWorkload(GlobalCCRegionWorkload &workload, uint32_t threadIndex)
{
    Region *region = workload.GetNextRegion();
    if (region == nullptr) {
        return;
    }

    Heap *localHeap = workload.localHeap;
    auto *localAllocator = localHeap->GetCCGC()->GetTlabAllocator(threadIndex);
    // Scope the worker's local TLAB to one heap assignment. This preserves
    // reuse across all regions of that heap, while releasing its tail before
    // the worker moves to another heap during ownership or tail-helping.
    localAllocator->Initialize();
    CCEvacuator localEvacuator(localHeap, localAllocator);
    size_t liveNativeBindingSize = 0;
    while (region != nullptr) {
        if constexpr (!G_USE_CMS_GC) {
            if (region->InNativePointerSpace()) {
                localHeap->GetNativePointerSpace()->CollectDeadCallbacksFromRegion(region,
                    [&localEvacuator, &liveNativeBindingSize](TaggedObject *object) {
                        MarkWord markWord(object, RELAXED_LOAD);
                        TaggedObject *toObject = markWord.IsForwardingAddress()
                            ? markWord.ToForwardingAddress() : localEvacuator.Copy(object, markWord);
                        liveNativeBindingSize += JSNativePointer::Cast(toObject)->GetBindingSize();
                        Region::ObjectAddressToRange(toObject)->AtomicMark(toObject);
                    });
            } else {
                ProcessRegion(region, localEvacuator);
            }
        } else {
            ProcessRegion(region, localEvacuator);
        }
        region = workload.GetNextRegion();
    }
    localAllocator->Finalize();
    if (liveNativeBindingSize != 0) {
        workload.liveNativeBindingSize.fetch_add(liveNativeBindingSize, std::memory_order_relaxed);
    }
}

void GlobalCCCopyTask::ProcessSharedWorkload(Region *region, SharedCCEvacuator &sharedEvacuator)
{
    while (region != nullptr) {
        ProcessRegion(region, sharedEvacuator);
        region = sharedWorkload_.GetNextRegion();
    }
}

template<typename Evacuator>
void GlobalCCCopyTask::ProcessRegion(Region *region, Evacuator &evacuator)
{
    ASSERT(region->IsFromRegion());
    region->IterateAllMarkedBits([&evacuator](void *mem) {
        TaggedObject *object = reinterpret_cast<TaggedObject*>(mem);
        MarkWord markWord(object, RELAXED_LOAD);
        TaggedObject *toObject = markWord.IsForwardingAddress()
            ? markWord.ToForwardingAddress()
            : evacuator.Copy(object, markWord);
        // Mark to-space copy so update phase skips new allocations.
        Region::ObjectAddressToRange(toObject)->AtomicMark(toObject);
    });
}

void GlobalCCCopyTask::HelpRemainingLocalWorkloads(uint32_t threadIndex)
{
    // Stagger the first help target to avoid every idle worker contending on
    // the same heap. A full pass with no region means every region has already
    // been claimed; claimed regions are completed before runningTaskCount_
    // reaches zero.
    size_t workloadCount = localWorkloads_.size();
    if (workloadCount == 0) {
        return;
    }
    size_t index = static_cast<size_t>(threadIndex) % workloadCount;
    for (size_t offset = 0; offset < workloadCount; offset++) {
        ProcessLocalWorkload(*localWorkloads_[index], threadIndex);
        if (++index == workloadCount) {
            index = 0;
        }
    }
}

bool GlobalCCUpdateTask::Run(uint32_t threadIndex)
{
    ProcessLocalWorkloads(threadIndex);
    ProcessSharedWorkload(sharedWorkload_.GetNextRegion());
    NotifyFinished();
    return true;
}

bool GlobalCCUpdateTask::RunSharedWorkload(uint32_t threadIndex)
{
    ASSERT(threadIndex == DAEMON_THREAD_INDEX);
    ProcessSharedWorkload(sharedWorkload_.GetNextRegion());
    // Update uses no heap-local allocator, so the daemon can also process local heaps.
    ProcessLocalWorkloads(threadIndex);
    NotifyFinished();
    return true;
}

void GlobalCCUpdateTask::ProcessLocalWorkloads(uint32_t threadIndex)
{
    GlobalCCRegionWorkload *workload = GetNextLocalWorkload();
    while (workload != nullptr) {
        ProcessLocalWorkload(*workload);
        workload = GetNextLocalWorkload();
    }
    HelpRemainingLocalWorkloads(threadIndex);
}

GlobalCCRegionWorkload *GlobalCCUpdateTask::GetNextLocalWorkload()
{
    size_t idx = localHeapIter_.fetch_add(1U, std::memory_order_relaxed);
    return idx < localWorkloads_.size() ? localWorkloads_[idx].get() : nullptr;
}

void GlobalCCUpdateTask::ProcessLocalWorkload(GlobalCCRegionWorkload &workload)
{
    Region *region = workload.GetNextRegion();
    GlobalCCLocalUpdateVisitor localVisitor;
    while (region != nullptr) {
        ASSERT(!region->InSharedHeap());
        region->IterateAllMarkedBits([&localVisitor](void *mem) {
            TaggedObject *object = reinterpret_cast<TaggedObject *>(mem);
            JSHClass *jsHclass = object->SynchronizedGetClass();
            ObjectXRay::VisitObjectBody<VisitType::OLD_GC_VISIT>(object, jsHclass, localVisitor);
        });
        region = workload.GetNextRegion();
    }
}

void GlobalCCUpdateTask::ProcessSharedWorkload(Region *region)
{
    SharedCCUpdateVisitor sharedVisitor;
    while (region != nullptr) {
        ASSERT(region->InSharedHeap());
        region->IterateAllMarkedBits([&sharedVisitor](void *mem) {
            TaggedObject *object = reinterpret_cast<TaggedObject *>(mem);
            JSHClass *jsHclass = object->SynchronizedGetClass();
            ObjectXRay::VisitObjectBody<VisitType::OLD_GC_VISIT>(object, jsHclass, sharedVisitor);
        });
        region = sharedWorkload_.GetNextRegion();
    }
}

void GlobalCCUpdateTask::HelpRemainingLocalWorkloads(uint32_t threadIndex)
{
    size_t workloadCount = localWorkloads_.size();
    if (workloadCount == 0) {
        return;
    }
    size_t index = static_cast<size_t>(threadIndex) % workloadCount;
    for (size_t offset = 0; offset < workloadCount; offset++) {
        ProcessLocalWorkload(*localWorkloads_[index]);
        if (++index == workloadCount) {
            index = 0;
        }
    }
}

void GlobalCCUpdateTask::NotifyFinished()
{
    if (runningTaskCount_.fetch_sub(1, std::memory_order_seq_cst) == 1) {
        cc_->NotifyTasksFinished();
    }
}

namespace {
void GlobalCCLocalUpdateVisitor::VisitObjectRangeImpl(BaseObject *root, ObjectSlot start, ObjectSlot end,
    VisitObjectArea area)
{
    auto rootObject = TaggedObject::Cast(root);
    Region *rootRegion = Region::ObjectAddressToRange(rootObject);

    if (UNLIKELY(area == VisitObjectArea::IN_OBJECT)) {
        HandleInObjectArea(rootObject, start, end);
        return;
    }

    for (ObjectSlot slot = start; slot < end; slot++) {
        HandleSlot(slot, rootRegion);
    }
}

void GlobalCCLocalUpdateVisitor::VisitCompressedObjectRangeImpl(BaseObject *root,
    CompressedObjectSlot start, CompressedObjectSlot end)
{
    Region *rootRegion = Region::ObjectAddressToRange(TaggedObject::Cast(root));
    for (CompressedObjectSlot slot = start; slot < end; slot++) {
        HandleSlot(slot, rootRegion);
    }
}

template<ReferenceType refType>
void GlobalCCLocalUpdateVisitor::HandleSlot(ObjectSlotBase<refType> slot, Region *rootRegion)
{
    TaggedValueType<refType> value = slot.GetTaggedValue();
    if (!value.IsHeapObject()) {
        return;
    }

    Region *objectRegion = Region::ObjectAddressToRange(value.GetRawHeapObject());
    if (objectRegion->IsFromRegion()) {
        TaggedObject *object = value.GetHeapObject();
        MarkWord markWord(object, RELAXED_LOAD);
        ASSERT(markWord.IsForwardingAddress());
        TaggedObject *target = markWord.ToForwardingAddress();
        TaggedObject *rawObject = value.GetRawHeapObject();
        if constexpr (ReferenceIsCompressed<refType>) {
            ASSERT(!value.IsWeakForHeapObject());
            slot.CASUpdate(rawObject, target);
        } else {
            if (value.IsWeakForHeapObject()) {
                slot.CASUpdateWeak(rawObject, target);
            } else {
                slot.CASUpdate(rawObject, target);
            }
        }
    }

    if (objectRegion->InSharedSweepableSpace()) {
        rootRegion->InsertSweepingLocalToShareRSetForCC<refType>(slot.SlotAddress());
    }
}

void GlobalCCLocalUpdateVisitor::HandleInObjectArea(TaggedObject *rootObject, ObjectSlot startSlot,
                                                    ObjectSlot endSlot)
{
    Region *rootRegion = Region::ObjectAddressToRange(rootObject);
    JSHClass *hclass = rootObject->SynchronizedGetClass();
    ASSERT(!hclass->IsAllTaggedProp());
    int index = 0;
    LayoutInfo *layout = LayoutInfo::UncheckCast(
        hclass->GetLayout<RBMode::FAST_NO_RB>(THREAD_ARG_PLACEHOLDER).GetTaggedObject());
    ObjectSlot realEnd(reinterpret_cast<uintptr_t>(startSlot.SlotAddress()));
    realEnd += layout->GetPropertiesCapacity();
    ObjectSlot actualEnd = endSlot > realEnd ? realEnd : endSlot;

    for (ObjectSlot slot = startSlot; slot < actualEnd; slot++) {
        PropertyAttributes attr = layout->GetAttr<RBMode::FAST_NO_RB>(THREAD_ARG_PLACEHOLDER, index++);
        if (attr.IsTaggedRep()) {
            HandleSlot(slot, rootRegion);
        }
    }
}
}  // namespace

void GlobalCC::AppendEvacuatedRegionsToUpdateWorkloads()
{
    // TO regions prepare their rebuild sets at creation. Keep their write-barrier
    // records: the update phase skips fresh, unmarked allocations.
    GlobalCCRegionWorkload &sharedWorkload = *sharedUpdateWorkload_;
    auto collectSharedToSpace = [this, &sharedWorkload](Region *region) {
        sharedWorkload.AddRegion(region);
        updateRegionCount_++;
    };
    uint32_t totalTaskpoolThreads = common::Taskpool::GetCurrentTaskpool()->GetTotalThreadNum();
    for (uint32_t i = 0; i <= totalTaskpoolThreads; i++) {
        GetTlabAllocator(i)->GetSharedLocalSpace()->EnumerateToRegionsSync(collectSharedToSpace);
    }
    {
        LockHolder lock(evacuatorsMutex_);
        for (auto *evacuator : evacuators_) {
            evacuator->GetTlabAllocator()->GetSharedLocalSpace()->EnumerateToRegionsSync(collectSharedToSpace);
        }
    }
    // These workloads are the participant snapshot built in the remark STW.
    // Region enumeration snapshots the current TO-space tails; regions
    // appended later can only contain losing read-barrier copies.
    for (const auto &workload : localUpdateWorkloads_) {
        GlobalCCRegionWorkload &localWorkload = *workload;
        Heap *heap = localWorkload.localHeap;
        auto collectLocalTo = [this, &localWorkload](Region *region) {
            localWorkload.AddRegion(region);
            updateRegionCount_++;
        };
        heap->GetToSpace()->EnumerateToRegionsSync(collectLocalTo);
        heap->GetModuleSpace()->EnumerateToRegionsSync(collectLocalTo);
        if constexpr (!G_USE_CMS_GC) {
            heap->GetNativePointerSpace()->EnumerateToRegionsSync(collectLocalTo);
        }
    }
}

void GlobalCC::UpdateReferences()
{
    ECMA_BYTRACE_NAME(HITRACE_LEVEL_COMMERCIAL, HITRACE_TAG_ARK, "GlobalCC::UpdateReferences", "");
    TRACE_GC(GCStats::Scope::ScopeId::UpdateReference, sHeap_->GetEcmaGCStats());
    AppendEvacuatedRegionsToUpdateWorkloads();

    ASSERT(runningTaskCount_ == 0);
    std::sort(localUpdateWorkloads_.begin(), localUpdateWorkloads_.end(),
        [](const auto &lhs, const auto &rhs) { return lhs->RegionCount() > rhs->RegionCount(); });
    localUpdateHeapIter_ = 0;
    GlobalCCRegionWorkload &sharedWorkload = *sharedUpdateWorkload_;

    int workerCount = CalculateWorkerThreadNum(updateRegionCount_);
    runningTaskCount_ = workerCount + 1;
    for (int i = 0; i < workerCount; i++) {
        common::Taskpool::GetCurrentTaskpool()->PostTask(
            std::make_unique<GlobalCCUpdateTask>(localUpdateWorkloads_, localUpdateHeapIter_, runningTaskCount_,
                                                 sharedWorkload, this));
    }
    GlobalCCUpdateTask daemonTask(localUpdateWorkloads_, localUpdateHeapIter_, runningTaskCount_,
                                  sharedWorkload, this);
    daemonTask.RunSharedWorkload(DAEMON_THREAD_INDEX);
    WaitWorkloadFinished();
    ASSERT(localUpdateHeapIter_.load(std::memory_order_relaxed) >= localUpdateWorkloads_.size());
    ASSERT(sharedWorkload.AllRegionsClaimed());
    for (const auto &workload : localUpdateWorkloads_) {
        ASSERT(workload->AllRegionsClaimed());
    }
    sharedUpdateWorkload_.reset();
}

namespace {
const WeakRootVisitor &GetGlobalCCStringTableWeakVisitor()
{
    static const WeakRootVisitor visitor = [](TaggedObject *header) -> TaggedObject* {
        Region *objectRegion = Region::ObjectAddressToRange(header);
        if (!objectRegion) {
            return nullptr;
        }
        if (objectRegion->IsFromRegion()) {
            MarkWord markWord(header, RELAXED_LOAD);
            if (markWord.IsForwardingAddress()) {
                return markWord.ToForwardingAddress();
            }
            return nullptr;
        }
        return (!objectRegion->InSharedSweepableSpace() || objectRegion->Test(header)) ? header : nullptr;
    };
    return visitor;
}
}  // namespace

void GlobalCC::PostStringTableSweepTask()
{
    auto stringTableCleaner = Runtime::GetInstance()->GetEcmaStringTable()->GetCleaner();
    if (concurrentProcessStringTable_) {
        stringTableCleaner->PostConcurrentSweepWeakRefTask(GetGlobalCCStringTableWeakVisitor());
    } else {
        stringTableCleaner->PostSweepWeakRefTask(GetGlobalCCStringTableWeakVisitor());
    }
}

void GlobalCC::WaitStringTableSweep()
{
    ECMA_BYTRACE_NAME(HITRACE_LEVEL_COMMERCIAL, HITRACE_TAG_ARK, "GlobalCC::WaitStringTableSweep", "");
    auto stringTableCleaner = Runtime::GetInstance()->GetEcmaStringTable()->GetCleaner();
    if (concurrentProcessStringTable_) {
        stringTableCleaner->WaitConcurrentSweepWeakRefTaskFinished();
    } else {
        stringTableCleaner->JoinAndWaitSweepWeakRefTask(GetGlobalCCStringTableWeakVisitor());
    }
}

void GlobalCC::FinishConcurrentStringTableSweep()
{
    auto stringTableCleaner = Runtime::GetInstance()->GetEcmaStringTable()->GetCleaner();
    if (concurrentProcessStringTable_) {
        stringTableCleaner->FinishConcurrentSweepInSTW(dThread_);
    }
}

void GlobalCC::FinalizeAndReclaim()
{
    ECMA_BYTRACE_NAME(HITRACE_LEVEL_COMMERCIAL, HITRACE_TAG_ARK, "GlobalCC::FinalizeAndReclaim", "");
    ThreadManagedScope runningScope(dThread_);

    // Local concurrent marking stays quiescent for the whole cycle: every
    // trigger and GC entry waits on the borrowed copying status, and the
    // ReMark STW drained the running tasks and discarded the marker state,
    // so no running-mark-task snapshot is needed here.
    std::vector<Heap *> localHeaps;
    localHeaps.reserve(Runtime::GetInstance()->GetThreadListSize());
    ClockScope stwTimer;
    {
        SuspendAllScope scope(dThread_);
        // Stop new threads from joining the copy phase before the existing
        // participants and their evacuators are finalized below.
        {
            LockHolder lock(waitMutex_);
            ccRunning_ = false;
        }
        FinalizeSharedHeapForReclaim();
        for (const auto &workload : localCopyWorkloads_) {
            // Bindings allocated during copy remain outside the collected survivors' baseline.
            workload->localHeap->nativeBindingSizeAfterLastGC_ =
                workload->liveNativeBindingSize.load(std::memory_order_relaxed);
        }
        MergeNonEvacuatedLocalToShareRSets();
        Runtime::GetInstance()->GCIterateThreadList([this, &localHeaps](JSThread *thread) {
            Heap *heap = thread->GetEcmaVM()->GetHeap();
            FinalizeLocalHeapAndThread(thread, heap);
            localHeaps.emplace_back(heap);
        });
        localUpdateWorkloads_.clear();
        Runtime::GetInstance()->IterateAllThreadList([](JSThread *thread) {
            if (thread->IsJitThread()) {
                thread->ClearCCSuspend();
            }
        });
        FinalizeGlobalCCEvacuators();
        // Publish the post-GC thresholds and statistics before mutators resume allocation.
        finalizeSuspendDuration_ = stwTimer.TotalSpentTime();
        FinishGCStatistics();
        // Publish every heap's callback request and restore GC state before
        // waking idle owner threads. Managed entry waits for this STW to end.
        Runtime::GetInstance()->GCIterateThreadList([](JSThread *thread) {
            if (thread != Runtime::GetInstance()->GetMainThread() && thread->HasPostTaskToThreadCallback()) {
                thread->PostRuntimeThreadTask();
            }
        });
    }

    {
        LockHolder lock(waitMutex_);
        waitCV_.SignalAll();
    }
    common::Taskpool::GetCurrentTaskpool()->PostTask(std::make_unique<ReclaimTask>(
        dThread_->GetThreadId(), sHeap_, std::move(localHeaps)));
    if (UNLIKELY(sHeap_->ShouldVerifyHeap())) {
        VerifyHeapsAfterGlobalCC();
    }
}

void GlobalCC::MergeNonEvacuatedLocalToShareRSets()
{
    for (const auto &workload : localUpdateWorkloads_) {
        for (Region *region : workload->regions) {
            // TO regions merge their RSets when their owning spaces are merged below.
            if (!region->IsToRegion()) {
                region->MergeLocalToShareRSetForCC();
            }
        }
    }
}

void GlobalCC::FinalizeSharedHeapForReclaim()
{
    ECMA_BYTRACE_NAME(HITRACE_LEVEL_COMMERCIAL, HITRACE_TAG_ARK,
                      "GlobalCC::FinalizeSharedHeapForReclaim", "");
    TRACE_GC(GCStats::Scope::ScopeId::Finalize, sHeap_->GetEcmaGCStats());
    FinalizeSharedEvacuationResources();
    FinishConcurrentStringTableSweep();
    sHeap_->GetSweeper()->TryFillSweptRegion();
    MovedObjectScanner::ScanSharedCC(sHeap_);
    sHeap_->PrepareReclaimForGlobalCC();
}

void GlobalCC::FinalizeSharedEvacuationResources()
{
    sHeap_->GetOldSpace()->ResetPreservedSize();
    uint32_t totalTaskpoolThreads = common::Taskpool::GetCurrentTaskpool()->GetTotalThreadNum();
    for (uint32_t i = 0; i <= totalTaskpoolThreads; i++) {
        GetTlabAllocator(i)->Finalize();
    }
    {
        // The mutator-owned evacuator TLABs must be finalized before the
        // reclaim task merges the shared TO regions.
        LockHolder lock(evacuatorsMutex_);
        for (auto *evacuator : evacuators_) {
            evacuator->GetTlabAllocator()->Finalize();
        }
    }
}

void GlobalCC::FinalizeLocalHeapAndThread(JSThread *thread, Heap *heap)
{
    ECMA_BYTRACE_NAME(HITRACE_LEVEL_COMMERCIAL, HITRACE_TAG_ARK,
                      "GlobalCC::FinalizeLocalHeapAndThread", "");
    FinalizeLocalHeap(heap);
    RestoreLocalThreadAfterGlobalCC(thread, heap);
    thread->RequestGlobalCCCallback();
}

void GlobalCC::FinalizeLocalHeap(Heap *heap)
{
    heap->GetOldSpace()->ResetPreservedSize();
    heap->GetCCGC()->GetTlabAllocator(MAIN_THREAD_INDEX)->Finalize();
    heap->GetSweeper()->EnsureAllTaskFinished();
    MovedObjectScanner::ScanLocalGlobalCC(heap);
    heap->GetOldSpace()->MergeToSpace(heap->GetToSpace());
    heap->GetModuleSpace()->MergeToGCRegions();
    if constexpr (!G_USE_CMS_GC) {
        heap->GetNativePointerSpace()->MergeToGCRegions();
    }
    heap->PrepareReclaimForGlobalCC();
    heap->UpdateHeapStatsAfterGC(TriggerGCType::GLOBAL_CC);
    heap->SetFullMarkRequestedState(false);
    if (heap->IsNearGCInSensitive()) {
        heap->SetNearGCInSensitive(false);
    }
}

void GlobalCC::ReclaimLocalHeapsThenShared(SharedHeap *sHeap, const std::vector<Heap *> &localHeaps)
{
    for (Heap *heap : localHeaps) {
        heap->ReclaimRegions(TriggerGCType::LOCAL_CC, false);
    }
    sHeap->ReclaimRegions(TriggerGCType::GLOBAL_CC, true);
}

void GlobalCC::RestoreLocalThreadAfterGlobalCC(JSThread *thread, Heap *heap)
{
    thread->SetCCStatus(CCStatus::READY);
    // Restore the mark states set by GlobalCCMarker::InitializeMarking so
    // local concurrent marking can trigger again between rounds.
    thread->SetMarkStatus(MarkStatus::READY_TO_MARK);
    heap->SetMarkType(MarkType::MARK_YOUNG);

    thread->InstallSharedCCEvacuator(nullptr);
    thread->SetSharedCCStatus(SharedCCStatus::IDLE);
    thread->ReleaseReadBarrier(ReadBarrierOwner::SHARED_CC);
    thread->TryRestoreNormalStubs();
    thread->ClearCCSuspend();
#ifndef EXCLUDE_PGO
    std::shared_ptr<PGOProfiler> pgoProfiler = thread->GetEcmaVM()->GetPGOProfiler();
    if (pgoProfiler != nullptr && !thread->IsConcurrentCopying()) {
        pgoProfiler->ResumeByGC();
    }
#endif
#if defined(ECMASCRIPT_SUPPORT_CPUPROFILER)
    thread->SetGcState(false);
#endif
}

void GlobalCC::VerifyHeapsAfterGlobalCC()
{
    ECMA_BYTRACE_NAME(HITRACE_LEVEL_COMMERCIAL, HITRACE_TAG_ARK,
                      "GlobalCC::VerifyHeapsAfterGlobalCC", "");
    // Run the ordinary local and shared heap verification after the final STW restores GC state.
    SuspendAllScope scope(dThread_);
    // Keep the waits inside this STW. Otherwise a mutator could finish a
    // local GC and post a new clear task after the wait but before the
    // verifier starts walking regions.
    sHeap_->WaitClearTaskFinished();
    Runtime::GetInstance()->GCIterateThreadList([](JSThread *thread) {
        thread->GetEcmaVM()->GetHeap()->WaitClearTaskFinished();
    });
    Runtime::GetInstance()->GCIterateThreadList([](JSThread *thread) {
        auto *heap = thread->GetEcmaVM()->GetHeap();
        heap->PrepareForIteration();
        heap->FillBumpPointerForTlab();
        Verification(heap, VerifyKind::VERIFY_POST_GC).VerifyAll();
    });
    SharedHeapVerification(sHeap_, VerifyKind::VERIFY_POST_SHARED_GC).VerifyAll();
}

void GlobalCC::FinishGCStatistics()
{
    // The STW duration sums the ReMarkAndPrepare and finalization windows.
    sHeap_->GetEcmaGCStats()->RecordScopeDuration(
        GCStats::Scope::ScopeId::TotalGC, totalGcTimer_.TotalSpentTime());
    sHeap_->GetEcmaGCStats()->RecordScopeDuration(
        GCStats::Scope::ScopeId::SuspendAll, remarkStwDuration_ + finalizeSuspendDuration_);
    sHeap_->FinishGCStats(TriggerGCType::GLOBAL_CC);
    LOG_GC(INFO) << "GlobalCC: finished in " << totalGcTimer_.TotalSpentTime()
                 << "ms, stw " << (remarkStwDuration_ + finalizeSuspendDuration_)
                 << "ms (prepare " << remarkStwDuration_
                 << "ms, finalize " << finalizeSuspendDuration_ << "ms)";
}

}  // namespace panda::ecmascript
