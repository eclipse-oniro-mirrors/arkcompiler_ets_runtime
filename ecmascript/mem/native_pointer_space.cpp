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

#include "ecmascript/mem/native_pointer_space.h"

#include <algorithm>
#include "base_runtime.h"
#include "common_components/taskpool/taskpool.h"
#include "ecmascript/ecma_vm.h"
#include "ecmascript/js_thread.h"
#include "ecmascript/mem/heap-inl.h"
#include "ecmascript/mem/heap.h"
#include "ecmascript/mem/native_pointer_space-inl.h"
#include "ecmascript/mem/specialized_slot_space-inl.h"

namespace panda::ecmascript {

class NativePointerScanTask final : public common::Task {
public:
    NativePointerScanTask(int32_t taskId, NativePointerSpace *space)
        : common::Task(taskId), space_(space)
    {
    }

    bool Run([[maybe_unused]] uint32_t threadIndex) override
    {
        while (true) {
            size_t regionIndex = space_->collectRegionIndex_.fetch_add(1, std::memory_order_relaxed);
            if (regionIndex >= space_->collectRegions_.size()) {
                break;
            }
            space_->CollectDeadCallbacksFromRegion(space_->collectRegions_[regionIndex]);
        }
        LockHolder holder(space_->callbackMutex_);
        if (space_->collectTaskCount_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            space_->collectCV_.SignalAll();
        }
        return true;
    }

private:
    NativePointerSpace *space_ {nullptr};
};

NativePointerSpace::NativePointerSpace(Heap *heap, size_t initialCapacity, size_t maximumCapacity)
    : SpecializedSlotSpace(heap, MemSpaceType::NATIVE_POINTER_SPACE, JSNativePointer::SIZE,
                           initialCapacity, maximumCapacity)
{
}

SweepableSpace *GetSweepableSpace(NativePointerSpace *space)
{
    return space;
}

void NativePointerSpace::PrepareSweeping() {}

void NativePointerSpace::AsyncSweep([[maybe_unused]] bool isMain,
                                    [[maybe_unused]] bool releaseMemory) {}

bool NativePointerSpace::TryFillSweptRegion()
{
    return true;
}

bool NativePointerSpace::FinishFillSweptRegion()
{
    return true;
}

bool NativePointerSpace::CheckExpandBudget(size_t expandSize)
{
    size_t committedSize = localHeap_->GetOldSpaceCommittedSize() + expandSize;
    return committedSize < GetMaximumCapacity() + GetOutOfMemoryOvershootSize();
}

void NativePointerSpace::CollectDeadCallbacks()
{
    ASSERT(collectTaskCount_.load(std::memory_order_relaxed) == 0);
    ASSERT(collectRegions_.empty());
    collectRegionIndex_.store(0, std::memory_order_relaxed);
    EnumerateFromRegions([this](Region *region) {
        collectRegions_.emplace_back(region);
    });
    if (collectRegions_.empty()) {
        return;
    }

    if (localHeap_->GetMaxEvacuateTaskCount() == 0) {
        for (Region *region : collectRegions_) {
            CollectDeadCallbacksFromRegion(region);
        }
        collectRegions_.clear();
        return;
    }

    uint32_t taskCount = CalculateCollectTaskCount();
    collectTaskCount_.store(taskCount, std::memory_order_release);
    for (uint32_t i = 0; i < taskCount; i++) {
        common::Taskpool::GetCurrentTaskpool()->PostTask(std::make_unique<NativePointerScanTask>(
            localHeap_->GetJSThread()->GetThreadId(), this));
    }
}

void NativePointerSpace::CollectDeadCallbacksFromRegion(Region* region)
{
    CollectDeadCallbacksFromRegion(region, []([[maybe_unused]] TaggedObject *object) {});
}

uint32_t NativePointerSpace::CalculateCollectTaskCount() const
{
    // Keep task granularity aligned with SharedCC to avoid spawning excessive tasks for few regions.
    constexpr uint32_t regionPerThread = 8;
    uint32_t taskpoolThreadCount = common::Taskpool::GetCurrentTaskpool()->GetTotalThreadNum();
    uint32_t maxThreadCount = std::min(localHeap_->GetMaxEvacuateTaskCount(), taskpoolThreadCount);
    uint32_t taskCount = static_cast<uint32_t>(collectRegions_.size()) / regionPerThread;
    return std::min(std::max(1U, taskCount), maxThreadCount);
}

void NativePointerSpace::ProcessDeadCallbacks()
{
    LockHolder holder(callbackMutex_);
    while (collectTaskCount_.load(std::memory_order_acquire) > 0) {
        collectCV_.Wait(&callbackMutex_);
    }
    collectRegions_.clear();
}

void NativePointerSpace::ClearAll(JSThread* thread)
{
    std::vector<DeadNativePointer> records;
    IterateOverObjects([&records, thread](TaggedObject* taggedObject) {
        auto *object = JSNativePointer::Cast(taggedObject);
        DeadNativePointer record {
            std::make_pair(object->GetDeleter(),
                           std::make_tuple(thread->GetEnv(), object->GetExternalPointer(), object->GetData())),
            object->GetBindingSize(),
            object->GetNativeFlag(),
            object->GetIsConcurrent()};
        records.emplace_back(record);
        object->SetBindingSize(0);
        object->SetDeleter(nullptr);
    });
    for (const DeadNativePointer& record: records) {
        localHeap_->DecreaseNativeBindingSize(record.bindingSize_);
        localHeap_->GetNativeAreaAllocator()->DecreaseNativeSizeStats(record.bindingSize_, record.nativeFlag_);
        auto [callback, callbackData] = record.callback_;
        auto [env, externalPointer, data] = callbackData;
        if (callback != nullptr) {
            callback(env, externalPointer, data);
        }
    }
}

}  // namespace panda::ecmascript
