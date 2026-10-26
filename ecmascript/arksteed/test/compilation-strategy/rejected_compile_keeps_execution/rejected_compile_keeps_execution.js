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

// Verify execution results and compilation isolation after rejection.
// The entry count below counts generator executions, not compilation requests.

// Generator functions are rejected by CompileDecision::IsSupportFunctionKind.
// Keep JIT enabled so this exercises function rejection, not the global switch.
let entries = 0;
function* rejected(value) {
    entries++;
    yield value + 1;
    return value + 2;
}
function supported(value) { return value * 3; }
function consume(value) {
    const iterator = rejected(value);
    const first = iterator.next();
    const last = iterator.next();
    const exhausted = iterator.next();
    return first.value === value + 1 && !first.done &&
        last.value === value + 2 && last.done &&
        exhausted.value === undefined && exhausted.done;
}
print(consume(10));
ArkTools.arkSteedCompileSync(rejected);
print(ArkTools.arkSteedIsCompiled(rejected));
let correct = true;
for (let i = 0; i < 2000; i++) { correct = consume(i) && correct; }
ArkTools.waitAllJitCompileFinish();
print(correct);
print(entries);
print(ArkTools.arkSteedIsCompiled(rejected));
ArkTools.arkSteedCompileAsync(rejected);
ArkTools.waitAllJitCompileFinish();
print(consume(40));
print(entries);
print(ArkTools.arkSteedIsCompiled(rejected));
// Rejection of one method must not disable compilation for other methods.
for (let i = 0; i < 32; i++) { supported(i); }
ArkTools.arkSteedCompileSync(supported);
print(ArkTools.arkSteedIsCompiled(supported));
print(supported(14));
