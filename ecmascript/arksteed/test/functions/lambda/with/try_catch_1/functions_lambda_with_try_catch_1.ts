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
// `call_compiled_lambda_with_try_catch_1` compiles the class `LocalMath` and its
// static method `apply`, together with the closure `doubler`, and invokes
// `doubler`; `doubler` invokes the static `apply`. The class constructor is
// compiled through the existing `ArkTools.arkSteedCompileSync(LocalMath)` call
// but is never constructed, so it has no entry evidence and is not claimed; the
// invoked static `apply` is. The claimed methods are installed before their first
// invocation, so the call sites lower to the generic `Call` vertices observed in
// the caller dump. `COMPILE_COUNT_GE`/`EXEC_COUNT_GE` attribute compilation and
// entry of each claimed method; `DEOPT_COUNT *Deoptimize* 0` rules out a silent
// deopt into the interpreter. The pre-existing computed results and the
// expectation are unchanged.

//! PARAMS --compiler-jit-hotness-threshold=60000

//! METHOD call_compiled_lambda_with_try_catch_1
//! HAS Call
//! COMPILE_COUNT_GE *call_compiled_lambda_with_try_catch_1* 1
//! EXEC_COUNT_GE *call_compiled_lambda_with_try_catch_1* 1

//! METHOD doubler
//! COMPILE_COUNT_GE *doubler* 1
//! EXEC_COUNT_GE *doubler* 1

//! METHOD apply
//! COMPILE_COUNT_GE *apply* 1
//! EXEC_COUNT_GE *apply* 1

//! DEOPT_COUNT *Deoptimize* 0

// poplexenv inside compiled function: class defined inside function scope
let compiled = false;

function call_compiled_lambda_with_try_catch_1(x: number): number {
    class LocalMath {
        static factor: number = 2;
        static apply(n: number): number {
            return n * LocalMath.factor;
        }
    }
    const doubler = function(n: number): number {
        return LocalMath.apply(n);
    };
    if (!compiled) {
        ArkTools.arkSteedCompileSync(LocalMath);
        ArkTools.arkSteedCompileSync(LocalMath.apply);
        ArkTools.arkSteedCompileSync(doubler);
        compiled = true;
    }
    return doubler(x);
}

ArkTools.arkSteedCompileSync(call_compiled_lambda_with_try_catch_1);
print(call_compiled_lambda_with_try_catch_1(5));
print(call_compiled_lambda_with_try_catch_1(10));
print(call_compiled_lambda_with_try_catch_1(-3));
