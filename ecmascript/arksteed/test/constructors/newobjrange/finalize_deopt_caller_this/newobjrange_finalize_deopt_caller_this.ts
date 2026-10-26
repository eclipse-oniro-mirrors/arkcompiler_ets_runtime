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

// newobjrange specialization: lazy deopt when the caller's `this` differs from
// the newly allocated receiver. This verifies that the constructor-receiver
// recovery slot is a dedicated value and does not reuse the caller's
// THIS_OBJECT_INDEX.
//
// Trigger: the caller is invoked as `callWithThis.call(holder, depObj)` so its
// `this` is `holder` while the construct call allocates a fresh DeoptPrimitive.
// The body keeps `this` live across the call (`result === this`). On the
// trigger call the constructor invalidates the caller's `obj.x` stable-HClass
// dependency via ArkTools.forceLazyDeopt.
//
// Expected: after the deopt the selected result is the new receiver, never
// `holder`; `this` is still recovered as `holder`; the constructor ran once.

//! METHOD callWithThis
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

const holder = { tag: "holder" };

function callWithThis(obj) {
    const before = obj.x;
    const runsBefore = runs;
    const result = new DeoptPrimitive();
    const runsAfter = runs;
    return {
        tag: this.tag,
        before: before,
        same: result === lastReceiver,
        isHolder: result === this,
        value: result.v,
        runs: runsAfter - runsBefore,
    };
}

for (let i = 0; i < 16; i++) {
    callWithThis.call(holder, depObj);
}
ArkTools.arkSteedCompileSync(callWithThis);

print(ArkTools.arkSteedIsCompiled(callWithThis));

const normal = callWithThis.call(holder, depObj);
print(normal.tag);
print(normal.before);
print(normal.same);
print(normal.isHolder);
print(normal.value > 0);
print(normal.runs);

armed = true;
const deoptResult = callWithThis.call(holder, depObj);
print(deoptResult.tag);
print(deoptResult.same);
print(deoptResult.isHolder);
print(deoptResult.value > 0);
print(deoptResult.runs);
print(ArkTools.arkSteedIsCompiled(callWithThis));

const afterResult = callWithThis.call(holder, depObj);
print(afterResult.same);
print(afterResult.runs);
