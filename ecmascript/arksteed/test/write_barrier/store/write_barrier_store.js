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

//! PARAMS --enable-force-gc=true --compiler-jit-hotness-threshold=60000

// G4.3j: a store of a freshly allocated heap object retains the barrier, so the
// compiled method keeps `StoreTaggedFieldWithBarrier`. The compile and entry
// counters are exact (`COMPILE_COUNT`/`EXEC_COUNT`, not lower bounds), because
// the construction fixes their multiplicity: the hotness threshold 60000
// prevents premature compilation, the single synchronous
// `ArkTools.arkSteedCompileSync` installs the method once, and the post-install
// loop calls it a fixed 2000 times. This leaf stays on its force-GC scenario
// (`--enable-force-gc=true`) and therefore asserts no `GC_COUNT_GE` bound: in
// the Debug process the hook collects on almost every allocation, so the volume
// of per-collection records follows the allocation count instead of a bounded
// window. Both counter forms are declared: the exact `COMPILE_COUNT`/
// `EXEC_COUNT` forms required for the construction-fixed multiplicity and the
// `COMPILE_COUNT_GE`/`EXEC_COUNT_GE` lower-bound forms explicitly named by the
// store-leaf acceptance criterion; the lower bounds hold because the exact
// counts are 1 compilation and 2000 entries.
//! METHOD storeFresh
//! HAS StoreTaggedFieldWithBarrier
//! COMPILE_COUNT *storeFresh* 1
//! COMPILE_COUNT_GE *storeFresh* 1
//! EXEC_COUNT *storeFresh* 2000
//! EXEC_COUNT_GE *storeFresh* 2000
//! DEOPT_COUNT *Deoptimize* 0

class Holder {
    constructor() {
        this.value = null;
    }
}

function forceFullGC() {
    ArkTools.forceFullGC();
    if (ArkTools.GC !== undefined) {
        ArkTools.GC.startGC("full", undefined, true);
    }
}

function forceYoungGC() {
    if (ArkTools.GC !== undefined) {
        ArkTools.GC.startGC("young", undefined, true);
        return;
    }
    ArkTools.gc();
}

function makeNoise(seed) {
    let result = 0;
    for (let i = 0; i < 64; i++) {
        let tmp = { x: seed + i, y: [seed, i, seed + i] };
        result += tmp.x;
    }
    return result;
}

function storeFresh(holder, marker) {
    holder.value = { marker: marker, payload: [marker, marker + 1, marker + 2] };
}

let holder = new Holder();
for (let i = 0; i < 20000; i++) {
    storeFresh(holder, i);
}

forceFullGC();
let storeICState = ArkTools.getICState(storeFresh, 0, 1);
// One more explicit full collection before the synchronous installation: the
// IC read above allocates and leaves a forced collection pending, and the
// synchronous ArkSteed compile of this leaf deadlocks in that state while both
// deopt traces (implied by DEOPT_COUNT) are enabled. The extra collection is
// additive: the scenario stays a force-GC scenario and both collections sit in
// the same phase of the case.
forceFullGC();
ArkTools.arkSteedCompileSync(storeFresh);
ArkTools.waitJitCompileFinish(storeFresh);

let ok = storeICState === "mono";
ok = ok && ArkTools.isAOTCompiled(storeFresh);
for (let i = 0; i < 2000; i++) {
    storeFresh(holder, i);
    makeNoise(i);
    forceYoungGC();
    if (holder.value.marker !== i || holder.value.payload[2] !== i + 2) {
        ok = false;
        break;
    }
}

print(ok ? "PASS" : "FAIL");
