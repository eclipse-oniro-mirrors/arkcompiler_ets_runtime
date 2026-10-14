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
// `call_compiled_lambda_2` compiles and invokes the recursive
// function-expression closure `foo`, which reads its own captured binding from
// the lexical environment for the recursive call; that binding read is the
// `LoadTaggedField` at `GetLexicalEnvSlotOffset(1)` in its dump. `foo` is
// installed before its first invocation, so the call site lowers to the generic
// `Call` vertex observed in the caller dump. `COMPILE_COUNT_GE`/`EXEC_COUNT_GE`
// attribute compilation and entry of each claimed method; `DEOPT_COUNT
// *Deoptimize* 0` rules out a silent deopt into the interpreter. The pre-existing
// computed results and the expectation are unchanged.

//! PARAMS --compiler-jit-hotness-threshold=60000

//! METHOD call_compiled_lambda_2
//! HAS Call
//! COMPILE_COUNT_GE *call_compiled_lambda_2* 1
//! EXEC_COUNT_GE *call_compiled_lambda_2* 1

//! METHOD foo
//! HAS LoadTaggedField     # captured read of the closure binding `foo`
//! COMPILE_COUNT_GE *foo* 1
//! EXEC_COUNT_GE *foo* 1

//! DEOPT_COUNT *Deoptimize* 0
let compiled = false;

function call_compiled_lambda_2(x: number, y: number): number {
    const foo = function(a: number, b: number): number {
        return (b == 0) ? a : foo(b, a % b);
    };
    if (!compiled) {
        ArkTools.arkSteedCompileSync(foo);
        compiled = true;
    }
    return foo(x, y);
}

ArkTools.arkSteedCompileSync(call_compiled_lambda_2);
print(call_compiled_lambda_2(24, 36));
print(call_compiled_lambda_2(15, 48));
print(call_compiled_lambda_2(20, 12));
