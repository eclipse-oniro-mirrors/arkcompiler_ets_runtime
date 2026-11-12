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

#include "ecmascript/mem/local_cmc/concurrent_copy_gc.h"

#include "common_components/taskpool/taskpool.h"

#include "ecmascript/jit/jit.h"
#include "ecmascript/js_weak_container.h"
#include "ecmascript/linked_hash_table.h"
#include "ecmascript/mem/concurrent_marker.h"
#include "ecmascript/mem/local_cmc/cc_evacuator-inl.h"
#include "ecmascript/mem/local_cmc/cc_gc_visitor-inl.h"
#include "ecmascript/mem/verification.h"
#include "ecmascript/mem/work_manager-inl.h"
#include "ecmascript/runtime_call_id.h"
#include "ecmascript/runtime_lock.h"

namespace panda::ecmascript {
namespace {
TaggedObject *UpdateWeakRoot(CCEvacuator *evacuator, TaggedObject *object)
{
    Region *objectRegion = Region::ObjectAddressToRange(object);
    ASSERT(objectRegion != nullptr);
    if (objectRegion->InSharedHeap()) {
        return object;
    }
    if (!objectRegion->Test(object)) {
        return nullptr;
    }
    if (objectRegion->IsFromRegion()) {
        MarkWord markWord(object, RELAXED_LOAD);
        if (markWord.IsForwardingAddress()) {
            return markWord.ToForwardingAddress();
        }
        return evacuator->Copy(object, markWord);
    }
    return object;
}

}  // namespace

WeakRootVisitor ConcurrentCopyGC::CreateUpdateWeakVisitor(CCEvacuator *evacuator)
{
    return [evacuator](TaggedObject *object) -> TaggedObject* {
        return UpdateWeakRoot(evacuator, object);
    };
}

ConcurrentCopyGC::ConcurrentCopyGC(Heap *heap) : heap_(heap), thread_(heap->GetJSThread())
{
    for (uint32_t i = 0; i < common::MAX_TASKPOOL_THREAD_NUM + 1; i++) {
#if USE_CMS_GC
        tlabAllocators_.at(i).Setup(heap_->GetSlotSpace());
#else
        tlabAllocators_.at(i).Setup(heap_);
#endif
    }
    auto mainEvacuator = new CCEvacuator(heap_, GetTlabAllocator(MAIN_THREAD_INDEX));
    thread_->InstallLocalCCEvacuator(mainEvacuator);
}

void ConcurrentCopyGC::StartLocalCCCopy()
{
    ASSERT(thread_->IsInRunningState());
    SharedHeap *sHeap = SharedHeap::GetInstance();
    {
        RuntimeLockHolder lock(thread_, sHeap->localCCDrainMutex_);
        if (!sHeap->localCCCopyStartBlocked_) {
            sHeap->activeLocalCCCount_++;
            return;
        }
    }
    // SharedGC may need this thread to reach a safepoint, so never wait while remaining RUNNING.
    ThreadNativeScope nativeScope(thread_);
    LockHolder lock(sHeap->localCCDrainMutex_);
    while (sHeap->localCCCopyStartBlocked_) {
        sHeap->localCCDrainCV_.Wait(&sHeap->localCCDrainMutex_);
    }
    sHeap->activeLocalCCCount_++;
}

void ConcurrentCopyGC::RunPhase()
{
    ASSERT("ConcurrentCopyGC should be disabled" && !g_isEnableCMCGC);
    GCStats *gcStats = heap_->GetEcmaVM()->GetEcmaGCStats();
    ECMA_BYTRACE_NAME(HITRACE_LEVEL_COMMERCIAL, HITRACE_TAG_ARK,
        ("ConcurrentCopyGC::RunPhase;GCReason " + std::to_string(static_cast<int>(gcStats->GetGCReason()))
        + ";MarkReason" + std::to_string(static_cast<int>(gcStats->GetMarkReason()))
        + ";Sensitive" + std::to_string(static_cast<int>(heap_->GetSensitiveStatus()))
        + ";IsInBackground" + std::to_string(Runtime::GetInstance()->IsInBackground())
        + ";Startup" + std::to_string(static_cast<int>(heap_->GetStartupStatus()))
        + ";ConMark" + std::to_string(static_cast<int>(heap_->GetJSThread()->GetMarkStatus()))
#if USE_CMS_GC  // fixme: refactor?
        + ";Slot" + std::to_string(heap_->GetSlotSpace()->GetCommittedSize())
#else
        + ";Young" + std::to_string(heap_->GetNewSpace()->GetCommittedSize())
        + ";Old" + std::to_string(heap_->GetOldSpace()->GetCommittedSize())
#endif
        + ";TotalCommit" + std::to_string(heap_->GetCommittedSize())
        + ";NativeBindingSize" + std::to_string(heap_->GetNativeBindingSize())
        + ";NativeLimitSize" + std::to_string(heap_->GetGlobalSpaceNativeLimit())
        + ";ObjSizeBeforeSensitive" + std::to_string(heap_->GetRecordHeapObjectSizeBeforeSensitive())).c_str(), "");
    TRACE_GC(GCStats::Scope::ScopeId::TotalGC, gcStats);
    MEM_ALLOCATE_AND_GC_TRACE(heap_->GetEcmaVM(), PartialGC_RunPhases);
#ifndef EXCLUDE_JIT
    Jit::JitGCLockHolder lock(thread_);
#endif
    PreGC();
    ProcessWeakReference();
    Sweep();
    UpdateRoot();
#if !ENABLE_LOCALCC_OPTIMIZATION
    ConcurrentCopy();
#endif  // !ENABLE_LOCALCC_OPTIMIZATION
}

void ConcurrentCopyGC::PreGC()
{
#ifndef EXCLUDE_PGO
    std::shared_ptr<pgo::PGOProfiler> pgoProfiler =  thread_->GetEcmaVM()->GetPGOProfiler();
    if (pgoProfiler != nullptr) {
        pgoProfiler->SuspendByGC();
    }
#endif
    heap_->SetGCState(true);
    {
        TRACE_GC(GCStats::Scope::ScopeId::ProcessSharedGCRSetWorkList, heap_->GetEcmaVM()->GetEcmaGCStats());
        heap_->ProcessSharedGCRSetWorkList();
    }
    if (UNLIKELY(heap_->ShouldVerifyHeap())) {
        Verification::VerifyMark(heap_);
    }
}

void ConcurrentCopyGC::ProcessWeakReference()
{
    TRACE_GC(GCStats::Scope::ScopeId::UpdateWeekRef, heap_->GetEcmaVM()->GetEcmaGCStats());
    ASSERT(heap_->IsParallelGCEnabled());
#if ENABLE_LOCALCC_OPTIMIZATION
    uint32_t totalThreadCount = common::Taskpool::GetCurrentTaskpool()->GetTotalThreadNum() + 1; // 1 : mainthread
    ProcessRecordWeakReferenceInParallel(totalThreadCount);
#else
    uint32_t totalThreadCount =
        totalThreadCount = common::Taskpool::GetCurrentTaskpool()->GetTotalThreadNum() + 1; // 1 : mainthread
    for (uint32_t i = 0; i < totalThreadCount; i++) {
        UpdateRecordWeakReference(i);
    }
#endif  // ENABLE_LOCALCC_OPTIMIZATION
    for (uint32_t i = 0; i < totalThreadCount; i++) {
        UpdateRecordWeakLinkedHashMap(i);
    }
    if (UNLIKELY(heap_->ShouldVerifyHeap())) {
        Verification(heap_, VerifyKind::VERIFY_WEAK_REF).VerifyAll();
    }
}

void ConcurrentCopyGC::ProcessRecordWeakReferenceInParallel(uint32_t totalThreadCount)
{
    if (totalThreadCount <= 1) {
        UpdateRecordWeakReference(MAIN_THREAD_INDEX);
        return;
    }

    uint32_t workerCount = totalThreadCount - 1;
    common::TaskPackMonitor monitor(static_cast<int>(workerCount), static_cast<int>(workerCount));
    for (uint32_t threadIndex = 1; threadIndex < totalThreadCount; threadIndex++) {
        common::Taskpool::GetCurrentTaskpool()->PostTask(std::make_unique<ConcurrentCopyUpdateWeakReferenceTask>(
            thread_->GetThreadId(), this, threadIndex, &monitor));
    }

    UpdateRecordWeakReference(MAIN_THREAD_INDEX);
    monitor.WaitAllFinished();
}

void ConcurrentCopyGC::Sweep()
{
    ECMA_BYTRACE_NAME(HITRACE_LEVEL_COMMERCIAL, HITRACE_TAG_ARK, "ConcurrentCopyGC::Sweep", "");
    TRACE_GC(GCStats::Scope::ScopeId::Sweep, heap_->GetEcmaVM()->GetEcmaGCStats());
    heap_->GetSweeper()->Sweep(TriggerGCType::LOCAL_CC);
    heap_->PrepareRecordNonmovableRegions();
    heap_->GetConcurrentMarker()->Reset(false);
}

#if ENABLE_LOCALCC_OPTIMIZATION
void ConcurrentCopyGC::UpdateRoot()
{
    ECMA_BYTRACE_NAME(HITRACE_LEVEL_COMMERCIAL, HITRACE_TAG_ARK, "ConcurrentCopyGC::UpdateRoot", "");
    TRACE_GC(GCStats::Scope::ScopeId::UpdateRoot, heap_->GetEcmaVM()->GetEcmaGCStats());
    InitializeCopyPhase();
    CCEvacuator *mainEvacuator = thread_->GetLocalCCEvacuator();
    WeakRootVisitor weakVisitor = CreateUpdateWeakVisitor(mainEvacuator);
    bool enableParallel = heap_->IsParallelGCEnabled() &&
        common::Taskpool::GetCurrentTaskpool()->GetTotalThreadNum() > 0;
    if (enableParallel) {
        heap_->GetEcmaVM()->ProcessReferencesForLocalCC(weakVisitor);
    } else {
        heap_->GetEcmaVM()->ProcessReferences(weakVisitor);
    }
    heap_->GetEcmaVM()->ProcessPendingRemovalLocalModules(weakVisitor);
    if (!enableParallel) {
        heap_->GetSweeper()->PostTask(TriggerGCType::LOCAL_CC);
    }
    heap_->GetEcmaVM()->GetJSThread()->IterateWeakRoots(weakVisitor);
    heap_->GetEcmaVM()->GetJSThread()->IterateWeakEcmaGlobalStorage(weakVisitor);
    heap_->GetEcmaVM()->ProcessSnapShotEnv(weakVisitor);
    heap_->GetEcmaVM()->GetJSThread()->UpdateJitCodeMapReference(weakVisitor);
    heap_->GetEcmaVM()->IterateWeakGlobalEnvList(weakVisitor);
    CCUpdateRootVisitor updateRoot(mainEvacuator);
    ObjectXRay::VisitSTWVMRoots(heap_->GetEcmaVM(), updateRoot);
    heap_->GetEcmaVM()->IterateGlobalEnvField(updateRoot);
#if ECMASCRIPT_ENABLE_ARK_STEED
    auto *refs = heap_->GetEmbeddedCodeRefSet();
    if (!refs->IsEmpty()) {
        refs->VisitMarkedLocalTargets(updateRoot);
        if (!refs->UpdateMarkedLocalTargets()) {
            LOG_GC(FATAL) << "Failed to update ArkSteed embedded heap references during LocalCC";
        }
    }
#endif
    heap_->GetEcmaVM()->BeginConcurrentRootScan();
    if (enableParallel) {
        PostUpdateConcurrentRootsTask();
        return;
    }
    UpdateConcurrentRoots(MAIN_THREAD_INDEX);
    ConcurrentCopy();
}
#else
void ConcurrentCopyGC::UpdateRoot()
{
    ECMA_BYTRACE_NAME(HITRACE_LEVEL_COMMERCIAL, HITRACE_TAG_ARK, "ConcurrentCopyGC::UpdateRoot", "");
    TRACE_GC(GCStats::Scope::ScopeId::UpdateRoot, heap_->GetEcmaVM()->GetEcmaGCStats());
    InitializeCopyPhase();
    CCEvacuator *mainEvacuator = thread_->GetLocalCCEvacuator();
    WeakRootVisitor weakVisitor = [mainEvacuator](TaggedObject *object) -> TaggedObject* {
        Region *objectRegion = Region::ObjectAddressToRange(object);
        ASSERT(objectRegion != nullptr);
        if (objectRegion->InSharedHeap()) {
            return object;
        } else if (!objectRegion->Test(object)) {
            return nullptr;
        } else if (objectRegion->IsFromRegion()) {
            MarkWord markWord(object, RELAXED_LOAD);
            if (markWord.IsForwardingAddress()) {
                return markWord.ToForwardingAddress();
            } else {
                TaggedObject *ref = mainEvacuator->Copy(object, markWord);
                return ref;
            }
        } else {
            return object;
        }
    };
    heap_->GetEcmaVM()->ProcessReferences(weakVisitor);
    heap_->GetEcmaVM()->ProcessPendingRemovalLocalModules(weakVisitor);
    heap_->GetSweeper()->PostTask(TriggerGCType::LOCAL_CC);
    heap_->GetEcmaVM()->GetJSThread()->IterateWeakRoots(weakVisitor);
    heap_->GetEcmaVM()->GetJSThread()->IterateWeakEcmaGlobalStorage(weakVisitor);
    heap_->GetEcmaVM()->ProcessSnapShotEnv(weakVisitor);
    heap_->GetEcmaVM()->GetJSThread()->UpdateJitCodeMapReference(weakVisitor);
    heap_->GetEcmaVM()->IterateWeakGlobalEnvList(weakVisitor);
    CCUpdateRootVisitor updateRoot(mainEvacuator);
    if constexpr (G_USE_CMS_GC || !ENABLE_MODULE_SPACE) {
        ObjectXRay::VisitVMRoots(heap_->GetEcmaVM(), updateRoot);
    } else {
        ObjectXRay::VisitSTWVMRoots(heap_->GetEcmaVM(), updateRoot);
    }
    heap_->GetEcmaVM()->IterateGlobalEnvField(updateRoot);
#if ECMASCRIPT_ENABLE_ARK_STEED
    auto *refs = heap_->GetEmbeddedCodeRefSet();
    if (!refs->IsEmpty()) {
        refs->VisitMarkedLocalTargets(updateRoot);
        if (!refs->UpdateMarkedLocalTargets()) {
            LOG_GC(FATAL) << "Failed to update ArkSteed embedded heap references during LocalCC";
        }
    }
#endif
}
#endif

void ConcurrentCopyGC::PostUpdateConcurrentRootsTask()
{
    common::Taskpool::GetCurrentTaskpool()->PostTask(std::make_unique<ConcurrentCopyUpdateConcurrentRootsTask>(
        thread_->GetThreadId(), this));
}

void ConcurrentCopyGC::StartConcurrentNativePointerTasks(uint32_t coordinatorThreadIndex)
{
    ASSERT(coordinatorThreadIndex != MAIN_THREAD_INDEX);
    ASSERT(runningNativePointerTaskCount_ == 0);
    uint32_t taskCount = heap_->PrepareConcurrentNativePointerTasksForLocalCC();
    runningNativePointerTaskCount_.store(taskCount, std::memory_order_seq_cst);
    for (uint32_t taskIndex = 1; taskIndex < taskCount; taskIndex++) {
        common::Taskpool::GetCurrentTaskpool()->PostTask(std::make_unique<ConcurrentCopyNativePointerTask>(
            thread_->GetThreadId(), this, taskIndex));
    }
    ProcessConcurrentNativePointerTask(coordinatorThreadIndex, 0);
}

void ConcurrentCopyGC::ProcessConcurrentNativePointerTask(uint32_t threadIndex, uint32_t taskIndex)
{
    heap_->ProcessConcurrentNativePointerTaskForLocalCC(this, threadIndex, taskIndex);
    HandleConcurrentNativePointerTaskFinished(threadIndex);
}

void ConcurrentCopyGC::HandleConcurrentNativePointerTaskFinished(uint32_t threadIndex)
{
    if (runningNativePointerTaskCount_.fetch_sub(1, std::memory_order_seq_cst) != 1) {
        return;
    }
    heap_->MergeConcurrentNativePointerTaskResultsForLocalCC(threadIndex);
    heap_->GetSweeper()->PostTask(TriggerGCType::LOCAL_CC);
    StartConcurrentRootTasks(threadIndex);
}

void ConcurrentCopyGC::StartConcurrentRootTasks(uint32_t coordinatorThreadIndex)
{
    ASSERT(coordinatorThreadIndex != MAIN_THREAD_INDEX);
    ASSERT(runningConcurrentRootTaskCount_ == 0);
    ASSERT(localCCCallbackTask_ == nullptr);
    localCCCallbackTask_ = heap_->PrepareLocalCCCallback();
    concurrentRootTaskCount_ = heap_->GetEcmaVM()->PrepareConcurrentRootScan(CalculateConcurrentRootTaskCount());
    if (concurrentRootTaskCount_ == 0) {
        heap_->GetEcmaVM()->FinishConcurrentRootScan();
        ConcurrentCopy();
        PostLocalCCCallback();
        return;
    }

    runningConcurrentRootTaskCount_.store(concurrentRootTaskCount_, std::memory_order_seq_cst);
    for (uint32_t taskIndex = 1; taskIndex < concurrentRootTaskCount_; taskIndex++) {
        common::Taskpool::GetCurrentTaskpool()->PostTask(std::make_unique<ConcurrentCopyModuleRootTask>(
            thread_->GetThreadId(), this));
    }
    ProcessConcurrentRootTask(coordinatorThreadIndex);
}

void ConcurrentCopyGC::ProcessConcurrentRootTask(uint32_t threadIndex)
{
    ASSERT(threadIndex != MAIN_THREAD_INDEX);
    auto allocator = GetTlabAllocator(threadIndex);
    allocator->Initialize();
    {
        CCEvacuator evacuator(heap_, allocator);
        CCConcurrentUpdateRootVisitor visitor(&evacuator);
        auto rootVisitor = [&visitor](GCRoot &root) {
            visitor.VisitRoot(root);
        };
        ModuleRootVisitor moduleRootVisitor(rootVisitor);
        heap_->GetEcmaVM()->ProcessConcurrentRootScan(threadIndex, concurrentRootTaskCount_, moduleRootVisitor);
    }
    allocator->Finalize();
    HandleConcurrentRootTaskFinished(threadIndex);
}

void ConcurrentCopyGC::HandleConcurrentRootTaskFinished(uint32_t threadIndex)
{
    if (runningConcurrentRootTaskCount_.fetch_sub(1, std::memory_order_seq_cst) != 1) {
        return;
    }
    auto allocator = GetTlabAllocator(threadIndex);
    allocator->Initialize();
    {
        CCEvacuator evacuator(heap_, allocator);
        CCConcurrentUpdateRootVisitor visitor(&evacuator);
        auto rootVisitor = [&visitor](GCRoot &root) {
            visitor.VisitRoot(root);
        };
        ModuleRootVisitor moduleRootVisitor(rootVisitor);
        heap_->GetEcmaVM()->ProcessConcurrentSendableRootScan(threadIndex, moduleRootVisitor);
    }
    allocator->Finalize();
    heap_->GetEcmaVM()->FinishConcurrentRootScan();
    concurrentRootTaskCount_ = 0;
    ConcurrentCopy();
    PostLocalCCCallback();
}

void ConcurrentCopyGC::PostLocalCCCallback()
{
    if (localCCCallbackTask_ == nullptr) {
        return;
    }
    common::Taskpool::GetCurrentTaskpool()->PostTask(std::move(localCCCallbackTask_));
}

void ConcurrentCopyGC::UpdateConcurrentRoots(uint32_t coordinatorThreadIndex)
{
    ECMA_BYTRACE_NAME(HITRACE_LEVEL_COMMERCIAL, HITRACE_TAG_ARK,
                      "ConcurrentCopyGC::UpdateConcurrentRoots", "");

    ASSERT(coordinatorThreadIndex == MAIN_THREAD_INDEX);
    uint32_t taskCount = heap_->GetEcmaVM()->PrepareConcurrentRootScan(1);
    if (taskCount != 0) {
        CCConcurrentUpdateRootVisitor visitor(thread_->GetLocalCCEvacuator());
        auto rootVisitor = [&visitor](GCRoot &root) {
            visitor.VisitRoot(root);
        };
        ModuleRootVisitor moduleRootVisitor(rootVisitor);
        heap_->GetEcmaVM()->ProcessConcurrentRootScan(MAIN_THREAD_INDEX, taskCount, moduleRootVisitor);
        heap_->GetEcmaVM()->ProcessConcurrentSendableRootScan(MAIN_THREAD_INDEX, moduleRootVisitor);
    }
    heap_->GetEcmaVM()->FinishConcurrentRootScan();
}

void ConcurrentCopyGC::ConcurrentCopy()
{
    ASSERT(runningTaskCount_ == 0);
    runningTaskCount_ = CalculateCopyThreadNum();
    uint32_t totalTaskCount = runningTaskCount_;
    for (uint32_t i = 0; i < totalTaskCount; i++) {
        common::Taskpool::GetCurrentTaskpool()->PostTask(
            std::make_unique<ConcurrentCopyTask>(tasks_, taskIter_, runningTaskCount_, this));
    }
}

void ConcurrentCopyGC::InitializeCopyPhase()
{
    ECMA_BYTRACE_NAME(HITRACE_LEVEL_COMMERCIAL, HITRACE_TAG_ARK, "ConcurrentCopyGC::InitializeCopyPhase", "");
    heap_->EnumerateNonNewSpaceRegions([](Region *current) {
        current->ClearOldToNewRSet();
    });
    thread_->AcquireReadBarrier(ReadBarrierOwner::LOCAL_CC);
    thread_->SetCCStatus(CCStatus::COPYING);
    heap_->SelectFromSpace();
    ccUpdateFinished_ = false;
    GetTlabAllocator(MAIN_THREAD_INDEX)->Initialize();
}

uint32_t ConcurrentCopyGC::CalculateConcurrentRootTaskCount() const
{
    uint32_t taskpoolThreadCount = common::Taskpool::GetCurrentTaskpool()->GetTotalThreadNum();
    uint32_t maxWorkerCount = std::min(heap_->GetMaxEvacuateTaskCount(), taskpoolThreadCount);
    return std::max(1U, maxWorkerCount);
}

bool ConcurrentCopyUpdateConcurrentRootsTask::Run(uint32_t threadIndex)
{
    cc_->StartConcurrentNativePointerTasks(threadIndex);
    return true;
}

bool ConcurrentCopyNativePointerTask::Run(uint32_t threadIndex)
{
    cc_->ProcessConcurrentNativePointerTask(threadIndex, taskIndex_);
    return true;
}

bool ConcurrentCopyModuleRootTask::Run(uint32_t threadIndex)
{
    cc_->ProcessConcurrentRootTask(threadIndex);
    return true;
}

bool ConcurrentCopyTask::Run(uint32_t threadIndex)
{
    auto allocator = cc_->GetTlabAllocator(threadIndex);
    allocator->Initialize();
    CCEvacuator evacuator(cc_->GetHeap(), allocator);
    Region *region = GetNextTask();
    while (region) {
        ASSERT(region->IsFromRegion());
        region->IterateAllMarkedBits([&](void *mem) {
            TaggedObject *object = reinterpret_cast<TaggedObject *>(mem);
            MarkWord markWord(object, RELAXED_LOAD);
            if (!markWord.IsForwardingAddress()) {
                TaggedObject *toObject = evacuator.Copy(object, markWord);
                Region::ObjectAddressToRange(toObject)->AtomicMark(toObject);
            } else {
                TaggedObject *toObject = markWord.ToForwardingAddress();
                Region::ObjectAddressToRange(toObject)->AtomicMark(toObject);
            }
        });
        region = GetNextTask();
    }
    allocator->Finalize();
    if (runningTaskCount_.fetch_sub(1, std::memory_order_seq_cst) == 1) {
        cc_->RunUpdatePhase();
    }
    return true;
}

bool ConcurrentCopyUpdateWeakReferenceTask::Run([[maybe_unused]] uint32_t threadIndex)
{
    cc_->UpdateRecordWeakReference(weakReferenceThreadIndex_);
    monitor_->NotifyFinishOne();
    return true;
}

Region* ConcurrentCopyTask::GetNextTask()
{
    uint32_t idx = static_cast<uint32_t>(taskIter_.fetch_add(1U, std::memory_order_relaxed));
    if (idx < totalSize_) {
        return tasks_[idx];
    }
    return nullptr;
}

int ConcurrentCopyGC::CalculateCopyThreadNum()
{
    tasks_.clear();
    taskIter_ = 0;
    auto collectTask = [this](Region *region) {
        tasks_.emplace_back(region);
    };
    if constexpr (!G_USE_CMS_GC) {
        heap_->GetFromSpaceDuringEvacuation()->EnumerateRegions(collectTask);
        heap_->GetCompressSpace()->EnumerateRegions(collectTask);
    } else {
        heap_->GetSlotSpace()->EnumerateFromRegions(collectTask);
    }
    uint32_t count = tasks_.size();
    constexpr uint32_t regionPerThread = 8;
    uint32_t maxThreadNum = std::min(heap_->GetMaxEvacuateTaskCount(),
        common::Taskpool::GetCurrentTaskpool()->GetTotalThreadNum());
    return static_cast<int>(std::min(std::max(1U, count / regionPerThread), maxThreadNum));
}

void ConcurrentCopyGC::UpdateRecordWeakReference(uint32_t threadIndex)
{
    auto workManager = heap_->GetWorkManager();
    ProcessQueue *queue = workManager->GetWorkNodeHolder(threadIndex)->GetWeakReferenceQueue();
    while (true) {
        auto obj = queue->PopBack();
        if (UNLIKELY(obj == nullptr)) {
            break;
        }
        ObjectSlot slot(ToUintPtr(obj));
        JSTaggedValue value(slot.GetTaggedType());
        if (!value.IsWeak()) {
            continue;
        }
        auto header = value.GetTaggedWeakRef();
        Region *objectRegion = Region::ObjectAddressToRange(header);
        if (objectRegion->IsToRegion() || objectRegion->InSharedHeap()) {
            continue;
        } else if (!objectRegion->Test(header)) {
            slot.Clear();
        }
    }
}

void ConcurrentCopyGC::UpdateRecordWeakLinkedHashMap(uint32_t threadIndex)
{
    auto workManager = heap_->GetWorkManager();
    WeakLinkedHashMapProcessQueue *queue = workManager->GetWorkNodeHolder(threadIndex)->GetWeakLinkedHashMapQueue();
    JSThread *thread = heap_->GetJSThread();
    while (true) {
        TaggedObject *obj = queue->PopBack();
        if (UNLIKELY(obj == nullptr)) {
            break;
        }
        WeakLinkedHashMap *map = WeakLinkedHashMap::Cast(obj);
        ASSERT(map->VerifyLayout());

        int entries = map->NumberOfAllUsedElements();
        for (int i = 0; i < entries; ++i) {
            JSTaggedValue maybeKey = map->GetKey(thread, i);
            if (maybeKey.IsUndefined()) {
                // set to undefined by GC since key is dead, and the weak ref of key is updated.
                map->RemoveEntryFromGCThread(i);
            }
        }
    }
}

void ConcurrentCopyGC::RunUpdatePhase()
{
    ASSERT(thread_->NeedReadBarrier());
    CCUpdateVisitor<true> updatorWithBarrier(thread_);
    auto updateMarkedRegion = [](Region *region, auto &updator) {
        region->IterateAllMarkedBits([&](void *mem) {
            TaggedObject *object = reinterpret_cast<TaggedObject *>(mem);
            JSHClass *jsHClass = object->SynchronizedGetClass();
            ObjectXRay::VisitObjectBody<VisitType::OLD_GC_VISIT>(object, jsHClass, updator);
        });
    };
    auto toSpaceUpdator = [&](Region *region) {
        updateMarkedRegion(region, updatorWithBarrier);
        region->SetSwept();
    };
    if constexpr (!G_USE_CMS_GC) {
        heap_->GetToSpace()->EnumerateRegions(toSpaceUpdator);
    } else {
        heap_->GetSlotSpace()->EnumerateToRegions(toSpaceUpdator);
    }
    CCUpdateVisitor<false> updatorWithoutBarrier(thread_);
    heap_->EnumerateNonmovableRegionsWithRecord([&](Region *region) {
        updateMarkedRegion(region, updatorWithoutBarrier);
    });
    std::atomic_thread_fence(std::memory_order_seq_cst);
    {
        LockHolder waitLock(waitMutex_);
#if !ENABLE_LOCALCC_OPTIMIZATION
        heap_->SetGCState(false);
#endif  // !ENABLE_LOCALCC_OPTIMIZATION
        thread_->SetCCStatus(CCStatus::COPY_FINISHED);
        thread_->SetCheckSafePointStatus();
        ccUpdateFinished_ = true;
        waitCV_.SignalAll();
    }
    SharedHeap *sHeap = SharedHeap::GetInstance();
    {
        LockHolder drainLock(sHeap->localCCDrainMutex_);
        ASSERT(sHeap->activeLocalCCCount_ > 0);
        sHeap->activeLocalCCCount_--;
        if (sHeap->activeLocalCCCount_ == 0) {
            sHeap->localCCDrainCV_.SignalAll();
        }
    }
}

void ConcurrentCopyGC::WaitFinished()
{
    ECMA_BYTRACE_NAME(HITRACE_LEVEL_COMMERCIAL, HITRACE_TAG_ARK, "ConcurrentCopyGC::WaitFinished", "");
    LockHolder lock(waitMutex_);
    while (!ccUpdateFinished_) {
        waitCV_.Wait(&waitMutex_);
    }
}

void ConcurrentCopyGC::HandleUpdateFinished()
{
    ECMA_BYTRACE_NAME(HITRACE_LEVEL_COMMERCIAL, HITRACE_TAG_ARK, "ConcurrentCopyGC::HandleUpdateFinished", "");
    ASSERT(thread_->NeedReadBarrier());
    Finish();
    PostGC();
#if ENABLE_LOCALCC_OPTIMIZATION
    heap_->SetGCState(false);
#endif  // ENABLE_LOCALCC_OPTIMIZATION
}

void ConcurrentCopyGC::Finish()
{
    ECMA_BYTRACE_NAME(HITRACE_LEVEL_COMMERCIAL, HITRACE_TAG_ARK, "ConcurrentCopyGC::Finish", "");
    GetTlabAllocator(MAIN_THREAD_INDEX)->Finalize();
    if constexpr (!G_USE_CMS_GC) {
        heap_->GetOldSpace()->MergeToSpace(heap_->GetToSpace());
    } else {
        heap_->GetSlotSpace()->MergeToRegions();
    }
#if ENABLE_LOCALCC_OPTIMIZATION
    heap_->FinalizeConcurrentNativePointerListForLocalCC();
    GCStats *gcStats = heap_->GetEcmaVM()->GetEcmaGCStats();
    gcStats->RefreshLongGCNativeStatsAfterConcurrentGC();
#endif  // ENABLE_LOCALCC_OPTIMIZATION
    if (UNLIKELY(heap_->ShouldVerifyHeap())) {
        Verification::VerifyCC(heap_);
    }
    thread_->ReleaseReadBarrier(ReadBarrierOwner::LOCAL_CC);
    thread_->SetCCStatus(CCStatus::READY);
    heap_->ResumeCC();
    heap_->SetFullMarkRequestedState(false);
    if (heap_->IsNearGCInSensitive()) {
        heap_->SetNearGCInSensitive(false);
    }
}

void ConcurrentCopyGC::PostGC()
{
    if (thread_->TryRestoreNormalStubs()) {
        ASSERT(!thread_->NeedReadBarrier());
#ifndef EXCLUDE_PGO
        std::shared_ptr<pgo::PGOProfiler> pgoProfiler = thread_->GetEcmaVM()->GetPGOProfiler();
        if (pgoProfiler != nullptr) {
            pgoProfiler->ResumeByGC();
        }
#endif
    }
}
}  // namespace panda::ecmascript
