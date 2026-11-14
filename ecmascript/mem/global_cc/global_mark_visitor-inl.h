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

#ifndef ECMASCRIPT_MEM_GLOBAL_CC_GLOBAL_MARK_VISITOR_INL_H
#define ECMASCRIPT_MEM_GLOBAL_CC_GLOBAL_MARK_VISITOR_INL_H

#include "ecmascript/mem/global_cc/global_mark_visitor.h"

#include "ecmascript/js_hclass-inl.h"
#include "ecmascript/linked_hash_table.h"
#include "ecmascript/mem/region-inl.h"
#include "ecmascript/mem/work_manager-inl.h"

namespace panda::ecmascript {
void GlobalCCMarkRootVisitor::VisitRoot([[maybe_unused]] Root type, ObjectSlot slot)
{
    HandleSlot(slot);
}

void GlobalCCMarkRootVisitor::VisitRangeRoot([[maybe_unused]] Root type, ObjectSlot start, ObjectSlot end)
{
    for (ObjectSlot slot = start; slot < end; slot++) {
        HandleSlot(slot);
    }
}

void GlobalCCMarkRootVisitor::HandleSlot(ObjectSlot slot)
{
    JSTaggedValue value(slot.GetTaggedType());
    if (!value.IsHeapObject()) {
        return;
    }
    ASSERT(!value.IsWeak());
    TaggedObject *object = value.GetTaggedObject();
    Region *objectRegion = Region::ObjectAddressToRange(object);
    if (objectRegion->InSharedReadOnlySpace()) {
        return;
    }
    if (objectRegion->AtomicMark(object)) {
        holder_->Push(object);
    }
}

bool GlobalCCMarkObjectVisitor::IsAlive(TaggedObject *obj)
{
    Region *region = Region::ObjectAddressToRange(obj);
    return region->InSharedReadOnlySpace() || region->Test(obj);
}

void GlobalCCMarkObjectVisitor::VisitObjectRangeImpl(BaseObject *rootObject, ObjectSlot start, ObjectSlot end,
                                                     VisitObjectArea area)
{
    auto root = TaggedObject::Cast(rootObject);
    if (UNLIKELY(area == VisitObjectArea::IN_OBJECT)) {
        JSHClass *hclass = root->SynchronizedGetClass();
        ASSERT(!hclass->IsAllTaggedProp());
        int index = 0;
        LayoutInfo *layout = LayoutInfo::UncheckCast(hclass->GetLayout<RBMode::FAST_NO_RB>(nullptr).GetTaggedObject());
        ObjectSlot realEnd = start;
        realEnd += layout->GetPropertiesCapacity();
        end = end > realEnd ? realEnd : end;
        for (ObjectSlot slot = start; slot < end; slot++) {
            PropertyAttributes attr = layout->GetAttr<RBMode::FAST_NO_RB>(nullptr, index++);
            if (attr.IsTaggedRep()) {
                HandleSlot(slot);
            }
        }
        return;
    }
    for (ObjectSlot slot = start; slot < end; slot++) {
        HandleSlot(slot);
    }
}

void GlobalCCMarkObjectVisitor::VisitCompressedObjectRangeImpl(BaseObject *rootObject, CompressedObjectSlot start,
                                                               CompressedObjectSlot end)
{
    for (CompressedObjectSlot slot = start; slot < end; slot++) {
        HandleSlot(slot);
    }
}

void GlobalCCMarkObjectVisitor::VisitObjectHClassImpl([[maybe_unused]] BaseObject *rootObject, BaseObject *hclass)
{
    auto hclassObject = reinterpret_cast<TaggedObject *>(hclass);
    ASSERT(hclassObject->GetClass()->IsHClass());
    Region *hclassRegion = Region::ObjectAddressToRange(hclassObject);
    if (hclassRegion->InSharedReadOnlySpace()) {
        return;
    }
    if (hclassRegion->AtomicMark(hclassObject)) {
        holder_->Push(hclassObject);
    }
}

void GlobalCCMarkObjectVisitor::VisitWeakLinkedHashMapImpl(BaseObject *rootObject)
{
    TaggedObject *obj = TaggedObject::Cast(rootObject);
    ASSERT(JSTaggedValue(obj).IsWeakLinkedHashMap());
    holder_->PushWeakLinkedHashMap(obj);
    WeakLinkedHashMap *map = WeakLinkedHashMap::Cast(obj);
    ASSERT(map->VerifyLayout());
    JSTaggedType *data = map->GetData();
    ObjectSlot nextTableSlot(ToUintPtr(&data[WeakLinkedHashMap::NEXT_TABLE_INDEX]));
    HandleSlot(nextTableSlot);
    int entries = map->NumberOfAllUsedElements();
    for (int i = 0; i < entries; ++i) {
        int keyIdx = map->GetKeyIndex(i);
        ObjectSlot keySlot(ToUintPtr(&data[keyIdx]));
        JSTaggedValue keyValue(keySlot.GetTaggedType());
        if (!keyValue.IsHeapObject()) {
            continue;
        }
        ASSERT(keyValue.IsWeak());
        holder_->PushWeakReference(reinterpret_cast<JSTaggedType *>(keySlot.SlotAddress()));
        int valueIdx = map->GetValueIndex(i);
        ObjectSlot valueSlot(ToUintPtr(&data[valueIdx]));
        JSTaggedValue valueVal(valueSlot.GetTaggedType());
        if (!valueVal.IsHeapObject()) {
            continue;
        }
        TaggedObject *key = keyValue.GetTaggedWeakRef();
        TaggedObject *value = valueVal.GetHeapObject();
        if (!IsAlive(key) && !IsAlive(value)) {
            holder_->PushFreshWeakAggregate({keySlot, valueSlot});
        } else {
            HandleObject(value);
        }
    }
}

template <ReferenceType refType>
void GlobalCCMarkObjectVisitor::HandleSlot(ObjectSlotBase<refType> slot)
{
    TaggedValueType<refType> value = slot.GetTaggedValue();
    if (!value.IsHeapObject()) {
        return;
    }
    if constexpr (ReferenceIsCompressed<refType>) {
        ASSERT(!value.IsWeakForHeapObject());
        HandleObject(value.GetTaggedObject());
    } else {
        if (!value.IsWeakForHeapObject()) {
            HandleObject(value.GetTaggedObject());
        } else {
            RecordWeakReference(reinterpret_cast<JSTaggedType *>(slot.SlotAddress()));
        }
    }
}

void GlobalCCMarkObjectVisitor::HandleObject(TaggedObject *object)
{
    Region *objectRegion = Region::ObjectAddressToRange(object);
    if (objectRegion->InSharedReadOnlySpace()) {
        return;
    }
    if (objectRegion->AtomicMark(object)) {
        holder_->Push(object);
    }
}

void GlobalCCMarkObjectVisitor::RecordWeakReference(JSTaggedType *weak)
{
    holder_->PushWeakReference(weak);
}

}  // namespace panda::ecmascript
#endif  // ECMASCRIPT_MEM_GLOBAL_CC_GLOBAL_MARK_VISITOR_INL_H
