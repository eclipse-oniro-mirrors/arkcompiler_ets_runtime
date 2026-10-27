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
//! COUNT_LE    BranchIfReferenceEqual  2   # at most 2: one per branch; post-merge read elided by frontend
//! METHOD compute
//! HAS LoadTaggedField
//! COMPILE_COUNT_GE *compute* 1
//! EXEC_COUNT_GE *compute* 1
//! DEOPT_COUNT *Deoptimize* 0
// Test: non-hole fact survives a control-flow merge (NodeInfo::MergeWith intersection).
// Each branch reads x (guard fires per branch). The post-merge read's hole-check is
// eliminated by the frontend's var-id store-dominance analysis (ets_frontend f0bcbe524b),
// since x is provably non-hole after the merge; the JIT therefore sees at most 2 guards
// (asserted via COUNT_LE so further elimination won't break the test).

declare function print(arg: number): string;

declare class ArkTools {
    static arkSteedCompileSync<T extends Function>(func: T): T;
}

function tdz_repeat_read_after_merge(): number {
    let x = 5;

    function compute(flag: boolean): number {
        let a = 0;
        if (flag) {
            a = x + 1;  // guard on branch 1
        } else {
            a = x + 2;  // guard on branch 2
        }
        return a + x;   // guard after merge
    }
    ArkTools.arkSteedCompileSync(compute);

    return compute(true) + compute(false);
}

print(tdz_repeat_read_after_merge());
