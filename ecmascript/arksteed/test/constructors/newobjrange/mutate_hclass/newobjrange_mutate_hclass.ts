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

// newobjrange specialization: the constructor mutates the receiver's HClass
// (a dynamic key is added after the planned fields) and then returns a
// primitive; the normal path and construct-result recovery must
// both return that same mutated receiver.
//
// Trigger: the caller loads `obj.x` (stable-HClass dependency) and, after 16
// warm-ups and compilation, the constructor is armed to force a lazy deopt
// after mutating the receiver. Recovery must not decide validity by comparing
// the receiver's HClass with the planned initial HClass.
//
// Expected: normal and deopt calls both select the mutated receiver (same
// identity, fields present, Object.keys non-empty); the constructor body ran
// once per call.

//! METHOD runMut
//! HAS InitializeEmptyObject
//! HAS CallNewObjRangeBase
//! HAS CallNewObjRangeDerived
//! HAS     CallRuntime AllocateInYoung

class DepRoot {}
class DepMid extends DepRoot {}
class DepLeaf extends DepMid {}

DepRoot.prototype.x = 1;
const depObj = new DepLeaf();

let runs = 0;
let lastReceiver: any = null;
let armed = false;

class MutCtor {
    a: number;
    b: number;

    constructor() {
        runs++;
        lastReceiver = this;
        this.a = runs;
        this.b = runs + 1;
        const dynamicKey = "d" + (runs % 2);
        this[dynamicKey] = runs;
        if (armed) {
            armed = false;
            ArkTools.forceLazyDeopt(DepLeaf.prototype, 1, false);
        }
        return 42;
    }
}

function runMut(obj) {
    const before = obj.x;
    const runsBefore = runs;
    const result = new MutCtor();
    const runsAfter = runs;
    return {
        before: before,
        same: result === lastReceiver,
        a: result.a,
        b: result.b,
        keys: Object.keys(result).length,
        runs: runsAfter - runsBefore,
    };
}

for (let i = 0; i < 16; i++) {
    runMut(depObj);
}
ArkTools.arkSteedCompileSync(runMut);

print(ArkTools.arkSteedIsCompiled(runMut));

const normal = runMut(depObj);
print(normal.same);
print(normal.a > 0);
print(normal.b > 0);
print(normal.keys > 0);
print(normal.runs);
print(ArkTools.arkSteedIsCompiled(runMut));

armed = true;
const deoptResult = runMut(depObj);
print(deoptResult.same);
print(deoptResult.a > 0);
print(deoptResult.b > 0);
print(deoptResult.keys > 0);
print(deoptResult.runs);
print(ArkTools.arkSteedIsCompiled(runMut));

const afterResult = runMut(depObj);
print(afterResult.same);
print(afterResult.runs);
