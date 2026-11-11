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
#include "ecmascript/frames.h"
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

// Unit domain of the shared normal Debug integrity contract of
// `ArkSteedGraphVerifier`. The cases
// use fixture A: a real NativeAreaAllocator/Chunk/Graph with real IR vertices and
// no EcmaVM, no JSThread, no EcmaHandleScope, no assembler and no bytecode asset,
// following the VM-free precedent of `write_barrier_value_kind_test.cpp`.
//
//   * `RunVerifier` drives the production `ArkSteedGraphVerifier` through
//     `GraphProcessor<ArkSteedGraphVerifier>`, exactly as a real compilation does.
//   * `BuildFixture(kind, corrupt)` builds fixture A through the public
//     `Graph`/`BB`/`Vertex::New` surfaces. The graph is a finalized pre-allocation
//     graph and satisfies the whole strengthened contract: every block has an
//     owner-consistent control vertex, the entry block branches to two edge-split
//     targets that carry no Phis and jump to a merge block, the merge Phi has one
//     input per ordinary predecessor, an indexed `InitialValue` parameter is
//     installed in the entry block, a registered throwable reaches a graph-local
//     exception handler whose installed catch Phi has the exceptional domain
//     arity, all five constant pools hold their own constant category, every
//     referenced value resolves to an installed executable member or to a pool
//     constant, and a zero-input runtime call, a zero-input Deopt and a common-stub
//     call with its glue input are present. `corrupt = false` yields that graph; `corrupt = true`
//     adds exactly one controlled violation of the selected class X1-X8 of the
//     concrete-input/graph contract. The repaired twin of every class is the valid
//     shape, because each violation removes exactly one property of it.
//   * `SnapshotGraph` captures the structural facts that a verifier must not
//     change: block order/identity and ids, the ordered predecessor lists, the
//     full ordered membership and per-vertex structure of every constant pool, the
//     indexed parameter list, the control vertex of every block, the ordered Phi
//     and non-control compositions, and, per captured vertex, opcode, value
//     representation, input count and each in-range input identity. It never calls
//     `Vertex::GetId()`, whose register-allocation metadata is absent from fixture
//     A. Snapshot records are taken for constants, control, Phi and non-control
//     vertices alike, so a changed opcode, representation, arity or input slot is
//     visible even when the vertex pointer itself is unchanged.
//
// Mode layout of this stage: the positive control runs in x64 Debug and x64
// Release; the eight death cases are declared under `#ifndef NDEBUG` only,
// because `ASSERT` is a no-op under `NDEBUG` and Release establishes trusted
// internal IR instead of an integrity result. There is no Release corruption or
// no-abort control: Release promises no outcome at all for malformed internal IR,
// and no case in this domain may claim one.
//
// Independent oracle: the expectations below are the documented contracts of
// the verifier and of the exercised vertex types (`arksteed_graph_verifier.h`,
// `arksteed_opcode.h`), not a boolean recomputed from a production predicate. The
// call layout is the owner-corrected contract
// `GetInputCount() == CallVertex::FIRST_ARG_INDEX + GetActualArgc()` with
// `FIRST_ARG_INDEX = 3`; a runtime call with zero inputs and a Deopt with zero
// inputs are valid forms, and no minimum from the (formerly erroneous) verifier
// comment is asserted.

namespace {

// The corruption classes of the concrete-input/graph contract plus the
// un-corrupted valid shape. `corrupt = false` builds the repaired twin of the
// selected class; the repaired twin of every class is the valid shape.
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

// Intended assertion condition text of each corruption class.
// `ASSERTION FAILED: ` is the prefix the assertion printer writes before the
// condition (libpandabase macros.h, debug.cpp) and the condition is matched with
// the GTest death-test regexes below, so every regex metacharacter of the
// condition text is escaped. Only the condition is matched; an unrelated
// assertion of another condition therefore cannot satisfy a death case.
//
// The four input-contract classes below (`EMPTY_PHI`, `PHI_REPRESENTATION`,
// `CALL_LAYOUT`, `COMMON_STUB_MINIMUM`) are declared by the shared non-asserting
// `CheckInputs()` hook of their opcode (`arksteed_opcode.h`), which the
// production `VerifyInputs()` asserts as `ASSERT(CheckInputs())`. The assertion
// printer therefore reports the hook name, not the individual condition the hook
// evaluates, and the four death regexes name that shared condition. The
// corruption class is still discriminated by the corrupted fixture (each case
// asserts its own corrupted shape before running the verifier) together with the
// repaired twin of the class, not by the message text.
constexpr const char *DEATH_CONDITION_DUPLICATE_BLOCK_ID =
    "ASSERTION FAILED: scratch_->seen\\[block->GetId\\(\\)\\] == 0";  // arksteed_graph_verifier.h, PreProcessBlock
constexpr const char *DEATH_CONDITION_EMPTY_PHI =
    "ASSERTION FAILED: CheckInputs\\(\\)";  // arksteed_opcode.h, PhiVertex::CheckInputs (input count > 0)
constexpr const char *DEATH_CONDITION_PHI_REPRESENTATION =
    "ASSERTION FAILED: CheckInputs\\(\\)";  // arksteed_opcode.h, PhiVertex::CheckInputs (input representations agree)
constexpr const char *DEATH_CONDITION_NULL_PHI_INPUT =
    "ASSERTION FAILED: input != nullptr";  // arksteed_graph_verifier.h, ProcessVertex
// arksteed_opcode.h, JSCallVertexBase
constexpr const char *DEATH_CONDITION_CALL_LAYOUT =
    "ASSERTION FAILED: CheckInputs\\(\\)";  // arksteed_opcode.h, CallVertex::CheckInputs (stored layout arity)
constexpr const char *DEATH_CONDITION_FIXED_INPUT_REPRESENTATION =
    "ASSERTION FAILED: input->GetValueRepresentation\\(\\) == expectedRepr";  // declared INPUT_TYPES
constexpr const char *DEATH_CONDITION_COMMON_STUB_MINIMUM =
    "ASSERTION FAILED: CheckInputs\\(\\)";  // arksteed_opcode.h, CallCommonStubVertex::CheckInputs (at least one input)
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

// One block of the snapshot: identity and id, the ordered ordinary predecessor
// list, the control vertex record, the ordered Phi composition and the ordered
// non-control composition. The control record carries its own opcode,
// representation, arity and input identities; a null control vertex is recorded as
// an empty record and is never dereferenced.
struct BlockSnapshot {
    const BB *block {nullptr};
    uint32_t id {INVALID_BLOCK_ID};
    std::vector<const BB *> predecessors;
    VertexSnapshot control;
    std::vector<VertexSnapshot> phis;
    std::vector<VertexSnapshot> vertices;

    bool operator==(const BlockSnapshot &other) const
    {
        return block == other.block && id == other.id && predecessors == other.predecessors &&
               control == other.control && phis == other.phis && vertices == other.vertices;
    }
};

// Whole-graph snapshot. Constant pools are captured by ordered membership of full
// vertex records, so a pool that gains, loses or reorders a vertex is a snapshot
// difference, and so is any change to a member vertex's opcode, representation,
// arity or input slots. The indexed parameter list and `MaxBlockId` are captured
// for the same reason.
struct GraphSnapshot {
    uint32_t maxBlockId {0};
    std::vector<const ValueVertex *> parameters;
    std::vector<BlockSnapshot> blocks;
    std::vector<VertexSnapshot> int32Constants;
    std::vector<VertexSnapshot> int64Constants;
    std::vector<VertexSnapshot> float64Constants;
    std::vector<VertexSnapshot> taggedConstants;
    std::vector<VertexSnapshot> heapConstants;

    bool operator==(const GraphSnapshot &other) const
    {
        return maxBlockId == other.maxBlockId && parameters == other.parameters && blocks == other.blocks &&
               int32Constants == other.int32Constants && int64Constants == other.int64Constants &&
               float64Constants == other.float64Constants && taggedConstants == other.taggedConstants &&
               heapConstants == other.heapConstants;
    }
};

VertexSnapshot SnapshotVertex(const Vertex *vertex)
{
    VertexSnapshot snapshot;
    snapshot.vertex = vertex;
    snapshot.opcode = vertex->GetOpcode();
    snapshot.representation = vertex->GetValueRepresentation();
    snapshot.inputCount = static_cast<const Vertex *>(vertex)->GetInputCount();
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
    snapshot.maxBlockId = graph->MaxBlockId();
    for (size_t index = 0, count = graph->GetParameterCount(); index < count; ++index) {
        snapshot.parameters.push_back(graph->GetParameter(static_cast<uint32_t>(index)));
    }
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
        for (const BB *predecessor : block->GetPredecessors()) {
            blockSnapshot.predecessors.push_back(predecessor);
        }
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
    out << "@" << static_cast<const void *>(vertex.vertex) <<
        " opcode=" << static_cast<uint32_t>(vertex.opcode) <<
        " repr=" << ValueRepresentationName(vertex.representation) << " inputs=" << vertex.inputCount << " [";
    for (const ValueVertex *input : vertex.inputs) {
        out << static_cast<const void *>(input) << " ";
    }
    out << "]";
    return out.str();
}

std::string Describe(const GraphSnapshot &snapshot)
{
    std::ostringstream out;
    out << "GraphSnapshot{maxBlockId=" << snapshot.maxBlockId << " parameters=" << snapshot.parameters.size() <<
        " blocks=" << snapshot.blocks.size() << " int32=" << snapshot.int32Constants.size() <<
        " int64=" << snapshot.int64Constants.size() << " float64=" << snapshot.float64Constants.size() <<
        " tagged=" << snapshot.taggedConstants.size() << " heap=" << snapshot.heapConstants.size();
    for (const ValueVertex *parameter : snapshot.parameters) {
        out << "\n  parameter @" << static_cast<const void *>(parameter);
    }
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
        out << "\n  block id=" << block.id << " @" << static_cast<const void *>(block.block) <<
            " predecessors=" << block.predecessors.size() << " control=[" << Describe(block.control) <<
            "] phis=" << block.phis.size() << " vertices=" << block.vertices.size();
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

// Fixture facts derived from the snapshot: the inventory the positive case prints
// and checks, plus the count of uninitialized in-range input slots (which must stay
// zero for a valid fixture).
struct Inventory {
    size_t blocks {0};
    size_t blocksWithControl {0};
    size_t phis {0};
    size_t nonControlVertices {0};
    size_t parameters {0};
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
    inventory.parameters = snapshot.parameters.size();
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
    static constexpr int32_t sampleInt32Value = 7;
    static constexpr double sampleFloat64Value = 1.5;

    // Fixture A handle: the graph is owned by the chunk, and every pointer below stays
    // valid for the lifetime of the fixture (and therefore of one case).
    struct Fixture {
        Graph *graph {nullptr};
        BB *entry {nullptr};
        BB *ifTrue {nullptr};
        BB *ifFalse {nullptr};
        BB *merge {nullptr};
        BB *deoptBlock {nullptr};
        BB *catchBlock {nullptr};
        BB *handler {nullptr};
        ValueVertex *int32Value {nullptr};
        ValueVertex *int64Value {nullptr};
        ValueVertex *float64Value {nullptr};
        ValueVertex *taggedA {nullptr};
        ValueVertex *taggedB {nullptr};
        ValueVertex *heapValue {nullptr};
        InitialValueVertex *parameter {nullptr};
        PhiVertex *phi {nullptr};
        PhiVertex *catchPhi {nullptr};
        U32ToTaggedNumberVertex *u32ToTagged {nullptr};
        CallVertex *call {nullptr};
        CallRuntimeVertex *runtimeCall {nullptr};
        CallCommonStubVertex *commonStub {nullptr};
        BranchIfInt32CompareVertex *branch {nullptr};
        JumpVertex *trueJump {nullptr};
        JumpVertex *falseJump {nullptr};
        JumpVertex *mergeJump {nullptr};
        JumpVertex *catchJump {nullptr};
        ReturnVertex *handlerReturn {nullptr};
        DeoptVertex *deopt {nullptr};
    };

    NativeAreaAllocator nativeArea_;
    Chunk chunk_ {&nativeArea_};

    // Allocates a vertex with the requested input arity and fills every allocated input
    // slot. The returned pointer is checked here, before the slots are written, so a
    // failed allocation never becomes a partially initialized vertex. Ownership is
    // recorded by the caller, as `GraphBuilder::NewVertexImpl` records the same
    // relation: the owner of a listed entry is the block that lists it.
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

    void SetControl(BB *block, ControlVertex *vertex)
    {
        if (vertex != nullptr) {
            vertex->SetOwner(block);
        }
        block->SetControlVertex(vertex);
    }

    // Fresh fixture A. `corrupt = false` builds the repaired twin of the selected
    // class: every prerequisite of the strengthened contract holds and the verifier
    // must complete. `corrupt = true` adds the single controlled violation of that
    // class. The valid shape is the repaired twin of every class, because each
    // violation removes exactly one property of it.
    Fixture BuildFixture(CorruptionKind kind, bool corrupt)
    {
        if (kind == CorruptionKind::VALID && corrupt) {
            ADD_FAILURE() << "the valid shape of fixture A has no corruption to apply";
            return Fixture {};
        }

        Fixture fixture;
        fixture.graph = Graph::New(&chunk_);
        fixture.entry = BB::New(&chunk_);
        fixture.ifTrue = BB::New(&chunk_);
        fixture.ifFalse = BB::New(&chunk_);
        fixture.merge = BB::New(&chunk_);
        fixture.deoptBlock = BB::New(&chunk_);
        fixture.catchBlock = BB::New(&chunk_);
        fixture.handler = BB::New(&chunk_);
        if (fixture.graph == nullptr || fixture.entry == nullptr || fixture.ifTrue == nullptr ||
            fixture.ifFalse == nullptr || fixture.merge == nullptr || fixture.deoptBlock == nullptr ||
            fixture.catchBlock == nullptr || fixture.handler == nullptr) {
            ADD_FAILURE() << "fixture A construction failed: Graph::New or BB::New returned null";
            return Fixture {};
        }

        // Every block first receives its initial valid id through `Graph::Add`, which
        // allocates an id only while the block id is `INVALID_BLOCK_ID`. Only then does
        // X1 reuse the id of the entry block for the handler block, so the corrupted
        // fixture keeps the same id-allocation state as its repaired twin (seven ids
        // are handed out and `MaxBlockId` is 7 in both). The violated invariant is
        // therefore the uniqueness of block ids alone and not a read outside the
        // verifier's per-block table: the reused id stays below `MaxBlockId`.
        fixture.graph->Add(fixture.entry);
        fixture.graph->Add(fixture.ifTrue);
        fixture.graph->Add(fixture.ifFalse);
        fixture.graph->Add(fixture.merge);
        fixture.graph->Add(fixture.deoptBlock);
        fixture.graph->Add(fixture.catchBlock);
        fixture.graph->Add(fixture.handler);
        if (corrupt && kind == CorruptionKind::DUPLICATE_BLOCK_ID) {
            fixture.handler->SetId(fixture.entry->GetId());
        }

        // Non-empty pools of every constant kind the processor walks: Int32Constants,
        // IntPtr/Int64Constants, Float64Constants, TaggedConstants and HeapConstants.
        fixture.int32Value = fixture.graph->GetInt32Constant(sampleInt32Value);
        fixture.int64Value = fixture.graph->GetInt64Constant(0);
        fixture.float64Value = fixture.graph->GetFloat64Constant(sampleFloat64Value);
        fixture.taggedA = fixture.graph->GetTaggedConstant(JSTaggedValue::Undefined().GetRawData());
        fixture.taggedB = fixture.graph->GetTaggedConstant(JSTaggedValue::Null().GetRawData());
        fixture.heapValue = fixture.graph->GetHeapConstant(0, 0);
        // Every constant accessor forwards to `Vertex::New`, whose result is nullable.
        // Check all six before any consumer is built or the pool is traversed, so a
        // failed allocation is reported as a fixture failure instead of reaching a
        // snapshot or verifier dereference.
        if (fixture.int32Value == nullptr || fixture.int64Value == nullptr || fixture.float64Value == nullptr ||
            fixture.taggedA == nullptr || fixture.taggedB == nullptr || fixture.heapValue == nullptr) {
            ADD_FAILURE() << "fixture A construction failed: a constant accessor returned a null vertex";
            return Fixture {};
        }
        // The float64 pool is exercised by ProcessConstants even though no vertex takes
        // a float64 input in this shape.
        static_cast<void>(fixture.float64Value);

        // Input plan of the valid shape, declared independently of the production
        // predicates that the verifier applies: a merge Phi with two non-null TAGGED
        // predecessors, a call with the three leading slots and one actual argument, a
        // selected conversion with its INT32 input, a common-stub call with its glue
        // input and a selected branch with two INT32 inputs. At most one entry below is
        // replaced by the corrupted declaration of the selected class.
        std::vector<ValueVertex *> phiInputs {fixture.taggedA, fixture.taggedB};
        std::vector<ValueVertex *> callInputs {fixture.taggedA, fixture.taggedB, fixture.heapValue,
                                               fixture.int32Value};
        uint32_t callActualArgc = 1;
        std::vector<ValueVertex *> conversionInputs {fixture.int32Value};
        std::vector<ValueVertex *> commonStubInputs {fixture.int64Value};
        std::vector<ValueVertex *> branchInputs {fixture.int32Value, fixture.int32Value};
        if (corrupt) {
            switch (kind) {
                case CorruptionKind::EMPTY_PHI:
                    // X2: no predecessor at all, and therefore no input slot.
                    phiInputs.clear();
                    break;
                case CorruptionKind::PHI_REPRESENTATION_MISMATCH:
                    // X3: INT32 and TAGGED values cannot merge at one SSA join.
                    phiInputs = {fixture.int32Value, fixture.taggedA};
                    break;
                case CorruptionKind::NULL_PHI_INPUT:
                    // X4: the second input slot is allocated but never initialized.
                    phiInputs = {fixture.taggedA, nullptr};
                    break;
                case CorruptionKind::CALL_LAYOUT_MISMATCH:
                    // X5: three slots for an actual argument count of one, one slot short
                    // of the documented target/new.target/this/argument layout.
                    callInputs = {fixture.taggedA, fixture.taggedB, fixture.heapValue};
                    break;
                case CorruptionKind::CONVERSION_REPRESENTATION_MISMATCH:
                    // X6: the conversion takes a TAGGED value instead of its INT32 input.
                    conversionInputs = {fixture.taggedA};
                    break;
                case CorruptionKind::EMPTY_COMMON_STUB_CALL:
                    // X7: the common-stub call has no glue input.
                    commonStubInputs.clear();
                    break;
                case CorruptionKind::BRANCH_REPRESENTATION_MISMATCH:
                    // X8: the selected comparison takes TAGGED values instead of INT32.
                    branchInputs = {fixture.taggedA, fixture.taggedB};
                    break;
                case CorruptionKind::DUPLICATE_BLOCK_ID:
                case CorruptionKind::VALID:
                    // X1 is applied to the block ids above; the valid shape has nothing
                    // to apply.
                    break;
            }
        }

        // A well-formed merge Phi: two non-null TAGGED inputs with equal representation.
        fixture.phi = NewVertex<PhiVertex>(phiInputs, VirtualRegister(0));
        // The catch Phi of the synthetic exception handler: one incoming value, which is
        // the exceptional domain arity of the single registered throwable below.
        fixture.catchPhi = NewVertex<PhiVertex>({fixture.taggedB}, VirtualRegister(1));
        // A selected fixed-input non-control conversion: one INT32 input.
        fixture.u32ToTagged = NewVertex<U32ToTaggedNumberVertex>(conversionInputs);
        // A valid call: target, new.target, this, then one actual argument.
        fixture.call = NewVertex<CallVertex>(callInputs, callActualArgc);
        // A production-shaped runtime call with no input at all.
        fixture.runtimeCall = NewVertex<CallRuntimeVertex>({}, RTSTUB_ID(NewTaggedArray), SideEffectKind::SAFE_CALL);
        // A common-stub call with the implemented minimum of one input (the
        // first stub argument; glue is no longer an IR input).
        fixture.commonStub =
            NewVertex<CallCommonStubVertex>(commonStubInputs, CommonStubID::ToBooleanTrue, SideEffectKind::SAFE_CALL);
        // A selected fixed-input control branch: two INT32 inputs.
        fixture.branch = NewVertex<BranchIfInt32CompareVertex>(branchInputs, fixture.ifTrue, fixture.ifFalse,
                                                               Condition::EQUAL);
        // A control Deopt with the fixed zero-input shape.
        fixture.deopt =
            NewVertex<DeoptVertex>({}, &chunk_, kungfu::DeoptType::INSUFFICIENTPROFILE, static_cast<uint32_t>(0));
        fixture.trueJump = NewVertex<JumpVertex>({}, fixture.merge);
        fixture.falseJump = NewVertex<JumpVertex>({}, fixture.merge);
        fixture.mergeJump = NewVertex<JumpVertex>({}, fixture.deoptBlock);
        fixture.catchJump = NewVertex<JumpVertex>({}, fixture.handler);
        fixture.handlerReturn = NewVertex<ReturnVertex>({fixture.taggedA});
        // An indexed parameter descriptor installed in the entry block, exactly as
        // `GraphBuilder::InitializeStartBlock` installs every parameter.
        fixture.parameter =
            NewVertex<InitialValueVertex>(std::vector<ValueVertex *> {}, CALL_TARGET_FP_SLOT_INDEX);

        if (fixture.phi == nullptr || fixture.catchPhi == nullptr || fixture.u32ToTagged == nullptr ||
            fixture.call == nullptr || fixture.runtimeCall == nullptr || fixture.commonStub == nullptr ||
            fixture.branch == nullptr || fixture.deopt == nullptr || fixture.trueJump == nullptr ||
            fixture.falseJump == nullptr || fixture.mergeJump == nullptr || fixture.catchJump == nullptr ||
            fixture.handlerReturn == nullptr || fixture.parameter == nullptr) {
            ADD_FAILURE() << "fixture A construction failed: Vertex::New returned a null vertex";
            return Fixture {};
        }

        // Executable membership follows the production construction order, and every
        // entry is owned by the block that lists it.
        for (NonControlVertex *vertex : {static_cast<NonControlVertex *>(fixture.u32ToTagged),
                                        static_cast<NonControlVertex *>(fixture.call),
                                        static_cast<NonControlVertex *>(fixture.runtimeCall),
                                        static_cast<NonControlVertex *>(fixture.commonStub),
                                        static_cast<NonControlVertex *>(fixture.parameter)}) {
            vertex->SetOwner(fixture.entry);
            fixture.entry->AddVertex(vertex);
        }
        fixture.graph->AddParameter(fixture.parameter);
        SetControl(fixture.entry, fixture.branch);
        SetControl(fixture.ifTrue, fixture.trueJump);
        SetControl(fixture.ifFalse, fixture.falseJump);
        fixture.mergeJump->SetOwner(fixture.merge);
        SetControl(fixture.merge, fixture.mergeJump);
        SetControl(fixture.deoptBlock, fixture.deopt);
        fixture.catchPhi->SetOwner(fixture.catchBlock);
        fixture.catchBlock->AddPhiVertex(fixture.catchPhi);
        fixture.catchBlock->SetIsExceptionHandler(true);
        SetControl(fixture.catchBlock, fixture.catchJump);
        SetControl(fixture.handler, fixture.handlerReturn);
        fixture.merge->AddPhiVertex(fixture.phi);
        fixture.phi->SetOwner(fixture.merge);

        // One registered throwable reaches the graph-local synthetic exception handler:
        // the call occupies exceptional slot 0, which is also the domain of the handler
        // and the arity of its catch Phi. A catch block is reached only through the
        // exceptional edge, so it has no ordinary predecessor, while its jump to the
        // handler body is an ordinary control edge.
        fixture.call->LoadCatchBlock(fixture.catchBlock, 0);

        // Predecessor order follows the production construction order: the branch is the
        // single predecessor of both edge-split targets, each edge-split target is an
        // ordinary predecessor of the merge block at the index its unconditional control
        // vertex stores, the merge block is the predecessor of the deopt block, and the
        // exception handler is the predecessor of the handler body.
        fixture.ifTrue->AddPredecessor(fixture.entry);
        fixture.ifFalse->AddPredecessor(fixture.entry);
        fixture.trueJump->SetPredecessorId(fixture.merge->PredecessorCount());
        fixture.merge->AddPredecessor(fixture.ifTrue);
        fixture.falseJump->SetPredecessorId(fixture.merge->PredecessorCount());
        fixture.merge->AddPredecessor(fixture.ifFalse);
        fixture.mergeJump->SetPredecessorId(fixture.deoptBlock->PredecessorCount());
        fixture.deoptBlock->AddPredecessor(fixture.merge);
        fixture.catchJump->SetPredecessorId(fixture.handler->PredecessorCount());
        fixture.handler->AddPredecessor(fixture.catchBlock);
        return fixture;
    }

    // Runs the production verifier through the production processor on a fresh
    // instance, so the verifier's invocation-local state is never reused between runs.
    // There is no boolean acceptance result: completion is success and a Debug
    // assertion abort is the negative observation of the death cases.
    void RunVerifier(Graph *graph)
    {
        GraphProcessor<ArkSteedGraphVerifier> verifier(graph->GetChunk());
        verifier.Run(graph);
    }

#ifndef NDEBUG
    // Sensitivity half of a Debug death case: the repaired twin of one corruption
    // class completes the verifier run and is not mutated, so removing the violation
    // turns the observed death into completion. It runs in the parent process, before
    // the corrupted graph is observed, and the non-fatal expectation keeps the case red
    // without aborting it.
    void ExpectRepairedTwinCompletesUnchanged(const Fixture &repaired, const char *classId)
    {
        const GraphSnapshot before = SnapshotGraph(repaired.graph);
        RunVerifier(repaired.graph);
        const GraphSnapshot after = SnapshotGraph(repaired.graph);
        EXPECT_TRUE(before == after) << classId << ": the verifier changed the repaired twin\nbefore:\n" <<
            Describe(before) << "\nafter:\n" <<
            Describe(after);
    }
#endif
};

/**
 * @tc.name: ArkSteedGraphVerifierTest.ValidGraphPassesEveryProcessorStageWithoutMutation
 * @tc.desc: fixture A is a finalized pre-allocation graph with real ownership, CFG
 *           predecessor multiplicity, edge splitting, merge-Phi arity, an indexed
 *           parameter and an exceptional relation. It reaches every GraphProcessor
 *           position (all constant pools, Phi, non-control and control vertices,
 *           pre/post hooks) and the production ArkSteedGraphVerifier accepts it
 *           without abort and without mutating the graph, in x64 Debug and x64
 *           Release. The positive control pins the owner-corrected contract:
 *           zero-input Deopt, zero-input runtime call, four-slot one-argument Call,
 *           common stub with at least one input, one-INT32 conversion and two-INT32
 *           branch.
 * @tc.type: FUNC
 */
HWTEST_F_L0(ArkSteedGraphVerifierTest, ValidGraphPassesEveryProcessorStageWithoutMutation)
{
    Fixture fixture = BuildFixture(CorruptionKind::VALID, false);
    ASSERT_NE(fixture.graph, nullptr) << "fixture A construction failed";

    const GraphSnapshot before = SnapshotGraph(fixture.graph);
    const Inventory inventory = Summarize(before);

    std::cout << "[ArkSteedGraphVerifierTest inventory] blocks=" << inventory.blocks <<
        " blocksWithControl=" << inventory.blocksWithControl << " phis=" << inventory.phis <<
        " nonControlVertices=" << inventory.nonControlVertices <<
        " parameters=" << inventory.parameters <<
        " int32Constants=" << inventory.int32Constants << " int64Constants=" << inventory.int64Constants <<
        " float64Constants=" << inventory.float64Constants <<
        " taggedConstants=" << inventory.taggedConstants << " heapConstants=" << inventory.heapConstants <<
        " nullInputSlots=" << inventory.nullInputSlots << "\n";

    // Inventory: every processor position is populated, and every allocated input slot
    // is initialized.
    EXPECT_EQ(inventory.blocks, 7U) << "fixture A must cover the entry, two edge-split targets, the merge block "
                                       "and the exceptional structure";
    EXPECT_EQ(inventory.blocksWithControl, inventory.blocks) << "every block must carry a control vertex";
    EXPECT_EQ(inventory.phis, 2U) << "the valid graph must contain the merge Phi and the catch Phi";
    EXPECT_EQ(inventory.nonControlVertices, 5U) << "the valid graph must contain a non-control composition";
    EXPECT_EQ(inventory.parameters, 1U) << "the indexed parameter descriptor must be installed";
    EXPECT_EQ(inventory.int32Constants, 1U);
    EXPECT_EQ(inventory.int64Constants, 1U);
    EXPECT_EQ(inventory.float64Constants, 1U);
    EXPECT_EQ(inventory.taggedConstants, 2U);
    EXPECT_EQ(inventory.heapConstants, 1U);
    EXPECT_EQ(inventory.nullInputSlots, 0U) << "a valid fixture must initialize every allocated input slot";
    EXPECT_EQ(fixture.graph->NumBlocks(), 7U);
    EXPECT_EQ(fixture.graph->MaxBlockId(), 7U) << "every block id must stay below MaxBlockId";
    ASSERT_EQ(fixture.graph->GetParameterCount(), 1U);
    EXPECT_EQ(fixture.graph->GetParameter(0), static_cast<ValueVertex *>(fixture.parameter));
    EXPECT_EQ(fixture.parameter->GetFrameSlotIndex(), CALL_TARGET_FP_SLOT_INDEX);

    // Entry: no predecessor and a conditional control vertex.
    EXPECT_EQ(fixture.entry->PredecessorCount(), 0U);
    EXPECT_FALSE(fixture.entry->HasPhi());
    EXPECT_EQ(fixture.entry->GetControlVertex(), static_cast<ControlVertex *>(fixture.branch));

    // Edge splitting: both branch targets carry no Phi and each has exactly the single
    // branch predecessor.
    ASSERT_NE(fixture.ifTrue, nullptr);
    ASSERT_NE(fixture.ifFalse, nullptr);
    EXPECT_FALSE(fixture.ifTrue->HasPhi());
    EXPECT_FALSE(fixture.ifFalse->HasPhi());
    ASSERT_EQ(fixture.ifTrue->PredecessorCount(), 1U);
    EXPECT_EQ(fixture.ifTrue->GetPredecessor(0), fixture.entry);
    ASSERT_EQ(fixture.ifFalse->PredecessorCount(), 1U);
    EXPECT_EQ(fixture.ifFalse->GetPredecessor(0), fixture.entry);

    // Merge: two ordinary predecessors at the indexes the unconditional control
    // vertices store, and one Phi input per predecessor.
    ASSERT_EQ(fixture.merge->PredecessorCount(), 2U);
    EXPECT_EQ(fixture.merge->GetPredecessor(0), fixture.ifTrue);
    EXPECT_EQ(fixture.merge->GetPredecessor(1), fixture.ifFalse);
    EXPECT_EQ(fixture.trueJump->GetPredecessorId(), 0U);
    EXPECT_EQ(fixture.falseJump->GetPredecessorId(), 1U);
    ASSERT_NE(fixture.phi, nullptr);
    EXPECT_EQ(fixture.phi->GetPredecessorCount(), fixture.merge->PredecessorCount());
    EXPECT_EQ(fixture.phi->GetInputCount(), 2U);
    ASSERT_NE(fixture.phi->GetInput(0), nullptr);
    ASSERT_NE(fixture.phi->GetInput(1), nullptr);
    EXPECT_EQ(fixture.phi->GetInput(0)->GetValueRepresentation(), ValueRepresentation::TAGGED);
    EXPECT_EQ(fixture.phi->GetInput(1)->GetValueRepresentation(), ValueRepresentation::TAGGED);

    // Deopt block: one ordinary predecessor and a zero-input Deopt control vertex, which
    // is a valid control form.
    ASSERT_NE(fixture.deopt, nullptr);
    EXPECT_EQ(fixture.deopt->GetInputCount(), 0U);
    EXPECT_EQ(DeoptVertex::NUM_INPUTS, 0U);
    EXPECT_EQ(fixture.deoptBlock->GetControlVertex(), static_cast<ControlVertex *>(fixture.deopt));
    ASSERT_EQ(fixture.deoptBlock->PredecessorCount(), 1U);
    EXPECT_EQ(fixture.deoptBlock->GetPredecessor(0), fixture.merge);

    // Exceptional relation: the handler block is flagged as an exception handler, has no
    // ordinary predecessor, and its installed catch Phi has the domain arity of the one
    // registered throwable at the stored exceptional slot.
    ASSERT_TRUE(fixture.catchBlock->IsExceptionHandler());
    EXPECT_EQ(fixture.catchBlock->PredecessorCount(), 0U);
    ASSERT_EQ(fixture.catchBlock->GetPhis().size(), 1U);
    ASSERT_NE(fixture.catchPhi, nullptr);
    EXPECT_EQ(fixture.catchBlock->GetPhis()[0], fixture.catchPhi);
    EXPECT_EQ(fixture.catchPhi->GetInputCount(), 1U);
    ASSERT_NE(fixture.catchPhi->GetInput(0), nullptr);
    EXPECT_EQ(fixture.catchPhi->GetInput(0)->GetValueRepresentation(), ValueRepresentation::TAGGED);
    EXPECT_EQ(fixture.call->GetCatchBlock(), fixture.catchBlock);
    EXPECT_EQ(fixture.call->GetStoredCatchPredecessorIndex(), 0U);
    EXPECT_EQ(fixture.call->GetCatchPredecessorIndex(), 0U);
    ASSERT_EQ(fixture.handler->PredecessorCount(), 1U);
    EXPECT_EQ(fixture.handler->GetPredecessor(0), fixture.catchBlock);

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


    // Run the production verifier through the production processor.
    RunVerifier(fixture.graph);
    const GraphSnapshot after = SnapshotGraph(fixture.graph);
    EXPECT_TRUE(before == after) << "the verifier mutated the graph\nbefore:\n" <<
        Describe(before) << "\nafter:\n" <<
        Describe(after);

    // A second run on a fresh processor proves that no invocation-local state is reused
    // and that the graph stays accepted.
    RunVerifier(fixture.graph);
    const GraphSnapshot afterSecond = SnapshotGraph(fixture.graph);
    EXPECT_TRUE(before == afterSecond) << "the second verifier run changed the graph\nbefore:\n" <<
        Describe(before) << "\nafter:\n" <<
        Describe(afterSecond);
}

// The eight controlled corruption classes X1-X8 are observed in Debug only: `ASSERT`
// is a no-op under `NDEBUG`, so the detection contract exists in Debug alone and
// Release trusts internal IR. Every case declares its own fixture facts before the
// observed verifier call, so a refused construction, a foreign assertion or another
// crash cannot be reported as detection, and every case first proves that its repaired
// twin completes unchanged.
#ifndef NDEBUG

/**
 * @tc.name: ArkSteedGraphVerifierTest.DuplicateBlockIdIsDetectedInDebug
 * @tc.desc: X1: two distinct blocks of one graph reuse a single in-range id, while
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
    EXPECT_NE(repaired.entry->GetId(), repaired.handler->GetId()) <<
        "the repaired twin must give every block its own id";
    EXPECT_EQ(repaired.graph->MaxBlockId(), 7U) <<
        "the repaired twin must allocate one initial id per block through Graph::Add";

    ExpectRepairedTwinCompletesUnchanged(repaired, "X1");

    Fixture corrupted = BuildFixture(CorruptionKind::DUPLICATE_BLOCK_ID, true);
    ASSERT_NE(corrupted.graph, nullptr) << "X1: the corrupted fixture could not be built";
    ASSERT_NE(corrupted.entry, nullptr);
    ASSERT_NE(corrupted.handler, nullptr);
    ASSERT_NE(corrupted.entry, corrupted.handler) << "X1: two distinct blocks must share the id";
    ASSERT_EQ(corrupted.entry->GetId(), corrupted.handler->GetId()) <<
        "X1: exactly one id must be reused by both blocks";
    ASSERT_LT(corrupted.handler->GetId(), corrupted.graph->MaxBlockId()) <<
        "X1: the reused id must stay inside the verifier's per-block table";
    ASSERT_EQ(corrupted.graph->NumBlocks(), 7U);
    ASSERT_EQ(corrupted.graph->MaxBlockId(), 7U) <<
        "X1: every block must first take its initial valid id through Graph::Add, so the "
           "corruption changes id uniqueness only and not id allocation";

    // Detection: the death must come from the verifier run on the completed fixture.
    ASSERT_DEATH(RunVerifier(corrupted.graph), DEATH_CONDITION_DUPLICATE_BLOCK_ID);
}

/**
 * @tc.name: ArkSteedGraphVerifierTest.EmptyPhiIsDetectedInDebug
 * @tc.desc: X2: a Phi has no input at all, while a merge vertex must have at least
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
    ASSERT_EQ(corrupted.graph->NumBlocks(), 7U);
    ASSERT_EQ(corrupted.merge->PredecessorCount(), 2U) <<
        "X2: the merge block keeps its two ordinary predecessors, so the Phi alone is empty";
    ASSERT_EQ(corrupted.phi->GetInputCount(), 0U) << "X2: the corrupted Phi must have no input at all";

    // Detection: the death must come from the verifier run on the completed fixture.
    ASSERT_DEATH(RunVerifier(corrupted.graph), DEATH_CONDITION_EMPTY_PHI);
}

/**
 * @tc.name: ArkSteedGraphVerifierTest.PhiRepresentationMismatchIsDetectedInDebug
 * @tc.desc: X3: the two inputs of one Phi are INT32 and TAGGED, while all inputs of a
 *           merge vertex must share one value representation. The repaired twin with
 *           two TAGGED inputs completes unchanged; only the verifier run on the
 *           corrupted graph dies, with the representation consistency condition text
 *           (arksteed_opcode.h, PhiVertex::VerifyInputs).
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
    EXPECT_EQ(repaired.phi->GetInput(0)->GetValueRepresentation(), ValueRepresentation::TAGGED) <<
        "the repaired Phi must merge two TAGGED predecessors";
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
              corrupted.phi->GetInput(1)->GetValueRepresentation()) <<
        "X3: the merged predecessor values must disagree on the representation";

    // Detection: the death must come from the verifier run on the completed fixture.
    ASSERT_DEATH(RunVerifier(corrupted.graph), DEATH_CONDITION_PHI_REPRESENTATION);
}

/**
 * @tc.name: ArkSteedGraphVerifierTest.NullPhiInputIsDetectedInDebug
 * @tc.desc: X4: the second allocated input slot of a two-input Phi is null, while every
 *           input must be initialized. The repaired twin with two non-null TAGGED inputs
 *           completes unchanged; only the verifier run on the corrupted graph dies, with
 *           the non-null input condition text (arksteed_graph_verifier.h, ProcessVertex).
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
 * @tc.desc: X5: a CallVertex has three input slots for an actual argument count of one,
 *           while the layout is FIRST_ARG_INDEX + actualArgc with target, new.target and
 *           this as the three leading slots (FIRST_ARG_INDEX = 3). The repaired twin
 *           with four slots completes unchanged; only the verifier run on the corrupted
 *           graph dies, with the call-layout condition text (arksteed_opcode.h,
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
    EXPECT_EQ(repaired.call->GetInputCount(), CallVertex::FIRST_ARG_INDEX + repaired.call->GetActualArgc()) <<
        "the repaired call must fill the target, new.target, this and argument slots";

    ExpectRepairedTwinCompletesUnchanged(repaired, "X5");

    Fixture corrupted = BuildFixture(CorruptionKind::CALL_LAYOUT_MISMATCH, true);
    ASSERT_NE(corrupted.graph, nullptr) << "X5: the corrupted fixture could not be built";
    ASSERT_NE(corrupted.call, nullptr);
    ASSERT_EQ(corrupted.call->GetActualArgc(), 1U) << "X5: the violation is the slot count, not the argument count";
    ASSERT_EQ(corrupted.call->GetInputCount(), 3U);
    ASSERT_NE(corrupted.call->GetInputCount(), CallVertex::FIRST_ARG_INDEX + corrupted.call->GetActualArgc()) <<
        "X5: the call is one slot short of its documented layout";

    // Detection: the death must come from the verifier run on the completed fixture.
    ASSERT_DEATH(RunVerifier(corrupted.graph), DEATH_CONDITION_CALL_LAYOUT);
}

/**
 * @tc.name: ArkSteedGraphVerifierTest.ConversionRepresentationMismatchIsDetectedInDebug
 * @tc.desc: X6: the selected fixed-input non-control conversion (U32ToTaggedNumberVertex)
 *           takes a TAGGED input, while its declared input representation is INT32. The
 *           repaired twin with one INT32 input completes unchanged; only the verifier run
 *           on the corrupted graph dies, with the declared input representation condition
 *           text (arksteed_graph_verifier.h, ProcessVertex, for the same declared
 *           INPUT_TYPES contract arksteed_opcode.h asserts).
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
    EXPECT_EQ(repaired.u32ToTagged->GetInput(0)->GetValueRepresentation(), ValueRepresentation::INT32) <<
        "the repaired conversion must provide the INT32 input it declares";

    ExpectRepairedTwinCompletesUnchanged(repaired, "X6");

    Fixture corrupted = BuildFixture(CorruptionKind::CONVERSION_REPRESENTATION_MISMATCH, true);
    ASSERT_NE(corrupted.graph, nullptr) << "X6: the corrupted fixture could not be built";
    ASSERT_NE(corrupted.u32ToTagged, nullptr);
    ASSERT_EQ(U32ToTaggedNumberVertex::INPUT_TYPES[0], ValueRepresentation::INT32) <<
        "the selected conversion declares one INT32 input";
    ASSERT_EQ(corrupted.u32ToTagged->GetInputCount(), 1U) << "X6: the violation is the representation, not the arity";
    ASSERT_NE(corrupted.u32ToTagged->GetInput(0), nullptr);
    ASSERT_EQ(corrupted.u32ToTagged->GetInput(0)->GetValueRepresentation(), ValueRepresentation::TAGGED);
    ASSERT_NE(corrupted.u32ToTagged->GetInput(0)->GetValueRepresentation(), U32ToTaggedNumberVertex::INPUT_TYPES[0]) <<
        "X6: the supplied value must contradict the declared input representation";

    // Detection: the death must come from the verifier run on the completed fixture.
    ASSERT_DEATH(RunVerifier(corrupted.graph), DEATH_CONDITION_FIXED_INPUT_REPRESENTATION);
}

/**
 * @tc.name: ArkSteedGraphVerifierTest.EmptyCommonStubCallIsDetectedInDebug
 * @tc.desc: X7: a common-stub call has no input at all, while the implemented minimum of
 *           this vertex is one input (the glue). The repaired twin with one input
 *           completes unchanged; only the verifier run on the corrupted graph dies, with
 *           the minimum condition text (arksteed_opcode.h,
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
    ASSERT_LT(corrupted.commonStub->GetInputCount(), COMMON_STUB_MINIMUM_INPUTS) <<
        "X7: the corrupted call must stay below the implemented minimum";

    // Detection: the death must come from the verifier run on the completed fixture.
    ASSERT_DEATH(RunVerifier(corrupted.graph), DEATH_CONDITION_COMMON_STUB_MINIMUM);
}

/**
 * @tc.name: ArkSteedGraphVerifierTest.BranchRepresentationMismatchIsDetectedInDebug
 * @tc.desc: X8: the selected fixed-input control branch (BranchIfInt32CompareVertex) takes
 *           two TAGGED inputs, while its declared input representations are INT32. The
 *           repaired twin with two INT32 inputs completes unchanged; only the verifier run
 *           on the corrupted graph dies, with the declared input representation condition
 *           text (arksteed_graph_verifier.h, ProcessVertex, for the same declared
 *           INPUT_TYPES contract arksteed_opcode.h asserts).
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
    EXPECT_EQ(repaired.branch->GetInput(0)->GetValueRepresentation(), ValueRepresentation::INT32) <<
        "the repaired branch must compare two INT32 values";
    EXPECT_EQ(repaired.branch->GetInput(1)->GetValueRepresentation(), ValueRepresentation::INT32);

    ExpectRepairedTwinCompletesUnchanged(repaired, "X8");

    Fixture corrupted = BuildFixture(CorruptionKind::BRANCH_REPRESENTATION_MISMATCH, true);
    ASSERT_NE(corrupted.graph, nullptr) << "X8: the corrupted fixture could not be built";
    ASSERT_NE(corrupted.branch, nullptr);
    ASSERT_EQ(BranchIfInt32CompareVertex::INPUT_TYPES[0], ValueRepresentation::INT32);
    ASSERT_EQ(BranchIfInt32CompareVertex::INPUT_TYPES[1], ValueRepresentation::INT32) <<
        "the selected branch declares two INT32 inputs";
    ASSERT_EQ(corrupted.branch->GetInputCount(), 2U) << "X8: the violation is the representation, not the arity";
    ASSERT_NE(corrupted.branch->GetInput(0), nullptr);
    ASSERT_NE(corrupted.branch->GetInput(1), nullptr);
    ASSERT_EQ(corrupted.branch->GetInput(0)->GetValueRepresentation(), ValueRepresentation::TAGGED);
    ASSERT_EQ(corrupted.branch->GetInput(1)->GetValueRepresentation(), ValueRepresentation::TAGGED);
    ASSERT_NE(corrupted.branch->GetInput(0)->GetValueRepresentation(),
              BranchIfInt32CompareVertex::INPUT_TYPES[0]) <<
        "X8: the supplied value must contradict the declared input representation";

    // Detection: the death must come from the verifier run on the completed fixture.
    ASSERT_DEATH(RunVerifier(corrupted.graph), DEATH_CONDITION_FIXED_INPUT_REPRESENTATION);
}

#endif  // !NDEBUG


}  // namespace panda::ecmascript::arksteed
