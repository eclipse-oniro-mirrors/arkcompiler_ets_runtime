/**
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

#ifndef ECMASCRIPT_DFX_CPU_PROFILER_RAW_SAMPLES_QUEUE_H
#define ECMASCRIPT_DFX_CPU_PROFILER_RAW_SAMPLES_QUEUE_H

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <new>
#include <type_traits>

#include "libpandabase/macros.h"

#include "ecmascript/dfx/cpu_profiler/raw_sample.h"

namespace panda::ecmascript {
constexpr size_t CACHE_LINE_SIZE = 64;

// Preallocated single-producer single-consumer ring buffer for RawSample.
// Contract (plan §5.3):
//   - the SIGPROF handler is the ONLY producer and never blocks;
//   - the sampling processor thread is the ONLY consumer;
//   - a full queue drops the sample instead of waiting;
//   - slot ownership transitions EMPTY -> FULL (producer, release) and
//     FULL -> EMPTY (consumer, release), each performed by exactly one side,
//     so no intermediate claiming state is required.
// The queue itself is not copyable and must outlive every handler that can
// still touch it (see CpuProfilerSignalContext lifecycle rules).
class RawSamplesQueue {
public:
    static constexpr size_t QUEUE_CAPACITY = 128; // 128 slots * ~3.3 KiB ≈ 424 KiB per profiler

    RawSamplesQueue() = default;
    ~RawSamplesQueue() = default;

    NO_COPY_SEMANTIC(RawSamplesQueue);
    NO_MOVE_SEMANTIC(RawSamplesQueue);

    // Producer side (signal context). Returns nullptr when the queue is full;
    // the caller then drops the sample and counts it.
    RawSample *TryStartEnqueue()
    {
        if (entries_[producerPos_].state.load(std::memory_order_acquire) != EMPTY) {
            return nullptr;
        }
        return &entries_[producerPos_].sample;
    }

    // Producer side (signal context). Publishes the slot returned by the matching
    // TryStartEnqueue call. Valid because exactly one producer exists, so
    // producerPos_ cannot move between the two calls.
    void FinishEnqueue()
    {
        entries_[producerPos_].state.store(FULL, std::memory_order_release);
        producerPos_ = (producerPos_ + 1) % QUEUE_CAPACITY;
    }

    // Consumer side (processor thread). Returns false when the queue is empty.
    bool TryPop(RawSample &out)
    {
        if (entries_[consumerPos_].state.load(std::memory_order_acquire) != FULL) {
            return false;
        }
        out = entries_[consumerPos_].sample;
        entries_[consumerPos_].state.store(EMPTY, std::memory_order_release);
        consumerPos_ = (consumerPos_ + 1) % QUEUE_CAPACITY;
        return true;
    }

private:
    static constexpr uint8_t EMPTY = 0;
    static constexpr uint8_t FULL = 1;

    struct alignas(CACHE_LINE_SIZE) Entry {
        RawSample sample;
        std::atomic<uint8_t> state {EMPTY};
    };

    static_assert(sizeof(Entry) * QUEUE_CAPACITY <= 1024 * 1024); // plan §10.2: ≤ 1 MiB

    Entry entries_[QUEUE_CAPACITY] {};
    size_t producerPos_ {0};
    size_t consumerPos_ {0};
};
} // namespace panda::ecmascript
#endif // ECMASCRIPT_DFX_CPU_PROFILER_RAW_SAMPLES_QUEUE_H
