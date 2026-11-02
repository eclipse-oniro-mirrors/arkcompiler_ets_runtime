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

#ifndef ECMASCRIPT_MODULE_MANAGER_MAP_LOCALCC_INL_H
#define ECMASCRIPT_MODULE_MANAGER_MAP_LOCALCC_INL_H

#include "ecmascript/module/module_manager_map_localcc.h"

#if ENABLE_LATEST_OPTIMIZATION
namespace panda::ecmascript {
template <class Key, class Hash, class KeyEqual>
inline void ModuleManagerMap<Key, Hash, KeyEqual>::WaitForConcurrentRootScan()
{
    while (concurrentRootScanActive_) {
        concurrentRootScanCV_.Wait(&mutationMutex_);
    }
}

template <class Key, class Hash, class KeyEqual>
void ModuleManagerMap<Key, Hash, KeyEqual>::InsertImpl(const Key &key, JSTaggedValue value)
{
    // Check if expansion is needed
    if (UNLIKELY(size_ >= capacity_ - (capacity_ >> MAX_LOAD_FACTOR_SHIFT))) {
        Rehash(capacity_ * GROW_FACTOR);
    }
    size_t hash = hasher_(key) & HASH_MASK;
    bool found;
    size_t pos = FindInsertPosition(key, hash, found);
    if (pos != EMPTY_MARK) {
        Entry& entry = table_[pos];
        if (found) {
            entry.value = GCRoot(value);
        } else {
            // If inserting into deleted position, decrease deletion count
            if (entry.IsDeleted()) {
                deletedCount_--;
            }
            entry.SetOccupied(hash);
            entry.key = key;
            entry.value = GCRoot(value);
            size_++;
        }
    }
}

template <class Key, class Hash, class KeyEqual>
template <typename K>
bool ModuleManagerMap<Key, Hash, KeyEqual>::EmplaceImpl(const K &key, JSTaggedValue value)
{
    if (UNLIKELY(size_ >= capacity_ - (capacity_ >> MAX_LOAD_FACTOR_SHIFT))) {
        Rehash(capacity_ * GROW_FACTOR);
    }
    size_t hash = hasher_(key) & HASH_MASK;
    bool found;
    size_t pos = FindInsertPosition(key, hash, found);
    if (found || pos == EMPTY_MARK) {
        return false;
    }
    Entry& entry = table_[pos];
    if (entry.IsDeleted()) {
        deletedCount_--;
    }
    entry.SetOccupied(hash);
    entry.key = key;
    entry.value = GCRoot(value);
    size_++;
    return true;
}

template <class Key, class Hash, class KeyEqual>
void ModuleManagerMap<Key, Hash, KeyEqual>::EraseImpl(const Key &key)
{
    size_t pos = FindPosition(key);
    if (pos == EMPTY_MARK) {
        return;
    }
    Entry& entry = table_[pos];
    entry.SetDeleted();
    entry.key = Key();
    entry.value = GCRoot();
    size_--;
    deletedCount_++;

    // Check if we should shrink
    if (UNLIKELY(ShouldShrink())) {
        Rehash(capacity_ / GROW_FACTOR); // Check if need to clean up deletions
    } else if (UNLIKELY(TooManyDeletions())) {
        Rehash(capacity_);
    }
}

template <class Key, class Hash, class KeyEqual>
inline void ModuleManagerMap<Key, Hash, KeyEqual>::ClearImpl()
{
    // Always shrink to initial capacity when clearing
    if (capacity_ != INITIAL_CAPACITY) {
        table_ = std::make_unique<Entry[]>(INITIAL_CAPACITY);
        capacity_ = INITIAL_CAPACITY;
    } else {
        // If already at initial capacity, just clear entries
        for (size_t i = 0; i < capacity_; ++i) {
            table_[i].SetEmpty();
        }
    }
    size_ = 0;
    deletedCount_ = 0;
}

template <class Key, class Hash, class KeyEqual>
ARK_NOINLINE inline void ModuleManagerMap<Key, Hash, KeyEqual>::InsertWithConcurrentRootScanGate(
    const Key &key, JSTaggedValue value)
{
    LockHolder lock(mutationMutex_);
    WaitForConcurrentRootScan();
    InsertImpl(key, value);
}

template <class Key, class Hash, class KeyEqual>
template <typename K>
ARK_NOINLINE inline bool ModuleManagerMap<Key, Hash, KeyEqual>::EmplaceWithConcurrentRootScanGate(
    const K &key, JSTaggedValue value)
{
    LockHolder lock(mutationMutex_);
    WaitForConcurrentRootScan();
    return EmplaceImpl(key, value);
}

template <class Key, class Hash, class KeyEqual>
ARK_NOINLINE inline void ModuleManagerMap<Key, Hash, KeyEqual>::EraseWithConcurrentRootScanGate(const Key &key)
{
    LockHolder lock(mutationMutex_);
    WaitForConcurrentRootScan();
    EraseImpl(key);
}

template <class Key, class Hash, class KeyEqual>
ARK_NOINLINE inline void ModuleManagerMap<Key, Hash, KeyEqual>::ClearWithConcurrentRootScanGate()
{
    LockHolder lock(mutationMutex_);
    WaitForConcurrentRootScan();
    ClearImpl();
}

template <class Key, class Hash, class KeyEqual>
template <typename K>
size_t ModuleManagerMap<Key, Hash, KeyEqual>::FindPosition(const K& key) const
{
    if (UNLIKELY(size_ == 0)) {
        return EMPTY_MARK;
    }
    size_t hash = hasher_(key) & HASH_MASK;
    // assert capacity_ is 2^n
    ASSERT((capacity_ & (capacity_ - 1)) == 0);
    size_t mask = capacity_ - 1;
    size_t index = hash & mask;
    size_t startIndex = index;
    do {
        Entry& entry = table_[index];
        if (entry.IsOccupied() && entry.FastEqual(key, hash, keyEqual_)) {
            return index;
        }
        // Empty state means key doesn't exist
        if (entry.IsEmpty()) {
            break;
        }
        // Deleted state, continue probing
        index = (index + 1) & mask;
    } while (index != startIndex);
    return EMPTY_MARK;
}

template <class Key, class Hash, class KeyEqual>
template <typename K>
size_t ModuleManagerMap<Key, Hash, KeyEqual>::FindInsertPosition(const K& key, size_t hash, bool& found) const
{
    found = false;
    // assert capacity_ is 2^n
    ASSERT((capacity_ & (capacity_ - 1)) == 0);
    size_t mask = capacity_ - 1;
    size_t index = hash & mask;
    size_t startIndex = index;
    size_t firstDeleted = EMPTY_MARK;
    do {
        Entry& entry = table_[index];
        if (entry.IsOccupied()) {
            if (entry.FastEqual(key, hash, keyEqual_)) {
                found = true;
                return index;
            }
        } else if (entry.IsDeleted()) {
            // Record first deleted position
            if (firstDeleted == EMPTY_MARK) {
                firstDeleted = index;
            }
        } else if (entry.IsEmpty()) {
            // Prefer deleted position, otherwise use empty position
            return (firstDeleted != EMPTY_MARK) ? firstDeleted : index;
        }
        index = (index + 1) & mask;
    } while (index != startIndex);
    // Table full (shouldn't happen due to resizing)
    return EMPTY_MARK;
}

template <class Key, class Hash, class KeyEqual>
void ModuleManagerMap<Key, Hash, KeyEqual>::Rehash(size_t newCapacity)
{
    // Round up to power of two
    size_t roundedCapacity = 1;
    while (roundedCapacity < newCapacity) {
        roundedCapacity <<= 1;
    }
    newCapacity = roundedCapacity;
    // Ensure minimum capacity
    if (newCapacity < MIN_CAPACITY) {
        newCapacity = MIN_CAPACITY;
    }
    ASSERT((newCapacity & (newCapacity - 1)) == 0);
    auto newTable = std::make_unique<Entry[]>(newCapacity);
    // Reinsert occupied elements (skip deleted states)
    for (size_t i = 0; i < capacity_; ++i) {
        Entry& oldEntry = table_[i];
        if (oldEntry.IsOccupied()) {
            size_t hash = oldEntry.GetHash();
            ASSERT(newCapacity != 0);
            size_t newMask = newCapacity - 1;
            size_t newIndex = hash & newMask;
            while (newTable[newIndex].IsOccupied()) {
                newIndex = (newIndex + 1) & newMask;
            }
            newTable[newIndex] = std::move(oldEntry);
        }
    }
    table_ = std::move(newTable);
    capacity_ = newCapacity;
    deletedCount_ = 0;
}

template <class Key, class Hash, class KeyEqual>
inline bool ModuleManagerMap<Key, Hash, KeyEqual>::ShouldShrink() const
{
    // Don't shrink below minimum capacity
    if (capacity_ <= MIN_CAPACITY * GROW_FACTOR) {
        return false;
    }
    return size_ < (capacity_ >> MIN_LOAD_FACTOR_SHIFT);
}

template <class Key, class Hash, class KeyEqual>
inline bool ModuleManagerMap<Key, Hash, KeyEqual>::TooManyDeletions() const
{
    if (capacity_ == 0) return false;
    double deletionRatio = static_cast<double>(deletedCount_) / capacity_;
    return deletionRatio > DELETION_THRESHOLD;
}

template <class Key, class Hash, class KeyEqual>
inline void ModuleManagerMap<Key, Hash, KeyEqual>::ForEachValueRange(size_t begin, size_t end,
    const ModuleRootVisitor &visitor)
{
    for (size_t i = begin; i < end; ++i) {
        Entry& entry = table_[i];
        if (entry.IsOccupied()) {
            visitor(entry.value);
        }
    }
}

template <class Key, class Hash, class KeyEqual>
void ModuleManagerMap<Key, Hash, KeyEqual>::ForEachValueBlocks(ConcurrentRootScanContext &context,
    const ModuleRootVisitor &visitor)
{
    while (true) {
        size_t begin = context.nextIndex.fetch_add(context.blockSize, std::memory_order_relaxed);
        if (begin >= context.capacity) {
            return;
        }
        ForEachValueRange(begin, std::min(begin + context.blockSize, context.capacity), visitor);
    }
}

template <class Key, class Hash, class KeyEqual>
inline void ModuleManagerMap<Key, Hash, KeyEqual>::BeginConcurrentRootScan()
{
    ASSERT(concurrentRootScanContext_ == nullptr);
    ASSERT(!concurrentRootScanActive_);
    ASSERT(!concurrentRootScanCheckRequired_.load(std::memory_order_relaxed));
    concurrentRootScanCheckRequired_.store(true, std::memory_order_relaxed);
}

template <class Key, class Hash, class KeyEqual>
uint32_t ModuleManagerMap<Key, Hash, KeyEqual>::PrepareConcurrentRootScan(uint32_t maxTaskCount)
{
    LockHolder lock(mutationMutex_);
    WaitForConcurrentRootScan();
    ASSERT(concurrentRootScanCheckRequired_.load(std::memory_order_relaxed));
    ASSERT(concurrentRootScanContext_ == nullptr);

    uint32_t actualTaskCount = 0;
    if (size_ != 0) {
        uint32_t taskCountBySize = static_cast<uint32_t>(
            (capacity_ + PARALLEL_MIN_ENTRIES_PER_TASK - 1) / PARALLEL_MIN_ENTRIES_PER_TASK);
        actualTaskCount = std::min(std::max(1U, maxTaskCount), taskCountBySize);
    }
    size_t divisor = std::max<size_t>(1, actualTaskCount);
    size_t blockSize = std::min(PARALLEL_BLOCK_ENTRIES, (capacity_ + divisor - 1) / divisor);
    concurrentRootScanContext_ = std::make_unique<ConcurrentRootScanContext>(capacity_, blockSize);
    concurrentRootScanActive_ = true;
    return actualTaskCount;
}

template <class Key, class Hash, class KeyEqual>
inline void ModuleManagerMap<Key, Hash, KeyEqual>::ProcessConcurrentRootScan(uint32_t threadIndex, uint32_t taskCount,
    const ModuleRootVisitor &visitor)
{
    ConcurrentRootScanContext *context = concurrentRootScanContext_.get();
    ASSERT(context != nullptr);
    ForEachValueBlocks(*context, visitor);
    return;
}

template <class Key, class Hash, class KeyEqual>
inline void ModuleManagerMap<Key, Hash, KeyEqual>::FinishConcurrentRootScan()
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
}  // namespace panda::ecmascript
#endif  // ENABLE_LATEST_OPTIMIZATION
#endif  // ECMASCRIPT_MODULE_MANAGER_MAP_LOCALCC_INL_H
