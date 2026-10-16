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

#ifndef ECMASCRIPT_MEM_CPU_CLOCK_SCOPE_H
#define ECMASCRIPT_MEM_CPU_CLOCK_SCOPE_H

#include <cstdint>
#include <ctime>

#include "libpandabase/macros.h"

namespace panda::ecmascript {
// Measures how much CPU time this thread actually spent running: GC/stop-the-world pauses,
// lock waits and OS preemption are not charged to the scope.
class CPUClockScope {
public:
    CPUClockScope()
    {
        startUs_ = NowUs();
    }

    void Reset()
    {
        startUs_ = NowUs();
    }

    float TotalSpentTime() const
    {
        return static_cast<float>(NowUs() - startUs_) / MICROSECONDS_PER_MILLISECOND;
    }

    int TotalSpentTimeInMicroseconds() const
    {
        return static_cast<int>(NowUs() - startUs_);
    }

    // Microseconds this thread has been executing. Does not advance while the thread is
    // descheduled, blocked on a lock, or while the world is stopped for GC.
    static uint64_t NowUs()
    {
        struct timespec ts = {0, 0};
        if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts) == 0) {
            return static_cast<uint64_t>(ts.tv_sec) * MICROSECONDS_PER_SECOND +
                static_cast<uint64_t>(ts.tv_nsec) / NANOSECONDS_PER_MICROSECOND;
        }
        struct timespec wall = {0, 0};  // fallback: monotonic elapsed time
        if (clock_gettime(CLOCK_MONOTONIC, &wall) == 0) {
            return static_cast<uint64_t>(wall.tv_sec) * MICROSECONDS_PER_SECOND +
                static_cast<uint64_t>(wall.tv_nsec) / NANOSECONDS_PER_MICROSECOND;
        }
        return 0;
    }

private:
    NO_COPY_SEMANTIC(CPUClockScope);
    NO_MOVE_SEMANTIC(CPUClockScope);

    uint64_t startUs_ {0};
    static constexpr uint32_t MICROSECONDS_PER_MILLISECOND = 1000;
    static constexpr uint64_t MICROSECONDS_PER_SECOND = 1000000ULL;
    static constexpr uint64_t NANOSECONDS_PER_MICROSECOND = 1000ULL;
};
}  // namespace panda::ecmascript

#endif  // ECMASCRIPT_MEM_CPU_CLOCK_SCOPE_H
