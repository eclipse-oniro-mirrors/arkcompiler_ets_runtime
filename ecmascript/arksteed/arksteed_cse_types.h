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

#ifndef ECMASCRIPT_ARKSTEED_CSE_TYPES_H
#define ECMASCRIPT_ARKSTEED_CSE_TYPES_H

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>

#include "ecmascript/arksteed/arksteed_facts_hamt.h"
#include "libpandabase/macros.h"

namespace panda::ecmascript::arksteed {

constexpr uint32_t CSE_PURE_EPOCH = std::numeric_limits<uint32_t>::max();
constexpr uint32_t CSE_EPOCH_OVERFLOW = CSE_PURE_EPOCH - 1;

class ValueVertex;
class NonControlVertex;

bool IsElidableWhenUnused(const ValueVertex &vertex);

template <class VertexT>
struct CseTraits;

template <class VertexT>
struct CseQuery;

struct CseInputView {
    ValueVertex *const *data {nullptr};
    size_t count {0};

    size_t size() const
    {
        return count;
    }

    ValueVertex *const *begin() const
    {
        return data;
    }

    ValueVertex *const *end() const
    {
        return count == 0 ? data : data + count;
    }

    ValueVertex *operator[](size_t index) const
    {
        ASSERT(index < count);
        return data[index];
    }
};

struct CseUInt32View {
    const uint32_t *data {nullptr};
    size_t count {0};

    bool operator==(const CseUInt32View &other) const
    {
        return count == other.count && (count == 0 || std::equal(data, data + count, other.data));
    }

    bool operator!=(const CseUInt32View &other) const
    {
        return !(*this == other);
    }
};

struct CommutativeInputs {
    uint32_t left;
    uint32_t right;
};

enum class CsePolicy : uint8_t {
    NONE,
    VALUE,
    SUCCESS_CHECK,
};

struct AvailableExpressionRecord {
    NonControlVertex *node {nullptr};
    uint32_t effectEpoch {0};
};

struct ExpressionLink {
    const AvailableExpressionRecord *record {nullptr};
    const ExpressionLink *next {nullptr};
};

struct CseBucketHash {
    uint64_t operator()(uint64_t hash) const
    {
        return hash;
    }
};

struct CseBucketTraits {
    static uint32_t MinEpoch(const ExpressionLink *head)
    {
        uint32_t epoch = CSE_PURE_EPOCH;
        for (const ExpressionLink *link = head; link != nullptr; link = link->next) {
            epoch = std::min(epoch, link->record->effectEpoch);
        }
        return epoch;
    }

    static bool HasEmptyInfo([[maybe_unused]] const ExpressionLink *head)
    {
        return false;
    }
};

class CseAvailableExpressions {
public:
    explicit CseAvailableExpressions(Chunk *chunk) : chunk_(chunk), pureBuckets_(chunk), memoryBuckets_(chunk) {}

    NO_COPY_SEMANTIC(CseAvailableExpressions);
    NO_MOVE_SEMANTIC(CseAvailableExpressions);

    // The caller supplies the hash of this query and the epoch at lookup time.
    template <class VertexT>
    VertexT *Find(const CseQuery<VertexT> &query, uint64_t hash, uint32_t effectEpoch) const;

    // Reuse the query hash, but read effectEpoch after all node-building callbacks.
    template <class VertexT>
    void AddExpression(const CseQuery<VertexT> &query, VertexT *node, uint64_t hash, uint32_t effectEpoch);

    void CopyFrom(const CseAvailableExpressions &source);
    void IntersectWith(const CseAvailableExpressions &other, uint32_t effectEpoch);
    void Clear();
    void ClearMemoryExpressions();

    bool Empty() const
    {
        return pureBuckets_.Empty() && memoryBuckets_.Empty();
    }

private:
    void AddRecord(NonControlVertex *node, uint64_t hash, uint32_t recordEpoch);

    Chunk *chunk_;
    using Buckets =
        FactsHamt<uint64_t, const ExpressionLink *, CseBucketHash, std::equal_to<uint64_t>, CseBucketTraits>;
    Buckets pureBuckets_;
    Buckets memoryBuckets_;
};

}  // namespace panda::ecmascript::arksteed

#endif  // ECMASCRIPT_ARKSTEED_CSE_TYPES_H
