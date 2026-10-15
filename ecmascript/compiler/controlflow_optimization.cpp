/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
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
#include "ecmascript/compiler/controlflow_optimization.h"
#include "ecmascript/compiler/circuit.h"
#include "ecmascript/compiler/gate.h"
#include "ecmascript/compiler/gate_accessor.h"
#include "ecmascript/compiler/graph_editor.h"
#include "ecmascript/compiler/graph_linearizer.h"
#include "ecmascript/compiler/share_opcodes.h"
#include "ecmascript/log_wrapper.h"
#include "ecmascript/mem/chunk_containers.h"

namespace panda::ecmascript::kungfu {

void ControlFlowOptimization::Initialize()
{
    linearizer_.SetScheduleJSOpcode();
    linearizer_.SetAddBytecode();
    linearizer_.BuildControlflowInfo();
    lastStateGateInRegion_.resize(linearizer_.regionList_.size());
    for (auto region: linearizer_.regionList_) {
        lastStateGateInRegion_[region->GetId()] = region->GetGates().size() - 1;
    }
    linearizer_.Schedule();
    linearizer_.BuildDominanceFrontier();
    linearizer_.PrintGraph("controlflow opt test graph");
    acc_.GetAllGates(gateList_);
    circuit_->AdvanceTime();
    gateUseCount_.resize(circuit_->GetMaxGateId() + 1);
    loopExitLiveBranches_.resize(linearizer_.loops_.size() + 1);
    for (auto &loopInfo: linearizer_.loops_) {
        if (loopInfo.loopExits == nullptr) {
            auto loopBegin = loopInfo.loopHead->GetGates().front();
            ASSERT(acc_.GetOpCode(loopBegin) == OpCode::LOOP_BEGIN);
            LOG_COMPILER(DEBUG) << "mark infinite loop " << acc_.GetId(loopBegin);
            acc_.SetVisited(loopBegin);
            continue;
        }
        for (auto loopExit: *loopInfo.loopExits) {
            auto ifTf = loopExit->GetGates().front();
            loopExitBranchToLoopId_[ifTf] = &loopInfo;
        }
        loopExitLiveBranches_[loopInfo.loopHead->GetLoopNumber()] = Circuit::NullGate();
    }
}

void ControlFlowOptimization::PushUpdateWorkList(GateRef gate, GateRef from)
{
    if (acc_.IsVisited(gate)) {
        return;
    }
    updateWorkList_.push_back(gate);
    LOG_COMPILER(DEBUG) << "update gate " << acc_.GetId(gate) << " from " << acc_.GetId(from);
    acc_.SetVisited(gate);
}

bool ControlFlowOptimization::MayBeEliminated(GateRef gate)
{
    auto opcode = acc_.GetOpCode(gate);
    switch (opcode) {
        case OpCode::IF_BRANCH:
        case OpCode::IF_TRUE:
        case OpCode::IF_FALSE:
        case OpCode::MERGE:
        case OpCode::LOOP_BEGIN:
        case OpCode::LOOP_BACK:
        case OpCode::OVERFLOW_CHECK:
        case OpCode::INT32_UNSIGNED_UPPER_BOUND_CHECK:
        case OpCode::INT32_CHECK_RIGHT_IS_ZERO:
        case OpCode::REMAINDER_IS_NEGATIVE_ZERO:
        case OpCode::VALUE_CHECK_NEG_OVERFLOW:
        case OpCode::INT32_DIV_WITH_CHECK:
        case OpCode::PRODUCT_IS_NEGATIVE_ZERO:
            return true;
        default:
            return false;
    }
}

bool ControlFlowOptimization::IsDependGateWithSideEffect(GateRef gate)
{
    auto opcode = acc_.GetOpCode(gate);
    // not state gate
    if (acc_.IsState(gate)) {
        return false;
    }
    // not value gate
    if (!acc_.GetDependCount(gate)) {
        return false;
    }
    switch (opcode) {
        // hcr
        case OpCode::LOAD_HCLASS_OPCODE:
        // mcr
        case OpCode::LOAD_CONST_OFFSET:
        case OpCode::LOAD_HCLASS_CONST_OFFSET:
        case OpCode::LOAD_HCLASS_FROM_CONSTPOOL:
        // lcr
        case OpCode::LOAD:
        case OpCode::LOAD_WITHOUT_BARRIER:
        // other
        // dependrelay and statesplit should be unmarked at first to eliminate
        // unmarked state gate
        case OpCode::DEPEND_RELAY:
        case OpCode::DEPEND_SELECTOR:
        case OpCode::STATE_SPLIT:
            return false;
        default:
            return true;
    }
}

// mark state use for value gate
void ControlFlowOptimization::UpdateStateUseForValueGate(GateRef gate)
{
    bool isUsedByMod = false;
    auto uses = acc_.Uses(gate);
    for (auto useIt = uses.begin(); useIt != uses.end(); useIt ++) {
        if (!acc_.IsValueIn(useIt) || acc_.IsVisited(*useIt) || !acc_.IsState(*useIt)) {
            continue;
        }
        auto opcode = acc_.GetOpCode(*useIt);
        switch (opcode) {
            // valueIn in index 0 is the result
            case OpCode::OVERFLOW_CHECK:
            case OpCode::VALUE_CHECK_NEG_OVERFLOW:
            case OpCode::INT32_UNSIGNED_UPPER_BOUND_CHECK:
            case OpCode::PRODUCT_IS_NEGATIVE_ZERO:
            case OpCode::IF_BRANCH:
                if (acc_.GetValueIn(*useIt) != gate) {
                    continue;
                }
                updateWorkList_.push_back(*useIt);
                LOG_COMPILER(DEBUG) << "update gate " << acc_.GetId(*useIt) << " from " << acc_.GetId(gate);
                acc_.SetVisited(*useIt);
                break;
            // int32Div has value out
            case OpCode::INT32_DIV_WITH_CHECK:
            default:
                break;
        }
    }
}

// update loop from loopBack branch
void ControlFlowOptimization::UpdateLoopBack(GateRef gate)
{
    auto region = linearizer_.GateToRegion(gate);
    if (region == nullptr) {
        return;
    }
    auto regionId = region->GetId();
    auto lastState = region->GetGates()[lastStateGateInRegion_[regionId]];
    if (acc_.GetOpCode(lastState) == OpCode::LOOP_BACK) {
        auto loopBegin = *acc_.Uses(lastState).begin();
        ASSERT(acc_.GetOpCode(loopBegin) == OpCode::LOOP_BEGIN);
        PushUpdateWorkList(loopBegin, gate);
    }
}

// update loopBack from loopBegin
// The algorithm don't support simplify redundant loopBack, e.g.
//              ifBranch
//              /      \
//          ifTrue    ifFalse
//             |         |
//          loopBack  loopBack
//             \         /
//              loopBegin
void ControlFlowOptimization::UpdateLoopBegin(GateRef gate)
{
    if (acc_.GetOpCode(gate) != OpCode::LOOP_BEGIN) {
        return;
    }
    auto stateCount = acc_.GetStateCount(gate);
    for (size_t i = 0; i < stateCount; ++ i) {
        auto stateIn = acc_.GetState(gate, i);
        if (acc_.GetOpCode(stateIn) != OpCode::LOOP_BACK) {
            continue;
        }
        PushUpdateWorkList(stateIn, gate);
    }
}

void ControlFlowOptimization::UpdateForMod(GateRef gate)
{
    auto opcode = acc_.GetOpCode(gate);
    switch (opcode) {
        case OpCode::SMOD:
        case OpCode::FMOD: {
            auto right = acc_.GetValueIn(gate, 1);
            auto uses = acc_.Uses(right);
            for (auto use: uses) {
                auto useOpcode = acc_.GetOpCode(use);
                if ((useOpcode == OpCode::INT32_CHECK_RIGHT_IS_ZERO
                    || useOpcode == OpCode::REMAINDER_IS_NEGATIVE_ZERO)) {
                    PushUpdateWorkList(use, gate);
                }
            }
            break;
        }
        default:
            break;
    }
}

void ControlFlowOptimization::UpdateDominanceFrontier(GateRef gate)
{
    auto region = linearizer_.GateToRegion(gate);
    if (region == nullptr) {
        return;
    }
    // mark dominance frontiers
    for (auto df: region->GetDominanceFrontiers()) {
        auto regionId = df->GetId();
        auto branch = df->GetGates()[lastStateGateInRegion_[regionId]];
        auto first = df->GetGates().front();
        PushUpdateWorkList(branch, gate);
        PushUpdateWorkList(first, gate);
    }
    // mark loopBegin while multiple loop exit branches have been marked
    auto bbStart = region->GetState();
    if (loopExitBranchToLoopId_.count(bbStart)) {
        auto loopInfo = loopExitBranchToLoopId_[bbStart];
        auto loopId = loopInfo->loopHead->GetLoopNumber();
        if (loopExitLiveBranches_[loopId] == Circuit::NullGate()) {
            loopExitLiveBranches_[loopId] = acc_.GetState(bbStart);
            return;
        }
        // multiple live loop exit branch
        auto loopBegin = loopInfo->loopHead->GetState();
        ASSERT(acc_.GetOpCode(loopBegin) == OpCode::LOOP_BEGIN);
        PushUpdateWorkList(loopBegin, gate);
    }
}

void ControlFlowOptimization::UpdateFromMerge(GateRef gate)
{
    if (acc_.GetOpCode(gate) != OpCode::MERGE) {
        return;
    }
    // update gate from merge to controlflow related gates to mark
    // empty branch with live phis
    auto region = linearizer_.GateToRegion(gate);
    for (auto pred: region->GetPreds()) {
        auto root = pred->GetGates().front();
        auto rootOp = acc_.GetOpCode(root);
        if (rootOp == OpCode::IF_TRUE || rootOp == OpCode::IF_FALSE ||
            rootOp == OpCode::IF_BRANCH || rootOp == OpCode::MERGE) {
            PushUpdateWorkList(root, gate);
        }
    }
}

void ControlFlowOptimization::UpdateGate()
{
    while (!updateWorkList_.empty()) {
        auto gate = updateWorkList_.back();
        updateWorkList_.pop_back();
        auto valueCount = acc_.GetInValueCount(gate);
        for (size_t i = 0; i < valueCount; ++ i) {
            auto gateIn = acc_.GetValueIn(gate, i);
            // deoptCheck may link frame state with value wire
            if (acc_.GetOpCode(gateIn) == OpCode::FRAME_STATE) {
                continue;
            }
            PushUpdateWorkList(gateIn, gate);
        }
        UpdateLoopBack(gate);
        UpdateLoopBegin(gate);
        UpdateDominanceFrontier(gate);
        UpdateFromMerge(gate);
        if (acc_.IsState(gate) || acc_.GetDependCount(gate)) {
            continue;
        }
        UpdateForMod(gate);
        UpdateStateUseForValueGate(gate);
        // try to mark ifBranch through phi to handle empty branch with
        // live value phi, e.g. x < 3 ? a : b
        auto stateCount = acc_.GetStateCount(gate);
        for (size_t i = 0; i < stateCount; ++ i) {
            auto gateIn = acc_.GetState(gate, i);
            PushUpdateWorkList(gateIn, gate);
        }
    }
}

void ControlFlowOptimization::Mark()
{
    // 1. mark all gates that can not be eliminated
    for (auto gate: gateList_) {
        if ((acc_.IsState(gate) && !MayBeEliminated(gate)) ||
            IsDependGateWithSideEffect(gate) ||
            (acc_.IsFrameValues(gate) && graphEditor_.FrameValueUsedInCFGTailoring(gate)) ||
            acc_.GetOpCode(gate) == OpCode::FRAME_ARGS) {
            updateWorkList_.push_back(gate);
            LOG_COMPILER(DEBUG) << "mark init " << acc_.GetId(gate);
            acc_.SetVisited(gate);
        }

        auto stateInCount = acc_.GetStateCount(gate);
        if (!acc_.IsState(gate) || stateInCount == 0) {
            continue;
        }
        for (size_t i = 0; i < stateInCount; ++ i) {
            auto stateIn = acc_.GetState(gate, i);
            if (acc_.GetOpCode(stateIn) == OpCode::LOOP_BACK) {
                continue;
            }
            gateUseCount_[acc_.GetId(stateIn)] ++;
        }
    }

    // 2. mark all gates in the graph starting from marked gate
    UpdateGate();
}

void ControlFlowOptimization::TryEliminateMerge(GateRef gate)
{
    LOG_COMPILER(DEBUG) << "Try eliminate merge gate:" << acc_.GetId(gate);
    auto stateCount = acc_.GetStateCount(gate);
    if (stateCount > 1) {
        return;
    }

    auto liveIn = acc_.GetState(gate);
    auto uses = acc_.Uses(gate);
    for (auto useIt = uses.begin(); useIt != uses.end();) {
        auto useOp = acc_.GetOpCode(*useIt);
        if (useOp == OpCode::STATE_SPLIT) {
            acc_.ReplaceGate(*useIt, circuit_->DeadGate());
            useIt ++;
        } else if (acc_.IsState(*useIt)) {
            useIt = acc_.ReplaceIn(useIt, liveIn);
        } else {
            useIt ++;
        }
    }
    acc_.DeleteGate(gate);
}

void ControlFlowOptimization::EliminatePhi(GateRef gate)
{
    ASSERT(acc_.GetNumIns(gate) == 2); // 2: state in and value/depend in
    if (acc_.IsDependSelector(gate)) {
        auto dependIn = acc_.GetDep(gate);
        auto uses = acc_.Uses(gate);
        for (auto useIt = uses.begin(); useIt != uses.end();) {
            useIt = acc_.ReplaceIn(useIt, dependIn);
        }
    } else if (acc_.IsValueSelector(gate)) {
        auto valueIn = acc_.GetValueIn(gate);
        auto uses = acc_.Uses(gate);
        for (auto useIt = uses.begin(); useIt != uses.end();) {
            useIt = acc_.ReplaceIn(useIt, valueIn);
        }
    }
    acc_.DeleteGate(gate);
}

void ControlFlowOptimization::DecreaseMergeIn(GateRef gate, size_t idx)
{
    ASSERT(acc_.GetOpCode(gate) == OpCode::MERGE);
    LOG_COMPILER(DEBUG) << "decreaseIn for gate " << acc_.GetId(gate) << " edge index:" << idx;
    acc_.DecreaseIn(gate, idx);
    // The number of merge input should be the same with phis
    auto mergeUses = acc_.Uses(gate);
    for (auto mergeUseIt = mergeUses.begin(); mergeUseIt != mergeUses.end();) {
        // input of marked phi can not be decreased
        if (!acc_.IsSelector(*mergeUseIt)) {
            mergeUseIt ++;
            continue;
        }
        acc_.DecreaseIn(*mergeUseIt, idx + 1); // 1: for state
        if (acc_.GetNumIns(*mergeUseIt) == 2) { // 2: state in and value/depend in
            EliminatePhi(*mergeUseIt);
        } else {
            mergeUseIt ++;
        }
    }
}

void ControlFlowOptimization::EliminateIfTF(GateRef gate, bool keepEdge)
{
    auto ifUses = acc_.Uses(gate);
    auto succGate = Circuit::NullGate();
    size_t stateIndex = -1;
    for (auto ifUseIt = ifUses.begin(); ifUseIt != ifUses.end(); ifUseIt ++) {
        auto ifUse = *ifUseIt;
        if (!acc_.IsState(ifUse)) {
            // delete DependRelay
            LOG_COMPILER(DEBUG) << "in eliminate IfTF, gate:" << acc_.GetId(gate);
            LOG_COMPILER(DEBUG) << "now ifUse:" << acc_.GetId(ifUse);
            acc_.ReplaceGate(ifUse, circuit_->DeadGate());
        } else {
            // record state use
            succGate = ifUse;
            stateIndex = ifUseIt.GetIndex();
        }
    }
    // delete IfTrue/IfFalse
    if (keepEdge) {
        acc_.ReplaceGate(gate, circuit_->DeadGate());
    } else if (succGate == Circuit::NullGate()) {
        acc_.DeleteGate(gate);
    } else if (acc_.GetOpCode(succGate) == OpCode::LOOP_BACK) {
        // some branches may connect to loopBack, e.g. aottest/optimization
        auto loopBeginIn = acc_.Uses(succGate).begin();
        auto loopBegin = *loopBeginIn;
        auto idx = loopBeginIn.GetIndex();
        acc_.DecreaseIn(loopBeginIn);
        auto beginUses = acc_.Uses(loopBegin);
        for (auto beginUseIt = beginUses.begin(); beginUseIt != beginUses.end();) {
            if (!acc_.IsSelector(*beginUseIt)) {
                beginUseIt ++;
                continue;
            }
            acc_.DecreaseIn(*beginUseIt, idx + 1); // 1: for state
            if (acc_.GetNumIns(*beginUseIt) == 2) { // 2: state in and value/depend in
                EliminatePhi(*beginUseIt);
            } else {
                beginUseIt ++;
            }
        }
        acc_.DeleteGate(succGate);
        acc_.DeleteGate(gate);
    } else if (acc_.GetOpCode(succGate) == OpCode::MERGE) {
        DecreaseMergeIn(succGate, stateIndex);
        TryEliminateMerge(succGate);
        acc_.DeleteGate(gate);
    } else {
        acc_.DeleteGate(gate);
    }
}

void ControlFlowOptimization::EliminateBranch(GateRef gate)
{
    auto uses = acc_.Uses(gate);
    for (auto use: uses) {
        // loop exit branch is deleted with loop
        if (loopExitBranchToLoopId_.count(use)) {
            return;
        }
    }
    // keep one of the edge
    bool keepEdge = true;
    for (auto use: uses) {
        EliminateIfTF(use, keepEdge);
        keepEdge = false;
    }
    // delete IfBranch
    acc_.ReplaceGate(gate, circuit_->DeadGate());
    LOG_COMPILER(DEBUG) << "eliminate branch " << acc_.GetId(gate) << " successfully";
    return;
}

void ControlFlowOptimization::EliminateLoopBranch(GateRef gate)
{
    auto stateIn = acc_.GetState(gate);
    auto uses = acc_.Uses(gate);
    bool isLive = false;
    std::vector<GateRef> ifTF;
    for (auto useIt = uses.begin(); useIt != uses.end();) {
        ifTF.push_back(*useIt);
        if (!loopExitBranchToLoopId_.count(*useIt)) {
            useIt ++;
            continue;
        }
        auto loopId = loopExitBranchToLoopId_[*useIt]->loopHead->GetLoopNumber();
        auto &liveBranch = loopExitLiveBranches_[loopId];
        if (liveBranch != gate && liveBranch != Circuit::NullGate()) {
            // the loop has another live exit branch
            useIt ++;
            continue;
        }
        liveBranch = gate;
        isLive = true;
        useIt = acc_.ReplaceIn(useIt, stateIn);
    }
    acc_.DeleteGate(gate);
    for (auto ifGate: ifTF) {
        // keep depend wire of loop exit branch
        EliminateIfTF(ifGate, isLive && loopExitBranchToLoopId_.count(ifGate));
    }
}

void ControlFlowOptimization::EliminateUnmarkedGateInLoop(GateRef gate)
{
    auto nowOp = acc_.GetOpCode(gate);
    switch (nowOp) {
        case OpCode::LOOP_BACK:
            acc_.DeleteGate(gate);
            break;
        case OpCode::IF_TRUE:
        case OpCode::IF_FALSE:
        case OpCode::MERGE:
            break;
        case OpCode::IF_BRANCH:
            EliminateLoopBranch(gate);
            break;
        default:
            LOG_COMPILER(DEBUG) << "unexpected gate:" << acc_.GetId(gate);
            UNREACHABLE();
            break;
    }
}

void ControlFlowOptimization::EliminateLoop(GateRef loopBegin)
{
    auto region = linearizer_.GateToRegion(loopBegin);
    auto loopBeginUses = acc_.Uses(loopBegin);
    auto stateCount = acc_.GetStateCount(loopBegin);
    auto stateIn = Circuit::NullGate();
    std::vector<GateRef> loopBacks;
    for (size_t idx = 0; idx < stateCount; ++ idx) {
        auto gateIn = acc_.GetIn(loopBegin, idx);
        auto opcodeIn = acc_.GetOpCode(gateIn);
        if (opcodeIn == OpCode::LOOP_BACK) {
            loopBacks.push_back(gateIn);
        } else {
            stateIn = gateIn;
        }
    }

    for (auto loopBack: loopBacks) {
        GateRef curGate = loopBack;
        while (curGate != loopBegin && acc_.GetState(curGate) > 0) {
            auto nextGate = acc_.GetState(curGate);
            EliminateUnmarkedGateInLoop(curGate);
            curGate = nextGate;
        }
    }
    for (auto useIt = loopBeginUses.begin(); useIt != loopBeginUses.end();) {
        if (acc_.IsState(*useIt)) {
            useIt = acc_.ReplaceIn(useIt, stateIn);
        } else {
            useIt ++;
        }
    }
    LOG_COMPILER(DEBUG) << "eliminate loop " << acc_.GetId(loopBegin) << " successfully";
    acc_.DeleteGate(loopBegin);
}

bool ControlFlowOptimization::AlwaysLiveValue(GateRef gate)
{
    auto opcode = acc_.GetOpCode(gate);
        switch (opcode) {
            case OpCode::FRAME_ARGS:
            case OpCode::ARG:
            case OpCode::CIRCUIT_ROOT:
            case OpCode::STATE_ENTRY:
            case OpCode::DEPEND_ENTRY:
            case OpCode::FRAME_STATE:
            case OpCode::FRAME_VALUES:
            case OpCode::CONSTANT:
            case OpCode::RETURN_LIST:
            case OpCode::ARG_LIST:
            case OpCode::GET_SHARED_CONSTPOOL:
            case OpCode::GET_UNSHARED_CONSTPOOL:
                return true;
            default:
                return false;
        }
}

void ControlFlowOptimization::EliminateLastValueOrDepend()
{
    std::vector<GateRef> dependPhis;
    for (auto gate: gateList_) {
        if (AlwaysLiveValue(gate) || acc_.IsNop(gate) ||
            acc_.IsState(gate) || acc_.IsVisited(gate)) {
            continue;
        }
        // exclude depend relay and state split
        if (acc_.GetDependCount(gate) && acc_.GetStateCount(gate) &&
            !acc_.IsNop(acc_.GetState(gate))) {
            continue;
        }

        auto opcode = acc_.GetOpCode(gate);
        if (acc_.IsDependSelector(gate)) {
            dependPhis.push_back(gate);
            continue;
        } else if (acc_.GetDependCount(gate)) { // is depend gate
            ASSERT(acc_.GetDependCount(gate) == 1);
            LOG_COMPILER(DEBUG) << "delete unmarked gate " << acc_.GetId(gate);
            acc_.ReplaceGate(gate, optimizedGate_);
        } else { // is value gate
            LOG_COMPILER(DEBUG) << "delete unmarked gate " << acc_.GetId(gate);
            acc_.UpdateAllUses(gate, optimizedGate_);
            acc_.DeleteGate(gate);
        }
    }
    for (auto phi: dependPhis) {
        size_t depCount = acc_.GetDependCount(phi);
        GateRef dependIn = Circuit::NullGate();
        // At this time, all depend phi's input have been eliminated.
        // Remaining depend inputs will connect to a live gate or itself.
        for (size_t i = 0; i < depCount; ++ i) {
            auto gateIn = acc_.GetDep(phi, i);
            if (gateIn != phi) {
                dependIn = gateIn;
                break;
            }
        }
        auto phiUses = acc_.Uses(phi);
        for (auto useIt = phiUses.begin(); useIt != phiUses.end();) {
            useIt = acc_.ReplaceIn(useIt, dependIn);
        }
        acc_.DeleteGate(phi);
    }
}

void ControlFlowOptimization::EliminateUnmarkedGate(GateRef gate)
{
    auto opcode = acc_.GetOpCode(gate);
    switch (opcode) {
        case OpCode::IF_BRANCH:
            EliminateBranch(gate);
            // std::cout << "eliminate branch" << std::endl;
            break;
        case OpCode::LOOP_BEGIN:
            EliminateLoop(gate);
            // std::cout << "eliminate loop" << std::endl;
            break;
        case OpCode::INT32_DIV_WITH_CHECK: {
            auto uses = acc_.Uses(gate);
            for (auto useIt = uses.begin(); useIt != uses.end();) {
                if (!acc_.IsValueIn(useIt)) {
                    useIt ++;
                    continue;
                }
                useIt = acc_.ReplaceIn(useIt, optimizedGate_);
            }
            [[fallthrough]];
        }
        case OpCode::OVERFLOW_CHECK:
        case OpCode::INT32_UNSIGNED_UPPER_BOUND_CHECK:
        case OpCode::INT32_CHECK_RIGHT_IS_ZERO:
        case OpCode::REMAINDER_IS_NEGATIVE_ZERO:
        case OpCode::VALUE_CHECK_NEG_OVERFLOW:
        case OpCode::PRODUCT_IS_NEGATIVE_ZERO: {
            acc_.ReplaceGate(gate, circuit_->DeadGate());
            // std::cout << "eliminate check" << std::endl;
            break;
        }
        default:
            break;
    }
}

void ControlFlowOptimization::TryEliminateUnmarkedGate()
{
    for (auto gate: gateList_) {
        auto gateId = acc_.GetId(gate);
        if (acc_.IsState(gate) && gateUseCount_[gateId] == 0) {
            workList_.push_back(gate);
        }
    }

    while (!workList_.empty()) {
        auto gate = workList_.back();
        auto opcode = acc_.GetOpCode(gate);
        LOG_COMPILER(DEBUG) << "maybe unmarked gate:" << acc_.GetId(gate);
        workList_.pop_back();
        auto stateCount = acc_.GetStateCount(gate);
        for (size_t idx = 0; idx < stateCount; ++ idx) {
            auto stateIn = acc_.GetState(gate, idx);
            auto stateInOp = acc_.GetOpCode(stateIn);
            if (acc_.IsNop(stateIn) || stateInOp == OpCode::LOOP_BACK) {
                // loopBegin -> loopBack
                continue;
            }
            LOG_COMPILER(DEBUG) << "stateIn:" << acc_.GetId(stateIn) << " gate:" << acc_.GetId(gate);
            auto stateInId = acc_.GetId(stateIn);
            ASSERT(gateUseCount_[stateInId] > 0);
            gateUseCount_[stateInId] --;
            if (gateUseCount_[stateInId] == 0) {
                workList_.push_back(stateIn);
                LOG_COMPILER(DEBUG) << "push gate " << acc_.GetId(stateIn);
            }
        }
        if (acc_.IsVisited(gate)) {
            continue;
        }
        LOG_COMPILER(DEBUG) << "gate not visited, try eliminate";
        EliminateUnmarkedGate(gate);
    }

    // delete last value or depend gate with no side effects
    EliminateLastValueOrDepend();
}

std::string ControlFlowOptimization::GetMethodName()
{
    return methodName_;
}

void ControlFlowOptimization::Run()
{
    Initialize();
    Mark();
    TryEliminateUnmarkedGate();

    if (enableLog_) {
        LOG_COMPILER(INFO) << "";
        LOG_COMPILER(INFO) << "\033[34m"
                           << "===================="
                           << " After ControlFlow Optimization "
                           << "[" << GetMethodName() << "]"
                           << "===================="
                           << "\033[0m";
        circuit_->PrintAllGatesWithBytecode();
        LOG_COMPILER(INFO) << "\033[34m" << "========================= End ==========================" << "\033[0m";
    }
}

};
