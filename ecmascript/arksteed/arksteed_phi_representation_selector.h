/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef ECMASCRIPT_ARKSTEED_ARKSTEED_PHI_REPRESENTATION_SELECTOR_H
#define ECMASCRIPT_ARKSTEED_ARKSTEED_PHI_REPRESENTATION_SELECTOR_H

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "ecmascript/arksteed/arksteed_graph.h"
#include "ecmascript/arksteed/arksteed_snapshot_table.h"

namespace panda::ecmascript::arksteed {

// Block-local Phi selection and consumer rewriting. The supported widening
// lattice is Int32 -> Float64; Uint32 inputs can widen to
// Float64. HoleyFloat64 and speculative OSR entry conversions are not enabled.
class PhiRepresentationSelector {
public:
    explicit PhiRepresentationSelector(Graph *graph) : graph_(graph), chunk_(graph->GetChunk()) {}
    void Run();

private:
    enum class ProcessPhiResult : uint8_t { NONE, RETRY_ON_CHANGE, CHANGED };
    enum class UntaggingKind : uint8_t {
        CONSTANT,
        CONVERSION,
        UNTAGGED,
        KNOWN_INT32,
        KNOWN_NUMBER,
        HOISTED,
        SELF_BACKEDGE,
        PEELED_BACKEDGE,
    };
    using TagTable = SnapshotTable<ValueVertex *>;
    using TagSnapshot = TagTable::Snapshot;
    using ValueSet = std::unordered_set<ValueVertex *>;

    struct EdgeConversionKey {
        BB *predecessor;
        ValueVertex *input;
        VertexOpcode opcode;

        bool operator==(const EdgeConversionKey &other) const
        {
            return predecessor == other.predecessor && input == other.input && opcode == other.opcode;
        }
    };
    struct EdgeConversionHash {
        size_t operator()(const EdgeConversionKey &key) const
        {
            return std::hash<BB *>{}(key.predecessor) ^ (std::hash<ValueVertex *>{}(key.input) << 1U) ^
                (static_cast<size_t>(key.opcode) << 2U);
        }
    };

    void ComputeDeoptUses();
    void MaterializeDeoptPhiTags(Vertex *vertex);
    void CompactExceptionInputs();
    ValueVertex *NewNumericBoolean(PhiVertex *phi, bool trueIfNonZero);
    NumberTaggingKind GetTaggingKind(PhiVertex *phi) const;
    const UseRepresentationSet &GetRelevantUses(PhiVertex *phi) const;
    void PreProcessPhis(BB *block);
    ProcessPhiResult ProcessPhi(PhiVertex *phi);
    void EnsurePhiInputsTagged(PhiVertex *phi);
    void ProcessBlock(BB *block);
    void ProcessValue(NonControlVertex *vertex);
    void AppendCurrentVertex(NonControlVertex *vertex);
    void FixLoopPhisBackedge(BB *header);
    void PreparePhiTaggings(BB *block);
    ValueVertex *EnsurePhiTagged(PhiVertex *phi, BB *block, std::optional<uint32_t> predecessorIndex = std::nullopt);
    ValueVertex *NewTag(BB *block, ValueVertex *value);
    void AppendToPredecessor(BB *predecessor, NonControlVertex *vertex);
    void UpdateExceptionPhiInputs(Vertex *throwable);
    void UnwrapIdentitiesAndDeoptFrames();
    void ApplyUnconditionalDeopts();
    void RemoveUnreachableBlocksAndEdges();
    void MarkUnconditionalDeopt(NonControlVertex *vertex, kungfu::DeoptType reason);

    bool IsSpeculativePeeledBackedge(PhiVertex *phi, uint32_t inputIndex, ValueVertex *value) const;
    JumpVertex *GetEntryJump(PhiVertex *phi, uint32_t inputIndex) const;
    ValueVertex *GetSpeculativeEntryInput(PhiVertex *phi, uint32_t inputIndex, bool truncating);
    JumpLoopVertex *GetBackedgeJump(PhiVertex *phi, uint32_t inputIndex) const;
    bool IsExactInt32TaggedConstant(const TaggedConstantVertex *constant, int32_t *result = nullptr) const;
    ValueVertex *GetInt32Input(PhiVertex *phi, uint32_t inputIndex, UntaggingKind kind, bool truncating);
    ValueVertex *GetFloat64Input(PhiVertex *phi, uint32_t inputIndex, UntaggingKind kind);
    ValueVertex *GetOrCreateEdgeFloat64(BB *predecessor, ValueVertex *value);
    ValueVertex *GetOrCreateEdgeUint32ToFloat64(BB *predecessor, ValueVertex *value);
    ValueVertex *GetOrCreateEdgeTaggedIntToInt32(BB *predecessor, ValueVertex *value);
    ValueVertex *GetOrCreateEdgeTaggedNumberToFloat64(BB *predecessor, ValueVertex *value);
    ValueVertex *GetOrCreateEdgeInt32Trunc(BB *predecessor, ValueVertex *value);
    template <class VertexT>
    ValueVertex *GetOrCreateEdgeConversion(BB *predecessor, ValueVertex *value);
    ValueVertex *GetOrCreateSpeculativeInt32(PhiVertex *phi, uint32_t inputIndex, ValueVertex *value);
    ValueVertex *GetOrCreateSpeculativeFloat64(PhiVertex *phi, uint32_t inputIndex, ValueVertex *value);

    Graph *graph_;
    Chunk *chunk_;
    BB *currentBlock_ {nullptr};
    std::vector<NonControlVertex *> currentVertices_;
    size_t unchangedVertexCount_ {0};
    bool rebuildCurrentVertices_ {false};
    TagTable phiTaggings_;
    std::vector<const TagSnapshot *> predecessorSnapshots_;
    std::unordered_set<BB *> changedCatchBlocks_;
    std::unordered_map<NonControlVertex *, kungfu::DeoptType> unconditionalDeopts_;
    std::unordered_map<PhiVertex *, TagTable::Key> phiKeys_;
    std::vector<const TagSnapshot *> snapshots_;
    std::unordered_map<BB *, std::vector<NonControlVertex *>> pendingEdgeVertices_;
    std::unordered_map<EdgeConversionKey, ValueVertex *, EdgeConversionHash> edgeConversions_;
};

}  // namespace panda::ecmascript::arksteed

#endif  // ECMASCRIPT_ARKSTEED_ARKSTEED_PHI_REPRESENTATION_SELECTOR_H
