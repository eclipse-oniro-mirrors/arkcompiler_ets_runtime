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

// newobjrange specialization: exception lazy deopt and pending-exception
// priority on the construct call.
//
// Trigger: 16 warm-ups (constructor never throws) fill the feedback and mark
// the call's catch as never executed, so the compiled construct call gets an
// exception lazy-deopt safepoint. Three runs follow:
//   1. throw without invalidation - plain E-lazy path into the catch;
//   2. throw with an invalidation first (ArkTools.forceLazyDeopt), so a pending
//      exception and the construct-call recovery state exist at the same time;
//   3. throw from a caller without a catch so the exception propagates to the
//      module-level handler.
//
// Expected: the catch always receives the same thrown object, the constructor
// body runs exactly once per call, the invalidation clears the compiled state
// (isCompiled false), and the pending exception is never replaced by a normal
// construct result. `ok:1:true` before and after proves the accumulator/frame
// recovery. The first throw (no invalidation) keeps the caller compiled: it
// only consumes one slot of the default deopt threshold (10) installed for
// ArkSteed code.

//! METHOD callCatch
//! HAS InitializeEmptyObject
//! HAS CallNewObjRangeBase
//! HAS CallNewObjRangeDerived
//! HAS     CallRuntime AllocateInYoung
//! METHOD callPropagate
//! HAS InitializeEmptyObject
//! HAS CallNewObjRangeBase
//! HAS CallNewObjRangeDerived
//! HAS     CallRuntime AllocateInYoung

class DepRoot {}
class DepMid extends DepRoot {}
class DepLeaf extends DepMid {}

DepRoot.prototype.x = 1;
const depObj = new DepLeaf();

const boom = { code: 99 };
let runs = 0;
let armedThrow = false;
let armedInvalidate = false;

class ThrowCtor {
    ok: boolean;

    constructor() {
        runs++;
        if (armedThrow) {
            armedThrow = false;
            if (armedInvalidate) {
                armedInvalidate = false;
                ArkTools.forceLazyDeopt(DepLeaf.prototype, 1, false);
            }
            throw boom;
        }
        this.ok = true;
    }
}

function callCatch(obj) {
    const before = obj.x;
    try {
        const r = new ThrowCtor();
        return "ok:" + before + ":" + r.ok;
    } catch (e) {
        return e === boom ? "boom:" + before : "other";
    }
}

function callPropagate(obj) {
    const before = obj.x;
    return new ThrowCtor();
}

for (let i = 0; i < 16; i++) {
    callCatch(depObj);
    callPropagate(depObj);
}
ArkTools.arkSteedCompileSync(callCatch);
ArkTools.arkSteedCompileSync(callPropagate);

print(ArkTools.arkSteedIsCompiled(callCatch));
print(callCatch(depObj));
print(ArkTools.arkSteedIsCompiled(callCatch));

armedThrow = true;
armedInvalidate = false;
print(callCatch(depObj));
print(ArkTools.arkSteedIsCompiled(callCatch));

armedThrow = true;
armedInvalidate = true;
print(callCatch(depObj));
print(ArkTools.arkSteedIsCompiled(callCatch));
print(callCatch(depObj));

armedThrow = true;
armedInvalidate = true;
let caught = "none";
try {
    callPropagate(depObj);
} catch (e) {
    caught = e === boom ? "outer" : "wrong";
}
print(caught);
print(ArkTools.arkSteedIsCompiled(callPropagate));

print(runs > 0);
