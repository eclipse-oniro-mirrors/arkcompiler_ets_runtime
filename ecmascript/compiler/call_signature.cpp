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

#include "ecmascript/compiler/call_signature.h"

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wshadow"
#pragma clang diagnostic ignored "-Wunused-parameter"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif

#include "llvm-c/Core.h"
#include "llvm/Support/Host.h"

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace panda::ecmascript::kungfu {
DEF_CALL_SIGNATURE(Add)
{
    // 3 : 3 input parameters
    CallSignature Add("Add", 0, 3, ArgumentsOrder::DEFAULT_ORDER, VariableType::JS_ANY()); // number or hole
    *callSign = Add;
    // 3 : 3 input parameters
    std::array<VariableType, 3> params = {
        VariableType::NATIVE_POINTER(),
        VariableType::JS_ANY(),
        VariableType::JS_ANY(),
    };
    callSign->SetParameters(params.data());
    callSign->SetCallConv(CallSignature::CallConv::CCallConv);
}

DEF_CALL_SIGNATURE(Sub)
{
    // 3 : 3 input parameters
    CallSignature Sub("Sub", 0, 3, ArgumentsOrder::DEFAULT_ORDER, VariableType::JS_ANY()); // number or hole
    *callSign = Sub;
    // 3 : 3 input parameters
    std::array<VariableType, 3> params = {
        VariableType::NATIVE_POINTER(),
        VariableType::JS_ANY(),
        VariableType::JS_ANY(),
    };
    callSign->SetParameters(params.data());
    callSign->SetCallConv(CallSignature::CallConv::CCallConv);
}

DEF_CALL_SIGNATURE(Mul)
{
    // 3 : 3 input parameters
    CallSignature Mul("Mul", 0, 3, ArgumentsOrder::DEFAULT_ORDER, VariableType::JS_ANY()); // number or hole
    *callSign = Mul;
    // 3 : 3 input parameters
    std::array<VariableType, 3> params = {
        VariableType::NATIVE_POINTER(),
        VariableType::JS_ANY(),
        VariableType::JS_ANY(),
    };
    callSign->SetParameters(params.data());
    callSign->SetCallConv(CallSignature::CallConv::CCallConv);
}

#ifndef NDEBUG
DEF_CALL_SIGNATURE(MulGCTest)
{
    // 3 : 3 input parameters
    CallSignature MulGC("MulGCTest", 0, 3, ArgumentsOrder::DEFAULT_ORDER, VariableType::JS_ANY());
    *callSign = MulGC;
    // 3 : 3 input parameters
    std::array<VariableType, 3> params = {
        VariableType::NATIVE_POINTER(),
        VariableType::INT64(),
        VariableType::INT64(),
    };
    callSign->SetParameters(params.data());
    callSign->SetCallConv(CallSignature::CallConv::CCallConv);
}
#else
DEF_CALL_SIGNATURE(MulGCTest) {}
#endif

DEF_CALL_SIGNATURE(Div)
{
    // 3 : 3 input parameters
    CallSignature Div("Div", 0, 3, ArgumentsOrder::DEFAULT_ORDER, VariableType::JS_ANY()); // float or hole
    *callSign = Div;
    // 3 : 3 input parameters
    std::array<VariableType, 3> params = {
        VariableType::NATIVE_POINTER(),
        VariableType::JS_ANY(),
        VariableType::JS_ANY(),
    };
    callSign->SetParameters(params.data());
    callSign->SetCallConv(CallSignature::CallConv::CCallConv);
}

DEF_CALL_SIGNATURE(Mod)
{
    // 3 : 3 input parameters
    CallSignature Mod("Mod", 0, 3,
        ArgumentsOrder::DEFAULT_ORDER, VariableType::JS_ANY()); // int,float or hole
    *callSign = Mod;
    // 3 : 3 input parameters
    std::array<VariableType, 3> params = {
        VariableType::NATIVE_POINTER(),
        VariableType::JS_ANY(),
        VariableType::JS_ANY(),
    };
    callSign->SetParameters(params.data());
    callSign->SetCallConv(CallSignature::CallConv::CCallConv);
}

DEF_CALL_SIGNATURE(TypeOf)
{
    // 2 input parameters
    CallSignature TypeOf("TypeOf", 0, 2, ArgumentsOrder::DEFAULT_ORDER, VariableType::JS_POINTER());
    *callSign = TypeOf;
    // 2 input parameters
    std::array<VariableType, 2> params = {
        VariableType::NATIVE_POINTER(), // glue
        VariableType::JS_ANY(), // ACC
    };
    callSign->SetParameters(params.data());
    callSign->SetCallConv(CallSignature::CallConv::CCallConv);
}

DEF_CALL_SIGNATURE(Equal)
{
    // 3 input parameters, return may be true/false/hole
    CallSignature Equal("Equal", 0, 3, ArgumentsOrder::DEFAULT_ORDER, VariableType::JS_ANY());
    *callSign = Equal;
    // 3 input parameters
    std::array<VariableType, 3> params = {
        VariableType::NATIVE_POINTER(),
        VariableType::JS_ANY(),
        VariableType::JS_ANY(),
    };
    callSign->SetParameters(params.data());
    callSign->SetCallConv(CallSignature::CallConv::CCallConv);
}

DEF_CALL_SIGNATURE(SetPropertyByName)
{
    // 6 : 6 input parameters
    CallSignature setPropertyByName("SetPropertyByName", 0, 6, ArgumentsOrder::DEFAULT_ORDER, VariableType::JS_ANY());
    *callSign = setPropertyByName;
    // 6 : 6 input parameters
    std::array<VariableType, 6> params = {
        VariableType::NATIVE_POINTER(),   // glue
        VariableType::JS_ANY(),           // receiver
        VariableType::JS_POINTER(),       // key
        VariableType::JS_ANY(),           // value
        VariableType::JS_POINTER(),       // profileTypeInfo
        VariableType::INT32(),            // slot id
    };
    callSign->SetParameters(params.data());
    callSign->SetCallConv(CallSignature::CallConv::CCallConv);
}

DEF_CALL_SIGNATURE(DeprecatedSetPropertyByName)
{
    // 4 : 4 input parameters
    CallSignature setPropertyByName("DeprecatedSetPropertyByName", 0, 4, ArgumentsOrder::DEFAULT_ORDER,
        VariableType::JS_ANY());
    *callSign = setPropertyByName;
    // 4 : 4 input parameters
    std::array<VariableType, 4> params = {
        VariableType::NATIVE_POINTER(),
        VariableType::JS_POINTER(),
        VariableType::JS_POINTER(),
        VariableType::JS_ANY()
    };
    callSign->SetParameters(params.data());
    callSign->SetCallConv(CallSignature::CallConv::CCallConv);
}

DEF_CALL_SIGNATURE(SetPropertyByNameWithOwn)
{
    // 4 : 4 input parameters
    CallSignature setPropertyByNameWithOwn("SetPropertyByNameWithOwn", 0, 4, ArgumentsOrder::DEFAULT_ORDER,
        VariableType::JS_ANY());
    *callSign = setPropertyByNameWithOwn;
    // 4 : 4 input parameters
    std::array<VariableType, 4> params = {
        VariableType::NATIVE_POINTER(),
        VariableType::JS_POINTER(),
        VariableType::JS_POINTER(),
        VariableType::JS_ANY()
    };
    callSign->SetParameters(params.data());
    callSign->SetCallConv(CallSignature::CallConv::CCallConv);
}

DEF_CALL_SIGNATURE(SetPropertyByValue)
{
    // 6 : 6 input parameters
    CallSignature setPropertyByName("SetPropertyByValue", 0, 6, ArgumentsOrder::DEFAULT_ORDER,
        VariableType::JS_ANY());
    *callSign = setPropertyByName;
    // 6 : 6 input parameters
    std::array<VariableType, 6> params = {
        VariableType::NATIVE_POINTER(),
        VariableType::JS_POINTER(),
        VariableType::JS_ANY(),
        VariableType::JS_ANY(),
        VariableType::JS_POINTER(),
        VariableType::INT32(),
    };
    callSign->SetParameters(params.data());
    callSign->SetCallConv(CallSignature::CallConv::CCallConv);
}

DEF_CALL_SIGNATURE(DeprecatedSetPropertyByValue)
{
    // 4 : 4 input parameters
    CallSignature setPropertyByName("DeprecatedSetPropertyByValue", 0, 4, ArgumentsOrder::DEFAULT_ORDER,
        VariableType::JS_ANY());
    *callSign = setPropertyByName;
    // 4 : 4 input parameters
    std::array<VariableType, 4> params = {
        VariableType::NATIVE_POINTER(),
        VariableType::JS_POINTER(),
        VariableType::JS_ANY(),
        VariableType::JS_ANY()
    };
    callSign->SetParameters(params.data());
    callSign->SetCallConv(CallSignature::CallConv::CCallConv);
}

DEF_CALL_SIGNATURE(SetPropertyByValueWithOwn)
{
    // 4 : 4 input parameters
    CallSignature setPropertyByValueWithOwn("SetPropertyByValueWithOwn", 0, 4, ArgumentsOrder::DEFAULT_ORDER,
        VariableType::JS_ANY());
    *callSign = setPropertyByValueWithOwn;
    // 4 : 4 input parameters
    std::array<VariableType, 4> params = {
        VariableType::NATIVE_POINTER(),
        VariableType::JS_POINTER(),
        VariableType::JS_ANY(),
        VariableType::JS_ANY()
    };
    callSign->SetParameters(params.data());
    callSign->SetCallConv(CallSignature::CallConv::CCallConv);
}

DEF_CALL_SIGNATURE(GetPropertyByName)
{
    // 5 : 5 input parameters
    CallSignature getPropertyByName("GetPropertyByName", 0, 5, ArgumentsOrder::DEFAULT_ORDER, VariableType::JS_ANY());
    *callSign = getPropertyByName;
    // 5 : 5 input parameters
    std::array<VariableType, 5> params = {
        VariableType::NATIVE_POINTER(),   // glue
        VariableType::JS_ANY(),           // receiver
        VariableType::JS_POINTER(),       // key
        VariableType::JS_POINTER(),       // profileTypeInfo
        VariableType::INT32(),            // slot id
    };
    callSign->SetParameters(params.data());
    callSign->SetCallConv(CallSignature::CallConv::CCallConv);
}

DEF_CALL_SIGNATURE(DeprecatedGetPropertyByName)
{
    // 3 : 3 input parameters
    CallSignature getPropertyByName("DeprecatedGetPropertyByName", 0, 3, ArgumentsOrder::DEFAULT_ORDER,
        VariableType::JS_ANY());
    *callSign = getPropertyByName;
    // 3 : 3 input parameters
    std::array<VariableType, 3> params = {
        VariableType::NATIVE_POINTER(), // glue
        VariableType::JS_ANY(),         // receiver
        VariableType::JS_POINTER(),     // key
    };
    callSign->SetParameters(params.data());
    callSign->SetCallConv(CallSignature::CallConv::CCallConv);
}

DEF_CALL_SIGNATURE(TryLdGlobalByName)
{
    // 4 : 4 input parameters
    CallSignature signature("TryLdGlobalByName", 0, 4, ArgumentsOrder::DEFAULT_ORDER, VariableType::JS_ANY());
    *callSign = signature;
    // 4 : 4 input parameters
    std::array<VariableType, 4> params = {
        VariableType::NATIVE_POINTER(),   // glue
        VariableType::JS_POINTER(),       // key
        VariableType::JS_POINTER(),       // profileTypeInfo
        VariableType::INT32(),            // slot id
    };
    callSign->SetParameters(params.data());
    callSign->SetCallConv(CallSignature::CallConv::CCallConv);
}

DEF_CALL_SIGNATURE(TryStGlobalByName)
{
    // 5 : 5 input parameters
    CallSignature signature("TryStGlobalByName", 0, 5, ArgumentsOrder::DEFAULT_ORDER, VariableType::JS_ANY());
    *callSign = signature;
    // 5 : 5 input parameters
    std::array<VariableType, 5> params = {
        VariableType::NATIVE_POINTER(),   // glue
        VariableType::JS_POINTER(),       // key
        VariableType::JS_ANY(),           // value
        VariableType::JS_POINTER(),       // profileTypeInfo
        VariableType::INT32(),            // slot id
    };
    callSign->SetParameters(params.data());
    callSign->SetCallConv(CallSignature::CallConv::CCallConv);
}

DEF_CALL_SIGNATURE(LdGlobalVar)
{
    // 4 : 4 input parameters
    CallSignature signature("LdGlobalVar", 0, 4, ArgumentsOrder::DEFAULT_ORDER, VariableType::JS_ANY());
    *callSign = signature;
    // 4 : 4 input parameters
    std::array<VariableType, 4> params = {
        VariableType::NATIVE_POINTER(),   // glue
        VariableType::JS_POINTER(),       // key
        VariableType::JS_POINTER(),       // profileTypeInfo
        VariableType::INT32(),            // slot id
    };
    callSign->SetParameters(params.data());
    callSign->SetCallConv(CallSignature::CallConv::CCallConv);
}

DEF_CALL_SIGNATURE(StGlobalVar)
{
    // 5 : 5 input parameters
    CallSignature signature("StGlobalVar", 0, 5, ArgumentsOrder::DEFAULT_ORDER, VariableType::JS_ANY());
    *callSign = signature;
    // 5 : 5 input parameters
    std::array<VariableType, 5> params = {
        VariableType::NATIVE_POINTER(),   // glue
        VariableType::JS_POINTER(),       // key
        VariableType::JS_ANY(),           // value
        VariableType::JS_POINTER(),       // profileTypeInfo
        VariableType::INT32(),            // slot id
    };
    callSign->SetParameters(params.data());
    callSign->SetCallConv(CallSignature::CallConv::CCallConv);
}

DEF_CALL_SIGNATURE(GetPropertyByIndex)
{
    // 3 : 3 input parameters
    CallSignature getPropertyByIndex("GetPropertyByIndex", 0, 3, ArgumentsOrder::DEFAULT_ORDER,
        VariableType::JS_ANY());
    *callSign = getPropertyByIndex;
    // 3 : 3 input parameters
    std::array<VariableType, 3> params = {
        VariableType::NATIVE_POINTER(), // glue
        VariableType::JS_ANY(), // receiver
        VariableType::INT32(), // index
    };
    callSign->SetParameters(params.data());
    callSign->SetCallConv(CallSignature::CallConv::CCallConv);
}

DEF_CALL_SIGNATURE(SetPropertyByIndex)
{
    // 4 : 4 input parameters
    CallSignature setPropertyByIndex("SetPropertyByIndex", 0, 4, ArgumentsOrder::DEFAULT_ORDER,
        VariableType::JS_ANY()); // hole or undefined
    *callSign = setPropertyByIndex;
    // 4 : 4 input parameters
    std::array<VariableType, 4> params = {
        VariableType::NATIVE_POINTER(),
        VariableType::JS_POINTER(),
        VariableType::INT32(),
        VariableType::JS_ANY()
    };
    callSign->SetParameters(params.data());
    callSign->SetCallConv(CallSignature::CallConv::CCallConv);
}

DEF_CALL_SIGNATURE(SetPropertyByIndexWithOwn)
{
    // 4 : 4 input parameters
    CallSignature setPropertyByIndexWithOwn("SetPropertyByIndexWithOwn", 0, 4, ArgumentsOrder::DEFAULT_ORDER,
        VariableType::JS_ANY()); // hole or undefined
    *callSign = setPropertyByIndexWithOwn;
    // 4 : 4 input parameters
    std::array<VariableType, 4> params = {
        VariableType::NATIVE_POINTER(),
        VariableType::JS_POINTER(),
        VariableType::INT32(),
        VariableType::JS_ANY()
    };
    callSign->SetParameters(params.data());
    callSign->SetCallConv(CallSignature::CallConv::CCallConv);
}

DEF_CALL_SIGNATURE(GetPropertyByValue)
{
    // 5 : 5 input parameters
    CallSignature getPropertyByValue("GetPropertyByValue", 0, 5, ArgumentsOrder::DEFAULT_ORDER,
                                      VariableType::JS_ANY());
    *callSign = getPropertyByValue;
    // 5 : 5 input parameters
    std::array<VariableType, 5> params = {
        VariableType::NATIVE_POINTER(),
        VariableType::JS_POINTER(),
        VariableType::JS_ANY(),
        VariableType::JS_POINTER(),
        VariableType::INT32(),
    };
    callSign->SetParameters(params.data());
    callSign->SetCallConv(CallSignature::CallConv::CCallConv);
}

DEF_CALL_SIGNATURE(DeprecatedGetPropertyByValue)
{
    // 3 : 3 input parameters
    CallSignature getPropertyByValue("DeprecatedGetPropertyByValue", 0, 3, ArgumentsOrder::DEFAULT_ORDER,
                                      VariableType::JS_ANY());
    *callSign = getPropertyByValue;
    // 3 : 3 input parameters
    std::array<VariableType, 3> params = {
        VariableType::NATIVE_POINTER(),
        VariableType::JS_POINTER(),
        VariableType::JS_ANY(),
    };
    callSign->SetParameters(params.data());
    callSign->SetCallConv(CallSignature::CallConv::CCallConv);
}

DEF_CALL_SIGNATURE(TryLoadICByName)
{
    // 4 : 4 input parameters
    CallSignature tryLoadICByName("TryLoadICByName", 0, 4,
        ArgumentsOrder::DEFAULT_ORDER, VariableType::JS_ANY());
    *callSign = tryLoadICByName;
    // 4 : 4 input parameters
    std::array<VariableType, 4> params = {
        VariableType::NATIVE_POINTER(),
        VariableType::JS_ANY(),
        VariableType::JS_ANY(),
        VariableType::JS_ANY(),
    };
    callSign->SetParameters(params.data());
    callSign->SetCallConv(CallSignature::CallConv::CCallConv);
}

DEF_CALL_SIGNATURE(TryLoadICByValue)
{
    // 5 : 5 input parameters
    CallSignature tryLoadICByValue("TryLoadICByValue", 0, 5,
        ArgumentsOrder::DEFAULT_ORDER, VariableType::JS_ANY());
    *callSign = tryLoadICByValue;
    // 5 : 5 input parameters
    std::array<VariableType, 5> params = {
        VariableType::NATIVE_POINTER(),
        VariableType::JS_ANY(),
        VariableType::JS_ANY(),
        VariableType::JS_ANY(),
        VariableType::JS_ANY(),
    };
    callSign->SetParameters(params.data());
    callSign->SetCallConv(CallSignature::CallConv::CCallConv);
}

DEF_CALL_SIGNATURE(TryStoreICByName)
{
    // 5 : 5 input parameters
    CallSignature tryStoreICByName("TryStoreICByName", 0, 5,
        ArgumentsOrder::DEFAULT_ORDER, VariableType::JS_ANY()); // undefined or hole
    *callSign = tryStoreICByName;
    // 5 : 5 input parameters
    std::array<VariableType, 5> params = {
        VariableType::NATIVE_POINTER(),
        VariableType::JS_POINTER(),
        VariableType::JS_ANY(),
        VariableType::JS_ANY(),
        VariableType::JS_POINTER(),
    };
    callSign->SetParameters(params.data());
    callSign->SetCallConv(CallSignature::CallConv::CCallConv);
}

DEF_CALL_SIGNATURE(TryStoreICByValue)
{
    // 6 : 6 input parameters
    CallSignature tryStoreICByValue("TryStoreICByValue", 0, 6,
        ArgumentsOrder::DEFAULT_ORDER, VariableType::JS_ANY()); // undefined or hole
    *callSign = tryStoreICByValue;
    // 6 : 6 input parameters
    std::array<VariableType, 6> params = {
        VariableType::NATIVE_POINTER(),
        VariableType::JS_POINTER(),
        VariableType::JS_ANY(),
        VariableType::JS_ANY(),
        VariableType::JS_ANY(),
        VariableType::JS_POINTER(),
    };
    callSign->SetParameters(params.data());
    callSign->SetCallConv(CallSignature::CallConv::CCallConv);
}

DEF_CALL_SIGNATURE(SetValueWithBarrier)
{
    // 4 : 4 input parameters
    CallSignature setValueWithBarrier("SetValueWithBarrier", 0, 4, ArgumentsOrder::DEFAULT_ORDER,
        VariableType::VOID());
    *callSign = setValueWithBarrier;

    std::array<VariableType, 4> params = { // 4 : 4 input parameters
        VariableType::NATIVE_POINTER(),
        VariableType::JS_POINTER(),
        VariableType::NATIVE_POINTER(),
        VariableType::JS_ANY()
    };
    callSign->SetParameters(params.data());
    callSign->SetGCLeafFunction(true);
    callSign->SetCallConv(CallSignature::CallConv::CCallConv);
}

DEF_CALL_SIGNATURE(GetTaggedArrayPtrTest)
{
    // 2 : 2 input parameters
    CallSignature getTaggedArrayPtr("GetTaggedArrayPtrTest", 0, 2, ArgumentsOrder::DEFAULT_ORDER,
                                     VariableType::JS_POINTER());
    *callSign = getTaggedArrayPtr;
    // 2 : 2 input parameters
    std::array<VariableType, 2> params = {
        VariableType::NATIVE_POINTER(),
        VariableType::JS_ANY(),
    };
    callSign->SetParameters(params.data());
    callSign->SetTargetKind(CallSignature::TargetKind::RUNTIME_STUB);
}

DEF_CALL_SIGNATURE(Builtins)
{
    // 8 : 8 input parameters
    CallSignature builtins("Builtins", 0, 8,
        ArgumentsOrder::DEFAULT_ORDER, VariableType::JS_ANY());
    *callSign = builtins;
    std::array<VariableType, 8> params = { // 8 : 8 input parameters
        VariableType::NATIVE_POINTER(),
        VariableType::NATIVE_POINTER(),
        VariableType::JS_ANY(),
        VariableType::JS_ANY(),
        VariableType::NATIVE_POINTER(),
        VariableType::JS_ANY(),
        VariableType::JS_ANY(),
        VariableType::JS_ANY(),
    };
    callSign->SetVariadicArgs(true);
    callSign->SetParameters(params.data());
    callSign->SetTargetKind(CallSignature::TargetKind::BUILTINS_STUB);
    callSign->SetCallConv(CallSignature::CallConv::CCallConv);
}

DEF_CALL_SIGNATURE(BytecodeHandler)
{
    // 7 : 7 input parameters
    CallSignature bytecodeHandler("BytecodeHandler", 0, 7,
        ArgumentsOrder::DEFAULT_ORDER, VariableType::VOID());
    *callSign = bytecodeHandler;
    // 7 : 7 input parameters
    std::array<VariableType, 7> params = {
        VariableType::NATIVE_POINTER(),
        VariableType::NATIVE_POINTER(),
        VariableType::NATIVE_POINTER(),
        VariableType::JS_POINTER(),
        VariableType::JS_POINTER(),
        VariableType::JS_ANY(),
        VariableType::INT32(),
    };
    callSign->SetParameters(params.data());
    callSign->SetTargetKind(CallSignature::TargetKind::BYTECODE_HANDLER);
    callSign->SetCallConv(CallSignature::CallConv::GHCCallConv);
}

DEF_CALL_SIGNATURE(BytecodeDebuggerHandler)
{
    // 7 : 7 input parameters
    CallSignature bytecodeHandler("BytecodeDebuggerHandler", 0, 7,
                                  ArgumentsOrder::DEFAULT_ORDER, VariableType::VOID());
    *callSign = bytecodeHandler;
    // 7 : 7 input parameters
    std::array<VariableType, 7> params = { VariableType::NATIVE_POINTER(),
                                           VariableType::NATIVE_POINTER(),
                                           VariableType::NATIVE_POINTER(),
                                           VariableType::JS_POINTER(),
                                           VariableType::JS_POINTER(),
                                           VariableType::JS_ANY(),
                                           VariableType::INT32() };
    callSign->SetParameters(params.data());
    callSign->SetTargetKind(CallSignature::TargetKind::BYTECODE_DEBUGGER_HANDLER);
}

DEF_CALL_SIGNATURE(CallRuntime)
{
    /* 3 : 3 input parameters */
    CallSignature runtimeCallTrampoline("CallRuntime", 0, 3,
        ArgumentsOrder::DEFAULT_ORDER, VariableType::JS_ANY());
    *callSign = runtimeCallTrampoline;
    /* 3 : 3 input parameters */
    std::array<VariableType, 3> params = {
        VariableType::NATIVE_POINTER(),
        VariableType::INT64(),
        VariableType::INT64(),
    };
    callSign->SetVariadicArgs(true);
    callSign->SetParameters(params.data());
    callSign->SetTargetKind(CallSignature::TargetKind::RUNTIME_STUB);
    callSign->SetCallConv(CallSignature::CallConv::CCallConv);
}

DEF_CALL_SIGNATURE(AsmInterpreterEntry)
{
    /* 3 : 3 input parameters */
    CallSignature asmInterpreterEntry("AsmInterpreterEntry", 0, 3,
        ArgumentsOrder::DEFAULT_ORDER, VariableType::JS_ANY());
    *callSign = asmInterpreterEntry;
    /* 3 : 3 input parameters */
    std::array<VariableType, 3> params = {
        VariableType::NATIVE_POINTER(),  // glue
        VariableType::INT32(),  // argc
        VariableType::NATIVE_POINTER(),  // argv
    };
    callSign->SetParameters(params.data());
    callSign->SetTargetKind(CallSignature::TargetKind::RUNTIME_STUB_NO_GC);
    callSign->SetCallConv(CallSignature::CallConv::CCallConv);
}

DEF_CALL_SIGNATURE(GeneratorReEnterAsmInterp)
{
    /* 2 : 2 input parameters */
    CallSignature generatorReEnterAsmInterp("GeneratorReEnterAsmInterp", 0, 2,
        ArgumentsOrder::DEFAULT_ORDER, VariableType::JS_ANY());
    *callSign = generatorReEnterAsmInterp;
    std::array<VariableType, 2> params = { /* 2 : 2 input parameters */
        VariableType::NATIVE_POINTER(),  // glue
        VariableType::JS_POINTER(),      // context
    };
    callSign->SetParameters(params.data());
    callSign->SetTargetKind(CallSignature::TargetKind::RUNTIME_STUB_NO_GC);
    callSign->SetCallConv(CallSignature::CallConv::CCallConv);
}

DEF_CALL_SIGNATURE(CallRuntimeWithArgv)
{
    /* 4 : 4 input parameters */
    CallSignature runtimeCallTrampoline("CallRuntimeWithArgv", 0, 4,
        ArgumentsOrder::DEFAULT_ORDER, VariableType::JS_ANY());
    *callSign = runtimeCallTrampoline;
    std::array<VariableType, 4> params = { /* 4 : 4 input parameters */
        VariableType::NATIVE_POINTER(), // glue
        VariableType::INT64(),   // runtimeId
        VariableType::INT64(),   // argc
        VariableType::NATIVE_POINTER(), // argv
    };
    callSign->SetVariadicArgs(false);
    callSign->SetParameters(params.data());
    callSign->SetTargetKind(CallSignature::TargetKind::RUNTIME_STUB_VARARGS);
    callSign->SetCallConv(CallSignature::CallConv::CCallConv);
}

DEF_CALL_SIGNATURE(OptimizedCallOptimized)
{
    /* 4 : 4 input parameters */
    CallSignature runtimeCallTrampoline("OptimizedCallOptimized", 0, 4,
        ArgumentsOrder::DEFAULT_ORDER, VariableType::JS_ANY());
    *callSign = runtimeCallTrampoline;
    std::array<VariableType, 4> params = { /* 4 : 4 input parameters */
        VariableType::NATIVE_POINTER(),
        VariableType::INT64(),
        VariableType::INT64(),
        VariableType::NATIVE_POINTER(),
    };
    callSign->SetVariadicArgs(true);
    callSign->SetParameters(params.data());
    callSign->SetTargetKind(CallSignature::TargetKind::RUNTIME_STUB_NO_GC);
    callSign->SetCallConv(CallSignature::CallConv::CCallConv);
}

DEF_CALL_SIGNATURE(JSCall)
{
    // 6 : 6 input parameters
    CallSignature jSCall("JSCall", 0, 6,
        ArgumentsOrder::DEFAULT_ORDER, VariableType::JS_ANY());
    *callSign = jSCall;
    std::array<VariableType, 6> params = { // 6 : 6 input parameters
        VariableType::NATIVE_POINTER(),     // glue
        VariableType::JS_ANY(),      // lexenv
        VariableType::INT64(),       // actual argC
        VariableType::JS_ANY(),      // call target
        VariableType::JS_ANY(),      // new target
        VariableType::JS_ANY(),      // thisobj
    };
    callSign->SetVariadicArgs(true);
    callSign->SetParameters(params.data());
    callSign->SetCallConv(CallSignature::CallConv::WebKitJSCallConv);
    callSign->SetTargetKind(CallSignature::TargetKind::RUNTIME_STUB_NO_GC);
}

DEF_CALL_SIGNATURE(JSCallNew)
{
    // 6 : 6 input parameters
    CallSignature jSCallNew("JSCallNew", 0, 6,
        ArgumentsOrder::DEFAULT_ORDER, VariableType::JS_ANY());
    *callSign = jSCallNew;
    std::array<VariableType, 6> params = { // 6 : 6 input parameters
        VariableType::NATIVE_POINTER(),     // glue
        VariableType::JS_ANY(),      // lexenv
        VariableType::INT64(),       // actual argC
        VariableType::JS_ANY(),      // call target
        VariableType::JS_ANY(),      // new target
        VariableType::JS_ANY(),      // thisobj
    };
    callSign->SetVariadicArgs(true);
    callSign->SetParameters(params.data());
    callSign->SetCallConv(CallSignature::CallConv::WebKitJSCallConv);
    callSign->SetTargetKind(CallSignature::TargetKind::RUNTIME_STUB_NO_GC);
}

DEF_CALL_SIGNATURE(ConstructorJSCall)
{
    // 6 : 6 input parameters
    CallSignature constructorJsCall("ConstructorJSCall", 0, 6,
        ArgumentsOrder::DEFAULT_ORDER, VariableType::JS_ANY());
    *callSign = constructorJsCall;
    std::array<VariableType, 6> params = { // 6 : 6 input parameters
        VariableType::NATIVE_POINTER(),     // glue
        VariableType::JS_ANY(),      // lexenv
        VariableType::INT64(),       // actual argC
        VariableType::JS_ANY(),      // call target
        VariableType::JS_ANY(),      // new target
        VariableType::JS_ANY(),      // thisobj
    };
    callSign->SetVariadicArgs(true);
    callSign->SetParameters(params.data());
    callSign->SetCallConv(CallSignature::CallConv::WebKitJSCallConv);
    callSign->SetTargetKind(CallSignature::TargetKind::RUNTIME_STUB_NO_GC);
}

DEF_CALL_SIGNATURE(JSProxyCallInternalWithArgV)
{
    // 4 : 4 input parameters
    CallSignature jSProxyCallInternalWithArgV("JSProxyCallInternalWithArgV", 0, 4,
        ArgumentsOrder::DEFAULT_ORDER, VariableType::JS_ANY());
    *callSign = jSProxyCallInternalWithArgV;
    std::array<VariableType, 4> params = { // 4 : 4 input parameters
        VariableType::NATIVE_POINTER(),     // glue
        VariableType::INT64(),       // actual argC
        VariableType::JS_ANY(),      // call target
        VariableType::NATIVE_POINTER(),    // argv
    };
    callSign->SetParameters(params.data());
    callSign->SetTargetKind(CallSignature::TargetKind::RUNTIME_STUB_NO_GC);
    callSign->SetGCLeafFunction(true);
    callSign->SetTailCall(true);
}

DEF_CALL_SIGNATURE(JSFunctionEntry)
{
    // 5 : 5 input parameters
    CallSignature jsCallFunctionEntry("JSFunctionEntry", 0, 5,
        ArgumentsOrder::DEFAULT_ORDER, VariableType::JS_ANY());
    *callSign = jsCallFunctionEntry;
    std::array<VariableType, 5> params = {  // 5 : 5 input parameters
        VariableType::NATIVE_POINTER(),     // glue
        VariableType::INT64(),              // argc
        VariableType::NATIVE_POINTER(),     // argv
        VariableType::NATIVE_POINTER(),     // prevFp
        VariableType::BOOL(),               // isNew
    };
    callSign->SetParameters(params.data());
    callSign->SetTargetKind(CallSignature::TargetKind::RUNTIME_STUB_NO_GC);
    callSign->SetCallConv(CallSignature::CallConv::CCallConv);
}

DEF_CALL_SIGNATURE(ResumeRspAndDispatch)
{
    // 8 : 8 input parameters
    CallSignature resumeRspAndDispatch("ResumeRspAndDispatch", 0, 8,
        ArgumentsOrder::DEFAULT_ORDER, VariableType::VOID());
    *callSign = resumeRspAndDispatch;
    std::array<VariableType, 8> params = { // 8 : 8 input parameters
        VariableType::NATIVE_POINTER(),
        VariableType::NATIVE_POINTER(),
        VariableType::NATIVE_POINTER(),
        VariableType::JS_POINTER(),
        VariableType::JS_POINTER(),
        VariableType::JS_ANY(),
        VariableType::INT32(),
        VariableType::NATIVE_POINTER(),
    };
    callSign->SetParameters(params.data());
    callSign->SetTargetKind(CallSignature::TargetKind::RUNTIME_STUB_NO_GC);
    callSign->SetCallConv(CallSignature::CallConv::GHCCallConv);
}

DEF_CALL_SIGNATURE(ResumeRspAndReturn)
{
    // 3 : 3 input parameters
    CallSignature resumeRspAndReturn("ResumeRspAndReturn", 0, 3,
        ArgumentsOrder::DEFAULT_ORDER, VariableType::VOID());
    *callSign = resumeRspAndReturn;
    std::array<VariableType, 3> params = { // 3 : 3 input parameters
        VariableType::JS_ANY(),
        VariableType::NATIVE_POINTER(),
        VariableType::NATIVE_POINTER(),
    };
    callSign->SetParameters(params.data());
    callSign->SetTargetKind(CallSignature::TargetKind::RUNTIME_STUB_NO_GC);
    callSign->SetCallConv(CallSignature::CallConv::GHCCallConv);
}

DEF_CALL_SIGNATURE(ResumeCaughtFrameAndDispatch)
{
    // 7 : 7 input parameters
    CallSignature resumeCaughtFrameAndDispatch("ResumeCaughtFrameAndDispatch", 0, 7,
        ArgumentsOrder::DEFAULT_ORDER, VariableType::VOID());
    *callSign = resumeCaughtFrameAndDispatch;
    // 7 : 7 input parameters
    std::array<VariableType, 7> params = {
        VariableType::NATIVE_POINTER(),
        VariableType::NATIVE_POINTER(),
        VariableType::NATIVE_POINTER(),
        VariableType::JS_POINTER(),
        VariableType::JS_POINTER(),
        VariableType::JS_ANY(),
        VariableType::INT32(),
    };
    callSign->SetParameters(params.data());
    callSign->SetTargetKind(CallSignature::TargetKind::RUNTIME_STUB_NO_GC);
    callSign->SetCallConv(CallSignature::CallConv::GHCCallConv);
}

DEF_CALL_SIGNATURE(ResumeUncaughtFrameAndReturn)
{
    // 3 : 3 input parameters
    CallSignature resumeUncaughtFrameAndReturn("ResumeUncaughtFrameAndReturn", 0, 3,
        ArgumentsOrder::DEFAULT_ORDER, VariableType::VOID());
    *callSign = resumeUncaughtFrameAndReturn;
    std::array<VariableType, 3> params = { // 3 : 3 input parameters
        VariableType::NATIVE_POINTER(),
        VariableType::NATIVE_POINTER(),
        VariableType::JS_ANY(),
    };
    callSign->SetParameters(params.data());
    callSign->SetTargetKind(CallSignature::TargetKind::RUNTIME_STUB_NO_GC);
    callSign->SetCallConv(CallSignature::CallConv::GHCCallConv);
}

DEF_CALL_SIGNATURE(StringsAreEquals)
{
    // 2 : 2 input parameters
    CallSignature stringsAreEquals("StringsAreEquals", 0, 2,
        ArgumentsOrder::DEFAULT_ORDER, VariableType::BOOL());
    *callSign = stringsAreEquals;
    std::array<VariableType, 2> params = { // 2 : 2 input parameters
        VariableType::JS_POINTER(),
        VariableType::JS_POINTER(),
    };
    callSign->SetParameters(params.data());
    callSign->SetGCLeafFunction(true);
    callSign->SetTargetKind(CallSignature::TargetKind::RUNTIME_STUB_NO_GC);
}

DEF_CALL_SIGNATURE(BigIntEquals)
{
    // 2 : 2 input parameters
    CallSignature bigIntEquals("BigIntEquals", 0, 2,
        ArgumentsOrder::DEFAULT_ORDER, VariableType::BOOL());
    *callSign = bigIntEquals;
    std::array<VariableType, 2> params = { // 2 : 2 input parameters
        VariableType::JS_POINTER(),
        VariableType::JS_POINTER(),
    };
    callSign->SetParameters(params.data());
    callSign->SetGCLeafFunction(true);
    callSign->SetTargetKind(CallSignature::TargetKind::RUNTIME_STUB_NO_GC);
}

#define PUSH_CALL_ARGS_AND_DISPATCH_SIGNATURE_COMMON(name)                  \
    /* 1 : 1 input parameters */                                            \
    CallSignature signature(#name, 0, 1,                                    \
        ArgumentsOrder::DEFAULT_ORDER, VariableType::JS_ANY());             \
    *callSign = signature;                                                  \
    std::array<VariableType, 1> params = { /* 1: 1 input parameters */      \
        VariableType::NATIVE_POINTER(),                                     \
    };                                                                      \
    callSign->SetVariadicArgs(true);                                        \
    callSign->SetParameters(params.data());                                 \
    callSign->SetTargetKind(CallSignature::TargetKind::RUNTIME_STUB_NO_GC);

#define PUSH_CALL_ARGS_AND_DISPATCH_SIGNATURE(name)                      \
    PUSH_CALL_ARGS_AND_DISPATCH_SIGNATURE_COMMON(name)                   \
    callSign->SetCallConv(CallSignature::CallConv::GHCCallConv);

#define PUSH_CALL_ARGS_AND_DISPATCH_NATIVE_SIGNATURE(name)               \
    PUSH_CALL_ARGS_AND_DISPATCH_SIGNATURE_COMMON(name)                   \
    callSign->SetCallConv(CallSignature::CallConv::WebKitJSCallConv);

#define PUSH_CALL_ARGS_AND_DISPATCH_NATIVE_RANGE_SIGNATURE(name)         \
    PUSH_CALL_ARGS_AND_DISPATCH_SIGNATURE_COMMON(name)                   \
    callSign->SetCallConv(CallSignature::CallConv::CCallConv);

DEF_CALL_SIGNATURE(PushCallArgsAndDispatchNative)
{
    PUSH_CALL_ARGS_AND_DISPATCH_NATIVE_SIGNATURE(PushCallArgsAndDispatchNative)
}

DEF_CALL_SIGNATURE(PushCallArg0AndDispatch)
{
    PUSH_CALL_ARGS_AND_DISPATCH_SIGNATURE(PushCallArg0AndDispatch)
}

DEF_CALL_SIGNATURE(PushCallArg1AndDispatch)
{
    PUSH_CALL_ARGS_AND_DISPATCH_SIGNATURE(PushCallArg1AndDispatch)
}

DEF_CALL_SIGNATURE(PushCallArgs2AndDispatch)
{
    PUSH_CALL_ARGS_AND_DISPATCH_SIGNATURE(PushCallArgs2AndDispatch)
}

DEF_CALL_SIGNATURE(PushCallArgs3AndDispatch)
{
    PUSH_CALL_ARGS_AND_DISPATCH_SIGNATURE(PushCallArgs3AndDispatch)
}

DEF_CALL_SIGNATURE(PushCallThisArg0AndDispatch)
{
    PUSH_CALL_ARGS_AND_DISPATCH_SIGNATURE(PushCallThisArg0AndDispatch)
}

DEF_CALL_SIGNATURE(PushCallThisArg1AndDispatch)
{
    PUSH_CALL_ARGS_AND_DISPATCH_SIGNATURE(PushCallThisArg1AndDispatch)
}

DEF_CALL_SIGNATURE(PushCallThisArgs2AndDispatch)
{
    PUSH_CALL_ARGS_AND_DISPATCH_SIGNATURE(PushCallThisArgs2AndDispatch)
}

DEF_CALL_SIGNATURE(PushCallThisArgs3AndDispatch)
{
    PUSH_CALL_ARGS_AND_DISPATCH_SIGNATURE(PushCallThisArgs3AndDispatch)
}

DEF_CALL_SIGNATURE(PushCallRangeAndDispatchNative)
{
    PUSH_CALL_ARGS_AND_DISPATCH_NATIVE_RANGE_SIGNATURE(PushCallRangeAndDispatchNative)
}

DEF_CALL_SIGNATURE(PushCallNewAndDispatchNative)
{
    PUSH_CALL_ARGS_AND_DISPATCH_NATIVE_RANGE_SIGNATURE(PushCallNewAndDispatchNative)
}

DEF_CALL_SIGNATURE(PushCallNewAndDispatch)
{
    PUSH_CALL_ARGS_AND_DISPATCH_SIGNATURE(PushCallNewAndDispatch)
}

DEF_CALL_SIGNATURE(PushCallRangeAndDispatch)
{
    PUSH_CALL_ARGS_AND_DISPATCH_SIGNATURE(PushCallRangeAndDispatch)
}

DEF_CALL_SIGNATURE(PushCallThisRangeAndDispatch)
{
    PUSH_CALL_ARGS_AND_DISPATCH_SIGNATURE(PushCallThisRangeAndDispatch)
}

DEF_CALL_SIGNATURE(CallGetter)
{
    PUSH_CALL_ARGS_AND_DISPATCH_NATIVE_RANGE_SIGNATURE(CallGetter)
}

DEF_CALL_SIGNATURE(CallSetter)
{
    PUSH_CALL_ARGS_AND_DISPATCH_NATIVE_RANGE_SIGNATURE(CallSetter)
}

DEF_CALL_SIGNATURE(CallContainersArgs3)
{
    PUSH_CALL_ARGS_AND_DISPATCH_NATIVE_RANGE_SIGNATURE(CallContainersArgs3)
}

DEF_CALL_SIGNATURE(JSCallWithArgV)
{
    // 6 : 6 input parameters
    CallSignature jSCallWithArgV("JSCallWithArgV", 0, 6,
        ArgumentsOrder::DEFAULT_ORDER, VariableType::JS_ANY());
    *callSign = jSCallWithArgV;
    // 6 : 6 input parameters
    std::array<VariableType, 6> params = {
        VariableType::NATIVE_POINTER(),   // glue
        VariableType::INT64(),            // actualNumArgs
        VariableType::JS_ANY(),           // jsfunc
        VariableType::JS_ANY(),           // newTarget
        VariableType::JS_ANY(),           // this
        VariableType::NATIVE_POINTER(),   // argV
    };
    callSign->SetParameters(params.data());
    callSign->SetTargetKind(CallSignature::TargetKind::RUNTIME_STUB_NO_GC);
    callSign->SetCallConv(CallSignature::CallConv::CCallConv);
}

DEF_CALL_SIGNATURE(JSCallNewWithArgV)
{
    // 6 : 6 input parameters
    CallSignature jsCallNewWithArgV("JSCallNewWithArgV", 0, 6,
        ArgumentsOrder::DEFAULT_ORDER, VariableType::JS_ANY());
    *callSign = jsCallNewWithArgV;
    // 6 : 6 input parameters
    std::array<VariableType, 6> params = {
        VariableType::NATIVE_POINTER(),   // glue
        VariableType::INT64(),            // actualNumArgs
        VariableType::JS_ANY(),           // jsfunc
        VariableType::JS_ANY(),           // newTarget
        VariableType::JS_ANY(),           // this
        VariableType::NATIVE_POINTER(),   // argV
    };
    callSign->SetParameters(params.data());
    callSign->SetTargetKind(CallSignature::TargetKind::RUNTIME_STUB_NO_GC);
    callSign->SetCallConv(CallSignature::CallConv::CCallConv);
}

DEF_CALL_SIGNATURE(ConstructorJSCallWithArgV)
{
    // 6 : 6 input parameters
    CallSignature constructorJSCallWithArgV("ConstructorJSCallWithArgV", 0, 6,
        ArgumentsOrder::DEFAULT_ORDER, VariableType::JS_ANY());
    *callSign = constructorJSCallWithArgV;
    // 6 : 6 input parameters
    std::array<VariableType, 6> params = {
        VariableType::NATIVE_POINTER(),   // glue
        VariableType::INT64(),            // actualNumArgs
        VariableType::JS_ANY(),           // jsfunc
        VariableType::JS_ANY(),           // newTarget
        VariableType::JS_ANY(),           // this
        VariableType::NATIVE_POINTER(),   // argV
    };
    callSign->SetParameters(params.data());
    callSign->SetTargetKind(CallSignature::TargetKind::RUNTIME_STUB_NO_GC);
    callSign->SetCallConv(CallSignature::CallConv::CCallConv);
}

DEF_CALL_SIGNATURE(DebugPrint)
{
    // 1 : 1 input parameters
    CallSignature debugPrint("DebugPrint", 0, 1,
        ArgumentsOrder::DEFAULT_ORDER, VariableType::VOID());
    *callSign = debugPrint;
    // 1 : 1 input parameters
    std::array<VariableType, 1> params = {
        VariableType::INT32(),
    };
    callSign->SetVariadicArgs(true);
    callSign->SetParameters(params.data());
    callSign->SetGCLeafFunction(true);
    callSign->SetTargetKind(CallSignature::TargetKind::RUNTIME_STUB_NO_GC);
}

DEF_CALL_SIGNATURE(DebugPrintInstruction)
{
    // 2 : 2 input parameters
    CallSignature debugPrintInstruction("DebugPrintInstruction", 0, 2,
        ArgumentsOrder::DEFAULT_ORDER, VariableType::VOID());
    *callSign = debugPrintInstruction;
    // 2 : 2 input parameters
    std::array<VariableType, 2> params = {
        VariableType::NATIVE_POINTER(),
        VariableType::NATIVE_POINTER(),
    };
    callSign->SetVariadicArgs(true);
    callSign->SetParameters(params.data());
    callSign->SetGCLeafFunction(true);
    callSign->SetTargetKind(CallSignature::TargetKind::RUNTIME_STUB_NO_GC);
}

DEF_CALL_SIGNATURE(PGOProfiler)
{
    // 2 : 2 input parameters
    CallSignature pgoProfilerInstruction("PGOProfiler", 0, 2, ArgumentsOrder::DEFAULT_ORDER, VariableType::VOID());
    *callSign = pgoProfilerInstruction;
    // 2 : 2 input parameters
    std::array<VariableType, 2> params = {
        VariableType::NATIVE_POINTER(),
        VariableType::JS_ANY(),
    };
    callSign->SetVariadicArgs(true);
    callSign->SetParameters(params.data());
    callSign->SetGCLeafFunction(true);
    callSign->SetTargetKind(CallSignature::TargetKind::RUNTIME_STUB_NO_GC);
}

DEF_CALL_SIGNATURE(FatalPrint)
{
    // 1 : 1 input parameters
    CallSignature fatalPrint("FatalPrint", 0, 1,
        ArgumentsOrder::DEFAULT_ORDER, VariableType::VOID());
    *callSign = fatalPrint;
    // 1 : 1 input parameters
    std::array<VariableType, 1> params = {
        VariableType::INT32(),
    };
    callSign->SetVariadicArgs(true);
    callSign->SetParameters(params.data());
    callSign->SetGCLeafFunction(true);
    callSign->SetTargetKind(CallSignature::TargetKind::RUNTIME_STUB_NO_GC);
}

DEF_CALL_SIGNATURE(InsertOldToNewRSet)
{
    // 3 : 3 input parameters
    CallSignature index("InsertOldToNewRSet", 0, 3, ArgumentsOrder::DEFAULT_ORDER, VariableType::VOID());
    *callSign = index;
    // 3 : 3 input parameters
    std::array<VariableType, 3> params = {
        VariableType::NATIVE_POINTER(),
        VariableType::JS_POINTER(),
        VariableType::NATIVE_POINTER(),
    };
    callSign->SetParameters(params.data());
    callSign->SetGCLeafFunction(true);
    callSign->SetTargetKind(CallSignature::TargetKind::RUNTIME_STUB_NO_GC);
}

DEF_CALL_SIGNATURE(FloatMod)
{
    // 2 : 2 input parameters
    CallSignature index("FloatMod", 0, 2, ArgumentsOrder::DEFAULT_ORDER, VariableType::JS_ANY());
    *callSign = index;
    // 2 : 2 input parameters
    std::array<VariableType, 2> params = {
        VariableType::FLOAT64(),
        VariableType::FLOAT64(),
    };
    callSign->SetParameters(params.data());
    callSign->SetGCLeafFunction(true);
    callSign->SetTargetKind(CallSignature::TargetKind::RUNTIME_STUB_NO_GC);
}

DEF_CALL_SIGNATURE(FloatSqrt)
{
    // 1 : 1 input parameters
    CallSignature index("FloatSqrt", 0, 1, ArgumentsOrder::DEFAULT_ORDER, VariableType::JS_ANY());
    *callSign = index;
    // 1 : 1 input parameters
    std::array<VariableType, 1> params = {
        VariableType::FLOAT64(),
    };
    callSign->SetParameters(params.data());
    callSign->SetGCLeafFunction(true);
    callSign->SetTargetKind(CallSignature::TargetKind::RUNTIME_STUB_NO_GC);
}

DEF_CALL_SIGNATURE(FloatCos)
{
    // 1 : 1 input parameters
    CallSignature index("FloatCos", 0, 1, ArgumentsOrder::DEFAULT_ORDER, VariableType::JS_ANY());
    *callSign = index;
    // 1 : 1 input parameters
    std::array<VariableType, 1> params = {
        VariableType::FLOAT64(),
    };
    callSign->SetParameters(params.data());
    callSign->SetGCLeafFunction(true);
    callSign->SetTargetKind(CallSignature::TargetKind::RUNTIME_STUB_NO_GC);
}

DEF_CALL_SIGNATURE(FloatSin)
{
    // 1 : 1 input parameters
    CallSignature index("FloatSin", 0, 1, ArgumentsOrder::DEFAULT_ORDER, VariableType::JS_ANY());
    *callSign = index;
    // 1 : 1 input parameters
    std::array<VariableType, 1> params = {
        VariableType::FLOAT64(),
    };
    callSign->SetParameters(params.data());
    callSign->SetGCLeafFunction(true);
    callSign->SetTargetKind(CallSignature::TargetKind::RUNTIME_STUB_NO_GC);
}

DEF_CALL_SIGNATURE(FloatACos)
{
    // 1 : 1 input parameters
    CallSignature index("FloatACos", 0, 1, ArgumentsOrder::DEFAULT_ORDER, VariableType::JS_ANY());
    *callSign = index;
    // 1 : 1 input parameters
    std::array<VariableType, 1> params = {
        VariableType::FLOAT64(),
    };
    callSign->SetParameters(params.data());
    callSign->SetGCLeafFunction(true);
    callSign->SetTargetKind(CallSignature::TargetKind::RUNTIME_STUB_NO_GC);
}

DEF_CALL_SIGNATURE(FloatATan)
{
    // 1 : 1 input parameters
    CallSignature index("FloatATan", 0, 1, ArgumentsOrder::DEFAULT_ORDER, VariableType::JS_ANY());
    *callSign = index;
    // 1 : 1 input parameters
    std::array<VariableType, 1> params = {
        VariableType::FLOAT64(),
    };
    callSign->SetParameters(params.data());
    callSign->SetGCLeafFunction(true);
    callSign->SetTargetKind(CallSignature::TargetKind::RUNTIME_STUB_NO_GC);
}

DEF_CALL_SIGNATURE(FindElementWithCache)
{
    // 4 : 4 input parameters
    CallSignature index("FindElementWithCache", 0, 4, ArgumentsOrder::DEFAULT_ORDER, VariableType::INT32());
    *callSign = index;
    // 4 : 4 input parameters
    std::array<VariableType, 4> params = {
        VariableType::NATIVE_POINTER(),
        VariableType::JS_ANY(),
        VariableType::JS_ANY(),
        VariableType::INT32(),
    };
    callSign->SetParameters(params.data());
    callSign->SetGCLeafFunction(true);
    callSign->SetTargetKind(CallSignature::TargetKind::RUNTIME_STUB_NO_GC);
}

DEF_CALL_SIGNATURE(DoubleToInt)
{
    // 1 : 1 input parameters
    CallSignature index("DoubleToInt", 0, 1, ArgumentsOrder::DEFAULT_ORDER, VariableType::INT32());
    *callSign = index;
    // 1 : 1 input parameters
    std::array<VariableType, 1> params = {
        VariableType::FLOAT64(),
    };
    callSign->SetParameters(params.data());
    callSign->SetGCLeafFunction(true);
    callSign->SetTargetKind(CallSignature::TargetKind::RUNTIME_STUB_NO_GC);
}

DEF_CALL_SIGNATURE(MarkingBarrier)
{
    // 4 : 4 input parameters
    CallSignature index("MarkingBarrier", 0, 4, ArgumentsOrder::DEFAULT_ORDER, VariableType::VOID());
    *callSign = index;
    // 4 : 4 input parameters
    std::array<VariableType, 4> params = {
        VariableType::NATIVE_POINTER(),
        VariableType::JS_POINTER(),
        VariableType::NATIVE_POINTER(),
        VariableType::JS_POINTER()
    };
    callSign->SetParameters(params.data());
    callSign->SetGCLeafFunction(true);
    callSign->SetTargetKind(CallSignature::TargetKind::RUNTIME_STUB_NO_GC);
}

DEF_CALL_SIGNATURE(StoreBarrier)
{
    // 4 : 4 input parameters
    CallSignature index("StoreBarrier", 0, 4, ArgumentsOrder::DEFAULT_ORDER, VariableType::VOID());
    *callSign = index;
    // 4 : 4 input parameters
    std::array<VariableType, 4> params = {
        VariableType::NATIVE_POINTER(),
        VariableType::JS_POINTER(),
        VariableType::NATIVE_POINTER(),
        VariableType::JS_POINTER()
    };
    callSign->SetParameters(params.data());
    callSign->SetGCLeafFunction(true);
    callSign->SetTargetKind(CallSignature::TargetKind::RUNTIME_STUB_NO_GC);
}

DEF_CALL_SIGNATURE(CallArg0)
{
    // 2 : 2 input parameters
    CallSignature callArg0("callArg0", 0, 2,
        ArgumentsOrder::DEFAULT_ORDER, VariableType::JS_ANY());
    *callSign = callArg0;
    // 2 : 2 input parameters
    std::array<VariableType, 2> params = {
        VariableType::NATIVE_POINTER(),
        VariableType::JS_ANY()
    };
    callSign->SetParameters(params.data());
    callSign->SetTargetKind(CallSignature::TargetKind::RUNTIME_STUB);
}

DEF_CALL_SIGNATURE(CallArg1)
{
    // 3 : 3 input parameters
    CallSignature callArg1("callArg1", 0, 3,
        ArgumentsOrder::DEFAULT_ORDER, VariableType::JS_ANY());
    *callSign = callArg1;
    // 3 : 3 input parameters
    std::array<VariableType, 3> params = {
        VariableType::NATIVE_POINTER(),
        VariableType::JS_ANY(),
        VariableType::JS_ANY()
    };
    callSign->SetParameters(params.data());
    callSign->SetTargetKind(CallSignature::TargetKind::RUNTIME_STUB);
}

DEF_CALL_SIGNATURE(CallArgs2)
{
    // 4 : 4 input parameters
    CallSignature callArgs2("callArgs2", 0, 4,
        ArgumentsOrder::DEFAULT_ORDER, VariableType::JS_ANY());
    *callSign = callArgs2;
    // 4 : 4 input parameters
    std::array<VariableType, 4> params = {
        VariableType::NATIVE_POINTER(),
        VariableType::JS_ANY(),
        VariableType::JS_ANY(),
        VariableType::JS_ANY()
    };
    callSign->SetParameters(params.data());
    callSign->SetTargetKind(CallSignature::TargetKind::RUNTIME_STUB);
}

DEF_CALL_SIGNATURE(CallArgs3)
{
    // 5 : 5 input parameters
    CallSignature callArgs3("callArgs3", 0, 5,
        ArgumentsOrder::DEFAULT_ORDER, VariableType::JS_ANY());
    *callSign = callArgs3;
    // 5 : 5 input parameters
    std::array<VariableType, 5> params = {
        VariableType::NATIVE_POINTER(),
        VariableType::JS_ANY(),
        VariableType::JS_ANY(),
        VariableType::JS_ANY(),
        VariableType::JS_ANY()
    };
    callSign->SetParameters(params.data());
    callSign->SetTargetKind(CallSignature::TargetKind::RUNTIME_STUB);
}

DEF_CALL_SIGNATURE(CallThisRange)
{
    // 3 : 3 input parameters
    CallSignature callThisRange("callThisRange", 0, 3,
        ArgumentsOrder::DEFAULT_ORDER, VariableType::JS_ANY());
    *callSign = callThisRange;
    // 3 : 3 input parameters
    std::array<VariableType, 3> params = {
        VariableType::NATIVE_POINTER(),
        VariableType::JS_ANY(),
        VariableType::JS_ANY()
    };
    callSign->SetVariadicArgs(true);
    callSign->SetParameters(params.data());
    callSign->SetTargetKind(CallSignature::TargetKind::RUNTIME_STUB);
}

DEF_CALL_SIGNATURE(CallRange)
{
    // 2 : 2 input parameters
    CallSignature callRange("callRange", 0, 2,
        ArgumentsOrder::DEFAULT_ORDER, VariableType::JS_ANY());
    *callSign = callRange;
    // 2 : 2 input parameters
    std::array<VariableType, 2> params = {
        VariableType::NATIVE_POINTER(),
        VariableType::JS_ANY()
    };
    callSign->SetVariadicArgs(true);
    callSign->SetParameters(params.data());
    callSign->SetTargetKind(CallSignature::TargetKind::RUNTIME_STUB);
}

DEF_CALL_SIGNATURE(JsProxyCallInternal)
{
    // 4 : 4 input parameters
    CallSignature proxyCallInternal("JsProxyCallInternal", 0, 4,
        ArgumentsOrder::DEFAULT_ORDER, VariableType::JS_POINTER());
    *callSign = proxyCallInternal;
    // 4 : 4 input parameters
    std::array<VariableType, 4> params = {
        VariableType::NATIVE_POINTER(),    // glue
        VariableType::INT64(),      // actual argC
        VariableType::JS_POINTER(), // callTarget
        VariableType::NATIVE_POINTER(),    // argv
    };
    callSign->SetVariadicArgs(false);
    callSign->SetParameters(params.data());
    callSign->SetTailCall(true);
    callSign->SetGCLeafFunction(true);
    callSign->SetTargetKind(CallSignature::TargetKind::COMMON_STUB);
    callSign->SetCallConv(CallSignature::CallConv::CCallConv);
}

DEF_CALL_SIGNATURE(CreateArrayFromList)
{
    // 3 : 3 input parameters
    CallSignature createArrayFromList("CreateArrayFromList", 0, 3, ArgumentsOrder::DEFAULT_ORDER,
                                     VariableType::JS_POINTER());
    *callSign = createArrayFromList;
    // 3 : 3 input parameters
    std::array<VariableType, 3> params = {
        VariableType::NATIVE_POINTER(),
        VariableType::INT32(),
        VariableType::NATIVE_POINTER(),
    };

    callSign->SetParameters(params.data());
    callSign->SetTargetKind(CallSignature::TargetKind::RUNTIME_STUB_VARARGS);
}

DEF_CALL_SIGNATURE(DeoptHandlerAsm)
{
    // 1 : 1 input parameters
    CallSignature deoptHandlerAsm("DeoptHandlerAsm", 0, 1,
        ArgumentsOrder::DEFAULT_ORDER, VariableType::JS_ANY());
    *callSign = deoptHandlerAsm;
    std::array<VariableType, 1> params = { // 1 : 1 input parameters
        VariableType::NATIVE_POINTER(),     // glue
    };
    callSign->SetVariadicArgs(false);
    callSign->SetParameters(params.data());
    callSign->SetCallConv(CallSignature::CallConv::CCallConv);
    callSign->SetTargetKind(CallSignature::TargetKind::DEOPT_STUB);
}
}  // namespace panda::ecmascript::kungfu
