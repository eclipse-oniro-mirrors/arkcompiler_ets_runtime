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

#ifndef ECMASCRIPT_DFX_CPU_PROFILER_PROFILER_METADATA_REGISTRY_H
#define ECMASCRIPT_DFX_CPU_PROFILER_PROFILER_METADATA_REGISTRY_H

#include <atomic>
#include <memory>

#include "ecmascript/jspandafile/js_pandafile.h"
#include "ecmascript/mem/c_containers.h"
#include "ecmascript/platform/mutex.h"

namespace panda::ecmascript {
// Phase 1 code-lifecycle registry (plan §5.4/§7.2): JSPandaFile load/remove events
// with a monotonically increasing sequence. Producers are loader/runtime threads
// (normal context, mutex-serialized); the SIGPROF handler only performs one
// lock-free acquire load of the published sequence; the consumer is the profiler
// processor thread, which applies events in order before symbolizing samples.
//
// Unloaded files are kept alive via shared_ptr keep-alives until the last profiler
// deactivates, so a sample captured just before an unload can still be symbolized
// (or dropped as METADATA_STALE) without touching freed memory.
class ProfilerMetadataRegistry {
public:
    struct Event {
        // 1-based position in the event log; equals the number of published events.
        uint64_t sequence {0};
        bool isDelete {false};
        const JSPandaFile *file {nullptr};
    };

    static ProfilerMetadataRegistry &Instance()
    {
        static ProfilerMetadataRegistry instance;
        return instance;
    }

    // CpuProfiler start/stop pairing (normal context).
    void Activate()
    {
        LockHolder lock(mutex_);
        profilerCount_++;
    }

    // Returns true when the last profiler deactivated (caller may log once).
    bool Deactivate()
    {
        LockHolder lock(mutex_);
        if (profilerCount_ > 0) {
            profilerCount_--;
        }
        if (profilerCount_ == 0) {
            // Every profiler has drained its samples: no in-flight sample can still
            // reference an unloaded file.
            events_.clear();
            keepAlives_.clear();
            publishedSequence_.store(0, std::memory_order_release);
            return true;
        }
        return false;
    }

    // Loader-thread hooks: one atomic branch of overhead when no profiler runs.
    void OnLoad(const JSPandaFile *file)
    {
        if (profilerCount_.load(std::memory_order_acquire) == 0) {
            return;
        }
        LockHolder lock(mutex_);
        AppendEventLocked(false, file, nullptr);
    }

    void OnUnload(const JSPandaFile *file, std::shared_ptr<JSPandaFile> keepAlive)
    {
        if (profilerCount_.load(std::memory_order_acquire) == 0) {
            return;
        }
        LockHolder lock(mutex_);
        AppendEventLocked(true, file, std::move(keepAlive));
    }

    // Handler-side sequence source; the pointer is stable for the process lifetime.
    std::atomic<uint64_t> *PublishedSequence()
    {
        return &publishedSequence_;
    }

    // Processor-side: copies events after the cursor; returns the new event count.
    uint64_t GetEvents(uint64_t cursor, CVector<Event> &out)
    {
        LockHolder lock(mutex_);
        for (uint64_t i = cursor; i < events_.size(); i++) {
            out.push_back(events_[i]);
        }
        return events_.size();
    }

private:
    ProfilerMetadataRegistry() = default;

    void AppendEventLocked(bool isDelete, const JSPandaFile *file, std::shared_ptr<JSPandaFile> keepAlive)
    {
        Event event;
        event.sequence = events_.size() + 1; // 1-based
        event.isDelete = isDelete;
        event.file = file;
        events_.push_back(event);
        if (isDelete && keepAlive != nullptr) {
            keepAlives_.push_back(std::move(keepAlive));
        }
        publishedSequence_.store(event.sequence, std::memory_order_release);
    }

    Mutex mutex_;
    // sequence is index + 1, so the log is append-only while any profiler is active.
    CVector<Event> events_;
    CVector<std::shared_ptr<JSPandaFile>> keepAlives_;
    std::atomic<uint64_t> publishedSequence_ {0};
    std::atomic<uint32_t> profilerCount_ {0};

    NO_COPY_SEMANTIC(ProfilerMetadataRegistry);
    NO_MOVE_SEMANTIC(ProfilerMetadataRegistry);
};
} // namespace panda::ecmascript
#endif // ECMASCRIPT_DFX_CPU_PROFILER_PROFILER_METADATA_REGISTRY_H
