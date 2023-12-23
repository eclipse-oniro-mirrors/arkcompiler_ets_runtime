/*
 * Copyright (c) 2023 Huawei Device Co., Ltd.
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

#ifndef ECMASCRIPT_OBJECT_FAST_OPERATOR_INL_H
#define ECMASCRIPT_OBJECT_FAST_OPERATOR_INL_H

#include "ecmascript/jspandafile/class_info_extractor.h"
#include "ecmascript/object_fast_operator.h"

#include "ecmascript/ecma_string_table.h"
#include "ecmascript/element_accessor-inl.h"
#include "ecmascript/js_api/js_api_arraylist.h"
#include "ecmascript/js_api/js_api_deque.h"
#include "ecmascript/js_api/js_api_linked_list.h"
#include "ecmascript/js_api/js_api_list.h"
#include "ecmascript/js_api/js_api_plain_array.h"
#include "ecmascript/js_api/js_api_queue.h"
#include "ecmascript/js_api/js_api_stack.h"
#include "ecmascript/js_api/js_api_vector.h"
#include "ecmascript/js_date.h"
#include "ecmascript/js_function.h"
#include "ecmascript/js_hclass-inl.h"
#include "ecmascript/js_tagged_value-inl.h"
#include "ecmascript/js_typed_array.h"
#include "ecmascript/message_string.h"
#include "ecmascript/property_attributes.h"
#include "ecmascript/runtime_call_id.h"
#include "ecmascript/tagged_dictionary.h"
#include <sstream>

namespace panda::ecmascript {
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define CHECK_IS_ON_PROTOTYPE_CHAIN(receiver, holder) \
    if (UNLIKELY((receiver) != (holder))) {           \
        return JSTaggedValue::Hole();                 \
    }

std::pair<JSTaggedValue, bool> ObjectFastOperator::HasOwnProperty(JSThread *thread,
                                                                  JSTaggedValue receiver, JSTaggedValue key)
{
    [[maybe_unused]] DisallowGarbageCollection noGc;
    if (!receiver.IsHeapObject() || !(receiver.IsRegularObject())) {
        return std::make_pair(JSTaggedValue::Hole(), false);
    }
    if (!key.IsString()) {
        return std::make_pair(JSTaggedValue::Hole(), false);
    }

    uint32_t index = 0;
    if (JSTaggedValue::ToElementIndex(key, &index)) {
        ASSERT(index < JSObject::MAX_ELEMENT_INDEX);
        JSHandle<JSObject> receiverObj(thread, receiver);
        if (ElementAccessor::GetElementsLength(receiverObj) == 0) {
            return std::make_pair(JSTaggedValue::Hole(), true);  // Empty Array
        }

        if (!ElementAccessor::IsDictionaryMode(receiverObj)) {
            if (ElementAccessor::GetElementsLength(receiverObj) <= index) {
                return std::make_pair(JSTaggedValue::Hole(), true);
            }
            JSTaggedValue value = ElementAccessor::Get(receiverObj, index);
            return std::make_pair(value, true);
        } else {
            NumberDictionary *dictionary =
                NumberDictionary::Cast(JSObject::Cast(receiver)->GetElements().GetTaggedObject());
            int entry = dictionary->FindEntry(JSTaggedValue(static_cast<int>(index)));
            if (entry == -1) {
                return std::make_pair(JSTaggedValue::Hole(), true);
            }
            return std::make_pair(JSTaggedValue::Undefined(), true);
        }
    }

    if (!EcmaStringAccessor(key).IsInternString()) {
        EcmaString *str =
            thread->GetEcmaVM()->GetEcmaStringTable()->TryGetInternString(EcmaString::Cast(key.GetTaggedObject()));
        if (str == nullptr) {
            return std::make_pair(JSTaggedValue::Hole(), true);
        }
        key = JSTaggedValue(str);
    }
    auto *hclass = receiver.GetTaggedObject()->GetClass();
    if (LIKELY(!hclass->IsDictionaryMode())) {
        ASSERT(!TaggedArray::Cast(JSObject::Cast(receiver)->GetProperties().GetTaggedObject())->IsDictionaryMode());
        int entry = JSHClass::FindPropertyEntry(thread, hclass, key);
        if (entry != -1) {
            return std::make_pair(JSTaggedValue::Undefined(), true);
        }
    } else {
        TaggedArray *array = TaggedArray::Cast(JSObject::Cast(receiver)->GetProperties().GetTaggedObject());
        ASSERT(array->IsDictionaryMode());
        NameDictionary *dict = NameDictionary::Cast(array);
        int entry = dict->FindEntry(key);
        if (entry != -1) {
            return std::make_pair(JSTaggedValue::Undefined(), true);
        }
    }
    return std::make_pair(JSTaggedValue::Hole(), true);
}

template<ObjectFastOperator::Status status>
JSTaggedValue ObjectFastOperator::GetPropertyByName(JSThread *thread, JSTaggedValue receiver,
                                                    JSTaggedValue key)
{
    INTERPRETER_TRACE(thread, GetPropertyByName);
    // no gc when return hole
    ASSERT(key.IsStringOrSymbol());
    JSTaggedValue holder = receiver;
    do {
        auto *hclass = holder.GetTaggedObject()->GetClass();
        JSType jsType = hclass->GetObjectType();
        if (IsSpecialIndexedObj(jsType)) {
            if (IsFastTypeArray(jsType)) {
                JSTaggedValue res = FastGetTypeArrayProperty(thread, receiver, holder, key, jsType);
                if (res.IsNull()) {
                    return JSTaggedValue::Hole();
                } else if (UNLIKELY(!res.IsHole())) {
                    return res;
                }
            } else {
                return JSTaggedValue::Hole();
            }
        }

        if (LIKELY(!hclass->IsDictionaryMode())) {
            ASSERT(!TaggedArray::Cast(JSObject::Cast(holder)->GetProperties().GetTaggedObject())->IsDictionaryMode());

            int entry = JSHClass::FindPropertyEntry(thread, hclass, key);
            if (entry != -1) {
                LayoutInfo *layoutInfo = LayoutInfo::Cast(hclass->GetLayout().GetTaggedObject());
                PropertyAttributes attr(layoutInfo->GetAttr(entry));
                ASSERT(static_cast<int>(attr.GetOffset()) == entry);
                auto value = JSObject::Cast(holder)->GetProperty(hclass, attr);
                if (UNLIKELY(attr.IsAccessor())) {
                    if (GetInternal(status)) {
                        return value;
                    }
                    return CallGetter(thread, receiver, holder, value);
                }
                ASSERT(!value.IsAccessor());
                if (!value.IsHole()) {
                    return value;
                }
            }
        } else {
            TaggedArray *array = TaggedArray::Cast(JSObject::Cast(holder)->GetProperties().GetTaggedObject());
            ASSERT(array->IsDictionaryMode());
            NameDictionary *dict = NameDictionary::Cast(array);
            int entry = dict->FindEntry(key);
            if (entry != -1) {
                auto value = dict->GetValue(entry);
                auto attr = dict->GetAttributes(entry);
                if (UNLIKELY(attr.IsAccessor())) {
                    if (GetInternal(status)) {
                        return value;
                    }
                    return CallGetter(thread, receiver, holder, value);
                }
                ASSERT(!value.IsAccessor());
                return value;
            }
        }
        if (UseOwn(status)) {
            break;
        }
        holder = hclass->GetPrototype();
    } while (holder.IsHeapObject());
    // not found
    return JSTaggedValue::Undefined();
}

template<ObjectFastOperator::Status status>
JSTaggedValue ObjectFastOperator::SetPropertyByName(JSThread *thread, JSTaggedValue receiver, JSTaggedValue key,
                                                    JSTaggedValue value, JSShared::SCheckMode sCheckMode)
{
    INTERPRETER_TRACE(thread, SetPropertyByName);
    // property
    JSTaggedValue holder = receiver;
    int receiverHoleEntry = -1;
    do {
        auto *hclass = holder.GetTaggedObject()->GetClass();
        JSType jsType = hclass->GetObjectType();
        if (IsSpecialIndexedObj(jsType)) {
            if (IsFastTypeArray(jsType)) {
                JSTaggedValue res = FastSetTypeArrayProperty(thread, receiver, holder, key, value, jsType);
                if (res.IsNull()) {
                    return JSTaggedValue::Hole();
                } else if (UNLIKELY(!res.IsHole())) {
                    return res;
                }
            } else if (IsSpecialContainer(jsType)) {
                THROW_TYPE_ERROR_AND_RETURN(thread, "Cannot set property on Container", JSTaggedValue::Exception());
            } else {
                return JSTaggedValue::Hole();
            }
        }
        // UpdateRepresentation
        if (LIKELY(!hclass->IsDictionaryMode())) {
            ASSERT(!TaggedArray::Cast(JSObject::Cast(holder)->GetProperties().GetTaggedObject())->IsDictionaryMode());
            int entry = JSHClass::FindPropertyEntry(thread, hclass, key);
            if (entry != -1) {
                LayoutInfo *layoutInfo = LayoutInfo::Cast(hclass->GetLayout().GetTaggedObject());
                PropertyAttributes attr(layoutInfo->GetAttr(entry));
                ASSERT(static_cast<int>(attr.GetOffset()) == entry);
                if (UNLIKELY(attr.IsAccessor())) {
                    auto accessor = JSObject::Cast(holder)->GetProperty(hclass, attr);
                    if (ShouldCallSetter(receiver, holder, accessor, attr)) {
                        return CallSetter(thread, receiver, value, accessor);
                    }
                }
                if (UNLIKELY(!attr.IsWritable())) {
                    std::ostringstream oss1;
                    receiver.Dump(oss1);
                    LOG_ECMA(INFO) << "dump log for read-only crash " << oss1.str();
                    std::ostringstream oss2;
                    holder.Dump(oss2);
                    LOG_ECMA(INFO) << "dump log for read-only crash " << oss2.str();
                    std::ostringstream oss3;
                    key.Dump(oss3);
                    LOG_ECMA(INFO) << "dump log for read-only crash " << oss3.str();
                    LOG_ECMA(INFO) << "dump log for read-only crash " << entry;
                    LOG_ECMA(INFO) << "dump log for read-only crash " << "cint1";
                    [[maybe_unused]] EcmaHandleScope handleScope(thread);
                    THROW_TYPE_ERROR_AND_RETURN(thread, "Cannot set readonly property", JSTaggedValue::Exception());
                }
                if (hclass->IsTS()) {
                    auto attrVal = JSObject::Cast(holder)->GetProperty(hclass, attr);
                    if (attrVal.IsHole()) {
                        if (receiverHoleEntry == -1 && holder == receiver) {
                            receiverHoleEntry = entry;
                        }
                        if (UseOwn(status)) {
                            break;
                        }
                        holder = hclass->GetPrototype();
                        continue;
                    }
                }
                if (UNLIKELY(holder != receiver)) {
                    break;
                }
                if (holder.IsJSShared() && (sCheckMode == JSShared::SCheckMode::CHECK)) {
                    if (JSObject::Cast(receiver.GetTaggedObject())->IsImmutable()) {
                        THROW_TYPE_ERROR_AND_RETURN(thread, GET_MESSAGE_STRING(SetImmutableProperty),
                                                    JSTaggedValue::Exception());
                    }
                    if (!ClassHelper::MatchTrackType(attr.GetTrackType(), value)) {
                        THROW_TYPE_ERROR_AND_RETURN((thread), GET_MESSAGE_STRING(SetTypeMismatchedSharedProperty),
                                                    JSTaggedValue::Exception());
                    }
                }
                JSObject::Cast(holder)->SetProperty(thread, hclass, attr, value);
                return JSTaggedValue::Undefined();
            }
        } else {
            TaggedArray *properties = TaggedArray::Cast(JSObject::Cast(holder)->GetProperties().GetTaggedObject());
            ASSERT(properties->IsDictionaryMode());
            NameDictionary *dict = NameDictionary::Cast(properties);
            int entry = dict->FindEntry(key);
            if (entry != -1) {
                auto attr = dict->GetAttributes(entry);
                if (UNLIKELY(attr.IsAccessor())) {
                    auto accessor = dict->GetValue(entry);
                    if (ShouldCallSetter(receiver, holder, accessor, attr)) {
                        return CallSetter(thread, receiver, value, accessor);
                    }
                }
                if (UNLIKELY(!attr.IsWritable())) {
                    std::ostringstream oss1;
                    receiver.Dump(oss1);
                    LOG_ECMA(INFO) << "dump log for read-only crash " << oss1.str();
                    std::ostringstream oss2;
                    holder.Dump(oss2);
                    LOG_ECMA(INFO) << "dump log for read-only crash " << oss2.str();
                    std::ostringstream oss3;
                    key.Dump(oss3);
                    LOG_ECMA(INFO) << "dump log for read-only crash " << oss3.str();
                    LOG_ECMA(INFO) << "dump log for read-only crash " << entry;
                    LOG_ECMA(INFO) << "dump log for read-only crash " << "cint2";
                    [[maybe_unused]] EcmaHandleScope handleScope(thread);
                    THROW_TYPE_ERROR_AND_RETURN(thread, "Cannot set readonly property", JSTaggedValue::Exception());
                }
                if (UNLIKELY(holder != receiver)) {
                    break;
                }
                if (!sCheckMode && holder.IsJSShared()) {
                    if (JSObject::Cast(receiver.GetTaggedObject())->IsImmutable()) {
                        THROW_TYPE_ERROR_AND_RETURN(thread, GET_MESSAGE_STRING(SetImmutableProperty),
                                                    JSTaggedValue::Exception());
                    }
                    if (!ClassHelper::MatchTrackType(attr.GetDictTrackType(), value)) {
                        THROW_TYPE_ERROR_AND_RETURN((thread), GET_MESSAGE_STRING(SetTypeMismatchedSharedProperty),
                                                    JSTaggedValue::Exception());
                    }
                }
                dict->UpdateValue(thread, entry, value);
                return JSTaggedValue::Undefined();
            }
        }
        if (UseOwn(status)) {
            break;
        }
        holder = hclass->GetPrototype();
    } while (holder.IsHeapObject());

    if (receiverHoleEntry != -1) {
        auto *receiverHClass = receiver.GetTaggedObject()->GetClass();
        LayoutInfo *receiverLayoutInfo = LayoutInfo::Cast(receiverHClass->GetLayout().GetTaggedObject());
        PropertyAttributes attr(receiverLayoutInfo->GetAttr(receiverHoleEntry));
        JSObject::Cast(receiver)->SetProperty(thread, receiverHClass, attr, value);
        return JSTaggedValue::Undefined();
    }

    [[maybe_unused]] EcmaHandleScope handleScope(thread);
    JSHandle<JSObject> objHandle(thread, receiver);
    JSHandle<JSTaggedValue> keyHandle(thread, key);
    JSHandle<JSTaggedValue> valueHandle(thread, value);

    if (UNLIKELY(!JSObject::Cast(receiver)->IsExtensible())) {
        THROW_TYPE_ERROR_AND_RETURN(thread, "Cannot add property in prevent extensions ", JSTaggedValue::Exception());
    }
    AddPropertyByName(thread, objHandle, keyHandle, valueHandle, PropertyAttributes::Default());
    return JSTaggedValue::Undefined();
}

template<ObjectFastOperator::Status status>
JSTaggedValue ObjectFastOperator::GetPropertyByIndex(JSThread *thread, JSTaggedValue receiver, uint32_t index)
{
    INTERPRETER_TRACE(thread, GetPropertyByIndex);
    [[maybe_unused]] EcmaHandleScope handleScope(thread);
    JSTaggedValue holder = receiver;
    do {
        auto *hclass = holder.GetTaggedObject()->GetClass();
        JSType jsType = hclass->GetObjectType();
        if (IsSpecialIndexedObj(jsType)) {
            if (jsType == JSType::JS_TYPED_ARRAY) {
                return JSTaggedValue::Hole();
            }
            if (IsFastTypeArray(jsType)) {
                return JSTypedArray::FastGetPropertyByIndex(thread, holder, index, jsType);
            }
            if (IsSpecialContainer(jsType)) {
                return GetContainerProperty(thread, holder, index, jsType);
            }
            return JSTaggedValue::Hole();
        }
        JSHandle<JSObject> currentHolder(thread, holder);
        if (!hclass->IsDictionaryElement()) {
            ASSERT(!ElementAccessor::IsDictionaryMode(currentHolder));
            if (index < ElementAccessor::GetElementsLength(currentHolder)) {
                JSTaggedValue value = ElementAccessor::Get(currentHolder, index);
                if (!value.IsHole()) {
                    return value;
                }
            }
        } else {
            TaggedArray *elements = TaggedArray::Cast(currentHolder->GetElements().GetTaggedObject());
            NumberDictionary *dict = NumberDictionary::Cast(elements);
            int entry = dict->FindEntry(JSTaggedValue(static_cast<int>(index)));
            if (entry != -1) {
                auto attr = dict->GetAttributes(entry);
                auto value = dict->GetValue(entry);
                if (UNLIKELY(attr.IsAccessor())) {
                    return CallGetter(thread, receiver, holder, value);
                }
                ASSERT(!value.IsAccessor());
                return value;
            }
        }
        if (UseOwn(status)) {
            break;
        }
        holder = JSObject::Cast(holder)->GetJSHClass()->GetPrototype();
    } while (holder.IsHeapObject());

    // not found
    return JSTaggedValue::Undefined();
}

template<ObjectFastOperator::Status status>
JSTaggedValue ObjectFastOperator::SetPropertyByIndex(JSThread *thread, JSTaggedValue receiver, uint32_t index,
                                                     JSTaggedValue value)
{
    INTERPRETER_TRACE(thread, SetPropertyByIndex);
    JSTaggedValue holder = receiver;
    do {
        auto *hclass = holder.GetTaggedObject()->GetClass();
        JSType jsType = hclass->GetObjectType();
        if (IsSpecialIndexedObj(jsType)) {
            if (jsType == JSType::JS_TYPED_ARRAY) {
                return JSTaggedValue::Hole();
            }
            if (IsFastTypeArray(jsType)) {
                CHECK_IS_ON_PROTOTYPE_CHAIN(receiver, holder);
                return JSTypedArray::FastSetPropertyByIndex(thread, receiver, index, value, jsType);
            }
            if (IsSpecialContainer(jsType)) {
                return SetContainerProperty(thread, holder, index, value, jsType);
            }
            return JSTaggedValue::Hole();
        }
        JSHandle<JSObject> arrayHandler(thread, holder);
        TaggedArray *elements = TaggedArray::Cast(arrayHandler->GetElements().GetTaggedObject());
        if (!hclass->IsDictionaryElement()) {
            ASSERT(!elements->IsDictionaryMode());
            if (UNLIKELY(holder != receiver)) {
                break;
            }
            if (index < elements->GetLength()) {
                JSTaggedValue oldValue = ElementAccessor::Get(arrayHandler, index);
                if (!oldValue.IsHole()) {
                    if (holder.IsJSCOWArray()) {
                        [[maybe_unused]] EcmaHandleScope handleScope(thread);
                        JSHandle<JSArray> holderHandler(thread, holder);
                        JSHandle<JSObject> obj(thread, holder);
                        JSHandle<JSTaggedValue> valueHandle(thread, value);
                        // CheckAndCopyArray may cause gc.
                        JSArray::CheckAndCopyArray(thread, holderHandler);
                        ElementAccessor::Set(thread, obj, index, valueHandle.GetTaggedValue(), true);
                        return JSTaggedValue::Undefined();
                    }
                    JSHandle<JSTaggedValue> valueHandle(thread, value);
                    ElementAccessor::Set(thread, arrayHandler, index, valueHandle.GetTaggedValue(), true);
                    return JSTaggedValue::Undefined();
                }
            }
        } else {
            NumberDictionary *dict = NumberDictionary::Cast(elements);
            int entry = dict->FindEntry(JSTaggedValue(static_cast<int>(index)));
            if (entry != -1) {
                auto attr = dict->GetAttributes(entry);
                if (UNLIKELY(!attr.IsWritable() || !attr.IsConfigurable())) {
                    return JSTaggedValue::Hole();
                }
                if (UNLIKELY(holder != receiver)) {
                    break;
                }
                if (UNLIKELY(attr.IsAccessor())) {
                    auto accessor = dict->GetValue(entry);
                    if (ShouldCallSetter(receiver, holder, accessor, attr)) {
                        return CallSetter(thread, receiver, value, accessor);
                    }
                }
                dict->UpdateValue(thread, entry, value);
                return JSTaggedValue::Undefined();
            }
            return JSTaggedValue::Hole();
        }
        if (UseOwn(status)) {
            break;
        }
        holder = JSObject::Cast(holder)->GetJSHClass()->GetPrototype();
    } while (holder.IsHeapObject());

    return AddPropertyByIndex(thread, receiver, index, value);
}

template<ObjectFastOperator::Status status>
JSTaggedValue ObjectFastOperator::GetPropertyByValue(JSThread *thread, JSTaggedValue receiver, JSTaggedValue key)
{
    INTERPRETER_TRACE(thread, GetPropertyByValue);
    if (UNLIKELY(!key.IsNumber() && !key.IsStringOrSymbol())) {
        return JSTaggedValue::Hole();
    }
    // fast path
    auto index = TryToElementsIndex(key);
    if (LIKELY(index >= 0)) {
        return GetPropertyByIndex<status>(thread, receiver, index);
    }
    if (!key.IsNumber()) {
        if (key.IsString() && !EcmaStringAccessor(key).IsInternString()) {
            // update string stable
            [[maybe_unused]] EcmaHandleScope handleScope(thread);
            JSHandle<JSTaggedValue> receiverHandler(thread, receiver);
            key = JSTaggedValue(thread->GetEcmaVM()->GetFactory()->InternString(JSHandle<JSTaggedValue>(thread, key)));
            // Maybe moved by GC
            receiver = receiverHandler.GetTaggedValue();
        }
        return ObjectFastOperator::GetPropertyByName<status>(thread, receiver, key);
    }
    return JSTaggedValue::Hole();
}

template<ObjectFastOperator::Status status>
JSTaggedValue ObjectFastOperator::SetPropertyByValue(JSThread *thread, JSTaggedValue receiver, JSTaggedValue key,
                                                     JSTaggedValue value, JSShared::SCheckMode sCheckMode)
{
    INTERPRETER_TRACE(thread, SetPropertyByValue);
    if (UNLIKELY(!key.IsNumber() && !key.IsStringOrSymbol())) {
        return JSTaggedValue::Hole();
    }
    // fast path
    auto index = TryToElementsIndex(key);
    if (LIKELY(index >= 0)) {
        return SetPropertyByIndex<status>(thread, receiver, index, value);
    }
    if (!key.IsNumber()) {
        if (key.IsString()) {
            if (!EcmaStringAccessor(key).IsInternString()) {
                // update string stable
                [[maybe_unused]] EcmaHandleScope handleScope(thread);
                JSHandle<JSTaggedValue> receiverHandler(thread, receiver);
                JSHandle<JSTaggedValue> valueHandler(thread, value);
                key = JSTaggedValue(
                    thread->GetEcmaVM()->GetFactory()->InternString(JSHandle<JSTaggedValue>(thread, key)));
                // Maybe moved by GC
                receiver = receiverHandler.GetTaggedValue();
                value = valueHandler.GetTaggedValue();
            }
        } else {
            ObjectOperator::UpdateDetector(thread, receiver, key);
        }
        return ObjectFastOperator::SetPropertyByName<status>(thread, receiver, key, value, sCheckMode);
    }
    return JSTaggedValue::Hole();
}

bool ObjectFastOperator::FastSetPropertyByValue(JSThread *thread, JSTaggedValue receiver, JSTaggedValue key,
                                                JSTaggedValue value)
{
    INTERPRETER_TRACE(thread, FastSetPropertyByValue);
    JSTaggedValue result = ObjectFastOperator::SetPropertyByValue(thread, receiver, key, value);
    if (!result.IsHole()) {
        return !result.IsException();
    }
    return JSTaggedValue::SetProperty(thread, JSHandle<JSTaggedValue>(thread, receiver),
                                      JSHandle<JSTaggedValue>(thread, key), JSHandle<JSTaggedValue>(thread, value),
                                      true);
}

bool ObjectFastOperator::FastSetPropertyByIndex(JSThread *thread, JSTaggedValue receiver, uint32_t index,
                                                JSTaggedValue value)
{
    INTERPRETER_TRACE(thread, FastSetPropertyByIndex);
    JSTaggedValue result = ObjectFastOperator::SetPropertyByIndex(thread, receiver, index, value);
    if (!result.IsHole()) {
        return !result.IsException();
    }
    return JSTaggedValue::SetProperty(thread, JSHandle<JSTaggedValue>(thread, receiver), index,
                                      JSHandle<JSTaggedValue>(thread, value), true);
}

JSTaggedValue ObjectFastOperator::FastGetPropertyByName(JSThread *thread, JSTaggedValue receiver,
                                                        JSTaggedValue key)
{
    INTERPRETER_TRACE(thread, FastGetPropertyByName);
    ASSERT(key.IsStringOrSymbol());
    if (key.IsString() && !EcmaStringAccessor(key).IsInternString()) {
        JSHandle<JSTaggedValue> receiverHandler(thread, receiver);
        key = JSTaggedValue(thread->GetEcmaVM()->GetFactory()->InternString(JSHandle<JSTaggedValue>(thread, key)));
        // Maybe moved by GC
        receiver = receiverHandler.GetTaggedValue();
    }
    JSTaggedValue result = ObjectFastOperator::GetPropertyByName<Status::GetInternal>(thread, receiver, key);
    if (result.IsHole()) {
        return JSTaggedValue::GetProperty(thread, JSHandle<JSTaggedValue>(thread, receiver),
            JSHandle<JSTaggedValue>(thread, key)).GetValue().GetTaggedValue();
    }
    return result;
}

JSTaggedValue ObjectFastOperator::FastGetPropertyByValue(JSThread *thread, JSTaggedValue receiver, JSTaggedValue key)
{
    INTERPRETER_TRACE(thread, FastGetPropertyByValue);
    JSTaggedValue result = ObjectFastOperator::GetPropertyByValue(thread, receiver, key);
    if (result.IsHole()) {
        return JSTaggedValue::GetProperty(thread, JSHandle<JSTaggedValue>(thread, receiver),
            JSHandle<JSTaggedValue>(thread, key)).GetValue().GetTaggedValue();
    }
    return result;
}

JSTaggedValue ObjectFastOperator::FastGetPropertyByIndex(JSThread *thread, JSTaggedValue receiver, uint32_t index)
{
    INTERPRETER_TRACE(thread, FastGetPropertyByIndex);
    JSTaggedValue result = ObjectFastOperator::GetPropertyByIndex(thread, receiver, index);
    if (result.IsHole()) {
        return JSTaggedValue::GetProperty(thread,
            JSHandle<JSTaggedValue>(thread, receiver), index).GetValue().GetTaggedValue();
    }
    return result;
}

JSTaggedValue ObjectFastOperator::FastParseDate(const EcmaString *str)
{
    int year = 0;
    int month = 1;
    int date = 1;
    int index = 0;

    CVector<uint8_t> tmpBuf;
    EcmaStringAccessor strAccessor(const_cast<EcmaString *>(str));
    int len = static_cast<int>(strAccessor.GetLength());
    auto data = reinterpret_cast<const char *>(strAccessor.GetUtf8DataFlat(str, tmpBuf));
    if (!GetNumFromString(data, len, &index, &year)) {
        return JSTaggedValue::Hole();
    }
    if (!GetNumFromString(data, len, &index, &month)) {
        return JSTaggedValue::Hole();
    }
    if (!GetNumFromString(data, len, &index, &date)) {
        return JSTaggedValue::Hole();
    }
    if (month < 1 || month > JSDate::MONTH_PER_YEAR) {
        return JSTaggedValue::Hole();
    }
    if (date < 1 || date > JSDate::MAX_DAYS_MONTH) {
        return JSTaggedValue::Hole();
    }
    double day = JSDate::MakeDay(year, month - 1, date);
    double timeValue = JSDate::TimeClip(JSDate::MakeDate(day, 0));
    return JSTaggedValue(timeValue);
}

PropertyAttributes ObjectFastOperator::AddPropertyByName(JSThread *thread, JSHandle<JSObject> objHandle,
                                                         JSHandle<JSTaggedValue> keyHandle,
                                                         JSHandle<JSTaggedValue> valueHandle,
                                                         PropertyAttributes attr)
{
    INTERPRETER_TRACE(thread, AddPropertyByName);

    if ((objHandle->IsJSArray() || objHandle->IsTypedArray()) &&
        keyHandle.GetTaggedValue() == thread->GlobalConstants()->GetConstructorString()) {
        objHandle->GetJSHClass()->SetHasConstructor(true);
    }
    int32_t nextInlinedPropsIndex = objHandle->GetJSHClass()->GetNextInlinedPropsIndex();
    if (nextInlinedPropsIndex >= 0) {
        attr.SetOffset(nextInlinedPropsIndex);
        attr.SetIsInlinedProps(true);
        JSHClass::AddProperty(thread, objHandle, keyHandle, attr);
        auto actualValue = JSHClass::ConvertOrTransitionWithRep(thread, objHandle, keyHandle, valueHandle, attr);
        if (actualValue.first) {
            objHandle->SetPropertyInlinedProps<true>(thread, nextInlinedPropsIndex, actualValue.second);
        } else {
            objHandle->SetPropertyInlinedProps<false>(thread, nextInlinedPropsIndex, actualValue.second);
        }
        return attr;
    }

    JSMutableHandle<TaggedArray> array(thread, objHandle->GetProperties());
    uint32_t length = array->GetLength();
    if (length == 0) {
        length = JSObject::MIN_PROPERTIES_LENGTH;
        ObjectFactory *factory = thread->GetEcmaVM()->GetFactory();
        array.Update(factory->NewTaggedArray(length).GetTaggedValue());
        objHandle->SetProperties(thread, array.GetTaggedValue());
    }

    if (!array->IsDictionaryMode()) {
        attr.SetIsInlinedProps(false);
        uint32_t nonInlinedProps = static_cast<uint32_t>(objHandle->GetJSHClass()->GetNextNonInlinedPropsIndex());
        ASSERT(length >= nonInlinedProps);
        uint32_t numberOfProps = objHandle->GetJSHClass()->NumberOfProps();
        if (UNLIKELY(numberOfProps >= PropertyAttributes::MAX_FAST_PROPS_CAPACITY)) {
                // change to dictionary and add one.
                JSHandle<NameDictionary> dict(JSObject::TransitionToDictionary(thread, objHandle));
                JSHandle<NameDictionary> newDict =
                    NameDictionary::PutIfAbsent(thread, dict, keyHandle, valueHandle, attr);
                objHandle->SetProperties(thread, newDict);
                // index is not essential when fastMode is false;
                return attr;
        }
        // if array is full, grow array or change to dictionary mode
        if (length == nonInlinedProps) {
            uint32_t maxNonInlinedFastPropsCapacity = objHandle->GetNonInlinedFastPropsCapacity();
            // Grow properties array size
            uint32_t capacity = JSObject::ComputeNonInlinedFastPropsCapacity(thread, length,
                                                                             maxNonInlinedFastPropsCapacity);
            ObjectFactory *factory = thread->GetEcmaVM()->GetFactory();
            array.Update(factory->CopyArray(array, length, capacity).GetTaggedValue());
            objHandle->SetProperties(thread, array.GetTaggedValue());
        }

        attr.SetOffset(nonInlinedProps + objHandle->GetJSHClass()->GetInlinedProperties());
        JSHClass::AddProperty(thread, objHandle, keyHandle, attr);
        auto actualValue = JSHClass::ConvertOrTransitionWithRep(thread, objHandle, keyHandle, valueHandle, attr);
        if (actualValue.first) {
            array->Set<true>(thread, nonInlinedProps, actualValue.second);
        } else {
            array->Set<false>(thread, nonInlinedProps, actualValue.second);
        }
    } else {
        JSHandle<NameDictionary> dictHandle(array);
        JSHandle<NameDictionary> newDict =
            NameDictionary::PutIfAbsent(thread, dictHandle, keyHandle, valueHandle, attr);
        objHandle->SetProperties(thread, newDict);
    }
    return attr;
}

JSTaggedValue ObjectFastOperator::CallGetter(JSThread *thread, JSTaggedValue receiver, JSTaggedValue holder,
                                             JSTaggedValue value)
{
    INTERPRETER_TRACE(thread, CallGetter);
    // Accessor
    [[maybe_unused]] EcmaHandleScope handleScope(thread);
    AccessorData *accessor = AccessorData::Cast(value.GetTaggedObject());
    if (UNLIKELY(accessor->IsInternal())) {
        JSHandle<JSObject> objHandle(thread, holder);
        return accessor->CallInternalGet(thread, objHandle);
    }
    JSHandle<JSTaggedValue> objHandle(thread, receiver);
    return JSObject::CallGetter(thread, accessor, objHandle);
}

JSTaggedValue ObjectFastOperator::CallSetter(JSThread *thread, JSTaggedValue receiver, JSTaggedValue value,
                                             JSTaggedValue accessorValue)
{
    INTERPRETER_TRACE(thread, CallSetter);
    // Accessor
    [[maybe_unused]] EcmaHandleScope handleScope(thread);
    JSHandle<JSTaggedValue> objHandle(thread, receiver);
    JSHandle<JSTaggedValue> valueHandle(thread, value);

    auto accessor = AccessorData::Cast(accessorValue.GetTaggedObject());
    bool success = JSObject::CallSetter(thread, *accessor, objHandle, valueHandle, true);
    RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
    return success ? JSTaggedValue::Undefined() : JSTaggedValue::Exception();
}

bool ObjectFastOperator::ShouldCallSetter(JSTaggedValue receiver, JSTaggedValue holder, JSTaggedValue accessorValue,
                                          PropertyAttributes attr)
{
    if (!AccessorData::Cast(accessorValue.GetTaggedObject())->IsInternal()) {
        return true;
    }
    if (receiver != holder) {
        return false;
    }
    return attr.IsWritable();
}

bool ObjectFastOperator::IsSpecialIndexedObj(JSType jsType)
{
    return jsType > JSType::JS_ARRAY;
}

bool ObjectFastOperator::IsFastTypeArray(JSType jsType)
{
    return jsType >= JSType::JS_TYPED_ARRAY_FIRST && jsType <= JSType::JS_FLOAT64_ARRAY;
}

JSTaggedValue ObjectFastOperator::FastGetTypeArrayProperty(JSThread *thread, JSTaggedValue receiver,
                                                           JSTaggedValue holder,
                                                           JSTaggedValue key, JSType jsType)
{
    CHECK_IS_ON_PROTOTYPE_CHAIN(receiver, holder);
    JSTaggedValue negativeZero = thread->GlobalConstants()->GetNegativeZeroString();
    if (UNLIKELY(negativeZero == key)) {
        return JSTaggedValue::Undefined();
    }
    uint32_t index = 0;
    if (TryStringOrSymbolToIndex(key, &index)) {
        if (UNLIKELY(index == JSObject::MAX_ELEMENT_INDEX)) {
            return JSTaggedValue::Null();
        }
        return JSTypedArray::FastGetPropertyByIndex(thread, receiver, index, jsType);
    }
    return JSTaggedValue::Hole();
}

bool ObjectFastOperator::TryStringOrSymbolToIndex(JSTaggedValue key, uint32_t *output)
{
    if (key.IsSymbol()) {
        return false;
    }
    auto strObj = static_cast<EcmaString *>(key.GetTaggedObject());
    return EcmaStringAccessor(strObj).ToTypedArrayIndex(output);
}

JSTaggedValue ObjectFastOperator::FastSetTypeArrayProperty(JSThread *thread, JSTaggedValue receiver,
                                                           JSTaggedValue holder, JSTaggedValue key,
                                                           JSTaggedValue value, JSType jsType)
{
    CHECK_IS_ON_PROTOTYPE_CHAIN(receiver, holder);
    JSTaggedValue negativeZero = thread->GlobalConstants()->GetNegativeZeroString();
    if (UNLIKELY(negativeZero == key)) {
        if (value.IsECMAObject()) {
            return JSTaggedValue::Null();
        }
        return JSTaggedValue::Undefined();
    }
    uint32_t index = 0;
    if (TryStringOrSymbolToIndex(key, &index)) {
        if (UNLIKELY(index == JSObject::MAX_ELEMENT_INDEX)) {
            return JSTaggedValue::Null();
        }
        return JSTypedArray::FastSetPropertyByIndex(thread, receiver, index, value, jsType);
    }
    return JSTaggedValue::Hole();
}

bool ObjectFastOperator::IsSpecialContainer(JSType jsType)
{
    return jsType >= JSType::JS_API_ARRAY_LIST && jsType <= JSType::JS_API_QUEUE;
}

JSTaggedValue ObjectFastOperator::GetContainerProperty(JSThread *thread, JSTaggedValue receiver, uint32_t index,
                                                       JSType jsType)
{
    JSTaggedValue res = JSTaggedValue::Undefined();
    switch (jsType) {
        case JSType::JS_API_ARRAY_LIST:
            res = JSAPIArrayList::Cast(receiver.GetTaggedObject())->Get(thread, index);
            break;
        case JSType::JS_API_QUEUE:
            res = JSAPIQueue::Cast(receiver.GetTaggedObject())->Get(thread, index);
            break;
        case JSType::JS_API_PLAIN_ARRAY:
            res = JSAPIPlainArray::Cast(receiver.GetTaggedObject())->Get(JSTaggedValue(index));
            break;
        case JSType::JS_API_DEQUE:
            res = JSAPIDeque::Cast(receiver.GetTaggedObject())->Get(index);
            break;
        case JSType::JS_API_STACK:
            res = JSAPIStack::Cast(receiver.GetTaggedObject())->Get(index);
            break;
        case JSType::JS_API_VECTOR: {
            auto self = JSHandle<JSTaggedValue>(thread, receiver);
            res = JSAPIVector::Get(thread, JSHandle<JSAPIVector>::Cast(self), index);
            break;
        }
        case JSType::JS_API_LIST: {
            res = JSAPIList::Cast(receiver.GetTaggedObject())->Get(index);
            break;
        }
        case JSType::JS_API_LINKED_LIST: {
            res = JSAPILinkedList::Cast(receiver.GetTaggedObject())->Get(index);
            break;
        }
        default:
            break;
    }
    return res;
}

JSTaggedValue ObjectFastOperator::SetContainerProperty(JSThread *thread, JSTaggedValue receiver, uint32_t index,
                                                       JSTaggedValue value, JSType jsType)
{
    JSTaggedValue res = JSTaggedValue::Undefined();
    switch (jsType) {
        case JSType::JS_API_ARRAY_LIST:
            res = JSAPIArrayList::Cast(receiver.GetTaggedObject())->Set(thread, index, value);
            break;
        case JSType::JS_API_QUEUE:
            res = JSAPIQueue::Cast(receiver.GetTaggedObject())->Set(thread, index, value);
            break;
        case JSType::JS_API_PLAIN_ARRAY: {
            JSHandle<JSAPIPlainArray> plainArray(thread, receiver);
            res = JSAPIPlainArray::Set(thread, plainArray, index, value);
            break;
        }
        case JSType::JS_API_DEQUE:
            res = JSAPIDeque::Cast(receiver.GetTaggedObject())->Set(thread, index, value);
            break;
        case JSType::JS_API_STACK:
            res = JSAPIStack::Cast(receiver.GetTaggedObject())->Set(thread, index, value);
            break;
        case JSType::JS_API_VECTOR:
            res = JSAPIVector::Cast(receiver.GetTaggedObject())->Set(thread, index, value);
            break;
        case JSType::JS_API_LIST: {
            JSHandle<JSAPIList> singleList(thread, receiver);
            res = JSAPIList::Set(thread, singleList, index, JSHandle<JSTaggedValue>(thread, value));
            break;
        }
        case JSType::JS_API_LINKED_LIST: {
            JSHandle<JSAPILinkedList> doubleList(thread, receiver);
            res = JSAPILinkedList::Set(thread, doubleList, index, JSHandle<JSTaggedValue>(thread, value));
            break;
        }
        default:
            break;
    }
    return res;
}

JSTaggedValue ObjectFastOperator::AddPropertyByIndex(JSThread *thread, JSTaggedValue receiver, uint32_t index,
                                                     JSTaggedValue value)
{
    INTERPRETER_TRACE(thread, AddPropertyByIndex);
    [[maybe_unused]] EcmaHandleScope handleScope(thread);
    if (UNLIKELY(!JSObject::Cast(receiver)->IsExtensible())) {
        THROW_TYPE_ERROR_AND_RETURN(thread, "Cannot add property in prevent extensions ", JSTaggedValue::Exception());
    }

    bool success = JSObject::AddElementInternal(thread, JSHandle<JSObject>(thread, receiver), index,
                                                JSHandle<JSTaggedValue>(thread, value), PropertyAttributes::Default());
    return success ? JSTaggedValue::Undefined() : JSTaggedValue::Exception();
}

int64_t ObjectFastOperator::TryToElementsIndex(JSTaggedValue key)
{
    if (LIKELY(key.IsInt())) {
        return key.GetInt();
    }
    if (key.IsString()) {
        uint32_t index = 0;
        if (JSTaggedValue::StringToElementIndex(key, &index)) {
            return static_cast<int64_t>(index);
        }
    } else if (key.IsDouble()) {
        double number = key.GetDouble();
        auto integer = static_cast<int32_t>(number);
        if (number == integer) {
            return integer;
        }
    }
    return -1;
}

bool ObjectFastOperator::GetNumFromString(const char *str, int len, int *index, int *num)
{
    int indexStr = *index;
    char oneByte = 0;
    oneByte = str[indexStr];
    if (oneByte < '0' || oneByte > '9') {
        return false;
    }
    if (indexStr >= len) {
        return false;
    }
    int value = 0;
    while (indexStr < len) {
        oneByte = str[indexStr];
        int val = static_cast<int>(oneByte - '0');
        if (val >= 0 && val <= JSDate::NUM_NINE) {
            value = value * JSDate::TEN + val;
            indexStr++;
        } else if (oneByte != '-') {
            return false;
        } else {
            indexStr++;
            break;
        }
    }
    *num = value;
    *index = indexStr;
    return true;
}
}
#endif  // ECMASCRIPT_OBJECT_FAST_OPERATOR_INL_H
