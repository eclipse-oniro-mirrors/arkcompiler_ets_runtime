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

// newobjrange specialization: repeated construction inside a loop, with calls
// that clobber caller-saved registers and reuse stack slots, plus a young GC
// every eight iterations.
//
// Trigger: 16 warm-ups (4 iterations of loopConstruct(4)) fill the constructor
// feedback and finish size tracking, then the loop function is compiled. The
// compiled run builds 64 instances while `i`, `sum`, `results` stay live across
// the construct calls and the GC calls.
//
// Expected: the sum, per-element field values, length and object identity of
// the result array are all correct after the loop; each iteration produced a
// distinct receiver and the constructor ran exactly `count` times.

//! METHOD loopConstruct
//! HAS InitializeEmptyObject
//! HAS CallNewObjRangeBase
//! HAS CallNewObjRangeDerived
//! HAS     CallRuntime AllocateInYoung

let ctorRuns = 0;

function forceYoungGC() {
    if (ArkTools.GC !== undefined) {
        ArkTools.GC.startGC("young", undefined, true);
    }
}

class LoopCtor {
    a: number;
    b: number;

    constructor(a: number, b: number) {
        ctorRuns++;
        this.a = a;
        this.b = b;
    }
}

function loopConstruct(count: number) {
    const results = [];
    let sum = 0;
    for (let i = 0; i < count; i++) {
        const obj = new LoopCtor(i, i * 2);
        sum += obj.a + obj.b;
        results.push(obj);
        if ((i & 7) === 0) {
            forceYoungGC();
        }
    }
    return { results: results, sum: sum };
}

for (let i = 0; i < 4; i++) {
    loopConstruct(4);
}
ArkTools.arkSteedCompileSync(loopConstruct);

const before = ctorRuns;
const r = loopConstruct(64);
print(r.sum);
print(r.results.length);
print(r.results[63].a);
print(r.results[63].b);
print(new Set(r.results).size);
print(r.results[0] === r.results[1]);
print(ctorRuns - before);
