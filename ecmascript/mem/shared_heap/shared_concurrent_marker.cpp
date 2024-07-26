/*
 * Copyright (c) 2024 Huawei Device Co., Ltd.
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

#include "ecmascript/mem/shared_heap/shared_concurrent_marker.h"

#include "ecmascript/checkpoint/thread_state_transition.h"
#include "ecmascript/ecma_string_table.h"
#include "ecmascript/mem/allocator-inl.h"
#include "ecmascript/mem/clock_scope.h"
#include "ecmascript/mem/heap-inl.h"
#include "ecmascript/mem/mark_stack.h"
#include "ecmascript/mem/mark_word.h"
#include "ecmascript/mem/shared_heap/shared_gc_marker-inl.h"
#include "ecmascript/mem/space-inl.h"
#include "ecmascript/mem/verification.h"
#include "ecmascript/mem/visitor.h"
#include "ecmascript/mem/gc_stats.h"
#include "ecmascript/mem/verification.h"
#include "ecmascript/mem/shared_heap/shared_concurrent_sweeper.h"
#include "ecmascript/taskpool/taskpool.h"

namespace panda::ecmascript {
SharedConcurrentMarker::SharedConcurrentMarker(EnableConcurrentMarkType type)
    : sHeap_(SharedHeap::GetInstance()),
      dThread_(DaemonThread::GetInstance()),
      sWorkManager_(sHeap_->GetWorkManager()),
      enableMarkType_(type) {}

void SharedConcurrentMarker::EnableConcurrentMarking(EnableConcurrentMarkType type)
{
    if (IsConfigDisabled()) {
        return;
    }
    if (IsEnabled() && !dThread_->IsReadyToConcurrentMark() && type == EnableConcurrentMarkType::DISABLE) {
        enableMarkType_ = EnableConcurrentMarkType::REQUEST_DISABLE;
    } else {
        enableMarkType_ = type;
    }
}

void SharedConcurrentMarker::Mark(TriggerGCType gcType, GCReason gcReason)
{
    RecursionScope recurScope(this);
    gcType_ = gcType;
    gcReason_ = gcReason;
    sHeap_->WaitSensitiveStatusFinished();
    {
        ThreadManagedScope runningScope(dThread_);
        SuspendAllScope scope(dThread_);
        TRACE_GC(GCStats::Scope::ScopeId::ConcurrentMark, sHeap_->GetEcmaGCStats());
        LOG_GC(DEBUG) << "SharedConcurrentMarker: Concurrent Marking Begin";
        ECMA_BYTRACE_NAME(HITRACE_TAG_ARK, "SharedConcurrentMarker::Mark");
        CHECK_DAEMON_THREAD();
        // TODO: support shared runtime state
        InitializeMarking();
        if (UNLIKELY(sHeap_->ShouldVerifyHeap())) {
            SharedHeapVerification(sHeap_, VerifyKind::VERIFY_PRE_SHARED_GC).VerifyAll();
        }
    }
    // Daemon thread do not need to post task to GC_Thread
    ASSERT(!dThread_->IsInRunningState());
    DoMarking();
    HandleMarkingFinished();
}

void SharedConcurrentMarker::Finish()
{
    sWorkManager_->Finish();
}

void SharedConcurrentMarker::ReMark()
{
    CHECK_DAEMON_THREAD();
#ifndef NDEBUG
    ASSERT(dThread_->HasLaunchedSuspendAll());
#endif
    TRACE_GC(GCStats::Scope::ScopeId::ReMark, sHeap_->GetEcmaGCStats());
    LOG_GC(DEBUG) << "SharedConcurrentMarker: Remarking Begin";
    // TODO: support shared runtime state
    SharedGCMarker *sharedGCMarker = sHeap_->GetSharedGCMarker();
    // If enable shared concurrent mark, the recorded weak reference slots from local to share may be changed
    // during LocalGC. For now just re-scan the local_to_share bit to record and update these weak references.
    sharedGCMarker->MarkRoots(DAEMON_THREAD_INDEX, SharedMarkType::CONCURRENT_MARK_REMARK);
    sharedGCMarker->DoMark<SharedMarkType::CONCURRENT_MARK_REMARK>(DAEMON_THREAD_INDEX);
    sharedGCMarker->MergeBackAndResetRSetWorkListHandler();
    sHeap_->WaitRunningTaskFinished();
}

void SharedConcurrentMarker::Reset(bool clearGCBits)
{
    Finish();
    dThread_->SetSharedMarkStatus(SharedMarkStatus::READY_TO_CONCURRENT_MARK);
    isConcurrentMarking_ = false;
    if (clearGCBits) {
        // Shared gc clear GC bits in ReclaimRegions after GC
        auto callback = [](Region *region) {
            region->ClearMarkGCBitset();
            region->ResetAliveObject();
        };
        sHeap_->EnumerateOldSpaceRegions(callback);
    }
}

void SharedConcurrentMarker::ResetWorkManager(SharedGCWorkManager *sWorkManager)
{
    sWorkManager_ = sWorkManager;
}

void SharedConcurrentMarker::InitializeMarking()
{
    CHECK_DAEMON_THREAD();
    // TODO: support shared runtime state
    sHeap_->Prepare(true);
    isConcurrentMarking_ = true;
    dThread_->SetSharedMarkStatus(SharedMarkStatus::CONCURRENT_MARKING_OR_FINISHED);

    sHeapObjectSize_ = sHeap_->GetHeapObjectSize();
    sHeap_->EnumerateOldSpaceRegions([](Region *current) {
        ASSERT(current->InSharedSweepableSpace());
        current->ResetAliveObject();
    });
    sWorkManager_->Initialize();
    sHeap_->GetSharedGCMarker()->MarkRoots(DAEMON_THREAD_INDEX, SharedMarkType::CONCURRENT_MARK_INITIAL_MARK);
}

void SharedConcurrentMarker::DoMarking()
{
    ClockScope clockScope;
    sHeap_->GetSharedGCMarker()->DoMark<SharedMarkType::CONCURRENT_MARK_INITIAL_MARK>(DAEMON_THREAD_INDEX);
    sHeap_->WaitRunningTaskFinished();
    FinishMarking(clockScope.TotalSpentTime());
}

void SharedConcurrentMarker::FinishMarking(float spendTime)
{
    sHeapObjectSize_ = sHeap_->GetHeapObjectSize();
    SetDuration(spendTime);
}

void SharedConcurrentMarker::HandleMarkingFinished()
{
    sHeap_->WaitSensitiveStatusFinished();
    sHeap_->DaemonCollectGarbage(gcType_, gcReason_);
}
}  // namespace panda::ecmascript