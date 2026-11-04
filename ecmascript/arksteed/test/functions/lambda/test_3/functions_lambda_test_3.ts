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
// `call_compiled_lambda_3` compiles and invokes the arrow-function closure `foo`,
// which computes the product of its arguments and reads no captured binding.
// `foo` is installed before its first invocation, so the call site lowers to the
// generic `Call` vertex observed in the caller dump.
// `COMPILE_COUNT_GE`/`EXEC_COUNT_GE` attribute compilation and entry of each
// claimed method; `DEOPT_COUNT *Deoptimize* 0` rules out a silent deopt into the
// interpreter. The pre-existing computed results and the expectation are
// unchanged.

//! PARAMS --compiler-jit-hotness-threshold=60000

//! METHOD call_compiled_lambda_3
//! HAS Call
//! COMPILE_COUNT_GE *call_compiled_lambda_3* 1
//! EXEC_COUNT_GE *call_compiled_lambda_3* 1

//! METHOD foo
//! COMPILE_COUNT_GE *foo* 1
//! EXEC_COUNT_GE *foo* 1

//! DEOPT_COUNT *Deoptimize* 0
let compiled = false;

function call_compiled_lambda_3(x: number): number {
    const foo = (a: number, b: number) => a * b;
    if (!compiled) {
        ArkTools.arkSteedCompileSync(foo);
        compiled = true;
    }
    return foo(x - 1, x + 2);
}

ArkTools.arkSteedCompileSync(call_compiled_lambda_3);
print(call_compiled_lambda_3(-2.5));
print(call_compiled_lambda_3(15));
