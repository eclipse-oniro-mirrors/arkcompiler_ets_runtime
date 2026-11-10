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

//! PARAMS --compiler-jit-hotness-threshold=20 --compiler-jit-ic-feedback-threshold=20 --compiler-max-inline-bytecodes=0

// Function.call with two arguments uses CALL_THIS_ARG2_WITH_RETURN.
function callTarget(x, y) { return x + y; }
// Array.forEach passes value, index and array using CALL_THIS_ARG3_WITH_RETURN.
let visited = 0;
function visitTarget(value, index, array) { visited += value; }
// Function.apply with four arguments uses CALL_THIS_ARGV_WITH_RETURN.
function applyTarget(a, b, c, d) { return a + b + c + d; }

print(ArkTools.arkSteedIsCompiled(callTarget));
print(ArkTools.arkSteedIsCompiled(visitTarget));
print(ArkTools.arkSteedIsCompiled(applyTarget));
let callSum = 0;
let applySum = 0;
const values = [0];
const args = [0, 2, 3, 4];
for (let i = 0; i < 5000; i++) {
    callSum += callTarget.call(null, i, 3);
    values[0] = i;
    values.forEach(visitTarget);
    args[0] = i;
    applySum += applyTarget.apply(null, args);
}
ArkTools.waitAllJitCompileFinish();
print(callSum);
print(visited);
print(applySum);
print(ArkTools.arkSteedIsCompiled(callTarget));
print(ArkTools.arkSteedIsCompiled(visitTarget));
print(ArkTools.arkSteedIsCompiled(applyTarget));
