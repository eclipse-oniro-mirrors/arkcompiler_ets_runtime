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
#ifndef ECMASCRIPT_ARKSTEED_SNAPSHOT_TABLE_H
#define ECMASCRIPT_ARKSTEED_SNAPSHOT_TABLE_H

#include <cstdint>
#include <deque>
#include <limits>
#include <vector>

#include "libpandabase/macros.h"

namespace panda::ecmascript::arksteed {

// Stable entry keys, a shared change log, and cached predecessor values.
// Get/GetPredecessorValue never switch snapshots.
template <class Value>
class SnapshotTable {
    static constexpr uint32_t NO_MERGE = std::numeric_limits<uint32_t>::max();
    struct Entry {
        Value value;
        uint32_t mergeOffset {NO_MERGE};
        uint32_t lastPredecessor {NO_MERGE};
    };
    struct Change {
        Entry *entry;
        Value oldValue;
        Value newValue;
    };

public:
    struct Key {
        Entry *entry;
        bool operator==(Key other) const { return entry == other.entry; }
    };
    struct Snapshot {
        const Snapshot *parent;
        size_t depth;
        size_t logBegin;
        size_t logEnd;
    };

    SnapshotTable() = default;
    SnapshotTable(const SnapshotTable &) = delete;
    SnapshotTable &operator=(const SnapshotTable &) = delete;
    SnapshotTable(SnapshotTable &&) = delete;
    SnapshotTable &operator=(SnapshotTable &&) = delete;

    Key NewKey(Value initial = Value {})
    {
        entries_.push_back({initial});
        return {&entries_.back()};
    }

    Value Get(Key key) const { return key.entry->value; }

    void Set(Key key, Value value)
    {
        if (key.entry->value != value) {
            log_.push_back({key.entry, key.entry->value, value});
            key.entry->value = value;
        }
    }

    const Snapshot *Seal()
    {
        if (activeLogBegin_ != log_.size()) {
            snapshots_.push_back({current_, current_->depth + 1, activeLogBegin_, log_.size()});
            current_ = &snapshots_.back();
            activeLogBegin_ = log_.size();
        }
        return current_;
    }

    Value GetPredecessorValue(Key key, size_t predecessorIndex) const
    {
        ASSERT(predecessorIndex < predecessorCount_);
        return key.entry->mergeOffset == NO_MERGE
            ? Get(key) : mergeValues_[key.entry->mergeOffset + predecessorIndex];
    }

    template <class Merge>
    void StartNewSnapshot(const std::vector<const Snapshot *> &predecessors, Merge merge)
    {
        Seal();
        for (Entry *entry : mergingEntries_) {
            entry->mergeOffset = NO_MERGE;
            entry->lastPredecessor = NO_MERGE;
        }
        mergingEntries_.clear();
        mergeValues_.clear();
        predecessorCount_ = predecessors.size();
        ASSERT(predecessorCount_ < NO_MERGE);
        if (predecessors.empty()) {
            SwitchTo(&root_);
            return;
        }
        const Snapshot *common = predecessors[0];
        for (const Snapshot *predecessor : predecessors) {
            common = CommonAncestor(common, predecessor);
        }
        SwitchTo(common);
        // Walk changes backwards: the first value encountered is the final
        // value on this predecessor. Unchanged paths retain the ancestor value.
        for (size_t i = 0; i < predecessors.size(); ++i) {
            for (auto *snapshot = predecessors[i]; snapshot != common; snapshot = snapshot->parent) {
                for (size_t j = snapshot->logEnd; j > snapshot->logBegin; --j) {
                    RecordPredecessorValue(log_[j - 1], i);
                }
            }
        }
        incoming_.resize(predecessors.size());
        for (Entry *entry : mergingEntries_) {
            bool equal = true;
            for (size_t i = 0; i < predecessors.size(); ++i) {
                incoming_[i] = mergeValues_[entry->mergeOffset + i];
                equal &= incoming_[i] == incoming_[0];
            }
            Key key {entry};
            Set(key, equal ? incoming_[0] : merge(key, incoming_));
        }
    }

  private:
    void RecordPredecessorValue(const Change &change, size_t predecessorIndex)
    {
        Entry *entry = change.entry;
        if (entry->mergeOffset == NO_MERGE) {
            ASSERT(mergeValues_.size() < NO_MERGE);
            entry->mergeOffset = static_cast<uint32_t>(mergeValues_.size());
            mergeValues_.insert(mergeValues_.end(), predecessorCount_, entry->value);
            mergingEntries_.push_back(entry);
        }
        if (entry->lastPredecessor != predecessorIndex) {
            mergeValues_[entry->mergeOffset + predecessorIndex] = change.newValue;
            entry->lastPredecessor = static_cast<uint32_t>(predecessorIndex);
        }
    }

    static const Snapshot *CommonAncestor(const Snapshot *left, const Snapshot *right)
    {
        while (left->depth > right->depth) {
            left = left->parent;
        }
        while (right->depth > left->depth) {
            right = right->parent;
        }
        while (left != right) {
            left = left->parent;
            right = right->parent;
        }
        return left;
    }

    void SwitchTo(const Snapshot *target)
    {
        ASSERT(activeLogBegin_ == log_.size());
        const Snapshot *common = CommonAncestor(current_, target);
        for (auto *snapshot = current_; snapshot != common; snapshot = snapshot->parent) {
            for (size_t i = snapshot->logEnd; i > snapshot->logBegin; --i) {
                const Change &change = log_[i - 1];
                change.entry->value = change.oldValue;
            }
        }
        path_.clear();
        for (auto *snapshot = target; snapshot != common; snapshot = snapshot->parent) {
            path_.push_back(snapshot);
        }
        for (auto it = path_.rbegin(); it != path_.rend(); ++it) {
            for (size_t i = (*it)->logBegin; i < (*it)->logEnd; ++i) {
                const Change &change = log_[i];
                change.entry->value = change.newValue;
            }
        }
        current_ = target;
    }

    Snapshot root_ {nullptr, 0, 0, 0};
    const Snapshot *current_ {&root_};
    size_t activeLogBegin_ {0};
    size_t predecessorCount_ {0};
    std::deque<Entry> entries_;
    std::deque<Snapshot> snapshots_;
    std::vector<Change> log_;
    std::vector<Entry *> mergingEntries_;
    std::vector<Value> mergeValues_;
    std::vector<Value> incoming_;
    std::vector<const Snapshot *> path_;
};
}  // namespace panda::ecmascript::arksteed
#endif  // ECMASCRIPT_ARKSTEED_SNAPSHOT_TABLE_H
