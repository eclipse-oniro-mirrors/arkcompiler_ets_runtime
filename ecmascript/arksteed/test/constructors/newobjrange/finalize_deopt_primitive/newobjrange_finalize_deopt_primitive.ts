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

// newobjrange specialization: dependency lazy deopt on the construct call
// return while the constructor returns a primitive value
// (PR98 BASE_CONSTRUCTOR_THIS_INDEX recovery value).
//
// Trigger: the caller first loads `obj.x` from the DepLeaf prototype chain,
// which installs a stable-HClass lazy-deopt dependency. After 16 warm-ups and
// the caller's compilation, `armed` makes the constructor call
// ArkTools.forceLazyDeopt(DepLeaf.prototype, 1, false) before returning 42.
// The invalidation patches the construct call's return PC, so the caller
// resumes in the interpreter with the constructor result still pending.
//
// Expected: the deopt selects the receiver (42 is not an ECMAScript object),
// the caller keeps executing after the newobjrange, the constructor body ran
// exactly once (per-call delta), and the receiver identity/value are the
// constructor's `this`. Before the trigger the specialized normal path must
// already return the receiver.

//! METHOD runPrimitive
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

class DeoptPrimitive {
    v: number;

    constructor() {
        runs++;
        lastReceiver = this;
        this.v = runs;
        if (armed) {
            armed = false;
            ArkTools.forceLazyDeopt(DepLeaf.prototype, 1, false);
        }
        return 42;
    }
}

function runPrimitive(obj) {
    const before = obj.x;
    const runsBefore = runs;
    const result = new DeoptPrimitive();
    const runsAfter = runs;
    return {
        before: before,
        selected: result === lastReceiver,
        objectValue: result.v,
        runs: runsAfter - runsBefore,
    };
}

for (let i = 0; i < 16; i++) {
    runPrimitive(depObj);
}
ArkTools.arkSteedCompileSync(runPrimitive);

print(ArkTools.arkSteedIsCompiled(runPrimitive));

const normal = runPrimitive(depObj);
print(normal.before);
print(normal.selected);
print(normal.objectValue > 0);
print(normal.runs);
print(ArkTools.arkSteedIsCompiled(runPrimitive));

armed = true;
const deoptResult = runPrimitive(depObj);
print(deoptResult.before);
print(deoptResult.selected);
print(deoptResult.objectValue > 0);
print(deoptResult.runs);
print(ArkTools.arkSteedIsCompiled(runPrimitive));

const afterResult = runPrimitive(depObj);
print(afterResult.selected);
print(afterResult.runs);
