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

#ifndef ECMASCRIPT_DFX_CPU_PROFILER_PROFILER_SIGNAL_CONTEXT_H
#define ECMASCRIPT_DFX_CPU_PROFILER_PROFILER_SIGNAL_CONTEXT_H

#include <atomic>
#include <cstdint>

#include "ecmascript/dfx/cpu_profiler/raw_samples_queue.h"

namespace panda::ecmascript {
class CpuProfiler;
class JSThread;
class AnFileDataManager;
class CrossVMOperator;

enum class ProfilerSignalState : uint8_t {
    STOPPED = 0,
    STARTING,
    RUNNING,
    STOPPING,
};

// Per-profiler state published BEFORE the first SIGPROF can arrive and read by the
// signal handler without any lock. All members are lock-free atomics or immutable
// during a RUNNING session (plan §5.5/§8.1):
//   - state/generation/pending bit/counters: acquire/release atomics;
//   - queue pointer: fixed for the whole profiler lifetime (owned by CpuProfiler).
// The context must outlive any handler that may still read it; destruction is only
// allowed after the signal handler is unregistered or activeHandlerCount has drained.
struct CpuProfilerSignalContext {
    std::atomic<uint8_t> profilerState {static_cast<uint8_t>(ProfilerSignalState::STOPPED)};
    std::atomic<uint32_t> generation {0};
    std::atomic<bool> shouldRecordSample {false};
    // Diagnostics/lifecycle only: never used to block or cancel the handler.
    std::atomic<bool> handlerRunning {false};
    std::atomic<uint32_t> activeHandlerCount {0};
    // Request timestamp written by the sending thread (plan §5.2: approximate time).
    std::atomic<uint64_t> requestTimestamp {0};
    // Signal-safe drop counters, preallocated; written by the handler and the
    // processor, reset at Start and reported once at Stop (LogSamplingStats).
    std::atomic<uint32_t> dropQueueFull {0};
    std::atomic<uint32_t> dropGcSkipped {0};
    std::atomic<uint32_t> dropStackNotIterable {0};
    std::atomic<uint32_t> dropInvalidFrame {0};
    std::atomic<uint32_t> samplesRecorded {0};
    std::atomic<uint32_t> handlerOverrunTicks {0};
    std::atomic<uint32_t> dropMetadataStale {0};
    // Immutable for the whole profiler lifetime; published before the first signal.
    CpuProfiler *profiler {nullptr};
    JSThread *jsThread {nullptr};
    // Cached at Start so the handler never routes through the lazily-initialized
    // AnFileDataManager::GetInstance() magic static (plan §6.1 bans lazy init on
    // the handler path, even though warm-up ordering would currently save us).
    AnFileDataManager *anFileDataManager {nullptr};
    // Cached at Start: hybrid stacks are routed to the legacy in-handler
    // processing (temporary signal-safety debt, see CollectSampleInto).
    bool crossVmOperatorPresent {false};
    CrossVMOperator *crossVMOperator {nullptr};
    // Sample identity (FFRT task id or tid), captured at Start in normal
    // context: the handler must not call FFRT per sample.
    uint64_t threadOrTaskId {0};
    // Points at the metadata registry's published-sequence atomic (process-lifetime
    // singleton storage); the handler performs one lock-free acquire load.
    std::atomic<uint64_t> *metadataSequence {nullptr};
    // Fixed for the profiler lifetime; the handler never frees it.
    RawSamplesQueue *queue {nullptr};

    bool IsRunning() const
    {
        return profilerState.load(std::memory_order_acquire) == static_cast<uint8_t>(ProfilerSignalState::RUNNING);
    }
};

// Process-lifetime count of rebuild-collision Lookup misses; global because it
// cannot be reset while other profilers are mid-session. The accessor is
// non-inline: inline variables are not shared across the test DSO boundary.
inline std::atomic<uint32_t> g_profilerSignalTableMisses {0};
uint32_t GetProfilerSignalTableMisses();

// Fixed-size open-addressing table mapping thread/task id -> signal context.
// Writers (profiler start/stop and the sampling processor rebinding) run in normal
// thread context; readers run inside the SIGPROF handler and perform lock-free
// linear probing only (plan §8.2: the handler must not touch a mutex-guarded map).
class ProfilerSignalContextTable {
public:
    static constexpr size_t TABLE_CAPACITY = 32; // 32 slots are enough for VM/worker profiling
    static constexpr uint64_t EMPTY_KEY = 0;

    ProfilerSignalContextTable() = default;

    // Normal context only. Publishing the same tid twice overwrites the binding.
    void Publish(uint64_t tid, CpuProfilerSignalContext *ctx)
    {
        size_t pos = Hash(tid);
        for (size_t probe = 0; probe < TABLE_CAPACITY; probe++) {
            Slot &slot = slots_[pos];
            uint64_t key = slot.key.load(std::memory_order_acquire);
            if (key == EMPTY_KEY || key == tid) {
                slot.ctx.store(ctx, std::memory_order_release);
                slot.key.store(tid, std::memory_order_release);
                return;
            }
            pos = (pos + 1) % TABLE_CAPACITY;
        }
        // Table full: profiling more threads than capacity is unsupported; the
        // start path checks IsFull and refuses to start (see CpuProfiler::Start*).
    }

    // Normal context only. Removes the binding for tid if it currently maps to ctx.
    void Unpublish(uint64_t tid, CpuProfilerSignalContext *ctx)
    {
        size_t pos = Hash(tid);
        for (size_t probe = 0; probe < TABLE_CAPACITY; probe++) {
            Slot &slot = slots_[pos];
            uint64_t key = slot.key.load(std::memory_order_acquire);
            if (key == EMPTY_KEY) {
                return;
            }
            if (key == tid) {
                if (slot.ctx.load(std::memory_order_acquire) == ctx) {
                    slot.ctx.store(nullptr, std::memory_order_release);
                    slot.key.store(EMPTY_KEY, std::memory_order_release);
                }
                return;
            }
            pos = (pos + 1) % TABLE_CAPACITY;
        }
    }

    // Normal context only. Removes every binding owned by ctx and rebuilds the table
    // so linear-probe chains of other profilers stay intact (clearing a slot in the
    // middle of a chain would make later entries unreachable).
    void UnpublishContext(CpuProfilerSignalContext *ctx)
    {
        uint64_t keys[TABLE_CAPACITY];
        CpuProfilerSignalContext *contexts[TABLE_CAPACITY];
        size_t liveCount = 0;
        for (size_t i = 0; i < TABLE_CAPACITY; i++) {
            uint64_t key = slots_[i].key.load(std::memory_order_acquire);
            if (key != EMPTY_KEY && slots_[i].ctx.load(std::memory_order_acquire) != ctx) {
                keys[liveCount] = key;
                contexts[liveCount] = slots_[i].ctx.load(std::memory_order_acquire);
                liveCount++;
            }
            slots_[i].ctx.store(nullptr, std::memory_order_relaxed);
            slots_[i].key.store(EMPTY_KEY, std::memory_order_relaxed);
        }
        for (size_t i = 0; i < liveCount; i++) {
            Publish(keys[i], contexts[i]);
        }
    }

    // Signal-context safe: lock-free linear probe keyed by the current thread/task id.
    // The slot content is re-validated after loading the context because a concurrent
    // UnpublishContext rebuild may have re-homed another key at this slot (Oracle
    // review F3): without the re-check the handler could dispatch to a wrong
    // profiler's context in multi-VM setups.
    CpuProfilerSignalContext *Lookup(uint64_t tid) const
    {
        for (int attempt = 0; attempt < 2; attempt++) { // 2: bounded revalidate rounds
            bool slotMoved = false;
            CpuProfilerSignalContext *ctx = ProbeOnce(tid, &slotMoved);
            if (!slotMoved) {
                return ctx;
            }
        }
        // Both rounds raced a rebuild and the caller drops this lookup; a plain
        // "tid not bound" miss is normal traffic and stays uncounted.
        g_profilerSignalTableMisses.fetch_add(1, std::memory_order_relaxed);
        return nullptr;
    }

    bool HasFreeSlot(uint64_t tid) const
    {
        size_t pos = Hash(tid);
        for (size_t probe = 0; probe < TABLE_CAPACITY; probe++) {
            uint64_t key = slots_[pos].key.load(std::memory_order_acquire);
            if (key == EMPTY_KEY || key == tid) {
                return true;
            }
            pos = (pos + 1) % TABLE_CAPACITY;
        }
        return false;
    }

private:
    // One probing pass; sets *slotMoved when the slot was rebuilt concurrently
    // mid-probe (the caller restarts) and returns nullptr-with-no-restart when
    // the key is absent.
    CpuProfilerSignalContext *ProbeOnce(uint64_t tid, bool *slotMoved) const
    {
        size_t pos = Hash(tid);
        for (size_t probe = 0; probe < TABLE_CAPACITY; probe++) {
            const Slot &slot = slots_[pos];
            uint64_t key = slot.key.load(std::memory_order_acquire);
            if (key == EMPTY_KEY) {
                *slotMoved = false;
                return nullptr;
            }
            if (key == tid) {
                CpuProfilerSignalContext *ctx = slot.ctx.load(std::memory_order_acquire);
                *slotMoved = slot.key.load(std::memory_order_acquire) != tid;
                return ctx;
            }
            pos = (pos + 1) % TABLE_CAPACITY;
        }
        *slotMoved = false;
        return nullptr;
    }

private:
    static constexpr uint64_t HASH_GOLDEN_RATIO = 0x9E3779B97F4A7C15ULL;
    static constexpr int HASH_HIGH_BITS_SHIFT = 40; // 40: fold on the product's high bits

    static size_t Hash(uint64_t tid)
    {
        return static_cast<size_t>((tid * HASH_GOLDEN_RATIO) >> HASH_HIGH_BITS_SHIFT) % TABLE_CAPACITY;
    }

    struct Slot {
        std::atomic<uint64_t> key {EMPTY_KEY};
        std::atomic<CpuProfilerSignalContext *> ctx {nullptr};
    };

    Slot slots_[TABLE_CAPACITY] {};

    NO_COPY_SEMANTIC(ProfilerSignalContextTable);
    NO_MOVE_SEMANTIC(ProfilerSignalContextTable);
};

// Namespace-scope constant-initialized instance: the handler reaches it without
// any magic-static guard (__cxa_guard_acquire is denylisted by plan §6.5/§6.1).
inline ProfilerSignalContextTable g_profilerSignalContextTable;
} // namespace panda::ecmascript
#endif // ECMASCRIPT_DFX_CPU_PROFILER_PROFILER_SIGNAL_CONTEXT_H
