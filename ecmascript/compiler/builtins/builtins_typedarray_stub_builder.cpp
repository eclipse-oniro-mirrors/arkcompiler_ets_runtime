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

#include "ecmascript/compiler/builtins/builtins_typedarray_stub_builder.h"

#include "ecmascript/base/typed_array_helper.h"
#include "ecmascript/byte_array.h"
#include "ecmascript/compiler/builtins/builtins_array_stub_builder.h"
#include "ecmascript/compiler/new_object_stub_builder.h"

namespace panda::ecmascript::kungfu {
GateRef BuiltinsTypedArrayStubBuilder::GetDataPointFromBuffer(GateRef arrBuf)
{
    auto env = GetEnvironment();
    Label entryPass(env);
    env->SubCfgEntry(&entryPass);
    Label isNull(env);
    Label exit(env);
    Label isByteArray(env);
    Label notByteArray(env);
    DEFVARIABLE(result, VariableType::NATIVE_POINTER(), IntPtr(0));
    BRANCH(IsByteArray(arrBuf), &isByteArray, &notByteArray);
    Bind(&isByteArray);
    {
        result = ChangeByteArrayTaggedPointerToInt64(PtrAdd(arrBuf, IntPtr(ByteArray::DATA_OFFSET)));
        Jump(&exit);
    }
    Bind(&notByteArray);
    {
        GateRef data = GetArrayBufferData(arrBuf);
        result = GetExternalPointer(data);
        Jump(&exit);
    }
    Bind(&exit);
    auto ret = *result;
    env->SubCfgExit();
    return ret;
}

GateRef BuiltinsTypedArrayStubBuilder::CheckTypedArrayIndexInRange(GateRef array, GateRef index)
{
    auto env = GetEnvironment();
    Label entryPass(env);
    env->SubCfgEntry(&entryPass);
    DEFVARIABLE(result, VariableType::BOOL(), False());
    Label exit(env);
    Label indexIsvalid(env);
    Label indexNotLessZero(env);
    BRANCH(Int64LessThan(index, Int64(0)), &exit, &indexNotLessZero);
    Bind(&indexNotLessZero);
    {
        GateRef arrLen = GetArrayLength(array);
        BRANCH(Int64GreaterThanOrEqual(index, ZExtInt32ToInt64(arrLen)), &exit, &indexIsvalid);
        Bind(&indexIsvalid);
        {
            result = True();
            Jump(&exit);
        }
    }
    Bind(&exit);
    auto ret = *result;
    env->SubCfgExit();
    return ret;
}

GateRef BuiltinsTypedArrayStubBuilder::LoadTypedArrayElement(GateRef glue, GateRef array, GateRef key, GateRef jsType)
{
    auto env = GetEnvironment();
    Label entryPass(env);
    env->SubCfgEntry(&entryPass);
    DEFVARIABLE(result, VariableType::JS_ANY(), Hole());
    Label exit(env);
    Label notDetached(env);
    Label indexIsvalid(env);
    Label slowPath(env);
    GateRef buffer = GetViewedArrayBuffer(array);
    BRANCH(IsDetachedBuffer(buffer), &exit, &notDetached);
    Bind(&notDetached);
    {
        GateRef index = TryToElementsIndex(glue, key);
        BRANCH(CheckTypedArrayIndexInRange(array, index), &indexIsvalid, &exit);
        Bind(&indexIsvalid);
        {
            GateRef offset = GetByteOffset(array);
            result = GetValueFromBuffer(buffer, TruncInt64ToInt32(index), offset, jsType);
            BRANCH(TaggedIsNumber(*result), &exit, &slowPath);
        }
        Bind(&slowPath);
        {
            result = CallRuntime(glue, RTSTUB_ID(GetTypeArrayPropertyByIndex),
                { array, IntToTaggedInt(index), IntToTaggedInt(jsType) });
            Jump(&exit);
        }
    }
    Bind(&exit);
    auto ret = *result;
    env->SubCfgExit();
    return ret;
}

GateRef BuiltinsTypedArrayStubBuilder::StoreTypedArrayElement(GateRef glue, GateRef array, GateRef index, GateRef value,
                                                              GateRef jsType)
{
    auto env = GetEnvironment();
    Label entryPass(env);
    env->SubCfgEntry(&entryPass);
    DEFVARIABLE(result, VariableType::JS_ANY(), Hole());
    Label exit(env);
    Label notDetached(env);
    Label indexIsvalid(env);
    GateRef buffer = GetViewedArrayBuffer(array);
    BRANCH(IsDetachedBuffer(buffer), &exit, &notDetached);
    Bind(&notDetached);
    {
        BRANCH(CheckTypedArrayIndexInRange(array, index), &indexIsvalid, &exit);
        Bind(&indexIsvalid);
        {
            result = CallRuntime(glue, RTSTUB_ID(SetTypeArrayPropertyByIndex),
                { array, IntToTaggedInt(index), value, IntToTaggedInt(jsType) });
            Jump(&exit);
        }
    }
    Bind(&exit);
    auto ret = *result;
    env->SubCfgExit();
    return ret;
}

GateRef BuiltinsTypedArrayStubBuilder::FastGetPropertyByIndex(GateRef glue, GateRef array,
                                                              GateRef index, GateRef jsType)
{
    auto env = GetEnvironment();
    Label entryPass(env);
    env->SubCfgEntry(&entryPass);
    DEFVARIABLE(result, VariableType::JS_ANY(), Undefined());
    Label exit(env);
    Label isDetached(env);
    Label notDetached(env);
    Label slowPath(env);
    Label indexIsvalid(env);
    
    GateRef buffer = GetViewedArrayBuffer(array);
    BRANCH(IsDetachedBuffer(buffer), &isDetached, &notDetached);
    Bind(&isDetached);
    {
        Jump(&slowPath);
    }
    Bind(&notDetached);
    {
        GateRef arrLen = GetArrayLength(array);
        BRANCH(Int32GreaterThanOrEqual(index, arrLen), &exit, &indexIsvalid);
        Bind(&indexIsvalid);
        {
            GateRef offset = GetByteOffset(array);
            result = GetValueFromBuffer(buffer, index, offset, jsType);
            BRANCH(TaggedIsNumber(*result), &exit, &slowPath);
        }
    }
    Bind(&slowPath);
    {
        result = CallRuntime(glue, RTSTUB_ID(GetTypeArrayPropertyByIndex),
            { array, IntToTaggedInt(index), IntToTaggedInt(jsType)});
        Jump(&exit);
    }
    Bind(&exit);
    auto ret = *result;
    env->SubCfgExit();
    return ret;
}

GateRef BuiltinsTypedArrayStubBuilder::FastCopyElementToArray(GateRef glue, GateRef typedArray, GateRef array)
{
    auto env = GetEnvironment();
    Label entryPass(env);
    env->SubCfgEntry(&entryPass);
    DEFVARIABLE(result, VariableType::BOOL(), True());
    DEFVARIABLE(start, VariableType::INT32(), Int32(0));
    Label exit(env);
    Label isDetached(env);
    Label notDetached(env);
    Label slowPath(env);
    Label begin(env);
    Label storeValue(env);
    Label endLoop(env);

    GateRef buffer = GetViewedArrayBuffer(typedArray);
    BRANCH(IsDetachedBuffer(buffer), &isDetached, &notDetached);
    Bind(&isDetached);
    {
        result = False();
        Jump(&slowPath);
    }
    Bind(&notDetached);
    {
        GateRef arrLen = GetArrayLength(typedArray);
        GateRef offset = GetByteOffset(typedArray);
        GateRef hclass = LoadHClass(typedArray);
        GateRef jsType = GetObjectType(hclass);

        Jump(&begin);
        LoopBegin(&begin);
        {
            BRANCH(Int32UnsignedLessThan(*start, arrLen), &storeValue, &exit);
            Bind(&storeValue);
            {
                GateRef value = GetValueFromBuffer(buffer, *start, offset, jsType);
                SetValueToTaggedArray(VariableType::JS_ANY(), glue, array, *start, value);
                start = Int32Add(*start, Int32(1));
                Jump(&endLoop);
            }
            Bind(&endLoop);
            LoopEnd(&begin);
        }
    }
    Bind(&slowPath);
    {
        CallRuntime(glue, RTSTUB_ID(FastCopyElementToArray), { typedArray, array});
        Jump(&exit);
    }
    Bind(&exit);
    auto ret = *result;
    env->SubCfgExit();
    return ret;
}

GateRef BuiltinsTypedArrayStubBuilder::GetValueFromBuffer(GateRef buffer, GateRef index, GateRef offset, GateRef jsType)
{
    auto env = GetEnvironment();
    Label entryPass(env);
    env->SubCfgEntry(&entryPass);
    DEFVARIABLE(result, VariableType::JS_ANY(), Undefined());
    Label exit(env);
    Label defaultLabel(env);
    Label isInt8(env);
    Label notInt8(env);
    Label isInt16(env);
    Label notInt16(env);

    Label labelBuffer[3] = { Label(env), Label(env), Label(env) };
    Label labelBuffer1[3] = { Label(env), Label(env), Label(env) };
    Label labelBuffer2[3] = { Label(env), Label(env), Label(env) };
    int64_t valueBuffer[3] = {
        static_cast<int64_t>(JSType::JS_INT8_ARRAY), static_cast<int64_t>(JSType::JS_UINT8_ARRAY),
        static_cast<int64_t>(JSType::JS_UINT8_CLAMPED_ARRAY) };
    int64_t valueBuffer1[3] = {
        static_cast<int64_t>(JSType::JS_INT16_ARRAY), static_cast<int64_t>(JSType::JS_UINT16_ARRAY),
        static_cast<int64_t>(JSType::JS_INT32_ARRAY) };
    int64_t valueBuffer2[3] = {
        static_cast<int64_t>(JSType::JS_UINT32_ARRAY), static_cast<int64_t>(JSType::JS_FLOAT32_ARRAY),
        static_cast<int64_t>(JSType::JS_FLOAT64_ARRAY) };

    BRANCH(Int32LessThanOrEqual(jsType, Int32(static_cast<int32_t>(JSType::JS_UINT8_CLAMPED_ARRAY))),
        &isInt8, &notInt8);
    Bind(&isInt8);
    {
        // 3 : this switch has 3 case
        Switch(jsType, &defaultLabel, valueBuffer, labelBuffer, 3);
        Bind(&labelBuffer[0]);
        {
            GateRef byteIndex = Int32Add(index, offset);
            GateRef block = GetDataPointFromBuffer(buffer);
            GateRef re = Load(VariableType::INT8(), block, byteIndex);
            result = IntToTaggedPtr(SExtInt8ToInt32(re));
            Jump(&exit);
        }

        Bind(&labelBuffer[1]);
        {
            GateRef byteIndex = Int32Add(index, offset);
            GateRef block = GetDataPointFromBuffer(buffer);
            GateRef re = Load(VariableType::INT8(), block, byteIndex);
            result = IntToTaggedPtr(ZExtInt8ToInt32(re));
            Jump(&exit);
        }
        // 2 : index of this buffer
        Bind(&labelBuffer[2]);
        {
            GateRef byteIndex = Int32Add(index, offset);
            GateRef block = GetDataPointFromBuffer(buffer);
            GateRef re = Load(VariableType::INT8(), block, byteIndex);
            result = IntToTaggedPtr(ZExtInt8ToInt32(re));
            Jump(&exit);
        }
    }

    Bind(&notInt8);
    {
        BRANCH(Int32LessThanOrEqual(jsType, Int32(static_cast<int32_t>(JSType::JS_INT32_ARRAY))),
            &isInt16, &notInt16);
        Bind(&isInt16);
        {
            // 3 : this switch has 3 case
            Switch(jsType, &defaultLabel, valueBuffer1, labelBuffer1, 3);
            Bind(&labelBuffer1[0]);
            {
                GateRef byteIndex = Int32Add(Int32Mul(index, Int32(base::ElementSize::TWO)), offset);
                GateRef block = GetDataPointFromBuffer(buffer);
                GateRef re = Load(VariableType::INT16(), block, byteIndex);
                result = IntToTaggedPtr(SExtInt16ToInt32(re));
                Jump(&exit);
            }

            Bind(&labelBuffer1[1]);
            {
                GateRef byteIndex = Int32Add(Int32Mul(index, Int32(base::ElementSize::TWO)), offset);
                GateRef block = GetDataPointFromBuffer(buffer);
                GateRef re = Load(VariableType::INT16(), block, byteIndex);
                result = IntToTaggedPtr(ZExtInt16ToInt32(re));
                Jump(&exit);
            }
            // 2 : index of this buffer
            Bind(&labelBuffer1[2]);
            {
                GateRef byteIndex = Int32Add(Int32Mul(index, Int32(base::ElementSize::FOUR)), offset);
                GateRef block = GetDataPointFromBuffer(buffer);
                GateRef re = Load(VariableType::INT32(), block, byteIndex);
                result = IntToTaggedPtr(re);
                Jump(&exit);
            }
        }
        Bind(&notInt16);
        {
            // 3 : this switch has 3 case
            Switch(jsType, &defaultLabel, valueBuffer2, labelBuffer2, 3);
            Bind(&labelBuffer2[0]);
            {
                Label overflow(env);
                Label notOverflow(env);
                GateRef byteIndex = Int32Add(Int32Mul(index, Int32(base::ElementSize::FOUR)), offset);
                GateRef block = GetDataPointFromBuffer(buffer);
                GateRef re = Load(VariableType::INT32(), block, byteIndex);

                auto condition = Int32UnsignedGreaterThan(re, Int32(INT32_MAX));
                BRANCH(condition, &overflow, &notOverflow);
                Bind(&overflow);
                {
                    result = DoubleToTaggedDoublePtr(ChangeUInt32ToFloat64(re));
                    Jump(&exit);
                }
                Bind(&notOverflow);
                {
                    result = IntToTaggedPtr(re);
                    Jump(&exit);
                }
            }
            Bind(&labelBuffer2[1]);
            {
                GateRef byteIndex = Int32Add(Int32Mul(index, Int32(base::ElementSize::FOUR)), offset);
                GateRef block = GetDataPointFromBuffer(buffer);
                GateRef re = Load(VariableType::INT32(), block, byteIndex);
                result = DoubleToTaggedDoublePtr(ExtFloat32ToDouble(CastInt32ToFloat32(re)));
                Jump(&exit);
            }
            // 2 : index of this buffer
            Bind(&labelBuffer2[2]);
            {
                GateRef byteIndex = Int32Add(Int32Mul(index, Int32(base::ElementSize::EIGHT)), offset);
                GateRef block = GetDataPointFromBuffer(buffer);
                GateRef re = Load(VariableType::INT64(), block, byteIndex);
                result = DoubleToTaggedDoublePtr(CastInt64ToFloat64(re));
                Jump(&exit);
            }
        }
    }

    Bind(&defaultLabel);
    {
        Jump(&exit);
    }
    Bind(&exit);
    auto ret = *result;
    env->SubCfgExit();
    return ret;
}

GateRef BuiltinsTypedArrayStubBuilder::CalculatePositionWithLength(GateRef position, GateRef length)
{
    auto env = GetEnvironment();
    Label entry(env);
    env->SubCfgEntry(&entry);
    DEFVARIABLE(result, VariableType::INT64(), Int64(0));
    Label positionLessThanZero(env);
    Label positionNotLessThanZero(env);
    Label resultNotGreaterThanZero(env);
    Label positionLessThanLength(env);
    Label positionNotLessThanLength(env);
    Label afterCalculatePosition(env);

    BRANCH(Int64LessThan(position, Int64(0)), &positionLessThanZero, &positionNotLessThanZero);
    Bind(&positionLessThanZero);
    {
        result = Int64Add(position, length);
        BRANCH(Int64GreaterThan(*result, Int64(0)), &afterCalculatePosition, &resultNotGreaterThanZero);
        Bind(&resultNotGreaterThanZero);
        {
            result = Int64(0);
            Jump(&afterCalculatePosition);
        }
    }
    Bind(&positionNotLessThanZero);
    {
        BRANCH(Int64LessThan(position, length), &positionLessThanLength, &positionNotLessThanLength);
        Bind(&positionLessThanLength);
        {
            result = position;
            Jump(&afterCalculatePosition);
        }
        Bind(&positionNotLessThanLength);
        {
            result = length;
            Jump(&afterCalculatePosition);
        }
    }
    Bind(&afterCalculatePosition);
    auto ret = *result;
    env->SubCfgExit();
    return ret;
}

void BuiltinsTypedArrayStubBuilder::Reverse(GateRef glue, GateRef thisValue, [[maybe_unused]] GateRef numArgs,
    Variable *result, Label *exit, Label *slowPath)
{
    auto env = GetEnvironment();
    Label ecmaObj(env);
    Label typedArray(env);
    Label isFastTypedArray(env);
    Label defaultConstr(env);
    BRANCH(IsEcmaObject(thisValue), &ecmaObj, slowPath);
    Bind(&ecmaObj);
    BRANCH(IsTypedArray(thisValue), &typedArray, slowPath);
    Bind(&typedArray);
    GateRef arrayType = GetObjectType(LoadHClass(thisValue));
    Branch(IsFastTypeArray(arrayType), &isFastTypedArray, slowPath);
    Bind(&isFastTypedArray);
    BRANCH(HasConstructor(thisValue), slowPath, &defaultConstr);
    Bind(&defaultConstr);

    DEFVARIABLE(thisArrLen, VariableType::INT64(), ZExtInt32ToInt64(GetArrayLength(thisValue)));
    GateRef middle = Int64Div(*thisArrLen, Int64(2));
    DEFVARIABLE(lower, VariableType::INT64(), Int64(0));
    Label loopHead(env);
    Label loopEnd(env);
    Label loopNext(env);
    Label loopExit(env);
    Jump(&loopHead);
    LoopBegin(&loopHead);
    {
        BRANCH(Int64NotEqual(*lower, middle), &loopNext, &loopExit);
        Bind(&loopNext);
        {
            DEFVARIABLE(upper, VariableType::INT64(), Int64Sub(Int64Sub(*thisArrLen, *lower), Int64(1)));
            Label hasException0(env);
            Label hasException1(env);
            Label notHasException0(env);
            GateRef lowerValue = FastGetPropertyByIndex(glue, thisValue,
                TruncInt64ToInt32(*lower), arrayType);
            GateRef upperValue = FastGetPropertyByIndex(glue, thisValue,
                TruncInt64ToInt32(*upper), arrayType);
            BRANCH(HasPendingException(glue), &hasException0, &notHasException0);
            Bind(&hasException0);
            {
                result->WriteVariable(Exception());
                Jump(exit);
            }
            Bind(&notHasException0);
            {
                StoreTypedArrayElement(glue, thisValue, *lower, upperValue, arrayType);
                StoreTypedArrayElement(glue, thisValue, *upper, lowerValue, arrayType);
                BRANCH(HasPendingException(glue), &hasException1, &loopEnd);
                Bind(&hasException1);
                {
                    result->WriteVariable(Exception());
                    Jump(exit);
                }
            }
        }
    }
    Bind(&loopEnd);
    lower = Int64Add(*lower, Int64(1));
    LoopEnd(&loopHead);
    Bind(&loopExit);
    result->WriteVariable(thisValue);
    Jump(exit);
}

void BuiltinsTypedArrayStubBuilder::LastIndexOf(GateRef glue, GateRef thisValue, GateRef numArgs,
    Variable *result, Label *exit, Label *slowPath)
{
    auto env = GetEnvironment();
    Label thisExists(env);
    Label isHeapObject(env);
    Label typedArray(env);

    BRANCH(TaggedIsUndefinedOrNull(thisValue), slowPath, &thisExists);
    Bind(&thisExists);
    BRANCH(TaggedIsHeapObject(thisValue), &isHeapObject, slowPath);
    Bind(&isHeapObject);
    BRANCH(IsTypedArray(thisValue), &typedArray, slowPath);
    Bind(&typedArray);

    GateRef len = ZExtInt32ToInt64(GetArrayLength(thisValue));
    Label isEmptyArray(env);
    Label notEmptyArray(env);
    BRANCH(Int64Equal(len, Int64(0)), &isEmptyArray, &notEmptyArray);
    Bind(&isEmptyArray);
    {
        result->WriteVariable(IntToTaggedPtr(Int32(-1)));
        Jump(exit);
    }
    Bind(&notEmptyArray);

    GateRef value = GetCallArg0(numArgs);
    DEFVARIABLE(relativeFromIndex, VariableType::INT64(), Int64(0));
    Label findIndex(env);
    Label isOneArg(env);
    Label isTwoArg(env);
    // 2:Indicates the number of parameters passed in.
    BRANCH(Int64Equal(TruncPtrToInt32(numArgs), Int32(2)), &isTwoArg, &isOneArg);
    Bind(&isOneArg);
    {
        relativeFromIndex = Int64Sub(len, Int64(1));
        Jump(&findIndex);
    }
    Bind(&isTwoArg);
    {
        GateRef fromIndex = GetCallArg1(numArgs);
        Label taggedIsInt(env);
        BRANCH(TaggedIsInt(fromIndex), &taggedIsInt, slowPath);
        Bind(&taggedIsInt);
        GateRef fromIndexInt = SExtInt32ToInt64(TaggedGetInt(fromIndex));
        Label isFromIndexLessZero(env);
        Label isFromIndexNotLessZero(env);
        BRANCH(Int64LessThan(fromIndexInt, Int64(0)), &isFromIndexLessZero, &isFromIndexNotLessZero);
        Bind(&isFromIndexLessZero);
        {
            relativeFromIndex = Int64Add(len, fromIndexInt);
            Jump(&findIndex);
        }
        Bind(&isFromIndexNotLessZero);
        {
            Label isFromIndexGreatLen(env);
            Label isFromIndexNotGreatLen(env);
            BRANCH(Int64GreaterThan(fromIndexInt, Int64Sub(len, Int64(1))),
                &isFromIndexGreatLen, &isFromIndexNotGreatLen);
            Bind(&isFromIndexGreatLen);
            {
                relativeFromIndex = Int64Sub(len, Int64(1));
                Jump(&findIndex);
            }
            Bind(&isFromIndexNotGreatLen);
            {
                relativeFromIndex = fromIndexInt;
                Jump(&findIndex);
            }
        }
    }

    Bind(&findIndex);
    {
        Label loopHead(env);
        Label loopEnd(env);
        Label loopExit(env);
        Label loopNext(env);
        Label isFound(env);
        Jump(&loopHead);
        LoopBegin(&loopHead);
        {
            BRANCH(Int64LessThan(*relativeFromIndex, Int64(0)), &loopExit, &loopNext);
            Bind(&loopNext);
            {
                GateRef hclass = LoadHClass(thisValue);
                GateRef jsType = GetObjectType(hclass);
                GateRef ele = FastGetPropertyByIndex(glue, thisValue, TruncInt64ToInt32(*relativeFromIndex), jsType);
                BRANCH(FastStrictEqual(glue, value, ele, ProfileOperation()), &isFound, &loopEnd);
                Bind(&isFound);
                {
                    result->WriteVariable(IntToTaggedPtr(*relativeFromIndex));
                    Jump(exit);
                }
            }
        }
        Bind(&loopEnd);
        relativeFromIndex = Int64Sub(*relativeFromIndex, Int64(1));
        LoopEnd(&loopHead);
        Bind(&loopExit);
        result->WriteVariable(IntToTaggedPtr(Int32(-1)));
        Jump(exit);
    }
}

void BuiltinsTypedArrayStubBuilder::IndexOf(GateRef glue, GateRef thisValue, GateRef numArgs,
    Variable *result, Label *exit, Label *slowPath)
{
    auto env = GetEnvironment();
    Label ecmaObj(env);
    Label typedArray(env);
    Label defaultConstr(env);
    BRANCH(IsEcmaObject(thisValue), &ecmaObj, slowPath);
    Bind(&ecmaObj);
    BRANCH(IsTypedArray(thisValue), &typedArray, slowPath);
    Bind(&typedArray);
    BRANCH(HasConstructor(thisValue), slowPath, &defaultConstr);
    Bind(&defaultConstr);

    DEFVARIABLE(fromIndex, VariableType::INT64(), Int64(0));
    DEFVARIABLE(thisArrLen, VariableType::INT64(), ZExtInt32ToInt64(GetArrayLength(thisValue)));
    Label thisIsEmpty(env);
    Label thisIsNotEmpty(env);
    Label getFromIndex(env);
    Label next(env);
    result->WriteVariable(IntToTaggedPtr(Int32(-1)));
    BRANCH(Int64Equal(*thisArrLen, Int64(0)), &thisIsEmpty, &thisIsNotEmpty);
    Bind(&thisIsEmpty);
    Jump(exit);
    Bind(&thisIsNotEmpty);
    // 2 : index of the param
    BRANCH(Int64Equal(numArgs, IntPtr(2)), &getFromIndex, &next);
    Bind(&getFromIndex);
    {
        GateRef index = GetCallArg1(numArgs);
        Label taggedIsInt(env);
        Label lessThanZero(env);
        Label stillLessThanZero(env);
        BRANCH(TaggedIsInt(index), &taggedIsInt, slowPath);
        Bind(&taggedIsInt);
        fromIndex = SExtInt32ToInt64(TaggedGetInt(index));
        BRANCH(Int64LessThan(*fromIndex, Int64(0)), &lessThanZero, &next);
        Bind(&lessThanZero);
        {
            fromIndex = Int64Add(*fromIndex, *thisArrLen);
            BRANCH(Int64LessThan(*fromIndex, Int64(0)), &stillLessThanZero, &next);
            Bind(&stillLessThanZero);
            fromIndex = Int64(0);
            Jump(&next);
        }
    }
    Bind(&next);
    {
        GateRef target = GetCallArg0(numArgs);
        DEFVARIABLE(curIndex, VariableType::INT64(), *fromIndex);
        Label lessThanLength(env);
        BRANCH(Int64GreaterThanOrEqual(*curIndex, *thisArrLen), exit, &lessThanLength);
        Bind(&lessThanLength);
        {
            Label loopHead(env);
            Label loopEnd(env);
            Label loopNext(env);
            Label loopExit(env);
            Jump(&loopHead);
            LoopBegin(&loopHead);
            {
                BRANCH(Int64LessThan(*curIndex, *thisArrLen), &loopNext, &loopExit);
                Bind(&loopNext);
                {
                    GateRef kValue = FastGetPropertyByIndex(glue, thisValue,
                        TruncInt64ToInt32(*curIndex), GetObjectType(LoadHClass(thisValue)));
                    Label hasException0(env);
                    Label notHasException0(env);
                    BRANCH(HasPendingException(glue), &hasException0, &notHasException0);
                    Bind(&hasException0);
                    {
                        result->WriteVariable(Exception());
                        Jump(exit);
                    }
                    Bind(&notHasException0);
                    {
                        Label find(env);
                        BRANCH(FastStrictEqual(glue, target, kValue, ProfileOperation()), &find, &loopEnd);
                        Bind(&find);
                        {
                            result->WriteVariable(IntToTaggedPtr(*curIndex));
                            Jump(exit);
                        }
                    }
                }
            }
            Bind(&loopEnd);
            curIndex = Int64Add(*curIndex, Int64(1));
            LoopEnd(&loopHead);
            Bind(&loopExit);
            Jump(exit);
        }
    }
}

void BuiltinsTypedArrayStubBuilder::Find(GateRef glue, GateRef thisValue, GateRef numArgs,
    Variable *result, Label *exit, Label *slowPath)
{
    auto env = GetEnvironment();
    Label isHeapObject(env);
    Label defaultConstr(env);
    Label typedArray(env);
    BRANCH(IsTypedArray(thisValue), &typedArray, slowPath);
    Bind(&typedArray);
    BRANCH(TaggedIsHeapObject(thisValue), &isHeapObject, slowPath);
    Bind(&isHeapObject);
    BRANCH(HasConstructor(thisValue), slowPath, &defaultConstr);
    Bind(&defaultConstr);
    GateRef callbackFnHandle = GetCallArg0(numArgs);
    Label arg0HeapObject(env);
    BRANCH(TaggedIsHeapObject(callbackFnHandle), &arg0HeapObject, slowPath);
    Bind(&arg0HeapObject);
    Label callable(env);
    BRANCH(IsCallable(callbackFnHandle), &callable, slowPath);
    Bind(&callable);
    GateRef argHandle = GetCallArg1(numArgs);
    GateRef thisArrLen = ZExtInt32ToInt64(GetArrayLength(thisValue));
    DEFVARIABLE(i, VariableType::INT64(), Int64(0));
    Label loopHead(env);
    Label loopEnd(env);
    Label next(env);
    Label loopExit(env);
    Jump(&loopHead);
    LoopBegin(&loopHead);
    {
        Label hasException0(env);
        Label notHasException0(env);
        BRANCH(Int64LessThan(*i, thisArrLen), &next, &loopExit);
        Bind(&next);
        GateRef kValue = FastGetPropertyByIndex(glue, thisValue, TruncInt64ToInt32(*i),
            GetObjectType(LoadHClass(thisValue)));
        BRANCH(HasPendingException(glue), &hasException0, &notHasException0);
        Bind(&hasException0);
        {
            result->WriteVariable(Exception());
            Jump(exit);
        }
        Bind(&notHasException0);
        {
            GateRef key = Int64ToTaggedInt(*i);
            Label hasException1(env);
            Label notHasException1(env);
            GateRef retValue = JSCallDispatch(glue, callbackFnHandle, Int32(NUM_MANDATORY_JSFUNC_ARGS), 0,
                Circuit::NullGate(), JSCallMode::CALL_THIS_ARG3_WITH_RETURN, { argHandle, kValue, key, thisValue });
            BRANCH(HasPendingException(glue), &hasException1, &notHasException1);
            Bind(&hasException1);
            {
                result->WriteVariable(retValue);
                Jump(exit);
            }
            Bind(&notHasException1);
            {
                Label find(env);
                BRANCH(TaggedIsTrue(FastToBoolean(retValue)), &find, &loopEnd);
                Bind(&find);
                {
                    result->WriteVariable(kValue);
                    Jump(exit);
                }
            }
        }
    }
    Bind(&loopEnd);
    i = Int64Add(*i, Int64(1));
    LoopEnd(&loopHead);
    Bind(&loopExit);
    Jump(exit);
}

void BuiltinsTypedArrayStubBuilder::Includes(GateRef glue, GateRef thisValue, GateRef numArgs,
    Variable *result, Label *exit, Label *slowPath)
{
    auto env = GetEnvironment();
    Label typedArray(env);
    Label isHeapObject(env);
    Label notFound(env);
    Label thisLenNotZero(env);
    BRANCH(IsTypedArray(thisValue), &typedArray, slowPath);
    Bind(&typedArray);
    BRANCH(TaggedIsHeapObject(thisValue), &isHeapObject, slowPath);
    Bind(&isHeapObject);
    GateRef thisLen = GetArrayLength(thisValue);
    BRANCH(Int32Equal(thisLen, Int32(0)), &notFound, &thisLenNotZero);
    Bind(&thisLenNotZero);
    {
        DEFVARIABLE(fromIndex, VariableType::INT32(), Int32(0));
        Label getArgTwo(env);
        Label nextProcess(env);
        BRANCH(Int64Equal(numArgs, IntPtr(2)), &getArgTwo, &nextProcess); // 2: 2 parameters
        Bind(&getArgTwo);
        {
            Label secondArgIsInt(env);
            GateRef fromIndexTemp = GetCallArg1(numArgs);
            BRANCH(TaggedIsInt(fromIndexTemp), &secondArgIsInt, slowPath);
            Bind(&secondArgIsInt);
            fromIndex = GetInt32OfTInt(fromIndexTemp);
            BRANCH(Int32GreaterThanOrEqual(*fromIndex, thisLen), &notFound, &nextProcess);
        }
        Bind(&nextProcess);
        {
            Label setBackZero(env);
            Label calculateFrom(env);
            Label nextCheck(env);
            BRANCH(Int64GreaterThanOrEqual(numArgs, IntPtr(1)), &nextCheck, slowPath);
            Bind(&nextCheck);
            {
                Label startLoop(env);
                BRANCH(Int32LessThan(Int32Add(*fromIndex, thisLen), Int32(0)), &setBackZero, &calculateFrom);
                Bind(&setBackZero);
                {
                    fromIndex = Int32(0);
                    Jump(&startLoop);
                }
                Bind(&calculateFrom);
                Label fromIndexLessThanZero(env);
                BRANCH(Int32GreaterThanOrEqual(*fromIndex, Int32(0)), &startLoop, &fromIndexLessThanZero);
                Bind(&fromIndexLessThanZero);
                {
                    fromIndex = Int32Add(*fromIndex, thisLen);
                    Jump(&startLoop);
                }
                Bind(&startLoop);
                GateRef searchElement = GetCallArg0(numArgs);
                Label loopHead(env);
                Label loopEnd(env);
                Label next(env);
                Label loopExit(env);
                Jump(&loopHead);
                LoopBegin(&loopHead);
                {
                    BRANCH(Int32LessThan(*fromIndex, thisLen), &next, &loopExit);
                    Bind(&next);
                    {
                        Label valueFound(env);
                        GateRef value = FastGetPropertyByIndex(glue, thisValue, *fromIndex,
                            GetObjectType(LoadHClass(thisValue)));
                        GateRef valueEqual = StubBuilder::SameValueZero(glue, searchElement, value);
                        BRANCH(valueEqual, &valueFound, &loopEnd);
                        Bind(&valueFound);
                        {
                            result->WriteVariable(TaggedTrue());
                            Jump(exit);
                        }
                    }
                }
                Bind(&loopEnd);
                fromIndex = Int32Add(*fromIndex, Int32(1));
                LoopEnd(&loopHead);
                Bind(&loopExit);
                Jump(&notFound);
            }
        }
    }
    Bind(&notFound);
    {
        result->WriteVariable(TaggedFalse());
        Jump(exit);
    }
}

void BuiltinsTypedArrayStubBuilder::CopyWithin(GateRef glue, GateRef thisValue, GateRef numArgs,
    Variable *result, Label *exit, Label *slowPath)
{
    auto env = GetEnvironment();
    Label isHeapObject(env);
    Label defaultConstr(env);
    Label typedArray(env);
    GateRef jsType = GetObjectType(LoadHClass(thisValue));
    BRANCH(TaggedIsHeapObject(thisValue), &isHeapObject, slowPath);
    Bind(&isHeapObject);
    BRANCH(IsFastTypeArray(jsType), &typedArray, slowPath);
    Bind(&typedArray);
    BRANCH(HasConstructor(thisValue), slowPath, &defaultConstr);
    Bind(&defaultConstr);

    DEFVARIABLE(startPos, VariableType::INT64(), Int64(0));
    DEFVARIABLE(endPos, VariableType::INT64(), Int64(0));
    Label targetTagExists(env);
    Label targetTagIsInt(env);
    Label startTagExists(env);
    Label startTagIsInt(env);
    Label afterCallArg1(env);
    Label endTagExists(env);
    Label endTagIsInt(env);
    Label afterCallArg2(env);
    GateRef thisLen = ZExtInt32ToInt64(GetArrayLength(thisValue));
    // 0 : index of the param
    BRANCH(Int64GreaterThanOrEqual(IntPtr(0), numArgs), slowPath, &targetTagExists);
    Bind(&targetTagExists);
    GateRef targetTag = GetCallArg0(numArgs);
    BRANCH(TaggedIsInt(targetTag), &targetTagIsInt, slowPath);
    Bind(&targetTagIsInt);
    GateRef argTarget = SExtInt32ToInt64(TaggedGetInt(targetTag));
    // 1 : index of the param
    BRANCH(Int64GreaterThanOrEqual(IntPtr(1), numArgs), &afterCallArg1, &startTagExists);
    Bind(&startTagExists);
    GateRef startTag = GetCallArg1(numArgs);
    BRANCH(TaggedIsInt(startTag), &startTagIsInt, slowPath);
    Bind(&startTagIsInt);
    startPos = SExtInt32ToInt64(TaggedGetInt(startTag));
    Jump(&afterCallArg1);
    Bind(&afterCallArg1);
    endPos = thisLen;
    // 2 : index of the param
    BRANCH(Int64GreaterThanOrEqual(IntPtr(2), numArgs), &afterCallArg2, &endTagExists);
    Bind(&endTagExists);
    GateRef endTag = GetCallArg2(numArgs);
    BRANCH(TaggedIsInt(endTag), &endTagIsInt, slowPath);
    Bind(&endTagIsInt);
    endPos = SExtInt32ToInt64(TaggedGetInt(endTag));
    Jump(&afterCallArg2);
    Bind(&afterCallArg2);

    DEFVARIABLE(copyTo, VariableType::INT64(), Int64(0));
    DEFVARIABLE(copyFrom, VariableType::INT64(), Int64(0));
    DEFVARIABLE(copyEnd, VariableType::INT64(), Int64(0));
    DEFVARIABLE(count, VariableType::INT64(), Int64(0));
    DEFVARIABLE(direction, VariableType::INT64(), Int64(0));
    Label calculateCountBranch1(env);
    Label calculateCountBranch2(env);
    Label afterCalculateCount(env);
    Label needToAdjustParam(env);
    Label afterAdjustParam(env);
    copyTo = CalculatePositionWithLength(argTarget, thisLen);
    copyFrom = CalculatePositionWithLength(*startPos, thisLen);
    copyEnd = CalculatePositionWithLength(*endPos, thisLen);
    BRANCH(Int64LessThan(Int64Sub(*copyEnd, *copyFrom), Int64Sub(thisLen, *copyTo)),
        &calculateCountBranch1, &calculateCountBranch2);
    Bind(&calculateCountBranch1);
    {
        count = Int64Sub(*copyEnd, *copyFrom);
        Jump(&afterCalculateCount);
    }
    Bind(&calculateCountBranch2);
    {
        count = Int64Sub(thisLen, *copyTo);
        Jump(&afterCalculateCount);
    }
    Bind(&afterCalculateCount);
    {
        direction = Int64(1);
        BRANCH(BoolAnd(Int64LessThan(*copyFrom, *copyTo), Int64LessThan(*copyTo, Int64Add(*copyFrom, *count))),
            &needToAdjustParam, &afterAdjustParam);
        Bind(&needToAdjustParam);
        {
            direction = Int64(-1);
            copyFrom = Int64Sub(Int64Add(*copyFrom, *count), Int64(1));
            copyTo = Int64Sub(Int64Add(*copyTo, *count), Int64(1));
            Jump(&afterAdjustParam);
        }
        Bind(&afterAdjustParam);
        {
            DEFVARIABLE(kValue, VariableType::JS_ANY(), Hole());
            Label loopHead(env);
            Label loopEnd(env);
            Label next(env);
            Label loopExit(env);
            Jump(&loopHead);
            LoopBegin(&loopHead);
            {
                Label hasException0(env);
                Label notHasException0(env);
                BRANCH(Int64GreaterThan(*count, Int64(0)), &next, &loopExit);
                Bind(&next);
                kValue = FastGetPropertyByIndex(glue, thisValue, TruncInt64ToInt32(*copyFrom), jsType);
                BRANCH(HasPendingException(glue), &hasException0, &notHasException0);
                Bind(&hasException0);
                {
                    result->WriteVariable(Exception());
                    Jump(exit);
                }
                Bind(&notHasException0);
                {
                    StoreTypedArrayElement(glue, thisValue, *copyTo, *kValue, jsType);
                    Jump(&loopEnd);
                }
            }
            Bind(&loopEnd);
            copyFrom = Int64Add(*copyFrom, *direction);
            copyTo = Int64Add(*copyTo, *direction);
            count = Int64Sub(*count, Int64(1));
            LoopEnd(&loopHead);
            Bind(&loopExit);
            result->WriteVariable(thisValue);
            Jump(exit);
        }
    }
}

void BuiltinsTypedArrayStubBuilder::ReduceRight(GateRef glue, GateRef thisValue, GateRef numArgs,
    Variable *result, Label *exit, Label *slowPath)
{
    auto env = GetEnvironment();
    Label ecmaObj(env);
    Label defaultConstr(env);
    Label atLeastOneArg(env);
    Label callbackFnHandleHeapObject(env);
    Label callbackFnHandleCallable(env);
    Label noTypeError(env);
    Label typedArray(env);

    BRANCH(IsEcmaObject(thisValue), &ecmaObj, slowPath);
    Bind(&ecmaObj);
    BRANCH(IsTypedArray(thisValue), &typedArray, slowPath);
    Bind(&typedArray);
    BRANCH(HasConstructor(thisValue), slowPath, &defaultConstr);
    Bind(&defaultConstr);
    GateRef thisLen = GetArrayLength(thisValue);
    BRANCH(Int64GreaterThanOrEqual(numArgs, IntPtr(1)), &atLeastOneArg, slowPath);
    Bind(&atLeastOneArg);
    GateRef callbackFnHandle = GetCallArg0(numArgs);
    BRANCH(TaggedIsHeapObject(callbackFnHandle), &callbackFnHandleHeapObject, slowPath);
    Bind(&callbackFnHandleHeapObject);
    BRANCH(IsCallable(callbackFnHandle), &callbackFnHandleCallable, slowPath);
    Bind(&callbackFnHandleCallable);
    GateRef thisLenIsZero = Int32Equal(thisLen, Int32(0));
    GateRef numArgsLessThanTwo = Int64LessThan(numArgs, IntPtr(2));
    BRANCH(BoolAnd(thisLenIsZero, numArgsLessThanTwo), slowPath, &noTypeError);
    Bind(&noTypeError);
    {
        DEFVARIABLE(accumulator, VariableType::JS_ANY(), Undefined());
        DEFVARIABLE(k, VariableType::INT32(), Int32Sub(thisLen, Int32(1)));
        Label updateAccumulator(env);
        Label accumulateBegin(env);
        Label defaultValue(env);
        // 2 : index of the param
        BRANCH(Int64Equal(numArgs, IntPtr(2)), &updateAccumulator, &defaultValue);
        Bind(&updateAccumulator);
        {
            accumulator = GetCallArg1(numArgs);
            Jump(&accumulateBegin);
        }
        Bind(&defaultValue);
        {
            accumulator = FastGetPropertyByIndex(glue, thisValue, *k, GetObjectType(LoadHClass(thisValue)));
            k = Int32Sub(*k, Int32(1));
            Jump(&accumulateBegin);
        }
        Bind(&accumulateBegin);
        {
            // 4 : max value of the param amount
            GateRef argsLength = Int32(4);
            NewObjectStubBuilder newBuilder(this);
            GateRef argList = newBuilder.NewTaggedArray(glue, argsLength);
            Label loopHead(env);
            Label next(env);
            Label loopEnd(env);
            Label loopExit(env);
            Jump(&loopHead);
            LoopBegin(&loopHead);
            {
                BRANCH(Int32GreaterThanOrEqual(*k, Int32(0)), &next, &loopExit);
                Bind(&next);
                {
                    GateRef kValue = FastGetPropertyByIndex(glue, thisValue, *k, GetObjectType(LoadHClass(thisValue)));
                    // 0 : the first position
                    SetValueToTaggedArray(VariableType::JS_ANY(), glue, argList, Int32(0), *accumulator);
                    // 1 : the second position
                    SetValueToTaggedArray(VariableType::JS_ANY(), glue, argList, Int32(1), kValue);
                    // 2 : the third position
                    SetValueToTaggedArray(VariableType::INT32(), glue, argList, Int32(2), IntToTaggedInt(*k));
                    // 3 : the fourth position
                    SetValueToTaggedArray(VariableType::JS_ANY(), glue, argList, Int32(3), thisValue);
                    GateRef argv = PtrAdd(argList, IntPtr(TaggedArray::DATA_OFFSET));
                    GateRef callResult = JSCallDispatch(glue, callbackFnHandle, argsLength, 0,
                        Circuit::NullGate(), JSCallMode::CALL_THIS_ARGV_WITH_RETURN,
                        {argsLength, argv, Undefined()});
                    Label hasException1(env);
                    Label notHasException1(env);
                    BRANCH(HasPendingException(glue), &hasException1, &notHasException1);
                    Bind(&hasException1);
                    {
                        result->WriteVariable(Exception());
                        Jump(exit);
                    }
                    Bind(&notHasException1);
                    {
                        accumulator = callResult;
                        Jump(&loopEnd);
                    }
                }
            }
            Bind(&loopEnd);
            k = Int32Sub(*k, Int32(1));
            LoopEnd(&loopHead);
            Bind(&loopExit);
            result->WriteVariable(*accumulator);
            Jump(exit);
        }
    }
}

void BuiltinsTypedArrayStubBuilder::Reduce(GateRef glue, GateRef thisValue, GateRef numArgs,
    Variable *result, Label *exit, Label *slowPath)
{
    auto env = GetEnvironment();
    Label ecmaObj(env);
    Label defaultConstr(env);
    Label atLeastOneArg(env);
    Label callbackFnHandleHeapObject(env);
    Label callbackFnHandleCallable(env);
    Label noTypeError(env);
    Label typedArray(env);

    BRANCH(IsEcmaObject(thisValue), &ecmaObj, slowPath);
    Bind(&ecmaObj);
    BRANCH(IsTypedArray(thisValue), &typedArray, slowPath);
    Bind(&typedArray);
    BRANCH(HasConstructor(thisValue), slowPath, &defaultConstr);
    Bind(&defaultConstr);
    GateRef thisLen = GetArrayLength(thisValue);
    BRANCH(Int64GreaterThanOrEqual(numArgs, IntPtr(1)), &atLeastOneArg, slowPath);
    Bind(&atLeastOneArg);
    GateRef callbackFnHandle = GetCallArg0(numArgs);
    BRANCH(TaggedIsHeapObject(callbackFnHandle), &callbackFnHandleHeapObject, slowPath);
    Bind(&callbackFnHandleHeapObject);
    BRANCH(IsCallable(callbackFnHandle), &callbackFnHandleCallable, slowPath);
    Bind(&callbackFnHandleCallable);
    GateRef thisLenIsZero = Int32Equal(thisLen, Int32(0));
    GateRef numArgsLessThanTwo = Int64LessThan(numArgs, IntPtr(2));
    BRANCH(BoolAnd(thisLenIsZero, numArgsLessThanTwo), slowPath, &noTypeError);
    Bind(&noTypeError);
    {
        DEFVARIABLE(accumulator, VariableType::JS_ANY(), Undefined());
        DEFVARIABLE(k, VariableType::INT32(), Int32(0));
        Label updateAccumulator(env);
        Label accumulateBegin(env);
        Label defaultValue(env);
        // 2 : index of the param
        BRANCH(Int64Equal(numArgs, IntPtr(2)), &updateAccumulator, &defaultValue);
        Bind(&updateAccumulator);
        {
            accumulator = GetCallArg1(numArgs);
            Jump(&accumulateBegin);
        }
        Bind(&defaultValue);
        {
            accumulator = FastGetPropertyByIndex(glue, thisValue, Int32(0), GetObjectType(LoadHClass(thisValue)));
            k = Int32Add(*k, Int32(1));
            Jump(&accumulateBegin);
        }
        Bind(&accumulateBegin);
        {
            // 4 : max value of the param amount
            GateRef argsLength = Int32(4);
            NewObjectStubBuilder newBuilder(this);
            GateRef argList = newBuilder.NewTaggedArray(glue, argsLength);
            Label loopHead(env);
            Label next(env);
            Label loopEnd(env);
            Label loopExit(env);
            Jump(&loopHead);
            LoopBegin(&loopHead);
            {
                BRANCH(Int32LessThan(*k, thisLen), &next, &loopExit);
                Bind(&next);
                {
                    GateRef kValue = FastGetPropertyByIndex(glue, thisValue, *k, GetObjectType(LoadHClass(thisValue)));
                    // 0 : the first position
                    SetValueToTaggedArray(VariableType::JS_ANY(), glue, argList, Int32(0), *accumulator);
                    // 1 : the second position
                    SetValueToTaggedArray(VariableType::JS_ANY(), glue, argList, Int32(1), kValue);
                    // 2 : the third position
                    SetValueToTaggedArray(VariableType::INT32(), glue, argList, Int32(2), IntToTaggedInt(*k));
                    // 3 : the fourth position
                    SetValueToTaggedArray(VariableType::JS_ANY(), glue, argList, Int32(3), thisValue);
                    GateRef argv = PtrAdd(argList, IntPtr(TaggedArray::DATA_OFFSET));
                    GateRef callResult = JSCallDispatch(glue, callbackFnHandle, argsLength, 0,
                        Circuit::NullGate(), JSCallMode::CALL_THIS_ARGV_WITH_RETURN,
                        {argsLength, argv, Undefined()});
                    Label hasException1(env);
                    Label notHasException1(env);
                    BRANCH(HasPendingException(glue), &hasException1, &notHasException1);
                    Bind(&hasException1);
                    {
                        result->WriteVariable(Exception());
                        Jump(exit);
                    }
                    Bind(&notHasException1);
                    {
                        accumulator = callResult;
                        Jump(&loopEnd);
                    }
                }
            }
            Bind(&loopEnd);
            k = Int32Add(*k, Int32(1));
            LoopEnd(&loopHead);
            Bind(&loopExit);
            result->WriteVariable(*accumulator);
            Jump(exit);
        }
    }
}

void BuiltinsTypedArrayStubBuilder::Every(GateRef glue, GateRef thisValue,  GateRef numArgs,
    Variable *result, Label *exit, Label *slowPath)
{
    auto env = GetEnvironment();
    Label isHeapObject(env);
    Label typedArray(env);
    Label defaultConstr(env);
    BRANCH(TaggedIsHeapObject(thisValue), &isHeapObject, slowPath);
    Bind(&isHeapObject);
    BRANCH(IsTypedArray(thisValue), &typedArray, slowPath);
    Bind(&typedArray);
    BRANCH(HasConstructor(thisValue), slowPath, &defaultConstr);
    Bind(&defaultConstr);
    Label arg0HeapObject(env);
    Label callable(env);
    GateRef callbackFnHandle = GetCallArg0(numArgs);
    BRANCH(TaggedIsHeapObject(callbackFnHandle), &arg0HeapObject, slowPath);
    Bind(&arg0HeapObject);
    BRANCH(IsCallable(callbackFnHandle), &callable, slowPath);
    Bind(&callable);
    GateRef argHandle = GetCallArg1(numArgs);
    DEFVARIABLE(i, VariableType::INT32(), Int32(0));
    DEFVARIABLE(kValue, VariableType::JS_ANY(), Hole());
    GateRef thisArrlen = GetArrayLength(thisValue);
    Label loopHead(env);
    Label loopEnd(env);
    Label next(env);
    Label loopExit(env);
    Jump(&loopHead);
    LoopBegin(&loopHead);
    {
        Label hasException0(env);
        Label notHasException0(env);
        Label hasException1(env);
        Label notHasException1(env);
        Label retValueIsFalse(env);
        BRANCH(Int32LessThan(*i, thisArrlen), &next, &loopExit);
        Bind(&next);
        kValue = FastGetPropertyByIndex(glue, thisValue, *i, GetObjectType(LoadHClass(thisValue)));
        BRANCH(HasPendingException(glue), &hasException0, &notHasException0);
        Bind(&hasException0);
        {
            result->WriteVariable(Exception());
            Jump(exit);
        }
        Bind(&notHasException0);
        {
            GateRef key = IntToTaggedInt(*i);
            GateRef retValue = JSCallDispatch(glue, callbackFnHandle, Int32(NUM_MANDATORY_JSFUNC_ARGS), 0,
                Circuit::NullGate(), JSCallMode::CALL_THIS_ARG3_WITH_RETURN, { argHandle, *kValue, key, thisValue });
            BRANCH(HasPendingException(glue), &hasException1, &notHasException1);
            Bind(&hasException1);
            {
                result->WriteVariable(Exception());
                Jump(exit);
            }
            Bind(&notHasException1);
            {
                BRANCH(TaggedIsFalse(FastToBoolean(retValue)), &retValueIsFalse, &loopEnd);
                Bind(&retValueIsFalse);
                {
                    result->WriteVariable(TaggedFalse());
                    Jump(exit);
                }
            }
        }
    }
    Bind(&loopEnd);
    i = Int32Add(*i, Int32(1));
    LoopEnd(&loopHead);
    Bind(&loopExit);
    result->WriteVariable(TaggedTrue());
    Jump(exit);
}

void BuiltinsTypedArrayStubBuilder::Some(GateRef glue, GateRef thisValue, GateRef numArgs,
    Variable *result, Label *exit, Label *slowPath)
{
    auto env = GetEnvironment();
    Label ecmaObj(env);
    Label typedArray(env);
    Label thisExists(env);
    BRANCH(IsEcmaObject(thisValue), &ecmaObj, slowPath);
    Bind(&ecmaObj);
    BRANCH(IsTypedArray(thisValue), &typedArray, slowPath);
    Bind(&typedArray);
    BRANCH(TaggedIsUndefinedOrNull(thisValue), slowPath, &thisExists);
    Bind(&thisExists);

    Label arg0HeapObject(env);
    Label callable(env);
    GateRef callbackFnHandle = GetCallArg0(numArgs);
    BRANCH(TaggedIsHeapObject(callbackFnHandle), &arg0HeapObject, slowPath);
    Bind(&arg0HeapObject);
    BRANCH(IsCallable(callbackFnHandle), &callable, slowPath);
    Bind(&callable);
    {
        GateRef argHandle = GetCallArg1(numArgs);
        DEFVARIABLE(i, VariableType::INT64(), Int64(0));
        DEFVARIABLE(thisArrLen, VariableType::INT64(), ZExtInt32ToInt64(GetArrayLength(thisValue)));
        DEFVARIABLE(kValue, VariableType::JS_ANY(), Hole());
        Label loopHead(env);
        Label loopEnd(env);
        Label next(env);
        Label loopExit(env);
        Jump(&loopHead);
        LoopBegin(&loopHead);
        {
            Label hasException0(env);
            Label notHasException0(env);
            Label callDispatch(env);
            Label hasException1(env);
            Label notHasException1(env);
            BRANCH(Int64LessThan(*i, *thisArrLen), &next, &loopExit);
            Bind(&next);
            kValue = FastGetPropertyByIndex(glue, thisValue, TruncInt64ToInt32(*i),
                GetObjectType(LoadHClass(thisValue)));
            BRANCH(HasPendingException(glue), &hasException0, &notHasException0);
            Bind(&hasException0);
            {
                result->WriteVariable(Exception());
                Jump(exit);
            }
            Bind(&notHasException0);
            {
                BRANCH(TaggedIsHole(*kValue), &loopEnd, &callDispatch);
            }
            Bind(&callDispatch);
            {
                GateRef key = Int64ToTaggedInt(*i);
                GateRef retValue = JSCallDispatch(glue, callbackFnHandle, Int32(NUM_MANDATORY_JSFUNC_ARGS),
                    0, Circuit::NullGate(), JSCallMode::CALL_THIS_ARG3_WITH_RETURN,
                    { argHandle, *kValue, key, thisValue });
                BRANCH(HasPendingException(glue), &hasException1, &notHasException1);
                Bind(&hasException1);
                {
                    result->WriteVariable(Exception());
                    Jump(exit);
                }
                Bind(&notHasException1);
                {
                    Label retValueIsTrue(env);
                    BRANCH(TaggedIsTrue(FastToBoolean(retValue)), &retValueIsTrue, &loopEnd);
                    Bind(&retValueIsTrue);
                    {
                        result->WriteVariable(TaggedTrue());
                        Jump(exit);
                    }
                }
            }
        }
        Bind(&loopEnd);
        i = Int64Add(*i, Int64(1));
        LoopEnd(&loopHead);
        Bind(&loopExit);
        result->WriteVariable(TaggedFalse());
        Jump(exit);
    }
}

void BuiltinsTypedArrayStubBuilder::Filter(GateRef glue, GateRef thisValue, GateRef numArgs,
    Variable *result, Label *exit, Label *slowPath)
{
    auto env = GetEnvironment();
    Label thisExists(env);
    Label isEcmaObject(env);
    Label isTypedArray(env);
    Label isFastTypedArray(env);
    Label defaultConstr(env);
    Label prototypeIsEcmaObj(env);
    Label isProtoChangeMarker(env);
    Label accessorNotChanged(env);
    BRANCH(TaggedIsUndefinedOrNull(thisValue), slowPath, &thisExists);
    Bind(&thisExists);
    BRANCH(IsEcmaObject(thisValue), &isEcmaObject, slowPath);
    Bind(&isEcmaObject);
    BRANCH(IsTypedArray(thisValue), &isTypedArray, slowPath);
    Bind(&isTypedArray);
    GateRef arrayType = GetObjectType(LoadHClass(thisValue));
    BRANCH(IsFastTypeArray(arrayType), &isFastTypedArray, slowPath);
    Bind(&isFastTypedArray);
    BRANCH(HasConstructor(thisValue), slowPath, &defaultConstr);
    Bind(&defaultConstr);
    GateRef prototype = GetPrototypeFromHClass(LoadHClass(thisValue));
    BRANCH(IsEcmaObject(prototype), &prototypeIsEcmaObj, slowPath);
    Bind(&prototypeIsEcmaObj);
    GateRef marker = GetProtoChangeMarkerFromHClass(LoadHClass(prototype));
    Branch(TaggedIsProtoChangeMarker(marker), &isProtoChangeMarker, slowPath);
    Bind(&isProtoChangeMarker);
    Branch(GetAccessorHasChanged(marker), slowPath, &accessorNotChanged);
    Bind(&accessorNotChanged);

    Label arg0HeapObject(env);
    Label callable(env);
    GateRef callbackFnHandle = GetCallArg0(numArgs);
    BRANCH(TaggedIsHeapObject(callbackFnHandle), &arg0HeapObject, slowPath);
    Bind(&arg0HeapObject);
    {
        BRANCH(IsCallable(callbackFnHandle), &callable, slowPath);
        Bind(&callable);
        {
            GateRef argHandle = GetCallArg1(numArgs);
            GateRef thisLen = GetArrayLength(thisValue);
            BuiltinsArrayStubBuilder arrayStubBuilder(this);
            GateRef kept = arrayStubBuilder.NewArray(glue, ZExtInt32ToInt64(thisLen));
            DEFVARIABLE(i, VariableType::INT32(), Int32(0));
            DEFVARIABLE(newArrayLen, VariableType::INT32(), Int32(0));
            Label loopHead(env);
            Label loopEnd(env);
            Label next(env);
            Label loopExit(env);
            Jump(&loopHead);
            LoopBegin(&loopHead);
            {
                Label hasException0(env);
                Label notHasException0(env);
                Label hasException1(env);
                Label notHasException1(env);
                Label retValueIsTrue(env);
                BRANCH(Int32LessThan(*i, thisLen), &next, &loopExit);
                Bind(&next);
                {
                    GateRef kValue = FastGetPropertyByIndex(glue, thisValue, *i, arrayType);
                    BRANCH(HasPendingException(glue), &hasException0, &notHasException0);
                    Bind(&hasException0);
                    {
                        result->WriteVariable(Exception());
                        Jump(exit);
                    }
                    Bind(&notHasException0);
                    {
                        GateRef key = IntToTaggedInt(*i);
                        GateRef retValue = JSCallDispatch(glue, callbackFnHandle, Int32(NUM_MANDATORY_JSFUNC_ARGS),
                            0, Circuit::NullGate(), JSCallMode::CALL_THIS_ARG3_WITH_RETURN,
                            { argHandle, kValue, key, thisValue });
                        BRANCH(HasPendingException(glue), &hasException1, &notHasException1);
                        Bind(&hasException1);
                        {
                            result->WriteVariable(Exception());
                            Jump(exit);
                        }
                        Bind(&notHasException1);
                        {
                            BRANCH(TaggedIsTrue(FastToBoolean(retValue)), &retValueIsTrue, &loopEnd);
                            Bind(&retValueIsTrue);
                            {
                                arrayStubBuilder.SetValueWithElementsKind(glue, kept, kValue, *newArrayLen,
                                    Boolean(true), Int32(static_cast<uint32_t>(ElementsKind::NONE)));
                                newArrayLen = Int32Add(*newArrayLen, Int32(1));
                                Jump(&loopEnd);
                            }
                        }
                    }
                }
            }
            Bind(&loopEnd);
            i = Int32Add(*i, Int32(1));
            LoopEnd(&loopHead);
            Bind(&loopExit);

            NewObjectStubBuilder newBuilder(this);
            newBuilder.SetParameters(glue, 0);
            GateRef newArray = newBuilder.NewTypedArray(glue, thisValue, arrayType, TruncInt64ToInt32(*newArrayLen));
            i = Int32(0);
            Label loopHead2(env);
            Label loopEnd2(env);
            Label next2(env);
            Label loopExit2(env);
            Jump(&loopHead2);
            LoopBegin(&loopHead2);
            {
                BRANCH(Int32LessThan(*i, *newArrayLen), &next2, &loopExit2);
                Bind(&next2);
                {
                    GateRef kValue = arrayStubBuilder.GetTaggedValueWithElementsKind(kept, *i);
                    StoreTypedArrayElement(glue, newArray, ZExtInt32ToInt64(*i), kValue, arrayType);
                    Jump(&loopEnd2);
                }
            }
            Bind(&loopEnd2);
            i = Int32Add(*i, Int32(1));
            LoopEnd(&loopHead2);
            Bind(&loopExit2);

            result->WriteVariable(newArray);
            Jump(exit);
        }
    }
}

void BuiltinsTypedArrayStubBuilder::Slice(GateRef glue, GateRef thisValue, GateRef numArgs,
    Variable *result, Label *exit, Label *slowPath)
{
    auto env = GetEnvironment();
    Label thisExists(env);
    Label isEcmaObject(env);
    Label isTypedArray(env);
    Label isFastTypedArray(env);
    Label defaultConstr(env);
    BRANCH(TaggedIsUndefinedOrNull(thisValue), slowPath, &thisExists);
    Bind(&thisExists);
    BRANCH(IsEcmaObject(thisValue), &isEcmaObject, slowPath);
    Bind(&isEcmaObject);
    BRANCH(IsTypedArray(thisValue), &isTypedArray, slowPath);
    Bind(&isTypedArray);
    GateRef arrayType = GetObjectType(LoadHClass(thisValue));
    BRANCH(IsFastTypeArray(arrayType), &isFastTypedArray, slowPath);
    Bind(&isFastTypedArray);
    BRANCH(HasConstructor(thisValue), slowPath, &defaultConstr);
    Bind(&defaultConstr);

    DEFVARIABLE(startPos, VariableType::INT64(), Int64(0));
    DEFVARIABLE(endPos, VariableType::INT64(), Int64(0));
    DEFVARIABLE(newArrayLen, VariableType::INT64(), Int64(0));
    Label startTagExists(env);
    Label startTagIsInt(env);
    Label afterCallArg(env);
    Label endTagExists(env);
    Label endTagIsInt(env);
    Label adjustArrLen(env);
    Label newTypedArray(env);
    Label writeVariable(env);
    Label copyBuffer(env);
    GateRef thisLen = ZExtInt32ToInt64(GetArrayLength(thisValue));
    BRANCH(Int64GreaterThanOrEqual(IntPtr(0), numArgs), slowPath, &startTagExists);
    Bind(&startTagExists);
    {
        GateRef startTag = GetCallArg0(numArgs);
        BRANCH(TaggedIsInt(startTag), &startTagIsInt, slowPath);
        Bind(&startTagIsInt);
        {
            startPos = SExtInt32ToInt64(TaggedGetInt(startTag));
            endPos = thisLen;
            BRANCH(Int64GreaterThanOrEqual(IntPtr(1), numArgs), &afterCallArg, &endTagExists);
            Bind(&endTagExists);
            {
                GateRef endTag = GetCallArg1(numArgs);
                BRANCH(TaggedIsInt(endTag), &endTagIsInt, slowPath);
                Bind(&endTagIsInt);
                {
                    endPos = SExtInt32ToInt64(TaggedGetInt(endTag));
                    Jump(&afterCallArg);
                }
            }
            Bind(&afterCallArg);
            {
                startPos = CalculatePositionWithLength(*startPos, thisLen);
                endPos = CalculatePositionWithLength(*endPos, thisLen);
                BRANCH(Int64GreaterThan(*endPos, *startPos), &adjustArrLen, &newTypedArray);
                Bind(&adjustArrLen);
                {
                    newArrayLen = Int64Sub(*endPos, *startPos);
                    Jump(&newTypedArray);
                }
            }
        }
    }
    Bind(&newTypedArray);
    {
        NewObjectStubBuilder newBuilder(this);
        newBuilder.SetParameters(glue, 0);
        GateRef newArray = newBuilder.NewTypedArray(glue, thisValue, arrayType, TruncInt64ToInt32(*newArrayLen));
        BRANCH(Int32Equal(TruncInt64ToInt32(*newArrayLen), Int32(0)), &writeVariable, &copyBuffer);
        Bind(&copyBuffer);
        {
            CallNGCRuntime(glue, RTSTUB_ID(CopyTypedArrayBuffer), { thisValue, newArray, TruncInt64ToInt32(*startPos),
                Int32(0), TruncInt64ToInt32(*newArrayLen), newBuilder.GetElementSizeFromType(glue, arrayType) });
            Jump(&writeVariable);
        }
        Bind(&writeVariable);
        {
            result->WriteVariable(newArray);
            Jump(exit);
        }
    }
}

void BuiltinsTypedArrayStubBuilder::SubArray(GateRef glue, GateRef thisValue, GateRef numArgs,
    Variable *result, Label *exit, Label *slowPath)
{
    auto env = GetEnvironment();
    Label ecmaObj(env);
    Label typedArray(env);
    Label isNotZero(env);
    DEFVARIABLE(beginIndex, VariableType::INT32(), Int32(0));
    DEFVARIABLE(endIndex, VariableType::INT32(), Int32(0));
    DEFVARIABLE(newLength, VariableType::INT32(), Int32(0));

    BRANCH(IsEcmaObject(thisValue), &ecmaObj, slowPath);
    Bind(&ecmaObj);
    BRANCH(IsTypedArray(thisValue), &typedArray, slowPath);
    Bind(&typedArray);

    GateRef objHclass = LoadHClass(thisValue);
    Label defaultConstructor(env);
    BRANCH(HasConstructorByHClass(objHclass), slowPath, &defaultConstructor);
    Bind(&defaultConstructor);
    GateRef arrayLen = GetArrayLength(thisValue);
    GateRef buffer = GetViewedArrayBuffer(thisValue);
    Label offHeap(env);
    BRANCH(BoolOr(IsJSObjectType(buffer, JSType::JS_ARRAY_BUFFER),
        IsJSObjectType(buffer, JSType::JS_SHARED_ARRAY_BUFFER)), &offHeap, slowPath);
    Bind(&offHeap);
    Label notDetached(env);
    BRANCH(IsDetachedBuffer(buffer), slowPath, &notDetached);
    Bind(&notDetached);

    Label intIndex(env);
    GateRef relativeBegin = GetCallArg0(numArgs);
    GateRef end = GetCallArg1(numArgs);
    BRANCH(TaggedIsInt(relativeBegin), &intIndex, slowPath);
    Bind(&intIndex);
    GateRef relativeBeginInt = GetInt32OfTInt(relativeBegin);
    beginIndex = CalArrayRelativePos(relativeBeginInt, arrayLen);

    Label undefEnd(env);
    Label defEnd(env);
    Label calNewLength(env);
    Label newArray(env);
    BRANCH(TaggedIsUndefined(end), &undefEnd, &defEnd);
    Bind(&undefEnd);
    {
        endIndex = arrayLen;
        Jump(&calNewLength);
    }
    Bind(&defEnd);
    {
        Label intEnd(env);
        BRANCH(TaggedIsInt(end), &intEnd, slowPath);
        Bind(&intEnd);
        {
            GateRef endVal = GetInt32OfTInt(end);
            endIndex = CalArrayRelativePos(endVal, arrayLen);
            Jump(&calNewLength);
        }
    }
    Bind(&calNewLength);
    {
        GateRef diffLen = Int32Sub(*endIndex, *beginIndex);
        Label diffLargeZero(env);
        BRANCH(Int32GreaterThan(diffLen, Int32(0)), &diffLargeZero, &newArray);
        Bind(&diffLargeZero);
        {
            newLength = diffLen;
            Jump(&newArray);
        }
    }
    Bind(&newArray);
    GateRef oldByteLength = Load(VariableType::INT32(), thisValue, IntPtr(JSTypedArray::BYTE_LENGTH_OFFSET));
    BRANCH(Int32Equal(arrayLen, Int32(0)), slowPath, &isNotZero);
    Bind(&isNotZero);
    GateRef elementSize = Int32Div(oldByteLength, arrayLen);
    NewObjectStubBuilder newBuilder(this);
    *result = newBuilder.NewTaggedSubArray(glue, thisValue, elementSize, *newLength, *beginIndex, objHclass, buffer);
    Jump(exit);
}

void BuiltinsTypedArrayStubBuilder::With(GateRef glue, GateRef thisValue, GateRef numArgs,
    Variable *result, Label *exit, Label *slowPath)
{
    auto env = GetEnvironment();
    DEFVARIABLE(relativeIndex, VariableType::INT64(), Int64(0));
    DEFVARIABLE(actualIndex, VariableType::INT64(), Int64(0));
    Label isHeapObject(env);
    Label typedArray(env);
    Label notCOWArray(env);
    BRANCH(TaggedIsHeapObject(thisValue), &isHeapObject, slowPath);
    Bind(&isHeapObject);
    BRANCH(IsTypedArray(thisValue), &typedArray, slowPath);
    Bind(&typedArray);
    BRANCH(IsJsCOWArray(thisValue), slowPath, &notCOWArray);
    Bind(&notCOWArray);
    GateRef thisLen = ZExtInt32ToInt64(GetArrayLength(thisValue));
    GateRef index = GetCallArg0(numArgs);
    Label taggedIsInt(env);
    BRANCH(TaggedIsInt(index), &taggedIsInt, slowPath);
    Bind(&taggedIsInt);
    relativeIndex = GetInt64OfTInt(index);
    DEFVARIABLE(value, VariableType::JS_ANY(), Undefined());
    Label indexGreaterOrEqualZero(env);
    Label indexLessZero(env);
    Label next(env);
    Label notOutOfRange(env);
    value = GetCallArg1(numArgs);
    GateRef hclass = LoadHClass(thisValue);
    GateRef jsType = GetObjectType(hclass);
    NewObjectStubBuilder newBuilder(this);
    newBuilder.SetParameters(glue, 0);
    GateRef newArray = newBuilder.NewTypedArray(glue, thisValue, jsType, TruncInt64ToInt32(thisLen));
    CallNGCRuntime(glue, RTSTUB_ID(CopyTypedArrayBuffer),
        {thisValue, newArray, Int32(0), Int32(0), TruncInt64ToInt32(thisLen),
        newBuilder.GetElementSizeFromType(glue, jsType)});
    BRANCH(Int64GreaterThanOrEqual(*relativeIndex, Int64(0)), &indexGreaterOrEqualZero, &indexLessZero);
    Bind(&indexGreaterOrEqualZero);
    {
        actualIndex = *relativeIndex;
        Jump(&next);
    }
    Bind(&indexLessZero);
    {
        actualIndex = Int64Add(thisLen, *relativeIndex);
        Jump(&next);
    }
    Bind(&next);
    {
        BRANCH(BoolOr(Int64GreaterThanOrEqual(*actualIndex, thisLen), Int64LessThan(*actualIndex, Int64(0))),
            slowPath, &notOutOfRange);
        Bind(&notOutOfRange);
        {
            DEFVARIABLE(k, VariableType::INT64(), Int64(0));
            Label loopHead(env);
            Label loopEnd(env);
            Label loopExit(env);
            Label loopNext(env);
            Label replaceIndex(env);
            Jump(&loopHead);
            LoopBegin(&loopHead);
            {
                BRANCH(Int64LessThan(*k, thisLen), &loopNext, &loopExit);
                Bind(&loopNext);
                BRANCH(Int64Equal(*k, *actualIndex), &replaceIndex, &loopEnd);
                Bind(&replaceIndex);
                {
                    StoreTypedArrayElement(glue, newArray, *k, *value, jsType);
                    Jump(&loopEnd);
                }
            }
            Bind(&loopEnd);
            k = Int64Add(*k, Int64(1));
            LoopEnd(&loopHead);
            Bind(&loopExit);
            result->WriteVariable(newArray);
            Jump(exit);
        }
    }
}

void BuiltinsTypedArrayStubBuilder::GetByteLength([[maybe_unused]] GateRef glue, GateRef thisValue,
    [[maybe_unused]] GateRef numArgs, Variable *result, Label *exit, Label *slowPath)
{
    auto env = GetEnvironment();
    Label ecmaObj(env);
    Label typedArray(env);
    Label Detached(env);
    Label notDetached(env);
    BRANCH(IsEcmaObject(thisValue), &ecmaObj, slowPath);
    Bind(&ecmaObj);
    BRANCH(IsTypedArray(thisValue), &typedArray, slowPath);
    Bind(&typedArray);
    GateRef buffer = GetViewedArrayBuffer(thisValue);
    BRANCH(IsDetachedBuffer(buffer), &Detached, &notDetached);
    Bind(&Detached);
    {
        *result = IntToTaggedPtr(Int32(0));
        Jump(exit);
    }
    Bind(&notDetached);
    {
        *result = IntToTaggedPtr(GetArrayLength(thisValue));
    }
    Jump(exit);
}

void BuiltinsTypedArrayStubBuilder::DoSort(
    GateRef glue, GateRef receiver, Variable* result, Label* exit, Label* slowPath)
{
    auto env = GetEnvironment();
    Label entry(env);
    Label lenGreaterZero(env);
    env->SubCfgEntry(&entry);
    GateRef len = ZExtInt32ToInt64(GetArrayLength(receiver));
    DEFVARIABLE(i, VariableType::INT64(), Int64(1));
    DEFVARIABLE(presentValue, VariableType::JS_ANY(), Undefined());
    DEFVARIABLE(middleValue, VariableType::JS_ANY(), Undefined());
    DEFVARIABLE(previousValue, VariableType::JS_ANY(), Undefined());
    Label loopHead(env);
    Label loopEnd(env);
    Label loopNext(env);
    Label loopExit(env);
    Label isNumber(env);
    Label hasException0(env);
    Label notHasException0(env);

    GateRef jsType = GetObjectType(LoadHClass(receiver));
    presentValue = FastGetPropertyByIndex(glue, receiver, Int32(0), jsType);
    BRANCH(HasPendingException(glue), &hasException0, &notHasException0);
    Bind(&hasException0);
    {
        result->WriteVariable(Exception());
        Jump(exit);
    }
    Bind(&notHasException0);
    {
        BRANCH(TaggedIsNumber(*presentValue), &isNumber, slowPath);
        Bind(&isNumber);
        BRANCH(Int64GreaterThan(len, Int64(0)), &lenGreaterZero, slowPath);
        Bind(&lenGreaterZero);

        GateRef isIntOrNot = TaggedIsInt(*presentValue);
        GateRef isUint32 = Int32Equal(jsType, Int32(static_cast<int32_t>(JSType::JS_UINT32_ARRAY)));

        Jump(&loopHead);
        LoopBegin(&loopHead);
        {
            BRANCH(Int64LessThan(*i, len), &loopNext, &loopExit);
            Bind(&loopNext);

            Label hasException1(env);
            Label notHasException1(env);
            DEFVARIABLE(beginIndex, VariableType::INT64(), Int64(0));
            DEFVARIABLE(endIndex, VariableType::INT64(), *i);
            presentValue = FastGetPropertyByIndex(glue, receiver, TruncInt64ToInt32(*i), jsType);
            BRANCH(HasPendingException(glue), &hasException1, &notHasException1);
            Bind(&hasException1);
            {
                result->WriteVariable(Exception());
                Jump(exit);
            }
            Bind(&notHasException1);
            {
                Label loopHead1(env);
                Label loopEnd1(env);
                Label loopNext1(env);
                Label loopExit1(env);

                Jump(&loopHead1);
                LoopBegin(&loopHead1);
                {
                    BRANCH(Int64LessThan(*beginIndex, *endIndex), &loopNext1, &loopExit1);
                    Bind(&loopNext1);
                    Label hasException2(env);
                    Label notHasException2(env);
                    GateRef sum = Int64Add(*beginIndex, *endIndex);
                    GateRef middleIndex = Int64Div(sum, Int64(2));
                    middleValue = FastGetPropertyByIndex(glue, receiver, TruncInt64ToInt32(middleIndex), jsType);
                    BRANCH(HasPendingException(glue), &hasException2, &notHasException2);
                    Bind(&hasException2);
                    {
                        result->WriteVariable(Exception());
                        Jump(exit);
                    }
                    Bind(&notHasException2);
                    {
                        Label goSort(env);
                        Label isFloat(env);
                        Label isInt(env);
                        Label uint32Compare(env);
                        Label notUint32(env);
                        DEFVARIABLE(compareResult, VariableType::INT32(), Int32(0));
                        BRANCH(isUint32, &uint32Compare, &notUint32);
                        Bind(&notUint32);
                        BRANCH(isIntOrNot, &isInt, &isFloat);

                        Bind(&uint32Compare);
                        {
                            DEFVARIABLE(middleValueInt64, VariableType::INT64(), Int64(0));
                            DEFVARIABLE(presentValueInt64, VariableType::INT64(), Int64(0));
                            middleValueInt64 = GetInt64OfTInt(*middleValue);
                            presentValueInt64 = GetInt64OfTInt(*presentValue);

                            Label intGreater(env);
                            Label intEqualOrNot(env);
                            Label intEqual(env);
                            Label intLess(env);

                            BRANCH(
                                Int64GreaterThan(*middleValueInt64, *presentValueInt64), &intGreater, &intEqualOrNot);
                            Bind(&intGreater);
                            {
                                compareResult = Int32(1);
                                Jump(&goSort);
                            }
                            Bind(&intEqualOrNot);
                            {
                                BRANCH(Int64Equal(*middleValueInt64, *presentValueInt64), &intEqual, &intLess);
                                Bind(&intEqual);
                                {
                                    compareResult = Int32(0);
                                    Jump(&goSort);
                                }
                                Bind(&intLess);
                                {
                                    compareResult = Int32(-1);
                                    Jump(&goSort);
                                }
                            }
                        }

                        Bind(&isInt);
                        {
                            DEFVARIABLE(middleValueInt32, VariableType::INT32(), Int32(0));
                            DEFVARIABLE(presentValueInt32, VariableType::INT32(), Int32(0));
                            middleValueInt32 = GetInt32OfTInt(*middleValue);
                            presentValueInt32 = GetInt32OfTInt(*presentValue);

                            Label intGreater(env);
                            Label intEqualOrNot(env);
                            Label intEqual(env);
                            Label intLess(env);

                            BRANCH(
                                Int32GreaterThan(*middleValueInt32, *presentValueInt32), &intGreater, &intEqualOrNot);
                            Bind(&intGreater);
                            {
                                compareResult = Int32(1);
                                Jump(&goSort);
                            }

                            Bind(&intEqualOrNot);
                            {
                                BRANCH(Int32Equal(*middleValueInt32, *presentValueInt32), &intEqual, &intLess);
                                Bind(&intEqual);
                                {
                                    compareResult = Int32(0);
                                    Jump(&goSort);
                                }
                                Bind(&intLess);
                                {
                                    compareResult = Int32(-1);
                                    Jump(&goSort);
                                }
                            }
                        }
                        Bind(&isFloat);
                        {
                            Label floatLess(env);
                            Label floatEqual(env);
                            Label floatGreater(env);
                            Label floatEqualOrNot(env);
                            Label float32EqualOrNot(env);
                            Label midIsNotNAN(env);
                            Label presentIsNotNAN(env);

                            DEFVARIABLE(middleValueFloat64, VariableType::FLOAT64(), Double(0));
                            DEFVARIABLE(presentValueFloat64, VariableType::FLOAT64(), Double(0));

                            middleValueFloat64 = GetDoubleOfTDouble(*middleValue);
                            presentValueFloat64 = GetDoubleOfTDouble(*presentValue);

                            BRANCH(DoubleIsNAN(*presentValueFloat64), &floatLess, &presentIsNotNAN);
                            Bind(&presentIsNotNAN);
                            BRANCH(DoubleIsNAN(*middleValueFloat64), &floatGreater, &midIsNotNAN);
                            Bind(&midIsNotNAN);

                            BRANCH(DoubleLessThan(*middleValueFloat64, *presentValueFloat64), &floatLess,
                                &floatEqualOrNot);
                            Bind(&floatLess);
                            {
                                compareResult = Int32(-1);
                                Jump(&goSort);
                            }

                            Bind(&floatEqualOrNot);
                            {
                                BRANCH(
                                    DoubleEqual(*middleValueFloat64, *presentValueFloat64), &floatEqual, &floatGreater);
                                Bind(&floatEqual);
                                {
                                    Label mIsPositive0(env);
                                    Label mIsNotPositive0(env);
                                    GateRef valueEqual = StubBuilder::SameValueZero(
                                        glue, *middleValue, DoubleToTaggedDoublePtr(Double(0.0)));
                                    BRANCH(valueEqual, &mIsPositive0, &mIsNotPositive0);
                                    Bind(&mIsPositive0);
                                    {
                                        valueEqual = StubBuilder::SameValueZero(
                                            glue, *presentValue, DoubleToTaggedDoublePtr(Double(-0.0)));
                                        BRANCH(valueEqual, &floatGreater, &mIsNotPositive0);
                                    }
                                    Bind(&mIsNotPositive0);
                                    {
                                        compareResult = Int32(0);
                                        Jump(&goSort);
                                    }
                                }

                                Bind(&floatGreater);
                                {
                                    compareResult = Int32(1);
                                    Jump(&goSort);
                                }
                            }
                        }
                        Bind(&goSort);
                        {
                            Label less0(env);
                            Label greater0(env);
                            BRANCH(Int32LessThanOrEqual(*compareResult, Int32(0)), &less0, &greater0);
                            Bind(&greater0);
                            {
                                endIndex = middleIndex;
                                Jump(&loopEnd1);
                            }
                            Bind(&less0);
                            {
                                beginIndex = middleIndex;
                                beginIndex = Int64Add(*beginIndex, Int64(1));
                                Jump(&loopEnd1);
                            }
                        }
                    }
                }
                Bind(&loopEnd1);
                LoopEnd(&loopHead1);
                Bind(&loopExit1);

                Label shouldCopy(env);
                GateRef isGreater0 = Int64GreaterThanOrEqual(*endIndex, Int64(0));
                GateRef isLessI = Int64LessThan(*endIndex, *i);
                BRANCH(BoolAnd(isGreater0, isLessI), &shouldCopy, &loopEnd);
                Bind(&shouldCopy);
                {
                    DEFVARIABLE(j, VariableType::INT64(), *i);
                    Label loopHead2(env);
                    Label loopEnd2(env);
                    Label loopNext2(env);
                    Label loopExit2(env);
                    Jump(&loopHead2);
                    LoopBegin(&loopHead2);
                    {
                        BRANCH(Int64GreaterThan(*j, *endIndex), &loopNext2, &loopExit2);
                        Bind(&loopNext2);
                        Label hasException3(env);
                        Label notHasException3(env);
                        previousValue = FastGetPropertyByIndex(glue, receiver,
                            TruncInt64ToInt32(Int64Sub(*j, Int64(1))), GetObjectType(LoadHClass(receiver)));
                        BRANCH(HasPendingException(glue), &hasException3, &notHasException3);
                        Bind(&hasException3);
                        {
                            result->WriteVariable(Exception());
                            Jump(exit);
                        }
                        Bind(&notHasException3);
                        {
                            StoreTypedArrayElement(
                                glue, receiver, *j, *previousValue, GetObjectType(LoadHClass(receiver)));
                            Jump(&loopEnd2);
                        }
                    }
                    Bind(&loopEnd2);
                    j = Int64Sub(*j, Int64(1));
                    LoopEnd(&loopHead2);
                    Bind(&loopExit2);
                    StoreTypedArrayElement(glue, receiver, *j, *presentValue, GetObjectType(LoadHClass(receiver)));
                    Jump(&loopEnd);
                }
            }
        }
        Bind(&loopEnd);
        i = Int64Add(*i, Int64(1));
        LoopEnd(&loopHead);
        Bind(&loopExit);
        env->SubCfgExit();
    }
}

void BuiltinsTypedArrayStubBuilder::Sort(
    GateRef glue, GateRef thisValue, GateRef numArgs, Variable* result, Label* exit, Label* slowPath)
{
    auto env = GetEnvironment();
    Label isHeapObject(env);
    Label typedArray(env);
    Label defaultConstr(env);
    Label argUndefined(env);
    BRANCH(TaggedIsHeapObject(thisValue), &isHeapObject, slowPath);
    Bind(&isHeapObject);
    BRANCH(IsFastTypeArray(GetObjectType(LoadHClass(thisValue))), &typedArray, slowPath);
    Bind(&typedArray);
    BRANCH(HasConstructor(thisValue), slowPath, &defaultConstr);
    Bind(&defaultConstr);
    GateRef callbackFnHandle = GetCallArg0(numArgs);
    BRANCH(TaggedIsUndefined(callbackFnHandle), &argUndefined, slowPath);
    Bind(&argUndefined);
    DoSort(glue, thisValue, result, exit, slowPath);
    result->WriteVariable(thisValue);
    Jump(exit);
}

void BuiltinsTypedArrayStubBuilder::GetByteOffset([[maybe_unused]] GateRef glue, GateRef thisValue,
    [[maybe_unused]] GateRef numArgs, Variable *result, Label *exit, Label *slowPath)
{
    auto env = GetEnvironment();
    Label ecmaObj(env);
    Label typedArray(env);
    Label Detached(env);
    Label notDetached(env);
    BRANCH(IsEcmaObject(thisValue), &ecmaObj, slowPath);
    Bind(&ecmaObj);
    BRANCH(IsTypedArray(thisValue), &typedArray, slowPath);
    Bind(&typedArray);
    GateRef buffer = GetViewedArrayBuffer(thisValue);
    BRANCH(IsDetachedBuffer(buffer), &Detached, &notDetached);
    Bind(&Detached);
    {
        *result = IntToTaggedPtr(Int32(0));
        Jump(exit);
    }
    Bind(&notDetached);
    {
        *result = IntToTaggedPtr(GetByteOffset(thisValue));
    }
    Jump(exit);
}

void BuiltinsTypedArrayStubBuilder::Set(GateRef glue, GateRef thisValue, GateRef numArgs,
    Variable *result, Label *exit, Label *slowPath)
{
    auto env = GetEnvironment();
    Label thisExists(env);
    Label ecmaObj(env);
    Label typedArray(env);
    Label typedArrayIsFast(env);

    BRANCH(TaggedIsUndefinedOrNull(thisValue), slowPath, &thisExists);
    Bind(&thisExists);
    BRANCH(IsEcmaObject(thisValue), &ecmaObj, slowPath);
    Bind(&ecmaObj);
    BRANCH(IsTypedArray(thisValue), &typedArray, slowPath);
    Bind(&typedArray);
    BRANCH(IsFastTypeArray(GetObjectType(LoadHClass(thisValue))), &typedArrayIsFast, slowPath);
    Bind(&typedArrayIsFast);
    GateRef len = ZExtInt32ToInt64(GetArrayLength(thisValue));
    GateRef arrayType = GetObjectType(LoadHClass(thisValue));
    Label notEmptyArray(env);
    BRANCH(Int64Equal(len, Int64(0)), slowPath, &notEmptyArray);
    Bind(&notEmptyArray);
    DEFVARIABLE(realOffset, VariableType::INT64(), Int64(0));
    DEFVARIABLE(startIndex, VariableType::INT64(), Int64(0));
    Label hasOffset(env);
    Label hasNoOffset(env);
    Label numArgsIsInt(env);
    GateRef fromOffset = GetCallArg1(numArgs);
    BRANCH(TaggedIsUndefinedOrNull(fromOffset), &hasNoOffset, &hasOffset);
    Bind(&hasOffset);
    {
        Label taggedIsInt(env);
        BRANCH(TaggedIsInt(fromOffset), &taggedIsInt, slowPath);
        Bind(&taggedIsInt);
        GateRef fromIndexToTagged = SExtInt32ToInt64(TaggedGetInt(fromOffset));
        Label offsetNotLessZero(env);
        BRANCH(Int64LessThan(fromIndexToTagged, Int64(0)), slowPath, &offsetNotLessZero);
        Bind(&offsetNotLessZero);
        {
            realOffset = fromIndexToTagged;
            Jump(&hasNoOffset);
        }
    }
    Bind(&hasNoOffset);

    Label srcArrayIsEcmaObj(env);
    Label srcArrayIsTypedArray(env);
    Label srcArrayIsJsArray(env);
    GateRef srcArray = GetCallArg0(numArgs);
    BRANCH(IsEcmaObject(srcArray), &srcArrayIsEcmaObj, slowPath);
    Bind(&srcArrayIsEcmaObj);
    BRANCH(IsTypedArray(srcArray), &srcArrayIsTypedArray, slowPath);
    Bind(&srcArrayIsTypedArray);
    {
        GateRef srcType = GetObjectType(LoadHClass(srcArray));
        Label isFastTypeArray(env);
        BRANCH(IsFastTypeArray(srcType), &isFastTypeArray, slowPath);
        Bind(&isFastTypeArray);
        Label isNotSameValue(env);
        GateRef targetBuffer = GetViewedArrayBuffer(thisValue);
        GateRef srcBuffer = GetViewedArrayBuffer(srcArray);
        BRANCH(SameValue(glue, targetBuffer, srcBuffer), slowPath, &isNotSameValue);
        Bind(&isNotSameValue);
        Label isNotGreaterThanLen(env);
        GateRef srcLen = ZExtInt32ToInt64(GetArrayLength(srcArray));
        BRANCH(Int64GreaterThan(Int64Add(*realOffset, srcLen), len), slowPath, &isNotGreaterThanLen);
        Bind(&isNotGreaterThanLen);
        {
            Label isSameType(env);
            Label isNotSameType(env);
            BRANCH(Equal(srcType, arrayType), &isSameType, &isNotSameType);
            Bind(&isSameType);
            {
                NewObjectStubBuilder newBuilder(this);
                CallNGCRuntime(glue, RTSTUB_ID(CopyTypedArrayBuffer),
                    { srcArray, thisValue, Int32(0), TruncInt64ToInt32(*realOffset), TruncInt64ToInt32(srcLen),
                    newBuilder.GetElementSizeFromType(glue, arrayType) });
                Jump(exit);
            }
            Bind(&isNotSameType);
            {
                Label loopHead(env);
                Label loopEnd(env);
                Label loopExit(env);
                Label loopNext(env);
                Jump(&loopHead);
                LoopBegin(&loopHead);
                {
                    BRANCH(Int64LessThan(*startIndex, srcLen), &loopNext, &loopExit);
                    Bind(&loopNext);
                    {
                        GateRef srcValue = FastGetPropertyByIndex(glue, srcArray, TruncInt64ToInt32(*startIndex),
                            GetObjectType(LoadHClass(srcArray)));
                        StoreTypedArrayElement(glue, thisValue, *realOffset, srcValue,
                            GetObjectType(LoadHClass(thisValue)));
                        Jump(&loopEnd);
                    }
                }
                Bind(&loopEnd);
                startIndex = Int64Add(*startIndex, Int64(1));
                realOffset = Int64Add(*realOffset, Int64(1));
                LoopEnd(&loopHead);
                Bind(&loopExit);
                result->WriteVariable(Undefined());
                Jump(exit);
            }
        }
    }
}

void BuiltinsTypedArrayStubBuilder::FindIndex(GateRef glue, GateRef thisValue, GateRef numArgs,
    Variable *result, Label *exit, Label *slowPath)
{
    auto env = GetEnvironment();
    Label ecmaObj(env);
    Label typedArray(env);
    Label defaultConstr(env);
    BRANCH(IsEcmaObject(thisValue), &ecmaObj, slowPath);
    Bind(&ecmaObj);
    BRANCH(IsTypedArray(thisValue), &typedArray, slowPath);
    Bind(&typedArray);
    BRANCH(HasConstructor(thisValue), slowPath, &defaultConstr);
    Bind(&defaultConstr);

    Label arg0HeapObject(env);
    Label callable(env);
    GateRef callbackFnHandle = GetCallArg0(numArgs);
    BRANCH(TaggedIsHeapObject(callbackFnHandle), &arg0HeapObject, slowPath);
    Bind(&arg0HeapObject);
    BRANCH(IsCallable(callbackFnHandle), &callable, slowPath);
    Bind(&callable);
    result->WriteVariable(IntToTaggedPtr(Int32(-1)));

    GateRef argHandle = GetCallArg1(numArgs);
    DEFVARIABLE(thisArrLen, VariableType::INT64(), ZExtInt32ToInt64(GetArrayLength(thisValue)));
    DEFVARIABLE(j, VariableType::INT32(), Int32(0));
    Label loopHead(env);
    Label loopEnd(env);
    Label next(env);
    Label loopExit(env);
    Jump(&loopHead);
    LoopBegin(&loopHead);
    {
        thisArrLen = ZExtInt32ToInt64(GetArrayLength(thisValue));
        BRANCH(Int64LessThan(ZExtInt32ToInt64(*j), *thisArrLen), &next, &loopExit);
        Bind(&next);
        {
            Label hasException0(env);
            Label notHasException0(env);
            GateRef kValue = FastGetPropertyByIndex(glue, thisValue, *j, GetObjectType(LoadHClass(thisValue)));
            BRANCH(HasPendingException(glue), &hasException0, &notHasException0);
            Bind(&hasException0);
            {
                result->WriteVariable(Exception());
                Jump(exit);
            }
            Bind(&notHasException0);
            {
                GateRef key = IntToTaggedPtr(*j);
                Label hasException(env);
                Label notHasException(env);
                GateRef retValue = JSCallDispatch(glue, callbackFnHandle, Int32(NUM_MANDATORY_JSFUNC_ARGS),
                    0, Circuit::NullGate(), JSCallMode::CALL_THIS_ARG3_WITH_RETURN,
                    { argHandle, kValue, key, thisValue });
                BRANCH(TaggedIsException(retValue), &hasException, &notHasException);
                Bind(&hasException);
                {
                    result->WriteVariable(retValue);
                    Jump(exit);
                }
                Bind(&notHasException);
                {
                    Label find(env);
                    BRANCH(TaggedIsTrue(FastToBoolean(retValue)), &find, &loopEnd);
                    Bind(&find);
                    {
                        result->WriteVariable(key);
                        Jump(exit);
                    }
                }
            }
        }
    }
    Bind(&loopEnd);
    thisArrLen = ZExtInt32ToInt64(GetArrayLength(thisValue));
    j = Int32Add(*j, Int32(1));
    LoopEnd(&loopHead);
    Bind(&loopExit);
    Jump(exit);
}
}  // namespace panda::ecmascript::kungfu
