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

#include "ecmascript/arksteed/arksteed_graph_verifier.h"

#ifndef NDEBUG
#include "ecmascript/arksteed/arksteed_register_merge_state.h"
#include "ecmascript/arksteed/arksteed_vertex.h"
#include "ecmascript/frames.h"
#include "ecmascript/js_arguments.h"
#include "ecmascript/js_array.h"
#include "ecmascript/mem/chunk_containers.h"
#include "ecmascript/mem/mem.h"

namespace panda::ecmascript::arksteed {

// Debug-only helpers of the shared normal integrity contract of
// `ArkSteedGraphVerifier`. Everything in this namespace is compiled out under
// `NDEBUG`: Release builds trust internal IR, so an explicit verifier call is a
// no-op and keeps no check loop, contract-hook evaluation, index construction or
// failure reporting.
namespace verifier_details {

// Compile-time detection of the per-opcode input metadata, mirroring the
// detection the concrete contracts of `arksteed_opcode.h` use.
template <typename T, typename = void>
struct HasNumInputs : std::false_type {};

template <typename T>
struct HasNumInputs<T, decltype(void(T::NUM_INPUTS))> : std::true_type {};

// The existing concrete-dispatch inventory. An opcode outside `ALL_VERTEX_LIST`
// has no concrete vertex type, so no category, layout or option question can be
// answered for it.
bool IsKnownConcreteOpcode(VertexOpcode opcode)
{
    // VertexOpcode is the contiguous enum generated from ALL_VERTEX_LIST.
    return opcode >= FIRST_OPCODE && opcode <= LAST_OPCODE;
}

// A direct input slot must hold an allocated object that is a real concrete
// value: a non-value or unknown object has no value interface, so treating such
// a slot as a dependency would be an unsafe typed access.
bool IsConcreteValueVertex(const ValueVertex *input)
{
    if (input == nullptr) {
        return false;
    }
    const VertexOpcode opcode = input->GetOpcode();
    return opcode >= FIRST_OPCODE && opcode <= LAST_OPCODE && IsKnownConcreteOpcode(opcode) &&
           IsValueVertex(opcode);
}

// Frame-translation contract of one deopt frame value: the recorded kind must be
// a real `DeoptTranslationKind` and must agree with the machine representation
// of the referenced value (`GraphBuilder::GetDeoptValueKind` derives the kind
// from the representation, so no production frame carries a mismatching pair).
bool FrameKindMatchesRepresentation(DeoptTranslationKind kind, ValueRepresentation representation)
{
    switch (kind) {
        case DeoptTranslationKind::TAGGED:
            return representation == ValueRepresentation::TAGGED;
        case DeoptTranslationKind::INT32_TO_TAGGED:
        case DeoptTranslationKind::RAW_INT32:
            return representation == ValueRepresentation::INT32;
        case DeoptTranslationKind::FLOAT64_TO_TAGGED_DOUBLE:
            return representation == ValueRepresentation::FLOAT64 ||
                   representation == ValueRepresentation::HOLEY_FLOAT64;
        default:
            return false;
    }
}

// The one production frame pairing whose recorded kind does not follow from the
// value's static `VALUE_TYPE`: the eager frame records the raw actual-argument
// count frame-slot descriptor (`GraphBuilder::InitializeStartBlock`,
// `ACTUAL_ARGC_FP_SLOT_INDEX`) with the hard-coded
// `DeoptTranslationKind::INT32_TO_TAGGED`, because the physical slot holds a raw
// int32 while every `InitialValueVertex` is typed `TAGGED`
// (`arksteed_deopt_helper.cpp:ValidateTranslationInput` accepts exactly this
// kind). The exception is bound to the descriptor identity, not to the opcode:
// every other `InitialValueVertex` of a frame - the call target, the new target,
// `this`, `arguments`, the parameters and the lexical environment - carries a
// real `JSTaggedValue` in a tagged slot, so accepting this kind for it would
// make the deopt trampoline reconstruct an integer where a tagged reference must
// be restored.
bool IsProductionFramePairing(const ValueVertex *value, DeoptTranslationKind kind)
{
    if (kind != DeoptTranslationKind::INT32_TO_TAGGED) {
        return false;
    }
    const InitialValueVertex *descriptor = value == nullptr ? nullptr : value->TryCast<InitialValueVertex>();
    return descriptor != nullptr && descriptor->GetFrameSlotIndex() == ACTUAL_ARGC_FP_SLOT_INDEX;
}

bool IsFrameValueCompatible(const ValueVertex *value, DeoptTranslationKind kind)
{
    return IsConcreteValueVertex(value) &&
           (FrameKindMatchesRepresentation(kind, value->GetValueRepresentation()) ||
            IsProductionFramePairing(value, kind));
}

// Graph-local domains established before any reference is followed.
using BlockSet = ChunkUnorderedSet<const BB *>;
using MemberSet = ChunkUnorderedSet<const Vertex *>;

// The retained-registry pointer domain of one graph: every indexed parameter
// descriptor and every binding of the five constant pools. Both are graph-owned
// registries that core retains unchanged and never prunes, so this is exactly the
// object set a reference may resolve to in addition to the executable members.
// Membership is a plain pointer fact: aliases and overlaps stay valid, because
// several keys may be bound to one object (`GetOrAddNewConstantVertex`) and a
// retained object may also be installed as an executable member.
using RegistrySet = ChunkUnorderedSet<const Vertex *>;

// A reference resolves when it names a graph-owned object: an executable member
// of some block or an object retained by a graph registry. Core never references
// a value that no installed list and no registry retains, and a throwable's
// incoming value is such a reference too, so a dangling or foreign producer is
// corruption rather than an omitted dependency. `nullptr` is never a resolvable
// reference; the category check names a null slot separately.
bool ResolvesToRetainedObject(const MemberSet &members, const RegistrySet &registry, const ValueVertex *value)
{
    return value != nullptr &&
           (members.find(value) != members.end() || registry.find(value) != registry.end());
}

// The exceptional-input slots of one catch block: the count of the throwable
// vertices that registered it (`GraphBuilder::CatchBlockInputData::
// AddCatchPredecessor`) and the set of the slot indexes they occupy. The index
// is NOT an index into `BB::GetPredecessors()`: a catch block is created with
// its catch Phi inputs before any ordinary CFG predecessor is added, so a
// production catch block can have zero ordinary predecessors and a valid index
// of zero.
struct CatchSlotFacts {
    explicit CatchSlotFacts(Chunk *chunk) : indexes(chunk) {}

    uint32_t count {0};
    ChunkSet<uint32_t> indexes;
};

using CatchSlotMap = ChunkUnorderedMap<const BB *, CatchSlotFacts *>;

// Executable membership, category and base ownership. Every installed entry is a
// non-null, unique executable member of exactly one list, carries the category
// its list requires, is owned by the block that lists it (`PhiVertex::GetOwner`
// is a virtual-register owner, so the base `Vertex::GetOwner` is read), and has
// a known concrete opcode. Only the block that lists a Phi owns it.
bool BlockExecutableStructureHolds(BB *block, MemberSet &members)
{
    for (PhiVertex *phi : block->GetPhis()) {
        if (phi == nullptr || phi->GetOpcode() != VertexOpcode::Phi ||
            static_cast<const Vertex *>(phi)->GetOwner() != block ||
            !IsKnownConcreteOpcode(phi->GetOpcode()) ||
            !members.insert(phi).second) {
            return false;
        }
    }
    for (NonControlVertex *vertex : block->GetVertices()) {
        if (vertex == nullptr || vertex->GetOpcode() == VertexOpcode::Phi ||
            !IsNonControlVertex(vertex->GetOpcode()) || !IsKnownConcreteOpcode(vertex->GetOpcode()) ||
            vertex->GetOwner() != block || !members.insert(vertex).second) {
            return false;
        }
    }
    ControlVertex *control = block->GetControlVertex();
    if (control == nullptr || !IsControlVertex(control->GetOpcode()) ||
        !IsKnownConcreteOpcode(control->GetOpcode()) || control->GetOwner() != block ||
        !members.insert(control).second) {
        return false;
    }
    return true;
}

bool ExecutableStructureHolds(Graph *graph, const BlockSet &blocks, MemberSet &members)
{
    for (BB *block : *graph) {
        if (block == nullptr || blocks.find(block) == blocks.end() ||
            !BlockExecutableStructureHolds(block, members)) {
            return false;
        }
    }
    return true;
}

// Each constant-pool binding is a non-null vertex of the pool's own constant
// category, and a heap-constant binding answers its own key. Several keys may
// alias one object (`GetOrAddNewConstantVertex`) and a pooled object may also be
// an executable member, so no binding is rejected merely because its pointer was
// already seen.
bool ConstantPoolBindingsAreValid(Graph *graph)
{
    for (const auto &entry : graph->GetInt32Constants()) {
        if (entry.second == nullptr || entry.second->GetOpcode() != VertexOpcode::Int32Constant) {
            return false;
        }
    }
    for (const auto &entry : graph->GetInt64Constants()) {
        if (entry.second == nullptr || entry.second->GetOpcode() != VertexOpcode::Int64Constant) {
            return false;
        }
    }
    for (const auto &entry : graph->GetFloat64Constants()) {
        if (entry.second == nullptr || entry.second->GetOpcode() != VertexOpcode::Float64Constant) {
            return false;
        }
    }
    for (const auto &entry : graph->GetTaggedConstants()) {
        if (entry.second == nullptr || entry.second->GetOpcode() != VertexOpcode::TaggedConstant) {
            return false;
        }
    }
    for (const auto &entry : graph->GetHeapConstants()) {
        if (entry.second == nullptr || entry.second->GetOpcode() != VertexOpcode::HeapConstant ||
            entry.second->GetHandleIndex() != entry.first) {
            return false;
        }
    }
    return true;
}

// Records the retained-registry pointer domain before any reference is followed.
// A null pool entry is not a domain object: `ConstantPoolBindingsAreValid` names
// it first, and a reference to it is a null slot, not a registry binding.
void CollectRetainedRegistryDomain(Graph *graph, RegistrySet &registry)
{
    auto add = [&registry](const ValueVertex *value) {
        if (value != nullptr) {
            registry.insert(value);
        }
    };
    for (size_t index = 0, count = graph->GetParameterCount(); index < count; ++index) {
        add(graph->GetParameter(static_cast<uint32_t>(index)));
    }
    for (const auto &entry : graph->GetInt32Constants()) {
        add(entry.second);
    }
    for (const auto &entry : graph->GetInt64Constants()) {
        add(entry.second);
    }
    for (const auto &entry : graph->GetFloat64Constants()) {
        add(entry.second);
    }
    for (const auto &entry : graph->GetTaggedConstants()) {
        add(entry.second);
    }
    for (const auto &entry : graph->GetHeapConstants()) {
        add(entry.second);
    }
}

// Every indexed parameter is a non-null `InitialValue` descriptor installed in
// the executable structure of some block (`GraphBuilder::InitializeStartBlock`
// adds each descriptor to the entry block). Parameter/executable overlap and
// repeated parameter bindings stay valid.
bool ParametersAreInstalled(Graph *graph, const MemberSet &members)
{
    for (size_t index = 0, count = graph->GetParameterCount(); index < count; ++index) {
        const ValueVertex *parameter = graph->GetParameter(static_cast<uint32_t>(index));
        if (parameter == nullptr || parameter->GetOpcode() != VertexOpcode::InitialValue ||
            members.find(parameter) == members.end()) {
            return false;
        }
    }
    return true;
}

// Every structural block reference is a block of this graph before any list of
// the referenced block is followed: ordinary predecessors, control successors,
// an unconditional target and a throwable's catch target. A `JumpLoop` back edge
// additionally targets a block the builder flagged as its loop header. Core
// changes no CFG fact, so a foreign or disconnected reference is corruption, not
// something to repair. Exceptional slots are never ordinary predecessors: a
// catch block needs no ordinary predecessor, and its catch Phi arity is the
// exceptional-input domain.
// Check a throwable's target before following the target's installed Phi list.
bool CatchTargetIsGraphLocal(const Vertex *vertex, const BlockSet &blocks)
{
    const ThrowableMixin *mixin = ThrowableMixinOf(vertex);
    if (mixin == nullptr || !mixin->HasCatchBlock()) {
        return true;
    }
    const BB *catchBlock = mixin->GetCatchBlock();
    return catchBlock != nullptr && blocks.find(catchBlock) != blocks.end();
}

bool SlotReferencesAreGraphLocal(Graph *graph, const BlockSet &blocks)
{
    for (BB *block : *graph) {
        if (block == nullptr || block->GetControlVertex() == nullptr) {
            return false;
        }
        for (BB *predecessor : block->GetPredecessors()) {
            if (predecessor == nullptr || blocks.find(predecessor) == blocks.end()) {
                return false;
            }
        }
        bool successorsValid = true;
        // `BB::ForEachSuccessor` reports exactly the ordinary control successors
        // (never an exception predecessor) once the control vertex exists.
        block->ForEachSuccessor([&blocks, &successorsValid](BB *successor) {
            if (successor == nullptr || blocks.find(successor) == blocks.end()) {
                successorsValid = false;
            }
        });
        if (!successorsValid) {
            return false;
        }
        if (JumpLoopVertex *jumpLoop = block->GetControlVertex()->TryCast<JumpLoopVertex>();
            jumpLoop != nullptr) {
            BB *target = jumpLoop->Target();
            if (target == nullptr || blocks.find(target) == blocks.end() || !target->IsLoopHeader()) {
                return false;
            }
        }
        for (NonControlVertex *vertex : block->GetVertices()) {
            if (vertex != nullptr && !CatchTargetIsGraphLocal(vertex, blocks)) {
                return false;
            }
        }
        if (!CatchTargetIsGraphLocal(block->GetControlVertex(), blocks)) {
            return false;
        }
    }
    return true;
}

// Exact control-edge/predecessor multiplicity. The builder appends one
// predecessor entry per control edge (`FinishBlockWithJump`,
// `FinishBlockWithJumpLoop`, and once per target in `FinishBlockWithBranch`), so
// a branch whose two targets are one block legitimately lists that predecessor
// twice. Membership alone would accept a phantom edge: a predecessor listed
// twice while its source targets the block once describes an edge that does not
// exist, and downstream Phi indexing would then read a slot no control flow
// reaches. An exception predecessor is not a control edge and is never part of
// this relation.
using PredecessorCounts = ChunkUnorderedMap<const BB *, uint32_t>;
using ControlEdges = ChunkUnorderedMap<const BB *, PredecessorCounts *>;

bool CollectControlEdges(Graph *graph, const BlockSet &blocks, ControlEdges &controlEdges)
{
    Chunk *chunk = graph->GetChunk();
    for (BB *block : *graph) {
        if (block == nullptr || block->GetControlVertex() == nullptr) {
            return false;
        }
        bool successorsValid = true;
        block->ForEachSuccessor([block, chunk, &blocks, &controlEdges, &successorsValid](BB *successor) {
            if (successor == nullptr || blocks.find(successor) == blocks.end()) {
                successorsValid = false;
                return;
            }
            auto it = controlEdges.find(successor);
            PredecessorCounts *counts = nullptr;
            if (it == controlEdges.end()) {
                counts = chunk->New<PredecessorCounts>(chunk);
                controlEdges.emplace(successor, counts);
            } else {
                counts = it->second;
            }
            (*counts)[block] += 1;
        });
        if (!successorsValid) {
            return false;
        }
    }
    return true;
}

bool ControlEdgeMultiplicityMatchesPredecessors(Graph *graph, const BlockSet &blocks)
{
    Chunk *chunk = graph->GetChunk();
    ControlEdges controlEdges(chunk);
    if (!CollectControlEdges(graph, blocks, controlEdges)) {
        return false;
    }
    for (BB *block : *graph) {
        PredecessorCounts predecessorCounts(chunk);
        for (BB *predecessor : block->GetPredecessors()) {
            if (predecessor == nullptr || blocks.find(predecessor) == blocks.end()) {
                return false;
            }
            predecessorCounts[predecessor] += 1;
        }
        auto it = controlEdges.find(block);
        if (it == controlEdges.end()) {
            if (!predecessorCounts.empty()) {
                return false;
            }
            continue;
        }
        const PredecessorCounts &expected = *it->second;
        if (predecessorCounts.size() != expected.size()) {
            return false;
        }
        for (const auto &entry : expected) {
            auto found = predecessorCounts.find(entry.first);
            if (found == predecessorCounts.end() || found->second != entry.second) {
                return false;
            }
        }
    }
    return true;
}


// An unconditional control vertex (`Jump`, `JumpLoop`) stores the index of its
// source block in the target's ordered predecessor list: the builder sets it
// from `target->PredecessorCount()` immediately before
// `target->AddPredecessor(owner)`. `LivenessProcessor::MarkInputUses` and
// `ArkSteedRegisterAllocator::ProcessUnconditionalControl` index the target's Phi
// inputs and input locations with exactly that id, so an id outside the list, or
// one that addresses a different predecessor, is integrity corruption rather
// than a value to repair.
bool UnconditionalPredecessorIdsAddressTheirSource(Graph *graph, const BlockSet &blocks)
{
    for (BB *block : *graph) {
        if (block == nullptr || block->GetControlVertex() == nullptr) {
            return false;
        }
        UnconditionalControlVertex *unconditional =
            block->GetControlVertex()->TryCast<UnconditionalControlVertex>();
        if (unconditional == nullptr) {
            continue;
        }
        BB *target = unconditional->Target();
        if (target == nullptr || blocks.find(target) == blocks.end()) {
            return false;
        }
        const uint32_t predecessorId = unconditional->GetPredecessorId();
        if (predecessorId >= target->PredecessorCount() ||
            target->GetPredecessor(predecessorId) != block) {
            return false;
        }
    }
    return true;
}

// Edge splitting: a conditional-branch target cannot carry Phi vertices
// (`LivenessProcessor` class comment;
// `ArkSteedRegisterAllocator::InitializeConditionalBranchTarget` asserts
// `!target->HasPhi()`). Only an unconditional target may hold a merge Phi.
bool BranchTargetsCarryNoPhi(Graph *graph, const BlockSet &blocks)
{
    for (BB *block : *graph) {
        if (block == nullptr || block->GetControlVertex() == nullptr) {
            return false;
        }
        BranchControlVertex *branch = block->GetControlVertex()->TryCast<BranchControlVertex>();
        if (branch == nullptr) {
            continue;
        }
        BB *ifTrue = branch->IfTrue();
        BB *ifFalse = branch->IfFalse();
        if (ifTrue == nullptr || ifFalse == nullptr || blocks.find(ifTrue) == blocks.end() ||
            blocks.find(ifFalse) == blocks.end() || ifTrue->HasPhi() || ifFalse->HasPhi()) {
            return false;
        }
    }
    return true;
}

// Production loop convention (`LivenessProcessor`): a block flagged as a loop
// header is pushed in block order, exactly one `JumpLoop` back edge pops it, and
// the back edge must target the header currently at the top of the stack; the
// stack is empty after the last block. The register allocator relies on this
// nesting to extend loop-external lifetimes and to spill/reload at the header.
// Core never rebuilds the loop form.
bool LoopHeaderStackHolds(Graph *graph, const BlockSet &blocks)
{
    ChunkVector<BB *> pendingLoopHeaders(graph->GetChunk());
    for (BB *block : *graph) {
        if (block == nullptr || block->GetControlVertex() == nullptr) {
            return false;
        }
        if (block->IsLoopHeader()) {
            pendingLoopHeaders.push_back(block);
        }
        if (JumpLoopVertex *jumpLoop = block->GetControlVertex()->TryCast<JumpLoopVertex>();
            jumpLoop != nullptr) {
            BB *target = jumpLoop->Target();
            if (target == nullptr || blocks.find(target) == blocks.end() || pendingLoopHeaders.empty() ||
                pendingLoopHeaders.back() != target) {
                return false;
            }
            pendingLoopHeaders.pop_back();
        }
    }
    return pendingLoopHeaders.empty();
}

// Ordinary predecessor/Phi relation. An exception handler merges exceptional
// inputs, so the exceptional-input domain below owns its Phi arity instead. Every
// other block gives each of its ordinary predecessors exactly one Phi input slot
// and keeps no input that no predecessor position can address, because
// downstream regalloc indexes a merge Phi with the predecessor position:
// `GraphBuilder::MergeFrameState` builds a merge Phi with exactly
// `actualNumPreds` inputs, `InitFrameStateForLoopHeader` builds a loop-header Phi
// with one slot per entry/back-edge predecessor, and
// `TurnOptimisticLoopIntoPeeledIteration` reduces a peeled header Phi together
// with its structural back edge.
bool OrdinaryPhiArityMatchesPredecessors(Graph *graph)
{
    for (BB *block : *graph) {
        if (block == nullptr || block->IsExceptionHandler()) {
            continue;
        }
        for (PhiVertex *phi : block->GetPhis()) {
            if (phi == nullptr || phi->GetInputCount() != block->PredecessorCount()) {
                return false;
            }
        }
    }
    return true;
}

// The exceptional-input structure of every catch block. The domain of a catch
// block is the count of its registered throwable vertices, which is also the
// arity `GraphBuilder::InitFrameStateForCatchBlockHeader` gives every catch Phi
// of that block. The union of the flagged exception-handler blocks and the
// registered catch targets is checked, so a flagged handler whose slots no
// throwable registered is not skipped: an absent slot-map entry is a zero
// throwing-site domain and then requires every installed catch Phi to have zero
// inputs. No two throwables may occupy one slot (checked while the slots are
// collected), every throwable slot lies inside that domain and inside the arity
// of every installed catch Phi, and every installed catch Phi keeps exactly the
// domain arity. The domain is a graph property, not a property of a removal
// selection: registered throwables are calls or throws and stay retained.
bool CatchPhiAritiesMatch(Graph *graph, const CatchSlotMap &slots)
{
    for (BB *block : *graph) {
        if (block == nullptr || block->GetControlVertex() == nullptr) {
            return false;
        }
        auto it = slots.find(block);
        if (it == slots.end() && !block->IsExceptionHandler()) {
            continue;
        }
        const uint32_t domain = it == slots.end() ? 0 : it->second->count;
        for (PhiVertex *phi : block->GetPhis()) {
            if (phi == nullptr || phi->GetInputCount() != domain) {
                return false;
            }
        }
    }
    return true;
}

bool ThrowableSlotIsInDomain(const Vertex *vertex, const CatchSlotMap &slots)
{
    if (vertex == nullptr) {
        return false;
    }
    const ThrowableMixin *mixin = ThrowableMixinOf(vertex);
    if (mixin == nullptr || !mixin->HasCatchBlock()) {
        return true;
    }
    const BB *catchBlock = mixin->GetCatchBlock();
    auto it = slots.find(catchBlock);
    if (it == slots.end()) {
        return false;
    }
    const uint32_t index = mixin->GetStoredCatchPredecessorIndex();
    if (index == static_cast<uint32_t>(-1) || index >= it->second->count) {
        return false;
    }
    for (PhiVertex *phi : catchBlock->GetPhis()) {
        if (phi == nullptr || index >= phi->GetInputCount()) {
            return false;
        }
    }
    return true;
}

bool ExceptionalStructureHolds(Graph *graph, const BlockSet &blocks, const CatchSlotMap &slots)
{
    for (const auto &entry : slots) {
        if (entry.first == nullptr || entry.second == nullptr ||
            blocks.find(entry.first) == blocks.end()) {
            return false;
        }
    }
    if (!CatchPhiAritiesMatch(graph, slots)) {
        return false;
    }
    for (BB *block : *graph) {
        if (block == nullptr || block->GetControlVertex() == nullptr) {
            return false;
        }
        for (NonControlVertex *vertex : block->GetVertices()) {
            if (!ThrowableSlotIsInDomain(vertex, slots)) {
                return false;
            }
        }
        if (!ThrowableSlotIsInDomain(block->GetControlVertex(), slots)) {
            return false;
        }
    }
    return true;
}


}  // namespace verifier_details


// Fresh per invocation; no graph field or transient bit is used as scratch.
struct ArkSteedGraphVerifier::Scratch {
    explicit Scratch(Chunk *chunk)
        : seen(chunk), blocks(chunk), members(chunk), registry(chunk), catchSlots(chunk), allocationBlocks(chunk)
    {}

    ChunkVector<uint8_t> seen;
    verifier_details::BlockSet blocks;
    verifier_details::MemberSet members;
    verifier_details::RegistrySet registry;
    verifier_details::CatchSlotMap catchSlots;
    // Each allocation block is cleared when its merge Phi consumes it.
    ChunkMap<ValueVertex *, BB *> allocationBlocks;
    ValueVertex *pendingAllocation {nullptr};
};

void ArkSteedGraphVerifier::PreProcessGraph(Graph *graph)
{
#ifndef NDEBUG
    ASSERT(graph != nullptr);
    // Block facts first: the id table and the block domain are established
    // before any indexed access or reference traversal.
    scratch_ = chunk_->New<Scratch>(chunk_);
    scratch_->seen.clear();
    scratch_->seen.resize(graph->MaxBlockId());
    // Register allocation creates temporary values and moves that do not obey
    // the finalized pre-allocation ownership/registry contract. Preserve the
    // base post-RA input and allocation-sequence checks without applying that
    // earlier phase's structural or empty-metadata requirements.
    if (afterRegalloc_) {
        return;
    }
    EstablishBlockDomain(graph);
    for (BB *block : *graph) {
        for (PhiVertex *phi : block->GetPhis()) {
            RegisterThrowable(phi);
        }
        for (NonControlVertex *vertex : block->GetVertices()) {
            RegisterThrowable(vertex);
        }
        RegisterThrowable(block->GetControlVertex());
    }
    // Membership, category and ownership of the executable structure, plus
    // the executable membership set the parameter check shares with this
    // invocation. They are established before the typed dispatch below reads
    // any vertex, so an entry whose object has no concrete opcode or the
    // wrong list category is a named violation of this contract instead of
    // an unreachable dispatch case.
    ASSERT(verifier_details::ExecutableStructureHolds(graph, scratch_->blocks, scratch_->members));
    // Registry bindings and indexed parameters: every binding is a non-null
    // vertex of its pool's own constant category and every parameter is an
    // installed `InitialValue` descriptor.
    ASSERT(verifier_details::ConstantPoolBindingsAreValid(graph));
    ASSERT(verifier_details::ParametersAreInstalled(graph, scratch_->members));
    // The retained-registry pointer domain the reference assertions below read:
    // the indexed parameter bindings and every constant-pool binding, which stay
    // valid alongside the executable membership established above.
    verifier_details::CollectRetainedRegistryDomain(graph, scratch_->registry);
#else
    static_cast<void>(graph);
#endif
}

void ArkSteedGraphVerifier::PostProcessGraph(Graph *graph)
{
#ifndef NDEBUG
    for (const auto &allocation : scratch_->allocationBlocks) {
        ASSERT(allocation.second == nullptr && "Allocation requires a merge and initializer");
    }
    if (afterRegalloc_) {
        return;
    }
    // Structural references, the CFG, the loop convention and the
    // exceptional-input domain of the finalized pre-allocation graph. The
    // membership, registry and parameter facts were established by
    // `PreProcessGraph`.
    ASSERT(verifier_details::SlotReferencesAreGraphLocal(graph, scratch_->blocks));
    ASSERT(verifier_details::ControlEdgeMultiplicityMatchesPredecessors(graph, scratch_->blocks));
    ASSERT(verifier_details::UnconditionalPredecessorIdsAddressTheirSource(graph, scratch_->blocks));
    ASSERT(verifier_details::BranchTargetsCarryNoPhi(graph, scratch_->blocks));
    ASSERT(verifier_details::LoopHeaderStackHolds(graph, scratch_->blocks));
    ASSERT(verifier_details::OrdinaryPhiArityMatchesPredecessors(graph));
    ASSERT(verifier_details::ExceptionalStructureHolds(graph, scratch_->blocks, scratch_->catchSlots));
#else
    static_cast<void>(graph);
#endif
}

void ArkSteedGraphVerifier::PostProcessBlock([[maybe_unused]] BB *block)
{
#ifndef NDEBUG
    if (scratch_->pendingAllocation != nullptr && IsUninitializedAllocation(scratch_->pendingAllocation)) {
        // Allocation blocks hand their raw storage to the merge block.
        ASSERT(block->GetControlVertex() != nullptr &&
               "Allocation block must end with a jump to the merge block");
        scratch_->pendingAllocation = nullptr;
        return;
    }
    ASSERT(scratch_->pendingAllocation == nullptr && "Incomplete initialization interval");
#endif
}

void ArkSteedGraphVerifier::PreProcessBlock([[maybe_unused]] BB *block)
{
#ifndef NDEBUG
    ASSERT(block != nullptr);
    ASSERT(block->GetId() < scratch_->seen.size());
    // Block ids are unique and were established inside `MaxBlockId` before
    // this indexed access.
    ASSERT(scratch_->seen[block->GetId()] == 0);
    scratch_->seen[block->GetId()] = 1;
#endif
}

template <typename VertexT>
void ArkSteedGraphVerifier::ProcessVertex(VertexT* vertex, const ArkSteedState& state)
{
#ifndef NDEBUG
    VerifyDirectInputs(vertex);
    VerifyInputRepresentations(vertex);
    vertex->VerifyInputs();
    VerifyFrameReferences(vertex);
    VerifyCatchReferences(vertex);
    ASSERT(afterRegalloc_ || vertex->GetRegallocInfo() == nullptr);
    if constexpr (std::is_same_v<VertexT, PhiVertex>) {
        TryRegisterAllocationPhi(vertex, state);
    }
    VerifyInitializationInterval(vertex, state);
#else
    static_cast<void>(vertex);
    static_cast<void>(state);
#endif
}

bool ArkSteedGraphVerifier::IsAllocationPhi(const ValueVertex* value)
{
    if (auto* phi = value->TryCast<PhiVertex>()) {
        for (uint32_t i = 0; i < phi->GetInputCount(); ++i) {
            auto* input = phi->GetInput(i);
            if (input != nullptr && IsUninitializedAllocation(input)) {
                return true;
            }
        }
    }
    return false;
}

template<typename VertexT>
constexpr bool ArkSteedGraphVerifier::IsInitializer()
{
    return std::is_same_v<VertexT, InitializeEmptyObjectVertex> ||
           std::is_same_v<VertexT, InitializeObjectWithBufferVertex> ||
           std::is_same_v<VertexT, InitializeEmptyArrayVertex> ||
           std::is_same_v<VertexT, InitializeArrayWithBufferVertex> ||
           std::is_same_v<VertexT, InitializeUnmappedArgumentsElementsVertex> ||
           std::is_same_v<VertexT, InitializeUnmappedArgumentsVertex>;
}

void ArkSteedGraphVerifier::VerifyStaticSize(uint64_t size, uint32_t minimumSize)
{
    ASSERT(size >= minimumSize);
    ASSERT(size <= MAX_REGULAR_HEAP_OBJECT_SIZE);
    ASSERT(size % JSTaggedValue::TaggedTypeSize() == 0);
}

bool ArkSteedGraphVerifier::IsInt64Constant(ValueVertex* value, int64_t expected)
{
    auto* constant = value->TryCast<Int64ConstantVertex>();
    return constant != nullptr && constant->GetValue() == expected;
}

void ArkSteedGraphVerifier::VerifyArgumentCount(ValueVertex* argc)
{
    if (auto* constant = argc->TryCast<Int64ConstantVertex>()) {
        ASSERT(constant->GetValue() >= NUM_MANDATORY_JSFUNC_ARGS);
        ASSERT(constant->GetValue() - NUM_MANDATORY_JSFUNC_ARGS <= INT32_MAX);
    } else {
        // Only the frame ABI supplies a nonnegative, INT32 user count.
        ASSERT(argc->Is<ActualArgcVertex>() && "Arguments require the actual frame count");
    }
}

void ArkSteedGraphVerifier::VerifyArgumentsElementsSize(ValueVertex* size, ValueVertex* argc)
{
    VerifyArgumentCount(argc);
    if (auto* constant = size->TryCast<Int64ConstantVertex>()) {
        auto* count = argc->Cast<Int64ConstantVertex>();
        int64_t expected = (count->GetValue() - NUM_MANDATORY_JSFUNC_ARGS) * JSTaggedValue::TaggedTypeSize() +
                           TaggedArray::DATA_OFFSET;
        ASSERT(constant->GetValue() == expected && "Arguments elements size must match the count");
        return;
    }
    // Match the full-width formula, not a narrowed copy of either the count
    // or the size: ((argc - mandatory) << slotShift) + dataOffset.
    auto* add = size->Cast<I64AddVertex>();
    ASSERT(IsInt64Constant(add->GetInput(I64AddVertex::RIGHT_INDEX), TaggedArray::DATA_OFFSET));
    auto* shift = add->GetInput(I64AddVertex::LEFT_INDEX)->Cast<I64ShiftLeftVertex>();
    ASSERT(shift->GetShift() == TAGGED_TYPE_SIZE_LOG);
    auto* subtract = shift->GetInput(I64ShiftLeftVertex::INPUT_INDEX)->Cast<I64SubVertex>();
    ASSERT(subtract->GetInput(I64SubVertex::LEFT_INDEX) == argc &&
           "Arguments elements size must match the count");
    ASSERT(IsInt64Constant(subtract->GetInput(I64SubVertex::RIGHT_INDEX), NUM_MANDATORY_JSFUNC_ARGS));
}

void ArkSteedGraphVerifier::VerifyTaggedSize(ValueVertex* taggedSize, ValueVertex* size)
{
    if (auto* constant = taggedSize->TryCast<TaggedConstantVertex>()) {
        auto* bytes = size->Cast<Int64ConstantVertex>();
        ASSERT(JSTaggedValue(constant->GetValue()).GetLargeUInt() == static_cast<uint64_t>(bytes->GetValue()) &&
               "Allocation must match the checked size");
        return;
    }
    auto* raw = taggedSize->Cast<RawI64ToTaggedVertex>();
    auto* bits = raw->GetInput(RawI64ToTaggedVertex::INPUT_INDEX);
    if (auto* constant = bits->TryCast<Int64ConstantVertex>()) {
        // Keep the proof valid if the size/tag OR has been constant-folded.
        auto* bytes = size->Cast<Int64ConstantVertex>();
        ASSERT(static_cast<uint64_t>(constant->GetValue()) ==
                   (static_cast<uint64_t>(bytes->GetValue()) | JSTaggedValue::TAG_INT) &&
               "Allocation must match the checked size");
        return;
    }
    auto* encoding = bits->Cast<I64BitwiseBinaryVertex>();
    ASSERT(encoding->GetKind() == IntBitwiseKind::BITWISE_OR);
    ASSERT(encoding->GetInput(I64BitwiseBinaryVertex::LEFT_INDEX) == size &&
           "Allocation must match the checked size");
    ASSERT(IsInt64Constant(encoding->GetInput(I64BitwiseBinaryVertex::RIGHT_INDEX),
                           static_cast<int64_t>(JSTaggedValue::TAG_INT)));
}

void ArkSteedGraphVerifier::TryRegisterAllocationPhi(PhiVertex* phi, const ArkSteedState& state)
{
    if (!IsAllocationPhi(phi)) {
        return;
    }
    // 2: one fast and one slow allocation input
    ASSERT(phi->GetInputCount() == 2 && "Allocation merge Phi requires exactly one fast and one slow input");
    auto* fast = phi->GetInput(0)->TryCast<AllocateInYoungVertex>();
    auto* slow = phi->GetInput(1)->TryCast<CallRuntimeVertex>();
    if (fast == nullptr || slow == nullptr) {
        fast = phi->GetInput(1)->TryCast<AllocateInYoungVertex>();
        slow = phi->GetInput(0)->TryCast<CallRuntimeVertex>();
    }
    ASSERT(fast != nullptr && slow != nullptr && slow->ReturnsUninitializedAllocation() &&
           "Allocation merge Phi requires one fast and one slow allocation");
    ASSERT(scratch_->allocationBlocks.count(fast) != 0 && scratch_->allocationBlocks.count(slow) != 0);
    BB* mergeBlock = state.GetBlock();
    ASSERT(scratch_->pendingAllocation == nullptr && "Second allocation in initialization interval");
    // 2: fast-path and slow-path blocks
    ASSERT(mergeBlock->GetPredecessors().size() == 2 && "Allocation merge requires two predecessors");
    for (uint32_t i = 0; i < 2; ++i) {  // 2: fast and slow inputs
        auto* allocation = phi->GetInput(i);
        auto* block = scratch_->allocationBlocks.at(allocation);
        ASSERT(block != nullptr && "Allocation has multiple merge Phis");
        ASSERT(block == mergeBlock->GetPredecessor(i) && "Allocation Phi input must match its predecessor");
        auto* jump = block->GetControlVertex()->Cast<JumpVertex>();
        ASSERT(jump->Target() == mergeBlock && "Allocation must jump to its merge Phi block");
    }
    ASSERT(scratch_->allocationBlocks.at(fast)->GetPredecessor(0) ==
           scratch_->allocationBlocks.at(slow)->GetPredecessor(0) &&
           "Allocation paths must share the capacity check");
    scratch_->allocationBlocks.at(fast) = nullptr;
    scratch_->allocationBlocks.at(slow) = nullptr;
    scratch_->pendingAllocation = phi;
}

template<typename VertexT>
void ArkSteedGraphVerifier::VerifyInitializer(VertexT* vertex)
{
    static_assert(!IsCall(VertexT::PROPERTIES) && !CanAllocate(VertexT::PROPERTIES) &&
                  !CanDeopt(VertexT::PROPERTIES) && !CanThrow(VertexT::PROPERTIES));
    // The arguments object uses only non-GC write-barrier calls. Its emitter
    // preserves the working registers and has no safepoint or recovery exit.
    // This exception is local to that initializer, not all deferred calls.
    static_assert(!IsDeferredCall(VertexT::PROPERTIES) ||
                  std::is_same_v<VertexT, InitializeUnmappedArgumentsVertex>);
    ASSERT(scratch_->pendingAllocation != nullptr && "Initializer without allocation in this block");
    ASSERT(vertex->GetInput(VertexT::ALLOCATION_INDEX) == scratch_->pendingAllocation);
    // The pending Phi has already passed TryRegisterAllocationPhi.
    auto* phi = scratch_->pendingAllocation->template Cast<PhiVertex>();
    auto* fast = phi->GetInput(0)->TryCast<AllocateInYoungVertex>();
    if (fast == nullptr) {
        fast = phi->GetInput(1)->Cast<AllocateInYoungVertex>();
    }
    if constexpr (!std::is_same_v<VertexT, InitializeUnmappedArgumentsElementsVertex>) {
        ASSERT(vertex->GetInput(VertexT::GLUE_INDEX) == fast->GetInput(AllocateInYoungVertex::GLUE_INDEX));
    }
    auto* size = fast->GetInput(AllocateInYoungVertex::SIZE_INDEX);
    if constexpr (std::is_same_v<VertexT, InitializeUnmappedArgumentsElementsVertex>) {
        // This installs the array header. LowerGetUnmappedArgs then fills
        // every element in its safepoint-free copy loop before allocating
        // the arguments object; the loop's barriers are non-GC calls.
        VerifyArgumentsElementsSize(size, vertex->GetInput(VertexT::NUM_ARGS_INDEX));
    } else if constexpr (std::is_same_v<VertexT, InitializeUnmappedArgumentsVertex>) {
        auto* argc = vertex->GetInput(VertexT::NUM_ARGS_INDEX);
        VerifyArgumentCount(argc);
        auto* elements = vertex->GetInput(VertexT::ELEMENTS_INDEX)
                             ->template Cast<InitializeUnmappedArgumentsElementsVertex>();
        ASSERT(elements->GetInput(InitializeUnmappedArgumentsElementsVertex::NUM_ARGS_INDEX) == argc &&
               "Arguments object and elements must use the same count");
        constexpr uint32_t objectSize
            = JSArguments::GetInlinedPropertyOffset(JSArguments::LENGTH_OF_INLINE_PROPERTIES);
        static_assert(JSHClass::DEFAULT_CAPACITY_OF_IN_OBJECTS == JSArguments::LENGTH_OF_INLINE_PROPERTIES);
        ASSERT(IsInt64Constant(size, objectSize) && "Arguments object requires its static layout size");
    } else {
        VerifyLiteralInitializer(vertex, size);
    }
    scratch_->pendingAllocation = nullptr;
}

template<typename VertexT>
void ArkSteedGraphVerifier::VerifyLiteralInitializer(VertexT* vertex, ValueVertex* size)
{
    uint32_t objectSize;
    uint32_t rootIndex;
    if constexpr (std::is_same_v<VertexT, InitializeObjectWithBufferVertex>) {
        const auto& info = vertex->GetInfo();
        objectSize = info.objectSize;
        rootIndex = VertexT::LITERAL_INDEX;
        VerifyStaticSize(objectSize, JSObject::SIZE);
        // Raw numeric bits are not tagged values. The broker proves the snapshot contains no roots.
    } else {
        objectSize = vertex->GetObjectSize();
        if constexpr (std::is_same_v<VertexT, InitializeEmptyObjectVertex>) {
            rootIndex = VertexT::HCLASS_INDEX;
            VerifyStaticSize(objectSize, JSObject::SIZE);
            ASSERT(vertex->GetInput(VertexT::SIZE_INDEX) == size);
        } else {
            VerifyStaticSize(objectSize, JSArray::SIZE + JSTaggedValue::TaggedTypeSize());
            if constexpr (std::is_same_v<VertexT, InitializeEmptyArrayVertex>) {
                rootIndex = VertexT::GLOBAL_ENV_INDEX;
            } else {
                rootIndex = VertexT::LITERAL_INDEX;
            }
        }
    }
    ASSERT(IsInt64Constant(size, objectSize) && "Literal allocation requires its static size");
    // These inputs are broker-protected heap constants, not raw pointers hidden in metadata.
    ASSERT(vertex->GetInput(rootIndex)->template Is<HeapConstantVertex>() &&
           "Initializer requires a rooted constant");
}

template<typename VertexT>
void ArkSteedGraphVerifier::VerifyInitializationInterval(VertexT* vertex, const ArkSteedState& state)
{
    if constexpr (std::is_same_v<VertexT, AllocateInYoungVertex> ||
                  std::is_same_v<VertexT, CallRuntimeVertex>) {
        if (IsUninitializedAllocation(vertex)) {
            VerifyUninitializedAllocation(vertex, state);
            scratch_->pendingAllocation = vertex;
            return;
        }
    }
    if constexpr (IsInitializer<VertexT>()) {
        VerifyInitializer(vertex);
    } else {
        if constexpr (std::is_same_v<VertexT, GapMoveVertex> ||
                      std::is_same_v<VertexT, ConstantGapMoveVertex>) {
            static_assert(!IsCall(VertexT::PROPERTIES) && !IsDeferredCall(VertexT::PROPERTIES) &&
                          !CanAllocate(VertexT::PROPERTIES) && !CanDeopt(VertexT::PROPERTIES) &&
                          !CanThrow(VertexT::PROPERTIES));
        }
        if constexpr (std::is_same_v<VertexT, ConstantGapMoveVertex>) {
            // ConstantGapMove has no SSA inputs; its source is a separate member.
            ASSERT(vertex->GetVertex() != nullptr);
            ASSERT(IsConstantValueVertex(vertex->GetVertex()->GetOpcode()));
        }
        if (scratch_->pendingAllocation != nullptr) {
            // Spill reloads are GapMove vertices, not arbitrary field loads. No other
            // operation, even a pure one, is allowed to interrupt initialization.
            // The merge Phi registers itself just above and must not trip this check.
            constexpr bool isMove
                = std::is_same_v<VertexT, GapMoveVertex> || std::is_same_v<VertexT, ConstantGapMoveVertex>;
            constexpr bool isMergeJump = std::is_same_v<VertexT, JumpVertex>;
            constexpr bool isPhiType = std::is_same_v<VertexT, PhiVertex>;
            bool isRegisteredPhi = isPhiType && scratch_->pendingAllocation == static_cast<Vertex*>(vertex);
            ASSERT((isRegisteredPhi || (afterRegalloc_ && isMove) ||
                    (isMergeJump && IsUninitializedAllocation(scratch_->pendingAllocation))) &&
                   "Illegal operation in initialization interval");
        }
    }
}

template<typename VertexT>
void ArkSteedGraphVerifier::VerifyUninitializedAllocation(VertexT* vertex, const ArkSteedState& state)
{
    static_assert(!IsDeferredCall(VertexT::PROPERTIES) && !CanEagerDeopt(VertexT::PROPERTIES));
    auto* block = state.GetBlock();
    ASSERT(scratch_->pendingAllocation == nullptr && "Second allocation in initialization interval");
    ASSERT(scratch_->allocationBlocks.insert({vertex, block}).second && "Allocation scheduled more than once");
    ASSERT(block->GetPredecessors().size() == 1 && "Allocation requires one checking predecessor");
    auto* checkBlock = block->GetPredecessor(0);
    auto* control = checkBlock->GetControlVertex();
    ASSERT(control != nullptr && "Allocation requires a capacity check");
    auto* branch = control->template Cast<BranchIfTrueVertex>();
    auto* check = branch->GetInput(0)->template TryCast<CanAllocateInYoungVertex>();
    ASSERT(check != nullptr && "Allocation requires a capacity predicate");
    // A boolean from another block or from before an intervening operation
    // cannot prove that the current bump pointer still has enough capacity.
    VerifyCapacityPredicate(checkBlock, check);
    auto *size = check->GetInput(CanAllocateInYoungVertex::SIZE_INDEX);
    // The matching initializer proves the size's source and layout. Literal
    // sizes remain regular constants; argument elements may be huge.
    if constexpr (std::is_same_v<VertexT, AllocateInYoungVertex>) {
        static_assert(!IsCall(VertexT::PROPERTIES) && !CanDeopt(VertexT::PROPERTIES) &&
                      !CanThrow(VertexT::PROPERTIES) && CanAllocate(VertexT::PROPERTIES));
        ASSERT(check->GetInput(CanAllocateInYoungVertex::GLUE_INDEX) == vertex->GetInput(VertexT::GLUE_INDEX) &&
               size == vertex->GetInput(VertexT::SIZE_INDEX) &&
               "Allocation must match the checked glue and size");
        ASSERT(branch->IfTrue() == block && "Fast allocation requires the successful check edge");
    } else {
        static_assert(IsCall(VertexT::PROPERTIES) && CanAllocate(VertexT::PROPERTIES));
        // Runtime argv starts with tagged size and may include HClass; glue comes from the call ABI.
        VerifyTaggedSize(vertex->GetInput(0), size);
        ASSERT(branch->IfFalse() == block && "Slow allocation requires the failed check edge");
    }
    VerifyAllocationPrecedingMoves(block, vertex);
}

void ArkSteedGraphVerifier::EstablishBlockDomain(Graph *graph)
{
    for (BB *block : *graph) {
        ASSERT(block != nullptr);
        ASSERT(block->GetId() < graph->MaxBlockId());
        ASSERT(scratch_->blocks.insert(block).second);
        // Pre-allocation boundary of a block: loop reload/spill hints are
        // produced by `LivenessProcessor` and an initialized merge state is
        // installed by the register allocator, both after this seam. An
        // uninitialized `RegisterMergeState` is the ordinary builder state
        // (`GraphBuilder::NewBlock` gives every production block an empty
        // one), so it is not an allocation marker.
        ASSERT(block->GetRegallocLoopInfo() == nullptr);
        ASSERT(!(block->HasRegisterMergeState() && block->GetRegisterMergeState()->IsInitialized()));
    }
}

void ArkSteedGraphVerifier::RegisterThrowable(Vertex *vertex)
{
    if (vertex == nullptr) {
        return;
    }
    ThrowableMixin *mixin = ThrowableMixinOf(vertex);
    if (mixin == nullptr || !mixin->HasCatchBlock()) {
        return;
    }
    BB *catchBlock = mixin->GetCatchBlock();
    ASSERT(catchBlock != nullptr);
    ASSERT(scratch_->blocks.find(catchBlock) != scratch_->blocks.end());
    const uint32_t index = mixin->GetStoredCatchPredecessorIndex();
    ASSERT(index != static_cast<uint32_t>(-1));
    auto it = scratch_->catchSlots.find(catchBlock);
    verifier_details::CatchSlotFacts *facts = nullptr;
    if (it == scratch_->catchSlots.end()) {
        facts = chunk_->New<verifier_details::CatchSlotFacts>(chunk_);
        scratch_->catchSlots.emplace(catchBlock, facts);
    } else {
        facts = it->second;
    }
    facts->count += 1;
    ASSERT(facts->indexes.insert(index).second);
}

template <typename VertexT>
void ArkSteedGraphVerifier::VerifyDirectInputs(VertexT *vertex)
{
    const uint32_t storedCount = static_cast<const Vertex *>(vertex)->GetInputCount();
    if constexpr (verifier_details::HasNumInputs<VertexT>::value) {
        ASSERT(storedCount == VertexT::NUM_INPUTS);
    }
    // Direct/reference category of every stored slot, checked before any typed
    // access to the referenced object, plus its resolution to a graph-owned
    // object: an executable member or a retained registry object. A dangling or
    // foreign producer is corruption, whether or not the referencing member is
    // still live.
    for (uint32_t index = 0; index < storedCount; ++index) {
        ValueVertex *input = vertex->GetInput(index);
        ASSERT(input != nullptr);
        VertexOpcode opcode = input->GetOpcode();
        ASSERT(opcode >= FIRST_OPCODE);
        ASSERT(opcode <= LAST_OPCODE);
        ASSERT(verifier_details::IsConcreteValueVertex(input));
        ASSERT(afterRegalloc_ ||
               verifier_details::ResolvesToRetainedObject(scratch_->members, scratch_->registry, input));
        if (IsUninitializedAllocation(input)) {
            // Raw storage may only be consumed by the merge Phi; the Phi is
            // validated by TryRegisterAllocationPhi below.
            ASSERT((std::is_same_v<VertexT, PhiVertex>)&&"Allocation escapes initialization");
        }
        if (IsAllocationPhi(input)) {
            if constexpr (IsInitializer<VertexT>()) {
                ASSERT(index == VertexT::ALLOCATION_INDEX && input == scratch_->pendingAllocation &&
                       "Allocation Phi may only feed its matching initializer");
            } else {
                ASSERT(false && "Allocation Phi escapes initialization");
            }
        }
    }
}

template <typename VertexT>
void ArkSteedGraphVerifier::VerifyInputRepresentations(VertexT *vertex)
{
    if constexpr (details::HasInputTypes<VertexT>::value) {
        const uint32_t storedCount = static_cast<const Vertex *>(vertex)->GetInputCount();
        static_assert(VertexT::INPUT_TYPES.size() == VertexT::NUM_INPUTS, "INPUT_TYPES / NUM_INPUTS mismatch");
        for (uint32_t index = 0; index < storedCount; ++index) {
            ValueVertex *input = vertex->GetInput(index);
            ValueRepresentation expectedRepr = VertexT::INPUT_TYPES[index];
            ASSERT(input->GetValueRepresentation() == expectedRepr);
        }
    }
}

void ArkSteedGraphVerifier::VerifyFrameReferences(Vertex *vertex)
{
    if (const EagerDeoptimizableMixin *eager = EagerDeoptimizableMixinOf(vertex); eager != nullptr) {
        for (uint32_t index = 0, count = eager->GetDeoptFrameValueCount(); index < count; ++index) {
            const ValueVertex *value = eager->GetDeoptFrameValue(index);
            ASSERT(value != nullptr);
            ASSERT(!IsUninitializedAllocation(value) && !IsAllocationPhi(value) &&
                   "Allocation escapes into frame state");
            ASSERT(afterRegalloc_ ||
                   verifier_details::IsFrameValueCompatible(value, eager->GetDeoptValueKind(index)));
            ASSERT(afterRegalloc_ ||
                   verifier_details::ResolvesToRetainedObject(scratch_->members, scratch_->registry, value));
        }
    }
    if (const LazyDeoptimizableMixin *lazy = LazyDeoptimizableMixinOf(vertex);
        lazy != nullptr && lazy->HasLazyDeoptFrameState()) {
        for (uint32_t index = 0, count = lazy->GetDeoptFrameValueCount(); index < count; ++index) {
            const ValueVertex *value = lazy->GetDeoptFrameValue(index);
            ASSERT(value != nullptr);
            ASSERT(!IsUninitializedAllocation(value) && !IsAllocationPhi(value) &&
                   "Allocation escapes into frame state");
            ASSERT(afterRegalloc_ ||
                   verifier_details::IsFrameValueCompatible(value, lazy->GetDeoptValueKind(index)));
            ASSERT(afterRegalloc_ ||
                   verifier_details::ResolvesToRetainedObject(scratch_->members, scratch_->registry, value));
        }
    }
}

void ArkSteedGraphVerifier::VerifyCatchReferences(Vertex *vertex)
{
    if (const ThrowableMixin *throwable = ThrowableMixinOf(vertex);
        !afterRegalloc_ && throwable != nullptr && throwable->HasCatchBlock()) {
        const BB *catchBlock = throwable->GetCatchBlock();
        ASSERT(catchBlock != nullptr);
        ASSERT(scratch_->blocks.find(catchBlock) != scratch_->blocks.end());
        const uint32_t index = throwable->GetStoredCatchPredecessorIndex();
        ASSERT(index != static_cast<uint32_t>(-1));
        for (PhiVertex *phi : catchBlock->GetPhis()) {
            ASSERT(phi != nullptr);
            ASSERT(index < phi->GetInputCount());
            ValueVertex *incoming = phi->GetInput(index);
            ASSERT(verifier_details::IsConcreteValueVertex(incoming));
            ASSERT(verifier_details::ResolvesToRetainedObject(scratch_->members, scratch_->registry, incoming));
        }
    }
}

void ArkSteedGraphVerifier::VerifyCapacityPredicate(BB *checkBlock, CanAllocateInYoungVertex *check)
{
    bool foundCheck = false;
    for (auto* preceding: checkBlock->GetVertices()) {
        if (preceding == check) {
            ASSERT(!foundCheck && "Capacity predicate scheduled more than once");
            foundCheck = true;
        } else if (foundCheck) {
            ASSERT(afterRegalloc_ && (preceding->Is<GapMoveVertex>() || preceding->Is<ConstantGapMoveVertex>()) &&
                   "Only register moves may separate the capacity predicate and branch");
        }
    }
    ASSERT(foundCheck && "Capacity predicate must be scheduled in the checking block");
    auto* size = check->GetInput(CanAllocateInYoungVertex::SIZE_INDEX);
    ASSERT(size->GetValueRepresentation() == ValueRepresentation::INT64);
    if (auto* constant = size->template TryCast<Int64ConstantVertex>()) {
        auto bytes = static_cast<uint64_t>(constant->GetValue());
        ASSERT(bytes >= TaggedArray::DATA_OFFSET && (bytes & JSTaggedValue::TAG_MARK) == 0);
        ASSERT(bytes % JSTaggedValue::TaggedTypeSize() == 0);
    }
}

void ArkSteedGraphVerifier::VerifyAllocationPrecedingMoves(BB *block, Vertex *vertex)
{
    ASSERT(!block->HasPhi() && "Allocation block must preserve checked inputs");
    for (auto* preceding: block->GetVertices()) {
        if (preceding == vertex) {
            break;
        }
        ASSERT(afterRegalloc_ && (preceding->Is<GapMoveVertex>() || preceding->Is<ConstantGapMoveVertex>()) &&
               "Only register moves may separate the check and allocation");
    }
}

// Reuse GraphProcessor's sole concrete-opcode dispatch to instantiate every
// typed verifier contract in this translation unit.
template class GraphProcessor<ArkSteedGraphVerifier>;

}  // namespace panda::ecmascript::arksteed
#endif  // !NDEBUG
