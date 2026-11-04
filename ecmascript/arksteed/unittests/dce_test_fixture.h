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

#ifndef ECMASCRIPT_ARKSTEED_UNITTESTS_DCE_TEST_FIXTURE_H
#define ECMASCRIPT_ARKSTEED_UNITTESTS_DCE_TEST_FIXTURE_H

#include "ecmascript/arksteed/arksteed_dce.h"
#include "ecmascript/arksteed/arksteed_graph.h"
#include "ecmascript/arksteed/arksteed_graph_processor.h"
#include "ecmascript/arksteed/arksteed_graph_verifier.h"
#include "ecmascript/arksteed/arksteed_opcode.h"
#include "ecmascript/arksteed/arksteed_opcode_list.h"
#include "ecmascript/arksteed/arksteed_regalloc_processors.h"
#include "ecmascript/js_tagged_value.h"
#include "ecmascript/mem/native_area_allocator.h"
#include "ecmascript/tests/test_helper.h"
#include "common_components/log/log.h"

#include <algorithm>
#include <cstdint>
#include <initializer_list>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace panda::ecmascript::arksteed {

// Unit domain for the shared DCE contract:
// the removal proof gate (`DceRemovalPolicy`), the complete dependency
// enumeration (`DceDependencies`) and the graph filtering authority
// (`DceGraphEditor::Filter` on a `DceLiveSet`).
//
// Fixture A (`NativeAreaAllocator` + `Chunk` + real `Graph`/`BB`/`Vertex::New`)
// reaches every production processor position of the graph-verifier fixture:
// all five constant pools, several blocks with a control vertex each, Phis, and
// a non-control sequence. No VM, `JSThread`, `EcmaHandleScope`, assembler or
// bytecode asset is created. Every mutating fixture is accepted by the production
// `ArkSteedGraphVerifier` through `GraphProcessor<ArkSteedGraphVerifier>` before
// and after the mutation, so a fixture error is reported instead of being
// mistaken for a DCE result.
//
// Expected dependency classes and structural order are independent of the
// production predicates under test.
//
// Trusted internal IR: this domain drives the valid-IR semantics only. DCE has no
// failure result and malformed internal IR is a Debug assertion error at the
// normal `ArkSteedGraphVerifier` boundary, which the graph-verifier domain owns;
// the artificial malformed-input refusal controls this domain used to carry were
// removed with that policy cutover, so Debug and Release run the same cases.

constexpr int32_t FIXTURE_INT32_VALUE = 7;
constexpr double FIXTURE_FLOAT64_VALUE = 1.5;
constexpr int32_t LOOP_ENTRY_VALUE = 3;
constexpr uint32_t DEAD_TAIL_VREG = 2;

template <typename T>
std::string DescribePointerVector(const T &values)
{
    std::ostringstream out;
    out << "[";
    for (const auto *value : values) {
        out << static_cast<const void *>(value) << " ";
    }
    out << "]";
    return out.str();
}

std::string SafeOpcodeNameForTest(VertexOpcode opcode);

// Overwrites only the opcode field of a real allocated vertex through its object
// representation. `Vertex` stores the opcode in the low 16 bits of its first
// `uint64_t` bitfield. A checked, bounded secure copy observes and writes the
// same storage without a type-punning read, allowing a probe whose opcode is
// absent from `ALL_VERTEX_LIST`, which the constructor cannot produce.
void SetOpcodeForTest(Vertex *vertex, VertexOpcode opcode);

// Overwrites only the stored property-flags field of a real allocated vertex.
// The field occupies bits 32..47 of the same first `uint64_t` bitfield as the
// opcode and the stored input count (`Vertex::PropertyFlagsField`), so the same
// checked, bounded secure copy writes that storage without a type-punning read.
// The unknown-opcode probe carries the positive removal bit, proving the policy
// requires a supported concrete opcode as well as the property authorization.
void SetPropertyFlagsForTest(Vertex *vertex, VertexPropertyFlag flags);

// One vertex record: identity, representation, owner, input count and every
// input pointer plus the hidden references and concrete options the contract
// must preserve. Used by the success cases to prove a survivor keeps its own
// structure, frame values, exception state and options across a commit.
std::string DescribeHiddenReferences(const Vertex *vertex);

// Concrete options of the opcodes this domain constructs. They are initialized
// semantic state of a real vertex, so a snapshot that omitted them could not
// detect a mutation of a condition, deopt type, builtin identity or divisor.
std::string DescribeConcreteOptions(const Vertex *vertex);

std::string DescribeVertexRecord(const Vertex *vertex);

// Concrete payload record of one pooled constant, so a constant-object mutation
// is observable in the snapshot.
std::string DescribeConstantPayload(const ValueVertex *value);

// Parameter descriptors (with their fixed frame slot), every constant pool and
// their interning keys plus each pooled constant's concrete payload. This is the
// registry half of the full snapshot.
std::string SnapshotRegistriesText(Graph *graph);

// Block identity, flags, ordered predecessors, control pointer, the
// unconditional predecessor id / target, the branch targets and the block-owned
// allocation/effect state. These are the structural fields filtering must never
// touch.
std::string SnapshotControlText(Graph *graph);

// Complete graph snapshot: registries, structural block state, each block's
// control-vertex record (identity, representation, owner, inputs and hidden
// eager/lazy/catch references) and the exact ordered executable/Phi membership
// with each member's own record.
std::string SnapshotGraphScalarsText(Graph *graph);

// Complete graph snapshot: graph scalars, registries, structural block state,
// each block's control-vertex record (identity, representation, owner, options,
// inputs and hidden eager/lazy/catch references) and the exact ordered
// executable/Phi membership with each member's own record.
std::string SnapshotGraphText(Graph *graph);

// Structural snapshot that never reads a vertex input slot: graph scalars,
// registries (parameter count/index/fixed frame slot, all five constant maps and
// their payloads), the structural block state (including the block-owned
// allocation/effect pointers) and the exact ordered executable/Phi membership
// pointers. It is used by the malformed-stored-arity negative controls, where a
// vertex input record would read the excess slot of a real object; it still
// covers exactly what filtering may mutate (the executable membership lists)
// plus everything the contract requires to stay unchanged.
std::string SnapshotMembershipText(Graph *graph);

// Test-local request shape: the exact set of vertices a case wants removed. It
// is expanded into a real `DceLiveSet` (every other executable/registry vertex is
// marked live), so the production `DceGraphEditor::Filter` derives exactly that
// candidate set through its public contract. Marking is explicit for the tests
// that drive `DceLiveSet` directly.
class RemovalRequest {
public:
    void MarkRemoved(const Vertex *vertex);

    bool Contains(const Vertex *vertex) const;

private:
    std::vector<const Vertex *> removed_;
};

// Marks every executable member, parameter descriptor and constant-pool entry
// live except the request's removals. The production filter therefore derives its
// candidates only from unmarked `RemovableValue`/Phi members.
void MarkAllLiveExcept(Graph *graph, DceLiveSet &live, const RemovalRequest &request);

// Enables the existing compiler log channel for a case that needs the compiler
// output redirected while the operation runs.
class CompilerLogCapture {
public:
    CompilerLogCapture();

    void Begin();

    std::string End();

private:
    std::ostringstream buffer_;
    std::streambuf *previous_ {nullptr};
};

// The public two-argument `Filter` entry point driven through a real
// compilation-local live set.
DceResult FilterRemovals(Graph *graph, Chunk *chunk, const RemovalRequest &request);

// The result and the captured compiler-log text of one `Filter` call.
struct FilterOutcome {
    DceResult result {DceResult::Unchanged};
    std::string log;
};

FilterOutcome FilterRemovalsCaptured(Graph *graph, Chunk *chunk, const RemovalRequest &request,
                                     CompilerLogCapture &capture);

class ArkSteedDceTestBase : public testing::Test {
protected:
    NativeAreaAllocator nativeArea_;
    Chunk chunk_ {&nativeArea_};

    // Enables and captures the compiler log channel the component uses for its
    // rule diagnostics. No diagnostic overload, return value or last-error state
    // is part of the public boundary.
    CompilerLogCapture capture_;

    // Classifies one vertex through the public `Classify(Vertex *)` contract. The
    // policy reads the positive removal property of the vertex object, so it is
    // driven with a real vertex of the concrete class under test (`NewVertex`
    // writes that class's central `PROPERTIES`), exactly as a compiler consumer
    // drives it.
    static DceRemovalClass ClassifyProbe(Vertex *vertex);

    // Real value vertex of the requested machine representation, built from the
    // pooled-constant shapes an ordinary graph uses. The removable classes declare
    // their input representations in `INPUT_TYPES`, so the classification cases
    // below can build production-shaped objects without a hand-written per-opcode
    // input inventory.
    ValueVertex *RepresentationSample(ValueRepresentation representation);

    // One real vertex of the concrete class, with one real input vertex per
    // declared `INPUT_TYPES` entry.
    template <typename VertexT, typename... Args>
    VertexT *NewRemovableVertex(Args &&...args)
    {
        std::vector<ValueVertex *> inputs;
        for (ValueRepresentation representation : VertexT::INPUT_TYPES) {
            inputs.push_back(RepresentationSample(representation));
        }
        return NewVertex<VertexT>(inputs, std::forward<Args>(args)...);
    }

    // Installs a control vertex on its block and records the production
    // ownership relation (`GraphBuilder::FinishBlockWith` sets the same owner).
    void InitializeInputs(Vertex *vertex, const std::vector<ValueVertex *> &inputs);
    void SetControl(BB *block, ControlVertex *vertex);

    template <typename Derived, typename... Args>
    Derived *NewVertex(const std::vector<ValueVertex *> &inputs, Args &&...args)
    {
        Derived *vertex = Vertex::New<Derived>(&chunk_, inputs.size(), std::forward<Args>(args)...);
        if (vertex == nullptr) {
            return nullptr;
        }
        InitializeInputs(vertex, inputs);
        return vertex;
    }

    // Runs the production graph verifier exactly as a real compilation does. A
    // Debug assertion abort or a failed fixture prerequisite is the caller's
    // responsibility; this helper only proves the graph reaches the verifier.
    void RunVerifier(Graph *graph);

    // Runs the production pre-regalloc consumers of the retained graph exactly in
    // the `RunPreRegallocProcessors` order of `ArkSteedCompilerTask::Compile`:
    // metadata allocation, location constraints, maximum call depth and allocator
    // liveness. Liveness indexes a merge Phi with the unconditional predecessor id
    // and pushes/pops loop headers, so executing it proves that a committed result
    // is consumable by the allocator rather than merely well-shaped for this
    // domain's validator. It must run only after the pointer/structure and
    // no-identifier assertions, because it allocates regalloc metadata and ids.
    void RunPreRegallocConsumers(Graph *graph);

    struct AllocationFixture {
        Graph *graph {nullptr};
        BB *entry {nullptr};
        BB *fastBlock {nullptr};
        BB *slowBlock {nullptr};
        BB *merge {nullptr};
        CanAllocateInYoungVertex *check {nullptr};
        AllocateInYoungVertex *allocation {nullptr};
        CallRuntimeVertex *slowAllocation {nullptr};
        PhiVertex *phi {nullptr};
        InitializeEmptyArrayVertex *initializer {nullptr};
        ValueVertex *taggedConstant {nullptr};
    };

    void InitializeAllocationValues(AllocationFixture &fixture);

    AllocationFixture BuildAllocationFixture();

    struct EditorFixture {
        Graph *graph {nullptr};
        BB *entry {nullptr};
        BB *ifTrue {nullptr};
        BB *ifFalse {nullptr};
        BB *merge {nullptr};
        ValueVertex *int32Constant {nullptr};
        ValueVertex *taggedConstant {nullptr};
        I32AddVertex *deadChainHead {nullptr};
        I32ToTaggedIntVertex *deadChainTail {nullptr};
        I32AddVertex *liveProducer {nullptr};
        I32AddVertex *deadConsumer {nullptr};
        I32ToTaggedIntVertex *deadSink {nullptr};
        InitialValueVertex *parameter {nullptr};
        BranchIfInt32CompareVertex *branch {nullptr};
        JumpVertex *trueJump {nullptr};
        JumpVertex *falseJump {nullptr};
        PhiVertex *livePhi {nullptr};
        PhiVertex *deadPhi {nullptr};
        ReturnVertex *mergeReturn {nullptr};
    };

    bool InitializeEditorConstants(EditorFixture &fixture);

    bool InitializeEditorValues(EditorFixture &fixture);

    bool InitializeEditorMerge(EditorFixture &fixture);

    EditorFixture BuildEditorFixture();

    void MarkDeadEditorSelection(const EditorFixture &fixture, RemovalRequest &request);

    // Production-shaped loop fixture. Block order is entry, header, body, exit;
    // the header is flagged as a loop header, holds a two-input Phi (one slot per
    // ordinary predecessor) and finishes with a conditional branch to the body and
    // the exit; the body finishes with the `JumpLoop` back edge to the header.
    // Every ordinary predecessor and the back edge's stored predecessor id follow
    // the production construction order (`FinishBlockWithJump`,
    // `FinishBlockWithJumpLoop`, `FinishBlockWithBranch`), so the shape matches
    // the `LivenessProcessor` loop push/pop convention.
    struct LoopFixture {
        Graph *graph {nullptr};
        BB *preheader {nullptr};
        BB *header {nullptr};
        BB *body {nullptr};
        BB *exit {nullptr};
        ValueVertex *entryValue {nullptr};
        ValueVertex *backedgeValue {nullptr};
        JumpVertex *preheaderJump {nullptr};
        PhiVertex *headerPhi {nullptr};
        BranchIfTrueVertex *headerBranch {nullptr};
        JumpLoopVertex *backedge {nullptr};
        ReturnVertex *exitReturn {nullptr};
    };

    bool InitializeLoopControls(LoopFixture &fixture);

    LoopFixture BuildLoopFixture();

    // Production-shaped exception fixture. In production every catch-block
    // header is synthetic and terminates in a single unconditional jump
    // (`GraphBuilder::ProcessCatchBlockHead` sets `SetIsExceptionHandler(true)` and
    // then `FinishBlockWithJump` to the handler body), while
    // `ActivateCatchBlock`/`InitFrameStateForCatchBlockHeader` create the block and
    // its catch Phi inputs without adding an ordinary CFG predecessor. The fixture
    // reproduces that shape: the entry block calls two registered throwables at
    // indexes 0 and 1, the exception handler block carries the handler flag, has
    // zero ordinary predecessors and jumps to a handler body that returns. Every
    // vertex has its owner, so the graph is consumable by the production verifier
    // and the pre-regalloc consumers. `first`/`second` come from the graph's
    // tagged constant pool, so they are registry entries and the editor sees every
    // reference as valid. The caller adds the catch Phi that fits the checked
    // shape (or none, for a no-Phi handler).
    struct CatchFixture {
        Graph *graph {nullptr};
        BB *entry {nullptr};
        BB *catchBlock {nullptr};
        BB *handler {nullptr};
        ValueVertex *first {nullptr};
        ValueVertex *second {nullptr};
        CallRuntimeVertex *firstThrowable {nullptr};
        CallRuntimeVertex *secondThrowable {nullptr};
        ReturnVertex *entryReturn {nullptr};
        JumpVertex *catchJump {nullptr};
        ReturnVertex *handlerReturn {nullptr};
    };

    bool InitializeCatchValues(CatchFixture &fixture);

    CatchFixture BuildCatchFixture();

    PhiVertex *AddCatchPhi(const CatchFixture &fixture, const std::vector<ValueVertex *> &inputs);

    // Fixture for the closure's hidden-only dependencies. It carries four
    // `I32Add` values with no ordinary consumer: three of them are referenced
    // only through a hidden channel of a `Required` consumer (the eager frame
    // state of an `I32AddWithOverflow`, the lazy frame state of a runtime call
    // and the incoming value of a catch Phi), and one (`deadPlain`) has no
    // reference at all. The catch shape follows the production synthetic
    // exception-handler convention of `BuildCatchFixture`: an exception-handler
    // block with zero ordinary predecessors that installs one catch Phi and
    // jumps to a handler body, which uses that Phi so it stays live.
    struct HiddenOnlyFixture {
        Graph *graph {nullptr};
        BB *entry {nullptr};
        BB *catchBlock {nullptr};
        BB *handler {nullptr};
        ValueVertex *taggedConstant {nullptr};
        I32AddVertex *deadPlain {nullptr};
        I32AddVertex *eagerProducer {nullptr};
        I32AddVertex *lazyProducer {nullptr};
        I32AddVertex *catchProducer {nullptr};
        I32AddWithOverflowVertex *eagerConsumer {nullptr};
        CallRuntimeVertex *lazyConsumer {nullptr};
        CallRuntimeVertex *catchThrowable {nullptr};
        PhiVertex *catchPhi {nullptr};
        ReturnVertex *entryReturn {nullptr};
        JumpVertex *catchJump {nullptr};
        ReturnVertex *handlerReturn {nullptr};
    };

    bool InitializeHiddenValues(HiddenOnlyFixture &fixture, ValueVertex *int32Constant);

    bool InitializeHiddenState(HiddenOnlyFixture &fixture);

    HiddenOnlyFixture BuildHiddenOnlyFixture();

    // Closure-shaped exception fixture for the `ArkSteedDce::Run` path of the
    // catch-Phi rule. It follows the production synthetic-handler convention of
    // `BuildCatchFixture` (an exception-handler block with zero ordinary
    // predecessors that installs catch Phis and jumps to a handler body) and adds
    // two registered throwables at exceptional slots 0 and 1 plus four value
    // producers in the entry block:
    //
    //   intProducer -> shared (-> deadHeadProducer)   liveOnly
    //                 |            |                    |
    //                 +-------- livePhi (consumed by the handler return)
    //                              deadHeadProducer -> deadHead -> deadTail
    //
    // `livePhi` is the only consumer of both `liveOnly` and `shared`, and the
    // dead chain `deadHeadProducer -> deadHead -> deadTail` has no consumer at
    // all. Every producer is defined before the throwables that reference it at
    // their exceptional slot, so the retained graph stays consumable by
    // `LivenessProcessor::MarkCatchPhiInputUses`. `reverseOrder` reorders the
    // throwables and the catch-Phi list without changing any dependency, so two
    // builds of the same logical graph exercise different traversal orders.
    struct CatchClosureFixture {
        Graph *graph {nullptr};
        BB *entry {nullptr};
        BB *catchBlock {nullptr};
        BB *handler {nullptr};
        ValueVertex *first {nullptr};
        ValueVertex *second {nullptr};
        I32AddVertex *intProducer {nullptr};
        I32ToTaggedIntVertex *shared {nullptr};
        ConvertHoleToUndefinedVertex *liveOnly {nullptr};
        ConvertHoleToUndefinedVertex *deadHeadProducer {nullptr};
        PhiVertex *livePhi {nullptr};
        PhiVertex *deadHead {nullptr};
        PhiVertex *deadTail {nullptr};
        CallRuntimeVertex *firstThrowable {nullptr};
        CallRuntimeVertex *secondThrowable {nullptr};
        ReturnVertex *entryReturn {nullptr};
        JumpVertex *catchJump {nullptr};
        ReturnVertex *handlerReturn {nullptr};
    };

    bool InitializeCatchClosureValues(CatchClosureFixture &fixture, ValueVertex *int32Constant);

    void InstallCatchClosure(CatchClosureFixture &fixture, bool reverseOrder);

    CatchClosureFixture BuildCatchClosureFixture(bool reverseOrder = false);
};
constexpr uint32_t SAMPLE_VERTEX_ID = 0;
constexpr int32_t SAMPLE_INT32_VALUE = 7;
constexpr uint32_t SAMPLE_BINDING_INDEX = 3;
constexpr uint32_t SAMPLE_FIELD_OFFSET = 32;
constexpr uint32_t SAMPLE_ADDRESS_OFFSET = 0;
constexpr uint32_t SAMPLE_ARGUMENT_COUNT = 1;
constexpr uint32_t SAMPLE_BYTECODE_OFFSET = 0;
constexpr uint32_t CATCH_SITE_INDEX = 1;
constexpr uint32_t FIRST_VREG = 0;
constexpr auto UNKNOWN_OPCODE = static_cast<VertexOpcode>(UINT16_MAX);

class ArkSteedDceTest : public ArkSteedDceTestBase {
protected:
    void ExpectRemovalClass(Vertex *vertex, DceRemovalClass expected);

    void ExpectRemovableValues(const std::vector<ValueVertex *> &values);

    void ExpectRequiredControls(const std::vector<Vertex *> &values);

    void CheckRemovableArithmetic();

    void CheckRemovableBitwiseAndComparisons();

    void CheckRemovableConversionsAndMerge();

    void CheckSideEffectsAndCseDoNotAuthorizeRemoval();

    void CheckRegistryAndStructuralValuesRemainRequired();

    void CheckUnknownOpcodesIgnoreRemovalAuthorization();

    void CheckCallsRemainRequired(ValueVertex *address, ValueVertex *tagged);

    void CheckWritesAndAllocationsRemainRequired(ValueVertex *address, ValueVertex *tagged);

    void CheckExceptionalAndMemoryOperationsRemainRequired(ValueVertex *address, ValueVertex *tagged);

    ValueVertex *NewTaggedSample(JSTaggedValue value);

    CallRuntimeVertex *NewRuntimeConsumer(const std::vector<ValueVertex *> &inputs);

    void AttachLazyFrame(CallRuntimeVertex *consumer, ValueVertex *value);

    void AttachCatchInputs(CallRuntimeVertex *consumer, ValueVertex *entry,
                           const std::vector<ValueVertex *> &incoming);

    void ExpectDependencies(Vertex *consumer, const std::vector<ValueVertex *> &expected);

    void CheckCombinedLazyAndCatchDependencies(ValueVertex *direct, ValueVertex *frame);

    void CheckCombinedEagerDependencies(ValueVertex *frame);

    void CheckAbsentLazyFrameContributesNoDependencies(ValueVertex *direct);

    void CheckIsolatedEagerDependency(ValueVertex *producer);

    void CheckIsolatedLazyDependency(ValueVertex *producer);

    void CheckIsolatedCatchDependency(ValueVertex *producer);
    void CheckEditorOrder(const EditorFixture &fixture);

    void CheckRemovedStorage(const EditorFixture &fixture);

    void BuildDummyLoop(Graph *&graph, BB *&header, BB *&latch, ValueVertex *&entryValue,
                        I32AddVertex *&backedgeValue, PhiVertex *&headerPhi,
                        ReturnVertex *&headerReturn, JumpLoopVertex *&latchBackedge);

    void ConnectPhiCycle(Graph *graph, BB *entry, BB *merge, BB *exit, ValueVertex *constant,
                         ReturnVertex *&exitReturn);

    void AddRetainedDeoptState(const CatchFixture &fixture, I32AddWithOverflowVertex *&eagerSurvivor);

    void CheckRetainedDeoptState(const CatchFixture &fixture, I32AddWithOverflowVertex *eagerSurvivor,
                                PhiVertex *catchPhi);

    void CheckRetainedExceptionalSite(const CatchFixture &fixture, PhiVertex *catchPhi);
    void CheckRootlessPhiCycle();
    void CheckRootedPhiCycle();
    void CheckRootedPhiCycleSurvivors(Graph *graph, BB *merge, PhiVertex *phiA, PhiVertex *phiB,
                                     ReturnVertex *exitReturn);
    void CheckRootedLoopPhi();
    void CheckRootlessCatchPhiCycle();
    void CheckRootedCatchPhiCycle();
    void CheckEarlyCatchPhiActivation();
    void CheckLateCatchPhiActivation();
    void CheckDeadCatchPhiProducerChain();
};

}  // namespace panda::ecmascript::arksteed

#endif  // ECMASCRIPT_ARKSTEED_UNITTESTS_DCE_TEST_FIXTURE_H
