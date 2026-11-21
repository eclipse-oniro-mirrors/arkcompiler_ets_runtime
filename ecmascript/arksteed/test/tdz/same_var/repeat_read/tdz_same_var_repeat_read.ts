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
//! COUNT_LE    BranchIfReferenceEqual  1
//! COUNT_LE    CallCommonStub   GetStringFromConstPool  1
//! HAS_NOT CallCommonStub Add
//! HAS LoadTaggedField
//! COMPILE_COUNT_GE *compute* 1
//! EXEC_COUNT_GE *compute* 1
//! DEOPT_COUNT *Deoptimize* 0
// Warm the arithmetic before synchronous compilation so additions use numeric
// IR. A generic Add can invoke user code and must invalidate lexical-load facts;
// this case checks redundant hole checks without that side effect.

declare function print(arg: number): string;

declare class ArkTools {
    static arkSteedCompileSync<T extends Function>(func: T): T;
}

function tdz_same_var_repeat_read(offset: number): number {
    let x = 7;

    function compute(): number {
        return x + x + x;  // three ldlexvar + three throw.undefinedifholewithname on one value
    }
    for (let i = 0; i < 100; i++) { compute(); }
    ArkTools.arkSteedCompileSync(compute);

    return compute() + offset;
}

print(tdz_same_var_repeat_read(0));
print(tdz_same_var_repeat_read(4));
