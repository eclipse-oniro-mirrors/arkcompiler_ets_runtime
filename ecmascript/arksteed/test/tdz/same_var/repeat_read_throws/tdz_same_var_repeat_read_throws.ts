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
// Test: throw.undefinedifholewithname on the SAME value, accessed BEFORE initialization.
// The frontend (ets_frontend f0bcbe524b) deduplicates the repeated reads of the same
// hole lexical var in one basic block, so at most one throw.undefinedifholewithname
// reaches the JIT (COUNT_LE tolerates further elimination); it fires on the first read.
// The call must throw a ReferenceError caught by the caller.

declare function print(arg: string): void;

declare class ArkTools {
    static arkSteedCompileSync<T extends Function>(func: T): T;
}

function tdz_same_var_repeat_read_throws(): void {
    function compute(): number {
        return x + x;  // TDZ: x not yet initialized; first hole guard throws
    }
    ArkTools.arkSteedCompileSync(compute);

    try {
        compute();
    } catch (e) {
        print(e.name);
    }

    let x = 1;
}

tdz_same_var_repeat_read_throws();
