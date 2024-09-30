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

#include "ecmascript/mem/shared_heap/shared_concurrent_sweeper.h"
#include "ecmascript/ecma_macros.h"
#include "ecmascript/mem/heap.h"
#include "ecmascript/mem/region-inl.h"
#include "ecmascript/mem/space-inl.h"
#include "ecmascript/taskpool/taskpool.h"

namespace panda::ecmascript {
SharedConcurrentSweeper::SharedConcurrentSweeper(SharedHeap *heap, EnableConcurrentSweepType type)
    : sHeap_(heap),
      enableType_(type)
{
}

void SharedConcurrentSweeper::PostTask()
{
    auto tid = DaemonThread::GetInstance()->GetThreadId();
    if (ConcurrentSweepEnabled()) {
        GCWorkerPool::GetCurrentTaskpool()->PostTask(
            std::make_unique<SweeperTask>(tid, this, SHARED_OLD_SPACE));
        GCWorkerPool::GetCurrentTaskpool()->PostTask(
            std::make_unique<SweeperTask>(tid, this, SHARED_NON_MOVABLE));
    }
}

void SharedConcurrentSweeper::Sweep()
{
    if (ConcurrentSweepEnabled()) {
        // Add all region to region list. Ensure all task finish
        sHeap_->GetOldSpace()->PrepareSweeping();
        sHeap_->GetNonMovableSpace()->PrepareSweeping();
        // Prepare
        isSweeping_ = true;
        for (int spaceIndex = 0; spaceIndex < SHARED_SWEEPING_SPACE_NUM; spaceIndex++) {
            remainingTaskNum_[spaceIndex] = SHARED_SWEEPING_SPACE_NUM;
        }
    } else {
        sHeap_->GetOldSpace()->Sweep();
        sHeap_->GetNonMovableSpace()->Sweep();
    }
    sHeap_->GetHugeObjectSpace()->Sweep();
}

void SharedConcurrentSweeper::AsyncSweepSpace(MemSpaceType type, bool isMain)
{
    auto space = sHeap_->GetSpaceWithType(type);
    space->AsyncSweep(isMain);
    int spaceIndex = type - SHARED_SWEEPING_SPACE_BEGIN;
    LockHolder holder(mutexs_[spaceIndex]);
    if (--remainingTaskNum_[spaceIndex] == 0) {
        cvs_[spaceIndex].SignalAll();
    }
}

void SharedConcurrentSweeper::WaitAllTaskFinished()
{
    if (!isSweeping_) {
        return;
    }
    for (int spaceIndex = 0; spaceIndex < SHARED_SWEEPING_SPACE_NUM; spaceIndex++) {
        if (remainingTaskNum_[spaceIndex] > 0) {
            LockHolder holder(mutexs_[spaceIndex]);
            while (remainingTaskNum_[spaceIndex] > 0) {
                cvs_[spaceIndex].Wait(&mutexs_[spaceIndex]);
            }
        }
    }
}

// call in suspendAll thread.
void SharedConcurrentSweeper::EnsureAllTaskFinished()
{
    if (!isSweeping_) {
        return;
    }
    for (int spaceIndex = 0; spaceIndex < SHARED_SWEEPING_SPACE_NUM; spaceIndex++) {
        int type = spaceIndex + SHARED_SWEEPING_SPACE_BEGIN;
        WaitingTaskFinish(static_cast<MemSpaceType>(type));
    }
    isSweeping_ = false;
    if (IsRequestDisabled()) {
        enableType_ = EnableConcurrentSweepType::DISABLE;
    }
}

// call in mutator thread
void SharedConcurrentSweeper::EnsureTaskFinished(MemSpaceType type)
{
    if (!isSweeping_) {
        return;
    }
    WaitingTaskFinish(type);
}

void SharedConcurrentSweeper::WaitingTaskFinish(MemSpaceType type)
{
    int spaceIndex = type - SHARED_SWEEPING_SPACE_BEGIN;
    if (remainingTaskNum_[spaceIndex] > 0) {
        {
            LockHolder holder(mutexs_[spaceIndex]);
            remainingTaskNum_[spaceIndex]++;
        }
        AsyncSweepSpace(type, true);
        LockHolder holder(mutexs_[spaceIndex]);
        while (remainingTaskNum_[spaceIndex] > 0) {
            cvs_[spaceIndex].Wait(&mutexs_[spaceIndex]);
        }
    }
    SharedSparseSpace *space = sHeap_->GetSpaceWithType(type);
    space->FinishFillSweptRegion();
}

void SharedConcurrentSweeper::TryFillSweptRegion()
{
    sHeap_->GetOldSpace()->TryFillSweptRegion();
    sHeap_->GetNonMovableSpace()->TryFillSweptRegion();
}

bool SharedConcurrentSweeper::SweeperTask::Run([[maybe_unused]] uint32_t threadIndex)
{
    if (type_ == SHARED_NON_MOVABLE) {
        sweeper_->AsyncSweepSpace(SHARED_NON_MOVABLE, false);
        sweeper_->AsyncSweepSpace(SHARED_OLD_SPACE, false);
    } else {
        ASSERT(type_ == SHARED_OLD_SPACE);
        sweeper_->AsyncSweepSpace(SHARED_OLD_SPACE, false);
        sweeper_->AsyncSweepSpace(SHARED_NON_MOVABLE, false);
    }
    
    return true;
}

void SharedConcurrentSweeper::EnableConcurrentSweep(EnableConcurrentSweepType type)
{
    if (IsConfigDisabled()) {
        return;
    }
    if (ConcurrentSweepEnabled() && isSweeping_ && type == EnableConcurrentSweepType::DISABLE) {
        enableType_ = EnableConcurrentSweepType::REQUEST_DISABLE;
    } else {
        enableType_ = type;
    }
}
}  // namespace panda::ecmascript
