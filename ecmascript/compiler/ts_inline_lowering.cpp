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
#include "ecmascript/compiler/bytecode_circuit_builder.h"
#include "ecmascript/compiler/bytecodes.h"
#include "ecmascript/compiler/compiler_log.h"
#include "ecmascript/compiler/pass.h"
#include "ecmascript/compiler/ts_inline_lowering.h"
#include "ecmascript/ts_types/ts_manager.h"
#include "ecmascript/ts_types/ts_type.h"
#include "libpandabase/utils/utf.h"
#include "libpandafile/class_data_accessor-inl.h"

namespace panda::ecmascript::kungfu {
void TSInlineLowering::RunTSInlineLowering()
{
    std::vector<GateRef> gateList;
    circuit_->GetAllGates(gateList);
    for (const auto &gate : gateList) {
        auto op = acc_.GetOpCode(gate);
        if (op == OpCode::JS_BYTECODE) {
            TryInline(gate);
        }
    }
}

void TSInlineLowering::TryInline(GateRef gate)
{
    EcmaOpcode ecmaOpcode = acc_.GetByteCodeOpcode(gate);
    switch (ecmaOpcode) {
        case EcmaOpcode::CALLARG0_IMM8:
        case EcmaOpcode::CALLARG1_IMM8_V8:
        case EcmaOpcode::CALLARGS2_IMM8_V8_V8:
        case EcmaOpcode::CALLARGS3_IMM8_V8_V8_V8:
        case EcmaOpcode::CALLRANGE_IMM8_IMM8_V8:
        case EcmaOpcode::WIDE_CALLRANGE_PREF_IMM16_V8:
            TryInline(gate, false);
            break;
        case EcmaOpcode::CALLTHIS0_IMM8_V8:
        case EcmaOpcode::CALLTHIS1_IMM8_V8_V8:
        case EcmaOpcode::CALLTHIS2_IMM8_V8_V8_V8:
        case EcmaOpcode::CALLTHIS3_IMM8_V8_V8_V8_V8:
        case EcmaOpcode::CALLTHISRANGE_IMM8_IMM8_V8:
        case EcmaOpcode::WIDE_CALLTHISRANGE_PREF_IMM16_V8:
            TryInline(gate, true);
            break;
        default:
            break;
    }
}

void TSInlineLowering::TryInline(GateRef gate, bool isCallThis)
{
    // first elem is function in old isa
    size_t funcIndex = acc_.GetNumValueIn(gate) - 1;
    auto funcType = acc_.GetGateType(acc_.GetValueIn(gate, funcIndex));
    MethodLiteral* inlinedMethod = nullptr;
    if (tsManager_->IsFunctionTypeKind(funcType)) {
        GlobalTSTypeRef gt = funcType.GetGTRef();
        auto methodOffset = tsManager_->GetFuncMethodOffset(gt);
        if (tsManager_->IsSkippedMethod(methodOffset)) {
            return;
        }
        inlinedMethod = info_->jsPandaFile->FindMethodLiteral(methodOffset);
        auto &bytecodeInfo = info_->bcInfoCollector->GetBytecodeInfo();
        auto &methodInfo = bytecodeInfo.methodList.at(methodOffset);
        auto &methodPcInfo = bytecodeInfo.methodPcInfos[methodInfo.methodPcInfoIndex];
        if (methodPcInfo.pcOffsets.size() <= MAX_INLINE_BYTECODE_COUNT &&
            inlinedCall_ <= MAX_INLINE_CALL_ALLOWED) {
            auto success = FilterInlinedMethod(inlinedMethod, methodPcInfo.pcOffsets);
            if (success) {
                circuit_->PushFrameState();
                InlineCall(methodInfo, methodPcInfo, inlinedMethod);
                ReplaceCallInput(gate, isCallThis);
                circuit_->PopFrameState();
                inlinedCall_++;
            }
        }
    }

    if ((inlinedMethod != nullptr) && IsLogEnabled()) {
        auto jsPandaFile = info_->jsPandaFile;
        const std::string methodName(
            MethodLiteral::GetMethodName(jsPandaFile, inlinedMethod->GetMethodId()));
        std::string fileName = jsPandaFile->GetFileName();
        std::string fullName = methodName + "@" + fileName;
        LOG_COMPILER(INFO) << "";
        LOG_COMPILER(INFO) << "\033[34m"
                           << "===================="
                           << " After inlining "
                           << "[" << fullName << "]"
                           << "===================="
                           << "\033[0m";
        circuit_->PrintAllGatesWithBytecode();
        LOG_COMPILER(INFO) << "\033[34m" << "========================= End ==========================" << "\033[0m";
    }
}

bool TSInlineLowering::FilterInlinedMethod(MethodLiteral* method, std::vector<const uint8_t*> pcOffsets)
{
    auto jsPandaFile = info_->jsPandaFile;
    auto pf = jsPandaFile->GetPandaFile();
    panda_file::MethodDataAccessor mda(*pf, method->GetMethodId());
    panda_file::CodeDataAccessor cda(*pf, mda.GetCodeId().value());
    if (cda.GetTriesSize() != 0) {
        return false;
    }
    for (size_t i = 0; i < pcOffsets.size(); i++) {
        auto pc = pcOffsets[i];
        auto ecmaOpcode = info_->bytecodes->GetOpcode(pc);
        switch (ecmaOpcode) {
            case EcmaOpcode::GETUNMAPPEDARGS:
            case EcmaOpcode::SUSPENDGENERATOR_V8:
            case EcmaOpcode::RESUMEGENERATOR:
            case EcmaOpcode::COPYRESTARGS_IMM8:
            case EcmaOpcode::WIDE_COPYRESTARGS_PREF_IMM16:
            case EcmaOpcode::CREATEASYNCGENERATOROBJ_V8:
                return false;
            default:
                break;
        }
    }
    return true;
}

CString TSInlineLowering::GetRecordName(const JSPandaFile *jsPandaFile,
    panda_file::File::EntityId methodIndex)
{
    const panda_file::File *pf = jsPandaFile->GetPandaFile();
    panda_file::MethodDataAccessor patchMda(*pf, methodIndex);
    panda_file::ClassDataAccessor patchCda(*pf, patchMda.GetClassId());
    CString desc = utf::Mutf8AsCString(patchCda.GetDescriptor());
    return jsPandaFile->ParseEntryPoint(desc);
}

void TSInlineLowering::InlineCall(MethodInfo &methodInfo, MethodPcInfo &methodPCInfo, MethodLiteral* method)
{
    auto jsPandaFile = info_->jsPandaFile;
    auto cmpCfg = info_->cmpCfg;
    auto tsManager = info_->tsManager;
    auto log = info_->log;
    auto recordName = GetRecordName(jsPandaFile, method->GetMethodId());
    bool hasTyps = jsPandaFile->HasTSTypes(recordName);
    if (!hasTyps) {
        return;
    }
    const std::string methodName(MethodLiteral::GetMethodName(jsPandaFile, method->GetMethodId()));
    std::string fileName = jsPandaFile->GetFileName();
    std::string fullName = methodName + "@" + fileName;

    BytecodeCircuitBuilder builder(jsPandaFile, method, methodPCInfo,
                                   tsManager, circuit_,
                                   info_->bytecodes, true, IsLogEnabled(),
                                   hasTyps, fullName, recordName);
    {
        TimeScope timeScope("BytecodeToCircuit", methodName, method->GetMethodId().GetOffset(), log);
        builder.BytecodeToCircuit();
    }

    PassData data(circuit_, log, fullName, method->GetMethodId().GetOffset());
    PassRunner<PassData> pipeline(&data);
    pipeline.RunPass<TypeInferPass>(&builder, info_, methodInfo.methodInfoIndex, hasTyps);
    pipeline.RunPass<AsyncFunctionLoweringPass>(&builder, cmpCfg);
}

void TSInlineLowering::ReplaceCallInput(GateRef gate, bool isCallThis)
{
    std::vector<GateRef> vec;
    GateRef glue = acc_.GetGlueFromArgList();
    size_t numIns = acc_.GetNumValueIn(gate);
    // 1: last one elem is function
    GateRef callTarget = acc_.GetValueIn(gate, numIns - 1);
    GateRef thisObj = Circuit::NullGate();
    size_t fixedInputsNum = 0;
    if (isCallThis) {
        fixedInputsNum = 1;
        thisObj = acc_.GetValueIn(gate, 0);
    } else {
        thisObj = builder_.Undefined();
    }
    // -1: callTarget
    size_t actualArgc = numIns + NUM_MANDATORY_JSFUNC_ARGS - fixedInputsNum;
    vec.emplace_back(glue); // glue
    vec.emplace_back(builder_.Undefined()); // env
    vec.emplace_back(builder_.Int64(actualArgc)); // argc
    vec.emplace_back(callTarget);
    vec.emplace_back(builder_.Undefined()); // newTarget
    vec.emplace_back(thisObj);
    // -1: call Target
    for (size_t i = fixedInputsNum; i < numIns - 1; i++) {
        vec.emplace_back(acc_.GetValueIn(gate, i));
    }
    LowerToInlineCall(gate, vec);
}

GateRef TSInlineLowering::MergeAllReturn(const std::vector<GateRef> &returnVector,
    GateRef &state, GateRef &depend)
{
    size_t numOfIns = returnVector.size();
    auto stateList = std::vector<GateRef>(numOfIns, Circuit::NullGate());
    auto dependList = std::vector<GateRef>(numOfIns + 1, Circuit::NullGate());
    auto vaueList = std::vector<GateRef>(numOfIns + 1, Circuit::NullGate());

    dependList[0] = state;
    vaueList[0] = state;
    for (size_t i = 0; i < numOfIns; i++) {
        GateRef returnGate = returnVector.at(i);
        stateList[i] = acc_.GetState(returnGate);
        dependList[i + 1] = acc_.GetDep(returnGate);
        vaueList[i + 1] = acc_.GetValueIn(returnGate, 0);
        acc_.DeleteGate(returnGate);
    }

    state = circuit_->NewGate(OpCode(OpCode::MERGE), numOfIns,
                              stateList, GateType::Empty());
    depend = circuit_->NewGate(OpCode(OpCode::DEPEND_SELECTOR), numOfIns,
                               dependList, GateType::Empty());
    return circuit_->NewGate(OpCode(OpCode::VALUE_SELECTOR), MachineType::I64, numOfIns,
                             vaueList, GateType::AnyType());
}

void TSInlineLowering::ReplaceEntryGate(GateRef callGate)
{
    auto stateEntry = Circuit::GetCircuitRoot(OpCode(OpCode::STATE_ENTRY));
    auto dependEntry = Circuit::GetCircuitRoot(OpCode(OpCode::DEPEND_ENTRY));

    GateRef callState = acc_.GetState(callGate);
    GateRef callDepend = acc_.GetDep(callGate);
    auto stateUse = acc_.Uses(stateEntry).begin();
    acc_.ReplaceIn(stateUse, callState);
    auto dependUse = acc_.Uses(dependEntry).begin();
    acc_.ReplaceIn(dependUse, callDepend);
}

void TSInlineLowering::ReplaceReturnGate(GateRef callGate)
{
    std::vector<GateRef> returnVector;
    acc_.GetReturnOutVector(returnVector);

    GateRef value = Circuit::NullGate();
    GateRef state = Circuit::NullGate();
    GateRef depend = Circuit::NullGate();
    // 2: if success and if exception
    if (returnVector.size() == 2) {
        for (size_t i = 0; i < returnVector.size(); i++) {
            GateRef returnGate = returnVector.at(i);
            GateRef returnState = acc_.GetState(returnGate);
            if (acc_.GetOpCode(returnState) == OpCode::IF_EXCEPTION) {
                continue;
            }
            ASSERT(acc_.GetOpCode(returnState) == OpCode::IF_SUCCESS ||
                   acc_.GetOpCode(returnState) == OpCode::MERGE);
            depend = acc_.GetDep(returnGate);
            state = returnState;
            value = acc_.GetValueIn(returnGate, 0);
            acc_.DeleteGate(returnGate);
            break;
        }
    } else {
        value = MergeAllReturn(returnVector, state, depend);
    }
    ReplaceHirAndDeleteState(callGate, state, depend, value);
}

void TSInlineLowering::ReplaceHirAndDeleteState(GateRef gate, GateRef state, GateRef depend, GateRef value)
{
    auto uses = acc_.Uses(gate);
    for (auto useIt = uses.begin(); useIt != uses.end();) {
        const OpCode op = acc_.GetOpCode(*useIt);
        if (op == OpCode::IF_SUCCESS) {
            auto firstUse = acc_.Uses(*useIt).begin();
            acc_.ReplaceIn(*firstUse, firstUse.GetIndex(), state);
            useIt = acc_.DeleteGate(useIt);
        } else if (op == OpCode::IF_EXCEPTION) {
            auto firstUse = acc_.Uses(*useIt).begin();
            acc_.DeleteGate(firstUse);
            useIt = acc_.DeleteGate(useIt);
        } else if (acc_.IsDependIn(useIt)) {
            useIt = acc_.ReplaceIn(useIt, depend);
        } else if (acc_.IsValueIn(useIt)) {
            useIt = acc_.ReplaceIn(useIt, value);
        } else {
            UNREACHABLE();
        }
    }
    acc_.DeleteGate(gate);
}

void TSInlineLowering::LowerToInlineCall(GateRef callGate, const std::vector<GateRef> &args)
{
    // replace in value/args
    ArgumentAccessor argAcc(circuit_);
    ASSERT(argAcc.ArgsCount() == args.size());
    for (size_t i = 0; i < argAcc.ArgsCount(); i++) {
        GateRef arg = argAcc.ArgsAt(i);
        acc_.UpdateAllUses(arg, args.at(i));
        acc_.DeleteGate(arg);
    }
    // replace in depend and state
    ReplaceEntryGate(callGate);
    // replace use gate
    ReplaceReturnGate(callGate);
}
}  // namespace panda::ecmascript