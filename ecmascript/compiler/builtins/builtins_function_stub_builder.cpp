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

#include "ecmascript/compiler/builtins/builtins_function_stub_builder.h"

#include "ecmascript/compiler/builtins/builtins_object_stub_builder.h"
#include "ecmascript/compiler/new_object_stub_builder.h"
#include "ecmascript/compiler/stub_builder-inl.h"
#include "ecmascript/js_arguments.h"

namespace panda::ecmascript::kungfu {

void BuiltinsFunctionStubBuilder::Apply(GateRef glue, GateRef thisValue,
    GateRef numArgs, Variable* res, Label *exit, Label *slowPath)
{
    auto env = GetEnvironment();
    Label targetIsCallable(env);
    Label targetIsUndefined(env);
    Label targetNotUndefined(env);
    Label isHeapObject(env);
    //1. If IsCallable(func) is false, throw a TypeError exception
    BRANCH(TaggedIsHeapObject(thisValue), &isHeapObject, slowPath);
    Bind(&isHeapObject);
    {
        BRANCH(IsCallable(thisValue), &targetIsCallable, slowPath);
        Bind(&targetIsCallable);
        {
            GateRef thisArg = GetCallArg0(numArgs);
            GateRef arrayObj = GetCallArg1(numArgs);
            // 2. If argArray is null or undefined, then
            BRANCH(TaggedIsUndefined(arrayObj), &targetIsUndefined, &targetNotUndefined);
            Bind(&targetIsUndefined);
            {
                // a. Return Call(func, thisArg).
                res->WriteVariable(JSCallDispatch(glue, thisValue, Int32(0), 0, Circuit::NullGate(),
                    JSCallMode::CALL_GETTER, { thisArg }));
                Jump(exit);
            }
            Bind(&targetNotUndefined);
            {
                // 3. Let argList be CreateListFromArrayLike(argArray).
                GateRef elements = BuildArgumentsListFastElements(glue, arrayObj);
                Label targetIsHole(env);
                Label targetNotHole(env);
                BRANCH(TaggedIsHole(elements), &targetIsHole, &targetNotHole);
                Bind(&targetIsHole);
                {
                    BuiltinsObjectStubBuilder objectStubBuilder(this);
                    GateRef argList = objectStubBuilder.CreateListFromArrayLike(glue, arrayObj);
                    // 4. ReturnIfAbrupt(argList).
                    Label isPendingException(env);
                    Label noPendingException(env);
                    BRANCH(HasPendingException(glue), &isPendingException, &noPendingException);
                    Bind(&isPendingException);
                    {
                        Jump(slowPath);
                    }
                    Bind(&noPendingException);
                    {
                        GateRef argsLength = GetLengthOfTaggedArray(argList);
                        GateRef argv = PtrAdd(argList, IntPtr(TaggedArray::DATA_OFFSET));
                        res->WriteVariable(JSCallDispatch(glue, thisValue, argsLength, 0, Circuit::NullGate(),
                            JSCallMode::CALL_THIS_ARGV_WITH_RETURN, { argsLength, argv, thisArg }));
                        Jump(exit);
                    }
                }
                Bind(&targetNotHole);
                {
                    // 6. Return Call(func, thisArg, argList).
                    Label taggedIsStableJsArg(env);
                    Label taggedNotStableJsArg(env);
                    BRANCH(IsStableJSArguments(glue, arrayObj), &taggedIsStableJsArg, &taggedNotStableJsArg);
                    Bind(&taggedIsStableJsArg);
                    {
                        GateRef hClass = LoadHClass(arrayObj);
                        GateRef PropertyInlinedPropsOffset = IntPtr(JSArguments::LENGTH_INLINE_PROPERTY_INDEX);
                        GateRef result = GetPropertyInlinedProps(arrayObj, hClass, PropertyInlinedPropsOffset);
                        GateRef length = TaggedGetInt(result);
                        GateRef argsLength = MakeArgListWithHole(glue, elements, length);
                        GateRef elementArgv = PtrAdd(elements, IntPtr(TaggedArray::DATA_OFFSET));
                        res->WriteVariable(JSCallDispatch(glue, thisValue, argsLength, 0, Circuit::NullGate(),
                            JSCallMode::CALL_THIS_ARGV_WITH_RETURN, { argsLength, elementArgv, thisArg }));
                        Jump(exit);
                    }
                    Bind(&taggedNotStableJsArg);
                    {
                        GateRef length = GetArrayLength(arrayObj);
                        GateRef argsLength = MakeArgListWithHole(glue, elements, length);
                        GateRef elementArgv = PtrAdd(elements, IntPtr(TaggedArray::DATA_OFFSET));
                        res->WriteVariable(JSCallDispatch(glue, thisValue, argsLength, 0, Circuit::NullGate(),
                            JSCallMode::CALL_THIS_ARGV_WITH_RETURN, { argsLength, elementArgv, thisArg }));
                        Jump(exit);
                    }
                }
            }
        }
    }
}

// return elements
GateRef BuiltinsFunctionStubBuilder::BuildArgumentsListFastElements(GateRef glue, GateRef arrayObj)
{
    auto env = GetEnvironment();
    Label subentry(env);
    env->SubCfgEntry(&subentry);
    DEFVARIABLE(res, VariableType::JS_ANY(), Hole());
    Label exit(env);
    Label hasStableElements(env);
    Label targetIsStableJSArguments(env);
    Label targetNotStableJSArguments(env);
    Label targetIsInt(env);
    Label hClassEqual(env);
    Label targetIsStableJSArray(env);
    Label targetNotStableJSArray(env);

    BRANCH(HasStableElements(glue, arrayObj), &hasStableElements, &exit);
    Bind(&hasStableElements);
    {
        BRANCH(IsStableJSArguments(glue, arrayObj), &targetIsStableJSArguments, &targetNotStableJSArguments);
        Bind(&targetIsStableJSArguments);
        {
            GateRef hClass = LoadHClass(arrayObj);
            GateRef glueGlobalEnvOffset = IntPtr(JSThread::GlueData::GetGlueGlobalEnvOffset(env->Is32Bit()));
            GateRef glueGlobalEnv = Load(VariableType::NATIVE_POINTER(), glue, glueGlobalEnvOffset);
            GateRef argmentsClass = GetGlobalEnvValue(VariableType::JS_ANY(), glueGlobalEnv,
                                                      GlobalEnv::ARGUMENTS_CLASS);
            BRANCH(Int64Equal(hClass, argmentsClass), &hClassEqual, &exit);
            Bind(&hClassEqual);
            {
                GateRef PropertyInlinedPropsOffset = IntPtr(JSArguments::LENGTH_INLINE_PROPERTY_INDEX);
                GateRef result = GetPropertyInlinedProps(arrayObj, hClass, PropertyInlinedPropsOffset);
                BRANCH(TaggedIsInt(result), &targetIsInt, &exit);
                Bind(&targetIsInt);
                {
                    res = GetElementsArray(arrayObj);
                    Label isMutantTaggedArray(env);
                    BRANCH(IsMutantTaggedArray(*res), &isMutantTaggedArray, &exit);
                    Bind(&isMutantTaggedArray);
                    {
                        NewObjectStubBuilder newBuilder(this);
                        GateRef elementsLength = GetLengthOfTaggedArray(*res);
                        GateRef newTaggedArgList = newBuilder.NewTaggedArray(glue, elementsLength);
                        DEFVARIABLE(index, VariableType::INT32(), Int32(0));
                        Label loopHead(env);
                        Label loopEnd(env);
                        Label afterLoop(env);
                        Label storeValue(env);
                        Jump(&loopHead);
                        LoopBegin(&loopHead);
                        {
                            BRANCH(Int32UnsignedLessThan(*index, elementsLength), &storeValue, &afterLoop);
                            Bind(&storeValue);
                            {
                                GateRef value = GetTaggedValueWithElementsKind(arrayObj, *index);
                                SetValueToTaggedArray(VariableType::JS_ANY(), glue, newTaggedArgList, *index, value);
                                index = Int32Add(*index, Int32(1));
                                Jump(&loopEnd);
                            }
                        }
                        Bind(&loopEnd);
                        LoopEnd(&loopHead);
                        Bind(&afterLoop);
                        {
                            res = newTaggedArgList;
                            Jump(&exit);
                        }
                    }
                }
            }
        }
        Bind(&targetNotStableJSArguments);
        {
            BRANCH(IsStableJSArray(glue, arrayObj), &targetIsStableJSArray, &targetNotStableJSArray);
            Bind(&targetIsStableJSArray);
            {
                res = GetElementsArray(arrayObj);
                Label isMutantTaggedArray(env);
                BRANCH(IsMutantTaggedArray(*res), &isMutantTaggedArray, &exit);
                Bind(&isMutantTaggedArray);
                {
                    NewObjectStubBuilder newBuilder(this);
                    GateRef elementsLength = GetLengthOfTaggedArray(*res);
                    GateRef newTaggedArgList = newBuilder.NewTaggedArray(glue, elementsLength);
                    DEFVARIABLE(index, VariableType::INT32(), Int32(0));
                    Label loopHead(env);
                    Label loopEnd(env);
                    Label afterLoop(env);
                    Label storeValue(env);
                    Jump(&loopHead);
                    LoopBegin(&loopHead);
                    {
                        BRANCH(Int32UnsignedLessThan(*index, elementsLength), &storeValue, &afterLoop);
                        Bind(&storeValue);
                        {
                            GateRef value = GetTaggedValueWithElementsKind(arrayObj, *index);
                            SetValueToTaggedArray(VariableType::JS_ANY(), glue, newTaggedArgList, *index, value);
                            index = Int32Add(*index, Int32(1));
                            Jump(&loopEnd);
                        }
                    }
                    Bind(&loopEnd);
                    LoopEnd(&loopHead);
                    Bind(&afterLoop);
                    {
                        res = newTaggedArgList;
                        Jump(&exit);
                    }
                }
            }
            Bind(&targetNotStableJSArray);
            {
                FatalPrint(glue, { Int32(GET_MESSAGE_STRING_ID(ThisBranchIsUnreachable)) });
                Jump(&exit);
            }
        }
    }
    Bind(&exit);
    auto ret = *res;
    env->SubCfgExit();
    return ret;
}

GateRef BuiltinsFunctionStubBuilder::MakeArgListWithHole(GateRef glue, GateRef argv, GateRef length)
{
    auto env = GetEnvironment();
    Label subentry(env);
    env->SubCfgEntry(&subentry);
    DEFVARIABLE(res, VariableType::INT32(), length);
    DEFVARIABLE(i, VariableType::INT32(), Int32(0));
    Label exit(env);
    Label greatThanZero(env);
    Label lessThanZero(env);
    BRANCH(Int32GreaterThan(length, Int32(0)), &greatThanZero, &lessThanZero);
    Bind(&lessThanZero);
    {
        res = Int32(0);
        Jump(&exit);
    }
    Bind(&greatThanZero);
    GateRef argsLength = GetLengthOfTaggedArray(argv);
    Label lengthGreaterThanArgsLength(env);
    Label lengthLessThanArgsLength(env);
    BRANCH(Int32GreaterThan(length, argsLength), &lengthGreaterThanArgsLength, &lengthLessThanArgsLength);
    Bind(&lengthGreaterThanArgsLength);
    {
        res = argsLength;
        Jump(&lengthLessThanArgsLength);
    }
    Bind(&lengthLessThanArgsLength);
    {
        Label loopHead(env);
        Label loopEnd(env);
        Label targetIsHole(env);
        Label targetNotHole(env);
        BRANCH(Int32UnsignedLessThan(*i, *res), &loopHead, &exit);
        LoopBegin(&loopHead);
        {
            GateRef value = GetValueFromTaggedArray(argv, *i);
            BRANCH(TaggedIsHole(value), &targetIsHole, &targetNotHole);
            Bind(&targetIsHole);
            {
                SetValueToTaggedArray(VariableType::JS_ANY(), glue, argv, *i, Undefined());
                Jump(&targetNotHole);
            }
            Bind(&targetNotHole);
            i = Int32Add(*i, Int32(1));
            BRANCH(Int32UnsignedLessThan(*i, *res), &loopEnd, &exit);
        }
        Bind(&loopEnd);
        LoopEnd(&loopHead);
    }
    Bind(&exit);
    auto ret = *res;
    env->SubCfgExit();
    return ret;
}
}  // namespace panda::ecmascript::kungfu
