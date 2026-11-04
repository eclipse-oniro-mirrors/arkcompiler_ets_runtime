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

/**
 * cpuprofiler_jit_alloc_repro.js
 *
 * Minimal reproducer for the SIGPROF crash in CpuProfiler::GetStackSignalHandler
 * when sampling fires during a JIT -> runtime-stub (RuntimeOptNewObjRange) transition.
 *
 * Symptom (processdump):
 *   LastFatalMessage: [GetFunction] Unknown frame type: <garbage>
 *   #05 CpuProfiler::GetStackSignalHandler
 *   #04 CpuProfiler::GetStack
 *   #03 FrameIterator::CheckAndGetMethod
 *   #08 RTStub_ArkSteedCallEntry
 *   #11 RuntimeStubs::RuntimeOptNewObjRange
 *
 * Root cause: arksteed prologue/epilogue FP discipline is already fixed (problem A),
 * but the C++ -> Stub path (problem B) is not fully closed: IsLegalSp cannot
 * distinguish a C++ frame's FP from a valid arksteed frame's FP when both share the
 * C/ASM stack, and CheckFrameType only validates the first 2 frames, so frame 3+
 * reads garbage -> GetFunction() default -> LOG_FULL(FATAL).
 *
 * Trigger recipe:
 *   1. Enable JIT + JIT FORT + ark tools.
 *   2. Start cpuprofile (default 500us SIGPROF interval).
 *   3. Run a JIT-compiled function that densely allocates many small objects per
 *      iteration, so a large fraction of JIT execution time is inside
 *      RuntimeOptNewObjRange (the JIT -> runtime object-allocation stub).
 *   4. Keep running long enough for SIGPROF to hit the transition window.
 *
 * Run (standalone build):
 *   cd ark_standalone_build
 *   # compile JS -> abc (host es2abc)
 *   python ark.py x64.release ark_js_host_linux_tools_packages   # one-time tool build
 *   es2abc --module cpuprofiler_jit_alloc_repro.js -o cpuprofiler_jit_alloc_repro.abc
 *   # run with JIT + profiler
 *   ./ark.js x64.release \
 *       --compiler-enable-jit=true \
 *       --compiler-enable-jitfort=true \
 *       --compiler-enable-litecg=true \
 *       --open-ark-tools=true \
 *       --enable-force-gc=false \
 *       cpuprofiler_jit_alloc_repro.abc
 *
 * Expected on a vulnerable build: process aborts with
 *   "[GetFunction] Unknown frame type: <large garbage value>"
 * within seconds. On a fixed build: prints the result line and exits 0, leaving
 * cpuprofile_jit_alloc_repro.cpuprofile on disk.
 */

// A small "bean" with many fields, constructed fresh every call, mirroring the
// protobuf-bean allocation pattern that produced the original crash.
function makeBean(i) {
    return {
        a: i,
        b: i + 1,
        c: i + 2,
        d: i + 3,
        e: i * 2,
        f: i * 3,
        g: 's' + i,
        h: i < 0 ? null : makeInner(i),
    };
}

function makeInner(i) {
    return { x: i, y: i + 7, z: [i, i + 1, i + 2] };
}

// Hot function: dense allocation inside a tight loop so JIT spends most of its
// time in RuntimeOptNewObjRange rather than pure arithmetic.
function allocHotLoop(n) {
    let acc = 0;
    for (let i = 0; i < n; i++) {
        const bean = makeBean(i); // triggers JIT-compiled object allocation
        acc += bean.a + bean.e;
        if ((i & 0x3ff) === 0) {
            acc = acc & 0xffff; // keep accumulator bounded
        }
    }
    return acc;
}

function main() {
    // Warm up makeBean / makeInner so arksteed JIT-compiles them before sampling
    // begins (we want samples to land in JIT code, not interpreter).
    let warm = 0;
    for (let k = 0; k < 2000; k++) {
        warm += makeBean(k).a;
    }

    // Start CPU profiling. SIGPROF fires every ~500us while the JIT-compiled
    // allocHotLoop is running.
    ArkTools.startCpuProf('cpuprofile_jit_alloc_repro');

    // Long enough (millions of allocations) for many SIGPROF samples to hit the
    // JIT -> RuntimeOptNewObjRange transition window.
    const result = allocHotLoop(2_000_000);

    ArkTools.stopCpuProf();
    print('cpuprofiler_jit_alloc_repro result: ' + result);
    print('cpuprofiler_jit_alloc_repro warm: ' + warm);
}

main();
