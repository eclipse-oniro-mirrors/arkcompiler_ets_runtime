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

function check(value, message) {
    if (!value) throw new Error(message);
}
class DepRoot {}
class DepMiddle extends DepRoot {}
class DepLeaf extends DepMiddle {}
DepRoot.prototype.x = 3;
const dependency = new DepLeaf();
let armed = false;
let runs = 0;
function Target(x) {
    const inherited = dependency.x;
    runs++;
    this.x = x + inherited;
    if (armed) {
        armed = false;
        ArkTools.forceLazyDeopt(DepLeaf.prototype, 1, false);
    }
}
function makeTarget(x) {
    return new Target(x);
}
for (let i = 0; i < 32; i++) makeTarget(i);
ArkTools.arkSteedCompileSync(makeTarget);
check(makeTarget(5).x === 8 && !ArkTools.arkSteedIsCompiled(Target), "interpreted body");
ArkTools.arkSteedCompileSync(Target);
check(ArkTools.arkSteedIsCompiled(Target), "installed Steed body");
check(makeTarget(6).x === 9, "direct body");
const before = runs;
armed = true;
check(makeTarget(7).x === 10 && runs === before + 1, "target deopt while caller stays active");
check(!ArkTools.arkSteedIsCompiled(Target), "entry invalidated");
check(makeTarget(8).x === 11 && runs === before + 2, "stale MachineCode cannot select direct entry");
check(ArkTools.arkSteedIsCompiled(makeTarget), "caller retained");
print("target_invalidation passed");
