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

#include "ecmascript/mem/heap.h"

#include "ecmascript/mem/module_space.h"
#include "ecmascript/module/js_module_source_text.h"

namespace panda::ecmascript {

// Capacities are intentionally 0: module growth is charged to the old-space budget through
// CheckExpandBudget, and nothing reads GetInitialCapacity()/GetMaximumCapacity() here.
ModuleSpace::ModuleSpace(Heap *heap)
    : SpecializedSlotSpace(heap, MemSpaceType::MODULE_SPACE, SourceTextModule::SIZE, 0, 0)
{
}

bool ModuleSpace::CheckExpandBudget(size_t expandSize)
{
    // Module growth shares the old-space budget (never an independent OOM source).
    return !localHeap_->OldSpaceExceedCapacity(expandSize);
}

}  // namespace panda::ecmascript
