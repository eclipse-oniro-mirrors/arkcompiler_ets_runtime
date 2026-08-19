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
// return while the constructor returns an explicit object
// (PR98 BASE_CONSTRUCTOR_THIS_INDEX recovery value).
//
// Trigger: the caller installs a stable-HClass dependency through `obj.x`; 16
// warm-ups fill the constructor feedback, then the caller is compiled. On the
// trigger call the constructor invalidates the dependency with
// ArkTools.forceLazyDeopt(DepLeaf.prototype, 1, false) and returns a fresh
// `{ y: 2 }`, so the recorded raw result is an ECMAScript object.
//
// Expected: the recovery selects the explicit returned object (not the
// allocated receiver), the object identity is stable, the constructor body ran
// exactly once, and the caller resumes after the newobjrange in the
// interpreter.

//! METHOD runObject
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

class DeoptObject {
    v: number;

    constructor() {
        runs++;
        lastReceiver = this;
        this.v = runs;
        if (armed) {
            armed = false;
            ArkTools.forceLazyDeopt(DepLeaf.prototype, 1, false);
        }
        return { y: 2 };
    }
}

function runObject(obj) {
    const before = obj.x;
    const runsBefore = runs;
    const result = new DeoptObject();
    const runsAfter = runs;
    return {
        before: before,
        y: result.y,
        isReceiver: result === lastReceiver,
        runs: runsAfter - runsBefore,
    };
}

for (let i = 0; i < 16; i++) {
    runObject(depObj);
}
ArkTools.arkSteedCompileSync(runObject);

print(ArkTools.arkSteedIsCompiled(runObject));

const normal = runObject(depObj);
print(normal.y);
print(normal.isReceiver);
print(normal.runs);

armed = true;
const deoptResult = runObject(depObj);
print(deoptResult.y);
print(deoptResult.isReceiver);
print(deoptResult.runs);
print(ArkTools.arkSteedIsCompiled(runObject));

const afterResult = runObject(depObj);
print(afterResult.y);
print(afterResult.runs);
