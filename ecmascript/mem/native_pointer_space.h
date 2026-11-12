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

#ifndef ECMASCRIPT_MEM_NATIVE_POINTER_SPACE_H
#define ECMASCRIPT_MEM_NATIVE_POINTER_SPACE_H

#include <atomic>
#include <vector>

#include "ecmascript/js_native_pointer.h"
#include "ecmascript/mem/specialized_slot_space.h"
#include "ecmascript/platform/mutex.h"

namespace panda::ecmascript {
class JSThread;
class NativePointerScanTask;

struct DeadNativePointer {
    NativePointerCallbackData callback_ {};
    size_t bindingSize_ {0};
    NativeFlag nativeFlag_ {NativeFlag::NO_DIV};
    bool isConcurrent_ {false};
};

class NativePointerSpace final : public SpecializedSlotSpace {
public:
    NativePointerSpace(Heap *heap, size_t initialCapacity, size_t maximumCapacity);
    ~NativePointerSpace() override = default;

    NO_COPY_SEMANTIC(NativePointerSpace);
    NO_MOVE_SEMANTIC(NativePointerSpace);

    void CollectDeadCallbacks();
    void ProcessDeadCallbacks();
    void CollectDeadCallbacksFromRegion(Region* region);
    template <typename Callback>
    void CollectDeadCallbacksFromRegion(Region* region, const Callback &liveObjectCallback);
    void ClearAll(JSThread *thread);

    void PrepareSweeping() override;
    void AsyncSweep([[maybe_unused]] bool isMain,
                    [[maybe_unused]] bool releaseMemory = false) override;
    bool TryFillSweptRegion() override;
    bool FinishFillSweptRegion() override;

private:
    bool CheckExpandBudget(size_t expandSize) override;

    uint32_t CalculateCollectTaskCount() const;

    Mutex callbackMutex_ {};
    ConditionVariable collectCV_ {};
    std::vector<Region *> collectRegions_ {};
    std::atomic<size_t> collectRegionIndex_ {0};
    std::atomic<uint32_t> collectTaskCount_ {0};

    friend class NativePointerScanTask;
};
}  // namespace panda::ecmascript

#endif  // ECMASCRIPT_MEM_NATIVE_POINTER_SPACE_H
