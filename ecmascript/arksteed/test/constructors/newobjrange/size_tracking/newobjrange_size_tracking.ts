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

// newobjrange specialization: instance layout qualification and object-size
// tracking.
//
// Trigger A (tracking in progress -> helper allocation): `Tracking` has declared class
// fields, so its instance HClass starts slack tracking. Only one construction
// runs before the caller is compiled, leaving the tracking counter non-zero;
// FastNewThisObject handles allocation while the instance size is still tracked.
// The caller retains the known constructor and fixed argument preparation.
//
// Trigger B (tracking finished -> specialize): `Stable` receives 16 warm-up
// constructions, which drives the tracking counter to zero; the plan must be
// created and the shared allocation/initialization path used. The object must be
// fully initialized with the planned constant size.
//
// Expected: helper and constant-layout sites keep correct field values. Tracking does not
// leak uninitialized or wrongly sized instances.

//! METHOD callTracking
//! COUNT CallCommonStub FastNewThisObject 1
//! HAS_NOT InitializeEmptyObject
//! COUNT CallNewObjRangeBase 1
//! COUNT CallNewObjRangeDerived 1
//! CODEGEN_COUNT *CallNewObjRange*argumentPreparation = fixed* 2
//! CODEGEN_COUNT *CallNewObjRange*commonStub = ArkSteedCallConstructorBody* 0
//! HAS_NOT CallRuntime OptNewObjRange
//! METHOD callStable
//! HAS     InitializeEmptyObject
//! HAS     CallNewObjRangeBase
//! HAS     CallNewObjRangeDerived
//! HAS     CallRuntime AllocateInYoung

class Tracking {
    a: number;
    b: number;

    constructor(a: number, b: number) {
        this.a = a;
        this.b = b;
    }
}

class Stable {
    p: number;
    q: number;

    constructor(p: number, q: number) {
        this.p = p;
        this.q = q;
    }
}

function callTracking(x: number) {
    return new Tracking(x, x + 1);
}

function callStable(x: number) {
    return new Stable(x, x + 1);
}

callTracking(1);
ArkTools.arkSteedCompileSync(callTracking);

print(ArkTools.arkSteedIsCompiled(callTracking));
const t1 = callTracking(10);
print(t1.a);
print(t1.b);
const t2 = callTracking(20);
print(t2.a);
print(t2.b);

for (let i = 0; i < 16; i++) {
    callStable(i);
}
ArkTools.arkSteedCompileSync(callStable);

print(ArkTools.arkSteedIsCompiled(callStable));
const s = callStable(7);
print(s.p);
print(s.q);
