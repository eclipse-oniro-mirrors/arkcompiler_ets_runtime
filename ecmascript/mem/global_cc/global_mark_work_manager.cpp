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

#include "ecmascript/mem/global_cc/global_mark_work_manager.h"

#include "ecmascript/mem/heap.h"
#include "ecmascript/mem/work_manager-inl.h"

namespace panda::ecmascript {

void GlobalCCWorkNodeHolder::Setup(GlobalCCWorkManager *wm, GlobalMarkStack *markStack,
                                   GlobalWeakAggregateStack *freshStack,
                                   GlobalWeakAggregateStack *pendingStack)
{
    workManager_ = wm;
    markWorkNodeWrapper_.Setup(markStack);
    freshWeakAggregateWorkNodeWrapper_.Setup(freshStack);
    pendingWeakAggregateWorkNodeWrapper_.Setup(pendingStack);
    continuousQueue_ = new ProcessQueue();
    continuousWeakLinkedHashMapQueue_ = new WeakLinkedHashMapProcessQueue();
}

void GlobalCCWorkNodeHolder::Destroy()
{
    if (continuousQueue_ != nullptr) {
        continuousQueue_->Destroy();
        delete continuousQueue_;
        continuousQueue_ = nullptr;
    }
    if (continuousWeakLinkedHashMapQueue_ != nullptr) {
        continuousWeakLinkedHashMapQueue_->Destroy();
        delete continuousWeakLinkedHashMapQueue_;
        continuousWeakLinkedHashMapQueue_ = nullptr;
    }
}

void GlobalCCWorkNodeHolder::Initialize()
{
    markWorkNodeWrapper_.Initialize(workManager_);
    freshWeakAggregateWorkNodeWrapper_.Initialize(workManager_);
    pendingWeakAggregateWorkNodeWrapper_.Initialize(workManager_);
    weakQueue_ = new ProcessQueue();
    weakQueue_->BeginMarking(continuousQueue_);
    weakLinkedHashMapQueue_ = new WeakLinkedHashMapProcessQueue();
    weakLinkedHashMapQueue_->BeginMarking(continuousWeakLinkedHashMapQueue_);
    aliveSize_ = 0;
}

void GlobalCCWorkNodeHolder::Finish()
{
    if (weakQueue_ != nullptr) {
        weakQueue_->FinishMarking(continuousQueue_);
        delete weakQueue_;
        weakQueue_ = nullptr;
    }
    if (weakLinkedHashMapQueue_ != nullptr) {
        weakLinkedHashMapQueue_->FinishMarking(continuousWeakLinkedHashMapQueue_);
        delete weakLinkedHashMapQueue_;
        weakLinkedHashMapQueue_ = nullptr;
    }
}

void GlobalCCWorkNodeHolder::Push(TaggedObject *object)
{
    markWorkNodeWrapper_.Push(object, workManager_);
}

void GlobalCCWorkNodeHolder::SetupMarkOnly(GlobalCCWorkManager *wm, GlobalMarkStack *markStack)
{
    workManager_ = wm;
    markWorkNodeWrapper_.Setup(markStack);
}

void GlobalCCWorkNodeHolder::InitializeMarkOnly()
{
    markWorkNodeWrapper_.Initialize(workManager_);
}

void GlobalCCWorkNodeHolder::FlushMarkStackToGlobal()
{
    markWorkNodeWrapper_.PushWorkNodeToGlobal(workManager_);
}

bool GlobalCCWorkNodeHolder::Pop(TaggedObject **object)
{
    return markWorkNodeWrapper_.Pop(object);
}

void GlobalCCWorkNodeHolder::FlushAll()
{
    markWorkNodeWrapper_.PushWorkNodeToGlobal(workManager_);
    freshWeakAggregateWorkNodeWrapper_.PushWorkNodeToGlobal(workManager_);
    pendingWeakAggregateWorkNodeWrapper_.PushWorkNodeToGlobal(workManager_);
}

void GlobalCCWorkNodeHolder::PushFreshWeakAggregate(WeakAggregate entry)
{
    freshWeakAggregateWorkNodeWrapper_.Push(entry, workManager_);
}

bool GlobalCCWorkNodeHolder::PopFreshWeakAggregate(WeakAggregate *entry)
{
    return freshWeakAggregateWorkNodeWrapper_.Pop(entry);
}

void GlobalCCWorkNodeHolder::PushPendingWeakAggregate(WeakAggregate entry)
{
    pendingWeakAggregateWorkNodeWrapper_.Push(entry, workManager_);
}

bool GlobalCCWorkNodeHolder::PopPendingWeakAggregate(WeakAggregate *entry)
{
    return pendingWeakAggregateWorkNodeWrapper_.Pop(entry);
}

void GlobalCCWorkNodeHolder::PushWeakReference(JSTaggedType *weak)
{
    weakQueue_->PushBack(weak);
}

void GlobalCCWorkNodeHolder::PushWeakLinkedHashMap(TaggedObject *weakLinkedHashMap)
{
    weakLinkedHashMapQueue_->PushBack(weakLinkedHashMap);
}

GlobalCCWorkManager::GlobalCCWorkManager(SharedHeap *sHeap, uint32_t threadNum)
    : WorkManagerBase(sHeap->GetNativeAreaAllocator()), threadNum_(threadNum), sHeap_(sHeap)
{
    for (uint32_t i = 0; i < threadNum_; i++) {
        works_.at(i).Setup(this, &markStack_, &freshWeakAggregateStack_, &pendingWeakAggregateStack_);
    }
}

GlobalCCWorkManager::~GlobalCCWorkManager()
{
    Finish();
    for (uint32_t i = 0; i < threadNum_; i++) {
        works_.at(i).Destroy();
    }
}

void GlobalCCWorkManager::Initialize()
{
    InitializeBase();
    for (uint32_t i = 0; i < threadNum_; i++) {
        works_.at(i).Initialize();
    }
    if (initialized_.load(std::memory_order_acquire)) { // LOCV_EXCL_BR_LINE
        LOG_ECMA(FATAL) << "this branch is unreachable";
        UNREACHABLE();
    }
    initialized_.store(true, std::memory_order_release);
}

size_t GlobalCCWorkManager::Finish()
{
    size_t aliveSize = 0;
    for (uint32_t i = 0; i < threadNum_; i++) {
        GlobalCCWorkNodeHolder &holder = works_.at(i);
        holder.Finish();
        aliveSize += holder.GetAliveSize();
    }
    markStack_.Clear();
    freshWeakAggregateStack_.Clear();
    pendingWeakAggregateStack_.Clear();
    FinishBase();
    initialized_.store(false, std::memory_order_release);
    return aliveSize;
}

void GlobalCCWorkManager::CheckAndPostTask()
{
    if (sHeap_->IsParallelGCEnabled() && sHeap_->CheckCanDistributeTask()) {
        sHeap_->TryPostGlobalCCMarkingTask();
    }
}

bool GlobalCCWorkManager::IsGlobalMarkEmpty()
{
    return markStack_.IsEmpty();
}

bool GlobalCCWorkManager::IsGlobalFreshWeakEmpty()
{
    return freshWeakAggregateStack_.IsEmpty();
}

bool GlobalCCWorkManager::IsGlobalPendingWeakEmpty()
{
    return pendingWeakAggregateStack_.IsEmpty();
}

void GlobalCCWorkManager::PushToLocalMarkingBuffer(MarkWorkNode *&markingBuffer, TaggedObject *object)
{
    if (UNLIKELY(markingBuffer == nullptr)) {
        markingBuffer = AllocateWorkNode<MarkWorkNode>();
    }
    ASSERT(markingBuffer != nullptr);
    if (UNLIKELY(markingBuffer->IsFull())) {
        ASSERT(!markingBuffer->IsEmpty());
        markStack_.Push(markingBuffer);
        CheckAndPostTask();
        markingBuffer = AllocateWorkNode<MarkWorkNode>();
    }
    markingBuffer->Push(object);
}

void GlobalCCWorkManager::FlushLocalMarkingBuffer(MarkWorkNode *&markingBuffer)
{
    if (markingBuffer != nullptr && !markingBuffer->IsEmpty()) {
        markStack_.Push(markingBuffer);
    }
    markingBuffer = nullptr;
}

}  // namespace panda::ecmascript
