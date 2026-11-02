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

#include "ecmascript/arksteed/arksteed_dce.h"

#include <utility>

#include "ecmascript/arksteed/arksteed_graph.h"
#include "ecmascript/arksteed/arksteed_opcode.h"
#include "ecmascript/mem/chunk_containers.h"

namespace panda::ecmascript::arksteed {

namespace dce_internal {

class DependencyAccess {
public:
    // Enumeration of the prospective surviving-Phi view used by the `Filter`
    // selection assertion.
    template <typename Callback>
    static void EmitReferences(Vertex *vertex, Callback &callback, const DceRemovalSet *prospectiveRemovals)
    {
        DceDependencies::EmitReferences(vertex, callback, prospectiveRemovals);
    }

    // Enumeration of the ordinary closure, with the private live-Phi catch guard.
    template <typename Callback>
    static void EmitClosureReferences(Vertex *vertex, Callback &callback)
    {
        DceDependencies::EmitClosureReferences(vertex, callback);
    }
};

}  // namespace dce_internal

const char *DceRemovalClassName(DceRemovalClass removalClass)
{
    switch (removalClass) {
        case DceRemovalClass::Required:
            return "Required";
        case DceRemovalClass::RemovableValue:
            return "RemovableValue";
    }
    return "Unknown";
}

const char *DceResultName(DceResult result)
{
    switch (result) {
        case DceResult::Unchanged:
            return "Unchanged";
        case DceResult::Changed:
            return "Changed";
    }
    return "Unknown";
}

// =============================================================================
// DceRemovalPolicy
//
// The admitted set is not enumerated here. It is declared once per opcode by the
// positive `VertexPropertyFlag::CAN_BE_REMOVED_IF_DEAD` bit of the central
// `PROPERTIES` in `arksteed_opcode.h`. Every admitted opcode is a total numeric,
// bitwise, comparison or conversion value (or a Phi) whose emitter is
// register-only on x64 and arm64 and whose emitted code
// cannot throw, deopt, write, read observable state, allocate, invoke a hook or
// reach a runtime helper or safepoint. Every other opcode lacks the bit, so a new
// opcode is conservatively required until the audit admits it.
//
// The conservative default is a policy decision and is deliberately separate
// from the concrete dispatch of the transform below: an opcode absent from
// `ALL_VERTEX_LIST` carries no reviewed removal proof, so it is `Required` even
// when a malformed object happens to carry the property bit.
// =============================================================================

namespace {

using CandidateSet = DceRemovalSet;
using RegistrySet = ChunkUnorderedSet<const Vertex *>;

// The concrete enum is contiguous between the bounds generated from
// `ALL_VERTEX_LIST`; INVALID and unknown values have no reviewed removal proof.
bool IsKnownConcreteOpcode(VertexOpcode opcode)
{
    return opcode >= FIRST_OPCODE && opcode <= LAST_OPCODE;
}

// Invocation owner of the transient `Vertex::DceMarked` liveness bit. It records
// each object *before* it sets that object's bit, so cleanup reaches removed
// members and every object whose bit this invocation changed: `ApplyRemovals`
// detaches a removed member before this owner is destroyed, and destruction
// still clears the same recorded pointer. The owner is compilation-local and
// allocates only its one chunk vector, and it is also the monotonic worklist of
// the ordinary closure: marks are appended in first-mark order and an index
// cursor processes each recorded vertex exactly once.
class InvocationBits {
public:
    explicit InvocationBits(Chunk *chunk) : marked_(chunk) {}

    ~InvocationBits()
    {
        for (Vertex *vertex : marked_) {
            vertex->SetDceMarked(false);
        }
    }

    NO_COPY_SEMANTIC(InvocationBits);
    NO_MOVE_SEMANTIC(InvocationBits);

    // Monotonic liveness: reports whether `vertex` became live. A null vertex is
    // never a live vertex and is never recorded. The pointer is recorded before
    // the transient bit is set, so cleanup reaches every object whose bit this
    // invocation changes even if the invocation stops between the record and the
    // mutation.
    bool MarkLive(Vertex *vertex)
    {
        if (vertex == nullptr || vertex->IsDceMarked()) {
            return false;
        }
        marked_.push_back(vertex);
        vertex->SetDceMarked(true);
        return true;
    }

    ChunkVector<Vertex *> &Marked()
    {
        return marked_;
    }

private:
    ChunkVector<Vertex *> marked_;
};

// Marks every graph-owned entry/constant registry vertex. Core never filters a
// graph-owned registry, so a registry object is never a removal candidate even
// when the caller did not mark it. The stored pointer set is pointer-only and
// allocation failure inside the compilation Chunk is process-fatal, so there is
// no recoverable error path here. A null pool entry contributes no registration.
void CollectRegistryEntries(Graph *graph, RegistrySet &registries)
{
    auto add = [&registries](const ValueVertex *value) {
        if (value != nullptr) {
            registries.insert(value);
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

// Upper bound of the closure's mark record. Every mark is an executable list
// member (Phi, non-control value or control vertex) or a retained registry
// entry, so the sum of the list sizes plus the registry and parameter counts
// bounds it. It is a reservation hint only: the vector may still grow and never
// overruns it, and there is no graph-size admission cutoff.
size_t CountMarkableVertices(Graph *graph)
{
    size_t bound = 0;
    for (BB *block : *graph) {
        if (block == nullptr) {
            continue;
        }
        bound += block->GetPhis().size();
        bound += block->GetVertices().size();
        if (block->GetControlVertex() != nullptr) {
            bound += 1;
        }
    }
    bound += graph->GetParameterCount();
    bound += graph->GetInt32Constants().size();
    bound += graph->GetInt64Constants().size();
    bound += graph->GetFloat64Constants().size();
    bound += graph->GetTaggedConstants().size();
    bound += graph->GetHeapConstants().size();
    return bound;
}

DceResult ApplyRemovals(Graph *graph, const CandidateSet &candidates);

// =============================================================================
// Invocation-local semantic preparation
//
// One `DcePreparation` owns the semantic state of exactly one invocation: the
// retained-registry pointer set, the monotonic liveness marks or the
// caller-supplied live set, the removal selection and the handoff to the single
// `ApplyRemovals` commit authority. It is private implementation of this
// component: it is not a public interface, it exports no fact, and its facts
// live only until the invocation returns.
//
// Common concrete/graph integrity is not re-validated here. The input is the
// trusted finalized pre-allocation graph that the normal Debug
// `ArkSteedGraphVerifier` has already checked, so this preparation contains no
// validation cache, no structural index, no diagnostic route and no failure
// result. Only the selection-specific Debug assertions retained by the design
// are conditional; they establish caller obligations before the only list write
// and are absent from the Release build.
// =============================================================================

class DcePreparation {
public:
    explicit DcePreparation(Graph *graph)
        : graph_(graph),
          bits_(graph->GetChunk()),
          registries_(graph->GetChunk()),
#ifndef NDEBUG
          members_(graph->GetChunk()),
#endif
          candidates_(graph->GetChunk())
    {
    }

    NO_COPY_SEMANTIC(DcePreparation);
    NO_MOVE_SEMANTIC(DcePreparation);

    DceResult Run();
    DceResult Filter(const DceLiveSet &live);

private:
    void RegisterRegistries();
    void SelectClosureCandidates();
    void SelectSuppliedCandidates(const DceLiveSet &live);
    void AddCandidate(Vertex *vertex, bool live);
    bool IsRegistryEntry(const Vertex *vertex) const;
    DceResult Commit();

#ifndef NDEBUG
    // Debug-only caller-obligation domain. The Release transform does not need
    // executable membership, so it does not build or maintain it.
    void RegisterMembers();
    bool IsExecutableMember(const Vertex *vertex) const;
    void AssertSuppliedLivePointers(const DceLiveSet &live) const;
    void AssertSelectionIsAuthorized() const;
    void AssertSurvivorReferencesRetained() const;
    void AssertMemberReferencesRetained(Vertex *vertex) const;
#endif

    Graph *graph_ {nullptr};
    InvocationBits bits_;
    RegistrySet registries_;
#ifndef NDEBUG
    CandidateSet members_;
#endif
    CandidateSet candidates_;
};

void DcePreparation::RegisterRegistries()
{
    CollectRegistryEntries(graph_, registries_);
}

bool DcePreparation::IsRegistryEntry(const Vertex *vertex) const
{
    return vertex != nullptr && registries_.find(vertex) != registries_.end();
}

// The ordinary pass. Roots are exactly the retained controls, the `Required`-
// classified non-control values/Phis and every graph-registered entry binding.
// Constant pools are not roots: a constant becomes live only through a real
// dependency. The drain is monotonic (one pointer worklist, each marked vertex
// processed at most once) and the catch guard contributes only a live installed
// catch Phi. Because every live Phi emits all of its own inputs when it is
// processed, a Phi that becomes live after its throwable was processed still
// contributes the incoming value at the throwable's stored catch index, and the
// result is independent of worklist order.
DceResult DcePreparation::Run()
{
    RegisterRegistries();
#ifndef NDEBUG
    RegisterMembers();
#endif

    ChunkVector<Vertex *> &worklist = bits_.Marked();
    worklist.reserve(CountMarkableVertices(graph_));

    for (BB *block : *graph_) {
        bits_.MarkLive(block->GetControlVertex());
        for (PhiVertex *phi : block->GetPhis()) {
            if (DceRemovalPolicy::Classify(phi) == DceRemovalClass::Required) {
                bits_.MarkLive(phi);
            }
        }
        for (NonControlVertex *vertex : block->GetVertices()) {
            if (DceRemovalPolicy::Classify(vertex) == DceRemovalClass::Required) {
                bits_.MarkLive(vertex);
            }
        }
    }
    for (size_t index = 0, count = graph_->GetParameterCount(); index < count; ++index) {
        bits_.MarkLive(graph_->GetParameter(static_cast<uint32_t>(index)));
    }

    auto mark = [this](ValueVertex *dependency) { bits_.MarkLive(dependency); };
    for (size_t cursor = 0, count = worklist.size(); cursor < count; ++cursor) {
        dce_internal::DependencyAccess::EmitClosureReferences(worklist[cursor], mark);
        count = worklist.size();
    }

    SelectClosureCandidates();
#ifndef NDEBUG
    AssertSelectionIsAuthorized();
#endif
    return Commit();
}

// The public `Filter` route. The caller's live set is the liveness source; it is
// read only and never modified.
DceResult DcePreparation::Filter(const DceLiveSet &live)
{
    RegisterRegistries();
#ifndef NDEBUG
    RegisterMembers();
    AssertSuppliedLivePointers(live);
#endif
    SelectSuppliedCandidates(live);
#ifndef NDEBUG
    AssertSelectionIsAuthorized();
    AssertSurvivorReferencesRetained();
#endif
    return Commit();
}

// Candidate removal is exactly an unmarked policy-authorized `RemovableValue`
// member: controls, `Required` operations and registry entries are never
// candidates.
void DcePreparation::SelectClosureCandidates()
{
    for (BB *block : *graph_) {
        for (PhiVertex *phi : block->GetPhis()) {
            AddCandidate(phi, phi->IsDceMarked());
        }
        for (NonControlVertex *vertex : block->GetVertices()) {
            AddCandidate(vertex, vertex->IsDceMarked());
        }
    }
}

void DcePreparation::SelectSuppliedCandidates(const DceLiveSet &live)
{
    for (BB *block : *graph_) {
        for (PhiVertex *phi : block->GetPhis()) {
            AddCandidate(phi, live.IsLive(phi));
        }
        for (NonControlVertex *vertex : block->GetVertices()) {
            AddCandidate(vertex, live.IsLive(vertex));
        }
    }
}

void DcePreparation::AddCandidate(Vertex *vertex, bool live)
{
    if (live || IsRegistryEntry(vertex)) {
        return;
    }
    if (DceRemovalPolicy::Classify(vertex) != DceRemovalClass::RemovableValue) {
        return;
    }
    candidates_.insert(vertex);
}

// All graph facts, live-set reads and Debug obligations are complete before
// this call; it performs the only list write of the invocation.
DceResult DcePreparation::Commit()
{
    return ApplyRemovals(graph_, candidates_);
}

#ifndef NDEBUG
// Debug-only membership registration of the executable structure. It is used by
// the selection assertions below and is deliberately absent from the Release
// transform, which trusts the verified graph.
void DcePreparation::RegisterMembers()
{
    for (BB *block : *graph_) {
        for (PhiVertex *phi : block->GetPhis()) {
            members_.insert(phi);
        }
        for (NonControlVertex *vertex : block->GetVertices()) {
            members_.insert(vertex);
        }
        if (block->GetControlVertex() != nullptr) {
            members_.insert(block->GetControlVertex());
        }
    }
}

bool DcePreparation::IsExecutableMember(const Vertex *vertex) const
{
    return vertex != nullptr && members_.find(vertex) != members_.end();
}

// The public `Filter` caller obligation: a live-set pointer that does not
// resolve to this graph's executable or registry domain is not a live vertex.
void DcePreparation::AssertSuppliedLivePointers(const DceLiveSet &live) const
{
    for (const Vertex *entry : live) {
        ASSERT(entry != nullptr);
        ASSERT(IsExecutableMember(entry) || IsRegistryEntry(entry));
    }
}

// The selection is authorized: every selected removal is a non-control
// `RemovableValue` of the executable non-control/Phi lists.
void DcePreparation::AssertSelectionIsAuthorized() const
{
    for (const Vertex *candidate : candidates_) {
        ASSERT(candidate != nullptr);
        ASSERT(!IsControlVertex(candidate->GetOpcode()));
        ASSERT(DceRemovalPolicy::Classify(const_cast<Vertex *>(candidate)) == DceRemovalClass::RemovableValue);
    }
}

void DcePreparation::AssertMemberReferencesRetained(Vertex *vertex) const
{
    if (candidates_.find(vertex) != candidates_.end()) {
        return;
    }
    bool retained = true;
    auto check = [this, &retained](ValueVertex *dependency) {
        if (IsRegistryEntry(dependency)) {
            return;
        }
        if (dependency == nullptr || members_.find(dependency) == members_.end() ||
            candidates_.find(dependency) != candidates_.end()) {
            retained = false;
        }
    };
    dce_internal::DependencyAccess::EmitReferences(vertex, check, &candidates_);
    ASSERT(retained);
}

// The public `Filter` caller obligation: every dependency of every surviving
// member resolves to a retained object under the prospective surviving catch-Phi
// view (a selected dead catch Phi contributes no incoming value). References of
// a selected removal itself are not part of this obligation; common integrity of
// the whole graph is owned by the normal Debug verifier.
void DcePreparation::AssertSurvivorReferencesRetained() const
{
    for (BB *block : *graph_) {
        for (PhiVertex *phi : block->GetPhis()) {
            AssertMemberReferencesRetained(phi);
        }
        for (NonControlVertex *vertex : block->GetVertices()) {
            AssertMemberReferencesRetained(vertex);
        }
        if (block->GetControlVertex() != nullptr) {
            AssertMemberReferencesRetained(block->GetControlVertex());
        }
    }
}
#endif  // !NDEBUG

// Allocation-free stable compaction of the two executable list kinds. The
// removal selection is authoritative; an empty selection is `Unchanged` and
// performs no write. Survivors keep their order, their inputs and every live Phi
// input; shrinking a `ChunkVector` does not allocate.
DceResult ApplyRemovals(Graph *graph, const CandidateSet &candidates)
{
    if (candidates.empty()) {
        return DceResult::Unchanged;
    }
    bool changed = false;
    for (BB *block : *graph) {
        // Phi list: survivors keep their relative order and every live Phi input.
        ChunkVector<PhiVertex *> &phis = block->GetPhis();
        size_t write = 0;
        for (size_t read = 0, count = phis.size(); read < count; ++read) {
            PhiVertex *phi = phis[read];
            if (candidates.find(phi) != candidates.end()) {
                changed = true;
                continue;
            }
            phis[write] = phi;
            ++write;
        }
        phis.resize(write);

        // Non-control vertex list: survivors keep their relative order. A
        // registry entry (a parameter descriptor or a pooled constant) is kept
        // in place even when it is not marked.
        ChunkVector<NonControlVertex *> &vertices = block->GetVertices();
        write = 0;
        for (size_t read = 0, count = vertices.size(); read < count; ++read) {
            NonControlVertex *vertex = vertices[read];
            if (candidates.find(vertex) != candidates.end()) {
                changed = true;
                continue;
            }
            vertices[write] = vertex;
            ++write;
        }
        vertices.resize(write);
    }

    return changed ? DceResult::Changed : DceResult::Unchanged;
}

}  // namespace

// The removal policy reads the positive removal property of the vertex itself.
// The bit is written from the concrete class's central `PROPERTIES` by
// `Vertex::New`, so a real vertex always carries the annotation of its own
// opcode. An absent bit (`INVALID`, an unknown or unproved opcode, the
// registry-owned constants, the entry bindings and every trap-sensitive or
// effecting opcode) is `Required` before any concrete cast is attempted.
DceRemovalClass DceRemovalPolicy::Classify(Vertex *vertex)
{
    if (vertex == nullptr || !IsKnownConcreteOpcode(vertex->GetOpcode())) {
        return DceRemovalClass::Required;
    }
    return vertex->CanBeRemovedIfDead() ? DceRemovalClass::RemovableValue : DceRemovalClass::Required;
}

// The public filtering authority. `Failed` no longer exists: the input is a
// trusted verified graph, so this route has exactly the two survivor outcomes.
DceResult DceGraphEditor::Filter(Graph *graph, const DceLiveSet &live)
{
    ASSERT(graph != nullptr);
    DcePreparation preparation(graph);
    return preparation.Filter(live);
}

// The ordinary pass performs exactly one semantic preparation per invocation
// with the compilation Chunk: the retained-registry set, one monotonic closure
// with the live-Phi catch guard, the removal selection and the single
// `ApplyRemovals` commit. There is no separate probe traversal, no
// reduced-depth preflight and no re-preparing anomaly fallback, and the pass
// never calls the public `DceGraphEditor::Filter` to repeat preparation. The
// closure result is deterministic for a fixed graph: it is the root closure
// under the direct/eager/lazy edges, and the guarded catch emission cannot
// change it because a live catch Phi always emits all of its own inputs.
DceResult ArkSteedDce::Run(Graph *graph)
{
    ASSERT(graph != nullptr);
    DcePreparation preparation(graph);
    return preparation.Run();
}

}  // namespace panda::ecmascript::arksteed
