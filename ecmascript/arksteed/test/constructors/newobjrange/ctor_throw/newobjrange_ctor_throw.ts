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

// newobjrange specialization: a constructor body that throws, with the
// exception caught by the compiled caller.
//
// Trigger: 16 interpreted warm-up calls (no throw) fill the constructor
// feedback; the caller is then compiled with ArkTools.arkSteedCompileSync. The
// constructor keeps a per-call counter to prove the body runs exactly once per
// call; the thrown value is a pre-created object checked by identity.
//
// Expected: the normal call yields the receiver's field, the throwing call
// lands in the caller's catch with the same thrown object and increments the
// constructor counter exactly once. The specializer's raw-result contract must
// not swallow the exception and must not complete the construct call with a
// normal result.

//! METHOD callThrow
//! HAS InitializeEmptyObject
//! HAS CallNewObjRangeBase
//! HAS CallNewObjRangeDerived
//! HAS     CallRuntime AllocateInYoung

let runs = 0;
const thrownObj = { code: 77 };

class Thrower {
    ok: boolean;

    constructor(shouldThrow: boolean) {
        runs++;
        if (shouldThrow) {
            throw thrownObj;
        }
        this.ok = true;
    }
}

function callThrow(shouldThrow: boolean) {
    try {
        const obj = new Thrower(shouldThrow);
        return "ok:" + obj.ok;
    } catch (e) {
        return e === thrownObj ? "caught" : "wrong";
    }
}

for (let i = 0; i < 16; i++) {
    callThrow(false);
}
ArkTools.arkSteedCompileSync(callThrow);

print(ArkTools.arkSteedIsCompiled(callThrow));

let before = runs;
print(callThrow(false));
print(runs - before);

before = runs;
print(callThrow(true));
print(runs - before);

print(thrownObj.code);
