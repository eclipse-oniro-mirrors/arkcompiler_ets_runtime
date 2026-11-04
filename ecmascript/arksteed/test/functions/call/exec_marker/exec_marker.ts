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

// @ts-nocheck
declare function print(value: any): void;

// Evidence infrastructure (E1/E2/E7): the ArkSteed entry-block execution marker
// (CallRuntime PrintMethodName) prints `>>> Running method: <record>.<method>`
// once per activation of the generated code. EXEC_COUNT asserts the exact
// number of executions, COMPILE_COUNT the number of compilations, and
// DEOPT_COUNT 0 that the normal path never deoptimizes into the interpreter.

//! PARAMS --compiler-jit-hotness-threshold=60000

//! METHOD target
//! HAS CallRuntime PrintMethodName
//! COMPILE_COUNT target 1
//! EXEC_COUNT *target 3
//! DEOPT_COUNT *Deoptimize* 0
function target(n: number): number {
    let sum = 0;
    for (let i = 0; i < n; i++) {
        sum += i;
    }
    return sum;
}

//! METHOD recursive
//! HAS CallRuntime PrintMethodName
//! COMPILE_COUNT recursive 1
//! EXEC_COUNT *recursive 5
//! DEOPT_COUNT *Deoptimize* 0
function recursive(n: number): number {
    if (n <= 0) {
        return 0;
    }
    return n + recursive(n - 1);
}

// Warm up without crossing the hotness threshold, so the functions run in the
// interpreter until explicitly compiled (no marker is printed during warm-up).
for (let i = 0; i < 20; i++) {
    target(10);
}
ArkTools.arkSteedCompileSync(target);
print(target(10));
print(target(10));
print(target(10));
print(ArkTools.arkSteedIsCompiled(target));

for (let i = 0; i < 20; i++) {
    recursive(4);
}
ArkTools.arkSteedCompileSync(recursive);
print(recursive(4));
print(ArkTools.arkSteedIsCompiled(recursive));
