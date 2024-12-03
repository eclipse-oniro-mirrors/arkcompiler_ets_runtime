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
#include "ecmascript/compiler/builtins/builtins_array_stub_builder.h"

#include "ecmascript/builtins/builtins_string.h"
#include "ecmascript/compiler/builtins/builtins_stubs.h"
#include "ecmascript/compiler/call_stub_builder.h"
#include "ecmascript/compiler/new_object_stub_builder.h"
#include "ecmascript/compiler/profiler_operation.h"
#include "ecmascript/compiler/rt_call_signature.h"
#include "ecmascript/runtime_call_id.h"
#include "ecmascript/js_iterator.h"
#include "ecmascript/base/array_helper.h"

namespace panda::ecmascript::kungfu {
void BuiltinsArrayStubBuilder::UnshiftOptimised(GateRef glue, GateRef thisValue, GateRef numArgs, Variable *result,
                                                Label *exit, Label *slowPath)
{
    auto env = GetEnvironment();
    Label isHeapObject(env);
    Label isJsArray(env);
    Label isStableJsArray(env);
    Label notOverRange(env);
    Label numNotEqualZero(env);
    Label numLessThanOrEqualThree(env);
    Label afterCopy(env);
    Label grow(env);
    Label setValue(env);
    Label numEqual2(env);
    Label numEqual3(env);
    Label threeArgs(env);
    Label final(env);
    BRANCH(TaggedIsHeapObject(thisValue), &isHeapObject, slowPath);
    Bind(&isHeapObject);
    BRANCH(IsJsArray(thisValue), &isJsArray, slowPath);
    Bind(&isJsArray);
    BRANCH(IsStableJSArray(glue, thisValue), &isStableJsArray, slowPath);
    Bind(&isStableJsArray);
    BRANCH(Int64GreaterThan(numArgs, IntPtr(0)), &numNotEqualZero, slowPath);
    Bind(&numNotEqualZero);
    GateRef thisLen = ZExtInt32ToInt64(GetArrayLength(thisValue));
    GateRef argLen = ZExtInt32ToInt64(ChangeIntPtrToInt32(numArgs));
    GateRef newLen = Int64Add(thisLen, argLen);
    BRANCH(Int64GreaterThan(newLen, Int64(base::MAX_SAFE_INTEGER)), slowPath, &notOverRange);
    Bind(&notOverRange);
    // 3 : max param num
    BRANCH(Int64LessThanOrEqual(numArgs, IntPtr(3)), &numLessThanOrEqualThree, slowPath);
    Bind(&numLessThanOrEqualThree);
    GateRef capacity = ZExtInt32ToInt64(GetLengthOfTaggedArray(GetElementsArray(thisValue)));
    BRANCH(Int64GreaterThan(newLen, capacity), &grow, &setValue);
    Bind(&grow);
    {
        GrowElementsCapacity(glue, thisValue, TruncInt64ToInt32(newLen));
        Jump(&setValue);
    }
    Bind(&setValue);
    {
        Label elementsKindEnabled(env);
        GateRef elements = GetElementsArray(thisValue);
        GateRef arrayStart = GetDataPtrInTaggedArray(elements);
        GateRef moveTo = PtrAdd(arrayStart, PtrMul(numArgs, IntPtr(JSTaggedValue::TaggedTypeSize())));
        GateRef isElementsKindEnabled = IsEnableElementsKind(glue);
        Label isIntOrNumber(env);
        Label isTagged(env);
        BRANCH_NO_WEIGHT(isElementsKindEnabled, &elementsKindEnabled, &isTagged);
        Bind(&elementsKindEnabled);
        {
            GateRef kind = GetElementsKindFromHClass(LoadHClass(thisValue));
            GateRef isInt = LogicAndBuilder(env)
                            .And(Int32GreaterThanOrEqual(kind, Int32(static_cast<int32_t>(ElementsKind::INT))))
                            .And(Int32LessThanOrEqual(kind, Int32(static_cast<int32_t>(ElementsKind::HOLE_INT))))
                            .Done();
            GateRef isNumber = LogicAndBuilder(env)
                               .And(Int32GreaterThanOrEqual(kind, Int32(static_cast<int32_t>(ElementsKind::NUMBER))))
                               .And(Int32LessThanOrEqual(kind, Int32(static_cast<int32_t>(ElementsKind::HOLE_NUMBER))))
                               .Done();
            GateRef isIntOrNumberKind = LogicOrBuilder(env).Or(isInt).Or(isNumber).Done();
            BRANCH_NO_WEIGHT(isIntOrNumberKind, &isIntOrNumber, &isTagged);
            Bind(&isIntOrNumber);
            {
                ArrayCopy<MustOverlap>(glue, arrayStart, elements, moveTo, TruncInt64ToInt32(thisLen), false);
                Jump(&afterCopy);
            }
            Bind(&isTagged);
            {
                ArrayCopy<MustOverlap>(glue, arrayStart, elements, moveTo, TruncInt64ToInt32(thisLen), true);
                Jump(&afterCopy);
            }
            Bind(&afterCopy);
            {
                GateRef value0 = GetCallArg0(numArgs);
                // 0 : the first Element position
                SetValueWithElementsKind(glue, thisValue, value0, Int64(0), Boolean(false),
                                         Int32(static_cast<uint32_t>(ElementsKind::NONE)));
                // 2 : the second param
                BRANCH(Int64GreaterThanOrEqual(numArgs, IntPtr(2)), &numEqual2, &numEqual3);
                Bind(&numEqual2);
                {
                    GateRef value1 = GetCallArg1(numArgs);
                    // 1 : the second Element position
                    SetValueWithElementsKind(glue, thisValue, value1, Int64(1), Boolean(false),
                                             Int32(static_cast<uint32_t>(ElementsKind::NONE)));
                    Jump(&numEqual3);
                }
                Bind(&numEqual3);
                {
                    // 3 : the third param
                    BRANCH(Int64Equal(numArgs, IntPtr(3)), &threeArgs, &final);
                    Bind(&threeArgs);
                    GateRef value2 = GetCallArg2(numArgs);
                    // 2 : the third Element position
                    SetValueWithElementsKind(glue, thisValue, value2, Int64(2), Boolean(false),
                                             Int32(static_cast<uint32_t>(ElementsKind::NONE)));
                    Jump(&final);
                }
                Bind(&final);
                {
                    SetArrayLength(glue, thisValue, newLen);
                    result->WriteVariable(IntToTaggedPtr(newLen));
                    Jump(exit);
                }
            }
        }
    }
}

GateRef BuiltinsArrayStubBuilder::DoSortOptimised(GateRef glue, GateRef receiver, GateRef receiverState,
                                         Variable *result, Label *exit, Label *slowPath, GateRef hir)
{
    auto env = GetEnvironment();
    Label entry(env);
    env->SubCfgEntry(&entry);
    GateRef len = ZExtInt32ToInt64(GetArrayLength(receiver));
    DEFVARIABLE(i, VariableType::INT64(), Int64(1));
    DEFVARIABLE(presentValue, VariableType::JS_ANY(), Undefined());
    DEFVARIABLE(middleValue, VariableType::JS_ANY(), Undefined());
    DEFVARIABLE(previousValue, VariableType::JS_ANY(), Undefined());
    Label loopHead(env);
    Label loopEnd(env);
    Label next(env);
    Label loopExit(env);
    Jump(&loopHead);
    LoopBegin(&loopHead);
    {
        BRANCH(Int64LessThan(*i, len), &next, &loopExit);
        Bind(&next);
        DEFVARIABLE(beginIndex, VariableType::INT64(), Int64(0));
        DEFVARIABLE(endIndex, VariableType::INT64(), *i);
        Label presentValueIsHole(env);
        Label afterGettingpresentValue(env);
        Label presentValueHasProperty(env);
        Label presentValueHasException0(env);
        presentValue = GetTaggedValueWithElementsKind(receiver, *i);
        BRANCH(TaggedIsHole(*presentValue), &presentValueIsHole, &afterGettingpresentValue);
        Bind(&presentValueIsHole);
        {
            GateRef presentValueHasProp = CallRuntime(glue, RTSTUB_ID(HasProperty), {receiver, IntToTaggedInt(*i)});
            BRANCH(TaggedIsTrue(presentValueHasProp), &presentValueHasProperty, &afterGettingpresentValue);
            Bind(&presentValueHasProperty);
            {
                presentValue = FastGetPropertyByIndex(glue, receiver, TruncInt64ToInt32(*i), ProfileOperation(), hir);
                BRANCH(HasPendingException(glue), &presentValueHasException0, &afterGettingpresentValue);
                Bind(&presentValueHasException0);
                {
                    result->WriteVariable(Exception());
                    Jump(exit);
                }
            }
        }
        Bind(&afterGettingpresentValue);
        {
            Label loopHead1(env);
            Label loopEnd1(env);
            Label next1(env);
            Label loopExit1(env);
            Jump(&loopHead1);
            LoopBegin(&loopHead1);
            {
                Label middleValueIsHole(env);
                Label afterGettingmiddleValue(env);
                Label middleValueHasProperty(env);
                Label middleValueHasException0(env);
                BRANCH(Int64LessThan(*beginIndex, *endIndex), &next1, &loopExit1);
                Bind(&next1);
                GateRef sum = Int64Add(*beginIndex, *endIndex);
                GateRef middleIndex = Int64Div(sum, Int64(2)); // 2 : half
                middleValue = GetTaggedValueWithElementsKind(receiver, middleIndex);
                BRANCH(TaggedIsHole(*middleValue), &middleValueIsHole, &afterGettingmiddleValue);
                Bind(&middleValueIsHole);
                {
                    GateRef middleValueHasProp = CallRuntime(glue, RTSTUB_ID(HasProperty),
                                                             {receiver, IntToTaggedInt(middleIndex)});
                    BRANCH(TaggedIsTrue(middleValueHasProp), &middleValueHasProperty, &afterGettingmiddleValue);
                    Bind(&middleValueHasProperty);
                    {
                        middleValue = FastGetPropertyByIndex(glue, receiver,
                                                             TruncInt64ToInt32(middleIndex), ProfileOperation(), hir);
                        BRANCH(HasPendingException(glue), &middleValueHasException0, &afterGettingmiddleValue);
                        Bind(&middleValueHasException0);
                        {
                            result->WriteVariable(Exception());
                            Jump(exit);
                        }
                    }
                }
                Bind(&afterGettingmiddleValue);
                {
                    Label intOrDouble(env);
                    Label notIntAndDouble(env);
                    Label exchangeIndex(env);
                    GateRef middleVal = *middleValue;
                    GateRef presentVal = *presentValue;
                    DEFVARIABLE(compareResult, VariableType::INT32(), Int32(0));
                    GateRef intDoubleCheck = BitOr(BitAnd(TaggedIsInt(middleVal), TaggedIsInt(presentVal)),
                                                   BitAnd(TaggedIsDouble(middleVal), TaggedIsDouble(presentVal)));
                    BRANCH(intDoubleCheck, &intOrDouble, &notIntAndDouble);
                    Bind(&intOrDouble);
                    {
                        compareResult =
                            CallNGCRuntime(glue, RTSTUB_ID(FastArraySort), {*middleValue, *presentValue});
                        Jump(&exchangeIndex);
                    }
                    Bind(&notIntAndDouble);
                    {
                        Label isString(env);
                        GateRef strBool = LogicAndBuilder(env)
                                          .And(TaggedIsString(middleVal))
                                          .And(TaggedIsString(presentVal))
                                          .Done();
                        BRANCH(strBool, &isString, slowPath);
                        Bind(&isString);
                        {
                            compareResult = CallNGCRuntime(glue,
                                RTSTUB_ID(FastArraySortString), {glue, *middleValue, *presentValue});
                            Jump(&exchangeIndex);
                        }
                    }
                    Bind(&exchangeIndex);
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
            GateRef lessI = Int64LessThan(*endIndex, *i);
            BRANCH(BitAnd(isGreater0, lessI), &shouldCopy, &loopEnd);
            Bind(&shouldCopy);
            {
                DEFVARIABLE(j, VariableType::INT64(), *i);
                Label loopHead2(env);
                Label loopEnd2(env);
                Label next2(env);
                Label loopExit2(env);
                Label receiverIsNew(env);
                Label receiverIsOrigin(env);
                Label receiverIsNew2(env);
                Label receiverIsOrigin2(env);
                Jump(&loopHead2);
                LoopBegin(&loopHead2);
                {
                    Label previousValueIsHole(env);
                    Label afterGettingpreviousValue(env);
                    Label previousValueHasProperty(env);
                    Label previousValueHasException0(env);
                    BRANCH(Int64GreaterThan(*j, *endIndex), &next2, &loopExit2);
                    Bind(&next2);
                    previousValue = GetTaggedValueWithElementsKind(receiver, Int64Sub(*j, Int64(1)));
                    BRANCH(TaggedIsHole(*previousValue), &previousValueIsHole, &afterGettingpreviousValue);
                    Bind(&previousValueIsHole);
                    {
                        GateRef previousValueHasProp = CallRuntime(glue, RTSTUB_ID(HasProperty),
                                                                   {receiver, IntToTaggedInt(Int64Sub(*j, Int64(1)))});
                        BRANCH(TaggedIsTrue(previousValueHasProp),
                               &previousValueHasProperty, &afterGettingpreviousValue);
                        Bind(&previousValueHasProperty);
                        {
                            previousValue = FastGetPropertyByIndex(glue, receiver,
                                                                   TruncInt64ToInt32(Int64Sub(*j, Int64(1))),
                                                                   ProfileOperation(), hir);
                            BRANCH(HasPendingException(glue), &previousValueHasException0, &afterGettingpreviousValue);
                            Bind(&previousValueHasException0);
                            {
                                result->WriteVariable(Exception());
                                Jump(exit);
                            }
                        }
                    }
                    Bind(&afterGettingpreviousValue);
                    {
                        BRANCH(receiverState, &receiverIsNew, &receiverIsOrigin);
                        Bind(&receiverIsNew);
                        SetValueWithElementsKind(glue, receiver, *previousValue, *j, Boolean(true),
                                                 Int32(static_cast<uint32_t>(ElementsKind::NONE)));
                        Jump(&loopEnd2);
                        Bind(&receiverIsOrigin);
                        SetValueWithElementsKind(glue, receiver, *previousValue, *j, Boolean(false),
                                                 Int32(static_cast<uint32_t>(ElementsKind::NONE)));
                        Jump(&loopEnd2);
                    }
                }
                Bind(&loopEnd2);
                j = Int64Sub(*j, Int64(1));
                LoopEnd(&loopHead2);
                Bind(&loopExit2);
                BRANCH(receiverState, &receiverIsNew2, &receiverIsOrigin2);
                Bind(&receiverIsNew2);
                {
                    SetValueWithElementsKind(glue, receiver, *presentValue, *endIndex, Boolean(true),
                                             Int32(static_cast<uint32_t>(ElementsKind::NONE)));
                    Jump(&loopEnd);
                }
                Bind(&receiverIsOrigin2);
                {
                    SetValueWithElementsKind(glue, receiver, *presentValue, *endIndex, Boolean(false),
                                             Int32(static_cast<uint32_t>(ElementsKind::NONE)));
                    Jump(&loopEnd);
                }
            }
        }
    }
    Bind(&loopEnd);
    i = Int64Add(*i, Int64(1));
    LoopEnd(&loopHead);
    Bind(&loopExit);
    env->SubCfgExit();
    return receiver;
}

void BuiltinsArrayStubBuilder::ToReversedOptimised(GateRef glue, GateRef thisValue, [[maybe_unused]] GateRef numArgs,
                                          Variable *result, Label *exit, Label *slowPath)
{
    auto env = GetEnvironment();
    Label isHeapObject(env);
    Label isJsArray(env);
    Label defaultConstr(env);
    Label isStability(env);
    Label notCOWArray(env);
    BRANCH(TaggedIsHeapObject(thisValue), &isHeapObject, slowPath);
    Bind(&isHeapObject);
    BRANCH(IsJsArray(thisValue), &isJsArray, slowPath);
    Bind(&isJsArray);
    BRANCH(HasConstructor(thisValue), slowPath, &defaultConstr);
    Bind(&defaultConstr);
    BRANCH(IsStableJSArray(glue, thisValue), &isStability, slowPath);
    Bind(&isStability);
    BRANCH(IsJsCOWArray(thisValue), slowPath, &notCOWArray);
    Bind(&notCOWArray);
    Label ElementsKindEnabled(env);
    Label ElementsKindDisabled(env);
    Label next(env);
    GateRef isElementsKindEnabled = IsEnableElementsKind(glue);
    DEFVARIABLE(receiver, VariableType::JS_ANY(), Hole());
    GateRef kind = GetElementsKindFromHClass(LoadHClass(thisValue));
    GateRef thisArrLen = GetArrayLength(thisValue);
    BRANCH_UNLIKELY(isElementsKindEnabled, &ElementsKindEnabled, &ElementsKindDisabled);
    Bind(&ElementsKindEnabled);
    {
        Label newArrayIsTagged(env);
        Label reuseOldHClass(env);
        BRANCH_NO_WEIGHT(ElementsKindHasHole(kind), &newArrayIsTagged, &reuseOldHClass);
        Bind(&newArrayIsTagged);
        {
            // If the kind has hole, we know it must be transited to TAGGED kind;
            // There will be no hole in the new array because hole will be converted to undefined.
            GateRef newHClass = GetGlobalConstantValue(VariableType::JS_ANY(), glue,
                                                       ConstantIndex::ELEMENT_TAGGED_HCLASS_INDEX);
            receiver = NewEmptyArrayWithHClass(glue, newHClass);
            Jump(&next);
        }
        Bind(&reuseOldHClass);
        {
            receiver = NewEmptyArrayWithHClass(glue, LoadHClass(thisValue));
            Jump(&next);
        }
    }
    Bind(&ElementsKindDisabled);
    {
        receiver = NewArray(glue, Int64(0));
        Jump(&next);
    }
    Bind(&next);
    GrowElementsCapacity(glue, *receiver, thisArrLen);
    SetArrayLength(glue, *receiver, thisArrLen);

    Label afterReverse(env);
    Label isIntOrNumber(env);
    Label notIntOrNumber(env);
    Label isTagged(env);
    Label isHoleOrIntOrNumber(env);
    Label elementsKindEnabled(env);
    BRANCH_NO_WEIGHT(isElementsKindEnabled, &elementsKindEnabled, &isTagged);
    Bind(&elementsKindEnabled);
    {
        GateRef intOrNumber = LogicOrBuilder(env)
                              .Or(Int32Equal(kind, Int32(static_cast<int32_t>(ElementsKind::INT))))
                              .Or(Int32Equal(kind, Int32(static_cast<int32_t>(ElementsKind::NUMBER))))
                              .Done();
        BRANCH_NO_WEIGHT(intOrNumber, &isIntOrNumber, &notIntOrNumber);
        Bind(&notIntOrNumber);
        {
            GateRef holeOrIntOrNumber = LogicOrBuilder(env)
                                        .Or(Int32Equal(kind, Int32(static_cast<int32_t>(ElementsKind::HOLE_INT))))
                                        .Or(Int32Equal(kind, Int32(static_cast<int32_t>(ElementsKind::HOLE_NUMBER))))
                                        .Done();
            BRANCH_NO_WEIGHT(holeOrIntOrNumber, &isHoleOrIntOrNumber, &isTagged);
        }
    }
    Bind(&isTagged);
    {
        // The old array and new array are both TaggedArray, so load and store the element directly.
        // And barrier is needed.
        DoReverse(glue, thisValue, *receiver, true, false, MemoryAttribute::Default());
        Jump(&afterReverse);
    }
    Bind(&isIntOrNumber);
    {
        // The old array and new array are both MutantTaggedArray, so load and store the element directly.
        // And barrier is not needed.
        DoReverse(glue, thisValue, *receiver, false, false, MemoryAttribute::NoBarrier());
        Jump(&afterReverse);
    }
    Bind(&isHoleOrIntOrNumber);
    {
        // The old array is mutant, but new array is TaggedArray, so load the value from old array with
        // elements kind. And set it to new array directly, And barrier is not needed.
        DoReverse(glue, thisValue, *receiver, true, true, MemoryAttribute::NoBarrier());
        Jump(&afterReverse);
    }
    Bind(&afterReverse);
    result->WriteVariable(*receiver);
    Jump(exit);
}

void BuiltinsArrayStubBuilder::DoReverse(GateRef glue, GateRef fromArray, GateRef toArray, bool holeToUndefined,
                                         bool getWithKind, MemoryAttribute mAttr)
{
    auto env = GetEnvironment();
    Label entry(env);
    env->SubCfgEntry(&entry);
    Label loopExit(env);
    Label begin(env);
    Label body(env);
    Label endLoop(env);

    GateRef fromElements = GetElementsArray(fromArray);
    GateRef toElements = GetElementsArray(toArray);
    GateRef thisArrLen = GetArrayLength(fromArray);
    DEFVARIABLE(index, VariableType::INT32(), Int32(0));
    GateRef endIndex = Int32Sub(thisArrLen, Int32(1));
    Jump(&begin);
    LoopBegin(&begin);
    {
        BRANCH_LIKELY(Int32UnsignedLessThan(*index, thisArrLen), &body, &loopExit);
        Bind(&body);
        {
            GateRef toIndex = Int32Sub(endIndex, *index);
            // The old array and new array are both TaggedArray, so load and store the element directly.
            // And barrier is needed.
            GateRef value = getWithKind
                                ? GetTaggedValueWithElementsKind(fromArray, *index)
                                : GetValueFromTaggedArray(fromElements, *index);
            if (holeToUndefined) {
                Label isHole(env);
                Label isNotHole(env);
                BRANCH_UNLIKELY(TaggedIsHole(value), &isHole, &isNotHole);
                Bind(&isHole);
                {
                    // The return value of toReversed() is never sparse.
                    // Empty slots become undefined in the returned array.
                    SetValueToTaggedArray(VariableType::JS_ANY(), glue, toElements, toIndex, Undefined(),
                                          MemoryAttribute::NoBarrier());
                    Jump(&endLoop);
                }
                Bind(&isNotHole);
            }
            SetValueToTaggedArray(VariableType::JS_ANY(), glue, toElements, toIndex, value, mAttr);
            Jump(&endLoop);
        }
    }
    Bind(&endLoop);
    index = Int32Add(*index, Int32(1));
    LoopEnd(&begin);
    Bind(&loopExit);
    env->SubCfgExit();
}


// new an empty array, the length is zero, but with specific hclass,
GateRef BuiltinsArrayStubBuilder::NewEmptyArrayWithHClass(GateRef glue, GateRef hclass)
{
#if ECMASCRIPT_ENABLE_ELEMENTSKIND_ALWAY_GENERIC
    hclass = GetGlobalConstantValue(VariableType::JS_ANY(), glue, ConstantIndex::ELEMENT_HOLE_TAGGED_HCLASS_INDEX);
#endif
    // New an array with zero length.
    auto env = GetEnvironment();
    Label entry(env);
    env->SubCfgEntry(&entry);
    DEFVARIABLE(result, VariableType::JS_POINTER(), Undefined());
    Label exit(env);
    Label setProperties(env);
    NewObjectStubBuilder newBuilder(this);
    newBuilder.SetParameters(glue, Int32(0));
    result = newBuilder.NewEmptyJSArrayWithHClass(hclass);
    BRANCH(TaggedIsException(*result), &exit, &setProperties);
    Bind(&setProperties);
    {
        InitializeArray(glue, Int32(0), &result, hclass);
        Jump(&exit);
    }
    Bind(&exit);
    auto res = *result;
    env->SubCfgExit();
    return res;
}

void BuiltinsArrayStubBuilder::FastToSpliced(GateRef glue, GateRef thisValue, GateRef newArray, GateRef actualStart,
                                             GateRef actualDeleteCount, GateRef insertCount, GateRef insertValue)
{
    auto env = GetEnvironment();
    Label entry(env);
    env->SubCfgEntry(&entry);
    Label copyBefore(env);
    Label copyAfter(env);
    Label insertArg(env);
    Label exit(env);
    GateRef srcElements = GetElementsArray(thisValue);
    GateRef dstElements = GetElementsArray(newArray);
    GateRef thisLength = GetLengthOfJSArray(thisValue);
    BRANCH(Int32GreaterThan(actualStart, Int32(0)), &copyBefore, &insertArg);
    Bind(&copyBefore);
    {
        GateRef srcStart = GetDataPtrInTaggedArray(srcElements);
        GateRef dstStart = GetDataPtrInTaggedArray(dstElements);
        ArrayCopyAndHoleToUndefined(glue, srcStart, dstElements, dstStart, actualStart);
        Jump(&insertArg);
    }
    Bind(&insertArg);
    {
        Label insert(env);
        BRANCH(Int32GreaterThan(insertCount, Int32(0)), &insert, &copyAfter);
        Bind(&insert);
        {
            SetValueToTaggedArray(VariableType::JS_ANY(), glue, dstElements, actualStart, insertValue);
            Jump(&copyAfter);
        }
    }
    Bind(&copyAfter);
    {
        Label canCopyAfter(env);
        Label setLength(env);
        GateRef oldIndex = Int32Add(actualStart, actualDeleteCount);
        GateRef newIndex = Int32Add(actualStart, insertCount);
        BRANCH(Int32LessThan(oldIndex, thisLength), &canCopyAfter, &setLength);
        Bind(&canCopyAfter);
        {
            GateRef srcStart = GetDataPtrInTaggedArray(srcElements, oldIndex);
            GateRef dstStart = GetDataPtrInTaggedArray(dstElements, newIndex);
            GateRef afterLength = Int32Sub(thisLength, oldIndex);
            ArrayCopyAndHoleToUndefined(glue, srcStart, dstElements, dstStart, afterLength);
            newIndex = Int32Add(newIndex, afterLength);
            Jump(&setLength);
        }
        Bind(&setLength);
        {
            SetArrayLength(glue, newArray, newIndex);
            Jump(&exit);
        }
    }
    Bind(&exit);
    env->SubCfgExit();
}

void BuiltinsArrayStubBuilder::ToSplicedOptimised(GateRef glue, GateRef thisValue, GateRef numArgs,
                                         Variable *result, Label *exit, Label *slowPath)
{
    auto env = GetEnvironment();
    Label isHeapObject(env);
    Label isJsArray(env);
    Label isStability(env);
    Label defaultConstr(env);
    Label isGeneric(env);
    BRANCH(TaggedIsHeapObject(thisValue), &isHeapObject, slowPath);
    Bind(&isHeapObject);
    BRANCH(IsJsArray(thisValue), &isJsArray, slowPath);
    Bind(&isJsArray);
    BRANCH(HasConstructor(thisValue), slowPath, &defaultConstr);
    Bind(&defaultConstr);
    BRANCH(IsStableJSArray(glue, thisValue), &isStability, slowPath);
    Bind(&isStability);
    Label notCOWArray(env);
    BRANCH(IsJsCOWArray(thisValue), slowPath, &notCOWArray);
    Bind(&notCOWArray);
    GateRef thisLen = GetArrayLength(thisValue);
    Label lessThreeArg(env);
    DEFVARIABLE(actualStart, VariableType::INT32(), Int32(0));
    DEFVARIABLE(actualDeleteCount, VariableType::INT32(), Int32(0));
    DEFVARIABLE(newLen, VariableType::INT32(), Int32(0));
    DEFVARIABLE(insertCount, VariableType::INT32(), Int32(0));
    GateRef argc = ChangeIntPtrToInt32(numArgs);
    // 3: max arg count
    BRANCH(Int32LessThanOrEqual(argc, Int32(3)), &lessThreeArg, slowPath);
    Bind(&lessThreeArg);
    {
        Label checkOverFlow(env);
        Label greaterZero(env);
        Label greaterOne(env);
        Label checkGreaterOne(env);
        Label notOverFlow(env);
        Label copyAfter(env);
        // 0: judge the first arg exists
        BRANCH(Int32GreaterThan(argc, Int32(0)), &greaterZero, &checkGreaterOne);
        Bind(&greaterZero);
        {
            GateRef taggedStart = GetCallArg0(numArgs);
            Label taggedStartInt(env);
            BRANCH(TaggedIsInt(taggedStart), &taggedStartInt, slowPath);
            Bind(&taggedStartInt);
            {
                GateRef intStart = GetInt32OfTInt(taggedStart);
                actualStart = CalArrayRelativePos(intStart, thisLen);
                actualDeleteCount = Int32Sub(thisLen, *actualStart);
                Jump(&checkGreaterOne);
            }
        }
        Bind(&checkGreaterOne);
        {
            // 1: judge the second arg exists
            BRANCH(Int32GreaterThan(argc, Int32(1)), &greaterOne, &checkOverFlow);
            Bind(&greaterOne);
            {
                // 2: arg count which is not an item
                insertCount = Int32Sub(argc, Int32(2));
                GateRef argDeleteCount = GetCallArg1(numArgs);
                Label argDeleteCountInt(env);
                BRANCH(TaggedIsInt(argDeleteCount), &argDeleteCountInt, slowPath);
                Bind(&argDeleteCountInt);
                {
                    DEFVARIABLE(deleteCount, VariableType::INT32(), TaggedGetInt(argDeleteCount));
                    Label deleteCountLessZero(env);
                    Label calActualDeleteCount(env);
                    BRANCH(Int32LessThan(*deleteCount, Int32(0)), &deleteCountLessZero, &calActualDeleteCount);
                    Bind(&deleteCountLessZero);
                    {
                        deleteCount = Int32(0);
                        Jump(&calActualDeleteCount);
                    }
                    Bind(&calActualDeleteCount);
                    {
                        actualDeleteCount = *deleteCount;
                        Label lessArrayLen(env);
                        BRANCH(Int32LessThan(Int32Sub(thisLen, *actualStart), *deleteCount),
                               &lessArrayLen, &checkOverFlow);
                        Bind(&lessArrayLen);
                        {
                            actualDeleteCount = Int32Sub(thisLen, *actualStart);
                            Jump(&checkOverFlow);
                        }
                    }
                }
            }
            Bind(&checkOverFlow);
            {
                newLen = Int32Add(Int32Sub(thisLen, *actualDeleteCount), *insertCount);
                BRANCH(Int64GreaterThan(ZExtInt32ToInt64(*newLen), Int64(base::MAX_SAFE_INTEGER)),
                       slowPath, &notOverFlow);
                Bind(&notOverFlow);
                Label newLenEmpty(env);
                Label newLenNotEmpty(env);
                BRANCH(Int32Equal(*newLen, Int32(0)), &newLenEmpty, &newLenNotEmpty);
                Bind(&newLenEmpty);
                {
                    NewObjectStubBuilder newBuilder(this);
                    result->WriteVariable(newBuilder.CreateEmptyArray(glue));
                    Jump(exit);
                }
                Bind(&newLenNotEmpty);
                {
                    Label copyBefore(env);
                    Label insertArg(env);
                    GateRef newArray = NewArray(glue, Int32(0));
                    GrowElementsCapacity(glue, newArray, *newLen);
                    Label elementsKindToSpliced(env);
                    Label fastToSpliced(env);
                    BRANCH_UNLIKELY(IsEnableElementsKind(glue), &elementsKindToSpliced, &fastToSpliced);
                    Bind(&fastToSpliced);
                    {
                        FastToSpliced(glue, thisValue, newArray, *actualStart, *actualDeleteCount, *insertCount,
                                      GetCallArg2(numArgs));
                        result->WriteVariable(newArray);
                        Jump(exit);
                    }
                    Bind(&elementsKindToSpliced);
                    DEFVARIABLE(oldIndex, VariableType::INT32(), Int32(0));
                    DEFVARIABLE(newIndex, VariableType::INT32(), Int32(0));
                    BRANCH(Int32GreaterThan(*actualStart, Int32(0)), &copyBefore, &insertArg);
                    Bind(&copyBefore);
                    {
                        Label loopHead(env);
                        Label loopEnd(env);
                        Label loopNext(env);
                        Label loopExit(env);
                        Label eleIsHole(env);
                        Label eleNotHole(env);
                        Jump(&loopHead);
                        LoopBegin(&loopHead);
                        {
                            BRANCH(Int32LessThan(*oldIndex, *actualStart), &loopNext, &loopExit);
                            Bind(&loopNext);
                            GateRef ele = GetTaggedValueWithElementsKind(thisValue, *oldIndex);
                            BRANCH(TaggedIsHole(ele), &eleIsHole, &eleNotHole);
                            Bind(&eleIsHole);
                            {
                                SetValueWithElementsKind(glue, newArray, Undefined(), *newIndex, Boolean(true),
                                                         Int32(static_cast<uint32_t>(ElementsKind::NONE)));
                                Jump(&loopEnd);
                            }
                            Bind(&eleNotHole);
                            {
                                SetValueWithElementsKind(glue, newArray, ele, *newIndex, Boolean(true),
                                                         Int32(static_cast<uint32_t>(ElementsKind::NONE)));
                                Jump(&loopEnd);
                            }
                        }
                        Bind(&loopEnd);
                        oldIndex = Int32Add(*oldIndex, Int32(1));
                        newIndex = Int32Add(*newIndex, Int32(1));
                        LoopEnd(&loopHead);
                        Bind(&loopExit);
                        Jump(&insertArg);
                    }
                    Bind(&insertArg);
                    {
                        Label insert(env);
                        BRANCH(Int32GreaterThan(*insertCount, Int32(0)), &insert, &copyAfter);
                        Bind(&insert);
                        {
                            GateRef insertNum = GetCallArg2(numArgs);
                            SetValueWithElementsKind(glue, newArray, insertNum, *newIndex, Boolean(true),
                                                     Int32(static_cast<uint32_t>(ElementsKind::NONE)));
                            newIndex = Int32Add(*newIndex, Int32(1));
                            Jump(&copyAfter);
                        }
                    }
                    Bind(&copyAfter);
                    {
                        Label canCopyAfter(env);
                        Label setLength(env);
                        oldIndex = Int32Add(*actualStart, *actualDeleteCount);
                        BRANCH(Int32LessThan(*oldIndex, thisLen), &canCopyAfter, &setLength);
                        Bind(&canCopyAfter);
                        {
                            Label loopHead1(env);
                            Label loopNext1(env);
                            Label loopEnd1(env);
                            Label loopExit1(env);
                            Label ele1IsHole(env);
                            Label ele1NotHole(env);
                            Jump(&loopHead1);
                            LoopBegin(&loopHead1);
                            {
                                BRANCH(Int32LessThan(*oldIndex, thisLen), &loopNext1, &loopExit1);
                                Bind(&loopNext1);
                                GateRef ele1 = GetTaggedValueWithElementsKind(thisValue, *oldIndex);
                                BRANCH(TaggedIsHole(ele1), &ele1IsHole, &ele1NotHole);
                                Bind(&ele1IsHole);
                                {
                                    SetValueWithElementsKind(glue, newArray, Undefined(), *newIndex, Boolean(true),
                                                             Int32(static_cast<uint32_t>(ElementsKind::NONE)));
                                    Jump(&loopEnd1);
                                }
                                Bind(&ele1NotHole);
                                {
                                    SetValueWithElementsKind(glue, newArray, ele1, *newIndex, Boolean(true),
                                                             Int32(static_cast<uint32_t>(ElementsKind::NONE)));
                                    Jump(&loopEnd1);
                                }
                            }
                            Bind(&loopEnd1);
                            oldIndex = Int32Add(*oldIndex, Int32(1));
                            newIndex = Int32Add(*newIndex, Int32(1));
                            LoopEnd(&loopHead1);
                            Bind(&loopExit1);
                            Jump(&setLength);
                        }
                        Bind(&setLength);
                        {
                            SetArrayLength(glue, newArray, *newLen);
                            result->WriteVariable(newArray);
                            Jump(exit);
                        }
                    }
                }
            }
        }
    }
}

void BuiltinsArrayStubBuilder::SliceOptimised(GateRef glue, GateRef thisValue, GateRef numArgs,
    Variable *result, Label *exit, Label *slowPath)
{
    auto env = GetEnvironment();
    Label isHeapObject(env);
    Label isJsArray(env);
    Label noConstructor(env);
    BRANCH(TaggedIsHeapObject(thisValue), &isHeapObject, slowPath);
    Bind(&isHeapObject);
    BRANCH(IsJsArray(thisValue), &isJsArray, slowPath);
    Bind(&isJsArray);
    BRANCH(HasConstructor(thisValue), slowPath, &noConstructor);
    Bind(&noConstructor);

    Label thisIsEmpty(env);
    Label thisNotEmpty(env);
    // Fast path if:
    // (1) this is an empty array with constructor not reset (see ArraySpeciesCreate for details);
    // (2) no arguments exist
    JsArrayRequirements req;
    req.defaultConstructor = true;
    BRANCH(IsJsArrayWithLengthLimit(glue, thisValue, MAX_LENGTH_ZERO, req), &thisIsEmpty, &thisNotEmpty);
    Bind(&thisIsEmpty);
    {
        Label noArgs(env);
        GateRef numArgsAsInt32 = TruncPtrToInt32(numArgs);
        BRANCH(Int32Equal(numArgsAsInt32, Int32(0)), &noArgs, slowPath);
        // Creates a new empty array on fast path
        Bind(&noArgs);
        NewObjectStubBuilder newBuilder(this);
        result->WriteVariable(newBuilder.CreateEmptyArray(glue));
        Jump(exit);
    }
    Bind(&thisNotEmpty);
    {
        Label stableJSArray(env);
        Label arrayLenNotZero(env);

        GateRef isThisStableJSArray = IsStableJSArray(glue, thisValue);
        BRANCH(isThisStableJSArray, &stableJSArray, slowPath);
        Bind(&stableJSArray);

        GateRef msg0 = GetCallArg0(numArgs);
        GateRef msg1 = GetCallArg1(numArgs);
        GateRef thisArrLen = ZExtInt32ToInt64(GetArrayLength(thisValue));
        Label msg0Int(env);
        BRANCH(TaggedIsInt(msg0), &msg0Int, slowPath);
        Bind(&msg0Int);
        DEFVARIABLE(start, VariableType::INT64(), Int64(0));
        DEFVARIABLE(end, VariableType::INT64(), thisArrLen);

        GateRef argStart = SExtInt32ToInt64(TaggedGetInt(msg0));
        Label arg0LessZero(env);
        Label arg0NotLessZero(env);
        Label startDone(env);
        BRANCH(Int64LessThan(argStart, Int64(0)), &arg0LessZero, &arg0NotLessZero);
        Bind(&arg0LessZero);
        {
            Label tempGreaterZero(env);
            Label tempNotGreaterZero(env);
            GateRef tempStart = Int64Add(argStart, thisArrLen);
            BRANCH(Int64GreaterThan(tempStart, Int64(0)), &tempGreaterZero, &tempNotGreaterZero);
            Bind(&tempGreaterZero);
            {
                start = tempStart;
                Jump(&startDone);
            }
            Bind(&tempNotGreaterZero);
            {
                Jump(&startDone);
            }
        }
        Bind(&arg0NotLessZero);
        {
            Label argLessLen(env);
            Label argNotLessLen(env);
            BRANCH(Int64LessThan(argStart, thisArrLen), &argLessLen, &argNotLessLen);
            Bind(&argLessLen);
            {
                start = argStart;
                Jump(&startDone);
            }
            Bind(&argNotLessLen);
            {
                start = thisArrLen;
                Jump(&startDone);
            }
        }
        Bind(&startDone);
        {
            Label endDone(env);
            Label msg1Def(env);
            BRANCH(TaggedIsUndefined(msg1), &endDone, &msg1Def);
            Bind(&msg1Def);
            {
                Label msg1Int(env);
                BRANCH(TaggedIsInt(msg1), &msg1Int, slowPath);
                Bind(&msg1Int);
                {
                    GateRef argEnd = SExtInt32ToInt64(TaggedGetInt(msg1));
                    Label arg1LessZero(env);
                    Label arg1NotLessZero(env);
                    BRANCH(Int64LessThan(argEnd, Int64(0)), &arg1LessZero, &arg1NotLessZero);
                    Bind(&arg1LessZero);
                    {
                        Label tempGreaterZero(env);
                        Label tempNotGreaterZero(env);
                        GateRef tempEnd = Int64Add(argEnd, thisArrLen);
                        BRANCH(Int64GreaterThan(tempEnd, Int64(0)), &tempGreaterZero, &tempNotGreaterZero);
                        Bind(&tempGreaterZero);
                        {
                            end = tempEnd;
                            Jump(&endDone);
                        }
                        Bind(&tempNotGreaterZero);
                        {
                            end = Int64(0);
                            Jump(&endDone);
                        }
                    }
                    Bind(&arg1NotLessZero);
                    {
                        Label argLessLen(env);
                        Label argNotLessLen(env);
                        BRANCH(Int64LessThan(argEnd, thisArrLen), &argLessLen, &argNotLessLen);
                        Bind(&argLessLen);
                        {
                            end = argEnd;
                            Jump(&endDone);
                        }
                        Bind(&argNotLessLen);
                        {
                            end = thisArrLen;
                            Jump(&endDone);
                        }
                    }
                }
            }
            Bind(&endDone);
            {
                DEFVARIABLE(count, VariableType::INT64(), Int64(0));
                GateRef tempCnt = Int64Sub(*end, *start);
                Label tempCntGreaterOrEqualZero(env);
                Label tempCntDone(env);
                BRANCH(Int64LessThan(tempCnt, Int64(0)), &tempCntDone, &tempCntGreaterOrEqualZero);
                Bind(&tempCntGreaterOrEqualZero);
                {
                    count = tempCnt;
                    Jump(&tempCntDone);
                }
                Bind(&tempCntDone);
                {
                    Label notOverFlow(env);
                    BRANCH(Int64GreaterThan(*count, Int64(JSObject::MAX_GAP)), slowPath, &notOverFlow);
                    Bind(&notOverFlow);
                    {
                        Label needBarrier(env);
                        Label noBarrier(env);
                        Label elementsKindEnabled(env);
                        Label notElementsKindEnabled(env);

                        Label jumpCopyExit(env);
                        GateRef isElementsKindEnabled = IsEnableElementsKind(glue);
                        BRANCH_NO_WEIGHT(isElementsKindEnabled, &elementsKindEnabled, &notElementsKindEnabled);
                        Bind(&elementsKindEnabled);
                        {
                            GateRef newArray = NewArray(glue, *count);
                            DEFVARIABLE(idx, VariableType::INT64(), Int64(0));
                            Label loopHead(env);
                            Label loopEnd(env);
                            Label next(env);
                            Label loopExit(env);
                            Jump(&loopHead);
                            LoopBegin(&loopHead);
                            {
                                BRANCH(Int64LessThan(*idx, *count), &next, &loopExit);
                                Bind(&next);
                                GateRef ele = GetTaggedValueWithElementsKind(thisValue, Int64Add(*idx, *start));
                                SetValueWithElementsKind(glue, newArray, ele, *idx, Boolean(true),
                                                         Int32(static_cast<uint32_t>(ElementsKind::NONE)));
                                Jump(&loopEnd);
                            }
                            Bind(&loopEnd);
                            idx = Int64Add(*idx, Int64(1));
                            LoopEnd(&loopHead, env, glue);
                            Bind(&loopExit);
                            result->WriteVariable(newArray);
                            Jump(exit);
                        }
                        Bind(&notElementsKindEnabled);
                        {
                            DEFVARIABLE(computeKind, VariableType::INT32(), Int32(0));
                            Label isEnableForArray(env);
                            Label notEnableForArray(env);
                            GateRef elementsKindForArray = True();
                            BRANCH(elementsKindForArray, &isEnableForArray, &notEnableForArray);
                            Bind(&isEnableForArray);
                            {
                                computeKind = ComputeTaggedArrayElementKind(thisValue, *start, *end);
                                Jump(&notEnableForArray);
                            }
                            Bind(&notEnableForArray);
                            GateRef kind = *computeKind;
                            GateRef elements = GetElementsArray(thisValue);
                            NewObjectStubBuilder newBuilder(this);
                            newBuilder.SetGlue(glue);
                            GateRef destElements = newBuilder.NewTaggedArray(glue, TruncInt64ToInt32(*count));
                            GateRef sourceStart = GetDataPtrInTaggedArray(elements, *start);
                            GateRef dest = GetDataPtrInTaggedArray(destElements);
                            GateRef barrier = NeedBarrier(kind);
                            BRANCH_NO_WEIGHT(barrier, &needBarrier, &noBarrier);
                            Bind(&needBarrier);
                            {
                                ArrayCopy<NotOverlap>(glue, sourceStart, destElements, dest,
                                    TruncInt64ToInt32(*count), true);
                                Jump(&jumpCopyExit);
                            }
                            Bind(&noBarrier);
                            {
                                ArrayCopy<NotOverlap>(glue, sourceStart, destElements, dest,
                                    TruncInt64ToInt32(*count), false);
                                Jump(&jumpCopyExit);
                            }
                            Bind(&jumpCopyExit);
                            GateRef array = newBuilder.CreateArrayFromList(glue, destElements, kind);
                            result->WriteVariable(array);
                            Jump(exit);
                        }
                    }
                }
            }
        }
    }
}

void BuiltinsArrayStubBuilder::ConcatOptimised(GateRef glue, GateRef thisValue, GateRef numArgs,
                                               Variable *result, Label *exit, Label *slowPath)
{
    auto env = GetEnvironment();
    Label isHeapObject(env);
    Label isJsArray(env);
    BRANCH(TaggedIsHeapObject(thisValue), &isHeapObject, slowPath);
    Bind(&isHeapObject);
    BRANCH(IsJsArray(thisValue), &isJsArray, slowPath);
    Bind(&isJsArray);
    {
        Label isExtensible(env);
        BRANCH(HasConstructor(thisValue), slowPath, &isExtensible);
        Bind(&isExtensible);
        {
            Label numArgsOne(env);
            BRANCH(Int64Equal(numArgs, IntPtr(1)), &numArgsOne, slowPath);
            Bind(&numArgsOne);
            {
                GateRef arg0 = GetCallArg0(numArgs);
                Label allStableJsArray(env);
                GateRef isAllStableJsArray = LogicAndBuilder(env).And(IsStableJSArray(glue, thisValue))
                    .And(IsStableJSArray(glue, arg0)).Done();
                BRANCH(isAllStableJsArray, &allStableJsArray, slowPath);
                Bind(&allStableJsArray);
                {
                    GateRef maxArrayIndex = Int64(TaggedArray::MAX_ARRAY_INDEX);
                    GateRef thisLen = ZExtInt32ToInt64(GetArrayLength(thisValue));
                    GateRef argLen = ZExtInt32ToInt64(GetArrayLength(arg0));
                    GateRef sumArrayLen = Int64Add(argLen, thisLen);
                    Label isEmptyArray(env);
                    Label notEmptyArray(env);
                    BRANCH(Int64Equal(sumArrayLen, Int64(0)), &isEmptyArray, &notEmptyArray);
                    Bind(&isEmptyArray);
                    {
                        NewObjectStubBuilder newBuilder(this);
                        result->WriteVariable(newBuilder.CreateEmptyArray(glue));
                        Jump(exit);
                    }
                    Bind(&notEmptyArray);
                    Label notOverFlow(env);
                    BRANCH(Int64GreaterThan(sumArrayLen, maxArrayIndex), slowPath, &notOverFlow);
                    Bind(&notOverFlow);
                    {
                        Label spreadable(env);
                        GateRef isAllConcatSpreadable = LogicAndBuilder(env).And(IsConcatSpreadable(glue, thisValue))
                            .And(IsConcatSpreadable(glue, arg0)).Done();
                        BRANCH(isAllConcatSpreadable, &spreadable, slowPath);
                        Bind(&spreadable);
                        {
                            Label enabledElementsKind(env);
                            Label disableElementsKind(env);
                            BRANCH(IsEnableElementsKind(glue), &enabledElementsKind, &disableElementsKind);
                            Bind(&enabledElementsKind);
                            {
                                DoConcat(glue, thisValue, arg0, result, exit, thisLen, argLen, sumArrayLen);
                            }
                            Bind(&disableElementsKind);
                            {
                                GateRef kind1 = GetElementsKindFromHClass(LoadHClass(thisValue));
                                GateRef kind2 = GetElementsKindFromHClass(LoadHClass(arg0));
                                GateRef tmpKind = Int32Or(kind1, kind2);
                                GateRef newKind = FixElementsKind(tmpKind);
                                Label shouldBarrier(env);
                                Label noBarrier(env);
                                Label shouldBarrier1(env);
                                Label noBarrier1(env);
                                Label shouldBarrier2(env);
                                Label noBarrier2(env);
                                Label afterThisCopy(env);
                                Label afterConcat(env);
                                GateRef thisElements = GetElementsArray(thisValue);
                                GateRef argElements = GetElementsArray(arg0);
                                NewObjectStubBuilder newBuilder(this);
                                GateRef newElements = newBuilder.NewTaggedArray(glue, TruncInt64ToInt32(sumArrayLen));
                                GateRef dst1 = GetDataPtrInTaggedArray(newElements);
                                GateRef dst2 = PtrAdd(dst1, PtrMul(thisLen, IntPtr(JSTaggedValue::TaggedTypeSize())));
                                BRANCH(NeedBarrier(newKind), &shouldBarrier, &noBarrier);
                                Bind(&shouldBarrier);
                                {
                                    BRANCH(NeedBarrier(kind1), &shouldBarrier1, &noBarrier1);
                                    Bind(&shouldBarrier1);
                                    {
                                        ArrayCopy<NotOverlap>(glue, GetDataPtrInTaggedArray(thisElements),
                                            newElements, dst1, TruncInt64ToInt32(thisLen), true);
                                        Jump(&afterThisCopy);
                                    }
                                    Bind(&noBarrier1);
                                    {
                                        ArrayCopy<NotOverlap>(glue, GetDataPtrInTaggedArray(thisElements),
                                            newElements, dst1, TruncInt64ToInt32(thisLen), false);
                                        Jump(&afterThisCopy);
                                    }
                                    Bind(&afterThisCopy);
                                    BRANCH(NeedBarrier(kind2), &shouldBarrier2, &noBarrier2);
                                    Bind(&shouldBarrier2);
                                    {
                                        ArrayCopy<NotOverlap>(glue, GetDataPtrInTaggedArray(argElements),
                                            newElements, dst2, TruncInt64ToInt32(argLen), true);
                                        Jump(&afterConcat);
                                    }
                                    Bind(&noBarrier2);
                                    {
                                        ArrayCopy<NotOverlap>(glue, GetDataPtrInTaggedArray(argElements),
                                            newElements, dst2, TruncInt64ToInt32(argLen), false);
                                        Jump(&afterConcat);
                                    }
                                }
                                Bind(&noBarrier);
                                {
                                    ArrayCopy<NotOverlap>(glue, GetDataPtrInTaggedArray(thisElements), newElements,
                                        dst1, TruncInt64ToInt32(thisLen), false);
                                    ArrayCopy<NotOverlap>(glue, GetDataPtrInTaggedArray(argElements), newElements,
                                        dst2, TruncInt64ToInt32(argLen), false);
                                    Jump(&afterConcat);
                                }
                                Bind(&afterConcat);
                                GateRef array = newBuilder.CreateArrayFromList(glue, newElements, newKind);
                                result->WriteVariable(array);
                                Jump(exit);
                            }
                        }
                    }
                }
            }
        }
    }
}

void BuiltinsArrayStubBuilder::DoConcat(GateRef glue, GateRef thisValue, GateRef arg0, Variable *result, Label *exit,
                                        GateRef thisLen, GateRef argLen, GateRef sumArrayLen)
{
    auto env = GetEnvironment();
    Label setProperties(env);
    GateRef glueGlobalEnvOffset =
        IntPtr(JSThread::GlueData::GetGlueGlobalEnvOffset(env->Is32Bit()));
    GateRef glueGlobalEnv = Load(VariableType::NATIVE_POINTER(), glue, glueGlobalEnvOffset);
    auto arrayFunc = GetGlobalEnvValue(VariableType::JS_ANY(), glueGlobalEnv,
        GlobalEnv::ARRAY_FUNCTION_INDEX);
    GateRef intialHClass = Load(VariableType::JS_ANY(), arrayFunc,
        IntPtr(JSFunction::PROTO_OR_DYNCLASS_OFFSET));
    NewObjectStubBuilder newBuilder(this);
    newBuilder.SetParameters(glue, 0);
    GateRef newArray = newBuilder.NewJSArrayWithSize(intialHClass, sumArrayLen);
    BRANCH(TaggedIsException(newArray), exit, &setProperties);
    Bind(&setProperties);
    {
        GateRef lengthOffset = IntPtr(JSArray::LENGTH_OFFSET);
        Store(VariableType::INT32(), glue, newArray, lengthOffset,
            TruncInt64ToInt32(sumArrayLen));
        GateRef accessor = GetGlobalConstantValue(VariableType::JS_ANY(), glue,
            ConstantIndex::ARRAY_LENGTH_ACCESSOR);
        SetPropertyInlinedProps(glue, newArray, intialHClass, accessor,
            Int32(JSArray::LENGTH_INLINE_PROPERTY_INDEX));
        SetExtensibleToBitfield(glue, newArray, true);
        DEFVARIABLE(i, VariableType::INT64(), Int64(0));
        DEFVARIABLE(j, VariableType::INT64(), Int64(0));
        DEFVARIABLE(k, VariableType::INT64(), Int64(0));
        Label loopHead(env);
        Label loopEnd(env);
        Label next(env);
        Label loopExit(env);
        Jump(&loopHead);
        LoopBegin(&loopHead);
        {
            BRANCH(Int64LessThan(*i, thisLen), &next, &loopExit);
            Bind(&next);
            GateRef ele = GetTaggedValueWithElementsKind(thisValue, *i);
            #if ECMASCRIPT_ENABLE_ELEMENTSKIND_ALWAY_GENERIC
            SetValueWithElementsKind(glue, newArray, ele, *j, Boolean(true),
                Int32(static_cast<uint32_t>(ElementsKind::GENERIC)));
            #else
            SetValueWithElementsKind(glue, newArray, ele, *j, Boolean(true),
                Int32(static_cast<uint32_t>(ElementsKind::NONE)));
            #endif
            Jump(&loopEnd);
        }
        Bind(&loopEnd);
        i = Int64Add(*i, Int64(1));
        j = Int64Add(*j, Int64(1));
        LoopEnd(&loopHead, env, glue);
        Bind(&loopExit);
        Label loopHead1(env);
        Label loopEnd1(env);
        Label next1(env);
        Label loopExit1(env);
        Jump(&loopHead1);
        LoopBegin(&loopHead1);
        {
            BRANCH(Int64LessThan(*k, argLen), &next1, &loopExit1);
            Bind(&next1);
            GateRef ele = GetTaggedValueWithElementsKind(arg0, *k);
            #if ECMASCRIPT_ENABLE_ELEMENTSKIND_ALWAY_GENERIC
            SetValueWithElementsKind(glue, newArray, ele, *j, Boolean(true),
                                     Int32(static_cast<uint32_t>(ElementsKind::GENERIC)));
            #else
            SetValueWithElementsKind(glue, newArray, ele, *j, Boolean(true),
                                     Int32(static_cast<uint32_t>(ElementsKind::NONE)));
            #endif
            Jump(&loopEnd1);
        }
        Bind(&loopEnd1);
        k = Int64Add(*k, Int64(1));
        j = Int64Add(*j, Int64(1));
        LoopEnd(&loopHead1);
        Bind(&loopExit1);
        result->WriteVariable(newArray);
        Jump(exit);
    }
}
} // namespace panda::ecmascript::kungfu
