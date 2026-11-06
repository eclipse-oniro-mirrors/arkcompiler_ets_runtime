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

#ifndef ECMASCRIPT_ARKSTEED_CODEGEN_H
#define ECMASCRIPT_ARKSTEED_CODEGEN_H

#include "ecmascript/arksteed/arksteed_assembler.h"
#include "ecmascript/arksteed/arksteed_deferred_code.h"
#include "ecmascript/arksteed/arksteed_graph.h"
#include "ecmascript/arksteed/arksteed_opcode.h"

#include <utility>
#include <vector>

namespace panda::ecmascript::arksteed {

class ArkSteedSafepointTableBuilder;
class ArkSteedCompilationUnit;
class DeoptLiteralTableBuilder;
class DeoptTranslationBuilder;
class GapMoveResolver;
class SourcePositionTableBuilder;

class ArkSteedCodeGenerator {
public:
    ArkSteedCodeGenerator(ArkSteedAssembler *assembler, Graph *graph,
                          ArkSteedSafepointTableBuilder *safepointBuilder = nullptr,
                          DeoptTranslationBuilder *translationBuilder = nullptr,
                          DeoptLiteralTableBuilder *deoptLiteralTableBuilder = nullptr,
                          SourcePositionTableBuilder *positionTableBuilder = nullptr,
                          bool withColors = false)
        : assembler_(assembler),
          graph_(graph),
          safepointBuilder_(safepointBuilder),
          translationBuilder_(translationBuilder),
          deoptLiteralTableBuilder_(deoptLiteralTableBuilder),
          positionTableBuilder_(positionTableBuilder),
          eagerDeoptTargetsById_(graph->GetChunk()),
          blockColorAssignment_(graph->GetChunk()),
          deferredCode_(graph->GetChunk()),
          withColors_(withColors)
    {}

    void Generate();

private:
    struct HasInPrototypeChainContext;
    void EmitHasInPrototypeChainLoop(HasInPrototypeChainContext &context);

    static int32_t ComputeFrameSize(Graph *graph);

    void ProcessValueVertex(ValueVertex *valueVertex);
    void ProcessNonControlVertex(NonControlVertex *vertex);
    void ProcessControlVertex(ControlVertex *vertex);
    void DeconstructPhisInSuccessor(BB *successor, uint32_t predecessorId);
    void CollectPhiMoves(GapMoveResolver *generalResolver, GapMoveResolver *doubleResolver, BB *successor,
                         int predecessorId, ArkSteedRegList *registersSetByPhis,
                         ArkDoubleRegList *doubleRegistersSetByPhis,
                         ChunkVector<std::pair<AllocatedState, ValueVertex *>> *constantMoves);
    void CollectRegisterStateMoves(GapMoveResolver *generalResolver, GapMoveResolver *doubleResolver, BB *successor,
                                   int predecessorId, const ArkSteedRegList &registersSetByPhis,
                                   const ArkDoubleRegList &doubleRegistersSetByPhis,
                                   ChunkVector<std::pair<AllocatedState, ValueVertex *>> *constantMoves);
    void LoadConstantToRegister(const ValueVertex *constVertex, ArkSteedRegister reg);
    void ExecuteConstantMove(const AllocatedState &dest, ValueVertex *constVertex,
                             const ArkSteedRegister *scratchGPR = nullptr,
                             const ArkSteedDoubleRegister *scratchFPR = nullptr);
    void ExecuteGapMove(const InstructionOperand &dest, const InstructionOperand &src,
                        const ArkSteedRegister *scratchGPR = nullptr,
                        const ArkSteedDoubleRegister *scratchFPR = nullptr);
    void StoreStubStackArgument(const Vertex *callVertex, int paramIdx, ArkSteedAssembler::MemoryOperand destMem);

    struct EagerDeoptTarget {
        Label label;
        DeoptId deoptId;

        explicit EagerDeoptTarget(DeoptId translationId) : deoptId(translationId) {}
    };

    Label *RecordEagerDeoptTarget(const EagerDeoptimizableMixin *vertex, kungfu::DeoptType type);
    void BranchToEagerDeoptTarget(Condition condition, const EagerDeoptimizableMixin *vertex, kungfu::DeoptType type);
    void EmitEagerDeoptExit(const EagerDeoptimizableMixin *vertex, kungfu::DeoptType type);
    void EmitQueuedEagerDeoptExits();
    void EmitEagerDeoptStackOverflow();

    int PrepareCommonStubStackArguments(const Vertex *callVertex, int argCount);
    int PrepareRuntimeStubStackArguments(const Vertex *callVertex, int argCount, int runtimeId);
    void LoadSteedExpectedArgc(ArkSteedRegister target, ArkSteedRegister expectedArgc);
    template <typename CallVertexT>
    void ComputeSteedCallSlotCount(CallVertexT *call, ArkSteedRegister slotCount);
    template <typename CallVertexT>
    void PrepareArkSteedCall(CallVertexT *call, ArkSteedRegister target, ArkSteedRegister scratch);
    // Runtime-argc spread variant of PrepareArkSteedCall: runtime-sized
    // reservation, argc slot from length + mandatory, mandatory argument placement
    // from the vertex's input locations, spread element copy loop and the
    // optional-arg undefined fill. `target` mirrors the non-spread contract;
    // `scratch`, `elements` and `elementsLength` are RA temporaries owned by
    // the caller and all three are clobbered. `elementsLength` is the elements
    // backing's own length (the copy/fill/reservation source, like the
    // interpreter's argv buffer); the argc comes from the JSArray length field
    // reloaded at the argc-slot write. The steed slot layout
    // [argc][target][newTarget][this][args...] matches the non-spread variant;
    // input indices come from CallVertexT (whose FUNC sits at index 0), slot
    // indices from the layout constants.
    template <typename CallVertexT>
    void PrepareArkSteedSpreadCall(CallVertexT *call, ArkSteedRegister target, ArkSteedRegister scratch,
                                   ArkSteedRegister elements, ArkSteedRegister elementsLength);
    void PrepareKnownArkSteedCall(CallKnownJSFunctionVertex *call, ArkSteedRegister target);
    void PrepareArkSteedSelfCall(CallSelfVertex *call);
    template <typename CallVertexT>
    void FreeArkSteedCallFrame(CallVertexT *call);
    template <class CallT>
    void EmitPreparedArkSteedCall(CallT *call, ArkSteedRegister target, Label *exit);
    template <typename CallVertexT>
    void EmitCallArkSteed(CallVertexT *call, ArkSteedRegister target, ArkSteedRegister scratch, Label *exit);
    // Compile-time frame-contract variant of EmitCallArkSteed: the steed frame
    // (slot reservation, argc, undefined fill) is materialized from the vertex's
    // known expected parameter count instead of runtime Method loads.
    template <typename CallVertexT>
    void EmitKnownArgsArkSteedCall(CallVertexT *call, Label *exit);
    // Runtime-argc spread variant: the shared steed-call tail (callee context,
    // call, lazy-deopt safepoint, frame free, jump to exit) around
    // PrepareArkSteedSpreadCall.
    template <typename CallVertexT>
    void EmitCallArkSteedSpread(ArkSteedRegister target, ArkSteedRegister scratch, ArkSteedRegister elements,
                                ArkSteedRegister elementsLength, CallVertexT *call, Label *exit);
    void EmitCallKnownArkSteed(CallKnownJSFunctionVertex *call, ArkSteedRegister target, Label *exit);
    template <class CallT>
    void EmitCallGeneric(CallT *call, ArkSteedRegister scratch);
    template <typename CallVertexT>
    void EmitCallConstructorGeneric(CallVertexT *call, ArkSteedRegister scratch);
    // Runtime-argc spread variant of the generic [[Construct]] fallback: the
    // JSCallNew trampoline layout ([argc][argv][target][newTarget][this][args])
    // with the JSArray length field in the argc slot and the spread elements
    // copied into the user-arg slots by the elements backing's own length
    // (holes mapped to undefined; the tail past the argc is dead, like the
    // interpreter's argv buffer). Input indices come
    // from CallVertexT, slot indices from the layout constants.
    template <typename CallVertexT>
    void EmitCallConstructorSpreadGeneric(CallVertexT *call, ArkSteedRegister scratch, ArkSteedRegister elements,
                                          ArkSteedRegister elementsLength);
    // Forward-all-args (SUPERCALLFORWARDALLARGS) variants of the spread call
    // pieces: the argument source is the caller frame's own incoming arguments
    // (count = frame argc - mandatory, carried by the ARGC input register and
    // derived into `userArgc` by the visitors), so no length/elements pair
    // exists; everything else mirrors the spread counterparts.
    template <typename CallVertexT>
    void PrepareArkSteedForwardAllArgsCall(CallVertexT *call, ArkSteedRegister target,
                                           ArkSteedRegister scratch, ArkSteedRegister userArgc);
    template <typename CallVertexT>
    void EmitCallArkSteedForwardAllArgs(ArkSteedRegister target, ArkSteedRegister scratch,
                                        ArkSteedRegister userArgc, CallVertexT *call, Label *exit);
    template <typename CallVertexT>
    void EmitCallConstructorForwardAllArgsGeneric(CallVertexT *call, ArkSteedRegister scratch,
                                                  ArkSteedRegister userArgc);
    void EmitReturnWithPendingException();
    void EmitReturnIfPendingException();
    Label *GetReturnWithPendingExceptionVeneer();
    int PrepareTrampolineArguments(const Vertex *call, uint32_t actualArgc, uint32_t targetIndex,
                                   uint32_t newTargetIndex, uint32_t receiverIndex, uint32_t firstArgIndex,
                                   ArkSteedRegister scratch);
    template <class CallT>
    int PrepareTrampolineArguments(CallT *call, ArkSteedRegister scratch);
    void PrepareKnownCallArguments(const Vertex *call, uint32_t actualArgc, uint32_t expectedArgc,
                                   uint32_t firstArgIndex);
    template <typename ConstructorVertexT>
    void EmitConstructorBodyStubCall(ConstructorVertexT *call);
    template <typename ConstructorVertexT>
    void EmitNewObjRangeConstructorBodyCall(ConstructorVertexT *call);
    int PrepareKnownBuiltinStubArguments(CallKnownBuiltinVertex *call);
    int PrepareKnownBuiltinNativeArguments(CallKnownBuiltinVertex *call);

    void EmitInitializeHClassWithBarrier(const DeferredRegisterSnapshot &snapshot, ArkSteedRegister glue,
                                         ArkSteedRegister object, ArkSteedRegister hclass);
    void EmitStoreTaggedFieldWithBarrier(const DeferredRegisterSnapshot &snapshot, ArkSteedRegister glue,
                                         ArkSteedRegister object, ArkSteedRegister value, int32_t offset,
                                         ArkSteedWriteBarrierValueKind valueKind);

    template <class VertexT>
    void VisitNonControlVertex(VertexT *vertex);

    template <class VertexT>
    void VisitControlVertex(VertexT *vertex);

    ArkSteedSafepointTableBuilder *GetSafepointBuilder() const
    {
        return safepointBuilder_;
    }

    // -------------------------------------------------------------------------
    // Comment recording helpers
    // -------------------------------------------------------------------------

    void RecordComment(const char *msg);
    void RecordBlockComment(BB *block);
    // Emits EagerInline begin/end markers when the inline-owner changes between
    // layout-adjacent blocks. A null block closes any open inline region.
    void RecordInlineTransitionComment(BB *block);
    void RecordVertexComment(uint32_t pcBefore, Vertex *vertex);
    void AppendVertexSuccessorInfo(std::ostringstream *ss, Vertex *vertex);
    void RecordGapMoveComment(const InstructionOperand &src, const InstructionOperand &dest, PhiVertex *phi);
    void RecordSpillComment();
    void EmitDeferredCode();

    int ComputeDeferredBlocks();
    void ReorderDeferredBlocks(int deferredCount);
    bool IsNextBlockInLayout(BB *target) const;

    bool AllPredecessorsDeferred(BB *block) const;
    bool AllSuccessorsDeferred(BB *block);

    void ComputeBlockColors();
    void BuildBlockAdjacencyList(std::vector<std::vector<int>> *adjacentBlocks);
    void AssignBlockColors(const std::vector<std::vector<int>> &adjacentBlocks);
    int GetBlockColorIndex(int blockId) const;

    void RecordPositionBoundary(uint32_t pcBefore, uint32_t bcOffset);

    ArkSteedAssembler *assembler_;
    Graph *graph_;
    ArkSteedSafepointTableBuilder *safepointBuilder_;
    DeoptTranslationBuilder *translationBuilder_;
    DeoptLiteralTableBuilder *deoptLiteralTableBuilder_;
    SourcePositionTableBuilder *positionTableBuilder_;
    Label entryLabel_;
    ChunkVector<EagerDeoptTarget *> eagerDeoptTargetsById_;
    int currentBlockColorIndex_ = 0;
    BB *currentLayoutNextBlock_ = nullptr;
    const ArkSteedCompilationUnit *prevInlineUnit_ {nullptr};

    // Block color assignment for CFG coloring (only computed when comments enabled)
    ChunkVector<int> blockColorAssignment_;
    ArkSteedDeferredCodeList deferredCode_;
    // Lazily created once per function; shared by every exception-check site.
    ArkSteedDeferredCode *returnIfPendingVeneer_ = nullptr;
    bool blockColorsComputed_ = false;
    bool withColors_;
};

}  // namespace panda::ecmascript::arksteed

#endif  // ECMASCRIPT_ARKSTEED_CODEGEN_H
