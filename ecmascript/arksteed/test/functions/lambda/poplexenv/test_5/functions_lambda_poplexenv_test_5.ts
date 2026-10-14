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
// `call_compiled_lambda_poplexenv_5` compiles and invokes the function-expression
// closure `calc`, whose class `Ops` supplies the branch methods; the closure is
// installed before its first invocation, so its call site lowers to the generic
// `Call` vertex observed in the caller dump. `COMPILE_COUNT_GE`/`EXEC_COUNT_GE`
// attribute compilation and entry of each claimed method; `DEOPT_COUNT
// *Deoptimize* 0` rules out a silent deopt into the interpreter. The pre-existing
// computed results and the expectation are unchanged.

//! PARAMS --compiler-jit-hotness-threshold=60000

//! METHOD call_compiled_lambda_poplexenv_5
//! HAS Call
//! COMPILE_COUNT_GE *call_compiled_lambda_poplexenv_5* 1
//! EXEC_COUNT_GE *call_compiled_lambda_poplexenv_5* 1

//! METHOD calc
//! COMPILE_COUNT_GE *calc* 1
//! EXEC_COUNT_GE *calc* 1

//! DEOPT_COUNT *Deoptimize* 0

// poplexenv in lambda with branching: class method feeds multi-branch logic
let compiled = false;

function call_compiled_lambda_poplexenv_5(op: number, a: number, b: number): number {
    const calc = function(o: number, x: number, y: number): number {
        class Ops {
            static opId: number = 0;
            static add(v1: number, v2: number): number { return v1 + v2 + Ops.opId; }
            static sub(v1: number, v2: number): number { return v1 - v2 - Ops.opId; }
            static mul(v1: number, v2: number): number { return v1 * v2 + Ops.opId; }
        }
        if (o == 1) {
            return Ops.add(x, y);
        } else if (o == 2) {
            return Ops.sub(x, y);
        } else if (o == 3) {
            return Ops.mul(x, y);
        }
        return -1;
    };
    if (!compiled) {
        ArkTools.arkSteedCompileSync(calc);
        compiled = true;
    }
    return calc(op, a, b);
}

ArkTools.arkSteedCompileSync(call_compiled_lambda_poplexenv_5);
print(call_compiled_lambda_poplexenv_5(1, 5, 3));
print(call_compiled_lambda_poplexenv_5(2, 10, 4));
print(call_compiled_lambda_poplexenv_5(3, 7, 2));
print(call_compiled_lambda_poplexenv_5(9, 1, 2));
