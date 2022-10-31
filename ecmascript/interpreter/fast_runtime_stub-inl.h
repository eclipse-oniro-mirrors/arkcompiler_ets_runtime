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

#ifndef ECMASCRIPT_INTERPRETER_FAST_RUNTIME_STUB_INL_H
#define ECMASCRIPT_INTERPRETER_FAST_RUNTIME_STUB_INL_H

#include "ecmascript/interpreter/fast_runtime_stub.h"

#include "ecmascript/global_dictionary-inl.h"
#include "ecmascript/global_env.h"
#include "ecmascript/interpreter/interpreter.h"
#include "ecmascript/js_api/js_api_arraylist.h"
#include "ecmascript/js_api/js_api_deque.h"
#include "ecmascript/js_api/js_api_linked_list.h"
#include "ecmascript/js_api/js_api_list.h"
#include "ecmascript/js_api/js_api_plain_array.h"
#include "ecmascript/js_api/js_api_queue.h"
#include "ecmascript/js_api/js_api_stack.h"
#include "ecmascript/js_api/js_api_vector.h"
#include "ecmascript/js_function.h"
#include "ecmascript/js_hclass-inl.h"
#include "ecmascript/js_proxy.h"
#include "ecmascript/js_tagged_value-inl.h"
#include "ecmascript/js_typed_array.h"
#include "ecmascript/object_factory-inl.h"
#include "ecmascript/runtime_call_id.h"
#include "ecmascript/tagged_dictionary.h"

namespace panda::ecmascript {
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define CHECK_IS_ON_PROTOTYPE_CHAIN(receiver, holder) \
    if (UNLIKELY((receiver) != (holder))) {           \
        return JSTaggedValue::Hole();                 \
    }

JSTaggedValue FastRuntimeStub::FastMul(JSTaggedValue left, JSTaggedValue right)
{
    if (left.IsNumber() && right.IsNumber()) {
        return JSTaggedValue(left.GetNumber() * right.GetNumber());
    }

    return JSTaggedValue::Hole();
}

JSTaggedValue FastRuntimeStub::FastDiv(JSTaggedValue left, JSTaggedValue right)
{
    if (left.IsNumber() && right.IsNumber()) {
        double dLeft = left.IsInt() ? left.GetInt() : left.GetDouble();
        double dRight = right.IsInt() ? right.GetInt() : right.GetDouble();
        if (UNLIKELY(dRight == 0.0)) {
            if (dLeft == 0.0 || std::isnan(dLeft)) {
                return JSTaggedValue(base::NAN_VALUE);
            }
            uint64_t flagBit = ((bit_cast<uint64_t>(dLeft)) ^ (bit_cast<uint64_t>(dRight))) & base::DOUBLE_SIGN_MASK;
            return JSTaggedValue(bit_cast<double>(flagBit ^ (bit_cast<uint64_t>(base::POSITIVE_INFINITY))));
        }
        return JSTaggedValue(dLeft / dRight);
    }
    return JSTaggedValue::Hole();
}

JSTaggedValue FastRuntimeStub::FastMod(JSTaggedValue left, JSTaggedValue right)
{
    if (right.IsInt() && left.IsInt()) {
        int iRight = right.GetInt();
        int iLeft = left.GetInt();
        if (iRight > 0 && iLeft > 0) {
            return JSTaggedValue(iLeft % iRight);
        }
    }
    if (left.IsNumber() && right.IsNumber()) {
        double dLeft = left.IsInt() ? left.GetInt() : left.GetDouble();
        double dRight = right.IsInt() ? right.GetInt() : right.GetDouble();
        if (dRight == 0.0 || std::isnan(dRight) || std::isnan(dLeft) || std::isinf(dLeft)) {
            return JSTaggedValue(base::NAN_VALUE);
        }
        if (dLeft == 0.0 || std::isinf(dRight)) {
            return JSTaggedValue(dLeft);
        }
        return JSTaggedValue(std::fmod(dLeft, dRight));
    }
    return JSTaggedValue::Hole();
}

JSTaggedValue FastRuntimeStub::FastEqual(JSTaggedValue left, JSTaggedValue right)
{
    if (left == right) {
        if (UNLIKELY(left.IsDouble())) {
            return JSTaggedValue(!std::isnan(left.GetDouble()));
        }
        return JSTaggedValue::True();
    }
    if (left.IsNumber()) {
        if (left.IsInt() && right.IsInt()) {
            return JSTaggedValue::False();
        }
    }
    if (right.IsUndefinedOrNull()) {
        if (left.IsHeapObject()) {
            return JSTaggedValue::False();
        }
        if (left.IsUndefinedOrNull()) {
            return JSTaggedValue::True();
        }
    }
    if (left.IsBoolean()) {
        if (right.IsSpecial()) {
            return JSTaggedValue::False();
        }
    }
    if (left.IsBigInt() && right.IsBigInt()) {
        return JSTaggedValue(BigInt::Equal(left, right));
    }
    return JSTaggedValue::Hole();
}

bool FastRuntimeStub::FastStrictEqual(JSTaggedValue left, JSTaggedValue right)
{
    if (left.IsNumber()) {
        if (right.IsNumber()) {
            double dLeft = left.IsInt() ? left.GetInt() : left.GetDouble();
            double dRight = right.IsInt() ? right.GetInt() : right.GetDouble();
            return JSTaggedValue::StrictNumberEquals(dLeft, dRight);
        }
        return false;
    }
    if (right.IsNumber()) {
        return false;
    }
    if (left == right) {
        return true;
    }
    if (left.IsString() && right.IsString()) {
        return EcmaStringAccessor::StringsAreEqual(static_cast<EcmaString *>(left.GetTaggedObject()),
                                                   static_cast<EcmaString *>(right.GetTaggedObject()));
    }
    if (left.IsBigInt()) {
        if (right.IsBigInt()) {
            return BigInt::Equal(left, right);
        }
        return false;
    }
    if (right.IsBigInt()) {
        return false;
    }
    return false;
}

bool FastRuntimeStub::IsSpecialIndexedObj(JSType jsType)
{
    return jsType > JSType::JS_ARRAY;
}

bool FastRuntimeStub::IsSpecialReceiverObj(JSType jsType)
{
    return jsType > JSType::JS_PRIMITIVE_REF;
}

bool FastRuntimeStub::IsSpecialContainer(JSType jsType)
{
    return jsType >= JSType::JS_API_ARRAY_LIST && jsType <= JSType::JS_API_QUEUE;
}

int32_t FastRuntimeStub::TryToElementsIndex(JSTaggedValue key)
{
    if (LIKELY(key.IsInt())) {
        return key.GetInt();
    }
    if (key.IsString()) {
        uint32_t index = 0;
        if (JSTaggedValue::StringToElementIndex(key, &index)) {
            return static_cast<int32_t>(index);
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

JSTaggedValue FastRuntimeStub::CallGetter(JSThread *thread, JSTaggedValue receiver, JSTaggedValue holder,
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

JSTaggedValue FastRuntimeStub::CallSetter(JSThread *thread, JSTaggedValue receiver, JSTaggedValue value,
                                          JSTaggedValue accessorValue)
{
    INTERPRETER_TRACE(thread, CallSetter);
    // Accessor
    [[maybe_unused]] EcmaHandleScope handleScope(thread);
    JSHandle<JSTaggedValue> objHandle(thread, receiver);
    JSHandle<JSTaggedValue> valueHandle(thread, value);

    auto accessor = AccessorData::Cast(accessorValue.GetTaggedObject());
    bool success = JSObject::CallSetter(thread, *accessor, objHandle, valueHandle, true);
    return success ? JSTaggedValue::Undefined() : JSTaggedValue::Exception();
}

bool FastRuntimeStub::ShouldCallSetter(JSTaggedValue receiver, JSTaggedValue holder, JSTaggedValue accessorValue,
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

PropertyAttributes FastRuntimeStub::AddPropertyByName(JSThread *thread, JSHandle<JSObject> objHandle,
                                                      JSHandle<JSTaggedValue> keyHandle,
                                                      JSHandle<JSTaggedValue> valueHandle,
                                                      PropertyAttributes attr)
{
    INTERPRETER_TRACE(thread, AddPropertyByName);

    if (objHandle->IsJSArray() && keyHandle.GetTaggedValue() == thread->GlobalConstants()->GetConstructorString()) {
        objHandle->GetJSHClass()->SetHasConstructor(true);
    }
    int32_t nextInlinedPropsIndex = objHandle->GetJSHClass()->GetNextInlinedPropsIndex();
    if (nextInlinedPropsIndex >= 0) {
        objHandle->SetPropertyInlinedProps(thread, nextInlinedPropsIndex, valueHandle.GetTaggedValue());
        attr.SetOffset(nextInlinedPropsIndex);
        attr.SetIsInlinedProps(true);
        JSHClass::AddProperty(thread, objHandle, keyHandle, attr);
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
        // if array is full, grow array or change to dictionary mode
        if (length == nonInlinedProps) {
            if (UNLIKELY(length == JSHClass::MAX_CAPACITY_OF_OUT_OBJECTS)) {
                // change to dictionary and add one.
                JSHandle<NameDictionary> dict(JSObject::TransitionToDictionary(thread, objHandle));
                JSHandle<NameDictionary> newDict =
                    NameDictionary::PutIfAbsent(thread, dict, keyHandle, valueHandle, attr);
                objHandle->SetProperties(thread, newDict);
                // index is not essential when fastMode is false;
                return attr;
            }
            // Grow properties array size
            uint32_t capacity = JSObject::ComputePropertyCapacity(length);
            ObjectFactory *factory = thread->GetEcmaVM()->GetFactory();
            array.Update(factory->CopyArray(array, length, capacity).GetTaggedValue());
            objHandle->SetProperties(thread, array.GetTaggedValue());
        }

        attr.SetOffset(nonInlinedProps + objHandle->GetJSHClass()->GetInlinedProperties());
        JSHClass::AddProperty(thread, objHandle, keyHandle, attr);
        array->Set(thread, nonInlinedProps, valueHandle.GetTaggedValue());
    } else {
        JSHandle<NameDictionary> dictHandle(array);
        JSHandle<NameDictionary> newDict =
            NameDictionary::PutIfAbsent(thread, dictHandle, keyHandle, valueHandle, attr);
        objHandle->SetProperties(thread, newDict);
    }
    return attr;
}

JSTaggedValue FastRuntimeStub::AddPropertyByIndex(JSThread *thread, JSTaggedValue receiver, uint32_t index,
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

template<bool UseOwn>
JSTaggedValue FastRuntimeStub::GetPropertyByIndex(JSThread *thread, JSTaggedValue receiver, uint32_t index)
{
    INTERPRETER_TRACE(thread, GetPropertyByIndex);
    [[maybe_unused]] EcmaHandleScope handleScope(thread);
    JSTaggedValue holder = receiver;
    do {
        auto *hclass = holder.GetTaggedObject()->GetClass();
        JSType jsType = hclass->GetObjectType();
        if (IsSpecialIndexedObj(jsType)) {
            if (IsFastTypeArray(jsType)) {
                return JSTypedArray::FastGetPropertyByIndex(thread, receiver, index, jsType);
            }
            if (IsSpecialContainer(jsType)) {
                return GetContainerProperty(thread, holder, index, jsType);
            }
            return JSTaggedValue::Hole();
        }
        TaggedArray *elements = TaggedArray::Cast(JSObject::Cast(holder)->GetElements().GetTaggedObject());

        if (!hclass->IsDictionaryElement()) {
            ASSERT(!elements->IsDictionaryMode());
            if (index < elements->GetLength()) {
                JSTaggedValue value = elements->Get(index);
                if (!value.IsHole()) {
                    return value;
                }
            } else {
                return JSTaggedValue::Hole();
            }
        } else {
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
        if (UseOwn) {
            break;
        }
        holder = JSObject::Cast(holder)->GetJSHClass()->GetPrototype();
    } while (holder.IsHeapObject());

    // not found
    return JSTaggedValue::Undefined();
}

template<bool UseOwn>
JSTaggedValue FastRuntimeStub::GetPropertyByValue(JSThread *thread, JSTaggedValue receiver, JSTaggedValue key)
{
    INTERPRETER_TRACE(thread, GetPropertyByValue);
    if (UNLIKELY(!key.IsNumber() && !key.IsStringOrSymbol())) {
        return JSTaggedValue::Hole();
    }
    // fast path
    auto index = TryToElementsIndex(key);
    if (LIKELY(index >= 0)) {
        return GetPropertyByIndex<UseOwn>(thread, receiver, index);
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
        return FastRuntimeStub::GetPropertyByName<UseOwn>(thread, receiver, key);
    }
    return JSTaggedValue::Hole();
}

template<bool UseOwn>
JSTaggedValue FastRuntimeStub::GetPropertyByName(JSThread *thread, JSTaggedValue receiver, JSTaggedValue key)
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

            LayoutInfo *layoutInfo = LayoutInfo::Cast(hclass->GetLayout().GetTaggedObject());
            uint32_t propsNumber = hclass->NumberOfProps();
            int entry = layoutInfo->FindElementWithCache(thread, hclass, key, propsNumber);
            if (entry != -1) {
                PropertyAttributes attr(layoutInfo->GetAttr(entry));
                ASSERT(static_cast<int>(attr.GetOffset()) == entry);
                auto value = JSObject::Cast(holder)->GetProperty(hclass, attr);
                if (UNLIKELY(attr.IsAccessor())) {
                    return CallGetter(thread, receiver, holder, value);
                }
                ASSERT(!value.IsAccessor());
                return value;
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
                    return CallGetter(thread, receiver, holder, value);
                }
                ASSERT(!value.IsAccessor());
                return value;
            }
        }
        if (UseOwn) {
            break;
        }
        holder = hclass->GetPrototype();
    } while (holder.IsHeapObject());
    // not found
    return JSTaggedValue::Undefined();
}

template<bool UseOwn>
JSTaggedValue FastRuntimeStub::SetPropertyByName(JSThread *thread, JSTaggedValue receiver, JSTaggedValue key,
                                                 JSTaggedValue value)
{
    INTERPRETER_TRACE(thread, SetPropertyByName);
    // property
    JSTaggedValue holder = receiver;
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

            LayoutInfo *layoutInfo = LayoutInfo::Cast(hclass->GetLayout().GetTaggedObject());

            uint32_t propsNumber = hclass->NumberOfProps();
            int entry = layoutInfo->FindElementWithCache(thread, hclass, key, propsNumber);
            if (entry != -1) {
                PropertyAttributes attr(layoutInfo->GetAttr(entry));
                ASSERT(static_cast<int>(attr.GetOffset()) == entry);
                if (UNLIKELY(attr.IsAccessor())) {
                    auto accessor = JSObject::Cast(holder)->GetProperty(hclass, attr);
                    if (ShouldCallSetter(receiver, holder, accessor, attr)) {
                        return CallSetter(thread, receiver, value, accessor);
                    }
                }
                if (UNLIKELY(!attr.IsWritable())) {
                    [[maybe_unused]] EcmaHandleScope handleScope(thread);
                    THROW_TYPE_ERROR_AND_RETURN(thread, "Cannot set readonly property", JSTaggedValue::Exception());
                }
                if (UNLIKELY(holder != receiver)) {
                    break;
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
                    [[maybe_unused]] EcmaHandleScope handleScope(thread);
                    THROW_TYPE_ERROR_AND_RETURN(thread, "Cannot set readonly property", JSTaggedValue::Exception());
                }
                if (UNLIKELY(holder != receiver)) {
                    break;
                }
                dict->UpdateValue(thread, entry, value);
                return JSTaggedValue::Undefined();
            }
        }
        if (UseOwn) {
            break;
        }
        holder = hclass->GetPrototype();
    } while (holder.IsHeapObject());

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

template<bool UseOwn>
JSTaggedValue FastRuntimeStub::SetPropertyByIndex(JSThread *thread, JSTaggedValue receiver, uint32_t index,
                                                  JSTaggedValue value)
{
    INTERPRETER_TRACE(thread, SetPropertyByIndex);
    JSTaggedValue holder = receiver;
    do {
        auto *hclass = holder.GetTaggedObject()->GetClass();
        JSType jsType = hclass->GetObjectType();
        if (IsSpecialIndexedObj(jsType)) {
            if (IsFastTypeArray(jsType)) {
                return JSTypedArray::FastSetPropertyByIndex(thread, receiver, index, value, jsType);
            }
            if (IsSpecialContainer(jsType)) {
                return SetContainerProperty(thread, holder, index, value, jsType);
            }
            return JSTaggedValue::Hole();
        }
        TaggedArray *elements = TaggedArray::Cast(JSObject::Cast(holder)->GetElements().GetTaggedObject());
        if (!hclass->IsDictionaryElement()) {
            ASSERT(!elements->IsDictionaryMode());
            if (UNLIKELY(holder != receiver)) {
                break;
            }
            if (index < elements->GetLength()) {
                if (!elements->Get(index).IsHole()) {
                    if (holder.IsJSCOWArray()) {
                        [[maybe_unused]] EcmaHandleScope handleScope(thread);
                        JSHandle<JSArray> holderHandler(thread, JSArray::Cast(holder.GetTaggedObject()));
                        JSHandle<JSTaggedValue> valueHandle(thread, value);
                        // CheckAndCopyArray may cause gc.
                        JSArray::CheckAndCopyArray(thread, holderHandler);
                        TaggedArray::Cast(holderHandler->GetElements())->Set(thread, index, valueHandle);
                        return JSTaggedValue::Undefined();
                    }
                    elements->Set(thread, index, value);
                    return JSTaggedValue::Undefined();
                }
            }
        } else {
            return JSTaggedValue::Hole();
        }
        if (UseOwn) {
            break;
        }
        holder = JSObject::Cast(holder)->GetJSHClass()->GetPrototype();
    } while (holder.IsHeapObject());

    return AddPropertyByIndex(thread, receiver, index, value);
}

template<bool UseOwn>
JSTaggedValue FastRuntimeStub::SetPropertyByValue(JSThread *thread, JSTaggedValue receiver, JSTaggedValue key,
                                                  JSTaggedValue value)
{
    INTERPRETER_TRACE(thread, SetPropertyByValue);
    if (UNLIKELY(!key.IsNumber() && !key.IsStringOrSymbol())) {
        return JSTaggedValue::Hole();
    }
    // fast path
    auto index = TryToElementsIndex(key);
    if (LIKELY(index >= 0)) {
        return SetPropertyByIndex<UseOwn>(thread, receiver, index, value);
    }
    if (!key.IsNumber()) {
        if (key.IsString() && !EcmaStringAccessor(key).IsInternString()) {
            // update string stable
            [[maybe_unused]] EcmaHandleScope handleScope(thread);
            JSHandle<JSTaggedValue> receiverHandler(thread, receiver);
            JSHandle<JSTaggedValue> valueHandler(thread, value);
            key = JSTaggedValue(thread->GetEcmaVM()->GetFactory()->InternString(JSHandle<JSTaggedValue>(thread, key)));
            // Maybe moved by GC
            receiver = receiverHandler.GetTaggedValue();
            value = valueHandler.GetTaggedValue();
        }
        return FastRuntimeStub::SetPropertyByName<UseOwn>(thread, receiver, key, value);
    }
    return JSTaggedValue::Hole();
}

JSTaggedValue FastRuntimeStub::GetGlobalOwnProperty(JSThread *thread, JSTaggedValue receiver, JSTaggedValue key)
{
    JSObject *obj = JSObject::Cast(receiver);
    TaggedArray *properties = TaggedArray::Cast(obj->GetProperties().GetTaggedObject());
    GlobalDictionary *dict = GlobalDictionary::Cast(properties);
    int entry = dict->FindEntry(key);
    if (entry != -1) {
        auto value = dict->GetValue(entry);
        if (UNLIKELY(value.IsAccessor())) {
            return CallGetter(thread, receiver, receiver, value);
        }
        ASSERT(!value.IsAccessor());
        return value;
    }
    return JSTaggedValue::Hole();
}

JSTaggedValue FastRuntimeStub::FastTypeOf(JSThread *thread, JSTaggedValue obj)
{
    INTERPRETER_TRACE(thread, FastTypeOf);
    const GlobalEnvConstants *globalConst = thread->GlobalConstants();
    switch (obj.GetRawData()) {
        case JSTaggedValue::VALUE_TRUE:
        case JSTaggedValue::VALUE_FALSE:
            return globalConst->GetBooleanString();
        case JSTaggedValue::VALUE_NULL:
            return globalConst->GetObjectString();
        case JSTaggedValue::VALUE_UNDEFINED:
            return globalConst->GetUndefinedString();
        default:
            if (obj.IsHeapObject()) {
                if (obj.IsString()) {
                    return globalConst->GetStringString();
                }
                if (obj.IsSymbol()) {
                    return globalConst->GetSymbolString();
                }
                if (obj.IsCallable()) {
                    return globalConst->GetFunctionString();
                }
                if (obj.IsBigInt()) {
                    return globalConst->GetBigIntString();
                }
                return globalConst->GetObjectString();
            }
            if (obj.IsNumber()) {
                return globalConst->GetNumberString();
            }
    }
    return globalConst->GetUndefinedString();
}

bool FastRuntimeStub::FastSetPropertyByIndex(JSThread *thread, JSTaggedValue receiver, uint32_t index,
                                             JSTaggedValue value)
{
    INTERPRETER_TRACE(thread, FastSetPropertyByIndex);
    JSTaggedValue result = FastRuntimeStub::SetPropertyByIndex(thread, receiver, index, value);
    if (!result.IsHole()) {
        return result != JSTaggedValue::Exception();
    }
    return JSTaggedValue::SetProperty(thread, JSHandle<JSTaggedValue>(thread, receiver), index,
                                      JSHandle<JSTaggedValue>(thread, value), true);
}

bool FastRuntimeStub::FastSetPropertyByValue(JSThread *thread, JSTaggedValue receiver, JSTaggedValue key,
                                             JSTaggedValue value)
{
    INTERPRETER_TRACE(thread, FastSetPropertyByValue);
    JSTaggedValue result = FastRuntimeStub::SetPropertyByValue(thread, receiver, key, value);
    if (!result.IsHole()) {
        return result != JSTaggedValue::Exception();
    }
    return JSTaggedValue::SetProperty(thread, JSHandle<JSTaggedValue>(thread, receiver),
                                      JSHandle<JSTaggedValue>(thread, key), JSHandle<JSTaggedValue>(thread, value),
                                      true);
}

// must not use for interpreter
JSTaggedValue FastRuntimeStub::FastGetPropertyByName(JSThread *thread, JSTaggedValue receiver, JSTaggedValue key)
{
    INTERPRETER_TRACE(thread, FastGetPropertyByName);
    ASSERT(key.IsStringOrSymbol());
    if (key.IsString() && !EcmaStringAccessor(key).IsInternString()) {
        JSHandle<JSTaggedValue> receiverHandler(thread, receiver);
        key = JSTaggedValue(thread->GetEcmaVM()->GetFactory()->InternString(JSHandle<JSTaggedValue>(thread, key)));
        // Maybe moved by GC
        receiver = receiverHandler.GetTaggedValue();
    }
    JSTaggedValue result = FastRuntimeStub::GetPropertyByName(thread, receiver, key);
    if (result.IsHole()) {
        return JSTaggedValue::GetProperty(thread, JSHandle<JSTaggedValue>(thread, receiver),
                                          JSHandle<JSTaggedValue>(thread, key))
            .GetValue()
            .GetTaggedValue();
    }
    return result;
}

JSTaggedValue FastRuntimeStub::FastGetPropertyByValue(JSThread *thread, JSTaggedValue receiver, JSTaggedValue key)
{
    INTERPRETER_TRACE(thread, FastGetPropertyByValue);
    JSTaggedValue result = FastRuntimeStub::GetPropertyByValue(thread, receiver, key);
    if (result.IsHole()) {
        return JSTaggedValue::GetProperty(thread, JSHandle<JSTaggedValue>(thread, receiver),
                                          JSHandle<JSTaggedValue>(thread, key))
            .GetValue()
            .GetTaggedValue();
    }
    return result;
}

template<bool UseHole>  // UseHole is only for Array::Sort() which requires Hole order
JSTaggedValue FastRuntimeStub::FastGetPropertyByIndex(JSThread *thread, JSTaggedValue receiver, uint32_t index)
{
    INTERPRETER_TRACE(thread, FastGetPropertyByIndex);
    JSTaggedValue result = FastRuntimeStub::GetPropertyByIndex(thread, receiver, index);
    if (result.IsHole() && !UseHole) {
        return JSTaggedValue::GetProperty(thread, JSHandle<JSTaggedValue>(thread, receiver), index)
            .GetValue()
            .GetTaggedValue();
    }
    return result;
}

JSTaggedValue FastRuntimeStub::NewLexicalEnv(JSThread *thread, ObjectFactory *factory, uint16_t numVars)
{
    INTERPRETER_TRACE(thread, NewLexicalEnv);
    [[maybe_unused]] EcmaHandleScope handleScope(thread);
    LexicalEnv *newEnv = factory->InlineNewLexicalEnv(numVars);
    if (UNLIKELY(newEnv == nullptr)) {
        return JSTaggedValue::Hole();
    }
    JSTaggedValue currentLexenv = thread->GetCurrentLexenv();
    newEnv->SetParentEnv(thread, currentLexenv);
    newEnv->SetScopeInfo(thread, JSTaggedValue::Hole());
    return JSTaggedValue(newEnv);
}

JSTaggedValue FastRuntimeStub::GetContainerProperty(JSThread *thread, JSTaggedValue receiver, uint32_t index,
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

JSTaggedValue FastRuntimeStub::SetContainerProperty(JSThread *thread, JSTaggedValue receiver, uint32_t index,
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

JSTaggedValue FastRuntimeStub::NewThisObject(JSThread *thread, JSTaggedValue ctor, JSTaggedValue newTarget,
                                             InterpretedFrame *state)
{
    [[maybe_unused]] EcmaHandleScope handleScope(thread);
    ObjectFactory *factory = thread->GetEcmaVM()->GetFactory();

    JSHandle<JSFunction> ctorHandle(thread, ctor);
    JSHandle<JSTaggedValue> newTargetHandle(thread, newTarget);
    JSHandle<JSObject> obj = factory->NewJSObjectByConstructor(ctorHandle, newTargetHandle);
    RETURN_VALUE_IF_ABRUPT_COMPLETION(thread, JSTaggedValue::Exception());

    Method *method = Method::Cast(ctorHandle->GetMethod().GetTaggedObject());
    state->function = ctorHandle.GetTaggedValue();
    state->constpool = method->GetConstantPool();
    state->profileTypeInfo = method->GetProfileTypeInfo();
    state->env = ctorHandle->GetLexicalEnv();

    return obj.GetTaggedValue();
}

bool FastRuntimeStub::TryStringOrSymbolToIndex(JSTaggedValue key, uint32_t *output)
{
    if (key.IsSymbol()) {
        return false;
    }
    auto strObj = static_cast<EcmaString *>(key.GetTaggedObject());
    return EcmaStringAccessor(strObj).ToTypedArrayIndex(output);
}

bool FastRuntimeStub::IsFastTypeArray(JSType jsType)
{
    return jsType >= JSType::JS_TYPED_ARRAY_FIRST && jsType <= JSType::JS_FLOAT64_ARRAY;
}

JSTaggedValue FastRuntimeStub::FastGetTypeArrayProperty(JSThread *thread, JSTaggedValue receiver, JSTaggedValue holder,
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

JSTaggedValue FastRuntimeStub::FastSetTypeArrayProperty(JSThread *thread, JSTaggedValue receiver, JSTaggedValue holder,
                                                        JSTaggedValue key, JSTaggedValue value, JSType jsType)
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
}  // namespace panda::ecmascript
#endif  // ECMASCRIPT_INTERPRETER_FAST_RUNTIME_STUB_INL_H
