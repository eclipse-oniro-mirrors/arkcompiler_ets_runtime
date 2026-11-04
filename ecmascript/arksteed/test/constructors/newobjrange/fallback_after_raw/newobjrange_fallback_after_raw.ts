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

//! METHOD runLoop
//! COUNT CallCommonStub NewThisObjectChecked 3
//! COUNT CallNewObjRangeBase 3
//! COUNT CallNewObjRangeDerived 3
//! CODEGEN_COUNT *CallNewObjRange*commonStub = ArkSteedCallConstructorBody* 6
//! HAS CallRuntime OptNewObjRange

function check(value, message) {
    if (!value) throw new Error(message);
}
class DepRoot {}
class DepMiddle extends DepRoot {}
class DepLeaf extends DepMiddle {}
DepRoot.prototype.x = 13;
const dependency = new DepLeaf();
let armed = false;
let runs = 0;
let secondReceiver;
function First(x) {
    runs++;
    this.x = x;
}
function Second(x) {
    runs++;
    this.x = x + 100;
    secondReceiver = this;
    if (armed) {
        armed = false;
        ArkTools.forceLazyDeopt(DepLeaf.prototype, 1, false);
        ArkTools.forceFullGC();
    }
    return 4;
}
const Bound = Second.bind(null);
const targets = [First, Bound];
function runLoop(dep) {
    const n = dep.x;
    let previous;
    let last;
    for (let i = 0; i < 2; i++) {
        previous = last;
        const C = targets[i];
        last = new C(n + i);
    }
    return [previous.x, last.x, last === secondReceiver, last !== previous];
}
for (let i = 0; i < 32; i++) runLoop(dependency);
ArkTools.arkSteedCompileSync(First);
ArkTools.arkSteedCompileSync(runLoop);
const before = runs;
armed = true;
const value = runLoop(dependency);
check(value[0] === 13 && value[1] === 114 && value[2] && value[3], "fallback has no previous raw marker");
check(runs === before + 2 && !ArkTools.arkSteedIsCompiled(runLoop), "resume loop exactly once");
print("fallback_after_raw passed");
