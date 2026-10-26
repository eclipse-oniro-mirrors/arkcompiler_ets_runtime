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

#include "ecmascript/arksteed/arksteed_graph.h"
#include "ecmascript/arksteed/arksteed_graph_processor.h"
#include "ecmascript/arksteed/arksteed_graph_verifier.h"
#include "ecmascript/arksteed/arksteed_opcode.h"
#include "ecmascript/js_tagged_value.h"
#include "ecmascript/mem/native_area_allocator.h"
#include "ecmascript/tests/test_helper.h"

#include <cstdint>
#include <initializer_list>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace panda::ecmascript::arksteed {

// Unit domain for matrix row G3.5 (detection of damaged graphs). The cases use graph
// fixture A: a real NativeAreaAllocator/Chunk/Graph with real IR vertices and no
// EcmaVM, no JSThread, no EcmaHandleScope, no assembler and no bytecode asset. The
// fixture follows the VM-free precedent of `write_barrier_value_kind_test.cpp` and
// pins the domain contract C5 of the wiki page `arksteed-cpp-unit-test-domains` and
// GV-1..GV-5 of `arksteed-graph-verifier-unit-domain`.
//
//   * `RunVerifier` drives the production `ArkSteedGraphVerifier` through
//     `GraphProcessor<ArkSteedGraphVerifier>`, exactly as a real compilation does.
//   * `BuildFixture(kind, corrupt)` builds fixture A through the public
//     `Graph`/`BB`/`Vertex::New` surfaces. The graph reaches every processor position:
//     all five constant pools, several blocks with a control vertex each, a Phi, and a
//     non-control sequence. `corrupt = false` yields a graph satisfying every
//     prerequisite; `corrupt = true` adds exactly one controlled violation of the
//     selected class X1-X8 of GV-4. The repaired twin of every class is the valid
//     shape, because each violation removes exactly one property of it.
//   * `SnapshotGraph` captures the structural facts that a verifier must not change:
//     block order/identity and ids, the full ordered membership and per-vertex
//     structure of every constant pool, the control vertex of every block, the ordered
//     Phi and non-control compositions, and, per captured vertex, opcode, value
//     representation, input count and each in-range input identity. It never calls
//     `Vertex::GetId()`, whose register-allocation metadata is absent from fixture A.
//     Snapshot records are taken for constants, control, Phi and non-control vertices
//     alike, so a changed opcode, representation, arity or input slot is visible even
//     when the vertex pointer itself is unchanged.
//
// Observations of this stage:
//
//   * `ValidGraphPassesEveryProcessorStageWithoutMutation` is the both-mode positive
//     control: it runs the verifier twice on the valid graph and requires completion and
//     equal before/after snapshots.
//   * Eight Debug-only death cases observe detection of X1-X8. Each case first runs its
//     repaired twin to completion in the parent with equal snapshots, then runs the
//     verifier on the fully constructed corrupted graph inside `ASSERT_DEATH` and
//     requires the intended assertion condition text of GV-4. Construction failures and
//     unrelated crashes therefore cannot pass as detection (D4, GV-5).
//   * `CorruptionFixturesWithoutActiveAssertionsDoNotAbortAndDoNotMutateTheGraph` is the
//     Release-only control. With every `ASSERT` disabled it runs the six defined
//     corrupted shapes X1, X3, X5, X6, X7 and X8 and the repaired twin of every class
//     X1-X8 to completion, each with a fresh fixture and a fresh processor and with equal
//     before/after snapshots. It makes no detection claim: completion under `NDEBUG` is
//     the documented Release behavior of these shapes, not an observation of a violation.
//     The corrupted X2 (zero-input Phi) and X4 (null Phi input) shapes are invoked by no
//     Release path at all (D5, GV-6).
//
// Mode layout of this stage: nine Debug cases (the positive control and the eight death
// cases) and two Release cases (the positive control and the Release corruption control).
// The death cases are declared only under `#ifndef NDEBUG`, because `ASSERT` is a no-op
// under `NDEBUG`; the Release corruption control is declared only under `#ifdef NDEBUG`.
// The corrupted X2 (zero-input Phi) and X4 (null Phi input) fixtures are reached by no
// Release path at all, since their verifier path reads an invalid input slot, and for X4
// dereferences a null input, once assertions are disabled (D5, GV-4). Only the repaired
// halves of X2 and X4 take part in the Release control.
//
// Independent oracle (D2): the expectations below are the documented contracts of the
// verifier and of the exercised vertex types (`arksteed_graph_verifier.h`,
// `arksteed_opcode.h`), not a boolean recomputed from a production predicate. The
// call layout is the owner-corrected contract
// `GetInputCount() == CallVertex::FIRST_ARG_INDEX + GetActualArgc()` with
// `FIRST_ARG_INDEX = 3`; a runtime call with zero inputs and a Deopt with zero inputs
// are valid forms, and no minimum from the (formerly erroneous) verifier comment is
// asserted.

namespace {

// The corruption classes of GV-4 plus the un-corrupted valid shape. `corrupt = false`
// builds the repaired twin of the selected class; the repaired twin of every class is
// the valid shape.
enum class CorruptionKind : uint32_t {
    VALID = 0,
    DUPLICATE_BLOCK_ID,                   // X1: two blocks reuse one in-range id.
    EMPTY_PHI,                            // X2: Phi without any input.
    PHI_REPRESENTATION_MISMATCH,          // X3: Phi inputs disagree on the representation.
    NULL_PHI_INPUT,                       // X4: allocated Phi input slot stays null.
    CALL_LAYOUT_MISMATCH,                 // X5: call slots do not match actual argument count.
    CONVERSION_REPRESENTATION_MISMATCH,   // X6: U32 conversion takes a TAGGED input.
    EMPTY_COMMON_STUB_CALL,               // X7: common-stub call without its glue input.
    BRANCH_REPRESENTATION_MISMATCH,       // X8: INT32 comparison takes TAGGED inputs.
};

#ifndef NDEBUG
// Implemented minimum of a common-stub call (arksteed_opcode.h,
// CallCommonStubVertex::VerifyInputs): at least one input is required.
constexpr uint32_t COMMON_STUB_MINIMUM_INPUTS = 1;

// Intended assertion condition text of each corruption class, as GV-4 prescribes it.
// `ASSERTION FAILED: ` is the prefix the assertion printer writes before the condition
// (libpandabase macros.h, debug.cpp) and the condition is matched with the GTest
// death-test regexes below, so every regex metacharacter of the condition text is
// escaped. Only the condition is matched; an unrelated assertion of another condition
// therefore cannot satisfy a death case.
constexpr const char *DEATH_CONDITION_DUPLICATE_BLOCK_ID =
    "ASSERTION FAILED: seen_\\[block->GetId\\(\\)\\] == 0";  // arksteed_graph_verifier.h, PreProcessBlock
constexpr const char *DEATH_CONDITION_EMPTY_PHI =
    "ASSERTION FAILED: GetInputCount\\(\\) > 0";  // arksteed_opcode.h, PhiVertex
constexpr const char *DEATH_CONDITION_PHI_REPRESENTATION =
    "ASSERTION FAILED: GetInput\\(i\\)->GetValueRepresentation\\(\\) == repr";  // arksteed_opcode.h, PhiVertex
constexpr const char *DEATH_CONDITION_NULL_PHI_INPUT =
    "ASSERTION FAILED: input != nullptr";  // arksteed_graph_verifier.h, ProcessVertex
constexpr const char *DEATH_CONDITION_CALL_LAYOUT =
    "ASSERTION FAILED: GetInputCount\\(\\) == FIRST_ARG_INDEX \\+ actualArgc_";  // arksteed_opcode.h, CallVertex
constexpr const char *DEATH_CONDITION_FIXED_INPUT_REPRESENTATION =
    "ASSERTION FAILED: input->GetValueRepresentation\\(\\) == expectedRepr";  // arksteed_opcode.h, CheckValueInput
constexpr const char *DEATH_CONDITION_COMMON_STUB_MINIMUM =
    "ASSERTION FAILED: GetInputCount\\(\\) >= 1";  // arksteed_opcode.h, CallCommonStubVertex
#endif

#ifdef NDEBUG
// Stable corruption-class label for the diagnostics of the Release control. The control
// runs the six defined corrupted shapes and the repaired twin of every class, so a
// failure message has to stay attributable to one corruption class.
const char *CorruptionClassId(CorruptionKind kind)
{
    switch (kind) {
        case CorruptionKind::DUPLICATE_BLOCK_ID:
            return "X1";
        case CorruptionKind::EMPTY_PHI:
            return "X2";
        case CorruptionKind::PHI_REPRESENTATION_MISMATCH:
            return "X3";
        case CorruptionKind::NULL_PHI_INPUT:
            return "X4";
        case CorruptionKind::CALL_LAYOUT_MISMATCH:
            return "X5";
        case CorruptionKind::CONVERSION_REPRESENTATION_MISMATCH:
            return "X6";
        case CorruptionKind::EMPTY_COMMON_STUB_CALL:
            return "X7";
        case CorruptionKind::BRANCH_REPRESENTATION_MISMATCH:
            return "X8";
        case CorruptionKind::VALID:
            break;
    }
    return "VALID";
}
#endif

// One vertex of the snapshot: identity, opcode, value representation, arity and the
// identity of every allocated in-range input slot. A null input is recorded as a null
// pointer and is never dereferenced.
struct VertexSnapshot {
    const Vertex *vertex {nullptr};
    VertexOpcode opcode {VertexOpcode::INVALID};
    ValueRepresentation representation {ValueRepresentation::NONE};
    uint32_t inputCount {0};
    std::vector<const ValueVertex *> inputs;

    bool operator==(const VertexSnapshot &other) const
    {
        return vertex == other.vertex && opcode == other.opcode &&
               representation == other.representation && inputCount == other.inputCount &&
               inputs == other.inputs;
    }
};

// One block of the snapshot: identity and id, the control vertex record, the ordered
// Phi composition and the ordered non-control composition. The control record carries
// its own opcode, representation, arity and input identities; a null control vertex is
// recorded as an empty record and is never dereferenced.
struct BlockSnapshot {
    const BB *block {nullptr};
    uint32_t id {INVALID_BLOCK_ID};
    VertexSnapshot control;
    std::vector<VertexSnapshot> phis;
    std::vector<VertexSnapshot> vertices;

    bool operator==(const BlockSnapshot &other) const
    {
        return block == other.block && id == other.id && control == other.control &&
               phis == other.phis && vertices == other.vertices;
    }
};

// Whole-graph snapshot. Constant pools are captured by ordered membership of full
// vertex records, so a pool that gains, loses or reorders a vertex is a snapshot
// difference, and so is any change to a member vertex's opcode, representation, arity
// or input slots.
struct GraphSnapshot {
    std::vector<BlockSnapshot> blocks;
    std::vector<VertexSnapshot> int32Constants;
    std::vector<VertexSnapshot> int64Constants;
    std::vector<VertexSnapshot> float64Constants;
    std::vector<VertexSnapshot> taggedConstants;
    std::vector<VertexSnapshot> heapConstants;

    bool operator==(const GraphSnapshot &other) const
    {
        return blocks == other.blocks && int32Constants == other.int32Constants &&
               int64Constants == other.int64Constants && float64Constants == other.float64Constants &&
               taggedConstants == other.taggedConstants && heapConstants == other.heapConstants;
    }
};

VertexSnapshot SnapshotVertex(const Vertex *vertex)
{
    VertexSnapshot snapshot;
    snapshot.vertex = vertex;
    snapshot.opcode = vertex->GetOpcode();
    snapshot.representation = vertex->GetValueRepresentation();
    snapshot.inputCount = vertex->GetInputCount();
    snapshot.inputs.reserve(snapshot.inputCount);
    for (uint32_t i = 0; i < snapshot.inputCount; ++i) {
        // In-range read only; a null slot stays a null pointer and is not dereferenced.
        snapshot.inputs.push_back(vertex->GetInput(i));
    }
    return snapshot;
}

// A null control vertex is recorded as an empty record instead of being dereferenced.
VertexSnapshot SnapshotNullableVertex(const Vertex *vertex)
{
    if (vertex == nullptr) {
        return VertexSnapshot {};
    }
    return SnapshotVertex(vertex);
}

GraphSnapshot SnapshotGraph(const Graph *graph)
{
    GraphSnapshot snapshot;
    for (const auto &entry : graph->GetInt32Constants()) {
        snapshot.int32Constants.push_back(SnapshotVertex(entry.second));
    }
    for (const auto &entry : graph->GetInt64Constants()) {
        snapshot.int64Constants.push_back(SnapshotVertex(entry.second));
    }
    for (const auto &entry : graph->GetFloat64Constants()) {
        snapshot.float64Constants.push_back(SnapshotVertex(entry.second));
    }
    for (const auto &entry : graph->GetTaggedConstants()) {
        snapshot.taggedConstants.push_back(SnapshotVertex(entry.second));
    }
    for (const auto &entry : graph->GetHeapConstants()) {
        snapshot.heapConstants.push_back(SnapshotVertex(entry.second));
    }

    for (BB *block : *graph) {
        BlockSnapshot blockSnapshot;
        blockSnapshot.block = block;
        blockSnapshot.id = block->GetId();
        blockSnapshot.control = SnapshotNullableVertex(block->GetControlVertex());
        for (PhiVertex *phi : block->GetPhis()) {
            blockSnapshot.phis.push_back(SnapshotVertex(phi));
        }
        for (NonControlVertex *vertex : block->GetVertices()) {
            blockSnapshot.vertices.push_back(SnapshotVertex(vertex));
        }
        snapshot.blocks.push_back(std::move(blockSnapshot));
    }
    return snapshot;
}

std::string Describe(const VertexSnapshot &vertex)
{
    std::ostringstream out;
    out << "@" << static_cast<const void *>(vertex.vertex)
        << " opcode=" << static_cast<uint32_t>(vertex.opcode)
        << " repr=" << ValueRepresentationName(vertex.representation) << " inputs=" << vertex.inputCount << " [";
    for (const ValueVertex *input : vertex.inputs) {
        out << static_cast<const void *>(input) << " ";
    }
    out << "]";
    return out.str();
}

std::string Describe(const GraphSnapshot &snapshot)
{
    std::ostringstream out;
    out << "GraphSnapshot{blocks=" << snapshot.blocks.size() << " int32=" << snapshot.int32Constants.size()
        << " int64=" << snapshot.int64Constants.size() << " float64=" << snapshot.float64Constants.size()
        << " tagged=" << snapshot.taggedConstants.size() << " heap=" << snapshot.heapConstants.size();
    auto printList = [&out](const char *kind, const std::vector<VertexSnapshot> &vertices) {
        for (const VertexSnapshot &vertex : vertices) {
            out << "\n  " << kind << " " << Describe(vertex);
        }
    };
    printList("int32-const", snapshot.int32Constants);
    printList("int64-const", snapshot.int64Constants);
    printList("float64-const", snapshot.float64Constants);
    printList("tagged-const", snapshot.taggedConstants);
    printList("heap-const", snapshot.heapConstants);
    for (const BlockSnapshot &block : snapshot.blocks) {
        out << "\n  block id=" << block.id << " @" << static_cast<const void *>(block.block)
            << " control=[" << Describe(block.control) << "] phis=" << block.phis.size()
            << " vertices=" << block.vertices.size();
        for (const VertexSnapshot &phi : block.phis) {
            out << "\n    phi " << Describe(phi);
        }
        for (const VertexSnapshot &vertex : block.vertices) {
            out << "\n    vertex " << Describe(vertex);
        }
    }
    out << "}";
    return out.str();
}

// Fixture facts derived from the snapshot: the inventory the positive case prints and
// checks, plus the count of uninitialized in-range input slots (which must stay zero
// for a valid fixture).
struct Inventory {
    size_t blocks {0};
    size_t blocksWithControl {0};
    size_t phis {0};
    size_t nonControlVertices {0};
    size_t int32Constants {0};
    size_t int64Constants {0};
    size_t float64Constants {0};
    size_t taggedConstants {0};
    size_t heapConstants {0};
    size_t nullInputSlots {0};
};

void CountVertexInputs(const VertexSnapshot &vertex, Inventory &inventory)
{
    for (const ValueVertex *input : vertex.inputs) {
        if (input == nullptr) {
            ++inventory.nullInputSlots;
        }
    }
}

Inventory Summarize(const GraphSnapshot &snapshot)
{
    Inventory inventory;
    inventory.blocks = snapshot.blocks.size();
    inventory.int32Constants = snapshot.int32Constants.size();
    inventory.int64Constants = snapshot.int64Constants.size();
    inventory.float64Constants = snapshot.float64Constants.size();
    inventory.taggedConstants = snapshot.taggedConstants.size();
    inventory.heapConstants = snapshot.heapConstants.size();
    // The slot inventory covers every captured vertex: constant pools, control,
    // Phi and non-control positions all contribute their in-range input slots.
    for (const std::vector<VertexSnapshot> *pool :
         {&snapshot.int32Constants, &snapshot.int64Constants, &snapshot.float64Constants,
          &snapshot.taggedConstants, &snapshot.heapConstants}) {
        for (const VertexSnapshot &vertex : *pool) {
            CountVertexInputs(vertex, inventory);
        }
    }
    for (const BlockSnapshot &block : snapshot.blocks) {
        if (block.control.vertex != nullptr) {
            ++inventory.blocksWithControl;
        }
        CountVertexInputs(block.control, inventory);
        inventory.phis += block.phis.size();
        inventory.nonControlVertices += block.vertices.size();
        for (const VertexSnapshot &phi : block.phis) {
            CountVertexInputs(phi, inventory);
        }
        for (const VertexSnapshot &vertex : block.vertices) {
            CountVertexInputs(vertex, inventory);
        }
    }
    return inventory;
}

}  // namespace

class ArkSteedGraphVerifierTest : public testing::Test {
protected:
    // Fixture A handle: the graph is owned by the chunk, and every pointer below stays
    // valid for the lifetime of the fixture (and therefore of one case).
    struct Fixture {
        Graph *graph {nullptr};
        BB *entry {nullptr};
        BB *deoptBlock {nullptr};
        BB *jumpBlock {nullptr};
        PhiVertex *phi {nullptr};
        U32ToTaggedNumberVertex *u32ToTagged {nullptr};
        CallVertex *call {nullptr};
        CallRuntimeVertex *runtimeCall {nullptr};
        CallCommonStubVertex *commonStub {nullptr};
        BranchIfInt32CompareVertex *branch {nullptr};
        DeoptVertex *deopt {nullptr};
        JumpVertex *jump {nullptr};
    };

    NativeAreaAllocator nativeArea_;
    Chunk chunk_ {&nativeArea_};

    // Allocates a vertex with the requested input arity and fills every allocated input
    // slot. The returned pointer is checked here, before the slots are written, so a
    // failed allocation never becomes a partially initialized vertex.
    template <typename Derived, typename... Args>
    Derived *NewVertex(const std::vector<ValueVertex *> &inputs, Args &&...args)
    {
        Derived *vertex = Vertex::New<Derived>(&chunk_, inputs.size(), std::forward<Args>(args)...);
        if (vertex == nullptr) {
            return nullptr;
        }
        uint32_t index = 0;
        for (ValueVertex *input : inputs) {
            vertex->SetInput(index++, input);
        }
        return vertex;
    }

    // Fresh fixture A. `corrupt = false` builds the repaired twin of the selected class:
    // every prerequisite holds and the verifier must complete. `corrupt = true` adds the
    // single controlled violation of that class (GV-2, GV-4). The valid shape is the
    // repaired twin of every class, because each violation removes exactly one property.
    Fixture BuildFixture(CorruptionKind kind, bool corrupt)
    {
        if (kind == CorruptionKind::VALID && corrupt) {
            ADD_FAILURE() << "the valid shape of fixture A has no corruption to apply";
            return Fixture {};
        }

        Fixture fixture;
        fixture.graph = Graph::New(&chunk_);
        fixture.entry = BB::New(&chunk_);
        fixture.deoptBlock = BB::New(&chunk_);
        fixture.jumpBlock = BB::New(&chunk_);
        if (fixture.graph == nullptr || fixture.entry == nullptr || fixture.deoptBlock == nullptr ||
            fixture.jumpBlock == nullptr) {
            ADD_FAILURE() << "fixture A construction failed: Graph::New or BB::New returned null";
            return Fixture {};
        }

        // All three blocks first receive their initial valid id through `Graph::Add`,
        // which allocates an id only while the block id is `INVALID_BLOCK_ID`. Only then
        // does X1 reuse the id of the entry block for the third block, so the corrupted
        // fixture keeps the same id-allocation state as its repaired twin (three ids are
        // handed out and `MaxBlockId` is 3 in both). The violated invariant is therefore
        // the uniqueness of block ids alone and not a read outside the verifier's
        // per-block table: the reused id stays below `MaxBlockId`.
        fixture.graph->Add(fixture.entry);
        fixture.graph->Add(fixture.deoptBlock);
        fixture.graph->Add(fixture.jumpBlock);
        if (corrupt && kind == CorruptionKind::DUPLICATE_BLOCK_ID) {
            fixture.jumpBlock->SetId(fixture.entry->GetId());
        }

        // Non-empty pools of every constant kind the processor walks: Int32Constants,
        // IntPtr/Int64Constants, Float64Constants, TaggedConstants and HeapConstants.
        ValueVertex *int32Value = fixture.graph->GetInt32Constant(7);
        ValueVertex *int64Value = fixture.graph->GetInt64Constant(0);
        ValueVertex *float64Value = fixture.graph->GetFloat64Constant(1.5);
        ValueVertex *taggedA = fixture.graph->GetTaggedConstant(JSTaggedValue::Undefined().GetRawData());
        ValueVertex *taggedB = fixture.graph->GetTaggedConstant(JSTaggedValue::Null().GetRawData());
        ValueVertex *heapValue = fixture.graph->GetHeapConstant(0, 0);
        // Every constant accessor forwards to `Vertex::New`, whose result is nullable.
        // Check all six before any consumer is built or the pool is traversed, so a
        // failed allocation is reported as a fixture failure instead of reaching a
        // snapshot or verifier dereference.
        if (int32Value == nullptr || int64Value == nullptr || float64Value == nullptr ||
            taggedA == nullptr || taggedB == nullptr || heapValue == nullptr) {
            ADD_FAILURE() << "fixture A construction failed: a constant accessor returned a null vertex";
            return Fixture {};
        }
        // The float64 pool is exercised by ProcessConstants even though no vertex takes
        // a float64 input in this shape.
        static_cast<void>(float64Value);

        // Input plan of the valid shape, declared independently of the production
        // predicates that the verifier applies (D2, D6): a Phi with two non-null TAGGED
        // predecessors, a call with the three leading slots and one actual argument, a
        // selected conversion with its INT32 input, a common-stub call with its glue
        // input and a selected branch with two INT32 inputs. At most one entry below is
        // replaced by the corrupted declaration of the selected class.
        std::vector<ValueVertex *> phiInputs {taggedA, taggedB};
        std::vector<ValueVertex *> callInputs {taggedA, taggedB, heapValue, int32Value};
        uint32_t callActualArgc = 1;
        std::vector<ValueVertex *> conversionInputs {int32Value};
        std::vector<ValueVertex *> commonStubInputs {int64Value};
        std::vector<ValueVertex *> branchInputs {int32Value, int32Value};
        if (corrupt) {
            switch (kind) {
                case CorruptionKind::EMPTY_PHI:
                    // X2: no predecessor at all, and therefore no input slot.
                    phiInputs.clear();
                    break;
                case CorruptionKind::PHI_REPRESENTATION_MISMATCH:
                    // X3: INT32 and TAGGED values cannot merge at one SSA join.
                    phiInputs = {int32Value, taggedA};
                    break;
                case CorruptionKind::NULL_PHI_INPUT:
                    // X4: the second input slot is allocated but never initialized.
                    phiInputs = {taggedA, nullptr};
                    break;
                case CorruptionKind::CALL_LAYOUT_MISMATCH:
                    // X5: three slots for an actual argument count of one, one slot short
                    // of the documented target/new.target/this/argument layout.
                    callInputs = {taggedA, taggedB, heapValue};
                    break;
                case CorruptionKind::CONVERSION_REPRESENTATION_MISMATCH:
                    // X6: the conversion takes a TAGGED value instead of its INT32 input.
                    conversionInputs = {taggedA};
                    break;
                case CorruptionKind::EMPTY_COMMON_STUB_CALL:
                    // X7: the common-stub call has no glue input.
                    commonStubInputs.clear();
                    break;
                case CorruptionKind::BRANCH_REPRESENTATION_MISMATCH:
                    // X8: the selected comparison takes TAGGED values instead of INT32.
                    branchInputs = {taggedA, taggedB};
                    break;
                case CorruptionKind::DUPLICATE_BLOCK_ID:
                case CorruptionKind::VALID:
                    // X1 is applied to the block ids above; the valid shape has nothing
                    // to apply.
                    break;
            }
        }

        // A well-formed Phi: two non-null TAGGED inputs with equal representation.
        fixture.phi = NewVertex<PhiVertex>(phiInputs, VirtualRegister(0));
        // A selected fixed-input non-control conversion: one INT32 input.
        fixture.u32ToTagged = NewVertex<U32ToTaggedNumberVertex>(conversionInputs);
        // A valid call: target, new.target, this, then one actual argument.
        fixture.call = NewVertex<CallVertex>(callInputs, callActualArgc);
        // A production-shaped runtime call with no input at all.
        fixture.runtimeCall = NewVertex<CallRuntimeVertex>({}, RTSTUB_ID(NewTaggedArray), SideEffectKind::SAFE_CALL);
        // A common-stub call with the implemented minimum of one input (the glue).
        fixture.commonStub =
            NewVertex<CallCommonStubVertex>(commonStubInputs, CommonStubID::ToBooleanTrue, SideEffectKind::SAFE_CALL);
        // A selected fixed-input control branch: two INT32 inputs.
        fixture.branch = NewVertex<BranchIfInt32CompareVertex>(branchInputs, fixture.deoptBlock,
                                                               fixture.jumpBlock, Condition::EQUAL);
        // A control Deopt with the fixed zero-input shape.
        fixture.deopt =
            NewVertex<DeoptVertex>({}, &chunk_, kungfu::DeoptType::INSUFFICIENTPROFILE, static_cast<uint32_t>(0));
        fixture.jump = NewVertex<JumpVertex>({}, fixture.deoptBlock);

        if (fixture.graph == nullptr || fixture.entry == nullptr || fixture.deoptBlock == nullptr ||
            fixture.jumpBlock == nullptr || fixture.phi == nullptr || fixture.u32ToTagged == nullptr ||
            fixture.call == nullptr || fixture.runtimeCall == nullptr || fixture.commonStub == nullptr ||
            fixture.branch == nullptr || fixture.deopt == nullptr || fixture.jump == nullptr) {
            ADD_FAILURE() << "fixture A construction failed: Vertex::New returned a null vertex";
            return Fixture {};
        }

        fixture.entry->AddPhiVertex(fixture.phi);
        fixture.entry->AddVertex(fixture.u32ToTagged);
        fixture.entry->AddVertex(fixture.call);
        fixture.entry->AddVertex(fixture.runtimeCall);
        fixture.entry->AddVertex(fixture.commonStub);
        fixture.entry->SetControlVertex(fixture.branch);
        fixture.deoptBlock->SetControlVertex(fixture.deopt);
        fixture.jumpBlock->SetControlVertex(fixture.jump);
        return fixture;
    }

    // Runs the production verifier through the production processor on a fresh
    // instance, so `seen_` is never reused between runs. There is no boolean
    // acceptance result: completion is success and a Debug assertion abort is the
    // negative observation of the death cases.
    void RunVerifier(Graph *graph)
    {
        GraphProcessor<ArkSteedGraphVerifier> verifier(graph->GetChunk());
        verifier.Run(graph);
    }

#ifndef NDEBUG
    // Sensitivity half of a Debug death case (GV-5): the repaired twin of one corruption
    // class completes the verifier run and is not mutated, so removing the violation turns
    // the observed death into completion. It runs in the parent process, before the
    // corrupted graph is observed, and the non-fatal expectation keeps the case red
    // without aborting it.
    void ExpectRepairedTwinCompletesUnchanged(const Fixture &repaired, const char *classId)
    {
        const GraphSnapshot before = SnapshotGraph(repaired.graph);
        RunVerifier(repaired.graph);
        const GraphSnapshot after = SnapshotGraph(repaired.graph);
        EXPECT_TRUE(before == after) << classId << ": the verifier changed the repaired twin\nbefore:\n"
                                     << Describe(before) << "\nafter:\n"
                                     << Describe(after);
    }
#endif

#ifdef NDEBUG
    // Release half of the mode split (GV-6, D5). With every `ASSERT` disabled, the
    // production verifier must complete on the requested fixture and must not change it.
    // Every call builds a fresh fixture and runs a fresh processor, so no `seen_` state and
    // no graph state survives between the requested shapes. `corrupt = true` requests the
    // controlled violation of `kind`; the Release case below requests only the six defined
    // corrupted shapes and the repaired twin of every class. A construction failure is
    // reported as a fixture error and cannot be mistaken for a verifier outcome.
    void ExpectFixtureCompletesUnchanged(CorruptionKind kind, bool corrupt, const char *classId)
    {
        Fixture fixture = BuildFixture(kind, corrupt);
        ASSERT_NE(fixture.graph, nullptr) << classId << ": the Release fixture could not be built";
        const GraphSnapshot before = SnapshotGraph(fixture.graph);
        RunVerifier(fixture.graph);
        const GraphSnapshot after = SnapshotGraph(fixture.graph);
        EXPECT_TRUE(before == after) << classId << ": the verifier changed the Release fixture\nbefore:\n"
                                     << Describe(before) << "\nafter:\n"
                                     << Describe(after);
    }
#endif
};

/**
 * @tc.name: ArkSteedGraphVerifierTest.ValidGraphPassesEveryProcessorStageWithoutMutation
 * @tc.desc: fixture A reaches every GraphProcessor position (all constant pools, Phi,
 *           non-control and control vertices, pre/post hooks) and the production
 *           ArkSteedGraphVerifier accepts it without abort and without mutating the graph,
 *           in x64 Debug and x64 Release. The positive control pins the owner-corrected
 *           contract: zero-input Deopt, zero-input runtime call, four-slot one-argument
 *           Call, common stub with at least one input, one-INT32 conversion and two-INT32
 *           branch (GV-3).
 * @tc.type: FUNC
 */
HWTEST_F_L0(ArkSteedGraphVerifierTest, ValidGraphPassesEveryProcessorStageWithoutMutation)
{
    Fixture fixture = BuildFixture(CorruptionKind::VALID, false);
    ASSERT_NE(fixture.graph, nullptr) << "fixture A construction failed";

    const GraphSnapshot before = SnapshotGraph(fixture.graph);
    const Inventory inventory = Summarize(before);

    std::cout << "[ArkSteedGraphVerifierTest inventory] blocks=" << inventory.blocks
              << " blocksWithControl=" << inventory.blocksWithControl << " phis=" << inventory.phis
              << " nonControlVertices=" << inventory.nonControlVertices
              << " int32Constants=" << inventory.int32Constants << " int64Constants=" << inventory.int64Constants
              << " float64Constants=" << inventory.float64Constants
              << " taggedConstants=" << inventory.taggedConstants << " heapConstants=" << inventory.heapConstants
              << " nullInputSlots=" << inventory.nullInputSlots << "\n";

    // Inventory: every processor position is populated, and every allocated input slot
    // is initialized.
    EXPECT_EQ(inventory.blocks, 3U) << "fixture A must have at least two blocks";
    EXPECT_EQ(inventory.blocksWithControl, inventory.blocks) << "every block must carry a control vertex";
    EXPECT_EQ(inventory.phis, 1U) << "the valid graph must contain one Phi";
    EXPECT_EQ(inventory.nonControlVertices, 4U) << "the valid graph must contain a non-control composition";
    EXPECT_EQ(inventory.int32Constants, 1U);
    EXPECT_EQ(inventory.int64Constants, 1U);
    EXPECT_EQ(inventory.float64Constants, 1U);
    EXPECT_EQ(inventory.taggedConstants, 2U);
    EXPECT_EQ(inventory.heapConstants, 1U);
    EXPECT_EQ(inventory.nullInputSlots, 0U) << "a valid fixture must initialize every allocated input slot";
    EXPECT_GE(fixture.graph->MaxBlockId(), fixture.graph->NumBlocks())
        << "every block id must stay below MaxBlockId, the size of the verifier's seen_";

    // Phi: two non-null TAGGED predecessors.
    ASSERT_NE(fixture.phi, nullptr);
    EXPECT_EQ(fixture.phi->GetPredecessorCount(), 2U);
    ASSERT_NE(fixture.phi->GetInput(0), nullptr);
    ASSERT_NE(fixture.phi->GetInput(1), nullptr);
    EXPECT_EQ(fixture.phi->GetInput(0)->GetValueRepresentation(), ValueRepresentation::TAGGED);
    EXPECT_EQ(fixture.phi->GetInput(1)->GetValueRepresentation(), ValueRepresentation::TAGGED);

    // Zero-input Deopt is a valid control form.
    ASSERT_NE(fixture.deopt, nullptr);
    EXPECT_EQ(fixture.deopt->GetInputCount(), 0U);
    EXPECT_EQ(DeoptVertex::NUM_INPUTS, 0U);
    EXPECT_EQ(fixture.deoptBlock->GetControlVertex(), static_cast<ControlVertex *>(fixture.deopt));

    // Zero-input runtime call is a valid production form.
    ASSERT_NE(fixture.runtimeCall, nullptr);
    EXPECT_EQ(fixture.runtimeCall->GetInputCount(), 0U);

    // Call layout: FIRST_ARG_INDEX + actualArgc slots (target, new.target, this, args).
    ASSERT_NE(fixture.call, nullptr);
    EXPECT_EQ(CallVertex::FIRST_ARG_INDEX, 3U);
    EXPECT_EQ(fixture.call->GetActualArgc(), 1U);
    EXPECT_EQ(fixture.call->GetInputCount(), CallVertex::FIRST_ARG_INDEX + fixture.call->GetActualArgc());
    EXPECT_EQ(fixture.call->GetInputCount(), 4U);

    // Common-stub minimum and the selected fixed-input non-control conversion.
    ASSERT_NE(fixture.commonStub, nullptr);
    EXPECT_GE(fixture.commonStub->GetInputCount(), 1U);
    ASSERT_NE(fixture.u32ToTagged, nullptr);
    EXPECT_EQ(fixture.u32ToTagged->GetInputCount(), 1U);
    ASSERT_NE(fixture.u32ToTagged->GetInput(0), nullptr);
    EXPECT_EQ(fixture.u32ToTagged->GetInput(0)->GetValueRepresentation(), ValueRepresentation::INT32);

    // Selected fixed-input control branch: two INT32 inputs.
    ASSERT_NE(fixture.branch, nullptr);
    EXPECT_EQ(fixture.branch->GetInputCount(), 2U);
    ASSERT_NE(fixture.branch->GetInput(0), nullptr);
    ASSERT_NE(fixture.branch->GetInput(1), nullptr);
    EXPECT_EQ(fixture.branch->GetInput(0)->GetValueRepresentation(), ValueRepresentation::INT32);
    EXPECT_EQ(fixture.branch->GetInput(1)->GetValueRepresentation(), ValueRepresentation::INT32);
    EXPECT_EQ(fixture.entry->GetControlVertex(), static_cast<ControlVertex *>(fixture.branch));

    // Run the production verifier through the production processor.
    RunVerifier(fixture.graph);
    const GraphSnapshot after = SnapshotGraph(fixture.graph);
    EXPECT_TRUE(before == after) << "the verifier mutated the graph\nbefore:\n"
                                 << Describe(before) << "\nafter:\n"
                                 << Describe(after);

    // A second run on a fresh processor proves `seen_` is never reused and the graph
    // stays accepted.
    RunVerifier(fixture.graph);
    const GraphSnapshot afterSecond = SnapshotGraph(fixture.graph);
    EXPECT_TRUE(before == afterSecond) << "the second verifier run changed the graph\nbefore:\n"
                                       << Describe(before) << "\nafter:\n"
                                       << Describe(afterSecond);
}

// The eight controlled corruption classes X1-X8 of GV-4 are observed in Debug only:
// `ASSERT` is a no-op under `NDEBUG`, so the detection contract exists in Debug alone.
// Every case declares its own fixture facts before the observed verifier call, so a
// refused construction, a foreign assertion or another crash cannot be reported as
// detection, and every case first proves that its repaired twin completes unchanged.
#ifndef NDEBUG

/**
 * @tc.name: ArkSteedGraphVerifierTest.DuplicateBlockIdIsDetectedInDebug
 * @tc.desc: X1 (GV-4): two distinct blocks of one graph reuse a single in-range id, while
 *           block ids must be unique. The repaired twin with distinct ids completes
 *           unchanged; only the verifier run on the corrupted graph dies, with the id
 *           uniqueness condition text (arksteed_graph_verifier.h, PreProcessBlock).
 * @tc.type: FUNC
 */
HWTEST_F_L0(ArkSteedGraphVerifierTest, DuplicateBlockIdIsDetectedInDebug)
{
    // Repaired twin of X1: the violation is removed, so the same verifier must complete
    // the run without changing the graph.
    Fixture repaired = BuildFixture(CorruptionKind::DUPLICATE_BLOCK_ID, false);
    ASSERT_NE(repaired.graph, nullptr) << "the repaired twin of X1 could not be built";
    EXPECT_NE(repaired.entry->GetId(), repaired.jumpBlock->GetId())
        << "the repaired twin must give every block its own id";
    EXPECT_EQ(repaired.graph->MaxBlockId(), 3U)
        << "the repaired twin must allocate one initial id per block through Graph::Add";

    ExpectRepairedTwinCompletesUnchanged(repaired, "X1");

    Fixture corrupted = BuildFixture(CorruptionKind::DUPLICATE_BLOCK_ID, true);
    ASSERT_NE(corrupted.graph, nullptr) << "X1: the corrupted fixture could not be built";
    ASSERT_NE(corrupted.entry, nullptr);
    ASSERT_NE(corrupted.jumpBlock, nullptr);
    ASSERT_NE(corrupted.entry, corrupted.jumpBlock) << "X1: two distinct blocks must share the id";
    ASSERT_EQ(corrupted.entry->GetId(), corrupted.jumpBlock->GetId())
        << "X1: exactly one id must be reused by both blocks";
    ASSERT_LT(corrupted.jumpBlock->GetId(), corrupted.graph->MaxBlockId())
        << "X1: the reused id must stay inside the verifier's per-block table";
    ASSERT_EQ(corrupted.graph->NumBlocks(), 3U);
    ASSERT_EQ(corrupted.graph->MaxBlockId(), 3U)
        << "X1: every block must first take its initial valid id through Graph::Add, so the "
           "corruption changes id uniqueness only and not id allocation";

    // Detection: the death must come from the verifier run on the completed fixture.
    ASSERT_DEATH(RunVerifier(corrupted.graph), DEATH_CONDITION_DUPLICATE_BLOCK_ID);
}

/**
 * @tc.name: ArkSteedGraphVerifierTest.EmptyPhiIsDetectedInDebug
 * @tc.desc: X2 (GV-4): a Phi has no input at all, while a merge vertex must have at least
 *           one. The repaired twin with two TAGGED inputs completes unchanged; only the
 *           verifier run on the corrupted graph dies, with the non-empty condition text
 *           (arksteed_opcode.h, PhiVertex::VerifyInputs).
 * @tc.type: FUNC
 */
HWTEST_F_L0(ArkSteedGraphVerifierTest, EmptyPhiIsDetectedInDebug)
{
    // Repaired twin of X2: the violation is removed, so the same verifier must complete
    // the run without changing the graph.
    Fixture repaired = BuildFixture(CorruptionKind::EMPTY_PHI, false);
    ASSERT_NE(repaired.graph, nullptr) << "the repaired twin of X2 could not be built";
    ASSERT_NE(repaired.phi, nullptr);
    EXPECT_EQ(repaired.phi->GetInputCount(), 2U) << "the repaired Phi must have both TAGGED predecessors";
    EXPECT_EQ(repaired.phi->GetInput(0)->GetValueRepresentation(), ValueRepresentation::TAGGED);

    ExpectRepairedTwinCompletesUnchanged(repaired, "X2");

    Fixture corrupted = BuildFixture(CorruptionKind::EMPTY_PHI, true);
    ASSERT_NE(corrupted.graph, nullptr) << "X2: the corrupted fixture could not be built";
    ASSERT_NE(corrupted.phi, nullptr);
    ASSERT_EQ(corrupted.graph->NumBlocks(), 3U);
    ASSERT_EQ(corrupted.phi->GetInputCount(), 0U) << "X2: the corrupted Phi must have no input at all";

    // Detection: the death must come from the verifier run on the completed fixture.
    ASSERT_DEATH(RunVerifier(corrupted.graph), DEATH_CONDITION_EMPTY_PHI);
}

/**
 * @tc.name: ArkSteedGraphVerifierTest.PhiRepresentationMismatchIsDetectedInDebug
 * @tc.desc: X3 (GV-4): the two inputs of one Phi are INT32 and TAGGED, while all inputs of
 *           a merge vertex must share one value representation. The repaired twin with two
 *           TAGGED inputs completes unchanged; only the verifier run on the corrupted graph
 *           dies, with the representation consistency condition text (arksteed_opcode.h,
 *           PhiVertex::VerifyInputs).
 * @tc.type: FUNC
 */
HWTEST_F_L0(ArkSteedGraphVerifierTest, PhiRepresentationMismatchIsDetectedInDebug)
{
    // Repaired twin of X3: the violation is removed, so the same verifier must complete
    // the run without changing the graph.
    Fixture repaired = BuildFixture(CorruptionKind::PHI_REPRESENTATION_MISMATCH, false);
    ASSERT_NE(repaired.graph, nullptr) << "the repaired twin of X3 could not be built";
    ASSERT_NE(repaired.phi, nullptr);
    EXPECT_EQ(repaired.phi->GetInputCount(), 2U);
    EXPECT_EQ(repaired.phi->GetInput(0)->GetValueRepresentation(), ValueRepresentation::TAGGED)
        << "the repaired Phi must merge two TAGGED predecessors";
    EXPECT_EQ(repaired.phi->GetInput(1)->GetValueRepresentation(), ValueRepresentation::TAGGED);

    ExpectRepairedTwinCompletesUnchanged(repaired, "X3");

    Fixture corrupted = BuildFixture(CorruptionKind::PHI_REPRESENTATION_MISMATCH, true);
    ASSERT_NE(corrupted.graph, nullptr) << "X3: the corrupted fixture could not be built";
    ASSERT_NE(corrupted.phi, nullptr);
    ASSERT_EQ(corrupted.phi->GetInputCount(), 2U) << "X3: the violation is the representation, not the arity";
    ASSERT_NE(corrupted.phi->GetInput(0), nullptr);
    ASSERT_NE(corrupted.phi->GetInput(1), nullptr);
    ASSERT_EQ(corrupted.phi->GetInput(0)->GetValueRepresentation(), ValueRepresentation::INT32);
    ASSERT_EQ(corrupted.phi->GetInput(1)->GetValueRepresentation(), ValueRepresentation::TAGGED);
    ASSERT_NE(corrupted.phi->GetInput(0)->GetValueRepresentation(),
              corrupted.phi->GetInput(1)->GetValueRepresentation())
        << "X3: the merged predecessor values must disagree on the representation";

    // Detection: the death must come from the verifier run on the completed fixture.
    ASSERT_DEATH(RunVerifier(corrupted.graph), DEATH_CONDITION_PHI_REPRESENTATION);
}

/**
 * @tc.name: ArkSteedGraphVerifierTest.NullPhiInputIsDetectedInDebug
 * @tc.desc: X4 (GV-4): the second allocated input slot of a two-input Phi is null, while
 *           every input must be initialized. The repaired twin with two non-null TAGGED
 *           inputs completes unchanged; only the verifier run on the corrupted graph dies,
 *           with the non-null input condition text (arksteed_graph_verifier.h,
 *           ProcessVertex).
 * @tc.type: FUNC
 */
HWTEST_F_L0(ArkSteedGraphVerifierTest, NullPhiInputIsDetectedInDebug)
{
    // Repaired twin of X4: the violation is removed, so the same verifier must complete
    // the run without changing the graph.
    Fixture repaired = BuildFixture(CorruptionKind::NULL_PHI_INPUT, false);
    ASSERT_NE(repaired.graph, nullptr) << "the repaired twin of X4 could not be built";
    ASSERT_NE(repaired.phi, nullptr);
    EXPECT_EQ(repaired.phi->GetInputCount(), 2U);
    EXPECT_NE(repaired.phi->GetInput(1), nullptr) << "the repaired Phi must initialize both slots";

    ExpectRepairedTwinCompletesUnchanged(repaired, "X4");

    Fixture corrupted = BuildFixture(CorruptionKind::NULL_PHI_INPUT, true);
    ASSERT_NE(corrupted.graph, nullptr) << "X4: the corrupted fixture could not be built";
    ASSERT_NE(corrupted.phi, nullptr);
    ASSERT_EQ(corrupted.phi->GetInputCount(), 2U) << "X4: the slot exists, so the arity is unchanged";
    ASSERT_NE(corrupted.phi->GetInput(0), nullptr);
    ASSERT_EQ(corrupted.phi->GetInput(1), nullptr) << "X4: the second predecessor slot must stay uninitialized";

    // Detection: the death must come from the verifier run on the completed fixture.
    ASSERT_DEATH(RunVerifier(corrupted.graph), DEATH_CONDITION_NULL_PHI_INPUT);
}

/**
 * @tc.name: ArkSteedGraphVerifierTest.CallLayoutMismatchIsDetectedInDebug
 * @tc.desc: X5 (GV-4, D3): a CallVertex has three input slots for an actual argument count
 *           of one, while the layout is FIRST_ARG_INDEX + actualArgc with target,
 *           new.target and this as the three leading slots (FIRST_ARG_INDEX = 3). The
 *           repaired twin with four slots completes unchanged; only the verifier run on the
 *           corrupted graph dies, with the call-layout condition text (arksteed_opcode.h,
 *           CallVertex::VerifyInputs).
 * @tc.type: FUNC
 */
HWTEST_F_L0(ArkSteedGraphVerifierTest, CallLayoutMismatchIsDetectedInDebug)
{
    ASSERT_EQ(CallVertex::FIRST_ARG_INDEX, 3U) << "the documented layout keeps three leading slots";
    // Repaired twin of X5: the violation is removed, so the same verifier must complete
    // the run without changing the graph.
    Fixture repaired = BuildFixture(CorruptionKind::CALL_LAYOUT_MISMATCH, false);
    ASSERT_NE(repaired.graph, nullptr) << "the repaired twin of X5 could not be built";
    ASSERT_NE(repaired.call, nullptr);
    EXPECT_EQ(repaired.call->GetActualArgc(), 1U);
    EXPECT_EQ(repaired.call->GetInputCount(), CallVertex::FIRST_ARG_INDEX + repaired.call->GetActualArgc())
        << "the repaired call must fill the target, new.target, this and argument slots";

    ExpectRepairedTwinCompletesUnchanged(repaired, "X5");

    Fixture corrupted = BuildFixture(CorruptionKind::CALL_LAYOUT_MISMATCH, true);
    ASSERT_NE(corrupted.graph, nullptr) << "X5: the corrupted fixture could not be built";
    ASSERT_NE(corrupted.call, nullptr);
    ASSERT_EQ(corrupted.call->GetActualArgc(), 1U) << "X5: the violation is the slot count, not the argument count";
    ASSERT_EQ(corrupted.call->GetInputCount(), 3U);
    ASSERT_NE(corrupted.call->GetInputCount(), CallVertex::FIRST_ARG_INDEX + corrupted.call->GetActualArgc())
        << "X5: the call is one slot short of its documented layout";

    // Detection: the death must come from the verifier run on the completed fixture.
    ASSERT_DEATH(RunVerifier(corrupted.graph), DEATH_CONDITION_CALL_LAYOUT);
}

/**
 * @tc.name: ArkSteedGraphVerifierTest.ConversionRepresentationMismatchIsDetectedInDebug
 * @tc.desc: X6 (GV-4): the selected fixed-input non-control conversion
 *           (U32ToTaggedNumberVertex) takes a TAGGED input, while its declared input
 *           representation is INT32. The repaired twin with one INT32 input completes
 *           unchanged; only the verifier run on the corrupted graph dies, with the declared
 *           input representation condition text (arksteed_opcode.h, CheckValueInput reached
 *           through the non-control dispatch).
 * @tc.type: FUNC
 */
HWTEST_F_L0(ArkSteedGraphVerifierTest, ConversionRepresentationMismatchIsDetectedInDebug)
{
    // Repaired twin of X6: the violation is removed, so the same verifier must complete
    // the run without changing the graph.
    Fixture repaired = BuildFixture(CorruptionKind::CONVERSION_REPRESENTATION_MISMATCH, false);
    ASSERT_NE(repaired.graph, nullptr) << "the repaired twin of X6 could not be built";
    ASSERT_NE(repaired.u32ToTagged, nullptr);
    EXPECT_EQ(repaired.u32ToTagged->GetInputCount(), 1U);
    ASSERT_NE(repaired.u32ToTagged->GetInput(0), nullptr);
    EXPECT_EQ(repaired.u32ToTagged->GetInput(0)->GetValueRepresentation(), ValueRepresentation::INT32)
        << "the repaired conversion must provide the INT32 input it declares";

    ExpectRepairedTwinCompletesUnchanged(repaired, "X6");

    Fixture corrupted = BuildFixture(CorruptionKind::CONVERSION_REPRESENTATION_MISMATCH, true);
    ASSERT_NE(corrupted.graph, nullptr) << "X6: the corrupted fixture could not be built";
    ASSERT_NE(corrupted.u32ToTagged, nullptr);
    ASSERT_EQ(U32ToTaggedNumberVertex::INPUT_TYPES[0], ValueRepresentation::INT32)
        << "the selected conversion declares one INT32 input";
    ASSERT_EQ(corrupted.u32ToTagged->GetInputCount(), 1U) << "X6: the violation is the representation, not the arity";
    ASSERT_NE(corrupted.u32ToTagged->GetInput(0), nullptr);
    ASSERT_EQ(corrupted.u32ToTagged->GetInput(0)->GetValueRepresentation(), ValueRepresentation::TAGGED);
    ASSERT_NE(corrupted.u32ToTagged->GetInput(0)->GetValueRepresentation(), U32ToTaggedNumberVertex::INPUT_TYPES[0])
        << "X6: the supplied value must contradict the declared input representation";

    // Detection: the death must come from the verifier run on the completed fixture.
    ASSERT_DEATH(RunVerifier(corrupted.graph), DEATH_CONDITION_FIXED_INPUT_REPRESENTATION);
}

/**
 * @tc.name: ArkSteedGraphVerifierTest.EmptyCommonStubCallIsDetectedInDebug
 * @tc.desc: X7 (GV-4): a common-stub call has no input at all, while the implemented
 *           minimum of this vertex is one input (the glue). The repaired twin with one
 *           input completes unchanged; only the verifier run on the corrupted graph dies,
 *           with the minimum condition text (arksteed_opcode.h,
 *           CallCommonStubVertex::VerifyInputs).
 * @tc.type: FUNC
 */
HWTEST_F_L0(ArkSteedGraphVerifierTest, EmptyCommonStubCallIsDetectedInDebug)
{
    // Repaired twin of X7: the violation is removed, so the same verifier must complete
    // the run without changing the graph.
    Fixture repaired = BuildFixture(CorruptionKind::EMPTY_COMMON_STUB_CALL, false);
    ASSERT_NE(repaired.graph, nullptr) << "the repaired twin of X7 could not be built";
    ASSERT_NE(repaired.commonStub, nullptr);
    EXPECT_EQ(repaired.commonStub->GetInputCount(), 1U) << "the repaired call must carry its glue input";
    EXPECT_GE(repaired.commonStub->GetInputCount(), COMMON_STUB_MINIMUM_INPUTS);

    ExpectRepairedTwinCompletesUnchanged(repaired, "X7");

    Fixture corrupted = BuildFixture(CorruptionKind::EMPTY_COMMON_STUB_CALL, true);
    ASSERT_NE(corrupted.graph, nullptr) << "X7: the corrupted fixture could not be built";
    ASSERT_NE(corrupted.commonStub, nullptr);
    ASSERT_EQ(corrupted.commonStub->GetInputCount(), 0U) << "X7: the corrupted call must have no input at all";
    ASSERT_LT(corrupted.commonStub->GetInputCount(), COMMON_STUB_MINIMUM_INPUTS)
        << "X7: the corrupted call must stay below the implemented minimum";

    // Detection: the death must come from the verifier run on the completed fixture.
    ASSERT_DEATH(RunVerifier(corrupted.graph), DEATH_CONDITION_COMMON_STUB_MINIMUM);
}

/**
 * @tc.name: ArkSteedGraphVerifierTest.BranchRepresentationMismatchIsDetectedInDebug
 * @tc.desc: X8 (GV-4): the selected fixed-input control branch (BranchIfInt32CompareVertex)
 *           takes two TAGGED inputs, while its declared input representations are INT32. The
 *           repaired twin with two INT32 inputs completes unchanged; only the verifier run on
 *           the corrupted graph dies, with the declared input representation condition text
 *           (arksteed_opcode.h, CheckValueInput reached through the control dispatch).
 * @tc.type: FUNC
 */
HWTEST_F_L0(ArkSteedGraphVerifierTest, BranchRepresentationMismatchIsDetectedInDebug)
{
    // Repaired twin of X8: the violation is removed, so the same verifier must complete
    // the run without changing the graph.
    Fixture repaired = BuildFixture(CorruptionKind::BRANCH_REPRESENTATION_MISMATCH, false);
    ASSERT_NE(repaired.graph, nullptr) << "the repaired twin of X8 could not be built";
    ASSERT_NE(repaired.branch, nullptr);
    EXPECT_EQ(repaired.branch->GetInputCount(), 2U);
    ASSERT_NE(repaired.branch->GetInput(0), nullptr);
    ASSERT_NE(repaired.branch->GetInput(1), nullptr);
    EXPECT_EQ(repaired.branch->GetInput(0)->GetValueRepresentation(), ValueRepresentation::INT32)
        << "the repaired branch must compare two INT32 values";
    EXPECT_EQ(repaired.branch->GetInput(1)->GetValueRepresentation(), ValueRepresentation::INT32);

    ExpectRepairedTwinCompletesUnchanged(repaired, "X8");

    Fixture corrupted = BuildFixture(CorruptionKind::BRANCH_REPRESENTATION_MISMATCH, true);
    ASSERT_NE(corrupted.graph, nullptr) << "X8: the corrupted fixture could not be built";
    ASSERT_NE(corrupted.branch, nullptr);
    ASSERT_EQ(BranchIfInt32CompareVertex::INPUT_TYPES[0], ValueRepresentation::INT32);
    ASSERT_EQ(BranchIfInt32CompareVertex::INPUT_TYPES[1], ValueRepresentation::INT32)
        << "the selected branch declares two INT32 inputs";
    ASSERT_EQ(corrupted.branch->GetInputCount(), 2U) << "X8: the violation is the representation, not the arity";
    ASSERT_NE(corrupted.branch->GetInput(0), nullptr);
    ASSERT_NE(corrupted.branch->GetInput(1), nullptr);
    ASSERT_EQ(corrupted.branch->GetInput(0)->GetValueRepresentation(), ValueRepresentation::TAGGED);
    ASSERT_EQ(corrupted.branch->GetInput(1)->GetValueRepresentation(), ValueRepresentation::TAGGED);
    ASSERT_NE(corrupted.branch->GetInput(0)->GetValueRepresentation(),
              BranchIfInt32CompareVertex::INPUT_TYPES[0])
        << "X8: the supplied value must contradict the declared input representation";

    // Detection: the death must come from the verifier run on the completed fixture.
    ASSERT_DEATH(RunVerifier(corrupted.graph), DEATH_CONDITION_FIXED_INPUT_REPRESENTATION);
}

#endif  // !NDEBUG

// The defined Release control of GV-6. `ASSERT` is a no-op under `NDEBUG`, so the verifier
// no longer detects any of X1-X8; the contract this case pins is the by-mode split of D5:
// the six shapes whose unchecked path stays inside initialized storage must complete the
// production processor run without aborting and without changing the graph, and the
// repaired twin of every class must keep being accepted. No detection is claimed here, and
// completion is never reported as an observation of a violation.
#ifdef NDEBUG

/**
 * @tc.name: ArkSteedGraphVerifierTest.CorruptionFixturesWithoutActiveAssertionsDoNotAbortAndDoNotMutateTheGraph
 * @tc.desc: Release-only control (GV-6, D5). With all `ASSERT`s disabled, the production
 *           verifier completes on the six defined corrupted shapes X1, X3, X5, X6, X7 and
 *           X8 and on the repaired twin of every class X1-X8, and mutates none of them.
 *           The corrupted X2 (zero-input Phi) and X4 (null Phi input) shapes are never
 *           invoked: their unchecked verifier path reads an invalid input slot, and for X4
 *           dereferences a null input, so no Release outcome is promised for them. The case
 *           asserts completion and immutability only; it makes no detection claim.
 * @tc.type: FUNC
 */
HWTEST_F_L0(ArkSteedGraphVerifierTest, CorruptionFixturesWithoutActiveAssertionsDoNotAbortAndDoNotMutateTheGraph)
{
    // The six defined corrupted Release shapes of GV-6. Corrupted X2 (zero-input Phi) and
    // X4 (null Phi input) are deliberately absent: with assertions disabled their verifier
    // path reads an invalid or null input slot, so neither absence of an abort nor any
    // other defined behavior is promised for them (D5, GV-4).
    const CorruptionKind corruptedShapes[] = {
        CorruptionKind::DUPLICATE_BLOCK_ID,
        CorruptionKind::PHI_REPRESENTATION_MISMATCH,
        CorruptionKind::CALL_LAYOUT_MISMATCH,
        CorruptionKind::CONVERSION_REPRESENTATION_MISMATCH,
        CorruptionKind::EMPTY_COMMON_STUB_CALL,
        CorruptionKind::BRANCH_REPRESENTATION_MISMATCH,
    };
    for (CorruptionKind kind : corruptedShapes) {
        ExpectFixtureCompletesUnchanged(kind, true, CorruptionClassId(kind));
    }

    // The repaired twin of all eight classes, including X2 and X4: removing the violation
    // makes the shape a valid graph, which the Release verifier must accept unchanged.
    const CorruptionKind repairedShapes[] = {
        CorruptionKind::DUPLICATE_BLOCK_ID,
        CorruptionKind::EMPTY_PHI,
        CorruptionKind::PHI_REPRESENTATION_MISMATCH,
        CorruptionKind::NULL_PHI_INPUT,
        CorruptionKind::CALL_LAYOUT_MISMATCH,
        CorruptionKind::CONVERSION_REPRESENTATION_MISMATCH,
        CorruptionKind::EMPTY_COMMON_STUB_CALL,
        CorruptionKind::BRANCH_REPRESENTATION_MISMATCH,
    };
    for (CorruptionKind kind : repairedShapes) {
        ExpectFixtureCompletesUnchanged(kind, false, CorruptionClassId(kind));
    }
}

#endif  // NDEBUG

}  // namespace panda::ecmascript::arksteed
