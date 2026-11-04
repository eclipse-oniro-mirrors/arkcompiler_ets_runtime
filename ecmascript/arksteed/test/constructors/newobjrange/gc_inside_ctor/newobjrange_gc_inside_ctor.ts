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

//! PARAMS --compiler-jit-hotness-threshold=60000

// newobjrange specialization: GC inside the constructor while the compiled
// caller holds the freshly allocated receiver in a tagged stack slot.
//
// Trigger: 16 warm-ups fill the constructor feedback and finish size tracking;
// the caller is compiled with ArkTools.arkSteedCompileSync. `gcArmed` then
// makes the constructor run a young GC and a full GC (ArkTools.GC.startGC /
// ArkTools.forceFullGC) after the receiver was written. The receiver captured
// inside the constructor (`lastReceiver`) and the caller's selected result must
// both reference the post-GC (possibly moved) object.
//
// Expected: the constructor runs once, the result equals the captured
// receiver, fields survive, and the prototype is intact after the GC.

//! METHOD callGC
//! HAS InitializeEmptyObject
//! HAS CallNewObjRangeBase
//! HAS CallNewObjRangeDerived
//! HAS     CallRuntime AllocateInYoung

let lastReceiver: any = null;
let ctorRuns = 0;
let gcArmed = false;

function forceYoungGC() {
    if (ArkTools.GC !== undefined) {
        ArkTools.GC.startGC("young", undefined, true);
    }
}

function forceFullGC() {
    ArkTools.forceFullGC();
    if (ArkTools.GC !== undefined) {
        ArkTools.GC.startGC("full", undefined, true);
    }
}

class GCCtor {
    v: number;

    constructor(v: number) {
        ctorRuns++;
        this.v = v;
        lastReceiver = this;
        if (gcArmed) {
            forceYoungGC();
            forceFullGC();
        }
    }
}

function callGC(v: number) {
    return new GCCtor(v);
}

for (let i = 0; i < 16; i++) {
    callGC(i);
}
ArkTools.arkSteedCompileSync(callGC);

const before = ctorRuns;
gcArmed = true;
const r = callGC(21);
gcArmed = false;

print(r.v);
print(r === lastReceiver);
print(Object.getPrototypeOf(r) === GCCtor.prototype);
print(ctorRuns - before);
