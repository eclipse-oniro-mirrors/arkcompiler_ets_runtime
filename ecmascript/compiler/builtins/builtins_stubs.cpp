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

#include "ecmascript/compiler/builtins/builtins_stubs.h"

#include "ecmascript/base/number_helper.h"
#include "ecmascript/compiler/builtins/builtins_array_stub_builder.h"
#include "ecmascript/compiler/builtins/builtins_call_signature.h"
#include "ecmascript/compiler/builtins/builtins_function_stub_builder.h"
#include "ecmascript/compiler/builtins/builtins_string_stub_builder.h"
#include "ecmascript/compiler/builtins/builtins_number_stub_builder.h"
#include "ecmascript/compiler/builtins/builtins_typedarray_stub_builder.h"
#include "ecmascript/compiler/builtins/containers_vector_stub_builder.h"
#include "ecmascript/compiler/builtins/containers_stub_builder.h"
#include "ecmascript/compiler/builtins/builtins_collection_stub_builder.h"
#include "ecmascript/compiler/builtins/builtins_object_stub_builder.h"
#include "ecmascript/compiler/codegen/llvm/llvm_ir_builder.h"
#include "ecmascript/compiler/interpreter_stub-inl.h"
#include "ecmascript/compiler/new_object_stub_builder.h"
#include "ecmascript/compiler/stub_builder-inl.h"
#include "ecmascript/compiler/stub_builder.h"
#include "ecmascript/compiler/variable_type.h"
#include "ecmascript/js_date.h"
#include "ecmascript/js_primitive_ref.h"
#include "ecmascript/linked_hash_table.h"
#include "ecmascript/js_set.h"
#include "ecmascript/js_map.h"

namespace panda::ecmascript::kungfu {
#if ECMASCRIPT_ENABLE_BUILTIN_LOG
#define DECLARE_BUILTINS(name)                                                                      \
void name##StubBuilder::GenerateCircuit()                                                           \
{                                                                                                   \
    GateRef glue = PtrArgument(static_cast<size_t>(BuiltinsArgs::GLUE));                            \
    GateRef nativeCode = PtrArgument(static_cast<size_t>(BuiltinsArgs::NATIVECODE));                \
    GateRef func = TaggedArgument(static_cast<size_t>(BuiltinsArgs::FUNC));                         \
    GateRef newTarget = TaggedArgument(static_cast<size_t>(BuiltinsArgs::NEWTARGET));               \
    GateRef thisValue = TaggedArgument(static_cast<size_t>(BuiltinsArgs::THISVALUE));               \
    GateRef numArgs = PtrArgument(static_cast<size_t>(BuiltinsArgs::NUMARGS));                      \
    DebugPrint(glue, { Int32(GET_MESSAGE_STRING_ID(name)) });                                       \
    GenerateCircuitImpl(glue, nativeCode, func, newTarget, thisValue, numArgs);                     \
}                                                                                                   \
void name##StubBuilder::GenerateCircuitImpl(GateRef glue, GateRef nativeCode, GateRef func,         \
                                            GateRef newTarget, GateRef thisValue, GateRef numArgs)
#else
#define DECLARE_BUILTINS(name)                                                                      \
void name##StubBuilder::GenerateCircuit()                                                           \
{                                                                                                   \
    GateRef glue = PtrArgument(static_cast<size_t>(BuiltinsArgs::GLUE));                            \
    GateRef nativeCode = PtrArgument(static_cast<size_t>(BuiltinsArgs::NATIVECODE));                \
    GateRef func = TaggedArgument(static_cast<size_t>(BuiltinsArgs::FUNC));                         \
    GateRef newTarget = TaggedArgument(static_cast<size_t>(BuiltinsArgs::NEWTARGET));               \
    GateRef thisValue = TaggedArgument(static_cast<size_t>(BuiltinsArgs::THISVALUE));               \
    GateRef numArgs = PtrArgument(static_cast<size_t>(BuiltinsArgs::NUMARGS));                      \
    GenerateCircuitImpl(glue, nativeCode, func, newTarget, thisValue, numArgs);                     \
}                                                                                                   \
void name##StubBuilder::GenerateCircuitImpl(GateRef glue, GateRef nativeCode, GateRef func,         \
                                            GateRef newTarget, GateRef thisValue, GateRef numArgs)
#endif

GateRef BuiltinsStubBuilder::GetArg(GateRef numArgs, GateRef index)
{
    auto env = GetEnvironment();
    Label entry(env);
    env->SubCfgEntry(&entry);
    DEFVARIABLE(arg, VariableType::JS_ANY(), Undefined());
    Label validIndex(env);
    Label exit(env);
    BRANCH(IntPtrGreaterThan(numArgs, index), &validIndex, &exit);
    Bind(&validIndex);
    {
        GateRef argv = GetArgv();
        arg = Load(VariableType::JS_ANY(), argv, PtrMul(index, IntPtr(JSTaggedValue::TaggedTypeSize())));
        Jump(&exit);
    }
    Bind(&exit);
    GateRef ret = *arg;
    env->SubCfgExit();
    return ret;
}

GateRef BuiltinsStubBuilder::CallSlowPath(GateRef nativeCode, GateRef glue, GateRef thisValue,
                                          GateRef numArgs, GateRef func, GateRef newTarget, const char* comment)
{
    auto env = GetEnvironment();
    Label entry(env);
    env->SubCfgEntry(&entry);
    Label exit(env);
    Label callThis0(env);
    Label notcallThis0(env);
    Label notcallThis1(env);
    Label callThis1(env);
    Label callThis2(env);
    Label callThis3(env);
    DEFVARIABLE(result, VariableType::JS_ANY(), Undefined());
    GateRef runtimeCallInfoArgs = PtrAdd(numArgs, IntPtr(NUM_MANDATORY_JSFUNC_ARGS));
    BRANCH(Int64Equal(numArgs, IntPtr(0)), &callThis0, &notcallThis0);
    Bind(&callThis0);
    {
        auto args = { nativeCode, glue, runtimeCallInfoArgs, func, newTarget, thisValue };
        result = CallBuiltinRuntime(glue, args, false, comment);
        Jump(&exit);
    }
    Bind(&notcallThis0);
    {
        BRANCH(Int64Equal(numArgs, IntPtr(1)), &callThis1, &notcallThis1);
        Bind(&callThis1);
        {
            GateRef arg0 = GetCallArg0(numArgs);
            auto args = { nativeCode, glue, runtimeCallInfoArgs, func, newTarget, thisValue, arg0 };
            result = CallBuiltinRuntime(glue, args, false, comment);
            Jump(&exit);
        }
        Bind(&notcallThis1);
        {
            BRANCH(Int64Equal(numArgs, IntPtr(2)), &callThis2, &callThis3); // 2: args2
            Bind(&callThis2);
            {
                GateRef arg0 = GetCallArg0(numArgs);
                GateRef arg1 = GetCallArg1(numArgs);
                auto args = { nativeCode, glue, runtimeCallInfoArgs, func, newTarget, thisValue, arg0, arg1 };
                result = CallBuiltinRuntime(glue, args, false, comment);
                Jump(&exit);
            }
            Bind(&callThis3);
            {
                GateRef arg0 = GetCallArg0(numArgs);
                GateRef arg1 = GetCallArg1(numArgs);
                GateRef arg2 = GetCallArg2(numArgs);
                auto args = { nativeCode, glue, runtimeCallInfoArgs, func, newTarget, thisValue, arg0, arg1, arg2 };
                result = CallBuiltinRuntime(glue, args, false, comment);
                Jump(&exit);
            }
        }
    }

    Bind(&exit);
    auto ret = *result;
    env->SubCfgExit();
    return ret;
}

#define DECLARE_BUILTINS_STUB_BUILDER(method, type, initValue)                                      \
DECLARE_BUILTINS(type##method)                                                                      \
{                                                                                                   \
    auto env = GetEnvironment();                                                                    \
    DEFVARIABLE(res, VariableType::JS_ANY(), initValue);                                            \
    Label exit(env);                                                                                \
    Label slowPath(env);                                                                            \
    Builtins##type##StubBuilder builder(this);                                                      \
    builder.method(glue, thisValue, numArgs, &res, &exit, &slowPath);                               \
    Bind(&slowPath);                                                                                \
    {                                                                                               \
        auto name = BuiltinsStubCSigns::GetName(BUILTINS_STUB_ID(type##method));                    \
        res = CallSlowPath(nativeCode, glue, thisValue, numArgs, func, newTarget, name.c_str());    \
        Jump(&exit);                                                                                \
    }                                                                                               \
    Bind(&exit);                                                                                    \
    Return(*res);                                                                                   \
}

#define DECLARE_BUILTINS_STUB_BUILDER1(method, type, initValue)                                     \
DECLARE_BUILTINS(type##method)                                                                      \
{                                                                                                   \
    auto env = GetEnvironment();                                                                    \
    DEFVARIABLE(res, VariableType::JS_ANY(), initValue);                                            \
    Label thisCollectionObj(env);                                                                   \
    Label slowPath(env);                                                                            \
    Label exit(env);                                                                                \
    Builtins##type##StubBuilder builder(this, glue, thisValue, numArgs);                            \
    builder.method(&res, &exit, &slowPath);                                                         \
    Bind(&slowPath);                                                                                \
    {                                                                                               \
        auto name = BuiltinsStubCSigns::GetName(BUILTINS_STUB_ID(type##method));                    \
        res = CallSlowPath(nativeCode, glue, thisValue, numArgs, func, newTarget, name.c_str());    \
        Jump(&exit);                                                                                \
    }                                                                                               \
    Bind(&exit);                                                                                    \
    Return(*res);                                                                                   \
}

// map and set stub function
#define DECLARE_BUILTINS_COLLECTION_STUB_BUILDER(method, type, retDefaultValue)                     \
DECLARE_BUILTINS(type##method)                                                                      \
{                                                                                                   \
    auto env = GetEnvironment();                                                                    \
    DEFVARIABLE(res, VariableType::JS_ANY(), retDefaultValue);                                      \
    Label slowPath(env);                                                                            \
    Label exit(env);                                                                                \
    BuiltinsCollectionStubBuilder<JS##type> builder(this, glue, thisValue, numArgs);                \
    builder.method(&res, &exit, &slowPath);                                                         \
    Bind(&slowPath);                                                                                \
    {                                                                                               \
        auto name = BuiltinsStubCSigns::GetName(BUILTINS_STUB_ID(type##method));                    \
        res = CallSlowPath(nativeCode, glue, thisValue, numArgs, func, newTarget, name.c_str());    \
        Jump(&exit);                                                                                \
    }                                                                                               \
    Bind(&exit);                                                                                    \
    Return(*res);                                                                                   \
}

BUILTINS_METHOD_STUB_LIST(DECLARE_BUILTINS_STUB_BUILDER, DECLARE_BUILTINS_STUB_BUILDER1,
                          DECLARE_BUILTINS_COLLECTION_STUB_BUILDER)
#undef DECLARE_BUILTINS_STUB_BUILDER
#undef DECLARE_BUILTINS_STUB_BUILDER1
#undef DECLARE_BUILTINS_COLLECTION_STUB_BUILDER

// aot and builtins public stub function
#define DECLARE_AOT_AND_BUILTINS_STUB_BUILDER(stubName, method, type, initValue)                    \
DECLARE_BUILTINS(stubName)                                                                          \
{                                                                                                   \
    auto env = GetEnvironment();                                                                    \
    DEFVARIABLE(res, VariableType::JS_ANY(), initValue);                                            \
    Label exit(env);                                                                                \
    Label slowPath(env);                                                                            \
    Builtins##type##StubBuilder builder(this);                                                      \
    builder.method(glue, thisValue, numArgs, &res, &exit, &slowPath);                               \
    Bind(&slowPath);                                                                                \
    {                                                                                               \
        auto name = BuiltinsStubCSigns::GetName(BUILTINS_STUB_ID(stubName));                        \
        res = CallSlowPath(nativeCode, glue, thisValue, numArgs, func, newTarget, name.c_str());    \
        Jump(&exit);                                                                                \
    }                                                                                               \
    Bind(&exit);                                                                                    \
    Return(*res);                                                                                   \
}

#define AOT_AND_BUILTINS_STUB_LIST_WITH_METHOD(V)                                                   \
    V(LocaleCompare,              LocaleCompare,      String, Undefined())                          \
    V(STRING_ITERATOR_PROTO_NEXT, StringIteratorNext, String, Undefined())                          \
    V(SORT,                       Sort,               Array,  Undefined())

AOT_AND_BUILTINS_STUB_LIST_WITH_METHOD(DECLARE_AOT_AND_BUILTINS_STUB_BUILDER)
#undef AOT_AND_BUILTINS_STUB_LIST
#undef DECLARE_AOT_AND_BUILTINS_STUB_BUILDER

// containers stub function
#define DECLARE_BUILTINS_WITH_CONTAINERS_STUB_BUILDER(funcName, type, method, methodType, resultVariableType)   \
DECLARE_BUILTINS(type##funcName)                                                                                \
{                                                                                                               \
    auto env = GetEnvironment();                                                                                \
    DEFVARIABLE(res, VariableType::resultVariableType(), Undefined());                                          \
    Label exit(env);                                                                                            \
    Label slowPath(env);                                                                                        \
    ContainersStubBuilder containersBuilder(this);                                                              \
    containersBuilder.method(glue, thisValue, numArgs, &res, &exit, &slowPath, ContainersType::methodType);     \
    Bind(&slowPath);                                                                                            \
    {                                                                                                           \
        auto name = BuiltinsStubCSigns::GetName(BUILTINS_STUB_ID(type##funcName));                              \
        res = CallSlowPath(nativeCode, glue, thisValue, numArgs, func, newTarget, name.c_str());                \
        Jump(&exit);                                                                                            \
    }                                                                                                           \
    Bind(&exit);                                                                                                \
    Return(*res);                                                                                               \
}

BUILTINS_WITH_CONTAINERS_STUB_BUILDER(DECLARE_BUILTINS_WITH_CONTAINERS_STUB_BUILDER)
#undef DECLARE_BUILTINS_WITH_CONTAINERS_STUB_BUILDER

DECLARE_BUILTINS(BooleanConstructor)
{
    auto env = GetEnvironment();
    DEFVARIABLE(res, VariableType::JS_ANY(), Undefined());

    Label newTargetIsHeapObject(env);
    Label newTargetIsJSFunction(env);
    Label slowPath(env);
    Label slowPath1(env);
    Label slowPath2(env);
    Label exit(env);

    BRANCH(TaggedIsHeapObject(newTarget), &newTargetIsHeapObject, &slowPath1);
    Bind(&newTargetIsHeapObject);
    BRANCH(IsJSFunction(newTarget), &newTargetIsJSFunction, &slowPath);
    Bind(&newTargetIsJSFunction);
    {
        Label intialHClassIsHClass(env);
        GateRef intialHClass = Load(VariableType::JS_ANY(), newTarget,
                                    IntPtr(JSFunction::PROTO_OR_DYNCLASS_OFFSET));
        BRANCH(IsJSHClass(intialHClass), &intialHClassIsHClass, &slowPath2);
        Bind(&intialHClassIsHClass);
        {
            NewObjectStubBuilder newBuilder(this);
            newBuilder.SetParameters(glue, 0);
            Label afterNew(env);
            newBuilder.NewJSObject(&res, &afterNew, intialHClass);
            Bind(&afterNew);
            {
                GateRef valueOffset = IntPtr(JSPrimitiveRef::VALUE_OFFSET);
                GateRef value = GetArg(numArgs, IntPtr(0));
                Store(VariableType::INT64(), glue, *res, valueOffset, FastToBoolean(value));
                Jump(&exit);
            }
        }
        Bind(&slowPath2);
        {
            auto name = BuiltinsStubCSigns::GetName(BUILTINS_STUB_ID(BooleanConstructor));
            GateRef argv = GetArgv();
            auto args = { glue, nativeCode, func, thisValue, numArgs, argv, newTarget };
            res = CallBuiltinRuntimeWithNewTarget(glue, args, name.c_str());
            Jump(&exit);
        }
    }
    Bind(&slowPath);
    {
        auto name = BuiltinsStubCSigns::GetName(BUILTINS_STUB_ID(BooleanConstructor));
        GateRef argv = GetArgv();
        auto args = { glue, nativeCode, func, thisValue, numArgs, argv };
        res = CallBuiltinRuntime(glue, args, true, name.c_str());
        Jump(&exit);
    }
    Bind(&slowPath1);
    {
        auto name = BuiltinsStubCSigns::GetName(BUILTINS_STUB_ID(BooleanConstructor));
        res = CallSlowPath(nativeCode, glue, thisValue, numArgs, func, newTarget, name.c_str());
        Jump(&exit);
    }
    Bind(&exit);
    Return(*res);
}

DECLARE_BUILTINS(DateConstructor)
{
    auto env = GetEnvironment();
    DEFVARIABLE(res, VariableType::JS_ANY(), Undefined());

    Label newTargetIsHeapObject(env);
    Label newTargetIsJSFunction(env);
    Label slowPath(env);
    Label slowPath1(env);
    Label slowPath2(env);
    Label exit(env);

    BRANCH(TaggedIsHeapObject(newTarget), &newTargetIsHeapObject, &slowPath1);
    Bind(&newTargetIsHeapObject);
    BRANCH(IsJSFunction(newTarget), &newTargetIsJSFunction, &slowPath);
    Bind(&newTargetIsJSFunction);
    {
        Label intialHClassIsHClass(env);
        GateRef intialHClass = Load(VariableType::JS_ANY(), newTarget,
                                    IntPtr(JSFunction::PROTO_OR_DYNCLASS_OFFSET));
        BRANCH(IsJSHClass(intialHClass), &intialHClassIsHClass, &slowPath2);
        Bind(&intialHClassIsHClass);
        {
            Label oneArg(env);
            Label notOneArg(env);
            Label newJSDate(env);
            DEFVARIABLE(timeValue, VariableType::FLOAT64(), Double(0));
            BRANCH(Int64Equal(numArgs, IntPtr(1)), &oneArg, &notOneArg);
            Bind(&oneArg);
            {
                Label valueIsNumber(env);
                GateRef value = GetArgNCheck(IntPtr(0));
                BRANCH(TaggedIsNumber(value), &valueIsNumber, &slowPath);
                Bind(&valueIsNumber);
                {
                    timeValue = CallNGCRuntime(glue, RTSTUB_ID(TimeClip), {GetDoubleOfTNumber(value)});
                    Jump(&newJSDate);
                }
            }
            Bind(&notOneArg);
            {
                Label threeArgs(env);
                BRANCH(Int64Equal(numArgs, IntPtr(3)), &threeArgs, &slowPath);  // 3: year month day
                Bind(&threeArgs);
                {
                    Label numberYearMonthDay(env);
                    GateRef year = GetArgNCheck(IntPtr(0));
                    GateRef month = GetArgNCheck(IntPtr(1));
                    GateRef day = GetArgNCheck(IntPtr(2));
                    BRANCH(IsNumberYearMonthDay(year, month, day), &numberYearMonthDay, &slowPath);
                    Bind(&numberYearMonthDay);
                    {
                        GateRef y = GetDoubleOfTNumber(year);
                        GateRef m = GetDoubleOfTNumber(month);
                        GateRef d = GetDoubleOfTNumber(day);
                        timeValue = CallNGCRuntime(glue, RTSTUB_ID(SetDateValues), {y, m, d});
                        Jump(&newJSDate);
                    }
                }
            }
            Bind(&newJSDate);
            {
                NewObjectStubBuilder newBuilder(this);
                newBuilder.SetParameters(glue, 0);
                Label afterNew(env);
                newBuilder.NewJSObject(&res, &afterNew, intialHClass);
                Bind(&afterNew);
                {
                    GateRef timeValueOffset = IntPtr(JSDate::TIME_VALUE_OFFSET);
                    Store(VariableType::JS_NOT_POINTER(), glue, *res, timeValueOffset,
                          DoubleToTaggedDoublePtr(*timeValue));
                    Jump(&exit);
                }
            }
        }
        Bind(&slowPath2);
        {
            auto name = BuiltinsStubCSigns::GetName(BUILTINS_STUB_ID(DateConstructor));
            GateRef argv = GetArgv();
            res = CallBuiltinRuntimeWithNewTarget(glue, { glue, nativeCode, func, thisValue, numArgs, argv, newTarget },
                name.c_str());
            Jump(&exit);
        }
    }
    Bind(&slowPath);
    {
        auto name = BuiltinsStubCSigns::GetName(BUILTINS_STUB_ID(DateConstructor));
        GateRef argv = GetArgv();
        res = CallBuiltinRuntime(glue, { glue, nativeCode, func, thisValue, numArgs, argv }, true, name.c_str());
        Jump(&exit);
    }
    Bind(&slowPath1);
    {
        auto name = BuiltinsStubCSigns::GetName(BUILTINS_STUB_ID(DateConstructor));
        res = CallSlowPath(nativeCode, glue, thisValue, numArgs, func, newTarget, name.c_str());
        Jump(&exit);
    }
    Bind(&exit);
    Return(*res);
}

DECLARE_BUILTINS(NumberConstructor)
{
    BuiltinsNumberStubBuilder builder(this, glue, thisValue, numArgs);
    builder.GenNumberConstructor(nativeCode, func, newTarget);
}

DECLARE_BUILTINS(ArrayConstructor)
{
    BuiltinsArrayStubBuilder builder(this);
    builder.GenArrayConstructor(glue, nativeCode, func, newTarget, thisValue, numArgs);
}

DECLARE_BUILTINS(MapConstructor)
{
    LinkedHashTableStubBuilder<LinkedHashMap, LinkedHashMapObject> hashTableBuilder(this, glue);
    hashTableBuilder.GenMapSetConstructor(nativeCode, func, newTarget, thisValue, numArgs);
}

DECLARE_BUILTINS(SetConstructor)
{
    LinkedHashTableStubBuilder<LinkedHashSet, LinkedHashSetObject> hashTableBuilder(this, glue);
    hashTableBuilder.GenMapSetConstructor(nativeCode, func, newTarget, thisValue, numArgs);
}
}  // namespace panda::ecmascript::kungfu
