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
// `call_compiled_lambda_poplexenv_1` compiles and invokes the function-expression
// closure `calc`, which defines the class `Doubler` per activation and returns
// `Doubler.scale(n)`. The closure is installed by `ArkTools.arkSteedCompileSync`
// before its first invocation, so its call site carries no monomorphic target
// feedback and lowers to the generic `Call` vertex observed in the dump of the
// caller. `COMPILE_COUNT_GE`/`EXEC_COUNT_GE` attribute compilation and entry of
// each claimed method; `DEOPT_COUNT *Deoptimize* 0` rules out a silent deopt into
// the interpreter. The pre-existing computed results and the expectation are
// unchanged.

//! PARAMS --compiler-jit-hotness-threshold=60000

//! METHOD call_compiled_lambda_poplexenv_1
//! HAS Call
//! COMPILE_COUNT_GE *call_compiled_lambda_poplexenv_1* 1
//! EXEC_COUNT_GE *call_compiled_lambda_poplexenv_1* 1

//! METHOD calc
//! COMPILE_COUNT_GE *calc* 1
//! EXEC_COUNT_GE *calc* 1

//! DEOPT_COUNT *Deoptimize* 0

// poplexenv in compiled lambda: class defined inside the lambda
let compiled = false;

function call_compiled_lambda_poplexenv_1(x: number): number {
    const calc = function(n: number): number {
        class Doubler {
            static factor: number = 2;
            static scale(v: number): number { return v * Doubler.factor; }
        }
        return Doubler.scale(n);
    };
    if (!compiled) {
        ArkTools.arkSteedCompileSync(calc);
        compiled = true;
    }
    return calc(x);
}

ArkTools.arkSteedCompileSync(call_compiled_lambda_poplexenv_1);
print(call_compiled_lambda_poplexenv_1(5));
print(call_compiled_lambda_poplexenv_1(0));
print(call_compiled_lambda_poplexenv_1(-7));
