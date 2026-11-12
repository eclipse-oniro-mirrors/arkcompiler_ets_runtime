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

#include "ecmascript/mem/global_cc/global_concurrent_marker.h"

#include "ecmascript/checkpoint/thread_state_transition.h"
#include "ecmascript/daemon/daemon_thread.h"
#include "ecmascript/ecma_vm.h"
#include "ecmascript/js_hclass-inl.h"
#include "ecmascript/linked_hash_table.h"
#include "ecmascript/mem/concurrent_marker.h"
#include "ecmascript/mem/global_cc/global_mark_visitor-inl.h"
#include "ecmascript/mem/heap.h"
#include "ecmascript/mem/object_xray.h"
#include "ecmascript/mem/region-inl.h"
#include "ecmascript/mem/verification.h"
#include "ecmascript/mem/work_manager-inl.h"
#include "ecmascript/runtime.h"

namespace panda::ecmascript {

GlobalCCMarker::GlobalCCMarker(GlobalCC *globalCC) : globalCC_(globalCC), sHeap_(globalCC->GetSharedHeap()) {}

// Sum of committed sizes over all local heaps; call only with all JSThreads suspended.
static size_t SumLocalHeapCommittedSize()
{
    size_t total = 0;
    Runtime::GetInstance()->GCIterateThreadList([&total](JSThread *thread) {
        total += thread->GetEcmaVM()->GetHeap()->GetCommittedSize();
    });
    return total;
}

class GlobalCCMarkSuspendCallback : public Closure {
public:
    explicit GlobalCCMarkSuspendCallback(GlobalCCMarker *marker) : marker_(marker) {}
    ~GlobalCCMarkSuspendCallback() override = default;

    NO_COPY_SEMANTIC(GlobalCCMarkSuspendCallback);
    NO_MOVE_SEMANTIC(GlobalCCMarkSuspendCallback);

    void Run([[maybe_unused]] JSThread *thread) override
    {
        ASSERT(thread->IsDaemonThread());
        CHECK_DAEMON_THREAD();
        SharedHeap *sHeap = SharedHeap::GetInstance();
        // All JSThreads are suspended for the whole callback; keep the daemon GC thread
        // and taskpool workers at STW QoS so scheduling latency does not extend the pause.
        sHeap->SetGCThreadQosPriority(common::PriorityMode::STW);
        TRACE_GC(GCStats::Scope::ScopeId::ConcurrentMark, sHeap->GetEcmaGCStats());
        ECMA_BYTRACE_NAME(HITRACE_LEVEL_COMMERCIAL, HITRACE_TAG_ARK, ("GlobalCCMarker::Mark;Sensitive"
            + std::to_string(static_cast<int>(sHeap->GetSensitiveStatus()))
            + ";IsInBackground" + std::to_string(Runtime::GetInstance()->IsInBackground())
            + ";Startup" + std::to_string(static_cast<int>(sHeap->GetStartupStatus()))
            + ";LocalTotCommit" + std::to_string(SumLocalHeapCommittedSize())
            + ";Old" + std::to_string(sHeap->GetOldSpace()->GetCommittedSize())
            + ";huge" + std::to_string(sHeap->GetHugeObjectSpace()->GetCommittedSize())
            + ";NonMov" + std::to_string(sHeap->GetNonMovableSpace()->GetCommittedSize())
            + ";TotCommit" + std::to_string(sHeap->GetCommittedSize())
            + ";NativeBindingSize" + std::to_string(sHeap->GetNativeSizeAfterLastGC())
            + ";NativeLimitGC" + std::to_string(sHeap->GetNativeSizeTriggerSharedGC())
            + ";NativeLimitCM" + std::to_string(sHeap->GetNativeSizeTriggerSharedCM())).c_str(), "");
        marker_->InitializeMarking();
        marker_->MarkGlobalRoots();
        sHeap->SetGCThreadQosPriority(common::PriorityMode::FOREGROUND);
    }

private:
    GlobalCCMarker *marker_ {nullptr};
};

class GlobalCCMarkFlipFunction : public Closure {
public:
    explicit GlobalCCMarkFlipFunction(GlobalCCMarker *marker) : marker_(marker) {}
    ~GlobalCCMarkFlipFunction() override = default;

    NO_COPY_SEMANTIC(GlobalCCMarkFlipFunction);
    NO_MOVE_SEMANTIC(GlobalCCMarkFlipFunction);

    void Run(JSThread *thread) override
    {
        marker_->MarkLocalRootsOnFlip(thread);
    }

private:
    GlobalCCMarker *marker_ {nullptr};
};

void GlobalCCMarker::Mark()
{
    RecursionScope recurScope(this);
    CHECK_DAEMON_THREAD();
    DaemonThread *dThread = DaemonThread::GetInstance();
    {
        SharedHeap::LocalCCDrainScope localCCDrainScope(dThread);
        ThreadManagedScope runningScope(dThread);
        GlobalCCMarkSuspendCallback suspendCallback(this);
        GlobalCCMarkFlipFunction flipFunction(this);
        Runtime::GetInstance()->FlipAllThreads(dThread, &suspendCallback, &flipFunction);
    }
    // Daemon thread do not need to post task to GC_Thread
    ASSERT(!dThread->IsInRunningState());
    DoMarking();
    HandleMarkingFinished();
}

void GlobalCCMarker::ReMark()
{
    CHECK_DAEMON_THREAD();
    ECMA_BYTRACE_NAME(HITRACE_LEVEL_COMMERCIAL, HITRACE_TAG_ARK, "GlobalCCMarker::ReMark", "");
    TRACE_GC(GCStats::Scope::ScopeId::ReMark, sHeap_->GetEcmaGCStats());
#ifndef NDEBUG
    ASSERT(DaemonThread::GetInstance()->HasLaunchedSuspendAll());
#endif
    auto *workManager = globalCC_->GetWorkManager();
    auto *holder = workManager->GetHolder(DAEMON_THREAD_INDEX);
    Runtime::GetInstance()->GCIterateThreadList([workManager](JSThread *thread) {
        MarkWorkNode *&buffer = thread->GetEcmaVM()->GetHeap()->GetGlobalCCMarkingLocalBuffer();
        workManager->FlushLocalMarkingBuffer(buffer);
    });

    MarkRoots();
    holder->FlushAll();
    ProcessMarkStack(DAEMON_THREAD_INDEX);
    sHeap_->WaitRunningMarkTaskFinished();
    MarkJitCodeMaps();
    sHeap_->SetParallelGCEnabled(false);
    MarkUntilFixPoint();
    sHeap_->SetParallelGCEnabled(true);

    if (sHeap_->ShouldVerifyHeap()) {
        SharedHeapVerification(sHeap_, VerifyKind::VERIFY_GLOBAL_CC_MARK).VerifyGlobalCCMark();
    }
}

bool GlobalCCMarker::IsAlive(TaggedObject *obj)
{
    Region *region = Region::ObjectAddressToRange(obj);
    return region->InSharedReadOnlySpace() || region->Test(obj);
}

bool GlobalCCMarker::HandleWeakAggregate(WeakAggregate weakAggregate, GlobalCCMarkObjectVisitor &visitor)
{
    ObjectSlot keySlot = weakAggregate.keySlot;
    ObjectSlot valueSlot = weakAggregate.valueSlot;
    JSTaggedValue key = keySlot.GetTaggedValue();
    JSTaggedValue value = valueSlot.GetTaggedValue();
    if (!key.IsHeapObject() || !value.IsHeapObject()) {
        return true;
    }
    if (IsAlive(key.GetTaggedWeakRef()) || IsAlive(value.GetHeapObject())) {
        visitor.HandleObject(value.GetHeapObject());
        return true;
    }
    return false;
}

void GlobalCCMarker::MarkUntilFixPoint()
{
    auto *workManager = globalCC_->GetWorkManager();
    auto *holder = workManager->GetHolder(DAEMON_THREAD_INDEX);
    GlobalCCMarkObjectVisitor visitor(holder);

    std::vector<WeakAggregate> pendingWeakAggregates;
    WeakAggregate weakAggregate;
    while (holder->PopPendingWeakAggregate(&weakAggregate)) {
        if (!HandleWeakAggregate(weakAggregate, visitor)) {
            pendingWeakAggregates.emplace_back(weakAggregate);
        }
    }

    while (true) {
        if (holder->IsMarkLocalEmpty() && workManager->IsGlobalMarkEmpty()) {
            return;
        }
        ProcessMarkStack(DAEMON_THREAD_INDEX);
        MarkJitCodeMaps();

        while (holder->PopFreshWeakAggregate(&weakAggregate)) {
            if (!HandleWeakAggregate(weakAggregate, visitor)) {
                pendingWeakAggregates.emplace_back(weakAggregate);
            }
        }
        auto it = pendingWeakAggregates.begin();
        while (it != pendingWeakAggregates.end()) {
            if (HandleWeakAggregate(*it, visitor)) {
                *it = pendingWeakAggregates.back();
                pendingWeakAggregates.pop_back();
            } else {
                ++it;
            }
        }
    }
}

void GlobalCCMarker::MarkJitCodeMaps()
{
    auto *workManager = globalCC_->GetWorkManager();
    auto *holder = workManager->GetHolder(DAEMON_THREAD_INDEX);
    GlobalCCMarkObjectVisitor objectVisitor(holder);
    JitCodeMapVisitor visitor = [&objectVisitor](std::map<JSTaggedType, JitCodeVector *> &jitCodeMaps) {
        for (auto it = jitCodeMaps.begin(); it != jitCodeMaps.end(); ++it) {
            auto *jsError = reinterpret_cast<TaggedObject *>(it->first);
            Region *region = Region::ObjectAddressToRange(jsError);
            if (!region->Test(jsError)) {
                continue; // JsError dead → skip its JIT code
            }
            for (auto &entry : *(it->second)) {
                auto *jitCode = std::get<0>(entry);
                objectVisitor.HandleObject(jitCode);
            }
        }
    };
    Runtime::GetInstance()->GCIterateThreadList([&visitor](JSThread *iteratedThread) {
        iteratedThread->IterateJitCodeMap(visitor);
    });
#if ECMASCRIPT_ENABLE_ARK_STEED
    GlobalCCMarkRootVisitor rootVisitor(holder);
    Runtime::GetInstance()->GCIterateThreadList([&rootVisitor](JSThread *iteratedThread) {
        auto *refs = iteratedThread->GetEcmaVM()->GetHeap()->GetEmbeddedCodeRefSet();
        if (refs->IsEmpty()) {
            return;
        }
        refs->VisitMarkedGlobalTargets(rootVisitor);
    });
#endif
    holder->FlushAll();
    ProcessMarkStack(DAEMON_THREAD_INDEX);
    sHeap_->WaitRunningMarkTaskFinished();
}

void GlobalCCMarker::HandleMarkingFinished()
{
    return;
}

void GlobalCCMarker::InitializeMarking()
{
    CHECK_DAEMON_THREAD();
    auto *workManager = globalCC_->GetWorkManager();
    sHeap_->Prepare(true);
    // Piggyback on the existing shared mark state: SetSharedMarkStatus broadcasts to all
    // JSThreads (via GCIterateThreadList) and new VM copy from dThread at creation. This
    // makes the stubs' existing shared-mark branch fire for shared values — no new stub branch.
    DaemonThread::GetInstance()->SetSharedMarkStatus(SharedMarkStatus::CONCURRENT_MARKING_OR_FINISHED);
    DaemonThread::GetInstance()->SetGlobalConcurrentMarking(true);
    auto clearAppSpawn = [](Region *current) {
        current->ClearMarkGCBitset();
        current->ClearCrossRegionRSet();
    };
    auto resetAlive = [](Region *current) {
        current->ResetAliveObject();
    };
    Runtime::GetInstance()->GCIterateThreadList(
        [this, &clearAppSpawn, &resetAlive](JSThread *thread) {
            Heap *heap = thread->GetEcmaVM()->GetHeap();
            heap->Prepare();
            if (heap->CheckOngoingConcurrentMarking()) {
                heap->GetConcurrentMarker()->Reset(true);
            }
            thread->SetMarkStatus(MarkStatus::MARKING);
            thread->SetGlobalConcurrentMarking(true);
            heap->GetAppSpawnSpace()->EnumerateRegions(clearAppSpawn);
            heap->EnumerateRegions(resetAlive);
        });
    sHeap_->GetAppSpawnSpace()->EnumerateRegions(clearAppSpawn);
    sHeap_->EnumerateOldSpaceRegions(resetAlive);
    workManager->Initialize();
}

void GlobalCCMarker::MarkRoots()
{
    auto *workManager = globalCC_->GetWorkManager();
    GlobalCCMarkRootVisitor rootVisitor(workManager->GetHolder(DAEMON_THREAD_INDEX));
    Runtime *runtime = Runtime::GetInstance();
    runtime->GCIterateThreadList([&rootVisitor](JSThread *thread) {
        EcmaVM *vm = thread->GetEcmaVM();
        ObjectXRay::VisitVMRoots(vm, rootVisitor);
    });
    runtime->IterateSharedRoot(rootVisitor);
}

void GlobalCCMarker::MarkGlobalRoots()
{
    auto *workManager = globalCC_->GetWorkManager();
    GlobalCCWorkNodeHolder *holder = workManager->GetHolder(DAEMON_THREAD_INDEX);
    GlobalCCMarkRootVisitor rootVisitor(holder);
    Runtime::GetInstance()->IterateSharedRoot(rootVisitor);
    holder->FlushAll();
}

void GlobalCCMarker::MarkLocalRootsOnFlip(JSThread *thread)
{
    auto *workManager = globalCC_->GetWorkManager();
    // Stack-local lightweight holder living only within this flip callback: it only pushes
    // to the mark stack (never the weak-aggregate wrappers or weak-reference queues), so
    // no Finish()/Destroy() teardown is required. Its work nodes are owned by the work
    // manager's bump allocator and reclaimed at round Finish().
    GlobalCCWorkNodeHolder lightHolder;
    lightHolder.SetupMarkOnly(workManager, workManager->GetMarkStack());
    lightHolder.InitializeMarkOnly();
    GlobalCCMarkRootVisitor rootVisitor(&lightHolder);
    ObjectXRay::VisitVMRoots(thread->GetEcmaVM(), rootVisitor);
    lightHolder.FlushMarkStackToGlobal();
}

void GlobalCCMarker::DoMarking()
{
    ProcessMarkStack(DAEMON_THREAD_INDEX);
    sHeap_->WaitRunningMarkTaskFinished();
}

void GlobalCCMarker::ProcessMarkStack(uint32_t threadId)
{
    auto *workManager = globalCC_->GetWorkManager();
    GlobalCCWorkNodeHolder *holder = workManager->GetHolder(threadId);
    GlobalCCMarkObjectVisitor visitor(holder);
    TaggedObject *obj = nullptr;
    while (true) {
        while (holder->Pop(&obj)) {
            JSHClass *hclass = obj->SynchronizedGetClass();
            size_t size = hclass->SizeFromJSHClass(obj);
            Region *region = Region::ObjectAddressToRange(obj);
            region->IncreaseAliveObject(size);
            visitor.VisitHClass(obj, hclass);
            ObjectXRay::VisitObjectBody<VisitType::OLD_GC_VISIT,
                VisitLinkedWeakHashMapType::AS_WEAK_AGGREGATE>(obj, hclass, visitor);
        }
        bool doMore = false;
        WeakAggregate weakAggregate;
        while (holder->PopFreshWeakAggregate(&weakAggregate)) {
            if (!HandleWeakAggregate(weakAggregate, visitor)) {
                holder->PushPendingWeakAggregate(weakAggregate);
            } else {
                doMore = true;
            }
        }
        if (!doMore) {
            holder->FlushPendingWeakAggregate();
            return;
        }
    }
}

void GlobalCCMarker::MarkFromWriteBarrier(const JSThread *thread, TaggedObject *value)
{
    Region *region = Region::ObjectAddressToRange(value);
    if (region->InSharedReadOnlySpace()) {
        return;
    }
    if (region->AtomicMark(value)) {
        std::atomic_thread_fence(std::memory_order_seq_cst);
        MarkWorkNode *&localBuffer = thread->GetEcmaVM()->GetHeap()->GetGlobalCCMarkingLocalBuffer();
        globalCC_->GetWorkManager()->PushToLocalMarkingBuffer(localBuffer, value);
    }
}
}  // namespace panda::ecmascript
