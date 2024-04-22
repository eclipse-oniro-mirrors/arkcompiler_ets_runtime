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

#ifndef ECMASCRIPT_MEM_SHARED_HEAP_SHARED_GC_MARKER_H
#define ECMASCRIPT_MEM_SHARED_HEAP_SHARED_GC_MARKER_H

#include "ecmascript/js_hclass.h"
#include "ecmascript/mem/slots.h"
#include "ecmascript/mem/work_manager.h"

namespace panda::ecmascript {
class Region;
class TaggedObject;

class SharedGCMarker {
public:
    explicit SharedGCMarker(SharedGCWorkManager *workManger) : sWorkManager_(workManger) {}
    ~SharedGCMarker() = default;

    void ResetWorkManager(SharedGCWorkManager *workManager);
    void MarkRoots(uint32_t threadId, EcmaVM *localVm);
    void MarkSerializeRoots(uint32_t threadId);
    void MarkSharedModule(uint32_t threadId);
    void ProcessMarkStack(uint32_t threadId);
    template <typename Callback>
    inline bool VisitBodyInObj(TaggedObject *root, ObjectSlot start, ObjectSlot end, Callback callback);
    inline void MarkValue(uint32_t threadId, ObjectSlot &slot);
    inline void MarkObject(uint32_t threadId, TaggedObject *object);
    inline void HandleRoots(uint32_t threadId, [[maybe_unused]] Root type, ObjectSlot slot);
    inline void HandleRangeRoots(uint32_t threadId, [[maybe_unused]] Root type, ObjectSlot start,
                                 ObjectSlot end);
    inline void HandleDerivedRoots(Root type, ObjectSlot base, ObjectSlot derived,
                                   uintptr_t baseOldObject);

    inline void ProcessLocalToShare(uint32_t threadId, Heap *localHeap);
    inline void HandleLocalToShareRSet(uint32_t threadId, Region *region);
    inline void RecordWeakReference(uint32_t threadId, JSTaggedType *ref);

private:
    SharedGCWorkManager *sWorkManager_ { nullptr };
};
}  // namespace panda::ecmascript
#endif  // ECMASCRIPT_MEM_SHARED_HEAP_SHARED_GC_MARKER_H