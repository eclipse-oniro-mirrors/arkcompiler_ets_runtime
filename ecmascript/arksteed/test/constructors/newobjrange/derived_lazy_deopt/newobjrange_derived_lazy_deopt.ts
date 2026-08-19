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

//! METHOD constructDerived
//! HAS CallNewObjRangeBase
//! HAS CallNewObjRangeDerived
//! HAS_NOT InitializeEmptyObject
//! METHOD constructCold
//! HAS CallNewObjRangeBase
//! HAS CallNewObjRangeDerived
//! HAS_NOT InitializeEmptyObject

function check(value, message) {
    if (!value) throw new Error(message);
}
class DepRoot {}
class DepMiddle extends DepRoot {}
class DepLeaf extends DepMiddle {}
DepRoot.prototype.x = 17;
const dependency = new DepLeaf();
let armed = false;
let mode = 0;
let runs = 0;
let finalRuns = 0;
let returned = { token: 91 };
class Parent {}
class Derived extends Parent {
    constructor() {
        if (runs < 0) super();
        runs++;
        if (armed) {
            armed = false;
            ArkTools.forceLazyDeopt(DepLeaf.prototype, 1, false);
            ArkTools.forceFullGC();
        }
        if (mode === 1) return 42;
        if (mode === 2) throw returned;
        if (mode === 3) return Symbol("primitive");
        return returned;
    }
}
function constructDerived(dep, local) {
    const dependencyValue = dep.x;
    const onlyCatch = local + 100;
    try {
        const value = new Derived();
        return [value === returned, dependencyValue, runs];
    } catch (error) {
        return [mode === 1 || mode === 3 ? error instanceof TypeError : error === returned, onlyCatch, runs];
    } finally {
        finalRuns++;
    }
}
for (let phase = 0; phase < 4; phase++) {
    mode = 0;
    for (let i = 0; i < 24; i++) constructDerived(dependency, i);
    ArkTools.arkSteedCompileSync(Derived);
    ArkTools.arkSteedCompileSync(constructDerived);
    check(ArkTools.arkSteedIsCompiled(constructDerived), "compiled caller");
    mode = phase;
    const before = runs;
    const beforeFinally = finalRuns;
    armed = true;
    const result = constructDerived(dependency, 37);
    check(
        result[0] && result[1] === (phase === 0 ? 17 : 137) && result[2] === before + 1,
        "lazy result, exception, catch-only value and exactly once",
    );
    check(!ArkTools.arkSteedIsCompiled(constructDerived), "dependency deoptimized caller");
    check(finalRuns === beforeFinally + 1, "finally exactly once after normal or abrupt recovery");
}
// Also exercise the compiled cold completion edge: no dependency invalidation, and
// no exception from the body. The first TypeError must preserve catch/finally locals.
function constructCold(local) {
    const onlyCatch = local + 200;
    try {
        return new Derived() === returned;
    } catch (error) {
        return error instanceof TypeError && onlyCatch === 237;
    } finally {
        finalRuns++;
    }
}
mode = 0;
for (let i = 0; i < 24; i++) constructCold(i);
ArkTools.arkSteedCompileSync(Derived);
ArkTools.arkSteedCompileSync(constructCold);
check(ArkTools.arkSteedIsCompiled(constructCold), "compiled cold completion caller");
const beforeCold = runs;
const beforeColdFinally = finalRuns;
mode = 1;
check(constructCold(37), "cold TypeError preserves catch-only value");
check(runs === beforeCold + 1 && finalRuns === beforeColdFinally + 1, "cold completion exactly once");
check(ArkTools.arkSteedIsCompiled(constructCold), "cold completion uses compiled catch");
print("derived_lazy_deopt passed");
