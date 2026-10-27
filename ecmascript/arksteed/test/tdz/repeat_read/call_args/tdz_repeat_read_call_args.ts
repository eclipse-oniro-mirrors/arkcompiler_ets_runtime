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

//! METHOD      compute
//! COUNT_LE    BranchIfReferenceEqual  1
//! COUNT_LE    CallCommonStub   GetStringFromConstPool  1
//! METHOD compute
//! HAS LoadTaggedField
//! COMPILE_COUNT_GE *compute* 1
//! EXEC_COUNT_GE *compute* 1
//! DEOPT_COUNT *Deoptimize* 0
// Note: the frontend (ets_frontend f0bcbe524b) eliminates the redundant hole-checks
// for the repeated argument reads of the same non-hole lexical var, so at most one
// throw.undefinedifholewithname reaches the JIT (COUNT_LE tolerates further elimination).

declare function print(arg: number): string;

declare class ArkTools {
    static arkSteedCompileSync<T extends Function>(func: T): T;
}

function tdz_repeat_read_call_args(): number {
    let x = 3;

    function add3(a: number, b: number, c: number): number {
        return a + b + c;
    }

    function compute(): number {
        return add3(x, x, x);
    }
    ArkTools.arkSteedCompileSync(compute);

    return compute();
}

print(tdz_repeat_read_call_args());
