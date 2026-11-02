/*
 * Copyright (c) 2025 Huawei Device Co., Ltd.
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

#ifndef ECMASCRIPT_MODULE_MANAGER_MAP_LOCALCC_H
#define ECMASCRIPT_MODULE_MANAGER_MAP_LOCALCC_H

#include "ecmascript/base/config.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <type_traits>

#include "common_components/taskpool/taskpool.h"
#include "ecmascript/mem/gc_root.h"
#include "ecmascript/mem/c_containers.h"
#include "ecmascript/platform/mutex.h"
#include "ecmascript/module/module_root_visitor.h"

namespace panda::ecmascript {
/**
 * A hash map for use as a GC root container.
 * All stored values are wrapped in GCRoot with CMCGC.
 *
 * This container is not thread-safe. Callers must provide synchronization when
 * the same map can be accessed from multiple threads.
 */
#if ENABLE_LATEST_OPTIMIZATION
template <class Key, class Hash = std::hash<Key>, class KeyEqual = std::equal_to<Key>>
class ModuleManagerMap {
private:
    // Use 2 bits for three states
    // State bits are the highest 2 bits, hash value is in the lower 62 bits
    static constexpr size_t STATE_BITS = 2;
    static constexpr size_t HASH_BITS = sizeof(size_t) * 8 - STATE_BITS;
    static constexpr size_t HASH_MASK = (static_cast<size_t>(1) << HASH_BITS) - 1;
    static constexpr size_t STATE_MASK = ~HASH_MASK;

    // State definitions
    static constexpr size_t EMPTY_STATE = 0ull << HASH_BITS;    // 00
    static constexpr size_t DELETED_STATE = 1ull << HASH_BITS;  // 01
    static constexpr size_t OCCUPIED_STATE = 2ull << HASH_BITS; // 10

    struct Entry {
        size_t hashState;  // Highest 2 bits for state, lower 62 bits for hash
        Key key;
        GCRoot value;

        Entry() : hashState(EMPTY_STATE) {}

        // Move constructor
        Entry(Entry&& other) noexcept
            : hashState(other.hashState),
              key(std::move(other.key)),
              value(std::move(other.value))
        {
            other.hashState = EMPTY_STATE;
        }

        // Move assignment operator
        Entry& operator=(Entry&& other) noexcept
        {
            if (this != &other) {
                hashState = other.hashState;
                key = std::move(other.key);
                value = std::move(other.value);
                other.hashState = EMPTY_STATE;
            }
            return *this;
        }

        // Disable copy
        Entry(const Entry&) = delete;
        Entry& operator=(const Entry&) = delete;

        // State checks
        bool IsEmpty() const { return (hashState & STATE_MASK) == EMPTY_STATE; }
        bool IsDeleted() const { return (hashState & STATE_MASK) == DELETED_STATE; }
        bool IsOccupied() const { return (hashState & STATE_MASK) == OCCUPIED_STATE; }

        // Get hash value (without state bits)
        size_t GetHash() const { return hashState & HASH_MASK; }

        // Set states
        void SetEmpty() { hashState = EMPTY_STATE; }
        void SetDeleted() { hashState = (hashState & HASH_MASK) | DELETED_STATE; }
        void SetOccupied(size_t hash) { hashState = (hash & HASH_MASK) | OCCUPIED_STATE; }

        // Fast comparison: first compare hash, then key
        template <typename K>
        bool FastEqual(const K& otherKey, size_t otherHash, const KeyEqual& equal) const
        {
            if (GetHash() != otherHash) {
                return false;
            }
            return equal(key, otherKey);
        }
    };

    // Hash table parameters
    static constexpr size_t INITIAL_CAPACITY = 16;
    static constexpr size_t MIN_CAPACITY = 16;
    static constexpr size_t GROW_FACTOR = 2;
    static constexpr double MAX_LOAD_FACTOR = 0.75;
    static constexpr double MIN_LOAD_FACTOR = 0.125;
    static constexpr double DELETION_THRESHOLD = 0.3;
    static constexpr size_t MAX_LOAD_FACTOR_SHIFT = 2; // 3 / 4
    static constexpr size_t MIN_LOAD_FACTOR_SHIFT = 3; // 1 / 8
    static constexpr size_t EMPTY_MARK = static_cast<size_t>(-1);
    static constexpr size_t PARALLEL_MIN_ENTRIES_PER_TASK = 8192;
    static constexpr size_t PARALLEL_BLOCK_ENTRIES = 4096;

    struct ConcurrentRootScanContext {
        ConcurrentRootScanContext(size_t capacity, size_t blockSize)
            : capacity(capacity), blockSize(blockSize) {}

        size_t capacity {0};
        size_t blockSize {0};
        std::atomic<size_t> nextIndex {0};
    };

    std::unique_ptr<Entry[]> table_;
    size_t capacity_;
    size_t size_;
    size_t deletedCount_;
    Hash hasher_;
    KeyEqual keyEqual_;
    std::atomic<bool> concurrentRootScanCheckRequired_ {false};
    mutable Mutex mutationMutex_;
    ConditionVariable concurrentRootScanCV_;
    bool concurrentRootScanActive_ {false};
    std::unique_ptr<ConcurrentRootScanContext> concurrentRootScanContext_;

    void WaitForConcurrentRootScan();

    void InsertImpl(const Key &key, JSTaggedValue value);

    template <typename K>
    bool EmplaceImpl(const K &key, JSTaggedValue value);

    void EraseImpl(const Key &key);

    void ClearImpl();

    ARK_NOINLINE void InsertWithConcurrentRootScanGate(const Key &key, JSTaggedValue value);

    template <typename K>
    ARK_NOINLINE bool EmplaceWithConcurrentRootScanGate(const K &key, JSTaggedValue value);

    ARK_NOINLINE void EraseWithConcurrentRootScanGate(const Key &key);

    ARK_NOINLINE void ClearWithConcurrentRootScanGate();

    // Find position of key - must skip deleted states
    template <typename K>
    size_t FindPosition(const K& key) const;

    // Find insertion position - can insert into deleted positions
    template <typename K>
    size_t FindInsertPosition(const K& key, size_t hash, bool& found) const;

    // Rehash to new capacity
    void Rehash(size_t newCapacity);

    // Check if we should shrink the table
    bool ShouldShrink() const;

    // Check if we need to rehash due to too many deletions
    bool TooManyDeletions() const;

    void ForEachValueRange(size_t begin, size_t end, const ModuleRootVisitor &visitor);

    void ForEachValueBlocks(ConcurrentRootScanContext &context, const ModuleRootVisitor &visitor);

public:
    ModuleManagerMap()
        : table_(std::make_unique<Entry[]>(INITIAL_CAPACITY)),
          capacity_(INITIAL_CAPACITY),
          size_(0),
          deletedCount_(0),
          hasher_(),
          keyEqual_() {}

    explicit ModuleManagerMap(const Hash& hasher, const KeyEqual& keyEqual = KeyEqual())
        : table_(std::make_unique<Entry[]>(INITIAL_CAPACITY)),
          capacity_(INITIAL_CAPACITY),
          size_(0),
          deletedCount_(0),
          hasher_(hasher),
          keyEqual_(keyEqual) {}

    ~ModuleManagerMap() = default;

    ModuleManagerMap(const ModuleManagerMap&) = delete;
    ModuleManagerMap& operator=(const ModuleManagerMap&) = delete;

    ModuleManagerMap(ModuleManagerMap&& other) noexcept
        : table_(std::move(other.table_)),
          capacity_(other.capacity_),
          size_(other.size_),
          deletedCount_(other.deletedCount_),
          hasher_(std::move(other.hasher_)),
          keyEqual_(std::move(other.keyEqual_))
    {
        ASSERT(!other.concurrentRootScanActive_);
        ASSERT(!other.concurrentRootScanCheckRequired_.load(std::memory_order_relaxed));
        other.capacity_ = INITIAL_CAPACITY;
        other.size_ = 0;
        other.deletedCount_ = 0;
        other.table_ = std::make_unique<Entry[]>(INITIAL_CAPACITY);
    }

    ModuleManagerMap& operator=(ModuleManagerMap&& other) noexcept
    {
        if (this != &other) {
            ASSERT(!concurrentRootScanActive_);
            ASSERT(!concurrentRootScanCheckRequired_.load(std::memory_order_relaxed));
            ASSERT(!other.concurrentRootScanActive_);
            ASSERT(!other.concurrentRootScanCheckRequired_.load(std::memory_order_relaxed));
            table_ = std::move(other.table_);
            capacity_ = other.capacity_;
            size_ = other.size_;
            deletedCount_ = other.deletedCount_;
            hasher_ = std::move(other.hasher_);
            keyEqual_ = std::move(other.keyEqual_);

            other.capacity_ = INITIAL_CAPACITY;
            other.size_ = 0;
            other.deletedCount_ = 0;
            other.table_ = std::make_unique<Entry[]>(INITIAL_CAPACITY);
        }
        return *this;
    }

    /**
      * Inserts or updates a key-value pair.
      * If the key already exists, replaces the existing value.
      */
    void Insert(const Key &key, JSTaggedValue value)
    {
        if (UNLIKELY(concurrentRootScanCheckRequired_.load(std::memory_order_relaxed))) {
            InsertWithConcurrentRootScanGate(key, value);
            return;
        }
        InsertImpl(key, value);
    }

    /**
      * Inserts a key-value pair only if the key doesn't exist.
      * If the key already exists, the operation is a no-op.
      * The try_emplace ensure a GCRoot constructor is only trigger
      * if the insertion actually happen.
      *
      * Note: Returns bool instead of <iterator,bool>. Exposing iterators would
      * be unsafe as they can become invalid after later mutations.
      */
    template <typename K>
    bool Emplace(const K &key, JSTaggedValue value)
    {
        if (UNLIKELY(concurrentRootScanCheckRequired_.load(std::memory_order_relaxed))) {
            return EmplaceWithConcurrentRootScanGate(key, value);
        }
        return EmplaceImpl(key, value);
    }

    /**
      * Safely retrieves a value by key with readbarrier.
      *
      * The returned value is obtained via GCRoot::Read(), ensuring proper read
      * barriers are applied when needed. The value is returned by copy.
      */
    template <typename K>
    std::optional<JSTaggedValue> Find(const K& key)
    {
        size_t pos = FindPosition(key);
        if (pos != EMPTY_MARK) {
            return std::make_optional(table_[pos].value.Read(concurrentRootScanCheckRequired_));
        }
        return std::nullopt;
    }

    template <typename K>
    bool FindValue(const K& key, JSTaggedValue &value)
    {
        size_t pos = FindPosition(key);
        if (pos == EMPTY_MARK) {
            return false;
        }
        value = table_[pos].value.Read(concurrentRootScanCheckRequired_);
        return true;
    }

    /**
     * Applies a function to each key-value pair.
     */
    template <typename Func>
    void ForEach(Func &&fn)
    {
        LockHolder lock(mutationMutex_);
        WaitForConcurrentRootScan();
        for (size_t i = 0; i < capacity_; ++i) {
            Entry& entry = table_[i];
            if (entry.IsOccupied()) {
                fn(entry.key, entry.value);
            }
        }
    }

    /**
     * Applies a function to each value.
     */
    template <typename Func>
    void ForEachValue(Func &&fn)
    {
        LockHolder lock(mutationMutex_);
        WaitForConcurrentRootScan();
        for (size_t i = 0; i < capacity_; ++i) {
            Entry& entry = table_[i];
            if (entry.IsOccupied()) {
                fn(entry.value);
            }
        }
    }

    void BeginConcurrentRootScan();

    uint32_t PrepareConcurrentRootScan(uint32_t maxTaskCount);

    void ProcessConcurrentRootScan(uint32_t threadIndex, uint32_t taskCount,
                                   const ModuleRootVisitor &visitor);

    void FinishConcurrentRootScan();

    /**
     * Removes a key-value pair from the map.
     */
    void Erase(const Key &key)
    {
        if (UNLIKELY(concurrentRootScanCheckRequired_.load(std::memory_order_relaxed))) {
            EraseWithConcurrentRootScanGate(key);
            return;
        }
        EraseImpl(key);
    }

    /**
     * Returns the number of occupied entries.
     */
    size_t Size() const
    {
        return size_;
    }

    /**
     * Clears all entries from the map and shrinks to initial capacity.
     */
    void Clear()
    {
        if (UNLIKELY(concurrentRootScanCheckRequired_.load(std::memory_order_relaxed))) {
            ClearWithConcurrentRootScanGate();
            return;
        }
        ClearImpl();
    }
};
#else
template <class Key, class Hash = std::hash<Key>, class KeyEqual = std::equal_to<Key>>
class ModuleManagerMap {
    using UnderlyingMap = CUnorderedMap<Key, GCRoot, Hash, KeyEqual>;
    struct ConcurrentRootScanContext {
        std::atomic<bool> claimed {false};
    };

    // Underlying storage with GCRoot values
    UnderlyingMap map_;
    std::atomic<bool> concurrentRootScanCheckRequired_ {false};
    mutable Mutex mutationMutex_;
    ConditionVariable concurrentRootScanCV_;
    bool concurrentRootScanActive_ {false};
    std::unique_ptr<ConcurrentRootScanContext> concurrentRootScanContext_;

    void WaitForConcurrentRootScan()
    {
        while (concurrentRootScanActive_) {
            concurrentRootScanCV_.Wait(&mutationMutex_);
        }
    }

    void InsertImpl(const Key &key, JSTaggedValue value)
    {
        map_[key] = GCRoot(value);
    }

    template <typename K>
    bool EmplaceImpl(const K &key, JSTaggedValue value)
    {
        return map_.try_emplace(key, value).second;
    }

    void EraseImpl(const Key &key)
    {
        map_.erase(key);
    }

    void ClearImpl()
    {
        map_.clear();
    }

    ARK_NOINLINE void InsertWithConcurrentRootScanGate(const Key &key, JSTaggedValue value)
    {
        LockHolder lock(mutationMutex_);
        WaitForConcurrentRootScan();
        InsertImpl(key, value);
    }

    template <typename K>
    ARK_NOINLINE bool EmplaceWithConcurrentRootScanGate(const K &key, JSTaggedValue value)
    {
        LockHolder lock(mutationMutex_);
        WaitForConcurrentRootScan();
        return EmplaceImpl(key, value);
    }

    ARK_NOINLINE void EraseWithConcurrentRootScanGate(const Key &key)
    {
        LockHolder lock(mutationMutex_);
        WaitForConcurrentRootScan();
        EraseImpl(key);
    }

    ARK_NOINLINE void ClearWithConcurrentRootScanGate()
    {
        LockHolder lock(mutationMutex_);
        WaitForConcurrentRootScan();
        ClearImpl();
    }

public:
    /**
     * Inserts or updates a key-value pair.
     * If the key already exists, replaces the existing value.
     */
    void Insert(const Key &key, JSTaggedValue value)
    {
        if (UNLIKELY(concurrentRootScanCheckRequired_.load(std::memory_order_relaxed))) {
            InsertWithConcurrentRootScanGate(key, value);
            return;
        }
        InsertImpl(key, value);
    }

    /**
     * Inserts a key-value pair only if the key doesn't exist.
     * If the key already exists, the operation is a no-op.
     * The try_emplace ensure a GCRoot constructor is only trigger
     * if the insertion actually happen
     *
     * Note: Returns bool instead of <iterator,bool>. Exposing iterators would
     * be unsafe as they can become invalid after later mutations.
     */
    template <typename K>
    bool Emplace(const K &key, JSTaggedValue value)
    {
        if (UNLIKELY(concurrentRootScanCheckRequired_.load(std::memory_order_relaxed))) {
            return EmplaceWithConcurrentRootScanGate(key, value);
        }
        return EmplaceImpl(key, value);
    }

    /**
     * Safely retrieves a value by key with readbarrier.
     *
     * The returned value is obtained via GCRoot::Read(), ensuring proper read
     * barriers are applied when needed. The value is returned by copy.
     */
    template <typename K>
    std::optional<JSTaggedValue> Find(const K &key)
    {
        auto it = map_.find(key);
        return it == map_.end() ? std::nullopt :
            std::make_optional(it->second.Read(concurrentRootScanCheckRequired_));
    }

    template <typename K>
    bool FindValue(const K &key, JSTaggedValue &value)
    {
        auto it = map_.find(key);
        if (it == map_.end()) {
            return false;
        }
        value = it->second.Read(concurrentRootScanCheckRequired_);
        return true;
    }

    /**
     * Applies a function to each key-value pair.
     */
    template <typename Func>
    void ForEach(Func &&fn)
    {
        LockHolder lock(mutationMutex_);
        WaitForConcurrentRootScan();
        for (auto it = map_.begin(); it != map_.end(); ++it) {
            fn(it);
        }
    }

    void BeginConcurrentRootScan()
    {
        ASSERT(concurrentRootScanContext_ == nullptr);
        ASSERT(!concurrentRootScanActive_);
        ASSERT(!concurrentRootScanCheckRequired_.load(std::memory_order_relaxed));
        concurrentRootScanCheckRequired_.store(true, std::memory_order_relaxed);
    }

    uint32_t PrepareConcurrentRootScan([[maybe_unused]] uint32_t maxTaskCount)
    {
        LockHolder lock(mutationMutex_);
        WaitForConcurrentRootScan();
        ASSERT(concurrentRootScanCheckRequired_.load(std::memory_order_relaxed));
        ASSERT(concurrentRootScanContext_ == nullptr);
        concurrentRootScanContext_ = std::make_unique<ConcurrentRootScanContext>();
        concurrentRootScanActive_ = true;
        return map_.empty() ? 0 : 1;
    }

    void ProcessConcurrentRootScan([[maybe_unused]] uint32_t threadIndex,
                                     [[maybe_unused]] uint32_t taskCount,
                                     const ModuleRootVisitor &visitor)
    {
        ConcurrentRootScanContext *context = concurrentRootScanContext_.get();
        ASSERT(context != nullptr);
        if (context->claimed.exchange(true, std::memory_order_relaxed)) {
            return;
        }
        for (auto it = map_.begin(); it != map_.end(); ++it) {
            visitor(it->second);
        }
        return;
    }

    void FinishConcurrentRootScan()
    {
        LockHolder lock(mutationMutex_);
        ASSERT(concurrentRootScanActive_);
        ASSERT(concurrentRootScanCheckRequired_.load(std::memory_order_relaxed));
        ASSERT(concurrentRootScanContext_ != nullptr);
        concurrentRootScanContext_.reset();
        concurrentRootScanActive_ = false;
        concurrentRootScanCheckRequired_.store(false, std::memory_order_relaxed);
        concurrentRootScanCV_.SignalAll();
    }

    void Erase(const Key &key)
    {
        if (UNLIKELY(concurrentRootScanCheckRequired_.load(std::memory_order_relaxed))) {
            EraseWithConcurrentRootScanGate(key);
            return;
        }
        EraseImpl(key);
    }

    size_t Size() const
    {
        return map_.size();
    }

    void Clear()
    {
        if (UNLIKELY(concurrentRootScanCheckRequired_.load(std::memory_order_relaxed))) {
            ClearWithConcurrentRootScanGate();
            return;
        }
        ClearImpl();
    }
};
#endif // ENABLE_LATEST_OPTIMIZATION
}  // namespace panda::ecmascript

#include "ecmascript/module/module_manager_map_localcc-inl.h"
#endif
