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
// `call_compiled_lambda_with_this_method` compiles and invokes the method
// `buildAdder`, which compiles and returns the lexical-`this` closure `foo`; the
// caller then invokes `foo`. All three are installed before their first
// invocation, so the call sites lower to the generic `Call` vertices observed in
// the caller dump. `COMPILE_COUNT_GE`/`EXEC_COUNT_GE` attribute compilation and
// entry of each claimed method; `DEOPT_COUNT *Deoptimize* 0` rules out a silent
// deopt into the interpreter. The pre-existing computed results and the
// expectation are unchanged.

//! PARAMS --compiler-jit-hotness-threshold=60000

//! METHOD call_compiled_lambda_with_this_method
//! HAS Call
//! COMPILE_COUNT_GE *call_compiled_lambda_with_this_method* 1
//! EXEC_COUNT_GE *call_compiled_lambda_with_this_method* 1

//! METHOD buildAdder
//! COMPILE_COUNT_GE *buildAdder* 1
//! EXEC_COUNT_GE *buildAdder* 1

//! METHOD foo
//! COMPILE_COUNT_GE *foo* 1
//! EXEC_COUNT_GE *foo* 1

//! DEOPT_COUNT *Deoptimize* 0
let call_compiled_lambda_with_this_method_compiled = false;

class CallCompiledLambdaWithThisMethodBox {
    value: number;

    constructor(value: number) {
        this.value = value;
    }

    buildAdder(delta: number): () => number {
        const foo = () => this.value + delta;
        if (!call_compiled_lambda_with_this_method_compiled) {
            ArkTools.arkSteedCompileSync(foo);
            call_compiled_lambda_with_this_method_compiled = true;
        }
        return foo;
    }
}

function call_compiled_lambda_with_this_method(): void {
    const box1 = new CallCompiledLambdaWithThisMethodBox(10);
    const box2 = new CallCompiledLambdaWithThisMethodBox(-3);
    print(box1.buildAdder(5)());
    print(box2.buildAdder(8)());
}

ArkTools.arkSteedCompileSync(call_compiled_lambda_with_this_method);
ArkTools.arkSteedCompileSync(CallCompiledLambdaWithThisMethodBox.prototype.buildAdder);

call_compiled_lambda_with_this_method();
