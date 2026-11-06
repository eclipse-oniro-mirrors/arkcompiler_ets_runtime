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

#ifndef ECMASCRIPT_ARKSTEED_ARKSTEED_GRAPH_VERIFIER_H
#define ECMASCRIPT_ARKSTEED_ARKSTEED_GRAPH_VERIFIER_H

#include "ecmascript/arksteed/arksteed_graph.h"
#include "ecmascript/arksteed/arksteed_graph_processor.h"

namespace panda::ecmascript::arksteed {

// The single owner of the common internal integrity contract of a finalized
// pre-allocation ArkSteed graph.
//
// The canonical entry is `GraphProcessor<ArkSteedGraphVerifier>(chunk).Run(graph)`
// and the canonical result is `void`: Debug success returns normally, leaves
// every graph field and transient bit unchanged and discards the invocation-local
// scratch of this verifier. A violated common invariant is an internal
// programming error and aborts through `ASSERT`; no recoverable error, post-abort
// graph guarantee, repair/retry protocol or first-error taxonomy for a
// multi-defect graph is defined. Release establishes the trusted internal-IR
// policy instead: the whole verification body is compiled out, so an explicit
// caller keeps the `GraphProcessor` shape but verifies nothing.
// The compiler also passes `afterRegalloc = true` after register allocation:
// that mode preserves concrete-input and allocation-initialization checks, but
// does not impose pre-allocation ownership, registry, CFG or metadata invariants
// on the allocator's temporary values and moves. Both modes are Release no-ops.
//
// The rule families and their owners inside this class:
//
//   1. Concrete layout and options - the typed `ProcessVertex` dispatch of
//      `GraphProcessor` (`ALL_VERTEX_LIST`) reaches the stored arity read through
//      the base `Vertex` accessor and compared against the compile-time
//      `NUM_INPUTS` (including zero) before the first slot read, the declared
//      `INPUT_TYPES` through exact representation equality, and the
//      per-opcode `VerifyInputs()`/`CheckInputs()` contract, which stays
//      authoritative for the widened descriptor-backed call layout and for the
//      builtin, HClass, divisor, store-case, deopt and prototype options. The
//      stored count is never read through the static
//      `FixedInputVertexMixin::GetInputCount()`, which would make the arity
//      comparison a tautology.
//   2. Direct/reference category - every stored slot of a dispatched vertex holds
//      a non-null object whose opcode is a known concrete value opcode, checked
//      before any typed access, and resolves to a graph-owned object: an
//      executable member or a retained registry object. The same resolution is
//      asserted for every eager and present optional-lazy frame value and for the
//      incoming value of every installed catch Phi, so a dangling or foreign
//      producer cannot pass the boundary through any reference channel.
//   3. Executable membership and ownership - `ExecutableStructureHolds`, run by
//      `PreProcessGraph` before the typed dispatch reads any entry.
//   4. Registries and parameters - `ConstantPoolBindingsAreValid` and
//      `ParametersAreInstalled` establish the bindings and the installed
//      descriptors, and `CollectRetainedRegistryDomain` records the pointer domain
//      those bindings retain (`RegistrySet`) for the resolution assertions above;
//      the concrete contract of each pooled vertex is reached through its own
//      `ProcessVertex` dispatch.
//   5. Frame references - eager and present optional-lazy frame values are
//      concrete values whose recorded kind agrees with their representation,
//      with exactly the raw actual-argument-count descriptor exception.
//   6. Catch references - `PreProcessGraph` refuses a non-graph-local catch
//      target before its Phi list is read and records the non-sentinel slot of
//      every throwable, unique per handler; `ProcessVertex` checks the incoming
//      value of every installed catch Phi, and `ExceptionalStructureHolds`
//      checks every installed catch Phi against the throwing-site domain arity
//      instead of the ordinary predecessor count, for the union of the flagged
//      exception-handler blocks and the registered catch targets, so a flagged
//      handler with no registered throwing site is a zero-arity domain and not a
//      skipped block.
//   7. CFG/predecessor/Phi - `ControlEdgeMultiplicityMatchesPredecessors`,
//      `UnconditionalPredecessorIdsAddressTheirSource`,
//      `BranchTargetsCarryNoPhi` and `OrdinaryPhiArityMatchesPredecessors`, all
//      in `PostProcessGraph`.
//   8. Loops and phase - `LoopHeaderStackHolds` in `PostProcessGraph`, the
//      supported builder dummy latches and disconnected structural blocks, an
//      uninitialized builder `RegisterMergeState`, and the absence of installed
//      vertex or block register-allocation metadata at the finalized
//      pre-allocation boundary.
//
// No reachability, dominance, alias analysis, CFG repair or GapMove rewrite is
// added, and no common check is deferred until after mutation. The verifier is
// read-only: the scratch below records invocation-local facts only.
// The uninitialized allocation contract is a checked allocation diamond:
//
//   entry: CanAllocateInYoung --BranchIfTrue--> fastBlock
//       \____________________________--> slowBlock (deferred)
//   fastBlock: AllocateInYoung (bump)     --> Jump(mergeBlock)
//   slowBlock: CallRuntime AllocateInYoung --> Jump(mergeBlock)
//   mergeBlock: Phi(fast, slow) --> InitializeXXX --> rest
//
// The uninitialized allocation may only feed the merge Phi, the merge Phi may only feed
// the matching initializer, and no GC-capable call, safepoint, deopt or exception
// exit may sit between raw storage and initialization. Gap moves inserted by
// register allocation are the only permitted intervening vertices. The arguments
// object initializer internally permits only non-GC write-barrier calls.
class ArkSteedGraphVerifier {
public:
    explicit ArkSteedGraphVerifier(Chunk *chunk, bool afterRegalloc = false)
        : chunk_(chunk), afterRegalloc_(afterRegalloc)
    {
    }

#ifndef NDEBUG
    void PreProcessGraph(Graph *graph);

    void PostProcessGraph(Graph *graph);

    void PostProcessBlock([[maybe_unused]] BB *block);

    void PreProcessBlock([[maybe_unused]] BB *block);

    template <typename VertexT>
    void ProcessVertex(VertexT* vertex, const ArkSteedState& state);

#else
    void PreProcessGraph([[maybe_unused]] Graph *graph) {}
    void PostProcessGraph([[maybe_unused]] Graph *graph) {}
    void PreProcessBlock([[maybe_unused]] BB *block) {}
    void PostProcessBlock([[maybe_unused]] BB *block) {}

    template <typename VertexT>
    void ProcessVertex([[maybe_unused]] VertexT *vertex, [[maybe_unused]] const ArkSteedState &state) {}
#endif

    void PostPhiProcessing() {}

private:
#ifndef NDEBUG
    static bool IsAllocationPhi(const ValueVertex* value);

    template<typename VertexT>
    static constexpr bool IsInitializer();

    static void VerifyStaticSize(uint64_t size, uint32_t minimumSize);

    static bool IsInt64Constant(ValueVertex* value, int64_t expected);

    static void VerifyArgumentCount(ValueVertex* argc);

    static void VerifyArgumentsElementsSize(ValueVertex* size, ValueVertex* argc);

    static void VerifyTaggedSize(ValueVertex* taggedSize, ValueVertex* size);

    void TryRegisterAllocationPhi(PhiVertex* phi, const ArkSteedState& state);

    // The super-this fill ladder's values: undefined, or the free-object
    // global constant behind the glue's constants table slot.
    static bool IsLadderFillValue(ValueVertex* fill);

    template<typename VertexT>
    void VerifyInitializer(VertexT* vertex);

    template<typename VertexT>
    static void VerifyLiteralInitializer(VertexT* vertex, ValueVertex* size);

    template<typename VertexT>
    void VerifyInitializationInterval(VertexT* vertex, const ArkSteedState& state);

    template<typename VertexT>
    void VerifyUninitializedAllocation(VertexT* vertex, const ArkSteedState& state);

    void EstablishBlockDomain(Graph *graph);

    void RegisterThrowable(Vertex *vertex);

    template <typename VertexT>
    void VerifyDirectInputs(VertexT *vertex);

    template <typename VertexT>
    void VerifyInputRepresentations(VertexT *vertex);

    void VerifyFrameReferences(Vertex *vertex);

    void VerifyCatchReferences(Vertex *vertex);

    void VerifyCapacityPredicate(BB *checkBlock, CanAllocateInYoungVertex *check);

    void VerifyAllocationPrecedingMoves(BB *block, Vertex *vertex);

    // Defined in the Debug implementation; no graph field is used as scratch.
    struct Scratch;
    Scratch *scratch_ {nullptr};
#endif
    [[maybe_unused]] Chunk *chunk_;
    [[maybe_unused]] bool afterRegalloc_;
};

#ifndef NDEBUG
// Instantiate the existing ALL_VERTEX_LIST dispatch where the typed verifier
// definitions are visible, rather than maintaining a second opcode inventory.
extern template class GraphProcessor<ArkSteedGraphVerifier>;
#endif

}  // namespace panda::ecmascript::arksteed

#endif  // ECMASCRIPT_ARKSTEED_ARKSTEED_GRAPH_VERIFIER_H
