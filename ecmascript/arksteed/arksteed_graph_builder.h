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

#include <cstdint>
#include <map>
#include <tuple>

#include "ecmascript/arksteed/arksteed_bb.h"
#include "ecmascript/arksteed/arksteed_bc_frame_state.h"
#include "ecmascript/arksteed/arksteed_bytecode_analysis.h"
#include "ecmascript/arksteed/arksteed_bytecode_preprocessor.h"
#include "ecmascript/arksteed/arksteed_inline_frame_state.h"
#include "ecmascript/arksteed/arksteed_pgo_context.h"
#include "ecmascript/compiler/common_stub_csigns.h"

namespace panda::ecmascript::arksteed {
class CompileInfoFacts;
class Graph;
class ArkSteedCompilationUnit;
class LoopEffects;
struct SideEffectDescriptor;


class GraphBuilder {
public:
    struct InlineBuildResult {
        BB *exitBlock {nullptr};
        ValueVertex *returnValue {nullptr};
        CompileInfoFacts *facts {nullptr};
        bool hasNormalReturn {false};
    };

    GraphBuilder(JSThread *compilerThread,
                 Graph *destGraph,
                 uintptr_t glueAddr,
                 BytecodePreprocessor *preproc,
                 BytecodeAnalysis *analysis,
                 bool isOsr,
                 const ArkSteedInlinedCallerFrame *parentDeoptFrame = nullptr);

    bool Run();
    bool RunInline(BB *callerBlock,
                   CompileInfoFacts *callerFacts,
                   const ChunkVector<ValueVertex *> &arguments,
                   uint32_t actualArgc,
                   InlineBuildResult *result);

private:
    using BasicBlockInfo = BytecodePreprocessor::BasicBlockInfo;
    using BytecodeLoopInfo = BytecodePreprocessor::BytecodeLoopInfo;
    using CommonStubID = kungfu::CommonStubCSigns::ID;
    using RuntimeStubID = kungfu::RuntimeStubCSigns::ID;

    struct CatchBlockInputData;
    struct InheritedCatchContext;
    struct LocalClosureInfo;
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
    void BuildBody(SharedBCFrameState frameState);
    void InitializeStartBlock(SharedBCFrameState frameState);
    void InitializeInlineStartBlock(SharedBCFrameState frameState,
                                    BB *callerBlock,
                                    CompileInfoFacts *callerFacts,
                                    const ChunkVector<ValueVertex *> &arguments,
                                    uint32_t actualArgc);
    void FinishInlineReturn(BB *returnBlock, ValueVertex *returnValue, CompileInfoFacts *returnFacts);
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

    // Populate Vertex bytecode offset: mixin-provided value takes priority; vertices without a
    // mixin offset fall back to the offset of the bytecode currently being processed, so that
    // every vertex emitted by codegen carries an offset for SourcePositionTable boundaries.
    template <class VertexT>
    void PopulateVertexBytecodeOffset(VertexT *vertex);

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
    const ArkSteedInlinedCallerFrame *parentDeoptFrame_ {nullptr};

    ArkSteedPGOContext pgoContext_;

    // Frequently used fields. Cached for performance.
    uint32_t numLocal_;   // Equivalent to preproc_->GetNumLocalRegs()
    uint32_t numParams_;  // Equivalent to preproc_->GetNumParamRegs()
    bool isOsr_;
    Chunk *chunk_;        // Equivalent to preproc_->GetChunk()

    // Constants frequently used
    ValueVertex *glue_ = nullptr;
    ValueVertex *undefinedValue_ = nullptr;
    ValueVertex *initialActualArgc_ = nullptr;
    ValueVertex *actualArgc_ = nullptr;
    ValueVertex *taggedActualArgc_ = nullptr;
    ValueVertex *initialCallTarget_ = nullptr;
    // Function whose body this builder is building, used as the caller argument of
    // the inline-entry runtime trace. Null on the root builder (initialCallTarget_
    // is used instead); set from the parent's call target for inlinee builders.
    ValueVertex *inlineTraceCallerVertex_ = nullptr;
    ValueVertex *initialLexicalEnv_ = nullptr;
    LoadTaggedFieldVertex *lazyGlobalEnv_ = nullptr;

    ChunkVector<BB *> blocks_;
    // Entry blocks are stable CFG targets; exit blocks track the final block after any subgraph lowering.
    ChunkVector<BB *> exitBlocks_;
    ChunkVector<CompileInfoFacts *> compileInfoFacts_;
    ChunkVector<CatchBlockInputData *> catchBlockInputs_;
    ChunkMap<ValueVertex *, LocalClosureInfo *> localClosureInfos_;
    ChunkVector<ValueVertex *> inlineActualArguments_;
    uint32_t inlineBytecodeBudgetUsedStorage_ {0};
    uint32_t inlineBytecodeBudgetUsedSmallStorage_ {0};
    uint32_t *inlineBytecodeBudgetUsed_ {&inlineBytecodeBudgetUsedStorage_};
    uint32_t *inlineBytecodeBudgetUsedSmall_ {&inlineBytecodeBudgetUsedSmallStorage_};

    // Inline preparation cache: when the same callee is inlined repeatedly under the
    // same caller unit and the same profile, the finished compilation unit, preprocessor
    // and analysis are shared (all three are pure functions of the callee).
    // Site-level checks (depth/budget/graph node count) still run on every inline.
    // Child GraphBuilders share the root builder's cache by pointer (same as
    // inlineBytecodeBudgetUsed_).
    struct InlinePrepCacheEntry {
        ArkSteedCompilationUnit *unit;
        BytecodePreprocessor *preprocessor;
        BytecodeAnalysis *analysis;
        uint32_t bytecodeSize;
    };
    using InlinePrepCacheKey = std::tuple<uint32_t, uint64_t, const ArkSteedCompilationUnit *>;
    using InlinePrepCache = std::map<InlinePrepCacheKey, InlinePrepCacheEntry>;
    InlinePrepCache inlinePrepCacheStorage_;
    InlinePrepCache *inlinePrepCache_ {&inlinePrepCacheStorage_};

    // An inlined callee without a local try block may still throw into a catch
    // block owned by an outer compilation unit. Its catch phis must receive the
    // caller frame at the call site, not the callee's virtual-register frame.
    InheritedCatchContext *inheritedCatchContext_ {nullptr};

    BB *inlineExitBlock_ {nullptr};
    BB *inlineTerminalBlock_ {nullptr};
    CompileInfoFacts *inlineExitFacts_ {nullptr};
    ChunkVector<ValueVertex *> inlineReturnValues_;
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

    // Bytecode offset currently being processed by BytecodeVisitor. Updated at Visit() entry;
    // consumed as the fallback source of Vertex::bytecodeOffset_ for vertices created without
    // an eager/lazy deopt mixin offset.
    uint32_t currentBytecodeOffset_ {0};
};
}  // namespace panda::ecmascript::arksteed

#endif  // ECMASCRIPT_ARKSTEED_GRAPH_BUILDER_NEW_H
