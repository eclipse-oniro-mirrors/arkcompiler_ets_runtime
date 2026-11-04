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

//! METHOD dynamicConstruct
//! COUNT CallCommonStub NewThisObjectChecked 1
//! COUNT CallNewObjRangeBase 1
//! COUNT CallNewObjRangeDerived 1
//! CODEGEN_COUNT *CallNewObjRange*commonStub = ArkSteedCallConstructorBody* 2
//! HAS_NOT InitializeEmptyObject
//! HAS CallRuntime OptNewObjRange

function check(value, message) {
    if (!value) throw new Error(message);
}
let seenTarget;
let runs = 0;
function A(x) {
    runs++;
    seenTarget = new.target;
    this.x = x;
    return 7;
}
function B(x) {
    runs++;
    seenTarget = new.target;
    this.x = x + 1;
}
function Pair(x, y = 99) {
    runs++;
    this.x = x;
    this.y = y;
    this.argc = arguments.length;
}
function dynamicConstruct(C, x) {
    return new C(x);
}
for (let i = 0; i < 32; i++) dynamicConstruct(i % 2 ? A : B, i);
ArkTools.arkSteedCompileSync(dynamicConstruct);
check(ArkTools.arkSteedIsCompiled(dynamicConstruct), "compiled caller");
for (let i = 0; i < 64; i++) {
    const C = i % 2 ? A : B;
    const before = runs;
    const r = dynamicConstruct(C, i);
    check(r.x === i + (C === B ? 1 : 0) && runs === before + 1, "dynamic base");
    check(Object.getPrototypeOf(r) === C.prototype && seenTarget === C, "prototype and newTarget");
}
ArkTools.arkSteedCompileSync(A);
ArkTools.arkSteedCompileSync(B);
check(ArkTools.arkSteedIsCompiled(A) && ArkTools.arkSteedIsCompiled(B), "compiled dynamic bodies");
check(dynamicConstruct(A, 8).x === 8 && dynamicConstruct(B, 8).x === 9, "direct entries");
const pair = dynamicConstruct(Pair, 5);
check(pair.x === 5 && pair.y === 99 && pair.argc === 1, "argument mismatch fallback");
const bound = A.bind(null);
check(dynamicConstruct(bound, 10).x === 10 && seenTarget === A, "bound fallback");
let trapRuns = 0;
const proxy = new Proxy(A, {
    construct(target, args, nt) {
        trapRuns++;
        return Reflect.construct(target, args, nt);
    },
});
check(dynamicConstruct(proxy, 11).x === 11 && trapRuns === 1 && seenTarget === proxy, "proxy fallback");
check(dynamicConstruct(Array, 3).length === 3, "native fallback");
let caught = false;
try {
    dynamicConstruct(() => 1, 0);
} catch (e) {
    caught = e instanceof TypeError;
}
check(caught, "nonconstructor");
check(ArkTools.arkSteedIsCompiled(dynamicConstruct), "dynamic misses retain code");
print("dynamic_paths passed");
