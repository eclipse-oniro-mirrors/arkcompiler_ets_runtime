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

#include "ecmascript/compiler/post_schedule.h"

#include "ecmascript/js_thread.h"

namespace panda::ecmascript::kungfu {
void PostSchedule::Run(ControlFlowGraph &cfg)
{
    GenerateExtraBB(cfg);

    if (IsLogEnabled()) {
        LOG_COMPILER(INFO) << "";
        LOG_COMPILER(INFO) << "\033[34m"
                           << "===================="
                           << " After post schedule "
                           << "[" << GetMethodName() << "]"
                           << "===================="
                           << "\033[0m";
        PrintGraph("Build extra basic block for scheduled gates", cfg);
        LOG_COMPILER(INFO) << "\033[34m" << "========================= End ==========================" << "\033[0m";
    }
}

void PostSchedule::GenerateExtraBB(ControlFlowGraph &cfg)
{
    size_t bbNum = cfg.size();
    size_t bbIdx = 0;
    while (bbIdx < bbNum) {
        const std::vector<GateRef>& bb = cfg.at(bbIdx);
        size_t instNum = bb.size();
        size_t instIdx = 0;
        while (instIdx < instNum) {
            const std::vector<GateRef>& currentBB = cfg.at(bbIdx);
            GateRef current = currentBB[instIdx];
            OpCode opcode = acc_.GetOpCode(current);
            bool needRetraverse = false;
            switch (opcode) {
                case OpCode::HEAP_ALLOC: {
                    needRetraverse = VisitHeapAlloc(current, cfg, bbIdx, instIdx);
                    break;
                }
                case OpCode::STORE: {
                    needRetraverse = VisitStore(current, cfg, bbIdx, instIdx);
                    break;
                }
                default: {
                    break;
                }
            }
            const std::vector<GateRef>& refreshedBB = cfg.at(bbIdx);
            instNum = refreshedBB.size();
            instIdx = needRetraverse ? 0 : (instIdx + 1);
        }
        bbNum = cfg.size();
        bbIdx++;
    }
}

bool PostSchedule::VisitHeapAlloc(GateRef gate, ControlFlowGraph &cfg, size_t bbIdx, size_t instIdx)
{
    int64_t flag = acc_.TryGetValue(gate);
    ASSERT(flag == RegionSpaceFlag::IN_YOUNG_SPACE ||
           flag == RegionSpaceFlag::IN_SHARED_OLD_SPACE);
    std::vector<GateRef> currentBBGates;
    std::vector<GateRef> successBBGates;
    std::vector<GateRef> failBBGates;
    std::vector<GateRef> endBBGates;
    if (flag == RegionSpaceFlag::IN_YOUNG_SPACE) {
        LoweringHeapAllocAndPrepareScheduleGate(gate, currentBBGates, successBBGates, failBBGates, endBBGates, flag);
#ifdef ARK_ASAN_ON
        ReplaceGateDirectly(currentBBGates, cfg, bbIdx, instIdx);
        return false;
#else
        ReplaceBBState(cfg, bbIdx, currentBBGates, endBBGates);
        ScheduleEndBB(endBBGates, cfg, bbIdx, instIdx);
        ScheduleNewBB(successBBGates, cfg, bbIdx);
        ScheduleNewBB(failBBGates, cfg, bbIdx);
        ScheduleCurrentBB(currentBBGates, cfg, bbIdx, instIdx);
        return true;
#endif
    } else if (flag == RegionSpaceFlag::IN_SHARED_OLD_SPACE) {
        LoweringHeapAllocate(gate, currentBBGates, successBBGates, failBBGates, endBBGates, flag);
        ReplaceGateDirectly(currentBBGates, cfg, bbIdx, instIdx);
        return false;
    }
    return false;
}

void PostSchedule::ReplaceGateDirectly(std::vector<GateRef> &gates, ControlFlowGraph &cfg, size_t bbIdx, size_t instIdx)
{
    std::vector<GateRef>& bb = cfg.at(bbIdx);
    bb.insert(bb.begin() + instIdx, gates.begin(), gates.end());
    bb.erase(bb.begin() + instIdx + gates.size());
}

void PostSchedule::ScheduleEndBB(std::vector<GateRef> &gates, ControlFlowGraph &cfg, size_t bbIdx, size_t instIdx)
{
    std::vector<GateRef>& bb = cfg.at(bbIdx);
    if (instIdx > 0) {
        gates.insert(gates.begin() + 1, bb.begin(), bb.begin() + instIdx);  // 1: after state gate
    }
    cfg.insert(cfg.begin() + bbIdx + 1, std::move(gates));  // 1: after current bb
}

void PostSchedule::ScheduleNewBB(std::vector<GateRef> &gates, ControlFlowGraph &cfg, size_t bbIdx)
{
    if (!gates.empty()) {
        cfg.insert(cfg.begin() + bbIdx + 1, std::move(gates));
    }
}

void PostSchedule::ScheduleCurrentBB(const std::vector<GateRef> &gates, ControlFlowGraph &cfg, size_t bbIdx,
                                     size_t instIdx)
{
    std::vector<GateRef>& bb = cfg.at(bbIdx);
    if (instIdx == 0) {
        bb.erase(bb.begin());
    } else {
        bb.erase(bb.begin(), bb.begin() + instIdx + 1);  // 1: include current gate
    }
    bb.insert(bb.begin(), gates.begin(), gates.end());
}

void PostSchedule::PrepareToScheduleNewGate(GateRef gate, std::vector<GateRef> &gates)
{
    gates.emplace_back(gate);
}

void PostSchedule::ReplaceBBState(ControlFlowGraph &cfg, size_t bbIdx, std::vector<GateRef> &currentBBGates,
                                  std::vector<GateRef> &endBBGates)
{
    GateRef floatBranch = currentBBGates[0];
    ASSERT(acc_.GetOpCode(floatBranch) == OpCode::IF_BRANCH);
    GateRef endBBState = endBBGates[0];
    ASSERT(acc_.GetOpCode(endBBState) == OpCode::MERGE);
    std::vector<GateRef>& bb = cfg.at(bbIdx);
    GateRef currentBBState = bb[0];
    ASSERT(acc_.IsState(currentBBState));

    OpCode opcode = acc_.GetOpCode(currentBBState);
    switch (opcode) {
        case OpCode::DEOPT_CHECK:
        case OpCode::RETURN:
        case OpCode::RETURN_VOID:
        case OpCode::IF_BRANCH:
        case OpCode::SWITCH_BRANCH: {
            GateRef stateIn = acc_.GetState(currentBBState, 0);
            acc_.ReplaceStateIn(floatBranch, stateIn);
            acc_.ReplaceStateIn(currentBBState, endBBState);
            break;
        }
        case OpCode::STATE_ENTRY:
        case OpCode::ORDINARY_BLOCK:
        case OpCode::IF_TRUE:
        case OpCode::IF_FALSE:
        case OpCode::SWITCH_CASE:
        case OpCode::DEFAULT_CASE:
        case OpCode::MERGE:
        case OpCode::LOOP_BEGIN:
        case OpCode::LOOP_BACK: {
            acc_.ReplaceControlGate(currentBBState, endBBState);
            acc_.ReplaceStateIn(floatBranch, currentBBState);
            currentBBGates.insert(currentBBGates.begin(), currentBBState);
            bb[0] = builder_.Nop();
            break;
        }
        default: {
            LOG_ECMA(FATAL) << "this branch is unreachable with opcode:" << opcode;
            UNREACHABLE();
        }
    }
}

void PostSchedule::LoweringHeapAllocAndPrepareScheduleGate(GateRef gate,
                                                           std::vector<GateRef> &currentBBGates,
                                                           std::vector<GateRef> &successBBGates,
                                                           std::vector<GateRef> &failBBGates,
                                                           std::vector<GateRef> &endBBGates,
                                                           [[maybe_unused]] int64_t flag)
{
#ifdef ARK_ASAN_ON
    LoweringHeapAllocate(gate, currentBBGates, successBBGates, failBBGates, endBBGates, flag);
#else
    Environment env(gate, circuit_, &builder_);
    Label exit(&builder_);
    GateRef glue = acc_.GetValueIn(gate, 0);
    GateRef size = acc_.GetValueIn(gate, 1);
    GateRef hole = circuit_->GetConstantGateWithoutCache(
        MachineType::I64, JSTaggedValue::VALUE_HOLE, GateType::TaggedValue());
    DEFVALUE(result, (&builder_), VariableType::JS_ANY(), hole);
    Label success(&builder_);
    Label callRuntime(&builder_);
    size_t topOffset = JSThread::GlueData::GetNewSpaceAllocationTopAddressOffset(false);
    size_t endOffset = JSThread::GlueData::GetNewSpaceAllocationEndAddressOffset(false);
    GateRef topAddrOffset = circuit_->GetConstantGateWithoutCache(MachineType::I64, topOffset, GateType::NJSValue());
    GateRef endAddrOffset = circuit_->GetConstantGateWithoutCache(MachineType::I64, endOffset, GateType::NJSValue());
    GateRef topAddrAddr = builder_.PtrAdd(glue, topAddrOffset);
    GateRef endAddrAddr = builder_.PtrAdd(glue, endAddrOffset);
    GateRef topAddress = builder_.Load(VariableType::NATIVE_POINTER(), topAddrAddr);
    GateRef endAddress = builder_.Load(VariableType::NATIVE_POINTER(), endAddrAddr);
    GateRef addrOffset = circuit_->GetConstantGateWithoutCache(MachineType::I64, 0, GateType::NJSValue());
    GateRef rawTopAddr = builder_.PtrAdd(topAddress, addrOffset);
    GateRef rawEndAddr = builder_.PtrAdd(endAddress, addrOffset);
    GateRef top = builder_.Load(VariableType::JS_POINTER(), rawTopAddr);
    GateRef end = builder_.Load(VariableType::JS_POINTER(), rawEndAddr);

    GateRef newTop = builder_.PtrAdd(top, size);
    GateRef condition = builder_.Int64GreaterThan(newTop, end);
    Label *currentLabel = env.GetCurrentLabel();
    BRANCH_CIR(condition, &callRuntime, &success);
    {
        GateRef ifBranch = currentLabel->GetControl();
        PrepareToScheduleNewGate(ifBranch, currentBBGates);
        PrepareToScheduleNewGate(condition, currentBBGates);
        PrepareToScheduleNewGate(newTop, currentBBGates);
        PrepareToScheduleNewGate(end, currentBBGates);
        PrepareToScheduleNewGate(top, currentBBGates);
        PrepareToScheduleNewGate(rawEndAddr, currentBBGates);
        PrepareToScheduleNewGate(rawTopAddr, currentBBGates);
        PrepareToScheduleNewGate(topAddress, currentBBGates);
        PrepareToScheduleNewGate(endAddress, currentBBGates);
        PrepareToScheduleNewGate(addrOffset, currentBBGates);
        PrepareToScheduleNewGate(topAddrAddr, currentBBGates);
        PrepareToScheduleNewGate(endAddrAddr, currentBBGates);
        PrepareToScheduleNewGate(topAddrOffset, currentBBGates);
        PrepareToScheduleNewGate(endAddrOffset, currentBBGates);
        PrepareToScheduleNewGate(hole, currentBBGates);
    }
    builder_.Bind(&success);
    {
        GateRef ifFalse = builder_.GetState();
        GateRef addr = builder_.PtrAdd(topAddress, addrOffset);
        builder_.StoreWithoutBarrier(VariableType::NATIVE_POINTER(), addr, newTop);
        GateRef store = builder_.GetDepend();
        result = top;
        builder_.Jump(&exit);
        {
            GateRef ordinaryBlock = success.GetControl();
            PrepareToScheduleNewGate(ordinaryBlock, successBBGates);
            PrepareToScheduleNewGate(store, successBBGates);
            PrepareToScheduleNewGate(addr, successBBGates);
            PrepareToScheduleNewGate(ifFalse, successBBGates);
        }
    }
    builder_.Bind(&callRuntime);
    {
        GateRef ifTrue = builder_.GetState();
        GateRef taggedIntMask = circuit_->GetConstantGateWithoutCache(
            MachineType::I64, JSTaggedValue::TAG_INT, GateType::NJSValue());
        GateRef taggedSize = builder_.Int64Or(size, taggedIntMask);
        GateRef target = circuit_->GetConstantGateWithoutCache(
            MachineType::ARCH, RTSTUB_ID(AllocateInYoung), GateType::NJSValue());
        const CallSignature *cs = RuntimeStubCSigns::Get(RTSTUB_ID(CallRuntime));
        ASSERT(cs->IsRuntimeStub());
        GateRef reseverdFrameArgs = circuit_->GetConstantGateWithoutCache(MachineType::I64, 0, GateType::NJSValue());
        GateRef reseverdPc = circuit_->GetConstantGateWithoutCache(MachineType::I64, 0, GateType::NJSValue());
        GateRef slowResult = builder_.Call(cs, glue, target, builder_.GetDepend(),
                                           { taggedSize, reseverdFrameArgs, reseverdPc }, Circuit::NullGate(),
                                           "Heap alloc");
        result = slowResult;
        builder_.Jump(&exit);
        {
            GateRef ordinaryBlock = callRuntime.GetControl();
            PrepareToScheduleNewGate(ordinaryBlock, failBBGates);
            PrepareToScheduleNewGate(slowResult, failBBGates);
            PrepareToScheduleNewGate(target, failBBGates);
            PrepareToScheduleNewGate(taggedSize, failBBGates);
            PrepareToScheduleNewGate(reseverdFrameArgs, failBBGates);
            PrepareToScheduleNewGate(reseverdPc, failBBGates);
            PrepareToScheduleNewGate(taggedIntMask, failBBGates);
            PrepareToScheduleNewGate(ifTrue, failBBGates);
        }
    }
    builder_.Bind(&exit);
    {
        GateRef merge = builder_.GetState();
        GateRef phi = *result;
        PrepareToScheduleNewGate(merge, endBBGates);
        PrepareToScheduleNewGate(phi, endBBGates);
    }
    acc_.ReplaceGate(gate, builder_.GetState(), builder_.GetDepend(), *result);
#endif
}

void PostSchedule::LoweringHeapAllocate(GateRef gate,
                                        std::vector<GateRef> &currentBBGates,
                                        std::vector<GateRef> &successBBGates,
                                        std::vector<GateRef> &failBBGates,
                                        std::vector<GateRef> &endBBGates,
                                        int64_t flag)
{
    Environment env(gate, circuit_, &builder_);
    (void)successBBGates;
    (void)failBBGates;
    (void)endBBGates;
    GateRef glue = acc_.GetValueIn(gate, 0);
    GateRef size = acc_.GetValueIn(gate, 1);
    GateRef taggedIntMask = circuit_->GetConstantGateWithoutCache(
        MachineType::I64, JSTaggedValue::TAG_INT, GateType::NJSValue());
    GateRef taggedSize = builder_.Int64Or(size, taggedIntMask);
    GateRef target;
    if (flag == RegionSpaceFlag::IN_YOUNG_SPACE) {
        target = circuit_->GetConstantGateWithoutCache(
            MachineType::ARCH, RTSTUB_ID(AllocateInYoung), GateType::NJSValue());
    } else {
        target = circuit_->GetConstantGateWithoutCache(
            MachineType::ARCH, RTSTUB_ID(AllocateInSOld), GateType::NJSValue());
    }
    const CallSignature *cs = RuntimeStubCSigns::Get(RTSTUB_ID(CallRuntime));
    ASSERT(cs->IsRuntimeStub());
    GateRef reseverdFrameArgs = circuit_->GetConstantGateWithoutCache(MachineType::I64, 0, GateType::NJSValue());
    GateRef reseverdPc = circuit_->GetConstantGateWithoutCache(MachineType::I64, 0, GateType::NJSValue());
    GateRef result = builder_.Call(cs, glue, target, builder_.GetDepend(),
                                   { taggedSize, reseverdFrameArgs, reseverdPc }, Circuit::NullGate(), "Heap alloc");
    acc_.ReplaceGate(gate, builder_.GetState(), builder_.GetDepend(), result);

    // Must keep the order of value-in/depend-in
    PrepareToScheduleNewGate(result, currentBBGates);
    PrepareToScheduleNewGate(target, currentBBGates);
    PrepareToScheduleNewGate(taggedSize, currentBBGates);
    PrepareToScheduleNewGate(reseverdFrameArgs, currentBBGates);
    PrepareToScheduleNewGate(reseverdPc, currentBBGates);
    PrepareToScheduleNewGate(taggedIntMask, currentBBGates);
    return;
}

bool PostSchedule::VisitStore(GateRef gate, ControlFlowGraph &cfg, size_t bbIdx, size_t instIdx)
{
    std::vector<GateRef> currentBBGates;
    std::vector<GateRef> barrierBBGates;
    std::vector<GateRef> endBBGates;
    MemoryOrder::Barrier kind = GetWriteBarrierKind(gate);
    switch (kind) {
        case MemoryOrder::Barrier::UNKNOWN_BARRIER: {
            LoweringStoreUnknownBarrierAndPrepareScheduleGate(gate, currentBBGates, barrierBBGates, endBBGates);
            ReplaceBBState(cfg, bbIdx, currentBBGates, endBBGates);
            ScheduleEndBB(endBBGates, cfg, bbIdx, instIdx);
            ScheduleNewBB(barrierBBGates, cfg, bbIdx);
            ScheduleCurrentBB(currentBBGates, cfg, bbIdx, instIdx);
            return true;
        }
        case MemoryOrder::Barrier::NEED_BARRIER: {
            LoweringStoreWithBarrierAndPrepareScheduleGate(gate, currentBBGates);
            ReplaceGateDirectly(currentBBGates, cfg, bbIdx, instIdx);
            return false;
        }
        case MemoryOrder::Barrier::NO_BARRIER: {
            LoweringStoreNoBarrierAndPrepareScheduleGate(gate, currentBBGates);
            ReplaceGateDirectly(currentBBGates, cfg, bbIdx, instIdx);
            return false;
        }
        default: {
            UNREACHABLE();
            return false;
        }
    }
    return false;
}

MemoryOrder::Barrier PostSchedule::GetWriteBarrierKind(GateRef gate)
{
    MemoryOrder order = acc_.GetMemoryOrder(gate);
    if (!acc_.IsGCRelated(gate)) {
        return MemoryOrder::Barrier::NO_BARRIER;
    }
    return order.GetBarrier();
}

void PostSchedule::LoweringStoreNoBarrierAndPrepareScheduleGate(GateRef gate, std::vector<GateRef> &currentBBGates)
{
    Environment env(gate, circuit_, &builder_);

    GateRef base = acc_.GetValueIn(gate, 1);   // 1: object
    GateRef offset = acc_.GetValueIn(gate, 2); // 2: offset
    GateRef value = acc_.GetValueIn(gate, 3);  // 3: value
    GateRef addr = builder_.PtrAdd(base, offset);
    VariableType type = VariableType(acc_.GetMachineType(gate), acc_.GetGateType(gate));
    builder_.StoreWithoutBarrier(type, addr, value, acc_.GetMemoryOrder(gate));
    GateRef store = builder_.GetDepend();
    {
        PrepareToScheduleNewGate(store, currentBBGates);
        PrepareToScheduleNewGate(addr, currentBBGates);
    }
    acc_.ReplaceGate(gate, builder_.GetState(), builder_.GetDepend(), Circuit::NullGate());
}

void PostSchedule::LoweringStoreWithBarrierAndPrepareScheduleGate(GateRef gate, std::vector<GateRef> &currentBBGates)
{
    Environment env(gate, circuit_, &builder_);

    GateRef glue = acc_.GetValueIn(gate, 0);
    GateRef base = acc_.GetValueIn(gate, 1);   // 1: object
    GateRef offset = acc_.GetValueIn(gate, 2); // 2: offset
    GateRef value = acc_.GetValueIn(gate, 3);  // 3: value
    GateRef addr = builder_.PtrAdd(base, offset);
    VariableType type = VariableType(acc_.GetMachineType(gate), acc_.GetGateType(gate));
    builder_.StoreWithoutBarrier(type, addr, value, acc_.GetMemoryOrder(gate));
    GateRef store = builder_.GetDepend();

    int index = CommonStubCSigns::SetValueWithBarrier;
    const CallSignature *cs = CommonStubCSigns::Get(index);
    ASSERT(cs->IsCommonStub());
    GateRef target = circuit_->GetConstantGateWithoutCache(MachineType::ARCH, index, GateType::NJSValue());
    GateRef reseverdFrameArgs = circuit_->GetConstantGateWithoutCache(MachineType::I64, 0, GateType::NJSValue());
    GateRef reseverdPc = circuit_->GetConstantGateWithoutCache(MachineType::I64, 0, GateType::NJSValue());
    GateRef storeBarrier = builder_.Call(cs, glue, target, builder_.GetDepend(),
                                         { glue, base, offset, value, reseverdFrameArgs, reseverdPc },
                                         Circuit::NullGate(), "store barrier");
    {
        PrepareToScheduleNewGate(storeBarrier, currentBBGates);
        PrepareToScheduleNewGate(reseverdPc, currentBBGates);
        PrepareToScheduleNewGate(reseverdFrameArgs, currentBBGates);
        PrepareToScheduleNewGate(target, currentBBGates);
        PrepareToScheduleNewGate(store, currentBBGates);
        PrepareToScheduleNewGate(addr, currentBBGates);
    }
    acc_.ReplaceGate(gate, builder_.GetState(), builder_.GetDepend(), Circuit::NullGate());
}

void PostSchedule::LoweringStoreUnknownBarrierAndPrepareScheduleGate(GateRef gate,
                                                                     std::vector<GateRef> &currentBBGates,
                                                                     std::vector<GateRef> &barrierBBGates,
                                                                     std::vector<GateRef> &endBBGates)
{
    Environment env(gate, circuit_, &builder_);

    GateRef glue = acc_.GetValueIn(gate, 0);
    GateRef base = acc_.GetValueIn(gate, 1);   // 1: object
    GateRef offset = acc_.GetValueIn(gate, 2); // 2: offset
    GateRef value = acc_.GetValueIn(gate, 3);  // 3: value
    GateRef addr = builder_.PtrAdd(base, offset);
    VariableType type = VariableType(acc_.GetMachineType(gate), acc_.GetGateType(gate));
    builder_.StoreWithoutBarrier(type, addr, value, acc_.GetMemoryOrder(gate));
    GateRef store = builder_.GetDepend();

    GateRef intVal = builder_.ChangeTaggedPointerToInt64(value);
    GateRef objMask = circuit_->GetConstantGateWithoutCache(
        MachineType::I64, JSTaggedValue::TAG_HEAPOBJECT_MASK, GateType::NJSValue());
    GateRef masked = builder_.Int64And(intVal, objMask, GateType::Empty(), "checkHeapObject");
    GateRef falseVal = circuit_->GetConstantGateWithoutCache(MachineType::I64, 0, GateType::NJSValue());
    GateRef condition = builder_.Equal(masked, falseVal, "checkHeapObject");
    Label exit(&builder_);
    Label isHeapObject(&builder_);
    Label *currentLabel = env.GetCurrentLabel();
    BRANCH_CIR(condition, &isHeapObject, &exit);
    {
        GateRef ifBranch = currentLabel->GetControl();
        PrepareToScheduleNewGate(ifBranch, currentBBGates);
        PrepareToScheduleNewGate(condition, currentBBGates);
        PrepareToScheduleNewGate(falseVal, currentBBGates);
        PrepareToScheduleNewGate(masked, currentBBGates);
        PrepareToScheduleNewGate(intVal, currentBBGates);
        PrepareToScheduleNewGate(objMask, currentBBGates);
        PrepareToScheduleNewGate(store, currentBBGates);
        PrepareToScheduleNewGate(addr, currentBBGates);
    }
    GateRef ifTrue = isHeapObject.GetControl();
    GateRef ifFalse = exit.GetControl();
    builder_.Bind(&isHeapObject);
    {
        int index = CommonStubCSigns::SetValueWithBarrier;
        const CallSignature *cs = CommonStubCSigns::Get(index);
        ASSERT(cs->IsCommonStub());
        GateRef target = circuit_->GetConstantGateWithoutCache(MachineType::ARCH, index, GateType::NJSValue());
        GateRef reseverdFrameArgs = circuit_->GetConstantGateWithoutCache(MachineType::I64, 0, GateType::NJSValue());
        GateRef reseverdPc = circuit_->GetConstantGateWithoutCache(MachineType::I64, 0, GateType::NJSValue());
        GateRef storeBarrier = builder_.Call(cs, glue, target, builder_.GetDepend(),
                                             { glue, base, offset, value, reseverdFrameArgs, reseverdPc },
                                             Circuit::NullGate(), "store barrier");
        builder_.Jump(&exit);
        {
            GateRef ordinaryBlock = isHeapObject.GetControl();
            PrepareToScheduleNewGate(ordinaryBlock, barrierBBGates);
            PrepareToScheduleNewGate(storeBarrier, barrierBBGates);
            PrepareToScheduleNewGate(reseverdFrameArgs, barrierBBGates);
            PrepareToScheduleNewGate(reseverdPc, barrierBBGates);
            PrepareToScheduleNewGate(ifTrue, barrierBBGates);
        }
    }
    builder_.Bind(&exit);
    {
        GateRef merge = builder_.GetState();
        PrepareToScheduleNewGate(merge, endBBGates);
        PrepareToScheduleNewGate(ifFalse, endBBGates);
    }
    acc_.ReplaceGate(gate, builder_.GetState(), builder_.GetDepend(), Circuit::NullGate());
}

void PostSchedule::PrintGraph(const char* title, ControlFlowGraph &cfg)
{
    LOG_COMPILER(INFO) << "======================== " << title << " ========================";
    for (size_t bbIdx = 0; bbIdx < cfg.size(); bbIdx++) {
        LOG_COMPILER(INFO) << "B" << bbIdx << ":";
        const std::vector<GateRef>& bb = cfg.at(bbIdx);
        for (size_t instIdx = 0; instIdx < bb.size(); instIdx++) {
            GateRef gate = bb[instIdx];
            acc_.Print(gate);
        }
        LOG_COMPILER(INFO) << "";
    }
}
}  // namespace panda::ecmascript::kungfu
