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
// `call_compiled_lambda_with_this_nested` compiles and invokes the method
// `buildNested`, which compiles and returns `outer`; `outer` compiles and invokes
// the lexical-`this` closure `inner`. All four are installed before their first
// invocation, so the call sites lower to the generic `Call` vertices observed in
// the caller dump. `COMPILE_COUNT_GE`/`EXEC_COUNT_GE` attribute compilation and
// entry of each claimed method; `DEOPT_COUNT *Deoptimize* 0` rules out a silent
// deopt into the interpreter. The pre-existing computed results and the
// expectation are unchanged.

//! PARAMS --compiler-jit-hotness-threshold=60000

//! METHOD call_compiled_lambda_with_this_nested
//! HAS Call
//! COMPILE_COUNT_GE *call_compiled_lambda_with_this_nested* 1
//! EXEC_COUNT_GE *call_compiled_lambda_with_this_nested* 1

//! METHOD buildNested
//! COMPILE_COUNT_GE *buildNested* 1
//! EXEC_COUNT_GE *buildNested* 1

//! METHOD outer
//! COMPILE_COUNT_GE *outer* 1
//! EXEC_COUNT_GE *outer* 1

//! METHOD inner
//! COMPILE_COUNT_GE *inner* 1
//! EXEC_COUNT_GE *inner* 1

//! DEOPT_COUNT *Deoptimize* 0
let call_compiled_lambda_with_this_nested_outer_compiled = false;
let call_compiled_lambda_with_this_nested_inner_compiled = false;

class CallCompiledLambdaWithThisNestedBox {
    base: number;

    constructor(base: number) {
        this.base = base;
    }

    buildNested(factor: number): () => number {
        const outer = () => {
            const inner = () => this.base * factor;
            if (!call_compiled_lambda_with_this_nested_inner_compiled) {
                ArkTools.arkSteedCompileSync(inner);
                call_compiled_lambda_with_this_nested_inner_compiled = true;
            }
            return inner();
        };
        if (!call_compiled_lambda_with_this_nested_outer_compiled) {
            ArkTools.arkSteedCompileSync(outer);
            call_compiled_lambda_with_this_nested_outer_compiled = true;
        }
        return outer;
    }
}

function call_compiled_lambda_with_this_nested(): void {
    const box1 = new CallCompiledLambdaWithThisNestedBox(6);
    const box2 = new CallCompiledLambdaWithThisNestedBox(7);
    print(box1.buildNested(4)());
    print(box2.buildNested(5)());
}

ArkTools.arkSteedCompileSync(call_compiled_lambda_with_this_nested);
ArkTools.arkSteedCompileSync(CallCompiledLambdaWithThisNestedBox.prototype.buildNested);

call_compiled_lambda_with_this_nested();
