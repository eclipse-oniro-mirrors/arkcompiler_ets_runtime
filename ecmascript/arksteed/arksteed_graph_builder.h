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

#ifndef ECMASCRIPT_ARKSTEED_GRAPH_BUILDER_NEW_H
#define ECMASCRIPT_ARKSTEED_GRAPH_BUILDER_NEW_H

#include "ecmascript/arksteed/arksteed_bb.h"
#include "ecmascript/arksteed/arksteed_bc_frame_state.h"
#include "ecmascript/arksteed/arksteed_bytecode_analysis.h"
#include "ecmascript/arksteed/arksteed_bytecode_preprocessor.h"
#include "ecmascript/arksteed/arksteed_pgo_context.h"
#include "ecmascript/compiler/common_stub_csigns.h"

namespace panda::ecmascript::arksteed {
class CompileInfoFacts;
class Graph;
class LoopEffects;
struct SideEffectDescriptor;

class GraphBuilder {
public:
    GraphBuilder(JSThread *compilerThread,
                 Graph *destGraph,
                 uintptr_t glueAddr,
                 BytecodePreprocessor *preproc,
                 BytecodeAnalysis *analysis,
                 bool isOsr);

    bool Run();

private:
    using BasicBlockInfo = BytecodePreprocessor::BasicBlockInfo;
    using BytecodeLoopInfo = BytecodePreprocessor::BytecodeLoopInfo;
    using CommonStubID = kungfu::CommonStubCSigns::ID;
    using RuntimeStubID = kungfu::RuntimeStubCSigns::ID;

    struct CatchBlockInputData;
    struct BytecodeVisitor;

    enum class LoopPeelingStage : uint8_t {
        NONE,
        PEELED_ITERATION,
        OPTIMISTIC_LOOP,
        CONSERVATIVE_LOOP,
    };

    struct LoopExitEdge {
        uint32_t sourceRpoIndex;
        uint32_t targetRpoIndex;
        BB *landing;
        BB *target;
    };

    struct LoopEffectScope {
        const BytecodeLoopInfo *source;
        LoopEffects *effects;
    };

    struct OutgoingBlockState {
        CondensedBCFrameState frameState;
        CompileInfoFacts *facts {nullptr};
    };

    VRegIDType LexicalEnvIndex() const
    {
        return VRegOfLexicalEnv(numLocal_, numParams_);
    }
    VRegIDType AccIndex() const
    {
        return VRegOfAcc(numLocal_, numParams_);
    }

    JSRuntimeOptions *GetOptions() const
    {
        return &preproc_->GetEnv()->GetJSOptions();
    }

    bool IsLazyDeoptEnabled() const
    {
        return GetOptions()->IsEnableJitLazyDeopt();
    }

    void DebugLog();
    void InitializeStartBlock(SharedBCFrameState frameState);
    void ProcessDeadBasicBlock(uint32_t rpoIndex);
    void ProcessBasicBlock(SharedBCFrameState frameState, uint32_t rpoIndex);
    void ProcessCatchBlockHead(SharedBCFrameState frameState, uint32_t rpoIndex);
    void FinishDeadLoopBackEdge(BB *owner, uint32_t rpoIndex);
    BB *VisitBytecodesOfBasicBlock(SharedBCFrameState frameState, uint32_t rpoIndex,
                                   CompileInfoFacts **outFacts);

    void InitializeLoopPeeling();
    bool IsEligibleForLoopPeeling(const BytecodeLoopInfo *loop) const;
    void BuildPeeledLoop(const BytecodeLoopInfo *loop);
    void ResetLoopBuildState(const BytecodeLoopInfo *loop);
    void FinishLoopBackedge(BB *owner, SharedBCFrameState frameState, CompileInfoFacts *facts,
                            uint32_t targetRpoIndex);
    bool IsOptimisticBackedgeCompatible(SharedBCFrameState frameState, CompileInfoFacts *facts) const;
    void TurnOptimisticLoopIntoPeeledIteration();
    void SynchronizeLoopEffects(const BytecodeLoopInfo *loop);
    void BeginLoopEffects(const BytecodeLoopInfo *loop);
    LoopEffects *EndLoopEffects();
    void FinalizeLoopExitEdges(uint32_t sourceRpoIndex);
    BB *GetOrCreateLoopExitLanding(uint32_t sourceRpoIndex, uint32_t targetRpoIndex, BB *target);

    void InitFrameState(SharedBCFrameState framestate, uint32_t rpoIndex);
    void InitFrameStateForLoopHeader(SharedBCFrameState framestate, uint32_t rpoIndex);
    void InitFrameStateForCatchBlockHeader(SharedBCFrameState framestate, uint32_t rpoIndex);

    void InitCompileInfoFacts(uint32_t rpoIndex);
    void InitCompileInfoFactsForCatchBlock(uint32_t rpoIndex);
    void WriteBackFrameStateToLoopHeader(SharedBCFrameState current, uint32_t rpoIndex);
    void MergeFrameState(SharedBCFrameState dest, uint32_t rpoIndex, const OutgoingBlockState &incoming,
                         uint32_t actualPredIndex, uint32_t actualNumPreds);
    void RecordOutgoingBlockState(uint32_t rpoIndex, SharedBCFrameState frameState);

    PhiVertex *NewPhiVertex(BB *owner, uint32_t numPredecessors, VRegIDType vreg);
    template <class InputRange = std::initializer_list<ValueVertex *>>
    PhiVertex *NewPhiVertexWith(BB *owner, const InputRange &inputs, VRegIDType vreg);

    // VertexT should be neither control vertex nor Phi
    template <class VertexT, class InputRange = std::initializer_list<ValueVertex *>, class... Args>
    VertexT *NewVertex(BB *owner, const InputRange &inputs, Args &&...args);

    template <class VertexT, class InputRange = std::initializer_list<ValueVertex *>, class... Args>
    VertexT *NewVertex(CompileInfoFacts *compileInfoFacts, BB *owner, const InputRange &inputs, Args &&...args);

    template <class VertexT, class InputRange = std::initializer_list<ValueVertex *>, class... Args>
    VertexT *NewVertexImpl(BB *owner, const InputRange &inputs, Args &&...args);

    JumpVertex *FinishBlockWithJump(BB *owner, BB *target);
    JumpLoopVertex *FinishBlockWithJumpLoop(BB *owner, BB *target);

    [[deprecated("Use FinishBlockWithBranch<BranchVertexT>() instead")]]
    ControlVertex *FinishBlockWithBranch(BB *owner, ValueVertex *input, BB *targetIfTrue, BB *targetIfFalse);

    template <class BranchVertexT, class InputRange = std::initializer_list<ValueVertex *>, class... Args>
    BranchVertexT *FinishBlockWithBranch(BB *owner, const InputRange &inputs, BB *targetIfTrue, BB *targetIfFalse,
                                         Args &&...args);

    // VertexT should be control vertex
    template <class VertexT, class InputRange = std::initializer_list<ValueVertex *>, class... Args>
    VertexT *FinishBlockWith(BB *owner, const InputRange &inputs, Args &&...args);

    BB *NewBlock();
    LoopEffects *CurrentLoopEffects();
    BB *ActivateNonCatchBlock(uint32_t rpoIndex);
    BB *ActivateCatchBlock(CatchBlockInputData **inputData, uint32_t rpoIndex);
    LoadTaggedFieldVertex *ActivateGlobalEnv();

    BB *&BlockAt(uint32_t rpoIndex);
    BB *&ExitBlockAt(uint32_t rpoIndex);
    CompileInfoFacts *&CompileInfoFactsAt(uint32_t rpoIndex);
    CatchBlockInputData *&CatchBlockInputsAt(uint32_t rpoIndex);
    const OutgoingBlockState &GetOutgoingBlockState(BB *block) const;

    Graph *graph_;
    JSThread *compilerThread_;
    uintptr_t glueAddr_;
    BytecodePreprocessor *preproc_;
    BytecodeAnalysis *analysis_;

    ArkSteedPGOContext pgoContext_;

    // Frequently used fields. Cached for performance.
    uint32_t numLocal_;   // Equivalent to preproc_->GetNumLocalRegs()
    uint32_t numParams_;  // Equivalent to preproc_->GetNumParamRegs()
    bool isOsr_;
    Chunk *chunk_;        // Equivalent to preproc_->GetChunk()

    // Constants frequently used
    ValueVertex *glue_ = nullptr;
    ValueVertex *undefinedValue_ = nullptr;
    InitialValueVertex *initialActualArgc_ = nullptr;
    ValueVertex *actualArgc_ = nullptr;
    ValueVertex *taggedActualArgc_ = nullptr;
    InitialValueVertex *initialCallTarget_ = nullptr;
    InitialValueVertex *initialLexicalEnv_ = nullptr;
    LoadTaggedFieldVertex *lazyGlobalEnv_ = nullptr;

    ChunkVector<BB *> blocks_;
    // Entry blocks are stable CFG targets; exit blocks track the final block after any subgraph lowering.
    ChunkVector<BB *> exitBlocks_;
    ChunkVector<CompileInfoFacts *> compileInfoFacts_;
    ChunkVector<CatchBlockInputData *> catchBlockInputs_;
    ChunkMap<BB *, OutgoingBlockState> outgoingBlockStates_;
    ChunkVector<uint8_t> loopHeadersToPeel_;
    ChunkVector<uint8_t> handledSourceBlocks_;
    ChunkVector<LoopEffectScope> loopEffectsStack_;
    ChunkVector<LoopExitEdge> loopExitEdges_;
    const BytecodeLoopInfo *activePeeledLoop_ {nullptr};
    CompileInfoFacts *optimisticHeaderFacts_ {nullptr};
    LoopEffects *peeledLoopEffects_ {nullptr};
    BB *nextLoopHeader_ {nullptr};
    LoopPeelingStage loopPeelingStage_ {LoopPeelingStage::NONE};
    uint32_t currentSourceRpoIndex_ {BytecodePreprocessor::NULL_INDEX};
};
}  // namespace panda::ecmascript::arksteed

#endif  // ECMASCRIPT_ARKSTEED_GRAPH_BUILDER_NEW_H
