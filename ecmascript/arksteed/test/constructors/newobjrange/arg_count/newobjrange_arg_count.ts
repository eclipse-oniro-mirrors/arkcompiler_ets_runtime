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

// newobjrange specialization: fewer, equal and more actual arguments than
// formal parameters (0, 1, 2, 3 arguments against two formals).
//
// Trigger: each call site is warmed up 16 times (so every plan records its own
// actual argc and finishes size tracking) and then compiled with
// ArkTools.arkSteedCompileSync. The constructor records arguments.length and
// arguments[2] to prove the argument adapter passes the real argument vector.
//
// Expected: missing arguments stay undefined, declared parameters receive the
// supplied values, extra arguments remain visible through the arguments object,
// and each site takes its own specialized plan.

//! METHOD callZero
//! HAS InitializeEmptyObject
//! HAS CallNewObjRangeBase
//! HAS CallNewObjRangeDerived
//! HAS     CallRuntime AllocateInYoung
//! METHOD callOne
//! HAS InitializeEmptyObject
//! HAS CallNewObjRangeBase
//! HAS CallNewObjRangeDerived
//! HAS     CallRuntime AllocateInYoung
//! METHOD callTwo
//! HAS InitializeEmptyObject
//! HAS CallNewObjRangeBase
//! HAS CallNewObjRangeDerived
//! HAS     CallRuntime AllocateInYoung
//! METHOD callThree
//! HAS InitializeEmptyObject
//! HAS CallNewObjRangeBase
//! HAS CallNewObjRangeDerived
//! HAS     CallRuntime AllocateInYoung

class ArgsCtor {
    a: any;
    b: any;
    n: number;
    c: any;

    constructor(a, b) {
        this.a = a;
        this.b = b;
        this.n = arguments.length;
        this.c = arguments.length > 2 ? arguments[2] : "none";
    }
}

function callZero() {
    return new ArgsCtor();
}
function callOne(x) {
    return new ArgsCtor(x);
}
function callTwo(x, y) {
    return new ArgsCtor(x, y);
}
function callThree(x, y, z) {
    return new ArgsCtor(x, y, z);
}

for (let i = 0; i < 16; i++) {
    callZero();
    callOne(i);
    callTwo(i, i + 1);
    callThree(i, i + 1, i + 2);
}
ArkTools.arkSteedCompileSync(callZero);
ArkTools.arkSteedCompileSync(callOne);
ArkTools.arkSteedCompileSync(callTwo);
ArkTools.arkSteedCompileSync(callThree);

const z = callZero();
print(z.a);
print(z.b);
print(z.n);
print(z.c);

const o = callOne(1);
print(o.a);
print(o.b);
print(o.n);
print(o.c);

const t = callTwo(1, 2);
print(t.a);
print(t.b);
print(t.n);
print(t.c);

const h = callThree(1, 2, 3);
print(h.a);
print(h.b);
print(h.n);
print(h.c);
