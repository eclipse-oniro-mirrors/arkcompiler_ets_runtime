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

// G11.6 (P0) additive call evidence for `functions/lambda`
// (`arksteed-g116-call-closure-and-additive-evidence` C1/C3/C4; S11; V1/V2).
// `call_compiled_lambda_8` compiles the nested closures `level1`, `level2` and
// `level3`, which share the read-only captured `outerFactor`; `level1` invokes
// `level2` and `level2` invokes `level3`. All three are installed before their
// first invocation, so the call sites lower to the generic `Call` vertices
// observed in the caller dump. `COMPILE_COUNT_GE`/`EXEC_COUNT_GE` attribute
// compilation and entry of each claimed method; `DEOPT_COUNT *Deoptimize* 0`
// rules out a silent deopt into the interpreter. The pre-existing computed
// results and the expectation are unchanged.

//! PARAMS --compiler-jit-hotness-threshold=60000

//! METHOD call_compiled_lambda_8
//! HAS Call
//! COMPILE_COUNT_GE *call_compiled_lambda_8* 1
//! EXEC_COUNT_GE *call_compiled_lambda_8* 1

//! METHOD level1
//! COMPILE_COUNT_GE *level1* 1
//! EXEC_COUNT_GE *level1* 1

//! METHOD level2
//! COMPILE_COUNT_GE *level2* 1
//! EXEC_COUNT_GE *level2* 1

//! METHOD level3
//! COMPILE_COUNT_GE *level3* 1
//! EXEC_COUNT_GE *level3* 1

//! DEOPT_COUNT *Deoptimize* 0

// Deeply nested lambdas: 3 levels at same scope, all capturing outerFactor via closure
let compiled = false;

function call_compiled_lambda_8(base: number, mult: number): number {
    let outerFactor: number = 3;
    const level3 = function(a: number, b: number, c: number): number {
        return (a + b + c) * outerFactor;
    };
    const level2 = function(a: number, mf: number): number {
        let b: number = a + outerFactor;
        return level3(a, b, b * mf);
    };
    const level1 = function(x: number): number {
        let midFactor: number = outerFactor - 1;
        return level2(x + 1, midFactor);
    };
    if (!compiled) {
        ArkTools.arkSteedCompileSync(level1);
        ArkTools.arkSteedCompileSync(level2);
        ArkTools.arkSteedCompileSync(level3);
        compiled = true;
    }
    return level1(base) * mult;
}

ArkTools.arkSteedCompileSync(call_compiled_lambda_8);
print(call_compiled_lambda_8(2, 1));
print(call_compiled_lambda_8(5, 2));
print(call_compiled_lambda_8(1, 10));
