/*
 * Copyright (c) 2022 Huawei Device Co., Ltd.
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
#include "ecmascript/compiler/ic_stub_builder.h"
#include "ecmascript/compiler/stub_builder-inl.h"
#include "ecmascript/compiler/typed_array_stub_builder.h"

namespace panda::ecmascript::kungfu {
void ICStubBuilder::NamedICAccessor(Variable* cachedHandler, Label *tryICHandler)
{
    auto env = GetEnvironment();
    Label receiverIsHeapObject(env);
    Label tryIC(env);

    Branch(TaggedIsHeapObject(receiver_), &receiverIsHeapObject, slowPath_);
    Bind(&receiverIsHeapObject);
    {
        Branch(TaggedIsUndefined(profileTypeInfo_), tryFastPath_, &tryIC);
        Bind(&tryIC);
        {
            Label isHeapObject(env);
            Label notHeapObject(env);
            GateRef firstValue = GetValueFromTaggedArray(
                profileTypeInfo_, slotId_);
            Branch(TaggedIsHeapObject(firstValue), &isHeapObject, &notHeapObject);
            Bind(&isHeapObject);
            {
                Label tryPoly(env);
                GateRef hclass = LoadHClass(receiver_);
                Branch(Equal(LoadObjectFromWeakRef(firstValue), hclass),
                       tryICHandler,
                       &tryPoly);
                Bind(&tryPoly);
                {
                    cachedHandler->WriteVariable(CheckPolyHClass(firstValue, hclass));
                    Branch(TaggedIsHole(cachedHandler->ReadVariable()), slowPath_, tryICHandler);
                }
            }
            Bind(&notHeapObject);
            {
                Branch(TaggedIsUndefined(firstValue), slowPath_, tryFastPath_);
            }
        }
    }
}

void ICStubBuilder::ValuedICAccessor(Variable* cachedHandler, Label *tryICHandler, Label* tryElementIC)
{
    auto env = GetEnvironment();
    Label receiverIsHeapObject(env);

    Branch(TaggedIsHeapObject(receiver_), &receiverIsHeapObject, slowPath_);
    Bind(&receiverIsHeapObject);
    {
        Label tryIC(env);
        Branch(TaggedIsUndefined(profileTypeInfo_), tryFastPath_, &tryIC);
        Bind(&tryIC);
        {
            Label isHeapObject(env);
            Label notHeapObject(env);
            GateRef firstValue = GetValueFromTaggedArray(
                profileTypeInfo_, slotId_);
            Branch(TaggedIsHeapObject(firstValue), &isHeapObject, &notHeapObject);
            Bind(&isHeapObject);
            {
                Label tryPoly(env);
                Label tryWithElementPoly(env);
                GateRef hclass = LoadHClass(receiver_);
                Branch(Equal(LoadObjectFromWeakRef(firstValue), hclass),
                       tryElementIC,
                       &tryPoly);
                Bind(&tryPoly);
                {
                    Label firstIsKey(env);
                    Branch(Int64Equal(firstValue, propKey_), &firstIsKey, &tryWithElementPoly);
                    Bind(&firstIsKey);
                    {
                        GateRef handler = CheckPolyHClass(cachedHandler->ReadVariable(), hclass);
                        cachedHandler->WriteVariable(handler);
                        Branch(TaggedIsHole(cachedHandler->ReadVariable()), slowPath_, tryICHandler);
                    }
                    Bind(&tryWithElementPoly);
                    {
                        Label checkSecond(env);
                        Label checkPoly(env);
                        Branch(TaggedIsWeak(firstValue), slowPath_, &checkSecond);
                        Bind(&checkSecond);
                        {
                            Branch(TaggedIsHole(cachedHandler->ReadVariable()), &checkPoly, slowPath_);
                        }
                        Bind(&checkPoly);
                        {
                            cachedHandler->WriteVariable(CheckPolyHClass(firstValue, hclass));
                            Branch(TaggedIsHole(cachedHandler->ReadVariable()), slowPath_, tryElementIC);
                        }
                    }
                }
            }
            Bind(&notHeapObject);
            {
                Branch(TaggedIsUndefined(firstValue), slowPath_, tryFastPath_);
            }
        }
    }
}

void ICStubBuilder::LoadICByName(
    Variable *result, Label *tryFastPath, Label *slowPath, Label *success, ProfileOperation callback)
{
    auto env = GetEnvironment();
    Label loadWithHandler(env);

    SetLabels(tryFastPath, slowPath, success);
    GateRef secondValue = GetValueFromTaggedArray(
        profileTypeInfo_, Int32Add(slotId_, Int32(1)));
    DEFVARIABLE(cachedHandler, VariableType::JS_ANY(), secondValue);
    NamedICAccessor(&cachedHandler, &loadWithHandler);
    Bind(&loadWithHandler);
    {
        GateRef ret = LoadICWithHandler(glue_, receiver_, receiver_, *cachedHandler, callback);
        result->WriteVariable(ret);
        Branch(TaggedIsHole(ret), slowPath_, success_);
    }
}

void ICStubBuilder::StoreICByName(Variable* result, Label* tryFastPath, Label *slowPath, Label *success)
{
    auto env = GetEnvironment();
    Label storeWithHandler(env);

    SetLabels(tryFastPath, slowPath, success);
    GateRef secondValue = GetValueFromTaggedArray(
        profileTypeInfo_, Int32Add(slotId_, Int32(1)));
    DEFVARIABLE(cachedHandler, VariableType::JS_ANY(), secondValue);
    NamedICAccessor(&cachedHandler, &storeWithHandler);
    Bind(&storeWithHandler);
    {
        GateRef ret = StoreICWithHandler(glue_, receiver_, receiver_, value_, *cachedHandler, callback_);
        result->WriteVariable(ret);
        Branch(TaggedIsHole(ret), slowPath_, success_);
    }
}

void ICStubBuilder::LoadICByValue(
    Variable *result, Label *tryFastPath, Label *slowPath, Label *success, ProfileOperation callback)
{
    auto env = GetEnvironment();
    Label loadWithHandler(env);
    Label loadElement(env);
    Label handlerInfoIsElement(env);
    Label handlerInfoNotElement(env);
    Label handlerInfoIsStringElement(env);
    Label handlerInfoNotStringElement(env);
    Label handlerInfoIsTypedArrayElement(env);
    Label exit(env);

    SetLabels(tryFastPath, slowPath, success);
    GateRef secondValue = GetValueFromTaggedArray(
        profileTypeInfo_, Int32Add(slotId_, Int32(1)));
    DEFVARIABLE(cachedHandler, VariableType::JS_ANY(), secondValue);
    DEFVARIABLE(ret, VariableType::JS_ANY(), secondValue);

    ValuedICAccessor(&cachedHandler, &loadWithHandler, &loadElement);
    Bind(&loadElement);
    {
        GateRef handlerInfo = GetInt32OfTInt(*cachedHandler);
        Branch(IsElement(handlerInfo), &handlerInfoIsElement, &handlerInfoNotElement);
        Bind(&handlerInfoIsElement);
        {
            ret = LoadElement(glue_, receiver_, propKey_);
            Jump(&exit);
        }
        Bind(&handlerInfoNotElement);
        {
            Branch(IsStringElement(handlerInfo), &handlerInfoIsStringElement, &handlerInfoNotStringElement);
            Bind(&handlerInfoIsStringElement);
            {
                ret = LoadStringElement(glue_, receiver_, propKey_);
                Jump(&exit);
            }
            Bind(&handlerInfoNotStringElement);
            {
                Branch(IsTypedArrayElement(handlerInfo), &handlerInfoIsTypedArrayElement, &exit);
                Bind(&handlerInfoIsTypedArrayElement);
                {
                    GateRef hclass = LoadHClass(receiver_);
                    GateRef jsType = GetObjectType(hclass);
                    TypedArrayStubBuilder typedArrayBuilder(reinterpret_cast<StubBuilder*>(this));
                    ret = typedArrayBuilder.LoadTypedArrayElement(glue_, receiver_, propKey_, jsType);
                    Jump(&exit);
                }
            }
        }
    }
    Bind(&loadWithHandler);
    {
        ret = LoadICWithHandler(glue_, receiver_, receiver_, *cachedHandler, callback);
        Jump(&exit);
    }
    Bind(&exit);
    result->WriteVariable(*ret);
    Branch(TaggedIsHole(*ret), slowPath_, success_);
}

void ICStubBuilder::StoreICByValue(Variable* result, Label* tryFastPath, Label *slowPath, Label *success)
{
    auto env = GetEnvironment();
    Label storeWithHandler(env);
    Label storeElement(env);

    SetLabels(tryFastPath, slowPath, success);
    GateRef secondValue = GetValueFromTaggedArray(
        profileTypeInfo_, Int32Add(slotId_, Int32(1)));
    DEFVARIABLE(cachedHandler, VariableType::JS_ANY(), secondValue);
    ValuedICAccessor(&cachedHandler, &storeWithHandler, &storeElement);
    Bind(&storeElement);
    {
        GateRef ret = ICStoreElement(glue_, receiver_, propKey_, value_, *cachedHandler);
        result->WriteVariable(ret);
        Branch(TaggedIsHole(ret), slowPath_, success_);
    }
    Bind(&storeWithHandler);
    {
        GateRef ret = StoreICWithHandler(glue_, receiver_, receiver_, value_, *cachedHandler, callback_);
        result->WriteVariable(ret);
        Branch(TaggedIsHole(ret), slowPath_, success_);
    }
}

void ICStubBuilder::TryLoadGlobalICByName(Variable* result, Label* tryFastPath, Label *slowPath, Label *success)
{
    auto env = GetEnvironment();
    Label tryIC(env);

    SetLabels(tryFastPath, slowPath, success);
    Branch(TaggedIsUndefined(profileTypeInfo_), tryFastPath_, &tryIC);
    Bind(&tryIC);
    {
        GateRef handler = GetValueFromTaggedArray(profileTypeInfo_, slotId_);
        Label isHeapObject(env);
        Branch(TaggedIsHeapObject(handler), &isHeapObject, slowPath_);
        Bind(&isHeapObject);
        {
            GateRef ret = LoadGlobal(handler);
            result->WriteVariable(ret);
            Branch(TaggedIsHole(ret), slowPath_, success_);
        }
    }
}

void ICStubBuilder::TryStoreGlobalICByName(Variable* result, Label* tryFastPath, Label *slowPath, Label *success)
{
    auto env = GetEnvironment();
    Label tryIC(env);

    SetLabels(tryFastPath, slowPath, success);
    Branch(TaggedIsUndefined(profileTypeInfo_), tryFastPath_, &tryIC);
    Bind(&tryIC);
    {
        GateRef handler = GetValueFromTaggedArray(profileTypeInfo_, slotId_);
        Label isHeapObject(env);
        Branch(TaggedIsHeapObject(handler), &isHeapObject, slowPath_);
        Bind(&isHeapObject);
        {
            GateRef ret = StoreGlobal(glue_, value_, handler);
            result->WriteVariable(ret);
            Branch(TaggedIsHole(ret), slowPath_, success_);
        }
    }
}
}  // namespace panda::ecmascript::kungfu
