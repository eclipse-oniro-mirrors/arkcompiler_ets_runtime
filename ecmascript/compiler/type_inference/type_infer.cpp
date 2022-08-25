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

#include "ecmascript/compiler/type_inference/type_infer.h"
#include "ecmascript/jspandafile/js_pandafile_manager.h"

namespace panda::ecmascript::kungfu {
void TypeInfer::TraverseCircuit()
{
    size_t gateCount = circuit_->GetGateCount();
    std::vector<bool> inQueue(gateCount, true);
    std::vector<bool> visited(gateCount, false);
    std::queue<GateRef> pendingQueue;
    std::vector<GateRef> gateList;
    circuit_->GetAllGates(gateList);
    for (auto gate : gateList) {
        pendingQueue.push(gate);
    }

    while (!pendingQueue.empty()) {
        auto curGate = pendingQueue.front();
        inQueue[gateAccessor_.GetId(curGate)] = false;
        pendingQueue.pop();
        auto uses = gateAccessor_.ConstUses(curGate);
        for (auto useIt = uses.begin(); useIt != uses.end(); useIt++) {
            auto gateId = gateAccessor_.GetId(*useIt);
            if (Infer(*useIt) && !inQueue[gateId]) {
                inQueue[gateId] = true;
                pendingQueue.push(*useIt);
            }
        }
    }

    if (IsLogEnabled()) {
        LOG_COMPILER(INFO) << "TypeInfer:======================================================";
        circuit_->PrintAllGates(*builder_);
    }

    if (tsManager_->AssertTypes()) {
        Verify();
    }

    if (tsManager_->PrintAnyTypes()) {
        TypeFilter filter(this);
        filter.Run();
    }
}

bool TypeInfer::UpdateType(GateRef gate, const GateType type)
{
    auto preType = gateAccessor_.GetGateType(gate);
    if (type.IsTSType() && !type.IsAnyType() && type != preType) {
        gateAccessor_.SetGateType(gate, type);
        return true;
    }
    return false;
}

bool TypeInfer::UpdateType(GateRef gate, const GlobalTSTypeRef &typeRef)
{
    auto type = GateType(typeRef);
    return UpdateType(gate, type);
}

bool TypeInfer::ShouldInfer(const GateRef gate) const
{
    auto op = gateAccessor_.GetOpCode(gate);
    // handle phi gates
    if (op == OpCode::VALUE_SELECTOR) {
        return true;
    }
    if (op == OpCode::JS_BYTECODE ||
        op == OpCode::CONSTANT ||
        op == OpCode::RETURN) {
        const auto &gateToBytecode = builder_->GetGateToBytecode();
        // handle gates generated by ecma.* bytecodes (not including Jump)
        if (gateToBytecode.find(gate) != gateToBytecode.end()) {
            return !builder_->GetByteCodeInfo(gate).IsJump();
        }
    }
    return false;
}

bool TypeInfer::Infer(GateRef gate)
{
    // TODO
    if (!ShouldInfer(gate)) {
        return false;
    }
    if (gateAccessor_.GetOpCode(gate) == OpCode::VALUE_SELECTOR) {
        return InferPhiGate(gate);
    }
    /*
    // infer ecma.* bytecode gates
    EcmaOpcode op = builder_->GetByteCodeOpcode(gate);
    switch (op) {
        case EcmaOpcode::LDNAN:
        case EcmaOpcode::LDINFINITY:
        case EcmaOpcode::SUB2_IMM8_V8:
        case EcmaOpcode::MUL2_IMM8_V8:
        case EcmaOpcode::DIV2_IMM8_V8:
        case EcmaOpcode::MOD2_IMM8_V8:
        case EcmaOpcode::SHL2_IMM8_V8:
        case EcmaOpcode::ASHR2_IMM8_V8:
        case EcmaOpcode::SHR2_IMM8_V8:
        case EcmaOpcode::AND2_IMM8_V8:
        case EcmaOpcode::OR2_IMM8_V8:
        case EcmaOpcode::XOR2_IMM8_V8:
        case EcmaOpcode::TONUMBER_IMM8:
        case EcmaOpcode::TONUMERIC_IMM8:
        case EcmaOpcode::NEG_IMM8:
        case EcmaOpcode::NOT_IMM8:
        case EcmaOpcode::INC_IMM8:
        case EcmaOpcode::DEC_IMM8:
        case EcmaOpcode::EXP_IMM8_V8:
        case EcmaOpcode::STARRAYSPREAD_V8_V8:
            return SetNumberType(gate);
        case EcmaOpcode::LDTRUE:
        case EcmaOpcode::LDFALSE:
        case EcmaOpcode::EQ_IMM8_V8:
        case EcmaOpcode::NOTEQ_IMM8_V8:
        case EcmaOpcode::LESS_IMM8_V8:
        case EcmaOpcode::LESSEQ_IMM8_V8:
        case EcmaOpcode::GREATER_IMM8_V8:
        case EcmaOpcode::GREATEREQ_IMM8_V8:
        case EcmaOpcode::ISIN_IMM8_V8:
        case EcmaOpcode::INSTANCEOF_IMM8_V8:
        case EcmaOpcode::STRICTNOTEQ_IMM8_V8:
        case EcmaOpcode::STRICTEQ_IMM8_V8:
        case EcmaOpcode::ISTRUE:
        case EcmaOpcode::ISFALSE:
        case EcmaOpcode::SETOBJECTWITHPROTO_IMM8_V8:
        case EcmaOpcode::DELOBJPROP_V8:
            return SetBooleanType(gate);
        case EcmaOpcode::LDUNDEFINED:
            return InferLdUndefined(gate);
        case EcmaOpcode::LDNULL:
            return InferLdNull(gate);
        case EcmaOpcode::LDAI_IMM32:
            return InferLdai(gate);
        case EcmaOpcode::FLDAI_IMM64:
            return InferFLdai(gate);
        case EcmaOpcode::LDSYMBOL:
            return InferLdSymbol(gate);
        case EcmaOpcode::THROW:
            return InferThrow(gate);
        case EcmaOpcode::TYPEOF_IMM8:
            return InferTypeOf(gate);
        case EcmaOpcode::ADD2_IMM8_V8:
            return InferAdd2(gate);
        case EcmaOpcode::LDOBJBYINDEX_IMM8_IMM16:
        case EcmaOpcode::LDOBJBYINDEX_IMM16_IMM16:
        case EcmaOpcode::WIDE_LDOBJBYINDEX_PREF_IMM32:
        case EcmaOpcode::DEPRECATED_LDOBJBYINDEX_PREF_V8_IMM32:
            return InferLdObjByIndex(gate);
        case EcmaOpcode::STGLOBALVAR_IMM16_ID16:
        case EcmaOpcode::STCONSTTOGLOBALRECORD_ID16:
        case EcmaOpcode::TRYSTGLOBALBYNAME_IMM8_ID16:
        case EcmaOpcode::STLETTOGLOBALRECORD_ID16:
        case EcmaOpcode::STCLASSTOGLOBALRECORD_ID16:
            return SetStGlobalBcType(gate);
        case EcmaOpcode::LDGLOBALVAR_IMM16_ID16:
            return InferLdGlobalVar(gate);
        case EcmaOpcode::RETURNUNDEFINED:
            return InferReturnUndefined(gate);
        case EcmaOpcode::RETURN:
            return InferReturn(gate);
        case EcmaOpcode::LDOBJBYNAME_IMM8_ID16:
            return InferLdObjByName(gate);
        case EcmaOpcode::LDA_STR_ID16:
            return InferLdStr(gate);
        case EcmaOpcode::CALLARG0_IMM8_V8:
        case EcmaOpcode::CALLARG1_IMM8_V8_V8:
        case EcmaOpcode::CALLARGS2_IMM8_V8_V8_V8:
        case EcmaOpcode::CALLARGS3_IMM8_V8_V8_V8_V8:
        case EcmaOpcode::CALLSPREAD_IMM8_V8_V8_V8:
        case EcmaOpcode::CALLRANGE_IMM8_IMM16_V8:
        case EcmaOpcode::CALLTHISRANGE_IMM8_IMM16_V8:
            return InferCallFunction(gate);
        case EcmaOpcode::LDOBJBYVALUE_IMM8_V8_V8:
            return InferLdObjByValue(gate);
        case EcmaOpcode::GETNEXTPROPNAME_V8:
            return InferGetNextPropName(gate);
        case EcmaOpcode::DEFINEGETTERSETTERBYVALUE_V8_V8_V8_V8:
            return InferDefineGetterSetterByValue(gate);
        case EcmaOpcode::NEWOBJDYNRANGE_PREF_IMM16_V8:
        case EcmaOpcode::NEWOBJSPREADDYN_PREF_V8_V8:
            return InferNewObject(gate);
        case EcmaOpcode::SUPERCALL_PREF_IMM16_V8:
        case EcmaOpcode::SUPERCALLSPREAD_PREF_V8:
            return InferSuperCall(gate);
        case EcmaOpcode::TRYLDGLOBALBYNAME_IMM8_ID16:
            return InferTryLdGlobalByName(gate);
        default:
            break;
    }
    */
    return false;
}

bool TypeInfer::InferPhiGate(GateRef gate)
{
    ASSERT(gateAccessor_.GetOpCode(gate) == OpCode::VALUE_SELECTOR);
    CVector<GlobalTSTypeRef> typeList;
    auto ins = gateAccessor_.ConstIns(gate);
    for (auto it =  ins.begin(); it != ins.end(); it++) {
        // assuming that VALUE_SELECTOR is NO_DEPEND and NO_ROOT
        if (gateAccessor_.GetOpCode(*it) == OpCode::MERGE ||
            gateAccessor_.GetOpCode(*it) == OpCode::LOOP_BEGIN) {
            continue;
        }
        auto valueInType = gateAccessor_.GetGateType(*it);
        if (valueInType.IsAnyType()) {
            return UpdateType(gate, valueInType);
        }
        typeList.emplace_back(GlobalTSTypeRef(valueInType.GetType()));
    }
    // deduplicate
    auto deduplicateIndex = std::unique(typeList.begin(), typeList.end());
    typeList.erase(deduplicateIndex, typeList.end());
    if (typeList.size() > 1) {
        auto unionType = tsManager_->GetOrCreateUnionType(typeList);
        return UpdateType(gate, unionType);
    }
    auto type = typeList.at(0);
    return UpdateType(gate, type);
}

bool TypeInfer::SetNumberType(GateRef gate)
{
    auto numberType = GateType::NumberType();
    return UpdateType(gate, numberType);
}

bool TypeInfer::SetBooleanType(GateRef gate)
{
    auto booleanType = GateType::BooleanType();
    return UpdateType(gate, booleanType);
}

bool TypeInfer::InferLdUndefined(GateRef gate)
{
    auto undefinedType = GateType::UndefinedType();
    return UpdateType(gate, undefinedType);
}

bool TypeInfer::InferLdNull(GateRef gate)
{
    auto nullType = GateType::NullType();
    return UpdateType(gate, nullType);
}

bool TypeInfer::InferLdai(GateRef gate)
{
    auto numberType = GateType::NumberType();
    return UpdateType(gate, numberType);
}

bool TypeInfer::InferFLdai(GateRef gate)
{
    auto numberType = GateType::NumberType();
    return UpdateType(gate, numberType);
}

bool TypeInfer::InferLdSymbol(GateRef gate)
{
    auto symbolType = GateType::SymbolType();
    return UpdateType(gate, symbolType);
}

bool TypeInfer::InferThrow(GateRef gate)
{
    ASSERT(gateAccessor_.GetNumValueIn(gate) == 1);
    auto gateType = gateAccessor_.GetGateType(gateAccessor_.GetValueIn(gate, 0));
    return UpdateType(gate, gateType);
}

bool TypeInfer::InferTypeOf(GateRef gate)
{
    ASSERT(gateAccessor_.GetNumValueIn(gate) == 1);
    auto gateType = gateAccessor_.GetGateType(gateAccessor_.GetValueIn(gate, 0));
    return UpdateType(gate, gateType);
}

bool TypeInfer::InferAdd2(GateRef gate)
{
    // 2: number of value inputs
    ASSERT(gateAccessor_.GetNumValueIn(gate) == 2);
    auto firInType = gateAccessor_.GetGateType(gateAccessor_.GetValueIn(gate, 0));
    auto secInType = gateAccessor_.GetGateType(gateAccessor_.GetValueIn(gate, 1));
    if (firInType.IsStringType() || secInType.IsStringType()) {
        return UpdateType(gate, GateType::StringType());
    }
    if (firInType.IsNumberType() && secInType.IsNumberType()) {
        return UpdateType(gate, GateType::NumberType());
    }
    return UpdateType(gate, GateType::AnyType());
}

bool TypeInfer::InferLdObjByIndex(GateRef gate)
{
    // 2: number of value inputs
    ASSERT(gateAccessor_.GetNumValueIn(gate) == 2);
    auto inValueType = gateAccessor_.GetGateType(gateAccessor_.GetValueIn(gate, 0));
    if (tsManager_->IsArrayTypeKind(inValueType)) {
        auto type = tsManager_->GetArrayParameterTypeGT(inValueType);
        return UpdateType(gate, type);
    }
    return false;
}

bool TypeInfer::SetStGlobalBcType(GateRef gate)
{
    auto byteCodeInfo = builder_->GetByteCodeInfo(gate);
    ASSERT(byteCodeInfo.inputs.size() == 1);
    auto stringId = std::get<StringId>(byteCodeInfo.inputs[0]).GetId();
    // 2: number of value inputs
    ASSERT(gateAccessor_.GetNumValueIn(gate) == 2);
    auto inValueType = gateAccessor_.GetGateType(gateAccessor_.GetValueIn(gate, 1));
    if (stringIdToGateType_.find(stringId) != stringIdToGateType_.end()) {
        stringIdToGateType_[stringId] = inValueType;
    } else {
        stringIdToGateType_.insert(std::pair<uint32_t, GateType>(stringId, inValueType));
    }
    return UpdateType(gate, inValueType);
}

bool TypeInfer::InferLdGlobalVar(GateRef gate)
{
    auto byteCodeInfo = builder_->GetByteCodeInfo(gate);
    ASSERT(byteCodeInfo.inputs.size() == 1);
    auto stringId = std::get<StringId>(byteCodeInfo.inputs[0]).GetId();
    auto iter = stringIdToGateType_.find(stringId);
    if (iter != stringIdToGateType_.end()) {
        return UpdateType(gate, iter->second);
    }
    return false;
}

bool TypeInfer::InferReturnUndefined(GateRef gate)
{
    auto undefinedType = GateType::UndefinedType();
    return UpdateType(gate, undefinedType);
}

bool TypeInfer::InferReturn(GateRef gate)
{
    ASSERT(gateAccessor_.GetNumValueIn(gate) == 1);
    auto gateType = gateAccessor_.GetGateType(gateAccessor_.GetValueIn(gate, 0));
    return UpdateType(gate, gateType);
}

bool TypeInfer::InferLdObjByName(GateRef gate)
{
    // 2: number of value inputs
    ASSERT(gateAccessor_.GetNumValueIn(gate) == 2);
    auto objType = gateAccessor_.GetGateType(gateAccessor_.GetValueIn(gate, 1));
    if (objType.IsTSType()) {
        // If this object has no gt type, we cannot get its internal property type
        if (IsObjectOrHClass(objType)) {
            auto index = gateAccessor_.GetBitField(gateAccessor_.GetValueIn(gate, 0));
            auto name = tsManager_->GetStringById(index);
            auto type = GetPropType(objType, name);
            return UpdateType(gate, type);
        }
    }
    return false;
}

bool TypeInfer::InferNewObject(GateRef gate)
{
    if (gateAccessor_.GetGateType(gate).IsAnyType()) {
        ASSERT(gateAccessor_.GetNumValueIn(gate) > 0);
        auto classType = gateAccessor_.GetGateType(gateAccessor_.GetValueIn(gate, 0));
        if (tsManager_->IsClassTypeKind(classType)) {
            auto classInstanceType = tsManager_->CreateClassInstanceType(classType);
            return UpdateType(gate, classInstanceType);
        }
    }
    return false;
}

bool TypeInfer::InferLdStr(GateRef gate)
{
    auto stringType = GateType::StringType();
    return UpdateType(gate, stringType);
}

bool TypeInfer::InferCallFunction(GateRef gate)
{
    // 0 : the index of function
    auto funcType = gateAccessor_.GetGateType(gateAccessor_.GetValueIn(gate, 0));
    if (funcType.IsTSType() && tsManager_->IsFunctionTypeKind(funcType)) {
        auto returnType = tsManager_->GetFuncReturnValueTypeGT(funcType);
        return UpdateType(gate, returnType);
    }
    return false;
}

bool TypeInfer::InferLdObjByValue(GateRef gate)
{
    auto objType = gateAccessor_.GetGateType(gateAccessor_.GetValueIn(gate, 0));
    if (objType.IsTSType()) {
        // handle array
        if (tsManager_->IsArrayTypeKind(objType)) {
            auto elementType = tsManager_->GetArrayParameterTypeGT(objType);
            return UpdateType(gate, elementType);
        }
        // handle object
        if (IsObjectOrHClass(objType)) {
            auto valueGate = gateAccessor_.GetValueIn(gate, 1);
            if (gateAccessor_.GetOpCode(valueGate) == OpCode::CONSTANT) {
                auto value = gateAccessor_.GetBitField(valueGate);
                auto type = GetPropType(objType, value);
                return UpdateType(gate, type);
            }
        }
    }
    return false;
}

bool TypeInfer::InferGetNextPropName(GateRef gate)
{
    auto stringType = GateType::StringType();
    return UpdateType(gate, stringType);
}

bool TypeInfer::InferDefineGetterSetterByValue(GateRef gate)
{
    // 0 : the index of obj
    auto objType = gateAccessor_.GetGateType(gateAccessor_.GetValueIn(gate, 0));
    return UpdateType(gate, objType);
}

bool TypeInfer::InferNewObjApply(GateRef gate)
{
    if (gateAccessor_.GetGateType(gate).IsAnyType()) {
        // 0 : the index of func
        auto funcType = gateAccessor_.GetGateType(gateAccessor_.GetValueIn(gate, 0));
        return UpdateType(gate, funcType);
    }
    return false;
}

bool TypeInfer::InferSuperCall(GateRef gate)
{
    ArgumentAccessor argAcc(circuit_);
    auto newTarget = argAcc.GetCommonArgGate(CommonArgIdx::NEW_TARGET);
    auto funcType = gateAccessor_.GetGateType(newTarget);
    if (!funcType.IsUndefinedType()) {
        return UpdateType(gate, funcType);
    }
    return false;
}

bool TypeInfer::InferTryLdGlobalByName(GateRef gate)
{
    // todo by hongtao, should consider function of .d.ts
    auto byteCodeInfo = builder_->GetByteCodeInfo(gate);
    ASSERT(byteCodeInfo.inputs.size() == 1);
    auto stringId = std::get<StringId>(byteCodeInfo.inputs[0]).GetId();
    auto iter = stringIdToGateType_.find(stringId);
    if (iter != stringIdToGateType_.end()) {
        return UpdateType(gate, iter->second);
    }
    return false;
}

void TypeInfer::Verify() const
{
    std::vector<GateRef> gateList;
    circuit_->GetAllGates(gateList);
    for (const auto &gate : gateList) {
        auto type = gateAccessor_.GetGateType(gate);
        if (ShouldInfer(gate) && type.IsTSType() && !type.IsAnyType()) {
            PrintType(gate);
        }
        auto op = gateAccessor_.GetOpCode(gate);
        if (op == OpCode::JS_BYTECODE) {
            TypeCheck(gate);
        }
    }
}

/*
 * Let v be a variable in one ts-file and t be a type. To check whether the type of v is t after
 * type inferenece, one should declare a function named "AssertType(value:any, type:string):void"
 * in ts-file and call it with arguments v and t, where t is the expected type string.
 * The following interface performs such a check at compile time.
 */
void TypeInfer::TypeCheck(GateRef gate) const
{
    auto info = builder_->GetByteCodeInfo(gate);
    if (!info.IsBc(EcmaOpcode::CALLARGS2_IMM8_V8_V8)) {
        return;
    }
    auto func = gateAccessor_.GetValueIn(gate, 0);
    auto funcInfo = builder_->GetByteCodeInfo(func);
    if (!funcInfo.IsBc(EcmaOpcode::TRYLDGLOBALBYNAME_IMM8_ID16)) {
        return;
    }
    auto funcName = gateAccessor_.GetValueIn(func, 0);
    if (tsManager_->GetStdStringById(gateAccessor_.GetBitField(funcName)) ==  "AssertType") {
        GateRef expectedGate = gateAccessor_.GetValueIn(gateAccessor_.GetValueIn(gate, 2), 0);
        auto expectedTypeStr = tsManager_->GetStdStringById(gateAccessor_.GetBitField(expectedGate));
        GateRef valueGate = gateAccessor_.GetValueIn(gate, 1);
        auto type = gateAccessor_.GetGateType(valueGate);
        if (expectedTypeStr != tsManager_->GetTypeStr(type)) {
            PrintType(valueGate);
            std::string log("[TypeInfer][Error] ");
            log += "gate id: "+ std::to_string(gateAccessor_.GetId(valueGate)) + ", ";
            log += "expected type: " + expectedTypeStr;
            LOG_COMPILER(FATAL) << log;
            std::abort();
        }
    }
}

void TypeInfer::PrintType(GateRef gate) const
{
    std::string log("[TypeInfer] ");
    log += "gate id: "+ std::to_string(gateAccessor_.GetId(gate)) + ", ";
    auto op = gateAccessor_.GetOpCode(gate);
    log += "opcode: " + op.Str() + ", ";
    if (op != OpCode::VALUE_SELECTOR) {
        log += "bytecode: " + builder_->GetBytecodeStr(gate) + ", ";
    }
    auto type = gateAccessor_.GetGateType(gate);
    log += "type: " + tsManager_->GetTypeStr(type) + ", ";
    auto typeRef = GlobalTSTypeRef(type.GetType());
    log += "moduleId: " + std::to_string(typeRef.GetModuleId()) + ", ";
    log += "localId: " + std::to_string(typeRef.GetLocalId());
    LOG_COMPILER(INFO) << log;
}

void TypeFilter::Run() const
{
    LOG_COMPILER(INFO) << "================== filter any types outputs ==================";
    const JSPandaFile *jsPandaFile = builder_->GetJSPandaFile();
    EntityId methodId = methodLiteral_->GetMethodId();

    JSPtExtractor *debugExtractor = JSPandaFileManager::GetInstance()->GetJSPtExtractor(jsPandaFile);
    const std::string &sourceFileName = debugExtractor->GetSourceFile(methodId);
    const std::string functionName = methodLiteral_->ParseFunctionName(jsPandaFile, methodId);

    std::vector<GateRef> gateList;
    circuit_->GetAllGates(gateList);
    for (const auto &gate : gateList) {
        GateType type = gateAccessor_.GetGateType(gate);
        if (infer_->ShouldInfer(gate) && type.IsAnyType()) {
            PrintAnyTypeGate(gate, debugExtractor, sourceFileName, functionName);
        }
    }
}

void TypeFilter::PrintAnyTypeGate(GateRef gate, JSPtExtractor *debugExtractor, const std::string &sourceFileName,
                                  const std::string &functionName) const
{
    std::string log("[TypeFilter] ");
    log += "gate id: "+ std::to_string(gateAccessor_.GetId(gate)) + ", ";
    OpCode op = gateAccessor_.GetOpCode(gate);
    if (op != OpCode::VALUE_SELECTOR) {
    // handle ByteCode gate: print gate id, bytecode and line number in source code.
        log += "bytecode: " + builder_->GetBytecodeStr(gate) + ", ";

        int32_t lineNumber = 0;
        auto callbackLineFunc = [&lineNumber](int32_t line) -> bool {
            lineNumber = line + 1;
            return true;
        };

        const auto &gateToBytecode = builder_->GetGateToBytecode();
        const uint8_t *pc = gateToBytecode.at(gate).second;

        uint32_t offset = pc - methodLiteral_->GetBytecodeArray();
        debugExtractor->MatchLineWithOffset(callbackLineFunc, methodLiteral_->GetMethodId(), offset);

        log += "at " + functionName + " (" + sourceFileName +  ":" + std::to_string(lineNumber) + ")";
    } else {
    // handle phi gate: print gate id and input gates id list.
        log += "phi gate, ins: ";
        auto ins = gateAccessor_.ConstIns(gate);
        for (auto it =  ins.begin(); it != ins.end(); it++) {
            log += std::to_string(gateAccessor_.GetId(*it)) + " ";
        }
    }

    LOG_COMPILER(INFO) << log;
}
}  // namespace panda::ecmascript
