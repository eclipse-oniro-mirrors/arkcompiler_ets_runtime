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

//! PARAMS --gc-long-paused-time=0 --compiler-jit-hotness-threshold=60000

// G4.3j: the write-barrier value-kind pass keeps the barrier for the Phi that
// mixes an int32 with a heap object, so the store stays
// `StoreTaggedFieldWithBarrier` with `value_kind = UNKNOWN`. The compile and
// entry counters are exact (`COMPILE_COUNT`/`EXEC_COUNT`, not lower bounds),
// because the construction fixes their multiplicity: the hotness threshold
// 60000 prevents premature compilation, the single synchronous
// `ArkTools.arkSteedCompileSync` installs the method once, and the two
// post-install calls are fixed. The first full collection promotes the holder
// before the object store; the bounded collection window is the full collection
// taken after that object store plus the young collection, and the retained
// object must keep its identity and contents across the window. The canonical
// row contract carries `GC_COUNT_GE <collection-head-pattern> 1`; the stronger
// bound of 2 is kept on top of it because the promotion phase already
// contributes one matching record, so the bound of 2 (and not the bound of 1)
// uniquely requires the post-object-store full collection.
//! METHOD storeMixedPhi
//! HAS StoreTaggedFieldWithBarrier
//! COMPILE_COUNT *storeMixedPhi* 1
//! EXEC_COUNT *storeMixedPhi* 2
//! DEOPT_COUNT *Deoptimize* 0
//! GC_COUNT_GE *CompressGC*Trigger by JS* 1
//! GC_COUNT_GE *CompressGC*Trigger by JS* 2

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

function storeMixedPhi(holder, flag, marker) {
    let value = 7;
    if (flag) {
        value = { marker: marker, payload: [marker, marker + 1] };
    }
    holder.value = value;
    return holder.value;
}

let holder = new Holder();

for (let i = 0; i < 20000; i++) {
    storeMixedPhi(holder, (i & 1) === 0, i);
}

let ok = true;
ArkTools.arkSteedCompileSync(storeMixedPhi);
ArkTools.waitJitCompileFinish(storeMixedPhi);
ok = ok && ArkTools.isAOTCompiled(storeMixedPhi);

ok = ok && storeMixedPhi(holder, false, 42) === 7;
// The first full collection promotes the holder, so the object store below
// writes a young object into an old one. The second full collection and the
// young collection form the bounded window taken after that object store; the
// reference captured in between and its contents must survive the window.
forceFullGC();
storeMixedPhi(holder, true, 99);
let retained = holder.value;
forceFullGC();
forceYoungGC();
ok = ok && holder.value === retained;
ok = ok && holder.value.marker === 99 && holder.value.payload[1] === 100;

print(ok ? "PASS" : "FAIL");
