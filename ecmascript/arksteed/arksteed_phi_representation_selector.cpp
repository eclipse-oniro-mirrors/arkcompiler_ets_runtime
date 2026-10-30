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

#include "ecmascript/arksteed/arksteed_phi_representation_selector.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <limits>
#include <utility>

#include "ecmascript/arksteed/arksteed_opcode.h"
#include "ecmascript/js_tagged_value-inl.h"
#include "libpandabase/utils/small_vector.h"

namespace panda::ecmascript::arksteed {

namespace {

constexpr bool IsUntagging(VertexOpcode opcode)
{
    switch (opcode) {
        case VertexOpcode::TaggedIntToI32:
        case VertexOpcode::CheckedTaggedIntToI32:
        case VertexOpcode::CheckedNumberToF64:
        case VertexOpcode::TaggedNumberToF64:
            return true;
        default:
            return false;
    }
}

constexpr bool CanTraceDeoptUse(VertexOpcode opcode)
{
    switch (opcode) {
        case VertexOpcode::Phi:
        case VertexOpcode::TaggedIntToI32:
        case VertexOpcode::CheckedTaggedIntToI32:
        case VertexOpcode::CheckedNumberToF64:
        case VertexOpcode::TaggedNumberToF64:
        case VertexOpcode::I32ToF64:
        case VertexOpcode::U32ToF64:
        case VertexOpcode::I32ToTaggedInt:
        case VertexOpcode::F64ToTaggedDouble:
        case VertexOpcode::U32ToTaggedNumber:
        case VertexOpcode::NumberToTagged:
            return true;
        default:
            return false;
    }
}

bool AcceptsUntaggedPhiInput(const Vertex *vertex)
{
    return vertex->Is<I32ToF64Vertex>() || vertex->Is<U32ToF64Vertex>() || vertex->Is<CheckedF64ToI32Vertex>() ||
           vertex->Is<UnsafeF64ToI32Vertex>() || vertex->Is<DoubleToInt32Vertex>() ||
           vertex->Is<I32ToTaggedIntVertex>() || vertex->Is<CheckedNonNegativeI32ToTaggedIntVertex>() ||
           vertex->Is<F64ToTaggedDoubleVertex>() || vertex->Is<U32ToTaggedNumberVertex>() ||
           vertex->Is<NumberToTaggedVertex>() || vertex->Is<I32ConditionCheckVertex>() ||
           vertex->Is<Float64ToBooleanVertex>();
}

template <class DeoptMixin, class Callback>
void ForEachDeoptFrameValue(DeoptMixin *deopt, Callback callback)
{
    for (uint32_t index = 0; index < deopt->GetDeoptFrameValueCount(); ++index) {
        callback(deopt, index);
    }
}

template <class Callback>
void DispatchDeoptFrameValues(Vertex *vertex, Callback callback)
{
    if (auto *eager = EagerDeoptimizableMixinOf(vertex)) {
        ForEachDeoptFrameValue(eager, callback);
    }
    if (auto *lazy = LazyDeoptimizableMixinOf(vertex); lazy != nullptr && lazy->HasLazyDeoptFrameState()) {
        ForEachDeoptFrameValue(lazy, callback);
    }
}

template <class Callback>
void ForEachGraphVertex(Graph *graph, Callback callback)
{
    for (BB *block : *graph) {
        for (PhiVertex *phi : block->GetPhis()) {
            callback(phi);
        }
        for (NonControlVertex *vertex : block->GetVertices()) {
            callback(vertex);
        }
        if (ControlVertex *control = block->GetControlVertex()) {
            callback(control);
        }
    }
}

EagerDeoptimizableMixin::EagerDeoptFrameState CloneEagerDeoptFrame(
    Chunk *chunk, const EagerDeoptimizableMixin::EagerDeoptFrameState &source)
{
    EagerDeoptimizableMixin::EagerDeoptFrameState copy(chunk);
    copy.reserve(source.size());
    for (const auto &value : source) {
        copy.emplace_back(value.vreg, value.value, value.valueKind);
    }
    return copy;
}

}  // namespace

NumberTaggingKind PhiRepresentationSelector::GetTaggingKind(PhiVertex *phi) const
{
    if (phi->GetType() == NodeType::INT) {
        return NumberTaggingKind::INT32;
    }
    if (phi->GetType() == NodeType::DOUBLE) {
        return NumberTaggingKind::DOUBLE;
    }
    return NumberTaggingKind::NUMBER;
}

void PhiRepresentationSelector::ComputeDeoptUses()
{
    // Deopt inputs are not ordinary graph edges. Preserve their original
    // numeric values after unwrapping conversions and following Phi inputs.
    std::vector<ValueVertex *> worklist;
    ValueSet visited;
    auto enqueue = [&](ValueVertex *value) {
        if (CanTraceDeoptUse(value->GetOpcode()) && visited.insert(value).second) {
            worklist.push_back(value);
        }
    };
    auto record = [&](ValueVertex *root) {
        enqueue(root);
        while (!worklist.empty()) {
            ValueVertex *value = worklist.back();
            worklist.pop_back();
            if (auto *phi = value->TryCast<PhiVertex>()) {
                phi->MaybeRecordUseReprHint(UseRepresentation::NON_TRUNCATED);
                for (uint32_t index = 0; index < phi->GetInputCount(); ++index) {
                    enqueue(phi->GetInput(index));
                }
            } else {
                enqueue(value->GetInput(0));
            }
            // A deopt use of DoubleToInt32 needs its result, not its original input.
        }
    };
    ForEachGraphVertex(graph_, [&](Vertex *vertex) {
        // Return has no Tagged preference, but must retain the original value.
        // Exception Phis likewise carry interpreter values, not truncated ones.
        if (vertex->Is<ReturnVertex>()) {
            record(vertex->GetInput(0));
        } else if (vertex->Is<PhiVertex>() && vertex->GetOwner()->IsExceptionHandler()) {
            record(vertex->Cast<PhiVertex>());
        }
        DispatchDeoptFrameValues(vertex,
                                 [&](auto *deopt, uint32_t index) { record(deopt->GetDeoptFrameValue(index)); });
        // Saved checkpoints supply eager-deopt frames for speculative conversions
        // inserted after representation selection.
        const auto *backedge = vertex->TryCast<JumpLoopVertex>();
        bool needsCheckpoint = (vertex->Is<JumpVertex>() && graph_->IsSpeculativePhiHoistingEnabled()) ||
            (backedge != nullptr && backedge->Target()->HasPeeledIteration());
        if (!needsCheckpoint) {
            return;
        }
        auto *jump = PhiUntaggingCheckpointMixinOf(vertex);
        if (jump == nullptr || !jump->HasPhiUntaggingFrameState()) {
            return;
        }
        for (const auto &value : jump->GetPhiUntaggingFrameState()) {
            record(value.value);
        }
    });
}

JumpVertex *PhiRepresentationSelector::GetEntryJump(PhiVertex *phi, uint32_t inputIndex) const
{
    if (!graph_->IsSpeculativePhiHoistingEnabled()) {
        return nullptr;
    }
    BB *header = static_cast<Vertex *>(phi)->GetOwner();
    if (!header->IsLoopHeader() || inputIndex + 1 == phi->GetInputCount() ||
        inputIndex >= header->PredecessorCount()) {
        return nullptr;
    }
    // GraphBuilder only checkpoints non-resumable loop entries, with one
    // unconditional successor. Branches and uncheckpointed jumps cannot hoist.
    auto *jump = header->GetPredecessor(inputIndex)->GetControlVertex()->TryCast<JumpVertex>();
    return jump != nullptr && jump->Target() == header && jump->HasPhiUntaggingFrameState() ? jump : nullptr;
}

ValueVertex *PhiRepresentationSelector::GetSpeculativeEntryInput(PhiVertex *phi, uint32_t inputIndex, bool truncating)
{
    JumpVertex *jump = GetEntryJump(phi, inputIndex);
    ASSERT(jump != nullptr);
    BB *predecessor = static_cast<Vertex *>(phi)->GetOwner()->GetPredecessor(inputIndex);
    ValueVertex *input = phi->GetInput(inputIndex)->UnwrapIdentities();
    auto frame = CloneEagerDeoptFrame(chunk_, jump->GetPhiUntaggingFrameState());
    for (auto &slot : frame) {
        slot.value = slot.value->UnwrapIdentities();
        if (slot.valueKind == DeoptTranslationKind::TAGGED) {
            if (auto *framePhi = slot.value->TryCast<PhiVertex>(); framePhi != nullptr && !framePhi->IsTagged()) {
                // This predecessor was already processed. Materialize before
                // the new check, using its snapshot rather than the header's.
                slot.value = EnsurePhiTagged(framePhi, predecessor, inputIndex);
            }
        }
    }
    auto *number = Vertex::New<CheckedNumberToF64Vertex>(chunk_, std::array<ValueVertex *, 1> {input},
                                                       chunk_, jump->GetPhiUntaggingBytecodeOffset());
    number->SetOwner(predecessor);
    number->SetEagerDeoptFrameState(CloneEagerDeoptFrame(chunk_, frame));
    AppendToPredecessor(predecessor, number);
    if (phi->IsFloat64()) {
        return number;
    }
    if (truncating) {
        return GetOrCreateEdgeInt32Trunc(predecessor, number);
    }
    auto *integer = Vertex::New<CheckedF64ToI32Vertex>(chunk_, std::array<ValueVertex *, 1> {number},
                                                     chunk_, jump->GetPhiUntaggingBytecodeOffset());
    integer->SetOwner(predecessor);
    integer->SetEagerDeoptFrameState(std::move(frame));
    AppendToPredecessor(predecessor, integer);
    return integer;
}

JumpLoopVertex *PhiRepresentationSelector::GetBackedgeJump(PhiVertex *phi, uint32_t inputIndex) const
{
    BB *header = static_cast<Vertex *>(phi)->GetOwner();
    if (!header->IsLoopHeader() || !header->HasPeeledIteration() || inputIndex + 1 != phi->GetInputCount() ||
        inputIndex >= header->PredecessorCount()) {
        return nullptr;
    }
    BB *predecessor = header->GetPredecessor(inputIndex);
    auto *jump = predecessor->GetControlVertex()->TryCast<JumpLoopVertex>();
    if (jump == nullptr || jump->Target() != header || !jump->HasPhiUntaggingFrameState()) {
        return nullptr;
    }
    return jump;
}

bool PhiRepresentationSelector::IsSpeculativePeeledBackedge(PhiVertex *phi, uint32_t inputIndex,
                                                            ValueVertex *value) const
{
    return value != phi && value->Is<PhiVertex>() && value->IsTagged() && GetBackedgeJump(phi, inputIndex) != nullptr;
}

bool PhiRepresentationSelector::IsExactInt32TaggedConstant(const TaggedConstantVertex *constant, int32_t *result) const
{
    JSTaggedValue tagged(constant->GetValue());
    int32_t value;
    if (tagged.IsInt()) {
        value = tagged.GetInt();
    } else if (tagged.IsDouble()) {
        double number = tagged.GetDouble();
        if (!std::isfinite(number) || number < static_cast<double>(std::numeric_limits<int32_t>::min()) ||
            number > static_cast<double>(std::numeric_limits<int32_t>::max()) ||
            (number == 0.0 && std::signbit(number))) {
            return false;
        }
        value = static_cast<int32_t>(number);
        if (static_cast<double>(value) != number) {
            return false;
        }
    } else {
        return false;
    }
    if (result != nullptr) {
        *result = value;
    }
    return true;
}

template <class VertexT>
ValueVertex *PhiRepresentationSelector::GetOrCreateEdgeConversion(BB *predecessor, ValueVertex *value)
{
    EdgeConversionKey key {predecessor, value, OpcodeOf<VertexT>};
    auto it = edgeConversions_.find(key);
    if (it != edgeConversions_.end()) {
        return it->second;
    }
    auto *conversion = Vertex::New<VertexT>(chunk_, std::array<ValueVertex *, 1> {value});
    conversion->SetOwner(predecessor);
    edgeConversions_.emplace(key, conversion);
    AppendToPredecessor(predecessor, conversion);
    return conversion;
}

ValueVertex *PhiRepresentationSelector::GetOrCreateEdgeFloat64(BB *predecessor, ValueVertex *value)
{
    ASSERT(value->IsInt32());
    return GetOrCreateEdgeConversion<I32ToF64Vertex>(predecessor, value);
}

ValueVertex *PhiRepresentationSelector::GetOrCreateEdgeUint32ToFloat64(BB *predecessor, ValueVertex *value)
{
    ASSERT(value->IsInt32());
    return GetOrCreateEdgeConversion<U32ToF64Vertex>(predecessor, value);
}

ValueVertex *PhiRepresentationSelector::GetOrCreateEdgeTaggedIntToInt32(BB *predecessor, ValueVertex *value)
{
    ASSERT(value->IsTagged());
    return GetOrCreateEdgeConversion<TaggedIntToI32Vertex>(predecessor, value);
}

ValueVertex *PhiRepresentationSelector::GetOrCreateEdgeTaggedNumberToFloat64(BB *predecessor, ValueVertex *value)
{
    ASSERT(value->IsTagged());
    return GetOrCreateEdgeConversion<TaggedNumberToF64Vertex>(predecessor, value);
}

ValueVertex *PhiRepresentationSelector::GetOrCreateEdgeInt32Trunc(BB *predecessor, ValueVertex *value)
{
    ASSERT(value->IsFloat64());
    return GetOrCreateEdgeConversion<DoubleToInt32Vertex>(predecessor, value);
}

ValueVertex *PhiRepresentationSelector::GetOrCreateSpeculativeInt32(PhiVertex *phi, uint32_t inputIndex,
                                                                    ValueVertex *value)
{
    JumpLoopVertex *jump = GetBackedgeJump(phi, inputIndex);
    ASSERT(jump != nullptr);
    BB *predecessor = static_cast<Vertex *>(phi)->GetOwner()->GetPredecessor(inputIndex);
    // A speculative backedge needs an exact conversion even if the forward
    // input was truncated. Preserve INT32 tag guarantees separately.
    if (GetTaggingKind(phi) != NumberTaggingKind::INT32) {
        ValueVertex *number = GetOrCreateSpeculativeFloat64(phi, inputIndex, value);
        auto *checked = Vertex::New<CheckedF64ToI32Vertex>(chunk_,
            std::array<ValueVertex *, 1> {number}, chunk_, jump->GetPhiUntaggingBytecodeOffset());
        checked->SetOwner(predecessor);
        checked->SetEagerDeoptFrameState(CloneEagerDeoptFrame(chunk_, jump->GetPhiUntaggingFrameState()));
        AppendToPredecessor(predecessor, checked);
        return checked;
    }
    auto *conversion = Vertex::New<CheckedTaggedIntToI32Vertex>(chunk_, std::array<ValueVertex *, 1> {value}, chunk_,
                                                                jump->GetPhiUntaggingBytecodeOffset());
    conversion->SetOwner(predecessor);
    conversion->SetEagerDeoptFrameState(CloneEagerDeoptFrame(chunk_, jump->GetPhiUntaggingFrameState()));
    AppendToPredecessor(predecessor, conversion);
    return conversion;
}

ValueVertex *PhiRepresentationSelector::GetOrCreateSpeculativeFloat64(PhiVertex *phi, uint32_t inputIndex,
                                                                      ValueVertex *value)
{
    JumpLoopVertex *jump = GetBackedgeJump(phi, inputIndex);
    ASSERT(jump != nullptr);
    BB *predecessor = static_cast<Vertex *>(phi)->GetOwner()->GetPredecessor(inputIndex);
    auto *conversion = Vertex::New<CheckedNumberToF64Vertex>(chunk_, std::array<ValueVertex *, 1> {value}, chunk_,
                                                             jump->GetPhiUntaggingBytecodeOffset());
    conversion->SetOwner(predecessor);
    conversion->SetEagerDeoptFrameState(CloneEagerDeoptFrame(chunk_, jump->GetPhiUntaggingFrameState()));
    AppendToPredecessor(predecessor, conversion);
    return conversion;
}

ValueVertex *PhiRepresentationSelector::GetInt32Input(PhiVertex *phi, uint32_t inputIndex,
                                                      UntaggingKind kind, bool truncating)
{
    ValueVertex *value = phi->GetInput(inputIndex)->UnwrapIdentities();
    BB *predecessor = static_cast<Vertex *>(phi)->GetOwner()->GetPredecessor(inputIndex);
    if (value->IsInt32()) {
        return value;
    }
    if (value->IsFloat64()) {
        ASSERT(truncating);
        return GetOrCreateEdgeInt32Trunc(predecessor, value);
    }
    if (auto *constant = value->TryCast<TaggedConstantVertex>()) {
        int32_t intValue;
        bool isExactInt32 = IsExactInt32TaggedConstant(constant, &intValue);
        ASSERT(isExactInt32);
        return graph_->GetInt32Constant(intValue);
    }
    if (kind == UntaggingKind::CONVERSION) {
        ASSERT(value->IsConversion() && value->IsTagged() && value->GetInputCount() == 1);
        ValueVertex *input = value->GetInput(0)->UnwrapIdentities();
        ASSERT(!value->Is<U32ToTaggedNumberVertex>() || truncating);
        if (input->IsInt32()) {
            return input;
        }
        ASSERT(input->IsFloat64());
        ASSERT(truncating || (value->Is<NumberToTaggedVertex>() &&
                              value->Cast<NumberToTaggedVertex>()->GetKind() == NumberTaggingKind::INT32));
        return GetOrCreateEdgeInt32Trunc(predecessor, input);
    }
    auto *inputPhi = value->TryCast<PhiVertex>();
    if (inputPhi != nullptr && inputPhi->IsInt32()) {
        return inputPhi;
    }
    if (inputPhi != nullptr && truncating && inputPhi->IsFloat64()) {
        return GetOrCreateEdgeInt32Trunc(predecessor, inputPhi);
    }
    if (kind == UntaggingKind::PEELED_BACKEDGE) {
        return GetOrCreateSpeculativeInt32(phi, inputIndex, value);
    }
    if (kind == UntaggingKind::KNOWN_INT32) {
        return GetOrCreateEdgeTaggedIntToInt32(predecessor, value);
    }
    if (kind == UntaggingKind::KNOWN_NUMBER) {
        ASSERT(truncating);
        ValueVertex *number = GetOrCreateEdgeTaggedNumberToFloat64(predecessor, value);
        return GetOrCreateEdgeInt32Trunc(predecessor, number);
    }
    if (kind == UntaggingKind::HOISTED) {
        return GetSpeculativeEntryInput(phi, inputIndex, truncating);
    }
    ASSERT(inputPhi != nullptr);
    ASSERT(IsSpeculativePeeledBackedge(phi, inputIndex, value));
    return GetOrCreateSpeculativeInt32(phi, inputIndex, value);
}

ValueVertex *PhiRepresentationSelector::GetFloat64Input(PhiVertex *phi, uint32_t inputIndex, UntaggingKind kind)
{
    ValueVertex *value = phi->GetInput(inputIndex)->UnwrapIdentities();
    BB *predecessor = static_cast<Vertex *>(phi)->GetOwner()->GetPredecessor(inputIndex);
    if (value->IsFloat64()) {
        return value;
    }
    if (value->IsInt32()) {
        return GetOrCreateEdgeFloat64(predecessor, value);
    }
    if (auto *constant = value->TryCast<TaggedConstantVertex>()) {
        JSTaggedValue tagged(constant->GetValue());
        ASSERT(tagged.IsNumber());
        double number = tagged.IsInt() ? static_cast<double>(tagged.GetInt()) : tagged.GetDouble();
        return graph_->GetFloat64Constant(number);
    }
    if (kind == UntaggingKind::CONVERSION) {
        ASSERT(value->IsConversion() && value->IsTagged() && value->GetInputCount() == 1);
        ValueVertex *input = value->GetInput(0)->UnwrapIdentities();
        if (value->Is<U32ToTaggedNumberVertex>()) {
            ASSERT(input->IsInt32());
            return GetOrCreateEdgeUint32ToFloat64(predecessor, input);
        }
        if (input->IsInt32()) {
            return GetOrCreateEdgeFloat64(predecessor, input);
        }
        ASSERT(input->IsFloat64());
        if (auto *tag = value->TryCast<NumberToTaggedVertex>();
            tag != nullptr && tag->GetKind() == NumberTaggingKind::INT32) {
            return GetOrCreateEdgeFloat64(predecessor, GetOrCreateEdgeInt32Trunc(predecessor, input));
        }
        return input;
    }
    auto *inputPhi = value->TryCast<PhiVertex>();
    if (inputPhi != nullptr && inputPhi->IsFloat64()) {
        return inputPhi;
    }
    if (inputPhi != nullptr && inputPhi->IsInt32()) {
        return GetOrCreateEdgeFloat64(predecessor, inputPhi);
    }
    if (kind == UntaggingKind::PEELED_BACKEDGE) {
        return GetOrCreateSpeculativeFloat64(phi, inputIndex, value);
    }
    if (kind == UntaggingKind::KNOWN_INT32) {
        ValueVertex *integer = GetOrCreateEdgeTaggedIntToInt32(predecessor, value);
        return GetOrCreateEdgeFloat64(predecessor, integer);
    }
    if (kind == UntaggingKind::KNOWN_NUMBER) {
        return GetOrCreateEdgeTaggedNumberToFloat64(predecessor, value);
    }
    if (kind == UntaggingKind::HOISTED) {
        return GetSpeculativeEntryInput(phi, inputIndex, false);
    }
    ASSERT(inputPhi != nullptr);
    ASSERT(IsSpeculativePeeledBackedge(phi, inputIndex, value));
    return GetOrCreateSpeculativeFloat64(phi, inputIndex, value);
}

void PhiRepresentationSelector::Run()
{
    if (!graph_->IsPhiUntaggingEnabled()) {
        return;
    }
    snapshots_.resize(graph_->MaxBlockId(), nullptr);
    ComputeDeoptUses();
    for (BB *block : *graph_) {
        currentBlock_ = block;
        PreparePhiTaggings(block);
        PreProcessPhis(block);
        ProcessBlock(block);
        snapshots_[block->GetId()] = phiTaggings_.Seal();
    }
    ApplyUnconditionalDeopts();
    CompactExceptionInputs();
    UnwrapIdentitiesAndDeoptFrames();
}

const UseRepresentationSet &PhiRepresentationSelector::GetRelevantUses(PhiVertex *phi) const
{
    const auto &sameLoop = phi->GetSameLoopUseReprHints();
    if (!static_cast<Vertex *>(phi)->GetOwner()->IsLoopHeader() || sameLoop.Empty() ||
        sameLoop.ContainsOnlyTruncatedInt32()) {
        return phi->GetUseReprHints();
    }
    return sameLoop;
}

void PhiRepresentationSelector::PreProcessPhis(BB *block)
{
    const auto &phis = block->GetPhis();
    size_t firstRetry = 0;
    size_t endRetry = 0;
    bool changed = false;
    for (size_t i = 0; i < phis.size(); ++i) {
        ProcessPhiResult result = ProcessPhi(phis[i]);
        changed |= result == ProcessPhiResult::CHANGED;
        if (result == ProcessPhiResult::RETRY_ON_CHANGE) {
            if (firstRetry == endRetry) {
                firstRetry = i;
            }
            endRetry = i + 1;
        }
    }
    // Retry the affected block-local interval once if another Phi changed.
    if (changed) {
        for (size_t i = firstRetry; i < endRetry; ++i) {
            ProcessPhi(phis[i]);
        }
    }
}

PhiRepresentationSelector::ProcessPhiResult PhiRepresentationSelector::ProcessPhi(PhiVertex *phi)
{
    BB *owner = static_cast<Vertex *>(phi)->GetOwner();
    if (!phi->IsTagged() || owner->IsExceptionHandler()) {
        return ProcessPhiResult::NONE;
    }
    // Cache-merge Phis have no use hints; they must remain Tagged.
    if (phi->GetUseReprHints().Empty()) {
        EnsurePhiInputsTagged(phi);
        return ProcessPhiResult::NONE;
    }
    constexpr uint8_t int32Bit = 1;
    constexpr uint8_t float64Bit = 2;
    constexpr uint8_t uint32Bit = 4;
    constexpr size_t inlinePhiInputCount = 8;
    uint8_t inputs = 0;
    bool taggedPhiInput = false;
    bool unsupported = false;
    bool floatConstant = false;
    SmallVector<UntaggingKind, inlinePhiInputCount> untaggingKinds;
    untaggingKinds.resize(phi->GetInputCount());
    for (uint32_t i = 0; i < phi->GetInputCount(); ++i) {
        ValueVertex *input = phi->GetInput(i)->UnwrapIdentities();
        phi->SetInput(i, input);
        if (auto *constant = input->TryCast<TaggedConstantVertex>()) {
            untaggingKinds[i] = UntaggingKind::CONSTANT;
            JSTaggedValue tagged(constant->GetValue());
            if (tagged.IsInt()) {
                inputs |= int32Bit;
            } else if (tagged.IsDouble()) {
                inputs |= float64Bit;
                floatConstant = true;
            } else {
                unsupported = true;
                break;
            }
        } else if (input->Is<Float64ConstantVertex>()) {
            untaggingKinds[i] = UntaggingKind::CONSTANT;
            inputs |= float64Bit;
            floatConstant = true;
        } else if (input->IsConversion() && input->IsTagged()) {
            ASSERT(input->GetInputCount() == 1);
            ValueVertex *raw = input->GetInput(0)->UnwrapIdentities();
            if (!raw->IsInt32() && !raw->IsFloat64()) {
                unsupported = true;
                break;
            }
            untaggingKinds[i] = UntaggingKind::CONVERSION;
            if (input->Is<U32ToTaggedNumberVertex>()) {
                // This opcode interprets its Int32 input bits as unsigned.
                inputs |= uint32Bit;
            } else if (auto *tag = input->TryCast<NumberToTaggedVertex>();
                       tag != nullptr && tag->GetKind() == NumberTaggingKind::INT32) {
                // The tagged result already includes truncation, even for a Float64 input.
                inputs |= int32Bit;
            } else {
                inputs |= raw->IsInt32() ? int32Bit : float64Bit;
            }
        } else if (input->IsInt32()) {
            untaggingKinds[i] = UntaggingKind::UNTAGGED;
            inputs |= int32Bit;
        } else if (input->IsFloat64()) {
            untaggingKinds[i] = UntaggingKind::UNTAGGED;
            inputs |= float64Bit;
        } else if (owner->IsLoopHeader() && i + 1 == phi->GetInputCount() && input == phi) {
            untaggingKinds[i] = UntaggingKind::SELF_BACKEDGE;
            break;
        } else if (IsSpeculativePeeledBackedge(phi, i, input)) {
            untaggingKinds[i] = UntaggingKind::PEELED_BACKEDGE;
            break;
        } else {
            ASSERT(input->IsTagged());
            NodeType type = input->GetStaticType();
            if (type == NodeType::INT) {
                untaggingKinds[i] = UntaggingKind::KNOWN_INT32;
                inputs |= int32Bit;
                continue;
            }
            if (type == NodeType::NUMBER || type == NodeType::DOUBLE) {
                untaggingKinds[i] = UntaggingKind::KNOWN_NUMBER;
                inputs |= float64Bit;
                continue;
            }
            if (GetEntryJump(phi, i) != nullptr) {
                untaggingKinds[i] = UntaggingKind::HOISTED;
                // The unknown entry contributes no representation, just a
                // checked conversion; infer the representation from other inputs.
                continue;
            }
            taggedPhiInput = input->Is<PhiVertex>();
            unsupported = true;
            break;
        }
    }
    auto unchanged = [&]() {
        EnsurePhiInputsTagged(phi);
        return taggedPhiInput ? ProcessPhiResult::RETRY_ON_CHANGE : ProcessPhiResult::NONE;
    };
    if (unsupported || inputs == 0) {
        return unchanged();
    }
    UseRepresentationSet uses = GetRelevantUses(phi);
    bool ignoredTaggedUse = (inputs & float64Bit) == 0;
    if (ignoredTaggedUse) {
        uses.Remove(UseRepresentation::TAGGED);
    }
    if (uses.Empty() || uses.Contains(UseRepresentation::TAGGED) || uses.Contains(UseRepresentation::UINT32) ||
        uses.Contains(UseRepresentation::HOLEY_FLOAT64)) {
        return unchanged();
    }
    // Inputs widen upwards; uses restrict the maximum representation.
    // Prefer Int32 when an integer use benefits from it, then consider Float64.
    uint8_t possible = (inputs & (float64Bit | uint32Bit)) != 0 ? float64Bit : int32Bit | float64Bit;
    uint8_t allowed = uses.Contains(UseRepresentation::TAGGED_FOR_NUMBER_TO_STRING) ? int32Bit | float64Bit
        : uses.Contains(UseRepresentation::INT32)                                   ? int32Bit
                                                                                    : int32Bit | float64Bit;
    uint8_t intersection = possible & allowed;
    bool truncate = graph_->IsTruncatedInt32PhiEnabled() && !ignoredTaggedUse && !floatConstant &&
                    uses.ContainsOnlyTruncatedInt32() &&
                    !phi->GetUseReprHints().Contains(UseRepresentation::NON_TRUNCATED);
    ValueRepresentation representation;
    if ((intersection & int32Bit) != 0 &&
        (uses.Contains(UseRepresentation::INT32) || uses.Contains(UseRepresentation::TAGGED_FOR_NUMBER_TO_STRING) ||
            uses.Contains(UseRepresentation::TRUNCATED_INT32))) {
        representation = ValueRepresentation::INT32;
        truncate = false;
    } else if (truncate) {
        representation = ValueRepresentation::INT32;
    } else if ((intersection & float64Bit) != 0) {
        representation = ValueRepresentation::FLOAT64;
    } else {
        return unchanged();
    }
    phi->SetValueRepresentation(representation);
    for (uint32_t i = 0; i < phi->GetInputCount(); ++i) {
        phi->SetInput(i, representation == ValueRepresentation::INT32
            ? GetInt32Input(phi, i, untaggingKinds[i], truncate) : GetFloat64Input(phi, i, untaggingKinds[i]));
    }
    return ProcessPhiResult::CHANGED;
}

void PhiRepresentationSelector::AppendToPredecessor(BB *predecessor, NonControlVertex *vertex)
{
    if (snapshots_[predecessor->GetId()] != nullptr) {
        predecessor->AddVertex(vertex);
    } else {
        // Backedge blocks are still ahead of us. Process these conversions at
        // their end, after the body Phis have acquired their representations.
        pendingEdgeVertices_[predecessor].push_back(vertex);
    }
}

void PhiRepresentationSelector::EnsurePhiInputsTagged(PhiVertex *phi)
{
    BB *owner = static_cast<Vertex *>(phi)->GetOwner();
    for (uint32_t i = 0; i < phi->GetInputCount(); ++i) {
        if (owner->IsLoopHeader() && i + 1 == phi->GetInputCount()) {
            continue;  // Repaired at JumpLoop, after the backedge was visited.
        }
        ValueVertex *input = phi->GetInput(i)->UnwrapIdentities();
        if (!input->IsTagged()) {
            input = EnsurePhiTagged(input->Cast<PhiVertex>(), owner->GetPredecessor(i), i);
        }
        phi->SetInput(i, input);
    }
}

ValueVertex *PhiRepresentationSelector::NewTag(BB *block, ValueVertex *value)
{
    NumberTaggingKind kind = value->IsInt32() ? NumberTaggingKind::INT32 : NumberTaggingKind::DOUBLE;
    if (auto *phi = value->TryCast<PhiVertex>()) {
        kind = GetTaggingKind(phi);
    }
    ValueVertex *tag;
    if (value->IsInt32() && kind != NumberTaggingKind::DOUBLE) {
        tag = Vertex::New<I32ToTaggedIntVertex>(chunk_, std::array<ValueVertex *, 1> {value});
    } else if (value->IsFloat64() && kind == NumberTaggingKind::DOUBLE) {
        tag = Vertex::New<F64ToTaggedDoubleVertex>(chunk_, std::array<ValueVertex *, 1> {value});
    } else {
        tag = Vertex::New<NumberToTaggedVertex>(chunk_, std::array<ValueVertex *, 1> {value}, kind);
    }
    tag->SetOwner(block);
    return tag;
}

ValueVertex *PhiRepresentationSelector::EnsurePhiTagged(
    PhiVertex *phi, BB *block, std::optional<uint32_t> predecessorIndex)
{
    if (phi->IsTagged()) {
        return phi;
    }
    auto it = phiKeys_.find(phi);
    if (it != phiKeys_.end()) {
        ValueVertex *existing = predecessorIndex.has_value()
            ? phiTaggings_.GetPredecessorValue(it->second, *predecessorIndex) : phiTaggings_.Get(it->second);
        if (existing != nullptr) {
            return existing;
        }
    }
    ValueVertex *tag = NewTag(block, phi);
    if (!predecessorIndex.has_value()) {
        AppendCurrentVertex(tag);
        if (it == phiKeys_.end()) {
            it = phiKeys_.emplace(phi, phiTaggings_.NewKey()).first;
        }
        phiTaggings_.Set(it->second, tag);
    } else {
        // A conversion added to a predecessor must not become available on
        // other incoming paths or change the current merge snapshot.
        AppendToPredecessor(block, tag);
    }
    return tag;
}

void PhiRepresentationSelector::PreparePhiTaggings(BB *block)
{
    predecessorSnapshots_.clear();
    uint32_t count = block->PredecessorCount() - (block->IsLoopHeader() ? 1U : 0U);
    for (uint32_t i = 0; i < count; ++i) {
        const TagSnapshot *snapshot = snapshots_[block->GetPredecessor(i)->GetId()];
        if (snapshot == nullptr) {
            predecessorSnapshots_.clear();  // Exception entry has no normal CFG predecessor snapshot.
            break;
        }
        predecessorSnapshots_.push_back(snapshot);
    }
    phiTaggings_.StartNewSnapshot(predecessorSnapshots_, [&](TagTable::Key, const std::vector<ValueVertex *> &values) {
        for (ValueVertex *value : values) {
            if (value == nullptr) {
                return static_cast<ValueVertex *>(nullptr);
            }
        }
        constexpr auto noOwner = std::numeric_limits<VRegIDType>::max();
        auto *phi = Vertex::New<PhiVertex>(chunk_, block->PredecessorCount(), VirtualRegister(noOwner));
        phi->SetOwner(block);
        for (uint32_t i = 0; i < values.size(); ++i) {
            phi->SetInput(i, values[i]);
        }
        if (block->IsLoopHeader()) {
            phi->SetInput(block->PredecessorCount() - 1, phi);
        }
        block->AddPhiVertex(phi);
        return static_cast<ValueVertex *>(phi);
    });
}

void PhiRepresentationSelector::UpdateExceptionPhiInputs(Vertex *throwable)
{
    BB *handler = CatchBlockOf(throwable);
    if (handler == nullptr) {
        return;
    }
    uint32_t index = CatchPredecessorIndexOf(throwable);
    for (PhiVertex *phi : handler->GetPhis()) {
        ASSERT(index < phi->GetInputCount());
        ValueVertex *input = phi->GetInput(index)->UnwrapIdentities();
        if (!input->IsTagged()) {
            if (auto *inputPhi = input->TryCast<PhiVertex>()) {
                input = EnsurePhiTagged(inputPhi, currentBlock_);
            } else {
                input = NewTag(currentBlock_, input);
                AppendCurrentVertex(input);
            }
        }
        phi->SetInput(index, input);
    }
}

void PhiRepresentationSelector::MaterializeDeoptPhiTags(Vertex *vertex)
{
    DispatchDeoptFrameValues(vertex, [&](auto *deopt, uint32_t i) {
        // An explicit numeric recovery describes a converted value, not the
        // original tagged Phi. Preserve that conversion's recovery contract.
        if (deopt->GetDeoptValueKind(i) != DeoptTranslationKind::TAGGED) {
            return;
        }
        auto *phi = deopt->GetDeoptFrameValue(i)->UnwrapIdentities()->template TryCast<PhiVertex>();
        if (phi == nullptr || phi->IsTagged()) {
            return;
        }
        auto kind = GetTaggingKind(phi);
        if ((phi->IsFloat64() && kind != NumberTaggingKind::DOUBLE) ||
            (phi->IsInt32() && kind == NumberTaggingKind::DOUBLE)) {
            deopt->SetDeoptFrameValue(i, EnsurePhiTagged(phi, currentBlock_), DeoptTranslationKind::TAGGED);
        }
    });
}

ValueVertex *PhiRepresentationSelector::NewNumericBoolean(PhiVertex *phi, bool trueIfNonZero)
{
    ValueVertex *result;
    if (phi->IsInt32()) {
        Condition condition = trueIfNonZero ? Condition::NOT_EQUAL : Condition::EQUAL;
        result = Vertex::New<I32ConditionCheckVertex>(chunk_,
            std::array<ValueVertex *, I32ConditionCheckVertex::NUM_INPUTS>{phi, graph_->GetInt32Constant(0)},
            condition);
    } else {
        result = Vertex::New<Float64ToBooleanVertex>(chunk_, std::array<ValueVertex *, 1>{phi}, trueIfNonZero);
    }
    result->SetOwner(currentBlock_);
    AppendCurrentVertex(result);
    return result;
}

// Removing a pure ToBoolean call removes one exception predecessor. Compact
// the catch inputs and every remaining throwable index together, after traversal.
void PhiRepresentationSelector::CompactExceptionInputs()
{
    if (changedCatchBlocks_.empty()) {
        return;
    }
    // Collect surviving exception predecessors once, retaining their old Phi
    // input indices until every input in the corresponding handler is moved.
    std::vector<std::pair<uint32_t, ThrowableMixin *>> remaining;
    ForEachGraphVertex(graph_, [&](Vertex *vertex) {
        auto *throwable = ThrowableMixinOf(vertex);
        if (throwable != nullptr && changedCatchBlocks_.count(throwable->GetCatchBlock()) != 0) {
            remaining.emplace_back(throwable->GetCatchPredecessorIndex(), throwable);
        }
    });
    std::sort(remaining.begin(), remaining.end(), [](const auto &a, const auto &b) {
        BB *left = a.second->GetCatchBlock();
        BB *right = b.second->GetCatchBlock();
        return left == right ? a.first < b.first : std::less<BB *>{}(left, right);
    });
    for (auto begin = remaining.begin(); begin != remaining.end();) {
        BB *handler = begin->second->GetCatchBlock();
        auto end = std::find_if(begin, remaining.end(),
                                [handler](const auto &input) { return input.second->GetCatchBlock() != handler; });
        uint32_t count = static_cast<uint32_t>(end - begin);
        for (PhiVertex *phi : handler->GetPhis()) {
            for (uint32_t i = 0; i < count; ++i) {
                phi->SetInput(i, phi->GetInput((begin + i)->first));
            }
            phi->ResizeExceptionInputs(count);
        }
        for (uint32_t i = 0; i < count; ++i) {
            (begin + i)->second->LoadCatchBlock(handler, i);
        }
        changedCatchBlocks_.erase(handler);
        begin = end;
    }
    for (BB *handler : changedCatchBlocks_) {
        for (PhiVertex *phi : handler->GetPhis()) {
            phi->OverwriteWithIdentity(graph_->GetTaggedConstant(JSTaggedValue::VALUE_UNDEFINED));
        }
        handler->GetPhis().clear();
    }
    changedCatchBlocks_.clear();
}

void PhiRepresentationSelector::ProcessValue(NonControlVertex *vertex)
{
    if (vertex->Is<IdentityVertex>()) {
        return;
    }
    UpdateExceptionPhiInputs(vertex);
    MaterializeDeoptPhiTags(vertex);
    if (IsUntagging(vertex->GetOpcode())) {
        ASSERT(vertex->GetInputCount() == 1);
        ValueVertex *input = vertex->GetInput(0)->UnwrapIdentities();
        vertex->SetInput(0, input);
        if (auto *phi = input->TryCast<PhiVertex>(); phi != nullptr && !phi->IsTagged()) {
            ASSERT(phi->IsInt32() || phi->IsFloat64());
            if (vertex->Is<CheckedTaggedIntToI32Vertex>() && GetTaggingKind(phi) == NumberTaggingKind::DOUBLE) {
                MarkUnconditionalDeopt(vertex, kungfu::DeoptType::NOTINT1);
                vertex->SetInput(0, EnsurePhiTagged(phi, currentBlock_));
                AppendCurrentVertex(vertex);
                return;
            }
            ValueVertex *replacement = input;
            ValueRepresentation outputRepresentation = vertex->GetValueRepresentation();
            if (outputRepresentation == ValueRepresentation::FLOAT64 && input->IsInt32()) {
                replacement = Vertex::New<I32ToF64Vertex>(chunk_, std::array<ValueVertex *, 1>{input});
                replacement->SetOwner(currentBlock_);
                AppendCurrentVertex(replacement);
            } else if (vertex->Is<TaggedIntToI32Vertex>() && input->IsFloat64()) {
                replacement = Vertex::New<UnsafeF64ToI32Vertex>(chunk_, std::array<ValueVertex *, 1>{input});
                replacement->SetOwner(currentBlock_);
                AppendCurrentVertex(replacement);
            } else if (outputRepresentation == ValueRepresentation::INT32 && input->IsFloat64()) {
                auto *old = vertex->Cast<CheckedTaggedIntToI32Vertex>();
                auto *checked = Vertex::New<CheckedF64ToI32Vertex>(chunk_, std::array<ValueVertex *, 1>{input}, chunk_,
                                                                   old->GetBytecodeOffset());
                checked->SetEagerDeoptFrameState(CloneEagerDeoptFrame(chunk_, old->GetEagerDeoptFrameState()));
                checked->SetOwner(currentBlock_);
                AppendCurrentVertex(checked);
                replacement = checked;
            }
            vertex->Cast<ValueVertex>()->OverwriteWithIdentity(replacement);
            return;
        }
        AppendCurrentVertex(vertex);
        return;
    }
    constexpr uint32_t toBooleanInputCount = 1;  // Glue is supplied by the root register.
    if (auto *call = vertex->TryCast<CallCommonStubVertex>(); call != nullptr &&
        call->GetInputCount() == toBooleanInputCount &&
        (call->GetCommonStubID() == CommonStubID::ToBooleanTrue ||
            call->GetCommonStubID() == CommonStubID::ToBooleanFalse)) {
        auto id = call->GetCommonStubID();
        auto *phi = call->GetInput(0)->UnwrapIdentities()->TryCast<PhiVertex>();
        if (phi != nullptr && !phi->IsTagged()) {
            if (BB *handler = CatchBlockOf(call)) {
                changedCatchBlocks_.insert(handler);
            }
            call->OverwriteWithIdentity(NewNumericBoolean(phi, id == CommonStubID::ToBooleanTrue));
            return;
        }
    }
    if (vertex->Is<NumberToStringVertex>()) {
        vertex->SetInput(0, vertex->GetInput(0)->UnwrapIdentities());
        AppendCurrentVertex(vertex);  // Accepts Tagged, Int32 or Float64 directly.
        return;
    }
    // Ark expresses a truncating tagged-number use as two nodes. Fold the
    // resulting Int32 -> Float64 -> ToInt32 chain after resolving identities.
    if (vertex->Is<DoubleToInt32Vertex>() || vertex->Is<UnsafeF64ToI32Vertex>()) {
        ValueVertex *input = vertex->GetInput(0)->UnwrapIdentities();
        if (input->Is<I32ToF64Vertex>()) {
            vertex->Cast<ValueVertex>()->OverwriteWithIdentity(input->GetInput(0));
            return;
        }
    }
    if (vertex->Is<TaggedNumberToBooleanVertex>() || vertex->Is<TaggedDoubleToBooleanVertex>()) {
        auto *phi = vertex->GetInput(0)->UnwrapIdentities()->TryCast<PhiVertex>();
        bool doubleOnly = vertex->Is<TaggedDoubleToBooleanVertex>();
        bool trueIfNonZero = doubleOnly ? vertex->Cast<TaggedDoubleToBooleanVertex>()->IsTrueIfNonZero()
                                      : vertex->Cast<TaggedNumberToBooleanVertex>()->IsTrueIfNonZero();
        if (phi != nullptr && !phi->IsTagged() && doubleOnly &&
            GetTaggingKind(phi) == NumberTaggingKind::INT32) {
            MarkUnconditionalDeopt(vertex, kungfu::DeoptType::NOTDOUBLE1);
        }
        if (phi != nullptr && !phi->IsTagged() && (!doubleOnly || GetTaggingKind(phi) == NumberTaggingKind::DOUBLE)) {
            ValueVertex *replacement = NewNumericBoolean(phi, trueIfNonZero);
            vertex->Cast<ValueVertex>()->OverwriteWithIdentity(replacement);
            return;
        }
    }
    if (vertex->Is<DeoptIfNotNumberVertex>() && !vertex->GetInput(0)->UnwrapIdentities()->IsTagged()) {
        return;
    }
    for (uint32_t i = 0; i < vertex->GetInputCount(); ++i) {
        ValueVertex *input = vertex->GetInput(i);
        if (input->Is<IdentityVertex>()) {
            vertex->SetInput(i, input->UnwrapIdentities());
        } else if (auto *phi = input->TryCast<PhiVertex>();
                   phi != nullptr && !phi->IsTagged() && !AcceptsUntaggedPhiInput(vertex)) {
            vertex->SetInput(i, EnsurePhiTagged(phi, currentBlock_));
        }
    }
    AppendCurrentVertex(vertex);
}

void PhiRepresentationSelector::AppendCurrentVertex(NonControlVertex *vertex)
{
    const auto &original = currentBlock_->GetVertices();
    if (!rebuildCurrentVertices_) {
        if (unchangedVertexCount_ < original.size() && original[unchangedVertexCount_] == vertex) {
            ++unchangedVertexCount_;
            return;
        }
        // Copy the retained prefix only when the block's node sequence changes.
        currentVertices_.assign(original.begin(), original.begin() + unchangedVertexCount_);
        rebuildCurrentVertices_ = true;
    }
    currentVertices_.push_back(vertex);
}

void PhiRepresentationSelector::ProcessBlock(BB *block)
{
    currentVertices_.clear();
    unchangedVertexCount_ = 0;
    rebuildCurrentVertices_ = false;
    for (NonControlVertex *vertex : block->GetVertices()) {
        ProcessValue(vertex);
    }
    auto pending = pendingEdgeVertices_.find(block);
    if (pending != pendingEdgeVertices_.end()) {
        for (NonControlVertex *vertex : pending->second) {
            ProcessValue(vertex);
        }
        pendingEdgeVertices_.erase(pending);
    }
    ControlVertex *control = block->GetControlVertex();
    if (control != nullptr) {
        UpdateExceptionPhiInputs(control);
        MaterializeDeoptPhiTags(control);
        for (uint32_t i = 0; i < control->GetInputCount(); ++i) {
            ValueVertex *input = control->GetInput(i);
            bool directPhi = input->Is<PhiVertex>() && !input->IsTagged();
            input = input->UnwrapIdentities();
            if (directPhi) {
                input = EnsurePhiTagged(input->Cast<PhiVertex>(), block);
            }
            control->SetInput(i, input);
        }
        if (auto *jump = control->TryCast<JumpLoopVertex>()) {
            FixLoopPhisBackedge(jump->Target());
        }
    }
    if (rebuildCurrentVertices_) {
        block->ReplaceVertices(currentVertices_);
    } else if (unchangedVertexCount_ != block->GetVertexCount()) {
        block->GetVertices().resize(unchangedVertexCount_);
    }
}

void PhiRepresentationSelector::FixLoopPhisBackedge(BB *header)
{
    for (PhiVertex *phi : header->GetPhis()) {
        uint32_t last = phi->GetInputCount() - 1;
        ValueVertex *input = phi->GetInput(last)->UnwrapIdentities();
        if (phi->IsTagged() && !input->IsTagged()) {
            input = EnsurePhiTagged(input->Cast<PhiVertex>(), currentBlock_);
        }
        phi->SetInput(last, input);
    }
}

void PhiRepresentationSelector::UnwrapIdentitiesAndDeoptFrames()
{
    // The common cleanup also reaches earlier loop headers and conversions
    // inserted into already-visited predecessors. No replacement maps are used.
    ForEachGraphVertex(graph_, [&](Vertex *vertex) {
        for (uint32_t i = 0; i < vertex->GetInputCount(); ++i) {
            vertex->SetInput(i, vertex->GetInput(i)->UnwrapIdentities());
        }
        DispatchDeoptFrameValues(vertex, [&](auto *deopt, uint32_t i) {
            ValueVertex *value = deopt->GetDeoptFrameValue(i)->UnwrapIdentities();
            // Uint32 recovery has no Ark translation kind; retain its tagging.
            while (value->Is<I32ToTaggedIntVertex>() || value->Is<F64ToTaggedDoubleVertex>()) {
                ValueVertex *input = value->GetInput(0)->UnwrapIdentities();
                // ActualArgc is Int64 despite using I32ToTaggedInt. Retain
                // its tagging: an Int64 value has no inferred recovery kind.
                if (!input->IsInt32() && !input->IsFloat64()) {
                    break;
                }
                value = input;
            }
            DeoptTranslationKind kind = deopt->GetDeoptValueKind(i);
            if (kind != DeoptTranslationKind::RAW_INT32) {
                if (value->IsInt32()) {
                    kind = DeoptTranslationKind::INT32_TO_TAGGED;
                } else if (value->IsFloat64()) {
                    kind = DeoptTranslationKind::FLOAT64_TO_TAGGED_DOUBLE;
                }
            }
            deopt->SetDeoptFrameValue(i, value, kind);
        });
    });
    for (BB *block : *graph_) {
        auto &phis = block->GetPhis();
        phis.erase(std::remove_if(phis.begin(), phis.end(), [](PhiVertex *phi) { return phi->Is<IdentityVertex>(); }),
                   phis.end());
        auto &vertices = block->GetVertices();
        vertices.erase(std::remove_if(vertices.begin(), vertices.end(),
                                      [](NonControlVertex *vertex) { return vertex->Is<IdentityVertex>(); }),
                       vertices.end());
    }
}

void PhiRepresentationSelector::MarkUnconditionalDeopt(NonControlVertex *vertex, kungfu::DeoptType reason)
{
    ASSERT(EagerDeoptimizableMixinOf(vertex) != nullptr);
    unconditionalDeopts_.emplace(vertex, reason);
}

void PhiRepresentationSelector::ApplyUnconditionalDeopts()
{
    if (unconditionalDeopts_.empty()) {
        return;
    }
    for (BB *block : *graph_) {
        auto &vertices = block->GetVertices();
        for (auto it = vertices.begin(); it != vertices.end(); ++it) {
            auto reason = unconditionalDeopts_.find(*it);
            if (reason == unconditionalDeopts_.end()) {
                continue;
            }
            auto *guard = EagerDeoptimizableMixinOf(*it);
            ASSERT(guard != nullptr);
            auto *deopt = Vertex::New<DeoptVertex>(chunk_, size_t(0), chunk_, reason->second,
                                                  guard->GetBytecodeOffset());
            deopt->SetOwner(block);
            deopt->SetEagerDeoptFrameState(CloneEagerDeoptFrame(chunk_, guard->GetEagerDeoptFrameState()));
            block->SetControlVertex(deopt);
            vertices.erase(it, vertices.end());
            break;
        }
    }
    RemoveUnreachableBlocksAndEdges();
}

void PhiRepresentationSelector::RemoveUnreachableBlocksAndEdges()
{
    std::unordered_set<BB *> reachable;
    std::vector<BB *> worklist;
    if (graph_->NumBlocks() != 0) {
        worklist.push_back(*graph_->begin());
    }
    while (!worklist.empty()) {
        BB *block = worklist.back();
        worklist.pop_back();
        if (!reachable.insert(block).second) {
            continue;
        }
        block->ForEachSuccessor([&](BB *next) { worklist.push_back(next); });
        auto visitException = [&](Vertex *vertex) {
            if (BB *handler = CatchBlockOf(vertex)) {
                worklist.push_back(handler);
            }
        };
        for (auto *vertex : block->GetVertices()) {
            visitException(vertex);
        }
        visitException(block->GetControlVertex());
    }
    std::vector<BB *> retained;
    for (BB *block : *graph_) {
        if (reachable.count(block) == 0) {
            continue;
        }
        retained.push_back(block);
        if (block->IsExceptionHandler()) {
            changedCatchBlocks_.insert(block);
            continue;  // Exception inputs are indexed by throw sites, not BBs.
        }
        auto &predecessors = block->GetPredecessors();
        std::vector<uint32_t> indices;
        for (uint32_t i = 0; i < predecessors.size(); ++i) {
            BB *predecessor = predecessors[i];
            bool hasEdge = false;
            if (reachable.count(predecessor) != 0) {
                predecessor->ForEachSuccessor([&](BB *next) { hasEdge |= next == block; });
            }
            if (hasEdge) {
                indices.push_back(i);
            }
        }
        if (indices.size() == predecessors.size()) {
            continue;
        }
        if (block->IsLoopHeader() &&
            std::find(indices.begin(), indices.end(), predecessors.size() - 1) == indices.end()) {
            block->SetIsLoopHeader(false);
            block->SetHasPeeledIteration(false);
            block->SetLoopEffects(nullptr);
        }
        for (PhiVertex *phi : block->GetPhis()) {
            ASSERT(!indices.empty());
            phi->RetainInputs(indices);
            if (indices.size() == 1 && phi->GetInput(0) != phi) {
                phi->OverwriteWithIdentity(phi->GetInput(0));
            }
        }
        auto &phis = block->GetPhis();
        phis.erase(std::remove_if(phis.begin(), phis.end(),
            [](PhiVertex *phi) { return phi->Is<IdentityVertex>(); }), phis.end());
        for (uint32_t i = 0; i < indices.size(); ++i) {
            predecessors[i] = predecessors[indices[i]];
        }
        predecessors.resize(indices.size());
        for (uint32_t i = 0; i < predecessors.size(); ++i) {
            if (auto *jump = predecessors[i]->GetControlVertex()->TryCast<UnconditionalControlVertex>()) {
                jump->SetPredecessorId(i);
            }
        }
    }
    graph_->RetainBlocks(retained);
    graph_->SetMayHaveUnreachableBlocks(false);
    for (BB *block : *graph_) {
        if (auto *jump = block->GetControlVertex()->TryCast<JumpLoopVertex>();
            jump != nullptr && !jump->Target()->IsLoopHeader()) {
            auto *forward = Vertex::New<JumpVertex>(chunk_, size_t(0), jump->Target());
            forward->SetOwner(block);
            forward->SetPredecessorId(jump->GetPredecessorId());
            block->SetControlVertex(forward);
        }
    }
}

}  // namespace panda::ecmascript::arksteed
