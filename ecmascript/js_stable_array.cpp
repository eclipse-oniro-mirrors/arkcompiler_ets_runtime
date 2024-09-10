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

#include "ecmascript/js_stable_array.h"

#include "ecmascript/base/array_helper.h"
#include "ecmascript/base/builtins_base.h"
#include "ecmascript/base/typed_array_helper-inl.h"
#include "ecmascript/builtins/builtins_arraybuffer.h"
#include "ecmascript/builtins/builtins_sendable_arraybuffer.h"
#include "ecmascript/ecma_macros.h"
#include "ecmascript/ecma_vm.h"
#include "ecmascript/ecma_string-inl.h"
#include "ecmascript/element_accessor.h"
#include "ecmascript/element_accessor-inl.h"
#include "ecmascript/global_env.h"
#include "ecmascript/interpreter/fast_runtime_stub-inl.h"
#include "ecmascript/js_array.h"
#include "ecmascript/js_hclass.h"
#include "ecmascript/js_tagged_value-inl.h"
#include "ecmascript/js_tagged_value.h"
#include "ecmascript/object_factory.h"
#include "ecmascript/shared_objects/js_shared_array.h"
#include "ecmascript/tagged_array.h"
#include "macros.h"

namespace panda::ecmascript {
using TypedArrayHelper = base::TypedArrayHelper;
using TypedArrayKind = base::TypedArrayKind;
using BuiltinsArrayBuffer = builtins::BuiltinsArrayBuffer;
using BuiltinsSendableArrayBuffer = builtins::BuiltinsSendableArrayBuffer;
template<TypedArrayKind typedArrayKind>
using BuiltinsArrayBufferType = base::BuiltinsArrayBufferType<typedArrayKind>;

JSTaggedValue JSStableArray::Push(JSHandle<JSSharedArray> receiver, EcmaRuntimeCallInfo *argv)
{
    JSThread *thread = argv->GetThread();
    uint32_t argc = argv->GetArgsNumber();
    uint32_t oldLength = receiver->GetArrayLength();
    uint32_t newLength = argc + oldLength;
    JSHandle<JSObject> thisObjHandle(receiver);

    TaggedArray *elements = TaggedArray::Cast(receiver->GetElements().GetTaggedObject());
    if (newLength > ElementAccessor::GetElementsLength(thisObjHandle)) {
        elements = *JSObject::GrowElementsCapacity(thread, JSHandle<JSObject>::Cast(receiver), newLength, true);
    }
    bool needTransition = true;
    for (uint32_t k = 0; k < argc; k++) {
        JSHandle<JSTaggedValue> value = argv->GetCallArg(k);
        ElementAccessor::Set(thread, thisObjHandle, oldLength + k, value, needTransition);
    }
    receiver->SetArrayLength(thread, newLength);

    return JSTaggedValue(newLength);
}

JSTaggedValue JSStableArray::Push(JSHandle<JSArray> receiver, EcmaRuntimeCallInfo *argv)
{
    JSThread *thread = argv->GetThread();
    uint32_t argc = argv->GetArgsNumber();
    uint32_t oldLength = receiver->GetArrayLength();
    uint32_t newLength = argc + oldLength;
    JSHandle<JSObject> thisObjHandle(receiver);

    TaggedArray *elements = TaggedArray::Cast(receiver->GetElements().GetTaggedObject());
    if (newLength > ElementAccessor::GetElementsLength(thisObjHandle)) {
        elements = *JSObject::GrowElementsCapacity(thread, JSHandle<JSObject>::Cast(receiver), newLength, true);
    }
    bool needTransition = true;
    for (uint32_t k = 0; k < argc; k++) {
        JSHandle<JSTaggedValue> value = argv->GetCallArg(k);
        ElementAccessor::Set(thread, thisObjHandle, oldLength + k, value, needTransition);
    }
    receiver->SetArrayLength(thread, newLength);

    return JSTaggedValue(newLength);
}

JSTaggedValue JSStableArray::Pop(JSHandle<JSSharedArray> receiver, EcmaRuntimeCallInfo *argv)
{
    JSThread *thread = argv->GetThread();
    uint32_t length = receiver->GetArrayLength();
    if (length == 0) {
        return JSTaggedValue::Undefined();
    }
    JSHandle<JSTaggedValue> holeHandle(thread, JSTaggedValue::Hole());
    JSSharedArray::CheckAndCopyArray(thread, receiver);
    JSHandle<JSObject> obj(receiver);
    uint32_t capacity = ElementAccessor::GetElementsLength(obj);
    uint32_t index = length - 1;
    JSMutableHandle<JSTaggedValue> result(thread, JSTaggedValue::Hole());
    if (index < capacity) {
        result.Update(ElementAccessor::Get(obj, index));
    }
    if (!result->IsHole()) {
        if (TaggedArray::ShouldTrim(capacity, index)) {
            TaggedArray *elements = TaggedArray::Cast(receiver->GetElements().GetTaggedObject());
            elements->Trim(thread, index);
        } else {
            ElementAccessor::Set(thread, obj, index, holeHandle, false);
        }
    } else {
        JSHandle<JSTaggedValue> thisObjVal(receiver);
        result.Update(JSArray::FastGetPropertyByValue(thread, thisObjVal, index).GetTaggedValue());
    }
    receiver->SetArrayLength(thread, index);
    return result->IsHole() ? JSTaggedValue::Undefined() : result.GetTaggedValue();
}

JSTaggedValue JSStableArray::Pop(JSHandle<JSArray> receiver, EcmaRuntimeCallInfo *argv)
{
    JSThread *thread = argv->GetThread();
    uint32_t length = receiver->GetArrayLength();
    if (length == 0) {
        return JSTaggedValue::Undefined();
    }
    JSHandle<JSTaggedValue> holeHandle(thread, JSTaggedValue::Hole());
    JSArray::CheckAndCopyArray(thread, receiver);
    JSHandle<JSObject> obj(receiver);
    uint32_t capacity = ElementAccessor::GetElementsLength(obj);
    uint32_t index = length - 1;
    JSMutableHandle<JSTaggedValue> result(thread, JSTaggedValue::Hole());
    if (index < capacity) {
        result.Update(ElementAccessor::Get(obj, index));
    }
    if (!result->IsHole()) {
        if (TaggedArray::ShouldTrim(capacity, index)) {
            TaggedArray *elements = TaggedArray::Cast(receiver->GetElements().GetTaggedObject());
            elements->Trim(thread, index);
        } else {
            ElementAccessor::Set(thread, obj, index, holeHandle, false);
        }
    } else {
        JSHandle<JSTaggedValue> thisObjVal(receiver);
        result.Update(JSArray::FastGetPropertyByValue(thread, thisObjVal, index).GetTaggedValue());
    }
    receiver->SetArrayLength(thread, index);
    return result->IsHole() ? JSTaggedValue::Undefined() : result.GetTaggedValue();
}

void JSStableArray::HandleArray(JSHandle<JSObject> &newArrayHandle, uint32_t &actualDeleteCount,
                                JSThread *thread, uint32_t &start, JSHandle<JSObject> &thisObjHandle,
                                JSHandle<JSTaggedValue> &holeHandle)
{
    TaggedArray *destElements = TaggedArray::Cast(newArrayHandle->GetElements().GetTaggedObject());
        if (actualDeleteCount > ElementAccessor::GetElementsLength(newArrayHandle)) {
            destElements = *JSObject::GrowElementsCapacity(thread, newArrayHandle, actualDeleteCount);
        }

        for (uint32_t idx = 0; idx < actualDeleteCount; idx++) {
            if ((start + idx) >= ElementAccessor::GetElementsLength(thisObjHandle)) {
                ElementAccessor::Set(thread, newArrayHandle, idx, holeHandle, true);
            } else {
                JSHandle<JSTaggedValue> valueHandle(thread, ElementAccessor::Get(thisObjHandle, start + idx));
                ElementAccessor::Set(thread, newArrayHandle, idx, valueHandle, true);
            }
        }
        JSHandle<JSArray>::Cast(newArrayHandle)->SetArrayLength(thread, actualDeleteCount);
}

JSTaggedValue JSStableArray::UpdateArrayCapacity(JSHandle<JSObject> &thisObjHandle, uint32_t &len,
                                                 uint32_t &insertCount, uint32_t &actualDeleteCount,
                                                 JSHandle<JSArray> &receiver, uint32_t &start,
                                                 JSThread *thread, bool &needTransition,
                                                 JSHandle<JSTaggedValue> &holeHandle,
                                                 EcmaRuntimeCallInfo *argv, JSHandle<JSTaggedValue> &thisObjVal,
                                                 JSHandle<JSTaggedValue> &lengthKey)
{
    uint32_t oldCapacity = ElementAccessor::GetElementsLength(thisObjHandle);
    ASSERT(len + insertCount >= actualDeleteCount);
    uint32_t newCapacity = len - actualDeleteCount + insertCount;
    TaggedArray *srcElements = TaggedArray::Cast(thisObjHandle->GetElements().GetTaggedObject());
    JSMutableHandle<TaggedArray> srcElementsHandle(thread, srcElements);
    uint32_t argc = argv->GetArgsNumber();
    if (newCapacity > oldCapacity) {
        srcElementsHandle.Update(JSObject::GrowElementsCapacity(thread, thisObjHandle, newCapacity));
    }
    if (insertCount < actualDeleteCount) {
        JSArray::CheckAndCopyArray(thread, receiver);
        srcElementsHandle.Update(receiver->GetElements());
        for (uint32_t idx = start; idx < len - actualDeleteCount; idx++) {
            JSMutableHandle<JSTaggedValue> element(thread, JSTaggedValue::Hole());
            if ((idx + actualDeleteCount) < ElementAccessor::GetElementsLength(thisObjHandle)) {
                element.Update(ElementAccessor::Get(thisObjHandle, idx + actualDeleteCount));
            }
            if ((idx + insertCount) < ElementAccessor::GetElementsLength(thisObjHandle)) {
                ElementAccessor::Set(thread, thisObjHandle, idx + insertCount, element, needTransition);
            }
        }

        if ((oldCapacity > newCapacity) && TaggedArray::ShouldTrim(oldCapacity, newCapacity)) {
            srcElementsHandle->Trim(thread, newCapacity);
        } else {
            for (uint32_t idx = newCapacity; idx < len; idx++) {
                if (idx < ElementAccessor::GetElementsLength(thisObjHandle)) {
                    ElementAccessor::Set(thread, thisObjHandle, idx, holeHandle, needTransition);
                }
            }
        }
    } else {
        ASSERT(len >= actualDeleteCount);
        for (uint32_t idx = len - actualDeleteCount; idx > start; idx--) {
            JSHandle<JSTaggedValue> element(thread, ElementAccessor::Get(thisObjHandle, idx + actualDeleteCount - 1));
            ElementAccessor::Set(thread, thisObjHandle, idx + insertCount - 1, element, needTransition);
        }
    }

    for (uint32_t i = 2, idx = start; i < argc; i++, idx++) {
        ElementAccessor::Set(thread, thisObjHandle, idx, argv->GetCallArg(i), needTransition);
    }

    JSHandle<JSTaggedValue> newLenHandle(thread, JSTaggedValue(newCapacity));
    JSTaggedValue::SetProperty(thread, thisObjVal, lengthKey, newLenHandle, true);
    RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
    return JSTaggedValue::Undefined();
}

JSTaggedValue JSStableArray::Splice(JSHandle<JSArray> receiver, EcmaRuntimeCallInfo *argv,
                                    uint32_t start, uint32_t insertCount, uint32_t actualDeleteCount,
                                    JSHandle<JSObject> newArrayHandle, uint32_t len)
{
    JSThread *thread = argv->GetThread();

    JSHandle<JSTaggedValue> holeHandle(thread, JSTaggedValue::Hole());
    JSHandle<JSObject> thisObjHandle(receiver);
    JSHandle<JSTaggedValue> thisObjVal(thisObjHandle);
    JSArray::CheckAndCopyArray(thread, receiver);
    JSHandle<JSTaggedValue> lengthKey = thread->GlobalConstants()->GetHandledLengthString();
    bool needTransition = true;
    if (newArrayHandle.GetTaggedValue().IsStableJSArray(thread)) {
        HandleArray(newArrayHandle, actualDeleteCount, thread, start, thisObjHandle, holeHandle);
    } else {
        JSMutableHandle<JSTaggedValue> fromKey(thread, JSTaggedValue::Undefined());
        JSMutableHandle<JSTaggedValue> toKey(thread, JSTaggedValue::Undefined());
        uint32_t k = 0;
        while (k < actualDeleteCount) {
            uint32_t from = start + k;
            fromKey.Update(JSTaggedValue(from));
            bool exists = JSTaggedValue::HasProperty(thread, thisObjVal, fromKey);
            RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
            if (exists) {
                JSHandle<JSTaggedValue> fromValue = JSArray::FastGetPropertyByValue(thread, thisObjVal, fromKey);
                RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
                toKey.Update(JSTaggedValue(k));
                if (newArrayHandle->IsJSProxy()) {
                    toKey.Update(JSTaggedValue::ToString(thread, toKey).GetTaggedValue());
                    RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
                }
                JSObject::CreateDataPropertyOrThrow(thread, newArrayHandle, toKey, fromValue);
                RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
            }
            k++;
        }

        JSHandle<JSTaggedValue> deleteCount(thread, JSTaggedValue(actualDeleteCount));
        JSTaggedValue::SetProperty(thread, JSHandle<JSTaggedValue>::Cast(newArrayHandle), lengthKey, deleteCount,
                                   true);
        RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
    }
    UpdateArrayCapacity(thisObjHandle, len, insertCount, actualDeleteCount, receiver, start,
                        thread, needTransition, holeHandle, argv, thisObjVal, lengthKey);
    RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
    return newArrayHandle.GetTaggedValue();
}

JSTaggedValue JSStableArray::Splice(JSHandle<JSSharedArray> receiver, EcmaRuntimeCallInfo *argv,
                                    uint32_t start, uint32_t insertCount, uint32_t actualDeleteCount,
                                    JSHandle<JSObject> newArrayHandle, uint32_t len)
{
    JSThread *thread = argv->GetThread();
    uint32_t argc = argv->GetArgsNumber();

    JSHandle<JSTaggedValue> holeHandle(thread, JSTaggedValue::Hole());
    JSHandle<JSObject> thisObjHandle(receiver);
    JSHandle<JSTaggedValue> thisObjVal(thisObjHandle);
    JSSharedArray::CheckAndCopyArray(thread, receiver);
    JSHandle<JSTaggedValue> lengthKey = thread->GlobalConstants()->GetHandledLengthString();
    TaggedArray *srcElements = TaggedArray::Cast(thisObjHandle->GetElements().GetTaggedObject());
    JSMutableHandle<TaggedArray> srcElementsHandle(thread, srcElements);
    bool needTransition = true;
    if (newArrayHandle.GetTaggedValue().IsStableJSArray(thread)) {
        TaggedArray *destElements = TaggedArray::Cast(newArrayHandle->GetElements().GetTaggedObject());
        if (actualDeleteCount > ElementAccessor::GetElementsLength(newArrayHandle)) {
            destElements = *JSObject::GrowElementsCapacity(thread, newArrayHandle, actualDeleteCount);
        }

        for (uint32_t idx = 0; idx < actualDeleteCount; idx++) {
            if ((start + idx) >= ElementAccessor::GetElementsLength(thisObjHandle)) {
                ElementAccessor::Set(thread, newArrayHandle, idx, holeHandle, needTransition);
            } else {
                JSHandle<JSTaggedValue> valueHandle(thread, ElementAccessor::Get(thisObjHandle, start + idx));
                ElementAccessor::Set(thread, newArrayHandle, idx, valueHandle, needTransition);
            }
        }
        JSHandle<JSSharedArray>::Cast(newArrayHandle)->SetArrayLength(thread, actualDeleteCount);
    } else {
        JSMutableHandle<JSTaggedValue> fromKey(thread, JSTaggedValue::Undefined());
        JSMutableHandle<JSTaggedValue> toKey(thread, JSTaggedValue::Undefined());
        uint32_t k = 0;
        while (k < actualDeleteCount) {
            uint32_t from = start + k;
            fromKey.Update(JSTaggedValue(from));
            bool exists = JSTaggedValue::HasProperty(thread, thisObjVal, fromKey);
            RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
            if (exists) {
                JSHandle<JSTaggedValue> fromValue = JSArray::FastGetPropertyByValue(thread, thisObjVal, fromKey);
                RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
                toKey.Update(JSTaggedValue(k));
                if (newArrayHandle->IsJSProxy()) {
                    toKey.Update(JSTaggedValue::ToString(thread, toKey).GetTaggedValue());
                    RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
                }
                JSObject::CreateDataPropertyOrThrow(thread, newArrayHandle, toKey, fromValue);
                RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
            }
            k++;
        }

        JSHandle<JSTaggedValue> deleteCount(thread, JSTaggedValue(actualDeleteCount));
        JSTaggedValue::SetProperty(thread, JSHandle<JSTaggedValue>::Cast(newArrayHandle), lengthKey, deleteCount,
                                   true);
        RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
    }
    uint32_t oldCapacity = ElementAccessor::GetElementsLength(thisObjHandle);
    ASSERT(len + insertCount >= actualDeleteCount);
    uint32_t newCapacity = len - actualDeleteCount + insertCount;
    if (newCapacity > oldCapacity) {
        srcElementsHandle.Update(JSObject::GrowElementsCapacity(thread, thisObjHandle, newCapacity));
    }
    if (insertCount < actualDeleteCount) {
        JSSharedArray::CheckAndCopyArray(thread, receiver);
        srcElementsHandle.Update(receiver->GetElements());
        for (uint32_t idx = start; idx < len - actualDeleteCount; idx++) {
            JSMutableHandle<JSTaggedValue> element(thread, JSTaggedValue::Hole());
            if ((idx + actualDeleteCount) < ElementAccessor::GetElementsLength(thisObjHandle)) {
                element.Update(ElementAccessor::Get(thisObjHandle, idx + actualDeleteCount));
            }
            if ((idx + insertCount) < ElementAccessor::GetElementsLength(thisObjHandle)) {
                ElementAccessor::Set(thread, thisObjHandle, idx + insertCount, element, needTransition);
            }
        }

        if ((oldCapacity > newCapacity) && TaggedArray::ShouldTrim(oldCapacity, newCapacity)) {
            srcElementsHandle->Trim(thread, newCapacity);
        } else {
            for (uint32_t idx = newCapacity; idx < len; idx++) {
                if (idx < ElementAccessor::GetElementsLength(thisObjHandle)) {
                    ElementAccessor::Set(thread, thisObjHandle, idx, holeHandle, needTransition);
                }
            }
        }
    } else {
        ASSERT(len >= actualDeleteCount);
        for (uint32_t idx = len - actualDeleteCount; idx > start; idx--) {
            JSHandle<JSTaggedValue> element(thread, ElementAccessor::Get(thisObjHandle, idx + actualDeleteCount - 1));
            ElementAccessor::Set(thread, thisObjHandle, idx + insertCount - 1, element, needTransition);
        }
    }

    for (uint32_t i = 2, idx = start; i < argc; i++, idx++) {
        ElementAccessor::Set(thread, thisObjHandle, idx, argv->GetCallArg(i), needTransition);
    }

    JSHandle<JSTaggedValue> newLenHandle(thread, JSTaggedValue(newCapacity));
    JSTaggedValue::SetProperty(thread, thisObjVal, lengthKey, newLenHandle, true);
    RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
    return newArrayHandle.GetTaggedValue();
}

JSTaggedValue JSStableArray::Shift(JSHandle<JSSharedArray> receiver, EcmaRuntimeCallInfo *argv)
{
    JSThread *thread = argv->GetThread();
    JSHandle<JSObject> thisObjHandle(receiver);
    JSHandle<JSTaggedValue> holeHandle(thread, JSTaggedValue::Hole());
    uint32_t length = receiver->GetArrayLength();
    if (length == 0) {
        return JSTaggedValue::Undefined();
    }
    JSSharedArray::CheckAndCopyArray(thread, receiver);
    TaggedArray *elements = TaggedArray::Cast(receiver->GetElements().GetTaggedObject());
    JSHandle<JSTaggedValue> result(thread, ElementAccessor::Get(thisObjHandle, 0));
    bool needTransition = false;
    for (uint32_t k = 1; k < length; k++) {
        JSHandle<JSTaggedValue> kValue(thread, ElementAccessor::Get(thisObjHandle, k));
        ElementAccessor::Set(thread, thisObjHandle, k - 1, kValue, needTransition);
    }
    uint32_t capacity = ElementAccessor::GetElementsLength(thisObjHandle);
    uint32_t index = length - 1;
    if (TaggedArray::ShouldTrim(capacity, index)) {
        elements->Trim(thread, index);
    } else {
        ElementAccessor::Set(thread, thisObjHandle, index, holeHandle, needTransition);
    }
    receiver->SetArrayLength(thread, index);
    return result->IsHole() ? JSTaggedValue::Undefined() : result.GetTaggedValue();
}

JSTaggedValue JSStableArray::Shift(JSHandle<JSArray> receiver, EcmaRuntimeCallInfo *argv)
{
    JSThread *thread = argv->GetThread();
    JSHandle<JSObject> thisObjHandle(receiver);
    JSHandle<JSTaggedValue> holeHandle(thread, JSTaggedValue::Hole());
    uint32_t length = receiver->GetArrayLength();
    if (length == 0) {
        return JSTaggedValue::Undefined();
    }
    JSArray::CheckAndCopyArray(thread, receiver);
    JSHandle<TaggedArray> elements(thread, TaggedArray::Cast(receiver->GetElements().GetTaggedObject()));
    JSHandle<JSTaggedValue> result(thread, ElementAccessor::Get(thisObjHandle, 0));
    bool needTransition = false;
    for (uint32_t k = 1; k < length; k++) {
        JSHandle<JSTaggedValue> kValue(thread, ElementAccessor::Get(thisObjHandle, k));
        ElementAccessor::Set(thread, thisObjHandle, k - 1, kValue, needTransition);
    }
    uint32_t capacity = ElementAccessor::GetElementsLength(thisObjHandle);
    uint32_t index = length - 1;
    if (TaggedArray::ShouldTrim(capacity, index)) {
        elements->Trim(thread, index);
    } else {
        ElementAccessor::Set(thread, thisObjHandle, index, holeHandle, needTransition);
    }
    receiver->SetArrayLength(thread, index);
    return result->IsHole() ? JSTaggedValue::Undefined() : result.GetTaggedValue();
}

void JSStableArray::SetSepValue(JSHandle<EcmaString> sepStringHandle, int &sep, uint32_t &sepLength)
{
    if (EcmaStringAccessor(sepStringHandle).IsUtf8() && EcmaStringAccessor(sepStringHandle).GetLength() == 1) {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
        sep = EcmaStringAccessor(sepStringHandle).Get(0);
    } else if (EcmaStringAccessor(sepStringHandle).GetLength() == 0) {
        sep = JSStableArray::SeparatorFlag::MINUS_TWO;
        sepLength = 0;
    } else {
        sep = JSStableArray::SeparatorFlag::MINUS_ONE;
        sepLength = EcmaStringAccessor(sepStringHandle).GetLength();
    }
}

JSTaggedValue JSStableArray::Join(JSThread *thread, JSHandle<JSArray> receiver,
                                  JSHandle<EcmaString> sepStringHandle, int64_t length)
{
    int sep = ',';
    uint32_t sepLength = 1;
    auto context = thread->GetCurrentEcmaContext();
    JSHandle<JSTaggedValue> receiverValue = JSHandle<JSTaggedValue>::Cast(receiver);
    SetSepValue(sepStringHandle, sep, sepLength);
    auto factory = thread->GetEcmaVM()->GetFactory();
    bool noCircular = context->JoinStackPushFastPath(receiverValue);
    if (!noCircular) {
        return factory->GetEmptyString().GetTaggedValue();
    }
    if (length == 0) {
        const GlobalEnvConstants *globalConst = thread->GlobalConstants();
        context->JoinStackPopFastPath(receiverValue);
        return globalConst->GetEmptyString();
    }
    JSHandle<JSObject> obj(thread, receiverValue.GetTaggedValue());
    uint64_t allocateLength = 0;
    bool isOneByte = (sep != JSStableArray::SeparatorFlag::MINUS_ONE) || EcmaStringAccessor(sepStringHandle).IsUtf8();
    CVector<JSHandle<EcmaString>> vec;
    JSMutableHandle<JSTaggedValue> elementHandle(thread, JSTaggedValue::Undefined());
    const GlobalEnvConstants *globalConst = thread->GlobalConstants();
    uint32_t elementsLength = ElementAccessor::GetElementsLength(obj);
    uint32_t len = elementsLength > length ? length : elementsLength;
    if (elementsLength == 0 && length != 0) {
        len = length;
    }
    if (len <= 1) {
        // sep unused, set isOneByte to default(true)
        isOneByte = true;
    }
    for (uint32_t k = 0; k < len; k++) {
        JSTaggedValue element = JSTaggedValue::Undefined();
        if (k < elementsLength) {
            element = ElementAccessor::Get(obj, k);
            if (element.IsHole() && JSTaggedValue::HasProperty(thread, receiverValue, k)) {
                element = JSArray::FastGetPropertyByValue(thread, receiverValue, k).GetTaggedValue();
                RETURN_EXCEPTION_AND_POP_JOINSTACK(thread, receiverValue);
            }
        }
        if (!element.IsUndefinedOrNull() && !element.IsHole()) {
            if (!element.IsString()) {
                elementHandle.Update(element);
                JSHandle<EcmaString> strElement = JSTaggedValue::ToString(thread, elementHandle);
                RETURN_EXCEPTION_AND_POP_JOINSTACK(thread, receiverValue);
                element = strElement.GetTaggedValue();
            }
            auto nextStr = EcmaString::Cast(element.GetTaggedObject());
            JSHandle<EcmaString> nextStrHandle(thread, nextStr);
            vec.push_back(nextStrHandle);
            isOneByte = EcmaStringAccessor(nextStr).IsUtf8() ? isOneByte : false;
            allocateLength += EcmaStringAccessor(nextStr).GetLength();
        } else {
            vec.push_back(JSHandle<EcmaString>(globalConst->GetHandledEmptyString()));
        }
    }
    if (len > 0) {
        allocateLength += static_cast<uint64_t>(sepLength) * (len - 1);
    }
    if (allocateLength > EcmaString::MAX_STRING_LENGTH) {
        context->JoinStackPopFastPath(receiverValue);
        THROW_RANGE_ERROR_AND_RETURN(thread, "Invalid string length", JSTaggedValue::Exception());
    }
    auto newString =
    EcmaStringAccessor::CreateLineString(thread->GetEcmaVM(), static_cast<size_t>(allocateLength), isOneByte);
    int current = 0;
    DISALLOW_GARBAGE_COLLECTION;
    for (uint32_t k = 0; k < len; k++) {
        if (k > 0) {
            if (sep >= 0) {
                EcmaStringAccessor(newString).Set(current, static_cast<uint16_t>(sep));
            } else if (sep != JSStableArray::SeparatorFlag::MINUS_TWO) {
                EcmaStringAccessor::ReadData(newString, *sepStringHandle, current,
                    allocateLength - static_cast<uint32_t>(current), sepLength);
            }
            current += static_cast<int>(sepLength);
        }
        JSHandle<EcmaString> nextStr = vec[k];
        int nextLength = static_cast<int>(EcmaStringAccessor(nextStr).GetLength());
        EcmaStringAccessor::ReadData(newString, *nextStr, current,
            allocateLength - static_cast<uint32_t>(current), nextLength);
        current += nextLength;
    }
    ASSERT_PRINT(
        isOneByte == EcmaStringAccessor::CanBeCompressed(newString), "isOneByte does not match the real value!");
    context->JoinStackPopFastPath(receiverValue);
    return JSTaggedValue(newString);
}

JSTaggedValue JSStableArray::Join(JSHandle<JSSharedArray> receiver, EcmaRuntimeCallInfo *argv)
{
    JSThread *thread = argv->GetThread();
    uint32_t length = receiver->GetArrayLength();
    JSHandle<JSTaggedValue> sepHandle = base::BuiltinsBase::GetCallArg(argv, 0);
    int sep = ',';
    uint32_t sepLength = 1;
    JSHandle<EcmaString> sepStringHandle;
    auto context = thread->GetCurrentEcmaContext();
    JSHandle<JSTaggedValue> receiverValue = JSHandle<JSTaggedValue>::Cast(receiver);
    if (!sepHandle->IsUndefined()) {
        if (sepHandle->IsString()) {
            sepStringHandle = JSHandle<EcmaString>::Cast(sepHandle);
        } else {
            sepStringHandle = JSTaggedValue::ToString(thread, sepHandle);
            RETURN_EXCEPTION_AND_POP_JOINSTACK(thread, receiverValue);
        }
        SetSepValue(sepStringHandle, sep, sepLength);
    }
    if (length == 0) {
        const GlobalEnvConstants *globalConst = thread->GlobalConstants();
        context->JoinStackPopFastPath(receiverValue);
        return globalConst->GetEmptyString();
    }
    JSHandle<JSObject> obj(thread, receiverValue.GetTaggedValue());
    size_t allocateLength = 0;
    bool isOneByte = (sep != JSStableArray::SeparatorFlag::MINUS_ONE) || EcmaStringAccessor(sepStringHandle).IsUtf8();
    CVector<JSHandle<EcmaString>> vec;
    JSMutableHandle<JSTaggedValue> elementHandle(thread, JSTaggedValue::Undefined());
    const GlobalEnvConstants *globalConst = thread->GlobalConstants();
    uint32_t elementsLength = ElementAccessor::GetElementsLength(obj);
    uint32_t len = elementsLength > length ? length : elementsLength;
    if (elementsLength == 0 && length != 0) {
        len = length;
    }
    if (len <= 1) {
        // sep unused, set isOneByte to default(true)
        isOneByte = true;
    }
    for (uint32_t k = 0; k < len; k++) {
        JSTaggedValue element = JSTaggedValue::Undefined();
        if (k < elementsLength) {
            element = ElementAccessor::Get(obj, k);
        }
        if (!element.IsUndefinedOrNull() && !element.IsHole()) {
            if (!element.IsString()) {
                elementHandle.Update(element);
                JSHandle<EcmaString> strElement = JSTaggedValue::ToString(thread, elementHandle);
                RETURN_EXCEPTION_AND_POP_JOINSTACK(thread, receiverValue);
                element = strElement.GetTaggedValue();
            }
            auto nextStr = EcmaString::Cast(element.GetTaggedObject());
            JSHandle<EcmaString> nextStrHandle(thread, nextStr);
            vec.push_back(nextStrHandle);
            isOneByte = EcmaStringAccessor(nextStr).IsUtf8() ? isOneByte : false;
            allocateLength += EcmaStringAccessor(nextStr).GetLength();
        } else {
            vec.push_back(JSHandle<EcmaString>(globalConst->GetHandledEmptyString()));
        }
    }
    if (len > 0) {
        allocateLength += sepLength * (len - 1);
    }
    auto newString = EcmaStringAccessor::CreateLineString(thread->GetEcmaVM(), allocateLength, isOneByte);
    int current = 0;
    DISALLOW_GARBAGE_COLLECTION;
    for (uint32_t k = 0; k < len; k++) {
        if (k > 0) {
            if (sep >= 0) {
                EcmaStringAccessor(newString).Set(current, static_cast<uint16_t>(sep));
            } else if (sep != JSStableArray::SeparatorFlag::MINUS_TWO) {
                EcmaStringAccessor::ReadData(newString, *sepStringHandle, current,
                    allocateLength - static_cast<uint32_t>(current), sepLength);
            }
            current += static_cast<int>(sepLength);
        }
        JSHandle<EcmaString> nextStr = vec[k];
        int nextLength = static_cast<int>(EcmaStringAccessor(nextStr).GetLength());
        EcmaStringAccessor::ReadData(newString, *nextStr, current,
            allocateLength - static_cast<uint32_t>(current), nextLength);
        current += nextLength;
    }
    ASSERT_PRINT(
        isOneByte == EcmaStringAccessor::CanBeCompressed(newString), "isOneByte does not match the real value!");
    context->JoinStackPopFastPath(receiverValue);
    return JSTaggedValue(newString);
}

JSTaggedValue JSStableArray::HandleFindIndexOfStable(JSThread *thread, JSHandle<JSObject> thisObjHandle,
                                                     JSHandle<JSTaggedValue> callbackFnHandle,
                                                     JSHandle<JSTaggedValue> thisArgHandle, uint32_t &k)
{
    JSHandle<JSTaggedValue> thisObjVal(thisObjHandle);
    JSHandle<JSTaggedValue> undefined = thread->GlobalConstants()->GetHandledUndefined();
    uint64_t len = static_cast<uint64_t>(base::ArrayHelper::GetArrayLength(thread, thisObjVal));
    RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
    JSTaggedValue callResult = base::BuiltinsBase::GetTaggedBoolean(false);
    const int32_t argsLength = 3; // 3: ?kValue, k, O?
    JSMutableHandle<JSTaggedValue> kValue(thread, JSTaggedValue::Undefined());
    while (k < len) {
        // Elements of thisObjHandle may change.
        JSTaggedValue val = ElementAccessor::Get(thisObjHandle, k);
        if (val.IsHole()) {
            auto res = JSArray::FastGetPropertyByValue(thread, thisObjVal, k).GetTaggedValue();
            if (res.IsHole()) {
                kValue.Update(JSTaggedValue::Undefined());
            } else {
                kValue.Update(res);
            }
        } else {
            kValue.Update(val);
        }
        EcmaRuntimeCallInfo *info =
            EcmaInterpreter::NewRuntimeCallInfo(thread, callbackFnHandle, thisArgHandle, undefined, argsLength);
        RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
        info->SetCallArg(kValue.GetTaggedValue(), JSTaggedValue(k), thisObjVal.GetTaggedValue());
        callResult = JSFunction::Call(info);
        RETURN_VALUE_IF_ABRUPT_COMPLETION(thread, callResult);
        if (callResult.ToBoolean()) {
            return callResult;
        }
        if (ElementAccessor::GetElementsLength(thisObjHandle) < len) {
            len = ElementAccessor::GetElementsLength(thisObjHandle);
        }
        k++;
        if (!thisObjVal->IsStableJSArray(thread)) {
            return callResult;
        }
    }
    return callResult;
}

JSTaggedValue JSStableArray::HandleFindLastIndexOfStable(JSThread *thread, JSHandle<JSObject> thisObjHandle,
                                                         JSHandle<JSTaggedValue> callbackFnHandle,
                                                         JSHandle<JSTaggedValue> thisArgHandle, int64_t &k)
{
    JSHandle<JSTaggedValue> thisObjVal(thisObjHandle);
    JSHandle<JSTaggedValue> undefined = thread->GlobalConstants()->GetHandledUndefined();
    JSTaggedValue callResult = base::BuiltinsBase::GetTaggedBoolean(false);
    const int32_t argsLength = 3; // 3: ?kValue, k, O?
    JSMutableHandle<JSTaggedValue> kValue(thread, JSTaggedValue::Undefined());
    while (k >= 0) {
        // Elements of thisObjHandle may change.
        JSTaggedValue val = ElementAccessor::Get(thisObjHandle, k);
        if (val.IsHole()) {
            auto res = JSArray::FastGetPropertyByValue(thread, thisObjVal, k).GetTaggedValue();
            if (res.IsHole()) {
                kValue.Update(JSTaggedValue::Undefined());
            } else {
                kValue.Update(res);
            }
        } else {
            kValue.Update(val);
        }
        EcmaRuntimeCallInfo *info =
            EcmaInterpreter::NewRuntimeCallInfo(thread, callbackFnHandle, thisArgHandle, undefined, argsLength);
        RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
        info->SetCallArg(kValue.GetTaggedValue(), JSTaggedValue(k), thisObjVal.GetTaggedValue());
        callResult = JSFunction::Call(info);
        RETURN_VALUE_IF_ABRUPT_COMPLETION(thread, callResult);
        if (callResult.ToBoolean()) {
            return callResult;
        }
        k--;
        if (base::ArrayHelper::GetArrayLength(thread, thisObjVal) - 1 < k) {
            RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
            return callResult;
        }
        if (!thisObjVal->IsStableJSArray(thread)) {
            return callResult;
        }
    }
    return callResult;
}

JSTaggedValue JSStableArray::HandleEveryOfStable(JSThread *thread, JSHandle<JSObject> thisObjHandle,
                                                 JSHandle<JSTaggedValue> callbackFnHandle,
                                                 JSHandle<JSTaggedValue> thisArgHandle, uint32_t &k)
{
    JSHandle<JSTaggedValue> thisObjVal(thisObjHandle);
    JSHandle<JSTaggedValue> undefined = thread->GlobalConstants()->GetHandledUndefined();
    uint64_t len = static_cast<uint64_t>(base::ArrayHelper::GetArrayLength(thread, thisObjVal));
    RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
    const int32_t argsLength = 3; // 3: ?kValue, k, O?
    JSTaggedValue callResult = base::BuiltinsBase::GetTaggedBoolean(true);
    JSMutableHandle<JSTaggedValue> kValue(thread, JSTaggedValue::Undefined());
    while (k < len) {
        // Elements of thisObjHandle may change.
        kValue.Update(ElementAccessor::Get(thisObjHandle, k));
        if (!kValue.GetTaggedValue().IsHole()) {
            EcmaRuntimeCallInfo *info =
                EcmaInterpreter::NewRuntimeCallInfo(thread, callbackFnHandle, thisArgHandle, undefined, argsLength);
            RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
            info->SetCallArg(kValue.GetTaggedValue(), JSTaggedValue(k), thisObjVal.GetTaggedValue());
            callResult = JSFunction::Call(info);
            RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
            if (ElementAccessor::GetElementsLength(thisObjHandle) < len) {
                len = ElementAccessor::GetElementsLength(thisObjHandle);
            }
        } else if (JSTaggedValue::HasProperty(thread, thisObjVal, k)) {
            JSHandle<JSTaggedValue> kValue1 = JSArray::FastGetPropertyByValue(thread, thisObjVal, k);
            RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
            EcmaRuntimeCallInfo *info =
                EcmaInterpreter::NewRuntimeCallInfo(thread, callbackFnHandle, thisArgHandle, undefined, argsLength);
            RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
            info->SetCallArg(kValue1.GetTaggedValue(), JSTaggedValue(k), thisObjVal.GetTaggedValue());
            callResult = JSFunction::Call(info);
            RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
        }
        if (!callResult.ToBoolean()) {
            return base::BuiltinsBase::GetTaggedBoolean(false);
        }
        k++;
        if (!thisObjVal->IsStableJSArray(thread)) {
            return base::BuiltinsBase::GetTaggedBoolean(true);
        }
    }
    return base::BuiltinsBase::GetTaggedBoolean(true);
}

JSTaggedValue JSStableArray::HandleSomeOfStable(JSThread *thread, JSHandle<JSObject> thisObjHandle,
                                                JSHandle<JSTaggedValue> callbackFnHandle,
                                                JSHandle<JSTaggedValue> thisArgHandle, uint32_t &k)
{
    JSHandle<JSTaggedValue> thisObjVal(thisObjHandle);
    JSHandle<JSTaggedValue> undefined = thread->GlobalConstants()->GetHandledUndefined();
    uint64_t len = static_cast<uint64_t>(base::ArrayHelper::GetArrayLength(thread, thisObjVal));
    RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
    const int32_t argsLength = 3; // 3: ?kValue, k, O?
    JSTaggedValue callResult = base::BuiltinsBase::GetTaggedBoolean(false);
    JSMutableHandle<JSTaggedValue> kValue(thread, JSTaggedValue::Undefined());
    while (k < len) {
        // Elements of thisObjHandle may change.
        kValue.Update(ElementAccessor::Get(thisObjHandle, k));
        if (!kValue.GetTaggedValue().IsHole()) {
            EcmaRuntimeCallInfo *info =
                EcmaInterpreter::NewRuntimeCallInfo(thread, callbackFnHandle, thisArgHandle, undefined, argsLength);
            RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
            info->SetCallArg(kValue.GetTaggedValue(), JSTaggedValue(k), thisObjVal.GetTaggedValue());
            callResult = JSFunction::Call(info);
            RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
        } else if (JSTaggedValue::HasProperty(thread, thisObjVal, k)) {
            JSHandle<JSTaggedValue> kValue1 = JSArray::FastGetPropertyByValue(thread, thisObjVal, k);
            RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
            EcmaRuntimeCallInfo *info =
                EcmaInterpreter::NewRuntimeCallInfo(thread, callbackFnHandle, thisArgHandle, undefined, argsLength);
            RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
            info->SetCallArg(kValue1.GetTaggedValue(), JSTaggedValue(k), thisObjVal.GetTaggedValue());
            callResult = JSFunction::Call(info);
            RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
        }
        if (ElementAccessor::GetElementsLength(thisObjHandle) < len) {
            len = ElementAccessor::GetElementsLength(thisObjHandle);
        }
        if (callResult.ToBoolean()) {
            return base::BuiltinsBase::GetTaggedBoolean(true);
        }
        k++;
        if (!thisObjVal->IsStableJSArray(thread)) {
            return base::BuiltinsBase::GetTaggedBoolean(false);
        }
    }
    return base::BuiltinsBase::GetTaggedBoolean(false);
}

JSTaggedValue JSStableArray::HandleforEachOfStable(JSThread *thread, JSHandle<JSObject> thisObjHandle,
                                                   JSHandle<JSTaggedValue> callbackFnHandle,
                                                   JSHandle<JSTaggedValue> thisArgHandle, uint32_t len, uint32_t &k)
{
    JSMutableHandle<JSTaggedValue> key(thread, JSTaggedValue::Undefined());
    JSHandle<JSTaggedValue> thisObjVal(thisObjHandle);
    JSHandle<JSTaggedValue> undefined = thread->GlobalConstants()->GetHandledUndefined();
    const int32_t argsLength = 3; // 3: ?kValue, k, O?
    JSMutableHandle<JSTaggedValue> kValue(thread, JSTaggedValue::Undefined());
    if (ElementAccessor::GetElementsLength(thisObjHandle) <= k) {
        return base::BuiltinsBase::GetTaggedBoolean(false);
    }
    while (k < len) {
        // Elements of thisObjHandle may change.
        kValue.Update(ElementAccessor::Get(thisObjHandle, k));
        if (!kValue.GetTaggedValue().IsHole()) {
            key.Update(JSTaggedValue(k));
            EcmaRuntimeCallInfo *info =
                EcmaInterpreter::NewRuntimeCallInfo(thread, callbackFnHandle, thisArgHandle, undefined, argsLength);
            RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
            info->SetCallArg(kValue.GetTaggedValue(), key.GetTaggedValue(), thisObjVal.GetTaggedValue());
            JSTaggedValue funcResult = JSFunction::Call(info);
            RETURN_VALUE_IF_ABRUPT_COMPLETION(thread, funcResult);
            if (ElementAccessor::GetElementsLength(thisObjHandle) < len) {
                len = ElementAccessor::GetElementsLength(thisObjHandle);
            }
        } else if (JSTaggedValue::HasProperty(thread, thisObjVal, k)) {
            key.Update(JSTaggedValue(k));
            JSHandle<JSTaggedValue> kValue1 = JSArray::FastGetPropertyByValue(thread, thisObjVal, k);
            RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
            EcmaRuntimeCallInfo *info =
                EcmaInterpreter::NewRuntimeCallInfo(thread, callbackFnHandle, thisArgHandle, undefined, argsLength);
            RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
            info->SetCallArg(kValue1.GetTaggedValue(), key.GetTaggedValue(), thisObjVal.GetTaggedValue());
            JSTaggedValue funcResult = JSFunction::Call(info);
            RETURN_VALUE_IF_ABRUPT_COMPLETION(thread, funcResult);
        }
        k++;
        if (!thisObjVal->IsStableJSArray(thread)) {
            break;
        }
    }
    return base::BuiltinsBase::GetTaggedBoolean(true);
}

template <class Predicate>
JSTaggedValue JSStableArray::FindRawData(IndexOfContext &ctx, Predicate &&predicate)
{
    DISALLOW_GARBAGE_COLLECTION;
    JSTaggedType *data = nullptr;
    JSTaggedValue elementsValue = JSHandle<JSObject>::Cast(ctx.receiver)->GetElements();
    ElementsKind kind = ElementsKind::GENERIC;
    if (elementsValue.IsMutantTaggedArray()) {
        JSHandle<MutantTaggedArray> elements(ctx.thread, elementsValue);
        data = elements->GetData();
        kind = JSHandle<JSObject>::Cast(ctx.receiver)->GetClass()->GetElementsKind();
    } else {
        JSHandle<TaggedArray> elements(ctx.thread, elementsValue);
        // Note: GC is guaranteed not to happen since no new object is created during the searching process.
        data = elements->GetData();
        // Note: for stable arrays, elements->GetLength() returns the CAPACITY, instead of actual length!
    }
    JSTaggedType *first = data + ctx.fromIndex;
    JSTaggedType *last = data + ctx.length;

    JSMutableHandle<JSTaggedValue> indexHandle(ctx.thread, JSTaggedValue::Undefined());
    for (JSTaggedType *cur = first; cur < last; ++cur) {
        JSTaggedValue convertedCur = JSTaggedValue(*cur);
        if (elementsValue.IsMutantTaggedArray()) {
            convertedCur = ElementAccessor::GetTaggedValueWithElementsKind(*cur, kind);
        }
        if (LIKELY(convertedCur.GetRawData() != JSTaggedValue::VALUE_HOLE)) {
            if (UNLIKELY(std::invoke(predicate, convertedCur.GetRawData()))) {
                return base::BuiltinsBase::GetTaggedInt64(cur - data);
            }
            continue;
        }
        // Fallback slow path
        indexHandle.Update(base::BuiltinsBase::GetTaggedInt64(cur - data));
        bool found = base::ArrayHelper::ElementIsStrictEqualTo(
            ctx.thread, ctx.receiver, indexHandle, ctx.searchElement);
        RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(ctx.thread);
        if (found) {
            return indexHandle.GetTaggedValue();
        }
    }
    return JSTaggedValue(-1); // Not found
}

template <class Predicate>
JSTaggedValue JSStableArray::FindLastRawData(IndexOfContext &ctx, Predicate &&predicate)
{
    DISALLOW_GARBAGE_COLLECTION;
    JSTaggedType *data = nullptr;
    JSTaggedValue elementsValue = JSHandle<JSObject>::Cast(ctx.receiver)->GetElements();
    ElementsKind kind = ElementsKind::GENERIC;
    if (elementsValue.IsMutantTaggedArray()) {
        JSHandle<MutantTaggedArray> elements(ctx.thread, JSHandle<JSObject>::Cast(ctx.receiver)->GetElements());
        data = elements->GetData();
        kind = JSHandle<JSObject>::Cast(ctx.receiver)->GetClass()->GetElementsKind();
    } else {
        JSHandle<TaggedArray> elements(ctx.thread, JSHandle<JSObject>::Cast(ctx.receiver)->GetElements());
        // Note: GC is guaranteed not to happen since no new object is created during the searching process.
        data = elements->GetData();
    }
    JSTaggedType *beforeFirst = data - 1;
    JSTaggedType *beforeLast = data + ctx.fromIndex;

    JSMutableHandle<JSTaggedValue> indexHandle(ctx.thread, JSTaggedValue::Undefined());
    for (JSTaggedType *cur = beforeLast; cur > beforeFirst; --cur) {
        JSTaggedValue convertedCur = JSTaggedValue(*cur);
        if (elementsValue.IsMutantTaggedArray()) {
            convertedCur = ElementAccessor::GetTaggedValueWithElementsKind(*cur, kind);
        }
        if (LIKELY(convertedCur.GetRawData() != JSTaggedValue::VALUE_HOLE)) {
            if (UNLIKELY(std::invoke(predicate, convertedCur.GetRawData()))) {
                return base::BuiltinsBase::GetTaggedInt64(cur - data);
            }
            continue;
        }
        // Fallback slow path
        indexHandle.Update(base::BuiltinsBase::GetTaggedInt64(cur - data));
        bool found = base::ArrayHelper::ElementIsStrictEqualTo(
            ctx.thread, ctx.receiver, indexHandle, ctx.searchElement);
        RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(ctx.thread);
        if (found) {
            return indexHandle.GetTaggedValue();
        }
    }
    return JSTaggedValue(-1); // Not found
}

template <class Predicate>
JSTaggedValue JSStableArray::FindRawDataDispatch(IndexOfType type, IndexOfContext &ctx, Predicate &&predicate)
{
    switch (type) {
        case IndexOfType::IndexOf:
            return FindRawData(ctx, std::forward<Predicate>(predicate));
        case IndexOfType::LastIndexOf:
            return FindLastRawData(ctx, std::forward<Predicate>(predicate));
        default:
            UNREACHABLE();
    }
}

// Zeros need special judge
JSTaggedValue JSStableArray::IndexOfZero(IndexOfType type, IndexOfContext &ctx)
{
    return FindRawDataDispatch(type, ctx, [](JSTaggedType cur) {
        return JSTaggedValue(cur).IsExactlyZero();
    });
}

JSTaggedValue JSStableArray::IndexOfInt32(IndexOfType type, IndexOfContext &ctx, JSTaggedValue searchElement)
{
    ASSERT(searchElement.IsInt());
    int32_t untagged = searchElement.GetInt();
    if (untagged == 0) {
        return IndexOfZero(type, ctx);
    }
    JSTaggedType targetInt32 = searchElement.GetRawData();
    JSTaggedType targetDouble = JSTaggedValue(static_cast<double>(untagged)).GetRawData();
    return FindRawDataDispatch(type, ctx, [targetInt32, targetDouble](JSTaggedType cur) {
        return cur == targetInt32 || cur == targetDouble;
    });
}

JSTaggedValue JSStableArray::IndexOfDouble(IndexOfType type, IndexOfContext &ctx, JSTaggedValue searchElement)
{
    ASSERT(searchElement.IsDouble());
    double untagged = searchElement.GetDouble();
    if (std::isnan(untagged)) {
        return JSTaggedValue(-1);
    }
    if (untagged == 0.0) {
        return IndexOfZero(type, ctx);
    }
    JSTaggedType targetDouble = searchElement.GetRawData();
    if (searchElement.WithinInt32()) {
        JSTaggedType targetInt32 = JSTaggedValue(static_cast<int32_t>(untagged)).GetRawData();
        return FindRawDataDispatch(type, ctx, [targetDouble, targetInt32](JSTaggedType cur) {
            return cur == targetDouble || cur == targetInt32;
        });
    } else {
        return FindRawDataDispatch(type, ctx, [targetDouble](JSTaggedType cur) {
            return cur == targetDouble;
        });
    }
}

JSTaggedValue JSStableArray::IndexOfObjectAddress(IndexOfType type, IndexOfContext &ctx, JSTaggedValue searchElement)
{
    ASSERT(searchElement.IsObject());
    JSTaggedType targetAddress = searchElement.GetRawData();
    return FindRawDataDispatch(type, ctx, [targetAddress](JSTaggedType cur) {
        return cur == targetAddress;
    });
}

JSTaggedValue JSStableArray::IndexOfString(IndexOfType type, IndexOfContext &ctx, JSTaggedValue searchElement)
{
    ASSERT(searchElement.IsString());
    JSTaggedType targetAddress = searchElement.GetRawData();
    return FindRawDataDispatch(type, ctx, [searchElement, targetAddress](JSTaggedType cur) {
        if (targetAddress == cur) {
            return true;
        }
        JSTaggedValue curValue(cur);
        if (!curValue.IsString()) {
            return false;
        }
        return JSTaggedValue::StringCompare(
            EcmaString::Cast(curValue.GetTaggedObject()),
            EcmaString::Cast(searchElement.GetTaggedObject()));
    });
}

JSTaggedValue JSStableArray::IndexOfBigInt(IndexOfType type, IndexOfContext &ctx, JSTaggedValue searchElement)
{
    ASSERT(searchElement.IsBigInt());
    JSTaggedType targetAddress = searchElement.GetRawData();
    return FindRawDataDispatch(type, ctx, [searchElement, targetAddress](JSTaggedType cur) {
        if (cur == targetAddress) {
            return true;
        }
        JSTaggedValue curValue(cur);
        if (!curValue.IsBigInt()) {
            return false;
        }
        return BigInt::Equal(curValue, searchElement);
    });
}

JSTaggedValue JSStableArray::IndexOfDispatch(IndexOfType type, IndexOfContext &ctx, JSTaggedValue searchElement)
{
    if (searchElement.IsInt()) {
        return IndexOfInt32(type, ctx, searchElement);
    } else if (searchElement.IsDouble()) {
        return IndexOfDouble(type, ctx, searchElement);
    } else if (searchElement.IsString()) {
        return IndexOfString(type, ctx, searchElement);
    } else if (searchElement.IsBigInt()) {
        return IndexOfBigInt(type, ctx, searchElement);
    } else {
        return IndexOfObjectAddress(type, ctx, searchElement);
    }
}

JSTaggedValue JSStableArray::IndexOf(JSThread *thread, JSHandle<JSTaggedValue> receiver,
                                     JSHandle<JSTaggedValue> searchElement, uint32_t from, uint32_t len)
{
    IndexOfContext ctx;
    ctx.thread = thread;
    ctx.receiver = receiver;
    ctx.searchElement = searchElement;
    ctx.fromIndex = from;
    ctx.length = len;
    return IndexOfDispatch(IndexOfType::IndexOf, ctx, searchElement.GetTaggedValue());
}

JSTaggedValue JSStableArray::LastIndexOf(JSThread *thread, JSHandle<JSTaggedValue> receiver,
                                         JSHandle<JSTaggedValue> searchElement, uint32_t from, uint32_t len)
{
    IndexOfContext ctx;
    ctx.thread = thread;
    ctx.receiver = receiver;
    ctx.searchElement = searchElement;
    ctx.fromIndex = from;
    ctx.length = len;
    return IndexOfDispatch(IndexOfType::LastIndexOf, ctx, searchElement.GetTaggedValue());
}

JSTaggedValue JSStableArray::Filter(JSHandle<JSObject> newArrayHandle, JSHandle<JSObject> thisObjHandle,
                                    EcmaRuntimeCallInfo *argv, uint32_t &k, uint32_t &toIndex)
{
    JSThread *thread = argv->GetThread();
    JSHandle<JSTaggedValue> callbackFnHandle = base::BuiltinsBase::GetCallArg(argv, 0);
    JSHandle<JSTaggedValue> thisArgHandle = base::BuiltinsBase::GetCallArg(argv, 1);
    JSHandle<JSTaggedValue> thisObjVal(thisObjHandle);
    JSMutableHandle<JSTaggedValue> key(thread, JSTaggedValue::Undefined());
    JSMutableHandle<JSTaggedValue> toIndexHandle(thread, JSTaggedValue::Undefined());
    JSHandle<JSTaggedValue> undefined = thread->GlobalConstants()->GetHandledUndefined();
    const int32_t argsLength = 3; // 3: ?kValue, k, O?
    uint64_t len = static_cast<uint64_t>(base::ArrayHelper::GetArrayLength(thread, thisObjVal));
    RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
    JSMutableHandle<JSTaggedValue> kValue(thread, JSTaggedValue::Undefined());
    while (k < len) {
        // Elements of thisObjHandle may change.
        JSTaggedValue value = ElementAccessor::Get(thisObjHandle, k);
        if (value.IsHole() && JSTaggedValue::HasProperty(thread, thisObjVal, k)) {
            value = JSArray::FastGetPropertyByValue(thread, thisObjVal, k).GetTaggedValue();
            RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
        }
        kValue.Update(value);
        if (!kValue.GetTaggedValue().IsHole()) {
            key.Update(JSTaggedValue(k));
            EcmaRuntimeCallInfo *info =
                EcmaInterpreter::NewRuntimeCallInfo(thread, callbackFnHandle, thisArgHandle, undefined, argsLength);
            RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
            info->SetCallArg(kValue.GetTaggedValue(), key.GetTaggedValue(), thisObjVal.GetTaggedValue());
            JSTaggedValue callResult = JSFunction::Call(info);
            RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
            if (ElementAccessor::GetElementsLength(thisObjHandle) < len) {
                len = ElementAccessor::GetElementsLength(thisObjHandle);
            }
            bool boolResult = callResult.ToBoolean();
            if (boolResult) {
                toIndexHandle.Update(JSTaggedValue(toIndex));
                JSObject::CreateDataPropertyOrThrow(thread, newArrayHandle, toIndexHandle, kValue);
                RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
                toIndex++;
            }
        }
        k++;
        if (!thisObjVal->IsStableJSArray(thread)) {
            break;
        }
    }
    return base::BuiltinsBase::GetTaggedDouble(true);
}

JSTaggedValue JSStableArray::Map(JSHandle<JSObject> newArrayHandle, JSHandle<JSObject> thisObjHandle,
                                 EcmaRuntimeCallInfo *argv, uint32_t &k, uint32_t len)
{
    JSThread *thread = argv->GetThread();
    JSHandle<JSTaggedValue> callbackFnHandle = base::BuiltinsBase::GetCallArg(argv, 0);
    JSHandle<JSTaggedValue> thisArgHandle = base::BuiltinsBase::GetCallArg(argv, 1);
    JSHandle<JSTaggedValue> thisObjVal(thisObjHandle);
    JSMutableHandle<JSTaggedValue> key(thread, JSTaggedValue::Undefined());
    JSHandle<JSTaggedValue> undefined = thread->GlobalConstants()->GetHandledUndefined();
    JSMutableHandle<JSTaggedValue> mapResultHandle(thread, JSTaggedValue::Undefined());
    JSMutableHandle<JSTaggedValue> kValue(thread, JSTaggedValue::Undefined());
    const int32_t argsLength = 3; // 3: ?kValue, k, O?
    while (k < len) {
        // Elements of thisObjHandle may change.
        JSTaggedValue value = ElementAccessor::Get(thisObjHandle, k);
        if (value.IsHole() && JSTaggedValue::HasProperty(thread, thisObjVal, k)) {
            value = JSArray::FastGetPropertyByValue(thread, thisObjVal, k).GetTaggedValue();
            RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
        }
        kValue.Update(value);
        if (!kValue.GetTaggedValue().IsHole()) {
            key.Update(JSTaggedValue(k));
            EcmaRuntimeCallInfo *info =
                EcmaInterpreter::NewRuntimeCallInfo(thread, callbackFnHandle, thisArgHandle, undefined, argsLength);
            RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
            info->SetCallArg(kValue.GetTaggedValue(), key.GetTaggedValue(), thisObjVal.GetTaggedValue());
            JSTaggedValue mapResult = JSFunction::Call(info);
            RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
            mapResultHandle.Update(mapResult);
            JSObject::CreateDataPropertyOrThrow(thread, newArrayHandle, k, mapResultHandle);
            RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
            if (ElementAccessor::GetElementsLength(thisObjHandle) < len) {
                len = ElementAccessor::GetElementsLength(thisObjHandle);
            }
        }
        k++;
        if (!thisObjVal->IsStableJSArray(thread)) {
            break;
        }
    }
    return base::BuiltinsBase::GetTaggedDouble(true);
}

JSTaggedValue JSStableArray::Reverse(JSThread *thread, JSHandle<JSObject> thisObjHandle,
                                     int64_t &lower, uint32_t len)
{
    JSHandle<JSTaggedValue> thisObjVal(thisObjHandle);
    if (thisObjHandle->IsJSArray()) {
        JSArray::CheckAndCopyArray(thread, JSHandle<JSArray>::Cast(thisObjHandle));
    }
    ElementsKind kind = thisObjHandle->GetClass()->GetElementsKind();
    JSHandle<TaggedArray> elements(thread, thisObjHandle->GetElements());
    bool enableElementsKind = thread->GetEcmaVM()->IsEnableElementsKind();
    if (enableElementsKind) {
        if (kind == ElementsKind::INT || kind == ElementsKind::HOLE_INT) {
            return FastReverse(thread, elements, lower, len, ElementsKind::INT);
        } else if (kind == ElementsKind::NUMBER || kind == ElementsKind::HOLE_NUMBER) {
            return FastReverse(thread, elements, lower, len, ElementsKind::NUMBER);
        }
    }
    return FastReverse(thread, elements, lower, len, ElementsKind::TAGGED);
}

JSTaggedValue JSStableArray::FastReverse(JSThread *thread, JSHandle<TaggedArray> elements,
                                         int64_t &lower, uint32_t len, ElementsKind kind)
{
    JSMutableHandle<JSTaggedValue> lowerValueHandle(thread, JSTaggedValue::Undefined());
    JSMutableHandle<JSTaggedValue> upperValueHandle(thread, JSTaggedValue::Undefined());
    int64_t middle = std::floor(len / 2);
    while (lower != middle) {
        if (elements->GetLength() != len) {
            break;
        }
        int64_t upper = static_cast<int64_t>(len) - lower - 1;
        lowerValueHandle.Update(ElementAccessor::FastGet(elements, lower, kind));
        upperValueHandle.Update(ElementAccessor::FastGet(elements, upper, kind));
        ElementAccessor::FastSet(thread, elements, lower, upperValueHandle, kind);
        ElementAccessor::FastSet(thread, elements, upper, lowerValueHandle, kind);
        lower++;
    }
    return base::BuiltinsBase::GetTaggedDouble(true);
}

JSTaggedValue JSStableArray::Concat(JSThread *thread, JSHandle<JSObject> newArrayHandle,
                                    JSHandle<JSObject> thisObjHandle, int64_t &k, int64_t &n)
{
    JSHandle<JSTaggedValue> thisObjVal(thisObjHandle);
    int64_t thisLen = base::ArrayHelper::GetArrayLength(thread, thisObjVal);
    RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
    JSMutableHandle<JSTaggedValue> toKey(thread, JSTaggedValue::Undefined());
    while (k < thisLen) {
        if (ElementAccessor::GetElementsLength(thisObjHandle) != thisLen) {
            break;
        }
        toKey.Update(JSTaggedValue(n));
        JSTaggedValue kValue = ElementAccessor::Get(thisObjHandle, k);
        if (!kValue.IsHole()) {
            JSObject::CreateDataPropertyOrThrow(thread, newArrayHandle, toKey, JSHandle<JSTaggedValue>(thread, kValue));
            RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
        }
        n++;
        k++;
    }
    return base::BuiltinsBase::GetTaggedDouble(true);
}

template JSTaggedValue JSStableArray::FastCopyFromArrayToTypedArray<TypedArrayKind::SHARED>(
    JSThread *thread, JSHandle<JSTypedArray> &targetArray, DataViewType targetType,
    uint64_t targetOffset, uint32_t srcLength, JSHandle<JSObject> &obj);
template JSTaggedValue JSStableArray::FastCopyFromArrayToTypedArray<TypedArrayKind::NON_SHARED>(
    JSThread *thread, JSHandle<JSTypedArray> &targetArray, DataViewType targetType,
    uint64_t targetOffset, uint32_t srcLength, JSHandle<JSObject> &obj);

template<TypedArrayKind typedArrayKind>
JSTaggedValue JSStableArray::FastCopyFromArrayToTypedArray(JSThread *thread, JSHandle<JSTypedArray> &targetArray,
                                                           DataViewType targetType, uint64_t targetOffset,
                                                           uint32_t srcLength, JSHandle<JSObject> &obj)
{
    JSHandle<JSTaggedValue> targetBuffer(thread, targetArray->GetViewedArrayBufferOrByteArray());
    // If IsDetachedBuffer(targetBuffer) is true, throw a TypeError exception.
    if (BuiltinsArrayBufferType<typedArrayKind>::Type::IsDetachedBuffer(targetBuffer.GetTaggedValue())) {
        THROW_TYPE_ERROR_AND_RETURN(thread, "The targetBuffer of This value is detached buffer.",
                                    JSTaggedValue::Exception());
    }
    uint32_t targetLength = targetArray->GetArrayLength();
    uint32_t targetByteOffset = targetArray->GetByteOffset();
    uint32_t targetElementSize = TypedArrayHelper::GetSizeFromType(targetType);
    if (srcLength + targetOffset > targetLength) {
        THROW_RANGE_ERROR_AND_RETURN(thread, "The sum of length and targetOffset is greater than targetLength.",
                                     JSTaggedValue::Exception());
    }
    uint32_t targetByteIndex = static_cast<uint32_t>(targetOffset * targetElementSize + targetByteOffset);
    ContentType contentType = targetArray->GetContentType();
    uint32_t elemLen = ElementAccessor::GetElementsLength(obj);
    if (contentType == ContentType::BigInt) {
        JSMutableHandle<JSTaggedValue> kValue(thread, JSTaggedValue::Hole());
        JSMutableHandle<JSTaggedValue> elem(thread, JSTaggedValue::Hole());
        for (uint32_t i = 0; i < srcLength; i++) {
            if (i < elemLen) {
                elem.Update(ElementAccessor::Get(obj, i));
            } else {
                elem.Update(JSTaggedValue::Hole());
            }
            kValue.Update(JSTaggedValue::ToBigInt(thread, elem));
            RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
            BuiltinsArrayBufferType<typedArrayKind>::Type::SetValueInBuffer(
                thread, targetBuffer.GetTaggedValue(), targetByteIndex, targetType, kValue, true);
            RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
            targetByteIndex += targetElementSize;
        }
    } else {
        double val = 0.0;
        uint32_t copyLen = srcLength > elemLen ? elemLen : srcLength;
        for (uint32_t i = 0; i < copyLen; i++) {
            JSTaggedValue taggedVal = ElementAccessor::Get(obj, i);
            if (!taggedVal.IsNumber()) {
                JSTaggedNumber taggedNumber = JSTaggedValue::ToNumber(thread, taggedVal);
                RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
                val = taggedNumber.GetNumber();
            } else {
                val = taggedVal.GetNumber();
            }
            BuiltinsArrayBufferType<typedArrayKind>::Type::FastSetValueInBuffer(
                thread, targetBuffer.GetTaggedValue(), targetByteIndex, targetType, val, true);
            RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
            targetByteIndex += targetElementSize;
        }

        for (uint32_t i = copyLen; i < srcLength; i++) {
            val = JSTaggedNumber(base::NAN_VALUE).GetNumber();
            BuiltinsArrayBufferType<typedArrayKind>::Type::FastSetValueInBuffer(
                thread, targetBuffer.GetTaggedValue(), targetByteIndex, targetType, val, true);
            RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
            targetByteIndex += targetElementSize;
        }
    }
    return JSTaggedValue::Undefined();
}

JSTaggedValue JSStableArray::At(JSHandle<JSArray> receiver, EcmaRuntimeCallInfo *argv)
{
    JSThread *thread = argv->GetThread();
    uint32_t thisLen = receiver->GetArrayLength();
    if (thisLen == 0) {
        return JSTaggedValue::Undefined();
    }
    JSTaggedNumber index = JSTaggedValue::ToInteger(thread, base::BuiltinsBase::GetCallArg(argv, 0));
    RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
    int64_t relativeIndex = index.GetNumber();
    int64_t k = 0;
    if (relativeIndex >= 0) {
        k = relativeIndex;
    } else {
        k = static_cast<int64_t>(thisLen) + relativeIndex;
    }
    if (k < 0 || k >= thisLen) {
        return JSTaggedValue::Undefined();
    }

    auto result = JSTaggedValue::Hole();
    result = ElementAccessor::Get(JSHandle<JSObject>::Cast(receiver), k);
    return result.IsHole() ? JSTaggedValue::Undefined() : result;
}

JSTaggedValue JSStableArray::At(JSHandle<JSSharedArray> receiver, EcmaRuntimeCallInfo *argv)
{
    JSThread *thread = argv->GetThread();
    uint32_t thisLen = receiver->GetArrayLength();
    if (thisLen == 0) {
        return JSTaggedValue::Undefined();
    }
    JSTaggedNumber index = JSTaggedValue::ToInteger(thread, base::BuiltinsBase::GetCallArg(argv, 0));
    RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
    int64_t relativeIndex = index.GetNumber();
    int64_t k = 0;
    if (relativeIndex >= 0) {
        k = relativeIndex;
    } else {
        k = static_cast<int64_t>(thisLen) + relativeIndex;
    }
    if (k < 0 || k >= thisLen) {
        return JSTaggedValue::Undefined();
    }

    auto result = JSTaggedValue::Hole();
    result = ElementAccessor::Get(JSHandle<JSObject>::Cast(receiver), k);
    return result.IsHole() ? JSTaggedValue::Undefined() : result;
}

JSTaggedValue JSStableArray::With(JSThread *thread, JSHandle<JSArray> receiver,
                                  int64_t insertCount, int64_t index, JSHandle<JSTaggedValue> value)
{
    JSHandle<JSObject> thisObjHandle(receiver);
    JSHandle<JSTaggedValue> undefinedHandle(thread, JSTaggedValue::Undefined());
    JSHandle<JSTaggedValue> newArray = JSArray::ArrayCreate(thread, JSTaggedNumber(0));
    RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
    JSHandle<JSObject> newArrayHandle(newArray);

    JSHandle<JSTaggedValue> thisObjVal(thisObjHandle);
    TaggedArray *destElements = TaggedArray::Cast(newArrayHandle->GetElements().GetTaggedObject());

    if (insertCount > ElementAccessor::GetElementsLength(newArrayHandle)) {
        destElements = *JSObject::GrowElementsCapacity(thread, newArrayHandle, insertCount);
    }
    ASSERT(!newArrayHandle->GetJSHClass()->IsDictionaryMode());
    bool needTransition = true;
    for (uint32_t idx = 0; idx < insertCount; idx++) {
        if (idx == index) {
            ElementAccessor::Set(thread, newArrayHandle, idx, value, needTransition);
        } else {
            JSHandle<JSTaggedValue> kValue(thread, ElementAccessor::Get(thisObjHandle, idx));
            if (kValue->IsHole()) {
                ElementAccessor::Set(thread, newArrayHandle, idx, undefinedHandle, needTransition);
            } else {
                ElementAccessor::Set(thread, newArrayHandle, idx, kValue, needTransition);
            }
        }
    }
    JSHandle<JSArray>::Cast(newArrayHandle)->SetArrayLength(thread, insertCount);
    return newArrayHandle.GetTaggedValue();
}

JSTaggedValue JSStableArray::ToSpliced(JSHandle<JSArray> receiver, EcmaRuntimeCallInfo *argv,
                                       int64_t argc, int64_t actualStart, int64_t actualSkipCount, int64_t insertCount)
{
    JSThread *thread = argv->GetThread();

    JSHandle<JSObject> thisObjHandle(receiver);
    JSHandle<JSTaggedValue> undefinedHandle(thread, JSTaggedValue::Undefined());
    JSHandle<JSTaggedValue> newArray = JSArray::ArrayCreate(thread, JSTaggedNumber(0));
    RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
    JSHandle<JSObject> newArrayHandle(newArray);

    JSHandle<JSTaggedValue> thisObjVal(thisObjHandle);
    TaggedArray *destElements = TaggedArray::Cast(newArrayHandle->GetElements().GetTaggedObject());

    if (insertCount > ElementAccessor::GetElementsLength(newArrayHandle)) {
        destElements = *JSObject::GrowElementsCapacity(thread, newArrayHandle, insertCount);
    }
    ASSERT(!newArrayHandle->GetJSHClass()->IsDictionaryMode());
    int64_t i = 0;
    int64_t r = actualStart + actualSkipCount;
    bool needTransition = true;
    for (int64_t idx = 0; idx < actualStart; idx++, i++) {
        JSHandle<JSTaggedValue> kValue(thread, ElementAccessor::Get(thisObjHandle, idx));
        if (kValue->IsHole()) {
            ElementAccessor::Set(thread, newArrayHandle, i, undefinedHandle, needTransition);
        } else {
            ElementAccessor::Set(thread, newArrayHandle, i, kValue, needTransition);
        }
    }

    for (uint32_t pos = 2; pos < argc; ++pos) { // 2:2 means there two arguments before the insert items.
        auto element = base::BuiltinsBase::GetCallArg(argv, pos);
        ElementAccessor::Set(thread, newArrayHandle, i, element, needTransition);
        ++i;
    }

    while (i < insertCount) {
        JSHandle<JSTaggedValue> kValue(thread, ElementAccessor::Get(thisObjHandle, r));
        if (kValue->IsHole()) {
            ElementAccessor::Set(thread, newArrayHandle, i, undefinedHandle, needTransition);
        } else {
            ElementAccessor::Set(thread, newArrayHandle, i, kValue, needTransition);
        }
        ++i;
        ++r;
    }

    JSHandle<JSArray>::Cast(newArrayHandle)->SetArrayLength(thread, insertCount);

    return newArrayHandle.GetTaggedValue();
}

JSTaggedValue JSStableArray::ToReversed(JSThread *thread, JSHandle<JSArray> receiver,
                                        int64_t insertCount)
{
    JSHandle<JSObject> thisObjHandle(receiver);
    JSHandle<JSTaggedValue> undefinedHandle(thread, JSTaggedValue::Undefined());
    JSHandle<JSTaggedValue> newArray = JSArray::ArrayCreate(thread, JSTaggedNumber(0));
    RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
    JSHandle<JSObject> newArrayHandle(newArray);

    JSHandle<JSTaggedValue> thisObjVal(thisObjHandle);
    TaggedArray *destElements = TaggedArray::Cast(newArrayHandle->GetElements().GetTaggedObject());

    if (insertCount > ElementAccessor::GetElementsLength(newArrayHandle)) {
        destElements = *JSObject::GrowElementsCapacity(thread, newArrayHandle, insertCount);
    }
    ASSERT(!newArrayHandle->GetJSHClass()->IsDictionaryMode());
    bool needTransition = true;
    for (uint32_t idx = 0; idx < insertCount; idx++) {
        JSHandle<JSTaggedValue> kValue(thread, ElementAccessor::Get(thisObjHandle, idx));
        if (kValue->IsHole()) {
            ElementAccessor::Set(thread, newArrayHandle, insertCount - idx - 1,
                                 undefinedHandle, needTransition);
        } else {
            ElementAccessor::Set(thread, newArrayHandle, insertCount - idx - 1, kValue, needTransition);
        }
    }
    JSHandle<JSArray>::Cast(newArrayHandle)->SetArrayLength(thread, insertCount);

    return newArrayHandle.GetTaggedValue();
}

JSTaggedValue JSStableArray::Reduce(JSThread *thread, JSHandle<JSObject> thisObjHandle,
                                    JSHandle<JSTaggedValue> callbackFnHandle,
                                    JSMutableHandle<JSTaggedValue> accumulator, int64_t &k, int64_t &len)
{
    const GlobalEnvConstants *globalConst = thread->GlobalConstants();
    JSHandle<JSTaggedValue> thisObjVal(thisObjHandle);
    JSTaggedValue callResult = JSTaggedValue::Undefined();
    while (k < len) {
        JSTaggedValue kValue(ElementAccessor::Get(thisObjHandle, k));
        if (!kValue.IsHole()) {
            JSHandle<JSTaggedValue> undefined = globalConst->GetHandledUndefined();
            const uint32_t argsLength = 4; // 4: «accumulator, kValue, k, O»
            EcmaRuntimeCallInfo *info =
                EcmaInterpreter::NewRuntimeCallInfo(thread, callbackFnHandle, undefined, undefined, argsLength);
            RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
            info->SetCallArg(accumulator.GetTaggedValue(), kValue, JSTaggedValue(k),
                             thisObjVal.GetTaggedValue());
            callResult = JSFunction::Call(info);
            RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
            if (ElementAccessor::GetElementsLength(thisObjHandle) < len) {
                len = ElementAccessor::GetElementsLength(thisObjHandle);
            }
            accumulator.Update(callResult);
        }
        k++;
        if (!thisObjVal->IsStableJSArray(thread)) {
            break;
        }
    }
    return base::BuiltinsBase::GetTaggedDouble(true);
}

JSTaggedValue JSStableArray::Slice(JSThread *thread, JSHandle<JSObject> thisObjHandle,
                                   int64_t &k, int64_t &count)
{
    ObjectFactory *factory = thread->GetEcmaVM()->GetFactory();
    JSHandle<JSTaggedValue> thisObjVal(thisObjHandle);
    int64_t len = static_cast<int64_t>(ElementAccessor::GetElementsLength(thisObjHandle));
    int64_t oldLen;
    if (len > k + count) {
        oldLen = count;
    } else {
        oldLen = len - k;
    }
    JSHandle<JSObject> arrayObj = factory->NewAndCopyJSArrayObject(thisObjHandle, count, oldLen, k);
    for (int i = 0; i < count; i++) {
        JSMutableHandle<JSTaggedValue> value(thread, ElementAccessor::Get(arrayObj, i));
        if (value->IsHole() && JSTaggedValue::HasProperty(thread, thisObjVal, i + k)) {
            value.Update(JSArray::FastGetPropertyByValue(thread, thisObjVal, i + k).GetTaggedValue());
            RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
            ElementAccessor::Set(thread, arrayObj, i, value, true);
        }
    }
    return arrayObj.GetTaggedValue();
}

JSTaggedValue JSStableArray::Sort(JSThread *thread, const JSHandle<JSObject> &thisObj,
                                  const JSHandle<JSTaggedValue> &callbackFnHandle)
{
    JSArray::SortElementsByObject(thread, thisObj, callbackFnHandle);
    return thisObj.GetTaggedValue();
}

JSTaggedValue JSStableArray::Fill(JSThread *thread, const JSHandle<JSObject> &thisObj,
                                  const JSHandle<JSTaggedValue> &value, int64_t start,
                                  int64_t end, int64_t len)
{
    JSArray::CheckAndCopyArray(thread, JSHandle<JSArray>::Cast(thisObj));
    uint32_t length = ElementAccessor::GetElementsLength(thisObj);
    ElementsKind oldKind = thisObj->GetClass()->GetElementsKind();
    if (JSHClass::TransitToElementsKind(thread, thisObj, value)) {
        ElementsKind newKind = thisObj->GetClass()->GetElementsKind();
        Elements::MigrateArrayWithKind(thread, thisObj, oldKind, newKind);
    }
    if (length >= end) {
        if (thisObj->GetElements().IsMutantTaggedArray()) {
            ElementsKind kind = thisObj->GetClass()->GetElementsKind();
            TaggedArray *elements = TaggedArray::Cast(thisObj->GetElements());
            JSTaggedValue migratedValue = JSTaggedValue(ElementAccessor::ConvertTaggedValueWithElementsKind(
                value.GetTaggedValue(), kind));
            for (int64_t idx = start; idx < end; idx++) {
                elements->Set<false>(thread, idx, migratedValue);
            }
        } else {
            TaggedArray *elements = TaggedArray::Cast(thisObj->GetElements());
            for (int64_t idx = start; idx < end; idx++) {
                elements->Set(thread, idx, value);
            }
        }
        return thisObj.GetTaggedValue();
    } else {
        if (thisObj->GetElements().IsMutantTaggedArray()) {
            ElementsKind kind = thisObj->GetClass()->GetElementsKind();
            JSHandle<MutantTaggedArray> newElements = thread->GetEcmaVM()->GetFactory()->NewMutantTaggedArray(len);
            JSTaggedValue migratedValue = JSTaggedValue(ElementAccessor::ConvertTaggedValueWithElementsKind(
                value.GetTaggedValue(), kind));
            for (int64_t idx = start; idx < end; idx++) {
                newElements->Set<false>(thread, idx, migratedValue);
            }
            thisObj->SetElements(thread, newElements);
        } else {
            JSHandle<TaggedArray> newElements = thread->GetEcmaVM()->GetFactory()->NewTaggedArray(len);
            for (int64_t idx = start; idx < end; idx++) {
                newElements->Set(thread, idx, value);
            }
            thisObj->SetElements(thread, newElements);
        }
        return thisObj.GetTaggedValue();
    }
}

JSTaggedValue JSStableArray::HandleFindLastOfStable(JSThread *thread, JSHandle<JSObject> thisObjHandle,
                                                    JSHandle<JSTaggedValue> callbackFnHandle,
                                                    JSHandle<JSTaggedValue> thisArgHandle,
                                                    JSMutableHandle<JSTaggedValue> &kValue, int64_t &k)
{
    JSHandle<JSTaggedValue> thisObjVal(thisObjHandle);
    JSHandle<JSTaggedValue> undefined = thread->GlobalConstants()->GetHandledUndefined();
    const uint32_t argsLength = 3; // 3: «kValue, k, O»
    JSTaggedValue callResult = base::BuiltinsBase::GetTaggedBoolean(false);
    while (k >= 0) {
        JSTaggedValue val = ElementAccessor::Get(thisObjHandle, k);
        if (!val.IsHole()) {
            kValue.Update(val);
        } else if (JSTaggedValue::HasProperty(thread, thisObjVal, k)) {
            auto res = JSArray::FastGetPropertyByValue(thread, thisObjVal, k).GetTaggedValue();
            RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
            kValue.Update(res);
        } else {
            kValue.Update(JSTaggedValue::Undefined());
        }
        EcmaRuntimeCallInfo *info =
            EcmaInterpreter::NewRuntimeCallInfo(thread, callbackFnHandle, thisArgHandle, undefined, argsLength);
        RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
        info->SetCallArg(kValue.GetTaggedValue(), JSTaggedValue(k), thisObjVal.GetTaggedValue());
        callResult = JSFunction::Call(info);
        RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
        if (callResult.ToBoolean()) {
            return callResult;
        }
        k--;
        ASSERT(ElementAccessor::GetElementsLength(thisObjHandle) > 0);
        if (ElementAccessor::GetElementsLength(thisObjHandle) - 1 < k) {
            break;
        }
        if (!thisObjVal->IsStableJSArray(thread)) {
            break;
        }
    }
    return callResult;
}

JSTaggedValue JSStableArray::HandleReduceRightOfStable(JSThread *thread, JSHandle<JSObject> thisObjHandle,
                                                       JSHandle<JSTaggedValue> callbackFnHandle,
                                                       JSMutableHandle<JSTaggedValue> &accumulator,
                                                       JSHandle<JSTaggedValue> thisArgHandle, int64_t &k)
{
    JSHandle<JSTaggedValue> thisObjVal(thisObjHandle);
    JSMutableHandle<JSTaggedValue> kValue(thread, JSTaggedValue::Hole());
    JSMutableHandle<JSTaggedValue> key(thread, JSTaggedValue::Undefined());
    JSTaggedValue callResult = JSTaggedValue::Undefined();
    JSHandle<JSTaggedValue> undefined = thread->GlobalConstants()->GetHandledUndefined();
    const int32_t argsLength = 4; // 4: «accumulator, kValue, k, O»
    int64_t len = static_cast<int64_t>(base::ArrayHelper::GetArrayLength(thread, thisObjVal));
    while (k >= 0) {
        key.Update(JSTaggedValue(k));
        kValue.Update(ElementAccessor::Get(thisObjHandle, k));
        if (!kValue.GetTaggedValue().IsHole()) {
            EcmaRuntimeCallInfo *info =
                EcmaInterpreter::NewRuntimeCallInfo(thread, callbackFnHandle, thisArgHandle, undefined, argsLength);
            RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
            info->SetCallArg(accumulator.GetTaggedValue(), kValue.GetTaggedValue(),
                key.GetTaggedValue(), thisObjVal.GetTaggedValue());
            callResult = JSFunction::Call(info);
            RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
            accumulator.Update(callResult);
        } else {
            bool exists = JSTaggedValue::HasProperty(thread, thisObjVal, key);
            RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
            if (exists) {
                auto res = JSArray::FastGetPropertyByValue(thread, thisObjVal, key).GetTaggedValue();
                RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
                kValue.Update(res);
                EcmaRuntimeCallInfo *info = EcmaInterpreter::NewRuntimeCallInfo(thread, callbackFnHandle,
                    thisArgHandle, undefined, argsLength);
                RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
                info->SetCallArg(accumulator.GetTaggedValue(), kValue.GetTaggedValue(),
                    key.GetTaggedValue(), thisObjVal.GetTaggedValue());
                callResult = JSFunction::Call(info);
                RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
                accumulator.Update(callResult);
            }
        }
        k--;
        int64_t newLen = static_cast<int64_t>(base::ArrayHelper::GetArrayLength(thread, thisObjVal));
        if (!thisObjVal->IsStableJSArray(thread) || newLen != len) {
            return base::BuiltinsBase::GetTaggedBoolean(false);
        }
    }
    return base::BuiltinsBase::GetTaggedBoolean(true);
}
}  // namespace panda::ecmascript
