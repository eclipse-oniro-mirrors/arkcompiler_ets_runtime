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

#ifndef ECMASCRIPT_ARKSTEED_FACTS_HAMT_H
#define ECMASCRIPT_ARKSTEED_FACTS_HAMT_H

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <limits>
#include <type_traits>

#include "ecmascript/mem/chunk.h"
#include "libpandabase/macros.h"

namespace panda::ecmascript::arksteed {

// Mix pointer and composite keys before consuming their bits. In particular,
// aligned vertex pointers must not concentrate in the same low-bit branches.
inline uint64_t MixFactsHash(uint64_t value)
{
    value ^= value >> 30U;
    value *= UINT64_C(0xbf58476d1ce4e5b9);
    value ^= value >> 27U;
    value *= UINT64_C(0x94d049bb133111eb);
    return value ^ (value >> 31U);
}

template <class Key>
struct FactsHamtHash {
    uint64_t operator()(const Key &key) const
    {
        return MixFactsHash(static_cast<uint64_t>(std::hash<Key> {}(key)));
    }
};

template <class Value>
struct FactsHamtValueTraits {
    static uint32_t MinEpoch([[maybe_unused]] Value value)
    {
        return std::numeric_limits<uint32_t>::max();
    }

    static bool HasEmptyInfo([[maybe_unused]] Value value)
    {
        return false;
    }
};

// A per-compilation hash trie. Values and collision-chain entries are immutable;
// an editor may change trie nodes only until its next snapshot is published.
template <class Key, class Value, class Hash = FactsHamtHash<Key>, class Equal = std::equal_to<Key>,
          class ValueTraits = FactsHamtValueTraits<Value>>
class FactsHamt {
    static_assert(std::is_pointer_v<Value>);
    static_assert(std::is_trivially_destructible_v<Key>);

public:
    struct Metadata {
        uint32_t minEpoch {std::numeric_limits<uint32_t>::max()};
        bool hasEmptyInfo {false};
    };

    explicit FactsHamt(Chunk *chunk) : chunk_(chunk)
    {
        ASSERT(chunk_ != nullptr);
    }

    NO_COPY_SEMANTIC(FactsHamt);
    NO_MOVE_SEMANTIC(FactsHamt);

    bool Empty() const
    {
        return root_ == nullptr;
    }

    void Clear()
    {
        root_ = nullptr;
    }

    void CopyFrom(const FactsHamt &source)
    {
        ASSERT(Empty());
        ASSERT(chunk_ == source.chunk_);
        source.Freeze();
        root_ = source.root_;
        editor_ = nullptr;
    }

    Value Find(const Key &key) const
    {
        const uint64_t hash = Hash {}(key);
        const Node *node = root_;
        uint32_t shift = 0;
        while (node != nullptr && node->kind == Kind::BRANCH) {
            const uint32_t bit = Bit(hash, shift);
            if ((node->bitmap & bit) == 0) {
                return nullptr;
            }
            node = node->children[Index(node->bitmap, bit)];
            shift += BITS_PER_LEVEL;
        }
        if (node == nullptr || node->hash != hash) {
            return nullptr;
        }
        for (const Entry *entry = node->entries; entry != nullptr; entry = entry->next) {
            if (Equal {}(entry->key, key)) {
                return entry->value;
            }
        }
        return nullptr;
    }

    void InsertOrAssign(const Key &key, Value value)
    {
        ASSERT(value != nullptr);
        Update(key, [value](Value) { return value; });
    }

    // Read the current value and publish its replacement in one descent.
    // The callback runs exactly once and must not mutate this trie.
    template <class Callback>
    void Update(const Key &key, Callback callback)
    {
        bool changed = false;
        root_ = UpdateNode(root_, key, Hash {}(key), 0, callback, changed);
    }

    void Erase(const Key &key)
    {
        bool changed = false;
        root_ = EraseNode(root_, key, Hash {}(key), 0, changed);
    }

    template <class Callback>
    void ForEach(Callback callback) const
    {
        Visit(root_, callback);
    }

    // callback(key, oldValue) returns a replacement or nullptr to erase it.
    template <class Callback>
    void Transform(Callback callback)
    {
        TransformWithReuse(callback, [](const Metadata &) { return false; });
    }

    template <class Callback, class Reuse>
    void TransformWithReuse(Callback callback, Reuse reuse)
    {
        if (Empty()) {
            return;
        }
        Freeze();
        editor_ = nullptr;
        EnsureEditor();
        root_ = TransformNode(root_, callback, reuse);
    }

    // merge(key, left, right) returns a replacement or nullptr to erase it.
    // reuse(metadata) decides whether an identical subtree needs no processing.
    template <class Merge, class Reuse>
    void IntersectWith(const FactsHamt &other, Merge merge, Reuse reuse)
    {
        ASSERT(chunk_ == other.chunk_);
        if (Empty() || other.Empty()) {
            Clear();
            return;
        }
        Freeze();
        other.Freeze();
        editor_ = nullptr;
        EnsureEditor();
        root_ = Intersect(root_, other.root_, 0, merge, reuse);
    }

private:
    static constexpr uint32_t BITS_PER_LEVEL = 5U;
    static constexpr uint32_t BRANCH_MASK = 31U;
    static constexpr size_t MAX_CHILDREN = 32U;

    enum class Kind : uint8_t { LEAF, BRANCH };

    struct Editor {
        bool active {true};
    };

    struct Entry {
        Entry(const Key &entryKey, Value entryValue, const Entry *nextEntry)
            : key(entryKey), value(entryValue), next(nextEntry)
        {}

        Key key;
        Value value;
        const Entry *next;
    };

    struct Node {
        Kind kind {Kind::LEAF};
        Editor *owner {nullptr};
        Metadata metadata {};
        uint64_t hash {0};
        const Entry *entries {nullptr};
        uint32_t bitmap {0};
        Node **children {nullptr};
    };

    static uint32_t Bit(uint64_t hash, uint32_t shift)
    {
        ASSERT(shift < 64U);
        return UINT32_C(1) << ((hash >> shift) & BRANCH_MASK);
    }

    static uint32_t Count(uint32_t bits)
    {
        return static_cast<uint32_t>(__builtin_popcount(bits));
    }

    static uint32_t Index(uint32_t bitmap, uint32_t bit)
    {
        return Count(bitmap & (bit - 1U));
    }

    void Freeze() const
    {
        if (editor_ != nullptr) {
            editor_->active = false;
        }
    }

    void EnsureEditor()
    {
        if (editor_ == nullptr || !editor_->active) {
            editor_ = chunk_->New<Editor>();
        }
    }

    Node **CopyChildren(Node *const *children, uint32_t count)
    {
        Node **copy = chunk_->NewArray<Node *>(count);
        std::copy_n(children, count, copy);
        return copy;
    }

    Node *Editable(Node *node, bool copyChildren = true)
    {
        if (node->owner == editor_ && editor_->active) {
            return node;
        }
        Node *copy = chunk_->New<Node>(*node);
        copy->owner = editor_;
        if (copyChildren && copy->kind == Kind::BRANCH) {
            copy->children = CopyChildren(node->children, Count(node->bitmap));
        }
        return copy;
    }

    static void UpdateMetadata(Node *node)
    {
        node->metadata = {};
        if (node->kind == Kind::LEAF) {
            for (const Entry *entry = node->entries; entry != nullptr; entry = entry->next) {
                node->metadata.minEpoch = std::min(node->metadata.minEpoch, ValueTraits::MinEpoch(entry->value));
                node->metadata.hasEmptyInfo |= ValueTraits::HasEmptyInfo(entry->value);
            }
            return;
        }
        for (uint32_t index = 0; index < Count(node->bitmap); ++index) {
            const Metadata &child = node->children[index]->metadata;
            node->metadata.minEpoch = std::min(node->metadata.minEpoch, child.minEpoch);
            node->metadata.hasEmptyInfo |= child.hasEmptyInfo;
        }
    }

    static void IncludeMetadata(Node *node, const Metadata &added)
    {
        node->metadata.minEpoch = std::min(node->metadata.minEpoch, added.minEpoch);
        node->metadata.hasEmptyInfo |= added.hasEmptyInfo;
    }

    static void ReplaceChildMetadata(Node *node, const Metadata &before, const Metadata &after)
    {
        // A removed contribution may have supplied the aggregate minimum/flag.
        if ((before.minEpoch == node->metadata.minEpoch && after.minEpoch > before.minEpoch) ||
            (before.hasEmptyInfo && !after.hasEmptyInfo)) {
            UpdateMetadata(node);
            return;
        }
        IncludeMetadata(node, after);
    }

    Node *NewLeaf(uint64_t hash, const Entry *entries)
    {
        ASSERT(entries != nullptr);
        Node *node = chunk_->New<Node>();
        node->owner = editor_;
        node->hash = hash;
        node->entries = entries;
        UpdateMetadata(node);
        return node;
    }

    Node *NewBranch(uint32_t bitmap, Node *const *children)
    {
        uint32_t count = Count(bitmap);
        if (count == 0) {
            return nullptr;
        }
        if (count == 1 && children[0]->kind == Kind::LEAF) {
            return children[0];
        }
        Node *node = chunk_->New<Node>();
        node->kind = Kind::BRANCH;
        node->owner = editor_;
        node->bitmap = bitmap;
        node->children = CopyChildren(children, count);
        UpdateMetadata(node);
        return node;
    }

    const Entry *ReplaceEntry(const Entry *entry, const Key &key, Value value)
    {
        ASSERT(entry != nullptr);
        if (Equal {}(entry->key, key)) {
            return chunk_->New<Entry>(key, value, entry->next);
        }
        return chunk_->New<Entry>(entry->key, entry->value, ReplaceEntry(entry->next, key, value));
    }

    const Entry *EraseEntry(const Entry *entry, const Key &key)
    {
        ASSERT(entry != nullptr);
        if (Equal {}(entry->key, key)) {
            return entry->next;
        }
        return chunk_->New<Entry>(entry->key, entry->value, EraseEntry(entry->next, key));
    }

    template <class Callback>
    Node *UpdateNode(Node *node, const Key &key, uint64_t hash, uint32_t shift, Callback &callback, bool &changed)
    {
        if (node == nullptr) {
            Value value = callback(nullptr);
            ASSERT(value != nullptr);
            EnsureEditor();
            changed = true;
            return NewLeaf(hash, chunk_->New<Entry>(key, value, nullptr));
        }
        if (node->kind == Kind::LEAF) {
            if (node->hash == hash) {
                const Entry *existing = node->entries;
                while (existing != nullptr && !Equal {}(existing->key, key)) {
                    existing = existing->next;
                }
                Value value = callback(existing == nullptr ? nullptr : existing->value);
                ASSERT(value != nullptr);
                if (existing != nullptr && existing->value == value) {
                    return node;
                }
                EnsureEditor();
                changed = true;
                Node *copy = Editable(node);
                copy->entries = existing != nullptr ? ReplaceEntry(node->entries, key, value)
                                                    : chunk_->New<Entry>(key, value, node->entries);
                UpdateMetadata(copy);
                return copy;
            }
            const uint32_t oldBit = Bit(node->hash, shift);
            const uint32_t newBit = Bit(hash, shift);
            if (oldBit == newBit) {
                Node *child = UpdateNode(node, key, hash, shift + BITS_PER_LEVEL, callback, changed);
                // A one-child branch is required here to preserve the consumed hash bits.
                Node *branch = chunk_->New<Node>();
                branch->kind = Kind::BRANCH;
                branch->owner = editor_;
                branch->bitmap = oldBit;
                branch->children = CopyChildren(&child, 1);
                UpdateMetadata(branch);
                return branch;
            }
            Node *added = UpdateNode(nullptr, key, hash, shift, callback, changed);
            std::array<Node *, 2> children =                           // 2: the existing node and the newly added node.
                oldBit < newBit ? std::array<Node *, 2> {node, added}  // 2: both children in bitmap order.
                                : std::array<Node *, 2> {added, node};  // 2: both children in bitmap order.
            return NewBranch(oldBit | newBit, children.data());
        }

        const uint32_t bit = Bit(hash, shift);
        const uint32_t index = Index(node->bitmap, bit);
        if ((node->bitmap & bit) != 0) {
            // Editable descendants can change in place, so capture metadata before descending.
            const Metadata before = node->children[index]->metadata;
            Node *child = UpdateNode(node->children[index], key, hash, shift + BITS_PER_LEVEL, callback, changed);
            if (!changed) {
                return node;
            }
            Node *copy = Editable(node);
            copy->children[index] = child;
            ReplaceChildMetadata(copy, before, child->metadata);
            return copy;
        }
        Node *added = UpdateNode(nullptr, key, hash, shift + BITS_PER_LEVEL, callback, changed);
        const uint32_t oldCount = Count(node->bitmap);
        // Growing the child array replaces it, so a frozen node needs only a shallow copy.
        Node *copy = Editable(node, false);
        Node **children = chunk_->NewArray<Node *>(oldCount + 1U);
        std::copy_n(copy->children, index, children);
        children[index] = added;
        std::copy_n(copy->children + index, oldCount - index, children + index + 1U);
        copy->bitmap |= bit;
        copy->children = children;
        IncludeMetadata(copy, added->metadata);
        return copy;
    }

    Node *EraseNode(Node *node, const Key &key, uint64_t hash, uint32_t shift, bool &changed)
    {
        if (node == nullptr) {
            return nullptr;
        }
        if (node->kind == Kind::LEAF) {
            if (node->hash != hash) {
                return node;
            }
            const Entry *existing = node->entries;
            while (existing != nullptr && !Equal {}(existing->key, key)) {
                existing = existing->next;
            }
            if (existing == nullptr) {
                return node;
            }
            EnsureEditor();
            changed = true;
            const Entry *entries = EraseEntry(node->entries, key);
            if (entries == nullptr) {
                return nullptr;
            }
            Node *copy = Editable(node);
            copy->entries = entries;
            UpdateMetadata(copy);
            return copy;
        }
        const uint32_t bit = Bit(hash, shift);
        if ((node->bitmap & bit) == 0) {
            return node;
        }
        const uint32_t index = Index(node->bitmap, bit);
        const Metadata before = node->children[index]->metadata;
        Node *child = EraseNode(node->children[index], key, hash, shift + BITS_PER_LEVEL, changed);
        if (!changed) {
            return node;
        }
        if (child != nullptr) {
            Node *copy = Editable(node);
            copy->children[index] = child;
            ReplaceChildMetadata(copy, before, child->metadata);
            return copy;
        }
        // NewBranch builds the replacement array; do not copy an array that is discarded.
        const uint32_t count = Count(node->bitmap);
        std::array<Node *, MAX_CHILDREN> children {};
        std::copy_n(node->children, index, children.data());
        std::copy_n(node->children + index + 1U, count - index - 1U, children.data() + index);
        return NewBranch(node->bitmap & ~bit, children.data());
    }

    template <class Callback>
    static void Visit(const Node *node, Callback &callback)
    {
        if (node == nullptr) {
            return;
        }
        if (node->kind == Kind::LEAF) {
            for (const Entry *entry = node->entries; entry != nullptr; entry = entry->next) {
                callback(entry->key, entry->value);
            }
            return;
        }
        for (uint32_t index = 0; index < Count(node->bitmap); ++index) {
            Visit(node->children[index], callback);
        }
    }

    template <class Callback>
    const Entry *TransformEntries(const Entry *entry, Callback &callback)
    {
        if (entry == nullptr) {
            return nullptr;
        }
        Value value = callback(entry->key, entry->value);
        const Entry *next = TransformEntries(entry->next, callback);
        if (value == nullptr) {
            return next;
        }
        if (value == entry->value && next == entry->next) {
            return entry;
        }
        return chunk_->New<Entry>(entry->key, value, next);
    }

    template <class Callback, class Reuse>
    Node *TransformNode(Node *node, Callback &callback, Reuse &reuse)
    {
        if (node == nullptr) {
            return nullptr;
        }
        if (reuse(node->metadata)) {
            return node;
        }
        if (node->kind == Kind::LEAF) {
            const Entry *entries = TransformEntries(node->entries, callback);
            if (entries == node->entries) {
                return node;
            }
            return entries == nullptr ? nullptr : NewLeaf(node->hash, entries);
        }
        std::array<Node *, MAX_CHILDREN> children {};
        uint32_t resultBitmap = 0;
        uint32_t resultCount = 0;
        uint32_t index = 0;
        bool unchanged = true;
        for (uint32_t bits = node->bitmap; bits != 0; bits &= bits - 1U) {
            const uint32_t bit = bits & (~bits + 1U);
            Node *child = TransformNode(node->children[index], callback, reuse);
            unchanged &= child == node->children[index++];
            if (child != nullptr) {
                resultBitmap |= bit;
                children[resultCount++] = child;
            }
        }
        return unchanged ? node : NewBranch(resultBitmap, children.data());
    }

    template <class Merge>
    const Entry *IntersectEntries(const Entry *left, const Entry *right, Merge &merge)
    {
        if (left == nullptr) {
            return nullptr;
        }
        const Entry *matching = right;
        while (matching != nullptr && !Equal {}(left->key, matching->key)) {
            matching = matching->next;
        }
        Value value = matching == nullptr ? nullptr : merge(left->key, left->value, matching->value);
        const Entry *next = IntersectEntries(left->next, right, merge);
        if (value == nullptr) {
            return next;
        }
        if (value == left->value && next == left->next) {
            return left;
        }
        return chunk_->New<Entry>(left->key, value, next);
    }

    template <class Merge, class Reuse>
    Node *Intersect(Node *left, Node *right, uint32_t shift, Merge &merge, Reuse &reuse)
    {
        if (left == nullptr || right == nullptr) {
            return nullptr;
        }
        if (left == right && reuse(left->metadata)) {
            return left;
        }
        if (left->kind == Kind::LEAF && right->kind == Kind::LEAF) {
            if (left->hash != right->hash) {
                return nullptr;
            }
            const Entry *entries = IntersectEntries(left->entries, right->entries, merge);
            if (entries == left->entries) {
                return left;
            }
            return entries == nullptr ? nullptr : NewLeaf(left->hash, entries);
        }
        if (left->kind == Kind::LEAF) {
            const uint32_t bit = Bit(left->hash, shift);
            return (right->bitmap & bit) == 0 ? nullptr
                                              : Intersect(left, right->children[Index(right->bitmap, bit)],
                                                          shift + BITS_PER_LEVEL, merge, reuse);
        }
        if (right->kind == Kind::LEAF) {
            const uint32_t bit = Bit(right->hash, shift);
            return (left->bitmap & bit) == 0 ? nullptr
                                             : Intersect(left->children[Index(left->bitmap, bit)], right,
                                                         shift + BITS_PER_LEVEL, merge, reuse);
        }
        std::array<Node *, MAX_CHILDREN> children {};
        uint32_t resultBitmap = 0;
        uint32_t resultCount = 0;
        bool unchanged = left->bitmap == (left->bitmap & right->bitmap);
        for (uint32_t bits = left->bitmap & right->bitmap; bits != 0; bits &= bits - 1U) {
            const uint32_t bit = bits & (~bits + 1U);
            Node *oldChild = left->children[Index(left->bitmap, bit)];
            Node *child =
                Intersect(oldChild, right->children[Index(right->bitmap, bit)], shift + BITS_PER_LEVEL, merge, reuse);
            unchanged &= child == oldChild;
            if (child != nullptr) {
                resultBitmap |= bit;
                children[resultCount++] = child;
            }
        }
        return unchanged ? left : NewBranch(resultBitmap, children.data());
    }

    Chunk *chunk_;
    Node *root_ {nullptr};
    mutable Editor *editor_ {nullptr};
};

}  // namespace panda::ecmascript::arksteed

#endif  // ECMASCRIPT_ARKSTEED_FACTS_HAMT_H
