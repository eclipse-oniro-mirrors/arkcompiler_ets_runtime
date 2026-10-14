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
// `call_compiled_lambda_with_try_catch_3` compiles the class `OuterCalc` and its
// static method `adjust`, invokes the closure factory `factory`, and invokes the
// returned anonymous closure, which calls `OuterCalc.adjust`. The class
// constructor is compiled through the existing
// `ArkTools.arkSteedCompileSync(OuterCalc)` call but is never constructed, so it
// has no entry evidence and is not claimed; the invoked factory and static
// `adjust` are. The claimed methods are installed before their first invocation,
// so the call sites lower to the generic `Call` vertices observed in the caller
// dump. `COMPILE_COUNT_GE`/`EXEC_COUNT_GE` attribute compilation and entry of each
// claimed method; `DEOPT_COUNT *Deoptimize* 0` rules out a silent deopt into the
// interpreter. The pre-existing computed results and the expectation are
// unchanged.

//! PARAMS --compiler-jit-hotness-threshold=60000

//! METHOD call_compiled_lambda_with_try_catch_3
//! HAS Call
//! COMPILE_COUNT_GE *call_compiled_lambda_with_try_catch_3* 1
//! EXEC_COUNT_GE *call_compiled_lambda_with_try_catch_3* 1

//! METHOD adjust
//! COMPILE_COUNT_GE *adjust* 1
//! EXEC_COUNT_GE *adjust* 1

//! METHOD factory
//! COMPILE_COUNT_GE *factory* 1
//! EXEC_COUNT_GE *factory* 1

//! DEOPT_COUNT *Deoptimize* 0

// Multi-level poplexenv: class in function + class in nested lambda
let compiled = false;

function call_compiled_lambda_with_try_catch_3(base: number, mode: number): number {
    class OuterCalc {
        static offset: number = 5;
        static adjust(x: number): number {
            return x + OuterCalc.offset;
        }
    }
    const factory = function(inc: number) {
        class InnerCalc {
            static scale: number = 2;
            static transform(v: number): number {
                return v * InnerCalc.scale + inc;
            }
        }
        return function(x: number): number {
            return InnerCalc.transform(OuterCalc.adjust(x));
        };
    };
    if (!compiled) {
        ArkTools.arkSteedCompileSync(OuterCalc);
        ArkTools.arkSteedCompileSync(OuterCalc.adjust);
        ArkTools.arkSteedCompileSync(factory);
        compiled = true;
    }
    const fn = factory(base);
    return fn(mode);
}

ArkTools.arkSteedCompileSync(call_compiled_lambda_with_try_catch_3);
print(call_compiled_lambda_with_try_catch_3(3, 1));
print(call_compiled_lambda_with_try_catch_3(0, 10));
print(call_compiled_lambda_with_try_catch_3(7, 2));
