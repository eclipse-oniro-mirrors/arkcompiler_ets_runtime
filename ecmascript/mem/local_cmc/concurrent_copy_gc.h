/*
 * Copyright (c) 2025 Huawei Device Co., Ltd.
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

#ifndef ECMASCRIPT_MEM_LOCAL_CMC_CONCURRENT_COPY_GC_H
#define ECMASCRIPT_MEM_LOCAL_CMC_CONCURRENT_COPY_GC_H

#include "ecmascript/base/config.h"
#include "ecmascript/mem/cms_mem/slot_allocator-inl.h"
#include "ecmascript/mem/heap.h"
#include "ecmascript/mem/tlab_allocator.h"

namespace panda {
namespace ecmascript {
class ConcurrentCopyGC {
public:
    explicit ConcurrentCopyGC(Heap *heap);
    ~ConcurrentCopyGC() = default;
    NO_COPY_SEMANTIC(ConcurrentCopyGC);
    NO_MOVE_SEMANTIC(ConcurrentCopyGC);

    void RunPhase();
    Heap *GetHeap() const
    {
        return heap_;
    }
    void HandleUpdateFinished(bool cleanCallback = true);
    PUBLIC_API void WaitFinished();
    static WeakRootVisitor CreateUpdateWeakVisitor(CCEvacuator *evacuator);

private:
    // Coordinate this collector's copy lifetime with the SharedGC LocalCC drain.
    // Returns false if a GlobalCC round hijacked this heap's mark state while blocked
    // (or already in flight at entry): the local CC cycle is dead and must not proceed.
    bool StartLocalCCCopy();
    void ProcessWeakReference();
    void ProcessRecordWeakReferenceInParallel(uint32_t totalThreadCount);
    void Sweep();
    void InitializeCopyPhase();
    uint32_t CalculateConcurrentRootTaskCount() const;
    void UpdateRoot();
    void PostUpdateConcurrentRootsTask();
    void StartConcurrentNativePointerTasks(uint32_t coordinatorThreadIndex);
    void ProcessConcurrentNativePointerTask(uint32_t threadIndex, uint32_t taskIndex);
    void HandleConcurrentNativePointerTaskFinished(uint32_t threadIndex);
    void StartConcurrentRootTasks(uint32_t coordinatorThreadIndex);
    void ProcessConcurrentRootTask(uint32_t threadIndex);
    void HandleConcurrentRootTaskFinished(uint32_t threadIndex);
    void PostLocalCCCallback();
    void UpdateConcurrentRoots(uint32_t coordinatorThreadIndex);
    void ConcurrentCopy();
    void RunUpdatePhase();
    void Finish();

    void UpdateRecordWeakReference(uint32_t threadIndex);
    void UpdateRecordWeakLinkedHashMap(uint32_t threadIndex);
    void PreGC();
    void PostGC();

    int CalculateCopyThreadNum();

    Heap *heap_{nullptr};
    JSThread *thread_ {nullptr};
    bool ccUpdateFinished_{false};
#if USE_CMS_GC
    SlotGCAllocator *GetTlabAllocator(uint32_t threadIndex)
    {
        return &tlabAllocators_.at(threadIndex);
    }
    std::array<SlotGCAllocator, common::MAX_TASKPOOL_THREAD_NUM + 1> tlabAllocators_;
#else
    CCTlabAllocator *GetTlabAllocator(uint32_t threadIndex)
    {
        return &tlabAllocators_.at(threadIndex);
    }
    std::array<CCTlabAllocator, common::MAX_TASKPOOL_THREAD_NUM + 1> tlabAllocators_;
#endif
    std::atomic<size_t> runningTaskCount_ {0};
    std::atomic<size_t> runningNativePointerTaskCount_ {0};
    std::atomic<size_t> runningConcurrentRootTaskCount_ {0};
    uint32_t concurrentRootTaskCount_ {0};
    std::unique_ptr<common::Task> localCCCallbackTask_;
    Mutex waitMutex_;
    ConditionVariable waitCV_;

    std::vector<Region*> tasks_ {};
    std::atomic<size_t> taskIter_ {0};
    friend class Heap;
    friend class ConcurrentCopyTask;
    friend class ConcurrentCopyNativePointerTask;
    friend class ConcurrentCopyModuleRootTask;
    friend class ConcurrentCopyUpdateConcurrentRootsTask;
    friend class ConcurrentCopyUpdateWeakReferenceTask;
};

class ConcurrentCopyUpdateConcurrentRootsTask : public common::Task {
public:
    ConcurrentCopyUpdateConcurrentRootsTask(int32_t id, ConcurrentCopyGC *cc) : Task(id), cc_(cc) {}
    ~ConcurrentCopyUpdateConcurrentRootsTask() override = default;
    bool Run(uint32_t threadIndex) override;

    NO_COPY_SEMANTIC(ConcurrentCopyUpdateConcurrentRootsTask);
    NO_MOVE_SEMANTIC(ConcurrentCopyUpdateConcurrentRootsTask);

private:
    ConcurrentCopyGC *cc_ {nullptr};
};

class ConcurrentCopyNativePointerTask : public common::Task {
public:
    ConcurrentCopyNativePointerTask(int32_t id, ConcurrentCopyGC *cc, uint32_t taskIndex)
        : Task(id), cc_(cc), taskIndex_(taskIndex) {}
    ~ConcurrentCopyNativePointerTask() override = default;
    bool Run(uint32_t threadIndex) override;

    NO_COPY_SEMANTIC(ConcurrentCopyNativePointerTask);
    NO_MOVE_SEMANTIC(ConcurrentCopyNativePointerTask);

private:
    ConcurrentCopyGC *cc_ {nullptr};
    uint32_t taskIndex_ {0};
};

class ConcurrentCopyModuleRootTask : public common::Task {
public:
    ConcurrentCopyModuleRootTask(int32_t id, ConcurrentCopyGC *cc) : Task(id), cc_(cc) {}
    ~ConcurrentCopyModuleRootTask() override = default;
    bool Run(uint32_t threadIndex) override;

    NO_COPY_SEMANTIC(ConcurrentCopyModuleRootTask);
    NO_MOVE_SEMANTIC(ConcurrentCopyModuleRootTask);

private:
    ConcurrentCopyGC *cc_ {nullptr};
};

class ConcurrentCopyTask : public common::Task {
public:
    ConcurrentCopyTask(std::vector<Region*> &tasks, std::atomic<size_t> &taskIter,
                       std::atomic<size_t> &runningTaskCount, ConcurrentCopyGC *cc)
        : Task(0), tasks_(tasks), taskIter_(taskIter), runningTaskCount_(runningTaskCount),
          cc_(cc), totalSize_(tasks_.size()) {}
    ~ConcurrentCopyTask() override = default;
    bool Run(uint32_t threadIndex) override;

    NO_COPY_SEMANTIC(ConcurrentCopyTask);
    NO_MOVE_SEMANTIC(ConcurrentCopyTask);

private:
    Region *GetNextTask();
    void CopyRegion(Region *region, CCEvacuator &evacuator);
    std::vector<Region*> &tasks_;
    std::atomic<size_t> &taskIter_;
    std::atomic<size_t> &runningTaskCount_;
    ConcurrentCopyGC *cc_ {nullptr};
    size_t totalSize_ {0};
};

class ConcurrentCopyUpdateWeakReferenceTask : public common::Task {
public:
    ConcurrentCopyUpdateWeakReferenceTask(int32_t id, ConcurrentCopyGC *cc, uint32_t weakReferenceThreadIndex,
                                          common::TaskPackMonitor *monitor)
        : Task(id), cc_(cc), weakReferenceThreadIndex_(weakReferenceThreadIndex), monitor_(monitor) {}
    ~ConcurrentCopyUpdateWeakReferenceTask() override = default;
    bool Run(uint32_t threadIndex) override;

    NO_COPY_SEMANTIC(ConcurrentCopyUpdateWeakReferenceTask);
    NO_MOVE_SEMANTIC(ConcurrentCopyUpdateWeakReferenceTask);

private:
    ConcurrentCopyGC *cc_ {nullptr};
    uint32_t weakReferenceThreadIndex_ {0};
    common::TaskPackMonitor *monitor_ {nullptr};
};
}  // namespace ecmascript
}  // namespace panda

#endif  // ECMASCRIPT_MEM_LOCAL_CMC_CONCURRENT_COPY_GC_H
