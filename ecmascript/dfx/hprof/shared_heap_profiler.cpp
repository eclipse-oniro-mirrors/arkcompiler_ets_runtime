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

#include "ecmascript/dfx/hprof/shared_heap_profiler.h"

#include "ecmascript/dfx/hprof/heap_profiler_interface.h"
#include "ecmascript/log_wrapper.h"

namespace panda::ecmascript {

std::atomic<size_t> SharedHeapProfiler::activeTrackerCount_ {0};

SharedHeapProfiler *SharedHeapProfiler::Instance()
{
    static SharedHeapProfiler *instance = new SharedHeapProfiler();
    return instance;
}

void SharedHeapProfiler::RegisterHeapTracker(HeapProfilerInterface *profiler)
{
    if (profiler == nullptr) {
        return;
    }
    SharedHeapProfiler &registry = *Instance();
    LockHolder lock(registry.mutex_);
    for (auto *tracker : registry.trackers_) {
        if (tracker == profiler) {
            return;  // idempotent
        }
    }
    registry.trackers_.push_back(profiler);
    activeTrackerCount_.fetch_add(1, std::memory_order_relaxed);
    if (registry.trackers_.size() == 1) {
        registry.sessionCreateCount_++;
        LOG_ECMA(INFO) << "[MoveEvents] tuning session created (first recorder registered)";
    }
}

void SharedHeapProfiler::UnregisterHeapTracker(HeapProfilerInterface *profiler)
{
    if (profiler == nullptr) {
        return;
    }
    SharedHeapProfiler &registry = *Instance();
    LockHolder lock(registry.mutex_);
    bool removed = false;
    for (auto it = registry.trackers_.begin(); it != registry.trackers_.end(); ++it) {
        if (*it == profiler) {
            registry.trackers_.erase(it);
            activeTrackerCount_.fetch_sub(1, std::memory_order_relaxed);
            removed = true;
            break;
        }
    }
    if (removed && registry.trackers_.empty()) {
        registry.trackers_.shrink_to_fit();  // release buffer when idle
        registry.sessionDestroyCount_++;
        LOG_ECMA(INFO) << "[MoveEvents] tuning session destroyed (last recorder unregistered)";
    }
}

void SharedHeapProfiler::LockActiveTrackers()
{
    // Held for the whole scan: a teardown unregister blocks here until the
    // scan finishes, so freed memory is never referenced.
    Instance()->mutex_.Lock();
}

void SharedHeapProfiler::UnlockActiveTrackers()
{
    Instance()->mutex_.Unlock();
}

void SharedHeapProfiler::DispatchMoveEvent(uintptr_t fromAddress, TaggedObject *toAddress, size_t size)
{
    for (auto *tracker : Instance()->trackers_) {
        tracker->MoveEventLocked(fromAddress, toAddress, size);
    }
}

uint32_t SharedHeapProfiler::GetSessionCreateCount()
{
    return Instance()->sessionCreateCount_;
}

uint32_t SharedHeapProfiler::GetSessionDestroyCount()
{
    return Instance()->sessionDestroyCount_;
}
}  // namespace panda::ecmascript
