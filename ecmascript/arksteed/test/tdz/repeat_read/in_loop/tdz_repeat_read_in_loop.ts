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
//! COUNT_LE    BranchIfReferenceEqual  2
//! COUNT_LE    CallCommonStub   GetStringFromConstPool  2
//! METHOD compute
//! HAS LoadTaggedField
//! COMPILE_COUNT_GE *compute* 1
//! EXEC_COUNT_GE *compute* 1
//! DEOPT_COUNT *Deoptimize* 0
// Test: the frontend (ets_frontend f0bcbe524b) eliminates the redundant hole-checks
// for the repeated loop-body reads of the same non-hole lexical var. The loop back-edge
// keeps two guards (one per read in the body), asserted via COUNT_LE to tolerate further
// elimination.

declare function print(arg: number): string;

declare class ArkTools {
    static arkSteedCompileSync<T extends Function>(func: T): T;
}

function tdz_repeat_read_in_loop(): number {
    let x = 3;

    function compute(n: number): number {
        let s = 0;
        for (let i = 0; i < n; i++) {
            s += x + x;  // hole guards re-executed each iteration
        }
        return s;
    }
    ArkTools.arkSteedCompileSync(compute);

    return compute(3) + compute(0);
}

print(tdz_repeat_read_in_loop());
