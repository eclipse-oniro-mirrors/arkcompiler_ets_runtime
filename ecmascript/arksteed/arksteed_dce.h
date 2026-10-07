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

#ifndef ECMASCRIPT_ARKSTEED_ARKSTEED_DCE_H
#define ECMASCRIPT_ARKSTEED_ARKSTEED_DCE_H

#include <cstddef>
#include <cstdint>

#include "ecmascript/arksteed/arksteed_graph.h"
#include "ecmascript/arksteed/arksteed_opcode.h"
#include "ecmascript/arksteed/arksteed_vertex.h"
#include "ecmascript/mem/chunk_containers.h"

namespace panda::ecmascript::arksteed {

// =============================================================================
// Shared DCE contract.
//
// This component is the single owner of the three surfaces every ArkSteed DCE
// mechanism consumes:
//
//   * `DceRemovalPolicy`   - the removal authorization gate;
//   * `DceDependencies`    - the complete implicit/direct dependency
//                            enumeration;
//   * `DceGraphEditor`     - the only authority that filters graph-owned
//                            executable lists.
//
// `ArkSteedDce` is the fourth surface: the graph-local
// root-marking and dependency-closure transform that feeds the editor. It is
// invoked by `ArkSteedCompilerTask::Compile` after the existing producers and
// before allocation metadata.
//
// Trusted internal IR: this component does not validate ordinary internal graph
// integrity. The common concrete layout/options, membership/ownership,
// registry/parameter, direct/frame/catch reference and CFG/predecessor/Phi/
// exception/loop invariants of a finalized pre-allocation graph belong to the
// normal Debug `ArkSteedGraphVerifier` contract, which the compiler runs before
// and after this pass. Malformed internal IR is a
// programmer error that the Debug verifier asserts; Release trusts valid IR.
// This component therefore returns no failure result, allocates no graph
// diagnostic and keeps no second validation route.
//
// The public boundary is exactly `DceRemovalPolicy::Classify`, the typed
// two-argument `DceDependencies::Visit`, the live-set two-argument
// `DceGraphEditor::Filter` and `ArkSteedDce::Run`. Opcode dispatch helpers and
// the prospective surviving-Phi traversal are private implementation of this
// component; no public overload, extension channel or global last-error state
// exists.
// =============================================================================

// Removal classes. Exactly two classes exist, because a vertex is either an
// execution/metadata obligation that must run, or a total value whose removal
// cannot be observed. `VertexPropertyFlag` is a conservative veto only and is
// never a removal proof by itself.
enum class DceRemovalClass : uint8_t {
    Required = 0,
    RemovableValue = 1,
};

const char *DceRemovalClassName(DceRemovalClass removalClass);

// Result of the shared DCE contract. `Changed` means at least one executable
// vertex or Phi was removed, not merely inspected. There is no third member:
// this component has no recoverable error, because malformed internal IR is a
// Debug assertion error at the normal graph verification boundary and Chunk
// exhaustion is process-fatal.
enum class DceResult : uint8_t {
    Unchanged = 0,
    Changed = 1,
};

const char *DceResultName(DceResult result);

// =============================================================================
// DceRemovalPolicy - removal authorization gate
//
// `Classify` defaults to `Required`. `RemovableValue` is granted exactly when
// the vertex carries the positive `VertexPropertyFlag::CAN_BE_REMOVED_IF_DEAD`
// property, which is declared once per opcode in the central `PROPERTIES` of
// `arksteed_opcode.h`. The admitted set is therefore the audited total numeric,
// bitwise, comparison and conversion values plus Phis. Constants, entry
// bindings, reads, calls, writes, barriers, allocations, deopt/throw producers
// and trap-sensitive opcodes do not carry the bit and stay `Required`; an
// invalid, unknown or unproved opcode is `Required` before any concrete cast is
// attempted.
//
// `Classify` never consults `MayHaveSideEffects()` or `CanParticipateInCSE()`;
// those predicates cannot distinguish a total value from a guard, a trap or a
// hidden effect (`I32DivVertex` shares both with `I32AddVertex`). Each positive
// annotation requires a separate proof that removing an unused value cannot be
// observed; the native DCE contract tests cover the admitted and required classes.
// =============================================================================

class DceRemovalPolicy {
public:
    static DceRemovalClass Classify(Vertex *vertex);
};

// =============================================================================
// Compilation-local membership
//
// The DCE closure and the removal selection ask the same question many times per
// enumerated dependency ("is this pointer a member / a registry entry / a
// selected removal / a live vertex?"). Those questions are answered by the
// shared chunk-allocated hash containers in
// `ecmascript/mem/chunk_containers.h` (`ChunkUnorderedSet`), which stores
// pointers only and is therefore usable before any register-allocation metadata
// exists. Expected cost is O(1) per operation with O(size) scratch state,
// which preserves the documented core complexity of O(vertices + enumerated
// dependencies). Iteration order is unspecified; every consumer of these sets
// only asks membership questions or performs a set-independent check per key.
// =============================================================================

// =============================================================================
// DceLiveSet - compilation-local pointer membership of marked (live) vertices.
//
// The closure (or a capability extension) marks every vertex it can still
// reach. The set stores pointers only: no id, counter or persistent reverse-user
// structure is required, so it is usable before any register-allocation
// metadata exists. Storage is chunk-allocated and lives only for the
// compilation. `MarkLive` is idempotent, never makes null a member, and reports
// whether the vertex became live, which lets the closure mark a dependency with
// one membership probe; `IsLive` is the membership query.
// =============================================================================

class DceLiveSet {
public:
    explicit DceLiveSet(Chunk *chunk) : live_(chunk) {}

    bool MarkLive(Vertex *vertex)
    {
        if (vertex == nullptr) {
            return false;
        }
        return live_.insert(vertex).second;
    }

    bool IsLive(const Vertex *vertex) const
    {
        return vertex != nullptr && live_.find(vertex) != live_.end();
    }

    size_t MarkedCount() const
    {
        return live_.size();
    }

    auto begin() const
    {
        return live_.begin();
    }

    auto end() const
    {
        return live_.end();
    }

private:
    ChunkUnorderedSet<const Vertex *> live_;
};

// =============================================================================
// DceDependencies - complete dependency enumeration
//
// The canonical callback shape is `callback(ValueVertex *value)`. `Visit`
// enumerates, deterministically at one graph state and without consulting
// register-allocation metadata:
//
//   1. every ordinary input of the vertex, in input-index order (for a Phi these
//      are the incoming values of its retained incoming edges);
//   2. every eager frame-state value, in stored frame-index order;
//   3. every lazy frame-state value, in stored frame-index order when the
//      optional lazy state is present; an absent state contributes zero visits
//      and no state is manufactured;
//   4. for a throwable vertex whose catch block has Phis, the incoming value at
//      the vertex's catch predecessor index of every currently installed catch
//      Phi, in list order.
//
// Repeated values in different slots produce repeated callbacks. `Visit` neither
// silently deduplicates nor recursively traverses dependencies; caller marking
// deduplicates pointers. It never consults `GetId`, `InputLocation`, result
// constraints or live-range information, is synchronous, non-throwing and
// non-mutating, and leaves the graph and its transient bits unchanged. Repeat
// calls on a stable valid vertex emit identical sequences.
//
// The input domain is the trusted finalized pre-allocation graph: the vertex is
// a concrete vertex of the graph's executable/registry structure, its stored
// slots reference concrete values and its installed catch lists are stable.
// Ordinary internal integrity of that structure is owned by the normal Debug
// `ArkSteedGraphVerifier`, not by this enumeration. A null vertex or an invalid
// reference is a caller programming error with no supported recovery.
//
// The prospective-surviving-Phi view of `DceGraphEditor` and the
// live-Phi-guarded closure view of the ordinary `ArkSteedDce::Run` preparation
// are private shared traversal logic of this component (both are friends); they
// are not public second entries and not extension channels. Both enumerate the
// same direct/eager/lazy slots and the same catch slot rules; they differ only
// in which installed catch Phis contribute an incoming value.
// =============================================================================

namespace dce_internal {
// Internal accessor for the private traversal variants of this component. It is
// not part of the public contract; it exists so the `DceGraphEditor` selection
// assertion and the ordinary `Run` preparation can reuse the same emission logic
// without a public second entry.
class DependencyAccess;
}  // namespace dce_internal

// The removal selection is the pointer-keyed set of executable members the core
// closure selected for removal.
using DceRemovalSet = ChunkUnorderedSet<const Vertex *>;

class DceDependencies {
public:
    template <typename Callback>
    static void Visit(Vertex *vertex, Callback &&callback)
    {
        EmitReferences(vertex, callback, nullptr);
    }

private:
    friend class dce_internal::DependencyAccess;

    // Emits the direct, eager and optional-lazy reference classes of a vertex:
    // every input slot in input-index order, then the eager frame values, then
    // the lazy frame values. The sequence is identical for every view; only the
    // catch channel differs.
    template <typename Callback>
    static void EmitOrdinaryReferences(Vertex *vertex, Callback &callback)
    {
        for (uint32_t index = 0, count = vertex->GetInputCount(); index < count; ++index) {
            callback(vertex->GetInput(index));
        }
        if (EagerDeoptimizableMixin *eager = EagerDeoptimizableMixinOf(vertex); eager != nullptr) {
            for (uint32_t index = 0, count = eager->GetDeoptFrameValueCount(); index < count; ++index) {
                callback(eager->GetDeoptFrameValue(index));
            }
        }
        if (LazyDeoptimizableMixin *lazy = LazyDeoptimizableMixinOf(vertex); lazy != nullptr &&
            lazy->HasLazyDeoptFrameState()) {
            for (uint32_t index = 0, count = lazy->GetDeoptFrameValueCount(); index < count; ++index) {
                callback(lazy->GetDeoptFrameValue(index));
            }
        }
    }

    // Emits the complete reference sequence: the ordinary references plus the
    // incoming values of the installed catch Phis. `prospectiveRemovals` selects
    // the prospective surviving-Phi view of the editor selection assertion; it is
    // null for the public installed-Phi view.
    template <typename Callback>
    static void EmitReferences(Vertex *vertex, Callback &callback,
                               const DceRemovalSet *prospectiveRemovals)
    {
        EmitOrdinaryReferences(vertex, callback);
        BB *catchBlock = CatchBlockOf(vertex);
        if (catchBlock != nullptr && catchBlock->HasPhi()) {
            uint32_t predecessorIndex = StoredCatchPredecessorIndexOf(vertex);
            for (PhiVertex *phi : catchBlock->GetPhis()) {
                if (phi == nullptr) {
                    continue;
                }
                if (prospectiveRemovals != nullptr &&
                    prospectiveRemovals->find(phi) != prospectiveRemovals->end()) {
                    continue;
                }
                callback(phi->GetInput(predecessorIndex));
            }
        }
    }

    // Private closure emission of the ordinary `Run` preparation. The catch
    // channel contributes only a catch Phi that is already live (P7). A live Phi
    // always emits every one of its own inputs when it is processed, so a Phi
    // that becomes live after this vertex was processed loses no dependency,
    // while a dead Phi never makes its exclusive producer live.
    template <typename Callback>
    static void EmitClosureReferences(Vertex *vertex, Callback &callback)
    {
        EmitOrdinaryReferences(vertex, callback);
        BB *catchBlock = CatchBlockOf(vertex);
        if (catchBlock == nullptr || !catchBlock->HasPhi()) {
            return;
        }
        uint32_t predecessorIndex = StoredCatchPredecessorIndexOf(vertex);
        for (PhiVertex *phi : catchBlock->GetPhis()) {
            if (phi == nullptr || !phi->IsDceMarked()) {
                continue;
            }
            callback(phi->GetInput(predecessorIndex));
        }
    }
};

// =============================================================================
// DceGraphEditor - graph filtering authority
//
// `Filter` is the only surface that removes removable values and dead Phis from
// executable block/Phi lists. It filters lists and does nothing else: it does
// not replace surviving identities or values, repair the CFG, remove blocks or
// edges, reorder or change predecessors, alter live Phi inputs or exception
// predecessor indexes, or rebalance loops.
//
// Candidate removal is exactly an unmarked `RemovableValue` entry of a block
// non-control list plus an unmarked dead Phi of a block Phi list. Controls,
// every `Required` operation (including an unmarked value the live set fails to
// mark) and every vertex returned by `Graph::GetParameter(i)` are retained, and
// constant maps are not filtered. The removal proof gate is therefore
// authoritative: an unmarked but `Required` call, store/barrier, allocation,
// guard, deopt/throw producer or trap-sensitive value such as `I32DivVertex` is
// not a candidate and stays in the graph.
//
// Success shortens only `BB::GetVertices()` and `BB::GetPhis()` by stable
// compaction in survivor order. `Changed` means at least one executable entry
// was removed; otherwise `Unchanged`. All surviving pointers, relative order,
// opcode/representation, input count and identity, owner, frame values,
// constant maps, parameter count/index/slot, blocks, control edges,
// predecessors, catch indexes and loop state remain exactly unchanged. Removed
// `Chunk` objects and their own inputs are never individually destroyed or freed.
//
// The caller contract of the live set is enforced before the only list write by
// Debug-only selection assertions, never by a Release branch and never by a
// common graph-integrity route: every supplied live pointer belongs to this
// graph's executable/registry domain, every selected removal is a
// policy-authorized non-control value or Phi, and every surviving direct, eager,
// lazy and surviving catch-Phi dependency is retained under the prospective
// surviving catch-Phi view. These are caller programming obligations with no
// recovery protocol and no post-abort guarantee. The common concrete and graph
// structure invariants of the input graph belong to the normal Debug
// `ArkSteedGraphVerifier`; `Filter` deliberately does not repeat them.
//
// One thread owns each graph; there is no global state, lock, internal retry or
// new timeout. Repeating `Filter` with the same compatible live set returns
// `Unchanged` with equal structure after the first success.
// =============================================================================

class DceGraphEditor {
public:
    static DceResult Filter(Graph *graph, const DceLiveSet &live);
};

// =============================================================================
// ArkSteedDce - root marking, dependency closure and filtering
//
// `Run` is the single graph-local transformation that consumes the three shared
// surfaces above and produces the filtered pre-allocation graph. It is the core
// algorithm of the epic: it collects the roots, drains a monotonic worklist
// through the complete dependency enumeration and commits the resulting removal
// selection through the shared list mutation. One liveness traversal is enough.
//
// Roots are exactly:
//
//   * every control vertex of every retained block (core never removes blocks);
//   * every `Required`-classified non-control value and Phi of a retained block
//     (`DceRemovalPolicy` is authoritative: calls, stores/barriers, allocations,
//     guards, throw/deopt producers, traps, observable reads and safepoints stay);
//   * every graph-registered entry binding (`Graph::GetParameter(i)`).
//
// Constant pools are NOT roots: a constant becomes live only through an actual
// dependency of a live vertex. Unused pooled constants still survive because the
// editor never filters graph-owned registries, so marking them is not required
// and must not be used as a substitute for a real dependency.
//
// Marking is monotonic: a vertex is marked at most once and each of its selected
// dependencies is traversed at most once, so cyclic input graphs terminate and
// the cost is O(vertices + enumerated dependencies) with O(vertices) scratch
// state. State is compilation-local; no persistent reverse-user index, no vertex
// id and no register-allocation metadata is read or created.
//
// `Run` performs one monotonic pointer-worklist closure from those roots,
// emitting already existing slots into the transient `Vertex::DceMarked` bit; the
// catch channel contributes only a live catch Phi, so a dead catch Phi and a
// producer used exclusively by it stay unmarked and are removed together, while
// a live Phi keeps all of its own inputs. The selection is exactly the unmarked
// policy-authorized values and dead Phis, and the single `ApplyRemovals` commit
// performs the only list write.
//
// Filtering removes the unmarked `RemovableValue` values and unmarked Phis of
// the same closure, so newly exposed dead chains disappear without a second
// whole-graph pass. Because marking is forward from roots, a producer that is
// referenced only by removed consumers is unmarked and removed with them. A
// catch Phi is such a consumer: it is live only when a live vertex uses it, and
// a live catch Phi then retains all of its own ordinary inputs. A dead catch Phi
// is therefore removed with the producers that only it referenced, even though
// the throwable naming its catch block stays live.
//
// There is no failure result: the input is a trusted finalized pre-allocation
// graph, Debug verification owns internal integrity, and an immediate repeat
// `Run` on an already filtered graph returns `Unchanged` with equal executable
// structure. Unknown effects are `Required`, which is the correct retention
// behavior and never a reason to refuse an otherwise supported method.
// =============================================================================

class ArkSteedDce {
public:
    static DceResult Run(Graph *graph);
};

}  // namespace panda::ecmascript::arksteed

#endif  // ECMASCRIPT_ARKSTEED_ARKSTEED_DCE_H
