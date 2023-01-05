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

#include "ecmascript/compiler/argument_accessor.h"
#include "ecmascript/compiler/circuit_builder.h"
#include "ecmascript/compiler/gate_accessor.h"

namespace panda::ecmascript::kungfu {
using UseIterator = GateAccessor::UseIterator;

size_t GateAccessor::GetNumIns(GateRef gate) const
{
    Gate *gatePtr = circuit_->LoadGatePtr(gate);
    return gatePtr->GetNumIns();
}

MarkCode GateAccessor::GetMark(GateRef gate) const
{
    return circuit_->GetMark(gate);
}

void GateAccessor::SetMark(GateRef gate, MarkCode mark)
{
    circuit_->SetMark(gate, mark);
}

bool GateAccessor::IsFinished(GateRef gate) const
{
    return GetMark(gate) == MarkCode::FINISHED;
}

bool GateAccessor::IsVisited(GateRef gate) const
{
    return GetMark(gate) == MarkCode::VISITED;
}

bool GateAccessor::IsNotMarked(GateRef gate) const
{
    return GetMark(gate) == MarkCode::NO_MARK;
}

void GateAccessor::SetFinished(GateRef gate)
{
    SetMark(gate, MarkCode::FINISHED);
}

void GateAccessor::SetVisited(GateRef gate)
{
    SetMark(gate, MarkCode::VISITED);
}

OpCode GateAccessor::GetOpCode(GateRef gate) const
{
    Gate *gatePtr = circuit_->LoadGatePtr(gate);
    return gatePtr->GetOpCode();
}

BitField GateAccessor::TryGetValue(GateRef gate) const
{
    Gate *gatePtr = circuit_->LoadGatePtr(gate);
    return gatePtr->TryGetValue();
}

ICmpCondition GateAccessor::GetICmpCondition(GateRef gate) const
{
    ASSERT(GetOpCode(gate) == OpCode::ICMP);
    Gate *gatePtr = circuit_->LoadGatePtr(gate);
    return static_cast<ICmpCondition>(gatePtr->GetOneParameterMetaData()->GetValue());
}

FCmpCondition GateAccessor::GetFCmpCondition(GateRef gate) const
{
    ASSERT(GetOpCode(gate) == OpCode::FCMP);
    Gate *gatePtr = circuit_->LoadGatePtr(gate);
    return static_cast<FCmpCondition>(gatePtr->GetOneParameterMetaData()->GetValue());
}

ConstDataId GateAccessor::GetConstDataId(GateRef gate) const
{
    ASSERT(GetOpCode(gate) == OpCode::CONST_DATA);
    Gate *gatePtr = circuit_->LoadGatePtr(gate);
    return ConstDataId(gatePtr->GetOneParameterMetaData()->GetValue());
}

TypedUnaryAccessor GateAccessor::GetTypedUnOp(GateRef gate) const
{
    ASSERT(GetOpCode(gate) == OpCode::TYPED_UNARY_OP);
    Gate *gatePtr = circuit_->LoadGatePtr(gate);
    return TypedUnaryAccessor(gatePtr->GetOneParameterMetaData()->GetValue());
}

TypedLoadOp GateAccessor::GetTypedLoadOp(GateRef gate) const
{
    ASSERT(GetOpCode(gate) == OpCode::LOAD_ELEMENT);
    Gate *gatePtr = circuit_->LoadGatePtr(gate);
    return static_cast<TypedLoadOp>(gatePtr->GetOneParameterMetaData()->GetValue());
}

TypedStoreOp GateAccessor::GetTypedStoreOp(GateRef gate) const
{
    ASSERT(GetOpCode(gate) == OpCode::STORE_ELEMENT);
    Gate *gatePtr = circuit_->LoadGatePtr(gate);
    return static_cast<TypedStoreOp>(gatePtr->GetOneParameterMetaData()->GetValue());
}

TypedBinOp GateAccessor::GetTypedBinaryOp(GateRef gate) const
{
    ASSERT(GetOpCode(gate) == OpCode::TYPED_BINARY_OP);
    Gate *gatePtr = circuit_->LoadGatePtr(gate);
    return gatePtr->GetTypedBinaryMegaData()->GetTypedBinaryOp();
}

GateType GateAccessor::GetParamGateType(GateRef gate) const
{
    ASSERT(GetOpCode(gate) == OpCode::PRIMITIVE_TYPE_CHECK ||
           GetOpCode(gate) == OpCode::OBJECT_TYPE_CHECK ||
           GetOpCode(gate) == OpCode::ARRAY_CHECK ||
           GetOpCode(gate) == OpCode::STABLE_ARRAY_CHECK ||
           GetOpCode(gate) == OpCode::TYPED_ARRAY_CHECK ||
           GetOpCode(gate) == OpCode::INDEX_CHECK);
    Gate *gatePtr = circuit_->LoadGatePtr(gate);
    GateTypeAccessor accessor(gatePtr->GetOneParameterMetaData()->GetValue());
    return accessor.GetGateType();
}

GateType GateAccessor::GetLeftType(GateRef gate) const
{
    ASSERT(GetOpCode(gate) == OpCode::TYPED_UNARY_OP ||
           GetOpCode(gate) == OpCode::TYPED_BINARY_OP ||
           GetOpCode(gate) == OpCode::TYPE_CONVERT);
    Gate *gatePtr = circuit_->LoadGatePtr(gate);
    GatePairTypeAccessor accessor(gatePtr->GetOneParameterMetaData()->GetValue());
    return accessor.GetLeftType();
}

GateType GateAccessor::GetRightType(GateRef gate) const
{
    ASSERT(GetOpCode(gate) == OpCode::TYPED_BINARY_OP ||
           GetOpCode(gate) == OpCode::TYPE_CONVERT);
    Gate *gatePtr = circuit_->LoadGatePtr(gate);
    GatePairTypeAccessor accessor(gatePtr->GetOneParameterMetaData()->GetValue());
    return accessor.GetRightType();
}

size_t GateAccessor::GetVirtualRegisterIndex(GateRef gate) const
{
    ASSERT(GetOpCode(gate) == OpCode::SAVE_REGISTER ||
           GetOpCode(gate) == OpCode::RESTORE_REGISTER);
    Gate *gatePtr = circuit_->LoadGatePtr(gate);
    return static_cast<size_t>(gatePtr->GetOneParameterMetaData()->GetValue());
}

uint64_t GateAccessor::GetConstantValue(GateRef gate) const
{
    ASSERT(GetOpCode(gate) == OpCode::CONSTANT);
    Gate *gatePtr = circuit_->LoadGatePtr(gate);
    return gatePtr->GetOneParameterMetaData()->GetValue();
}

const std::string GateAccessor::GetConstantString(GateRef gate) const
{
    ASSERT(GetOpCode(gate) == OpCode::CONSTSTRING);
    Gate *gatePtr = circuit_->LoadGatePtr(gate);
    return gatePtr->GetStringMetaData()->GetString();
}

uint32_t GateAccessor::GetBytecodeIndex(GateRef gate) const
{
    ASSERT(GetOpCode(gate) == OpCode::JS_BYTECODE);
    Gate *gatePtr = circuit_->LoadGatePtr(gate);
    return gatePtr->GetJSBytecodeMetaData()->GetBytecodeIndex();
}

EcmaOpcode GateAccessor::GetByteCodeOpcode(GateRef gate) const
{
    ASSERT(GetOpCode(gate) == OpCode::JS_BYTECODE);
    Gate *gatePtr = circuit_->LoadGatePtr(gate);
    return gatePtr->GetJSBytecodeMetaData()->GetByteCodeOpcode();
}

const std::map<std::pair<GateRef, uint32_t>, uint32_t> &GateAccessor::GetRestoreRegsInfo(GateRef gate) const
{
    ASSERT(GetOpCode(gate) == OpCode::RESTORE_REGISTER);
    Gate *gatePtr = circuit_->LoadGatePtr(gate);
    return gatePtr->GetRestoreRegsMetaData()->GetRestoreRegsInfo();
}

void GateAccessor::SetRestoreRegsInfo(GateRef gate, std::pair<GateRef, uint32_t> &info, uint32_t index) const
{
    ASSERT(GetOpCode(gate) == OpCode::RESTORE_REGISTER);
    Gate *gatePtr = circuit_->LoadGatePtr(gate);
    const_cast<RestoreRegsMetaData*>(gatePtr->GetRestoreRegsMetaData())->SetRestoreRegsInfo(info, index);
}

size_t GateAccessor::GetNumOfSaveRegs(GateRef gate) const
{
    ASSERT(GetOpCode(gate) == OpCode::SAVE_REGISTER);
    Gate *gatePtr = circuit_->LoadGatePtr(gate);
    return static_cast<size_t>(gatePtr->GetSaveRegsMetaData()->GetNumValue());
}

void GateAccessor::Print(GateRef gate) const
{
    Gate *gatePtr = circuit_->LoadGatePtr(gate);
    gatePtr->Print();
}

void GateAccessor::ShortPrint(GateRef gate) const
{
    Gate *gatePtr = circuit_->LoadGatePtr(gate);
    gatePtr->ShortPrint();
}

GateId GateAccessor::GetId(GateRef gate) const
{
    Gate *gatePtr = circuit_->LoadGatePtr(gate);
    return gatePtr->GetId();
}

size_t GateAccessor::GetInValueStarts(GateRef gate) const
{
    Gate *gatePtr = circuit_->LoadGatePtr(gate);
    return gatePtr->GetInValueStarts();
}

GateRef GateAccessor::GetValueIn(GateRef gate, size_t idx) const
{
    Gate *gatePtr = circuit_->LoadGatePtr(gate);
    ASSERT(idx < gatePtr->GetInValueCount());
    size_t valueIndex = gatePtr->GetInValueStarts();
    return circuit_->GetIn(gate, valueIndex + idx);
}

size_t GateAccessor::GetNumValueIn(GateRef gate) const
{
    Gate *gatePtr = circuit_->LoadGatePtr(gate);
    return gatePtr->GetInValueCount();
}

bool GateAccessor::IsGCRelated(GateRef gate) const
{
    return GetGateType(gate).IsGCRelated();
}

GateRef GateAccessor::GetIn(GateRef gate, size_t idx) const
{
    return circuit_->GetIn(gate, idx);
}

GateRef GateAccessor::GetState(GateRef gate, size_t idx) const
{
    ASSERT(idx < circuit_->LoadGatePtr(gate)->GetStateCount());
    return circuit_->GetIn(gate, idx);
}

void GateAccessor::GetInStates(GateRef gate, std::vector<GateRef>& ins) const
{
    const Gate *curGate = circuit_->LoadGatePtrConst(gate);
    for (size_t idx = 0; idx < curGate->GetStateCount(); idx++) {
        ins.push_back(circuit_->GetGateRef(curGate->GetInGateConst(idx)));
    }
}

void GateAccessor::GetIns(GateRef gate, std::vector<GateRef>& ins) const
{
    const Gate *curGate = circuit_->LoadGatePtrConst(gate);
    for (size_t idx = 0; idx < curGate->GetNumIns(); idx++) {
        ins.push_back(circuit_->GetGateRef(curGate->GetInGateConst(idx)));
    }
}

void GateAccessor::GetOuts(GateRef gate, std::vector<GateRef>& outs) const
{
    const Gate *curGate = circuit_->LoadGatePtrConst(gate);
    if (!curGate->IsFirstOutNull()) {
        const Out *curOut = curGate->GetFirstOutConst();
        GateRef ref = circuit_->GetGateRef(curOut->GetGateConst());
        outs.push_back(ref);
        while (!curOut->IsNextOutNull()) {
            curOut = curOut->GetNextOutConst();
            ref = circuit_->GetGateRef(curOut->GetGateConst());
            outs.push_back(ref);
        }
    }
}

void GateAccessor::GetOutStates(GateRef gate, std::vector<GateRef>& outStates) const
{
    const Gate *curGate = circuit_->LoadGatePtrConst(gate);
    if (!curGate->IsFirstOutNull()) {
        const Out *curOut = curGate->GetFirstOutConst();
        GateRef ref = circuit_->GetGateRef(curOut->GetGateConst());
        if (GetMetaData(ref)->IsState()) {
            outStates.push_back(ref);
        }
        while (!curOut->IsNextOutNull()) {
            curOut = curOut->GetNextOutConst();
            ref = circuit_->GetGateRef(curOut->GetGateConst());
            if (GetMetaData(ref)->IsState()) {
                outStates.push_back(ref);
            }
        }
    }
}

void GateAccessor::GetStateUses(GateRef gate, std::vector<GateRef>& outStates)
{
    auto uses = Uses(gate);
    for (auto it = uses.begin(); it != uses.end(); it++) {
        if (IsStateIn(it)) {
            outStates.emplace_back(*it);
        }
    }
}

void GateAccessor::GetAllGates(std::vector<GateRef>& gates) const
{
    circuit_->GetAllGates(gates);
}

bool GateAccessor::IsInGateNull(GateRef gate, size_t idx) const
{
    return circuit_->IsInGateNull(gate, idx);
}

bool GateAccessor::IsSelector(GateRef g) const
{
    return GetOpCode(g) == OpCode::VALUE_SELECTOR;
}

bool GateAccessor::IsControlCase(GateRef gate) const
{
    return circuit_->IsControlCase(gate);
}

bool GateAccessor::IsLoopHead(GateRef gate) const
{
    return circuit_->IsLoopHead(gate);
}

bool GateAccessor::IsLoopBack(GateRef gate) const
{
    return GetOpCode(gate) == OpCode::LOOP_BACK;
}

bool GateAccessor::IsState(GateRef gate) const
{
    return GetMetaData(gate)->IsState();
}

bool GateAccessor::IsConstant(GateRef gate) const
{
    return GetMetaData(gate)->IsConstant();
}

bool GateAccessor::IsConstantValue(GateRef gate, uint64_t value) const
{
    auto isConstant = IsConstant(gate);
    if (isConstant) {
        uint64_t bitField = GetConstantValue(gate);
        return bitField == value;
    }
    return false;
}

bool GateAccessor::IsTypedOperator(GateRef gate) const
{
    return GetMetaData(gate)->IsTypedOperator();
}

bool GateAccessor::IsNotWrite(GateRef gate) const
{
    auto op = GetOpCode(gate);
    switch (op) {
        case OpCode::STATE_ENTRY:
        case OpCode::RETURN:
        case OpCode::RETURN_VOID:
        case OpCode::ORDINARY_BLOCK:
        case OpCode::MERGE:
        case OpCode::IF_BRANCH:
        case OpCode::IF_TRUE:
        case OpCode::IF_FALSE:
        case OpCode::IF_SUCCESS:
        case OpCode::IF_EXCEPTION:
        case OpCode::TO_LENGTH:
        case OpCode::ARRAY_CHECK:
        case OpCode::STABLE_ARRAY_CHECK:
        case OpCode::PRIMITIVE_TYPE_CHECK:
        case OpCode::OBJECT_TYPE_CHECK:
        case OpCode::TYPED_ARRAY_CHECK:
        case OpCode::INDEX_CHECK:
        case OpCode::INT32_OVERFLOW_CHECK:
        case OpCode::TYPED_UNARY_OP:
        case OpCode::TYPED_BINARY_OP:
        case OpCode::TYPE_CONVERT:
        case OpCode::LOAD_ELEMENT:
        case OpCode::LOAD_PROPERTY:
        case OpCode::LOAD_ARRAY_LENGTH:
            return true;
        default:
            break;
    }
    if (op == OpCode::JS_BYTECODE) {
        auto ecmaOp = GetByteCodeOpcode(gate);
        switch (ecmaOp) {
            case EcmaOpcode::INC_IMM8:
            case EcmaOpcode::DEC_IMM8:
            case EcmaOpcode::GETPROPITERATOR:
            case EcmaOpcode::GETRESUMEMODE:
            case EcmaOpcode::ADD2_IMM8_V8:
            case EcmaOpcode::SUB2_IMM8_V8:
            case EcmaOpcode::MUL2_IMM8_V8:
            case EcmaOpcode::DIV2_IMM8_V8:
            case EcmaOpcode::MOD2_IMM8_V8:
            case EcmaOpcode::EQ_IMM8_V8:
            case EcmaOpcode::NOTEQ_IMM8_V8:
            case EcmaOpcode::LESS_IMM8_V8:
            case EcmaOpcode::LESSEQ_IMM8_V8:
            case EcmaOpcode::GREATER_IMM8_V8:
            case EcmaOpcode::GREATEREQ_IMM8_V8:
            case EcmaOpcode::TRYLDGLOBALBYNAME_IMM8_ID16:
            case EcmaOpcode::TRYLDGLOBALBYNAME_IMM16_ID16:
            case EcmaOpcode::GETITERATOR_IMM8:
            case EcmaOpcode::GETITERATOR_IMM16:
            case EcmaOpcode::TYPEOF_IMM8:
            case EcmaOpcode::TYPEOF_IMM16:
            case EcmaOpcode::LDSYMBOL:
            case EcmaOpcode::LDGLOBAL:
            case EcmaOpcode::TONUMBER_IMM8:
            case EcmaOpcode::NEG_IMM8:
            case EcmaOpcode::NOT_IMM8:
            case EcmaOpcode::SHL2_IMM8_V8:
            case EcmaOpcode::SHR2_IMM8_V8:
            case EcmaOpcode::ASHR2_IMM8_V8:
            case EcmaOpcode::AND2_IMM8_V8:
            case EcmaOpcode::OR2_IMM8_V8:
            case EcmaOpcode::XOR2_IMM8_V8:
            case EcmaOpcode::DEFINEMETHOD_IMM8_ID16_IMM8:
            case EcmaOpcode::DEFINEMETHOD_IMM16_ID16_IMM8:
            case EcmaOpcode::EXP_IMM8_V8:
            case EcmaOpcode::ISIN_IMM8_V8:
            case EcmaOpcode::STRICTNOTEQ_IMM8_V8:
            case EcmaOpcode::STRICTEQ_IMM8_V8:
            case EcmaOpcode::GETTEMPLATEOBJECT_IMM8:
            case EcmaOpcode::GETTEMPLATEOBJECT_IMM16:
            case EcmaOpcode::LDBIGINT_ID16:
            case EcmaOpcode::TONUMERIC_IMM8:
            case EcmaOpcode::LDEXTERNALMODULEVAR_IMM8:
            case EcmaOpcode::WIDE_LDEXTERNALMODULEVAR_PREF_IMM16:
            case EcmaOpcode::GETMODULENAMESPACE_IMM8:
            case EcmaOpcode::WIDE_GETMODULENAMESPACE_PREF_IMM16:
            case EcmaOpcode::WIDE_NEWOBJRANGE_PREF_IMM16_V8:
            case EcmaOpcode::JEQZ_IMM8:
            case EcmaOpcode::JEQZ_IMM16:
            case EcmaOpcode::JEQZ_IMM32:
            case EcmaOpcode::JNEZ_IMM8:
            case EcmaOpcode::JNEZ_IMM16:
            case EcmaOpcode::JNEZ_IMM32:
            case EcmaOpcode::ISTRUE:
            case EcmaOpcode::ISFALSE:
            case EcmaOpcode::GETNEXTPROPNAME_V8:
            case EcmaOpcode::LDSUPERBYVALUE_IMM8_V8:
            case EcmaOpcode::LDSUPERBYVALUE_IMM16_V8:
            case EcmaOpcode::LDGLOBALVAR_IMM16_ID16:
            case EcmaOpcode::LDOBJBYNAME_IMM8_ID16:
            case EcmaOpcode::LDOBJBYNAME_IMM16_ID16:
            case EcmaOpcode::LDOBJBYINDEX_IMM8_IMM16:
            case EcmaOpcode::LDOBJBYINDEX_IMM16_IMM16:
            case EcmaOpcode::WIDE_LDOBJBYINDEX_PREF_IMM32:
            case EcmaOpcode::LDOBJBYVALUE_IMM8_V8:
            case EcmaOpcode::LDOBJBYVALUE_IMM16_V8:
            case EcmaOpcode::LDTHISBYVALUE_IMM8:
            case EcmaOpcode::LDTHISBYVALUE_IMM16:
            case EcmaOpcode::LDSUPERBYNAME_IMM8_ID16:
            case EcmaOpcode::LDSUPERBYNAME_IMM16_ID16:
            case EcmaOpcode::LDLEXVAR_IMM4_IMM4:
            case EcmaOpcode::LDLEXVAR_IMM8_IMM8:
            case EcmaOpcode::WIDE_LDLEXVAR_PREF_IMM16_IMM16:
            case EcmaOpcode::WIDE_LDPATCHVAR_PREF_IMM16:
            case EcmaOpcode::LDLOCALMODULEVAR_IMM8:
            case EcmaOpcode::WIDE_LDLOCALMODULEVAR_PREF_IMM16:
            case EcmaOpcode::JSTRICTEQZ_IMM8:
            case EcmaOpcode::JSTRICTEQZ_IMM16:
            case EcmaOpcode::JNSTRICTEQZ_IMM8:
            case EcmaOpcode::JNSTRICTEQZ_IMM16:
            case EcmaOpcode::JEQNULL_IMM8:
            case EcmaOpcode::JEQNULL_IMM16:
            case EcmaOpcode::JNENULL_IMM8:
            case EcmaOpcode::JNENULL_IMM16:
            case EcmaOpcode::JSTRICTEQNULL_IMM8:
            case EcmaOpcode::JSTRICTEQNULL_IMM16:
            case EcmaOpcode::JNSTRICTEQNULL_IMM8:
            case EcmaOpcode::JNSTRICTEQNULL_IMM16:
            case EcmaOpcode::JEQUNDEFINED_IMM8:
            case EcmaOpcode::JEQUNDEFINED_IMM16:
            case EcmaOpcode::JNEUNDEFINED_IMM8:
            case EcmaOpcode::JNEUNDEFINED_IMM16:
            case EcmaOpcode::JSTRICTEQUNDEFINED_IMM8:
            case EcmaOpcode::JSTRICTEQUNDEFINED_IMM16:
            case EcmaOpcode::JNSTRICTEQUNDEFINED_IMM8:
            case EcmaOpcode::JNSTRICTEQUNDEFINED_IMM16:
            case EcmaOpcode::JEQ_V8_IMM8:
            case EcmaOpcode::JEQ_V8_IMM16:
            case EcmaOpcode::JNE_V8_IMM8:
            case EcmaOpcode::JNE_V8_IMM16:
            case EcmaOpcode::JSTRICTEQ_V8_IMM8:
            case EcmaOpcode::JSTRICTEQ_V8_IMM16:
            case EcmaOpcode::JNSTRICTEQ_V8_IMM8:
            case EcmaOpcode::JNSTRICTEQ_V8_IMM16:
            case EcmaOpcode::LDTHISBYNAME_IMM8_ID16:
            case EcmaOpcode::LDTHISBYNAME_IMM16_ID16:
                return true;
            default:
                break;
        }
    }
    return false;
}

bool GateAccessor::IsCheckWithTwoIns(GateRef gate) const
{
    return GetMetaData(gate)->IsCheckWithTwoIns();
}

bool GateAccessor::IsCheckWithOneIn(GateRef gate) const
{
    return GetMetaData(gate)->IsCheckWithOneIn();
}

bool GateAccessor::IsSchedulable(GateRef gate) const
{
    return GetMetaData(gate)->IsSchedulable();
}

GateRef GateAccessor::GetDep(GateRef gate, size_t idx) const
{
    Gate *gatePtr = circuit_->LoadGatePtr(gate);
    ASSERT(idx < gatePtr->GetDependCount());
    size_t dependIndex = gatePtr->GetStateCount();
    return circuit_->GetIn(gate, dependIndex + idx);
}

size_t GateAccessor::GetImmediateId(GateRef gate) const
{
    Gate *gatePtr = circuit_->LoadGatePtr(gate);
    ASSERT(gatePtr->GetGateType() == GateType::NJSValue());
    ASSERT(gatePtr->GetOpCode() == OpCode::CONSTANT);
    ASSERT(gatePtr->GetMachineType() == MachineType::I64);
    size_t imm = gatePtr->GetOneParameterMetaData()->GetValue();
    return imm;
}

void GateAccessor::SetDep(GateRef gate, GateRef depGate, size_t idx)
{
    Gate *gatePtr = circuit_->LoadGatePtr(gate);
    ASSERT(idx < gatePtr->GetDependCount());
    size_t dependIndex = gatePtr->GetStateCount();
    gatePtr->ModifyIn(dependIndex + idx, circuit_->LoadGatePtr(depGate));
}

UseIterator GateAccessor::ReplaceIn(const UseIterator &useIt, GateRef replaceGate)
{
    UseIterator next = useIt;
    next++;
    Gate *curGatePtr = circuit_->LoadGatePtr(*useIt);
    Gate *replaceGatePtr = circuit_->LoadGatePtr(replaceGate);
    curGatePtr->ModifyIn(useIt.GetIndex(), replaceGatePtr);
    return next;
}

GateType GateAccessor::GetGateType(GateRef gate) const
{
    return circuit_->LoadGatePtr(gate)->GetGateType();
}

void GateAccessor::SetGateType(GateRef gate, GateType gt)
{
    circuit_->LoadGatePtr(gate)->SetGateType(gt);
}

UseIterator GateAccessor::DeleteExceptionDep(const UseIterator &useIt)
{
    auto next = useIt;
    next++;
    ASSERT(GetOpCode(*useIt) == OpCode::RETURN || GetOpCode(*useIt) == OpCode::DEPEND_SELECTOR);
    if (GetOpCode(*useIt) == OpCode::RETURN) {
        DeleteGate(useIt);
    } else {
        size_t idx = useIt.GetIndex();
        auto merge = GetState(*useIt, 0);
        circuit_->DecreaseIn(merge, idx - 1);
        auto mergeUses = Uses(merge);
        for (auto useGate : mergeUses) {
            if (circuit_->GetOpCode(useGate) == OpCode::VALUE_SELECTOR) {
                circuit_->DecreaseIn(useGate, idx);
            }
        }
        DecreaseIn(useIt);
    }
    return next;
}

UseIterator GateAccessor::DeleteGate(const UseIterator &useIt)
{
    auto next = useIt;
    next++;
    circuit_->DeleteGate(*useIt);
    return next;
}

void GateAccessor::DecreaseIn(const UseIterator &useIt)
{
    size_t idx = useIt.GetIndex();
    circuit_->DecreaseIn(*useIt, idx);
}


void GateAccessor::DecreaseIn(GateRef gate, size_t index)
{
    circuit_->DecreaseIn(gate, index);
}

void GateAccessor::NewIn(GateRef gate, size_t idx, GateRef in)
{
    circuit_->NewIn(gate, idx, in);
}

size_t GateAccessor::GetStateCount(GateRef gate) const
{
    return circuit_->LoadGatePtr(gate)->GetStateCount();
}

size_t GateAccessor::GetDependCount(GateRef gate) const
{
    return circuit_->LoadGatePtr(gate)->GetDependCount();
}

size_t GateAccessor::GetInValueCount(GateRef gate) const
{
    return circuit_->LoadGatePtr(gate)->GetInValueCount();
}

void GateAccessor::UpdateAllUses(GateRef oldIn, GateRef newIn)
{
    auto uses = Uses(oldIn);
    for (auto useIt = uses.begin(); useIt != uses.end();) {
        useIt = ReplaceIn(useIt, newIn);
    }
}

void GateAccessor::ReplaceIn(GateRef gate, size_t index, GateRef in)
{
    circuit_->ModifyIn(gate, index, in);
}

void GateAccessor::DeleteIn(GateRef gate, size_t idx)
{
    ASSERT(idx < circuit_->LoadGatePtrConst(gate)->GetNumIns());
    ASSERT(!circuit_->IsInGateNull(gate, idx));
    circuit_->LoadGatePtr(gate)->DeleteIn(idx);
}

void GateAccessor::ReplaceStateIn(GateRef gate, GateRef in, size_t index)
{
    ASSERT(index < GetStateCount(gate));
    circuit_->ModifyIn(gate, index, in);
}

void GateAccessor::ReplaceDependIn(GateRef gate, GateRef in, size_t index)
{
    ASSERT(index < GetDependCount(gate));
    size_t stateCount = GetStateCount(gate);
    circuit_->ModifyIn(gate, stateCount + index, in);
}

void GateAccessor::ReplaceValueIn(GateRef gate, GateRef in, size_t index)
{
    ASSERT(index < GetInValueCount(gate));
    size_t valueStartIndex = GetInValueStarts(gate);
    circuit_->ModifyIn(gate, valueStartIndex + index, in);
}

void GateAccessor::DeleteGate(GateRef gate)
{
    circuit_->DeleteGate(gate);
}

MachineType GateAccessor::GetMachineType(GateRef gate) const
{
    return circuit_->GetMachineType(gate);
}

void GateAccessor::SetMachineType(GateRef gate, MachineType type)
{
    circuit_->SetMachineType(gate, type);
}

GateRef GateAccessor::GetConstantGate(MachineType bitValue, BitField bitfield, GateType type) const
{
    return circuit_->GetConstantGate(bitValue, bitfield, type);
}

bool GateAccessor::IsStateIn(const UseIterator &useIt) const
{
    size_t stateStartIndex = 0;
    size_t stateEndIndex = stateStartIndex + GetStateCount(*useIt);
    size_t index = useIt.GetIndex();
    return (index >= stateStartIndex && index < stateEndIndex);
}

bool GateAccessor::IsDependIn(const UseIterator &useIt) const
{
    size_t dependStartIndex = GetStateCount(*useIt);
    size_t dependEndIndex = dependStartIndex + GetDependCount(*useIt);
    size_t index = useIt.GetIndex();
    return (index >= dependStartIndex && index < dependEndIndex);
}

bool GateAccessor::IsValueIn(const UseIterator &useIt) const
{
    size_t valueStartIndex = GetInValueStarts(*useIt);
    size_t valueEndIndex = valueStartIndex + GetInValueCount(*useIt);
    size_t index = useIt.GetIndex();
    return (index >= valueStartIndex && index < valueEndIndex);
}

bool GateAccessor::IsExceptionState(const UseIterator &useIt) const
{
    auto op = GetOpCode(*useIt);
    bool isDependSelector = (op == OpCode::DEPEND_SELECTOR) &&
                            (GetOpCode(GetIn(GetIn(*useIt, 0), useIt.GetIndex() - 1)) == OpCode::IF_EXCEPTION);
    bool isReturn = (op == OpCode::RETURN && GetOpCode(GetIn(*useIt, 0)) == OpCode::IF_EXCEPTION);
    return isDependSelector || isReturn;
}

bool GateAccessor::IsDependIn(GateRef gate, size_t index) const
{
    size_t dependStartIndex = GetStateCount(gate);
    size_t dependEndIndex = dependStartIndex + GetDependCount(gate);
    return (index >= dependStartIndex && index < dependEndIndex);
}

bool GateAccessor::IsValueIn(GateRef gate, size_t index) const
{
    size_t valueStartIndex = GetInValueStarts(gate);
    size_t valueEndIndex = valueStartIndex + GetInValueCount(gate);
    return (index >= valueStartIndex && index < valueEndIndex);
}

void GateAccessor::DeleteStateSplitAndFrameState(GateRef gate)
{
    GateRef stateSplit = GetDep(gate);
    if (GetOpCode(stateSplit) == OpCode::STATE_SPLIT) {
        GateRef dep = GetDep(stateSplit);
        ReplaceDependIn(gate, dep);
        GateRef frameState = GetValueIn(stateSplit, 0);
        DeleteGate(frameState);
        DeleteGate(stateSplit);
    }
}

void GateAccessor::ReplaceGate(GateRef gate, GateRef state, GateRef depend, GateRef value)
{
    auto uses = Uses(gate);
    for (auto useIt = uses.begin(); useIt != uses.end();) {
        if (IsStateIn(useIt)) {
            useIt = ReplaceIn(useIt, state);
        } else if (IsDependIn(useIt)) {
            useIt = ReplaceIn(useIt, depend);
        } else if (IsValueIn(useIt)) {
            useIt = ReplaceIn(useIt, value);
        } else {
            LOG_ECMA(FATAL) << "this branch is unreachable";
            UNREACHABLE();
        }
    }
    DeleteGate(gate);
}

GateRef GateAccessor::GetRoot(OpCode opcode) const
{
    GateRef root = circuit_->GetRoot();
    if (opcode == OpCode::CIRCUIT_ROOT) {
        return root;
    }

    auto uses = ConstUses(root);
    for (auto useIt = uses.begin(); useIt != uses.end(); ++useIt) {
        if (GetOpCode(*useIt) == opcode) {
            return *useIt;
        }
    }
    return Circuit::NullGate();
}

GateRef GateAccessor::GetGlueFromArgList() const
{
    auto argRoot = GetArgRoot();
    ASSERT(static_cast<size_t>(CommonArgIdx::GLUE) == 0);
    const Gate *curGate = circuit_->LoadGatePtrConst(argRoot);

    const Out *curOut = curGate->GetFirstOutConst();
    ASSERT(!curGate->IsFirstOutNull());
    while (!curOut->IsNextOutNull()) {
        curOut = curOut->GetNextOutConst();
    }
    return circuit_->GetGateRef(curOut->GetGateConst());
}

void GateAccessor::GetArgsOuts(std::vector<GateRef>& outs) const
{
    auto argRoot = GetArgRoot();
    GetOuts(argRoot, outs);
}

void GateAccessor::GetReturnOuts(std::vector<GateRef>& outs) const
{
    auto returnRoot = GetReturnRoot();
    GetOuts(returnRoot, outs);
}

const GateMetaData *GateAccessor::GetMetaData(GateRef gate) const
{
    return circuit_->LoadGatePtrConst(gate)->GetMetaData();
}

void GateAccessor::SetMetaData(GateRef gate, const GateMetaData* meta)
{
    return circuit_->LoadGatePtr(gate)->SetMetaData(meta);
}
}  // namespace panda::ecmascript::kungfu
