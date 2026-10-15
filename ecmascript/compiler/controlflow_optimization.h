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
#ifndef ECMASCRIPT_CONTROLFLOW_OPTIMIZATION_H
#define ECMASCRIPT_CONTROLFLOW_OPTIMIZATION_H

#include "ecmascript/compiler/gate.h"
#include "ecmascript/compiler/gate_accessor.h"
#include "ecmascript/compiler/graph_editor.h"
#include "ecmascript/compiler/graph_linearizer.h"
#include "ecmascript/compiler/lcr_gate_meta_data.h"
#include "ecmascript/compiler/share_gate_meta_data.h"
#include "ecmascript/mem/chunk_containers.h"

namespace panda::ecmascript::kungfu {

class ControlFlowOptimization {
public:
    ControlFlowOptimization(Circuit *circuit, Chunk *chunk, bool enableLog, const std::string &name): acc_(circuit),
        linearizer_(circuit, enableLog, name, chunk, false, true),
        graphEditor_(circuit), circuit_(circuit), enableLog_(enableLog), methodName_(name),
        workList_(chunk), updateWorkList_(chunk), loopExitBranchToLoopId_(chunk), loopExitLiveBranches_(chunk),
        gateUseCount_(chunk), lastStateGateInRegion_(chunk) {
            optimizedGate_ = circuit_->GetConstantGate(MachineType::I64,
                JSTaggedValue::VALUE_OPTIMIZED_OUT,
                GateType::TaggedValue());
        }

    void Run();
private:
    GateAccessor acc_;
    GraphLinearizer linearizer_;
    GraphEditor graphEditor_;
    Circuit *circuit_;
    bool enableLog_;
    std::string methodName_;
    ChunkDeque<GateRef> workList_;
    ChunkDeque<GateRef> updateWorkList_;
    std::vector<GateRef> gateList_;
    ChunkMap<GateRef, GraphLinearizer::LoopInfo *> loopExitBranchToLoopId_;
    ChunkVector<GateRef> loopExitLiveBranches_;
    ChunkVector<GateRef> gateUseCount_;
    ChunkVector<GateRef> lastStateGateInRegion_;

    GateRef optimizedGate_;

    void Initialize();
    void Mark();
    void UpdateGate();
    void UpdateLoopBack(GateRef gate);
    void UpdateLoopBegin(GateRef gate);
    void UpdateDominanceFrontier(GateRef gate);
    void UpdateStateUseForValueGate(GateRef gate);
    void UpdateRegionRoot(GateRef gate);
    void UpdateForMod(GateRef gate);
    void UpdateFromMerge(GateRef gate);
    void TryEliminateUnmarkedGate();
    void EliminateUnmarkedGate(GateRef gate);
    bool MayBeEliminated(GateRef gate);
    void EliminateBranch(GateRef gate);
    void TryEliminateMerge(GateRef gate);
    void EliminateLoop(GateRef loopBegin);
    void EliminateIfTF(GateRef gate, bool keepEdge = true);
    void EliminateLoopBranch(GateRef gate);
    void EliminatePhi(GateRef gate);
    void DecreaseMergeIn(GateRef gate, size_t idx);
    bool IsDependGateWithSideEffect(GateRef gate);
    void EliminateLastValueOrDepend();
    void EliminateUnmarkedGateInLoop(GateRef gate);
    bool AlwaysLiveValue(GateRef gate);
    void PushUpdateWorkList(GateRef gate, [[maybe_unused]]GateRef from);
    std::string GetMethodName();
};

};

#endif
