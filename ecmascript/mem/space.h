/*
 * Copyright (c) 2021-2022 Huawei Device Co., Ltd.
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

#ifndef ECMASCRIPT_MEM_SPACE_H
#define ECMASCRIPT_MEM_SPACE_H

#include "ecmascript/mem/allocator.h"
#include "ecmascript/mem/c_containers.h"
#include "ecmascript/mem/ecma_list.h"
#include "ecmascript/mem/heap_region_allocator.h"
#include "ecmascript/mem/mem.h"
#include "ecmascript/mem/region.h"
#include "libpandabase/utils/type_helpers.h"

namespace panda::ecmascript {
class EcmaVM;
class Heap;

enum MemSpaceType {
    OLD_SPACE = 0,
    NON_MOVABLE,
    MACHINE_CODE_SPACE,
    HUGE_OBJECT_SPACE,
    SEMI_SPACE,
    SNAPSHOT_SPACE,
    COMPRESS_SPACE,
    LOCAL_SPACE,
    SPACE_TYPE_LAST,  // Count of different types

    FREE_LIST_NUM = MACHINE_CODE_SPACE - OLD_SPACE + 1,
};

enum TriggerGCType {
    SEMI_GC,
    OLD_GC,
    FULL_GC,
    GC_TYPE_LAST  // Count of different types
};

static inline std::string ToSpaceTypeName(MemSpaceType type)
{
    switch (type) {
        case OLD_SPACE:
            return "old space";
        case NON_MOVABLE:
            return "non movable space";
        case MACHINE_CODE_SPACE:
            return "machine code space";
        case HUGE_OBJECT_SPACE:
            return "huge object space";
        case SEMI_SPACE:
            return "semi space";
        case SNAPSHOT_SPACE:
            return "snapshot space";
        case COMPRESS_SPACE:
            return "compress space";
        default:
            return "unknow space";
    }
}

class Space {
public:
    Space(Heap *heap, MemSpaceType spaceType, size_t initialCapacity, size_t maximumCapacity);
    virtual ~Space() = default;
    NO_COPY_SEMANTIC(Space);
    NO_MOVE_SEMANTIC(Space);

    Heap *GetHeap() const
    {
        return heap_;
    }

    size_t GetMaximumCapacity() const
    {
        return maximumCapacity_;
    }

    void SetMaximumCapacity(size_t maximumCapacity)
    {
        maximumCapacity_ = maximumCapacity;
    }

    size_t GetInitialCapacity() const
    {
        return initialCapacity_;
    }

    void SetInitialCapacity(size_t initialCapacity)
    {
        initialCapacity_ = initialCapacity;
    }

    size_t GetCommittedSize() const
    {
        return committedSize_;
    }

    void IncrementCommitted(size_t bytes)
    {
        committedSize_ += bytes;
    }

    void DecrementCommitted(size_t bytes)
    {
        committedSize_ -= bytes;
    }

    void IncrementObjectSize(size_t bytes)
    {
        objectSize_ += bytes;
    }

    void DecrementObjectSize(size_t bytes)
    {
        objectSize_ -= bytes;
    }

    MemSpaceType GetSpaceType() const
    {
        return spaceType_;
    }

    inline RegionFlags GetRegionFlag() const;

    uintptr_t GetAllocateAreaBegin() const
    {
        return regionList_.GetLast()->GetBegin();
    }

    uintptr_t GetAllocateAreaEnd() const
    {
        return regionList_.GetLast()->GetEnd();
    }

    Region *GetCurrentRegion() const
    {
        return regionList_.GetLast();
    }

    uint32_t GetRegionCount()
    {
        return regionList_.GetLength();
    }

    EcmaList<Region> &GetRegionList()
    {
        return regionList_;
    }

    const EcmaList<Region> &GetRegionList() const
    {
        return regionList_;
    }

    void SetRecordRegion()
    {
        recordRegion_ = GetCurrentRegion();
    }

    template <class Callback>
    inline void EnumerateRegions(const Callback &cb, Region *region = nullptr) const;
    template <class Callback>
    inline void EnumerateRegionsWithRecord(const Callback &cb) const;

    inline void AddRegion(Region *region);
    inline void RemoveRegion(Region *region);

    virtual void Initialize() {};
    void Destroy();

    void ReclaimRegions();

    bool ContainObject(TaggedObject *object) const;

protected:
    void ClearAndFreeRegion(Region *region);

    Heap *heap_ {nullptr};
    EcmaVM *vm_ {nullptr};
    JSThread *thread_ {nullptr};
    HeapRegionAllocator *heapRegionAllocator_ {nullptr};
    EcmaList<Region> regionList_ {};
    MemSpaceType spaceType_ {};
    size_t initialCapacity_ {0};
    size_t maximumCapacity_ {0};
    size_t committedSize_ {0};
    size_t objectSize_ {0};
    Region *recordRegion_ {nullptr};
};

class HugeObjectSpace : public Space {
public:
    explicit HugeObjectSpace(Heap *heap, size_t initialCapacity = MAX_HUGE_OBJECT_SPACE_SIZE,
                             size_t maximumCapacity = MAX_HUGE_OBJECT_SPACE_SIZE);
    ~HugeObjectSpace() override = default;
    NO_COPY_SEMANTIC(HugeObjectSpace);
    NO_MOVE_SEMANTIC(HugeObjectSpace);
    uintptr_t Allocate(size_t objectSize);
    void Sweeping();
    size_t GetHeapObjectSize() const;
    void IterateOverObjects(const std::function<void(TaggedObject *object)> &objectVisitor) const;
};
}  // namespace panda::ecmascript
#endif  // ECMASCRIPT_MEM_SPACE_H
