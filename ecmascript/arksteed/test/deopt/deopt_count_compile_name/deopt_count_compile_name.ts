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

// Regression for the DEOPT_COUNT evidence set (ISSUE-0018). A method named
// `DeoptimizeTarget` shows up in the compilation markers
// (`Starts compiling: DeoptimizeTarget`), in the execution markers
// (`>>> Running method: ...DeoptimizeTarget`) and in the generated-code log,
// but none of those lines is a deopt event. The method is compiled and
// executed here (COMPILE_COUNT/EXEC_COUNT are satisfied) while `DEOPT_COUNT
// *Deoptimize* 0` must stay green: before the fix the collector put every
// compiler-component line into the deopt evidence and reported `found 1`.

//! PARAMS --compiler-jit-hotness-threshold=60000

//! METHOD DeoptimizeTarget
//! COMPILE_COUNT DeoptimizeTarget 1
//! EXEC_COUNT *DeoptimizeTarget 2
//! DEOPT_COUNT *Deoptimize* 0

function DeoptimizeTarget(n: number): number {
    let sum = 0;
    for (let i = 0; i < n; i++) {
        sum += i;
    }
    return sum;
}

ArkTools.arkSteedCompileSync(DeoptimizeTarget);
print(DeoptimizeTarget(10));
print(DeoptimizeTarget(10));
print(ArkTools.arkSteedIsCompiled(DeoptimizeTarget));
print("OK");
