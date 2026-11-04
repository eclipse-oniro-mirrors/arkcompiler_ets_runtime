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
// `call_compiled_lambda_5` declares the captured binding `acc` and compiles and
// invokes the function-expression closure `add`, which reads and writes that
// binding. In `add` the read lowers to `LoadTaggedField` on the lexical
// environment at `GetLexicalEnvSlotOffset(0)` and the write lowers to
// `StoreEnvSlot` at the same offset; in the caller the initialization of `acc`
// lowers to `StoreEnvSlot`. The closure is installed before its first invocation,
// so the call site lowers to the generic `Call` vertex observed in the caller
// dump. `COMPILE_COUNT_GE`/`EXEC_COUNT_GE` attribute compilation and entry of each
// claimed method; `DEOPT_COUNT *Deoptimize* 0` rules out a silent deopt into the
// interpreter. The pre-existing computed results and the expectation are
// unchanged.

//! PARAMS --compiler-jit-hotness-threshold=60000

//! METHOD call_compiled_lambda_5
//! HAS Call
//! HAS StoreEnvSlot        # StoreEnvSlot offset 40: the captured binding `acc`
//! COMPILE_COUNT_GE *call_compiled_lambda_5* 1
//! EXEC_COUNT_GE *call_compiled_lambda_5* 1

//! METHOD add
//! HAS LoadTaggedField     # captured read of `acc` (offset 40)
//! HAS StoreEnvSlot        # captured write of `acc` (offset 40)
//! COMPILE_COUNT_GE *add* 1
//! EXEC_COUNT_GE *add* 1

//! DEOPT_COUNT *Deoptimize* 0

// Closure: lambda captures and mutates outer variable
let compiled = false;

function call_compiled_lambda_5(init: number, x: number): number {
    let acc: number = init;
    const add = function(n: number): number {
        acc = acc + n;
        return acc;
    };
    if (!compiled) {
        ArkTools.arkSteedCompileSync(add);
        compiled = true;
    }
    return add(x);
}

ArkTools.arkSteedCompileSync(call_compiled_lambda_5);
print(call_compiled_lambda_5(0, 5));
print(call_compiled_lambda_5(10, 7));
print(call_compiled_lambda_5(3, -2));
