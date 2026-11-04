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
// `call_compiled_lambda_9` compiles and invokes the function-expression closure
// `calc`, which branches on its arguments and reads no captured binding. `calc`
// is installed before its first invocation, so the call site lowers to the
// generic `Call` vertex observed in the caller dump.
// `COMPILE_COUNT_GE`/`EXEC_COUNT_GE` attribute compilation and entry of each
// claimed method; `DEOPT_COUNT *Deoptimize* 0` rules out a silent deopt into the
// interpreter. The pre-existing computed results and the expectation are
// unchanged.

//! PARAMS --compiler-jit-hotness-threshold=60000

//! METHOD call_compiled_lambda_9
//! HAS Call
//! COMPILE_COUNT_GE *call_compiled_lambda_9* 1
//! EXEC_COUNT_GE *call_compiled_lambda_9* 1

//! METHOD calc
//! COMPILE_COUNT_GE *calc* 1
//! EXEC_COUNT_GE *calc* 1

//! DEOPT_COUNT *Deoptimize* 0

// Lambda with multi-branch if-else chain
let compiled = false;

function call_compiled_lambda_9(op: number, a: number, b: number): number {
    const calc = function(o: number, x: number, y: number): number {
        if (o == 1) {
            return x + y;
        } else if (o == 2) {
            return x - y;
        } else if (o == 3) {
            return x * y;
        } else {
            let s: number = x;
            for (let i: number = 1; i < y; i = i + 1) {
                s = s * x;
            }
            return s;  // pow when o == 4
        }
    };
    if (!compiled) {
        ArkTools.arkSteedCompileSync(calc);
        compiled = true;
    }
    return calc(op, a, b);
}

ArkTools.arkSteedCompileSync(call_compiled_lambda_9);
print(call_compiled_lambda_9(1, 3, 4));
print(call_compiled_lambda_9(2, 10, 6));
print(call_compiled_lambda_9(3, 5, 7));
print(call_compiled_lambda_9(4, 2, 5));
