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

#include "ecmascript/arksteed/arksteed_cse.h"

#include <cstdint>
#include <functional>

namespace panda::ecmascript::arksteed {

namespace {
bool IsCseConstant(ValueVertex *vertex)
{
    switch (vertex->GetOpcode()) {
        case VertexOpcode::Int32Constant:
        case VertexOpcode::Int64Constant:
        case VertexOpcode::Float64Constant:
        case VertexOpcode::TaggedConstant:
        case VertexOpcode::HeapConstant:
            return true;
        default:
            return false;
    }
}

bool ContainsRecord(const ExpressionLink *head, const AvailableExpressionRecord &record)
{
    for (const ExpressionLink *link = head; link != nullptr; link = link->next) {
        if (link->record->node == record.node && link->record->effectEpoch == record.effectEpoch) {
            return true;
        }
    }
    return false;
}

const ExpressionLink *PrependLink(Chunk *chunk, const AvailableExpressionRecord *record, const ExpressionLink *next)
{
    auto *link = chunk->New<ExpressionLink>();
    link->record = record;
    link->next = next;
    return link;
}
}  // namespace

uint64_t CseHashInputs(VertexOpcode opcode, CseInputView inputs)
{
    uint64_t hash = CseHashCombine(CSE_FNV_OFFSET_BASIS, static_cast<uint64_t>(opcode));
    hash = CseHashCombine(hash, inputs.count);
    for (size_t index = 0; index < inputs.count; ++index) {
        hash = CseHashCombine(hash, reinterpret_cast<uintptr_t>(inputs[index]));
    }
    return hash;
}

bool ShouldSwapCseOperands(ValueVertex *left, ValueVertex *right)
{
    ASSERT(left != nullptr && right != nullptr);
    bool leftConstant = IsCseConstant(left);
    bool rightConstant = IsCseConstant(right);
    if (leftConstant != rightConstant) {
        return leftConstant;
    }
    return std::less<ValueVertex *> {}(right, left);
}

bool IsElidableWhenUnused(const ValueVertex &vertex)
{
    switch (vertex.GetOpcode()) {
        case VertexOpcode::CheckedTaggedIntToI32:
        case VertexOpcode::CheckedNumberToF64:
        case VertexOpcode::CheckedTaggedString:
        case VertexOpcode::CheckedNonNegativeI32ToTaggedInt:
        case VertexOpcode::TaggedNumberToBoolean:
        case VertexOpcode::TaggedDoubleToBoolean:
        case VertexOpcode::I32AddWithOverflow:
        case VertexOpcode::I32SubWithOverflow:
        case VertexOpcode::I32MulWithOverflow:
        case VertexOpcode::I32DivWithOverflow:
        case VertexOpcode::CheckedI32Mod:
        case VertexOpcode::I32DivByConstWithCheck:
        case VertexOpcode::I32IncWithOverflow:
        case VertexOpcode::I32DecWithOverflow:
        case VertexOpcode::I32NegWithOverflow:
            return false;
        default:
            break;
    }
    VertexPropertyFlag flags = vertex.GetPropertyFlags();
    return !CanDeopt(flags) && !CanRead(flags) && !CanWrite(flags) && !CanAllocate(flags) && !CanThrow(flags) &&
           !IsCall(flags) && !IsNotIdempotent(flags);
}

void CseAvailableExpressions::AddRecord(NonControlVertex *node, uint64_t hash, uint32_t recordEpoch)
{
    // One updater instantiation serves every vertex type.
    auto &buckets = recordEpoch == CSE_PURE_EPOCH ? pureBuckets_ : memoryBuckets_;
    buckets.Update(hash, [this, node, recordEpoch](const ExpressionLink *previous) {
        auto *record = chunk_->New<AvailableExpressionRecord>();
        record->node = node;
        record->effectEpoch = recordEpoch;
        auto *link = chunk_->New<ExpressionLink>();
        link->record = record;
        link->next = previous;
        return link;
    });
}

void CseAvailableExpressions::CopyFrom(const CseAvailableExpressions &source)
{
    pureBuckets_.CopyFrom(source.pureBuckets_);
    memoryBuckets_.CopyFrom(source.memoryBuckets_);
}

void CseAvailableExpressions::IntersectWith(const CseAvailableExpressions &other, uint32_t effectEpoch)
{
    auto merge = [this, effectEpoch](uint64_t, const ExpressionLink *left, const ExpressionLink *right) {
        bool allRetained = true;
        for (const ExpressionLink *link = left; link != nullptr; link = link->next) {
            const AvailableExpressionRecord &record = *link->record;
            if (record.effectEpoch < effectEpoch || !ContainsRecord(right, record)) {
                allRetained = false;
            }
        }
        if (allRetained) {
            return left;
        }
        const ExpressionLink *head = nullptr;
        for (const ExpressionLink *link = left; link != nullptr; link = link->next) {
            const AvailableExpressionRecord &record = *link->record;
            if (record.effectEpoch >= effectEpoch && ContainsRecord(right, record)) {
                head = PrependLink(chunk_, link->record, head);
            }
        }
        return head;
    };
    auto reuse = [effectEpoch](const auto &metadata) { return metadata.minEpoch >= effectEpoch; };
    pureBuckets_.IntersectWith(other.pureBuckets_, merge, reuse);
    memoryBuckets_.IntersectWith(other.memoryBuckets_, merge, reuse);
}

void CseAvailableExpressions::Clear()
{
    pureBuckets_.Clear();
    ClearMemoryExpressions();
}

void CseAvailableExpressions::ClearMemoryExpressions()
{
    memoryBuckets_.Clear();
}

}  // namespace panda::ecmascript::arksteed
