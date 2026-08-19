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
//! METHOD construct
// Sixteen explicit compilations, each with one dynamic constructor body call.
//! COUNT CallCommonStub NewThisObjectChecked 16
//! COUNT CallNewObjRangeBase 16
//! COUNT CallNewObjRangeDerived 16
//! CODEGEN_COUNT *CallNewObjRange*commonStub = ArkSteedCallConstructorBody* 32

function check(value, message) {
    if (!value) throw new Error(message);
}
class Root {}
class Middle extends Root {}
class Leaf extends Middle {}
Root.prototype.x = 17;
const dependency = new Leaf();
let armed = false;
let mode = 0;
let runs = 0;
let finalRuns = 0;
let returned = { marker: 81 };
let lastReceiver;
function invalidate() {
    if (armed) {
        armed = false;
        ArkTools.forceLazyDeopt(Leaf.prototype, 1, false);
        ArkTools.forceFullGC();
    }
}
function Base(x, y) {
    runs++;
    this.x = x;
    this.y = y;
    lastReceiver = this;
    invalidate();
    if (mode === 2) throw returned;
    if (mode === 1) return returned;
    return 42;
}
class Parent {}
class Derived extends Parent {
    constructor(x, y) {
        if (mode < 0) super();
        runs++;
        invalidate();
        if (mode === 2) throw returned;
        if (mode === 3) return 42;
        return returned;
    }
}
function construct(C, dep, local) {
    const value = dep.x;
    const onlyCatch = { saved: local };
    try {
        const result = new C(local, onlyCatch);
        return [result, value, local];
    } catch (error) {
        return [error, onlyCatch.saved, local];
    } finally {
        finalRuns++;
    }
}

// Each tier/mode invalidates the dynamic caller through the common stub frame.
for (let tier = 0; tier < 2; tier++) {
    if (tier === 1) {
        ArkTools.arkSteedCompileSync(Base);
        ArkTools.arkSteedCompileSync(Derived);
        check(ArkTools.arkSteedIsCompiled(Base) && ArkTools.arkSteedIsCompiled(Derived), "compiled bodies");
    }
    for (const C of [Base, Derived]) {
        for (const completion of [0, 1, 2, 3]) {
            mode = 0;
            for (let i = 0; i < 24; i++) construct(i % 2 === 0 ? Base : Derived, dependency, i);
            ArkTools.arkSteedCompileSync(construct);
            check(ArkTools.arkSteedIsCompiled(construct), "compiled dynamic caller");
            const local = { token: 99 };
            const before = runs;
            const beforeFinally = finalRuns;
            mode = completion;
            armed = true;
            const result = construct(C, dependency, local);
            check(!ArkTools.arkSteedIsCompiled(construct), "caller return PC was patched");
            check(runs === before + 1 && finalRuns === beforeFinally + 1, "body and finally execute once");
            check(result[2] === local && local.token === 99, "live local survives GC and deopt");
            if (completion === 2 || (C === Derived && completion === 3)) {
                check(result[1] === local, "catch-only local restored");
                check(
                    completion === 2 ? result[0] === returned : result[0] instanceof TypeError,
                    "abrupt result recovery",
                );
            } else {
                check(result[1] === 17, "normal continuation");
                const expected = C === Derived || completion === 1 ? returned : lastReceiver;
                check(result[0] === expected, "raw construct result completed once");
                if (expected === lastReceiver) {
                    check(expected.x === local && expected.y.saved === local, "receiver restored");
                }
            }
        }
    }
}
print("body_stub_deopt passed");
