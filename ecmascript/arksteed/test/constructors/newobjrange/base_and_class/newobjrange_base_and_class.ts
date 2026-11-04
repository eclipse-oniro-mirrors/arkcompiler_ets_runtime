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

// newobjrange specialization: plain-function and base-class targets, zero and
// multiple arguments, property values, prototype identity, new.target and
// argument semantics.
//
// Trigger: 16 interpreted warm-up constructions per caller fill the
// monomorphic constructor feedback and finish instance object-size tracking;
// each caller is then compiled with ArkTools.arkSteedCompileSync. Every caller
// holds a single newobjrange site, so the specialized plan must be taken
// (shared AllocateInYoung + InitializeEmptyObject + CallConstructorBody).
//
// Expected: the constructed object owns the field values, its prototype is the
// constructor's prototype, the constructor observes `new.target` equal to the
// called constructor and the actual argument count.

//! METHOD callPlain
//! COUNT   InitializeEmptyObject 1
//! COUNT   CallRuntime AllocateInYoung 1
//! COUNT   CallNewObjRangeBase 1
//! COUNT   CallNewObjRangeDerived 1
//! METHOD callZero
//! COUNT   InitializeEmptyObject 1
//! COUNT   CallRuntime AllocateInYoung 1
//! COUNT   CallNewObjRangeBase 1
//! COUNT   CallNewObjRangeDerived 1
//! METHOD callMulti
//! COUNT   InitializeEmptyObject 1
//! COUNT   CallRuntime AllocateInYoung 1
//! COUNT   CallNewObjRangeBase 1
//! COUNT   CallNewObjRangeDerived 1

let lastThis: any = null;
let lastNewTarget: any = null;
let lastArgCount: number = -1;
let lastArgs: any[] = [];

function Plain(a, b) {
    lastThis = this;
    lastNewTarget = new.target;
    lastArgCount = arguments.length;
    lastArgs = [a, b];
    this.a = a;
    this.b = b;
}
Plain.prototype.baseTag = 7;

class Base {
    x: number;
    y: number;

    constructor(x: number = 1, y: number = 2) {
        lastThis = this;
        lastNewTarget = new.target;
        lastArgCount = arguments.length;
        lastArgs = [x, y];
        this.x = x;
        this.y = y;
    }
}
Base.prototype.classTag = "base";

function callPlain(a, b) {
    return new Plain(a, b);
}

function callZero() {
    return new Base();
}

function callMulti(a) {
    return new Base(a, a + 1);
}

for (let i = 0; i < 16; i++) {
    callPlain(i, i + 1);
    callZero();
    callMulti(i);
}
ArkTools.arkSteedCompileSync(callPlain);
ArkTools.arkSteedCompileSync(callZero);
ArkTools.arkSteedCompileSync(callMulti);

const p = callPlain("a", "b");
print(p.a);
print(p.b);
print(p.baseTag);
print(Object.getPrototypeOf(p) === Plain.prototype);
print(lastThis === p);
print(lastNewTarget === Plain);
print(lastArgCount);
print(lastArgs[0]);
print(lastArgs[1]);

const z = callZero();
print(z.x);
print(z.y);
print(z.classTag);
print(Object.getPrototypeOf(z) === Base.prototype);
print(lastThis === z);
print(lastNewTarget === Base);
print(lastArgCount);

const m = callMulti(5);
print(m.x);
print(m.y);
print(lastThis === m);
print(lastArgCount);
