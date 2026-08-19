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

//! PARAMS --compiler-jit-hotness-threshold=60000 --enable-force-gc=true

//! METHOD make0
//! COUNT CallCommonStub NewThisObjectChecked 1
//! COUNT CallNewObjRangeBase 1
//! COUNT CallNewObjRangeDerived 1
//! CODEGEN_COUNT *CallNewObjRange*commonStub = ArkSteedCallConstructorBody* 2
//! METHOD make1
//! COUNT CallCommonStub NewThisObjectChecked 1
//! COUNT CallNewObjRangeBase 1
//! COUNT CallNewObjRangeDerived 1
//! CODEGEN_COUNT *CallNewObjRange*commonStub = ArkSteedCallConstructorBody* 2
//! METHOD make2
//! COUNT CallCommonStub NewThisObjectChecked 1
//! COUNT CallNewObjRangeBase 1
//! COUNT CallNewObjRangeDerived 1
//! CODEGEN_COUNT *CallNewObjRange*commonStub = ArkSteedCallConstructorBody* 2
//! METHOD make8
//! COUNT CallCommonStub NewThisObjectChecked 1
//! COUNT CallNewObjRangeBase 1
//! COUNT CallNewObjRangeDerived 1
//! CODEGEN_COUNT *CallNewObjRange*commonStub = ArkSteedCallConstructorBody* 2
//! METHOD nested
//! COUNT CallCommonStub NewThisObjectChecked 2
//! COUNT CallNewObjRangeBase 2
//! COUNT CallNewObjRangeDerived 2
//! CODEGEN_COUNT *CallNewObjRange*commonStub = ArkSteedCallConstructorBody* 4

function check(value, message) {
    if (!value) throw new Error(message);
}

let runs = 0;
let collect = false;
let bodyResult;
let lastReceiver;
function A(a, b, c) {
    runs++;
    if (collect) ArkTools.forceFullGC();
    this.target = new.target;
    this.args = Array.prototype.slice.call(arguments);
    this.formals = [a, b, c];
    lastReceiver = this;
    return bodyResult;
}
function B(a) {
    runs++;
    if (collect) ArkTools.forceFullGC();
    this.target = new.target;
    this.args = Array.prototype.slice.call(arguments);
    lastReceiver = this;
    return bodyResult;
}
function make0(C) {
    return new C();
}
function make1(C, x) {
    return new C(x);
}
function make2(C, x, y) {
    return new C(x, y);
}
function make8(C, a, b, c, d, e, f, g, h) {
    return new C(a, b, c, d, e, f, g, h);
}
function nested(C, x) {
    return new C(new C(x));
}

for (let i = 0; i < 24; i++) {
    const C = i % 2 === 0 ? A : B;
    make0(C);
    make1(C, i);
    make2(C, i, i + 1);
    make8(C, i, i, i, i, i, i, i, i);
    nested(C, i);
}
const callers = [make0, make1, make2, make8, nested];
for (const caller of callers) ArkTools.arkSteedCompileSync(caller);

// Exercise the same dynamic sites with interpreter bodies, then ArkSteed bodies.
for (let tier = 0; tier < 2; tier++) {
    if (tier === 1) {
        ArkTools.arkSteedCompileSync(A);
        ArkTools.arkSteedCompileSync(B);
        check(ArkTools.arkSteedIsCompiled(A) && ArkTools.arkSteedIsCompiled(B), "compiled bodies");
    }
    collect = true;
    for (const C of [A, B]) {
        const a = { value: 11 };
        const b = { value: 22 };
        const c = Symbol("argument");
        const before = runs;
        const zero = make0(C);
        const one = make1(C, a);
        const two = make2(C, a, b);
        const eight = make8(C, a, b, c, a, b, c, a, b);
        check(runs === before + 4, "body executes once");
        check(zero.target === C && zero.args.length === 0, "zero argc and newTarget");
        check(one.target === C && one.args.length === 1 && one.args[0] === a, "odd argv");
        check(two.args.length === 2 && two.args[0] === a && two.args[1] === b, "even argv");
        check(eight.args.length === 8 && eight.args[2] === c && eight.args[7] === b, "stack arguments");
        check(a.value === 11 && b.value === 22, "live caller references across GC");
        if (C === A) {
            check(
                zero.formals.every(value => value === undefined),
                "zero argc padding",
            );
            check(
                one.formals[0] === a && one.formals[1] === undefined && one.formals[2] === undefined,
                "missing formal padding",
            );
            check(
                eight.formals[0] === a && eight.formals[1] === b && eight.formals[2] === c,
                "extra arguments preserve formals",
            );
        }
        const pair = nested(C, a);
        check(pair.args[0].args[0] === a && pair.target === C, "nested calls retain argv");
    }
    for (const value of [undefined, null, 17, "primitive", Symbol("result"), BigInt(23), {}, function result() {}]) {
        bodyResult = value;
        const result = make1(A, 91);
        const objectResult = typeof value === "function" || (typeof value === "object" && value !== null);
        check(result === (objectResult ? value : lastReceiver), "base raw result completion");
    }
    bodyResult = undefined;
    collect = false;
}

// A fresh constructor has no usable instance layout yet: use the receiver helper.
function Fresh(x) {
    this.x = x;
    this.target = new.target;
}
check(make1(Fresh, 31).x === 31 && make1(Fresh, 32).target === Fresh, "fresh receiver helper");

// Invalidation can leave MachineCode installed after the compiled-entry flag is cleared.
class DepRoot {}
class DepMiddle extends DepRoot {}
class DepLeaf extends DepMiddle {}
DepRoot.prototype.value = 13;
const dep = new DepLeaf();
function Invalidated(x) {
    this.value = dep.value + x;
    this.target = new.target;
    return 42;
}
for (let i = 0; i < 24; i++) make1(Invalidated, i);
ArkTools.arkSteedCompileSync(Invalidated);
check(ArkTools.arkSteedIsCompiled(Invalidated), "compiled entry before invalidation");
check(make1(Invalidated, 2).value === 15, "compiled dependency body");
ArkTools.forceLazyDeopt(DepLeaf.prototype, 1, false);
check(!ArkTools.arkSteedIsCompiled(Invalidated), "entry invalidated");
const invalidated = make1(Invalidated, 3);
check(invalidated.value === 16 && invalidated.target === Invalidated, "stale MachineCode uses generic bridge");

let derivedRuns = 0;
let derivedResult = { marker: 71 };
class Parent {}
class Derived extends Parent {
    constructor(x) {
        derivedRuns++;
        if (x === -1) super();
        ArkTools.forceFullGC();
        return derivedResult;
    }
}
for (let tier = 0; tier < 2; tier++) {
    if (tier === 1) ArkTools.arkSteedCompileSync(Derived);
    const before = derivedRuns;
    check(make1(Derived, 1) === derivedResult, "derived object raw result");
    derivedResult = 19;
    let caught = false;
    try {
        make1(Derived, 1);
    } catch (error) {
        caught = error instanceof TypeError;
    }
    check(caught && derivedRuns === before + 2, "derived primitive throws once");
    derivedResult = { marker: 71 };
}
for (const caller of callers) check(ArkTools.arkSteedIsCompiled(caller), "dynamic caller remains compiled");
print("body_stub passed");
