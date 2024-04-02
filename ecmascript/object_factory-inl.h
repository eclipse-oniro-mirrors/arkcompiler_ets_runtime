/*
 * Copyright (c) 2021 Huawei Device Co., Ltd.
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

#ifndef ECMASCRIPT_OBJECT_FACTORY_INL_H
#define ECMASCRIPT_OBJECT_FACTORY_INL_H

#include "ecmascript/global_env_constants-inl.h"
#include "ecmascript/global_env_constants.h"
#include "ecmascript/js_hclass-inl.h"
#include "ecmascript/js_thread.h"
#include "ecmascript/lexical_env.h"
#include "ecmascript/mem/heap-inl.h"
#include "ecmascript/mem/barriers-inl.h"
#include "ecmascript/mem/space.h"
#include "ecmascript/object_factory.h"
#include "ecmascript/tagged_array-inl.h"

namespace panda::ecmascript {
EcmaString *ObjectFactory::AllocLineStringObjectNoGC(size_t size)
{
    TaggedObject *object = nullptr;
    if (size > MAX_REGULAR_HEAP_OBJECT_SIZE) {
        object = reinterpret_cast<TaggedObject *>(sHeap_->GetHugeObjectSpace()->Allocate(thread_, size));
    } else {
        object = reinterpret_cast<TaggedObject *>(sHeap_->GetOldSpace()->Allocate(thread_, size, false));
    }
    object->SetClass(thread_, JSHClass::Cast(thread_->GlobalConstants()->GetLineStringClass().GetTaggedObject()));
    return EcmaString::Cast(object);
}

EcmaString *ObjectFactory::AllocNonMovableLineStringObject(size_t size)
{
    NewSObjectHook();
    return reinterpret_cast<EcmaString *>(sHeap_->AllocateNonMovableOrHugeObject(
        thread_, JSHClass::Cast(thread_->GlobalConstants()->GetLineStringClass().GetTaggedObject()), size));
}

EcmaString *ObjectFactory::AllocLineStringObject(size_t size)
{
    NewSObjectHook();
    return reinterpret_cast<EcmaString *>(sHeap_->AllocateOldOrHugeObject(
        thread_, JSHClass::Cast(thread_->GlobalConstants()->GetLineStringClass().GetTaggedObject()), size));
}

EcmaString *ObjectFactory::AllocOldSpaceLineStringObject(size_t size)
{
    NewSObjectHook();
    return reinterpret_cast<EcmaString *>(sHeap_->AllocateOldOrHugeObject(
        thread_, JSHClass::Cast(thread_->GlobalConstants()->GetLineStringClass().GetTaggedObject()), size));
}

EcmaString *ObjectFactory::AllocReadOnlyLineStringObject(size_t size)
{
    NewSObjectHook();
    return reinterpret_cast<EcmaString *>(sHeap_->AllocateReadOnlyOrHugeObject(
        thread_, JSHClass::Cast(thread_->GlobalConstants()->GetLineStringClass().GetTaggedObject()), size));
}

EcmaString *ObjectFactory::AllocSlicedStringObject(MemSpaceType type)
{
    ASSERT(IsSMemSpace(type));
    NewSObjectHook();
    return reinterpret_cast<EcmaString *>(AllocObjectWithSpaceType(SlicedString::SIZE,
        JSHClass::Cast(thread_->GlobalConstants()->GetSlicedStringClass().GetTaggedObject()), type));
}

EcmaString *ObjectFactory::AllocConstantStringObject(MemSpaceType type)
{
    ASSERT(IsSMemSpace(type));
    NewSObjectHook();
    return reinterpret_cast<EcmaString *>(AllocObjectWithSpaceType(ConstantString::SIZE,
        JSHClass::Cast(thread_->GlobalConstants()->GetConstantStringClass().GetTaggedObject()), type));
}

EcmaString *ObjectFactory::AllocTreeStringObject()
{
    NewSObjectHook();
    return reinterpret_cast<EcmaString *>(sHeap_->AllocateOldOrHugeObject(
        thread_, JSHClass::Cast(thread_->GlobalConstants()->GetTreeStringClass().GetTaggedObject()),
        TreeEcmaString::SIZE));
}

JSHandle<JSNativePointer> ObjectFactory::NewJSNativePointer(void *externalPointer,
                                                            const DeleteEntryPoint &callBack,
                                                            void *data,
                                                            bool nonMovable,
                                                            size_t nativeBindingsize,
                                                            Concurrent isConcurrent,
                                                            NativeFlag flag)
{
    NewObjectHook();
    TaggedObject *header;
    auto jsNativePointerClass = JSHClass::Cast(thread_->GlobalConstants()->GetJSNativePointerClass().GetTaggedObject());
    if (nonMovable) {
        header = heap_->AllocateNonMovableOrHugeObject(jsNativePointerClass);
    } else {
        header = heap_->AllocateOldOrHugeObject(jsNativePointerClass);
    }
    JSHandle<JSNativePointer> obj(thread_, header);
    obj->SetExternalPointer(externalPointer);
    obj->SetDeleter(callBack);
    obj->SetData(data);
    obj->SetBindingSize(nativeBindingsize);
    obj->SetNativeFlag(flag);

    if (callBack != nullptr) {
        heap_->IncreaseNativeBindingSize(nativeBindingsize);
        vm_->PushToNativePointerList(static_cast<JSNativePointer *>(header), isConcurrent);
        // In some cases, the size of JS/TS object is too small and the native binding size is too large.
        // Check and try trigger concurrent mark here.
        heap_->TryTriggerFullMarkByNativeSize();
    }
    return obj;
}

LexicalEnv *ObjectFactory::InlineNewLexicalEnv(int numSlots)
{
    NewObjectHook();
    size_t size = LexicalEnv::ComputeSize(numSlots);
    auto header = heap_->TryAllocateYoungGeneration(
        JSHClass::Cast(thread_->GlobalConstants()->GetEnvClass().GetTaggedObject()), size);
    if (UNLIKELY(header == nullptr)) {
        return nullptr;
    }
    LexicalEnv *array = LexicalEnv::Cast(header);
    array->InitializeWithSpecialValue(JSTaggedValue::Hole(), numSlots + LexicalEnv::RESERVED_ENV_LENGTH);
    return array;
}

template<typename T, typename S>
void ObjectFactory::NewJSIntlIcuData(const JSHandle<T> &obj, const S &icu, const DeleteEntryPoint &callback)
{
    S *icuPoint = vm_->GetNativeAreaAllocator()->New<S>(icu);
    ASSERT(icuPoint != nullptr);
    JSTaggedValue data = obj->GetIcuField();
    if (data.IsHeapObject() && data.IsJSNativePointer()) {
        JSNativePointer *native = JSNativePointer::Cast(data.GetTaggedObject());
        native->ResetExternalPointer(icuPoint);
        return;
    }
    JSHandle<JSNativePointer> pointer = NewJSNativePointer(icuPoint, callback, vm_);
    obj->SetIcuField(thread_, pointer.GetTaggedValue());
}

TaggedObject *ObjectFactory::AllocObjectWithSpaceType(size_t size, JSHClass *cls, MemSpaceType type)
{
    switch (type) {
        case MemSpaceType::SEMI_SPACE:
            return heap_->AllocateYoungOrHugeObject(cls, size);
        case MemSpaceType::OLD_SPACE:
            return heap_->AllocateOldOrHugeObject(cls, size);
        case MemSpaceType::NON_MOVABLE:
            return heap_->AllocateNonMovableOrHugeObject(cls, size);
        case MemSpaceType::SHARED_OLD_SPACE:
            return sHeap_->AllocateOldOrHugeObject(thread_, cls, size);
        case MemSpaceType::SHARED_NON_MOVABLE:
            return sHeap_->AllocateNonMovableOrHugeObject(thread_, cls, size);
        default:
            LOG_ECMA(FATAL) << "this branch is unreachable";
            UNREACHABLE();
    }
}
}  // namespace panda::ecmascript
#endif  // ECMASCRIPT_OBJECT_FACTORY_INL_H
