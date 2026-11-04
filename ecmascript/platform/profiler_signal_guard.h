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

#ifndef ECMASCRIPT_PLATFORM_PROFILER_SIGNAL_GUARD_H
#define ECMASCRIPT_PLATFORM_PROFILER_SIGNAL_GUARD_H

#include <atomic>

#include "libpandabase/macros.h"

namespace panda::ecmascript {
// Debug-build runtime interception state (plan §6.6). The SIGPROF handler marks
// itself via this flag; blocking-lock and handle-allocation entry points check it
// in debug builds and record violations into the preallocated atomic counter.
// A violation must never be logged from the hook itself: the hook can fire inside
// the handler, so the only allowed operation is a relaxed atomic increment.
//
// TLS model: initial-exec is mandatory here. The handler can run on an FFRT
// worker thread that never executed warm-up code, and a general-dynamic TLS
// access on such a thread would call __tls_get_addr, which can allocate --
// precisely the class of bug this module exists to catch (plan §6.2: the flag
// must live at a fixed per-thread address). WarmupProfilerSignalGuard stays as
// defense in depth for toolchains that ignore the attribute.
#if defined(__clang__) || defined(__GNUC__)
// NOLINTNEXTLINE(hicpp-signed-bitwise)
#define PROFILER_TLS_INITIAL_EXEC __attribute__((tls_model("initial-exec")))
#else
#define PROFILER_TLS_INITIAL_EXEC
#endif
inline thread_local bool g_inProfilerSignalHandler PROFILER_TLS_INITIAL_EXEC = false;
inline std::atomic<uint32_t> g_profilerSignalHandlerViolations {0};

inline void EnterProfilerSignalGuard()
{
#ifndef NDEBUG
    g_inProfilerSignalHandler = true;
#endif
}

inline void ExitProfilerSignalGuard()
{
#ifndef NDEBUG
    g_inProfilerSignalHandler = false;
#endif
}

// Must be called from normal context on every thread the handler can run on
// before the session starts: the first access to a dynamic-TLS variable can
// allocate the thread's DTV block, which is not async-signal-safe (plan §6.2).
inline void WarmupProfilerSignalGuard()
{
    volatile bool warm = g_inProfilerSignalHandler;
    static_cast<void>(warm);
}

class ProfilerSignalGuardScope {
public:
    ProfilerSignalGuardScope()
    {
        EnterProfilerSignalGuard();
    }

    ~ProfilerSignalGuardScope()
    {
        ExitProfilerSignalGuard();
    }

    NO_COPY_SEMANTIC(ProfilerSignalGuardScope);
    NO_MOVE_SEMANTIC(ProfilerSignalGuardScope);
};

// Interception hook body for the debug points (plan §6.6); the empty release
// body compiles away entirely.
inline void ProfilerSignalGuardCheck()
{
#ifndef NDEBUG
    if (UNLIKELY(g_inProfilerSignalHandler)) {
        g_profilerSignalHandlerViolations.fetch_add(1, std::memory_order_relaxed);
    }
#endif
}

// Defined in cpu_profiler.cpp. Non-inline on purpose: the unit-test binary is
// dynamically linked against the runtime library, and vague-linkage inline
// variables or TLS symbols cannot be shared reliably across that DSO boundary.
// These accessors let tests observe and drive the library-internal guard state.
uint32_t GetProfilerSignalHandlerViolationsForTest();
void SetProfilerSignalGuardForTest(bool active);
} // namespace panda::ecmascript
#endif // ECMASCRIPT_PLATFORM_PROFILER_SIGNAL_GUARD_H
