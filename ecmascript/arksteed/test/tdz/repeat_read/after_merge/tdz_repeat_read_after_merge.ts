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
//! METHOD      compute
//! COUNT Throw 2
//! COUNT CallCommonStub GetStringFromConstPool 2
//! HAS Phi
//! HAS_NOT CallCommonStub Add
//! HAS LoadTaggedField
//! COMPILE_COUNT_GE *compute* 1
//! EXEC_COUNT_GE *compute* 1
//! DEOPT_COUNT *Deoptimize* 0
// Each arm checks the same lexical binding before the merge. The bytecode
// optimizer removes the redundant check on the post-merge load, leaving
// two throw paths for ArkSteed to lower.
// Warm the additions to avoid generic Add invalidating lexical-load facts.

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
        return a + x;   // reload after merge
    }
    for (let i = 0; i < 100; i++) { compute(true); compute(false); }
    ArkTools.arkSteedCompileSync(compute);

    return compute(true) + compute(false);
}

print(tdz_repeat_read_after_merge());
