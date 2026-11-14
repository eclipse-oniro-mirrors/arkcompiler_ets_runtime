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

#ifndef ECMASCRIPT_MEM_GLOBAL_CC_GLOBAL_MARK_WORK_MANAGER_H
#define ECMASCRIPT_MEM_GLOBAL_CC_GLOBAL_MARK_WORK_MANAGER_H

#include <cstdint>

#include "ecmascript/mem/work_manager.h"
#include "ecmascript/mem/work_stack.h"

namespace panda::ecmascript {
class GlobalCCWorkManager;
class SharedHeap;

class GlobalCCWorkNodeHolder {
public:
    GlobalCCWorkNodeHolder() = default;
    ~GlobalCCWorkNodeHolder() = default;
    NO_COPY_SEMANTIC(GlobalCCWorkNodeHolder);
    NO_MOVE_SEMANTIC(GlobalCCWorkNodeHolder);

    void Setup(GlobalCCWorkManager *wm, GlobalMarkStack *markStack,
               GlobalWeakAggregateStack *freshStack, GlobalWeakAggregateStack *pendingStack);
    void Destroy();
    void Initialize();
    void Finish();

    // Lightweight mark-only lifecycle for a temporary holder used by flip-time root
    // marking: it only ever touches the mark stack (never the weak-aggregate wrappers or
    // the weak-reference queues), so Finish()/Destroy() are no-ops for it and no explicit
    // teardown is required. Its work nodes are owned by the work manager's bump allocator
    // and are reclaimed at round Finish() like any other holder's nodes.
    void SetupMarkOnly(GlobalCCWorkManager *wm, GlobalMarkStack *markStack);
    void InitializeMarkOnly();
    void FlushMarkStackToGlobal();

    void Push(TaggedObject *object);
    bool Pop(TaggedObject **object);
    void FlushAll();

    void PushFreshWeakAggregate(WeakAggregate entry);
    bool PopFreshWeakAggregate(WeakAggregate *entry);
    void PushPendingWeakAggregate(WeakAggregate entry);
    bool PopPendingWeakAggregate(WeakAggregate *entry);

    void PushWeakReference(JSTaggedType *weak);
    void PushWeakLinkedHashMap(TaggedObject *weakLinkedHashMap);

    ProcessQueue *GetWeakReferenceQueue() const
    {
        return weakQueue_;
    }
    WeakLinkedHashMapProcessQueue *GetWeakLinkedHashMapQueue() const
    {
        return weakLinkedHashMapQueue_;
    }

    void IncreaseAliveSize(size_t size)
    {
        aliveSize_ += size;
    }
    size_t GetAliveSize() const
    {
        return aliveSize_;
    }

    bool IsMarkLocalEmpty() const
    {
        return markWorkNodeWrapper_.IsLocalEmpty();
    }

    void FlushPendingWeakAggregate()
    {
        pendingWeakAggregateWorkNodeWrapper_.PushWorkNodeToGlobal(workManager_);
    }

private:
    WorkManagerBase *workManager_ {nullptr};
    WorkNodeWrapper<GlobalMarkStack> markWorkNodeWrapper_ {};
    WorkNodeWrapper<GlobalWeakAggregateStack> freshWeakAggregateWorkNodeWrapper_ {};
    WorkNodeWrapper<GlobalWeakAggregateStack> pendingWeakAggregateWorkNodeWrapper_ {};
    ProcessQueue *weakQueue_ {nullptr};
    WeakLinkedHashMapProcessQueue *weakLinkedHashMapQueue_ {nullptr};
    ContinuousStack<JSTaggedType> *continuousQueue_ {nullptr};
    ContinuousStack<TaggedObject> *continuousWeakLinkedHashMapQueue_ {nullptr};
    size_t aliveSize_ {0};
};

class GlobalCCWorkManager : public WorkManagerBase {
public:
    GlobalCCWorkManager(SharedHeap *sHeap, uint32_t threadNum);
    ~GlobalCCWorkManager() override;

    NO_COPY_SEMANTIC(GlobalCCWorkManager);
    NO_MOVE_SEMANTIC(GlobalCCWorkManager);

    void Initialize();
    size_t Finish() override;

    void CheckAndPostTask() override;

    bool HasInitialized() const
    {
        return initialized_.load(std::memory_order_acquire);
    }

    GlobalCCWorkNodeHolder *GetHolder(uint32_t threadId)
    {
        return &works_.at(threadId);
    }

    GlobalMarkStack *GetMarkStack()
    {
        return &markStack_;
    }

    uint32_t GetTotalThreadNum() const
    {
        return threadNum_;
    }

    void PushToLocalMarkingBuffer(MarkWorkNode *&markingBuffer, TaggedObject *object);

    void FlushLocalMarkingBuffer(MarkWorkNode *&markingBuffer);

    bool IsGlobalMarkEmpty();
    bool IsGlobalFreshWeakEmpty();
    bool IsGlobalPendingWeakEmpty();

private:
    uint32_t threadNum_ {0};
    SharedHeap *sHeap_ {nullptr};
    std::array<GlobalCCWorkNodeHolder, common::MAX_TASKPOOL_THREAD_NUM + 1> works_;
    GlobalMarkStack markStack_ {};
    GlobalWeakAggregateStack freshWeakAggregateStack_ {};
    GlobalWeakAggregateStack pendingWeakAggregateStack_ {};
    std::atomic<bool> initialized_ {false};
};
}  // namespace panda::ecmascript
#endif  // ECMASCRIPT_MEM_GLOBAL_CC_GLOBAL_MARK_WORK_MANAGER_H
