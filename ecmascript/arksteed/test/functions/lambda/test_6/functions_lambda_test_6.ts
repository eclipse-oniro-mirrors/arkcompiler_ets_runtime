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
// `call_compiled_lambda_6` declares the captured binding `sum` and compiles and
// invokes the function-expression closures `addA` and `addB`, which share that
// mutable binding. In each closure the read of `sum` lowers to `LoadTaggedField`
// on the lexical environment at `GetLexicalEnvSlotOffset(0)` and the write lowers
// to `StoreEnvSlot` at the same offset; in the caller the initialization of `sum`
// lowers to `StoreEnvSlot`. The closures are installed before their first
// invocation, so the call sites lower to the generic `Call` vertices observed in
// the caller dump. `COMPILE_COUNT_GE`/`EXEC_COUNT_GE` attribute compilation and
// entry of each claimed method; `DEOPT_COUNT *Deoptimize* 0` rules out a silent
// deopt into the interpreter. The pre-existing computed results and the
// expectation are unchanged.

//! PARAMS --compiler-jit-hotness-threshold=60000

//! METHOD call_compiled_lambda_6
//! HAS Call
//! HAS StoreEnvSlot        # captured binding `sum` (offset 40)
//! COMPILE_COUNT_GE *call_compiled_lambda_6* 1
//! EXEC_COUNT_GE *call_compiled_lambda_6* 1

//! METHOD addA
//! HAS LoadTaggedField     # captured read of `sum`
//! HAS StoreEnvSlot        # captured write of `sum`
//! COMPILE_COUNT_GE *addA* 1
//! EXEC_COUNT_GE *addA* 1

//! METHOD addB
//! HAS LoadTaggedField     # captured read of `sum`
//! HAS StoreEnvSlot        # captured write of `sum`
//! COMPILE_COUNT_GE *addB* 1
//! EXEC_COUNT_GE *addB* 1

//! DEOPT_COUNT *Deoptimize* 0

// Multiple closures sharing mutable outer scope
let compiled = false;

function call_compiled_lambda_6(a: number, b: number): number {
    let sum: number = 0;
    const addA = function(): void { sum = sum + a; };
    const addB = function(): void { sum = sum + b; };
    if (!compiled) {
        ArkTools.arkSteedCompileSync(addA);
        ArkTools.arkSteedCompileSync(addB);
        compiled = true;
    }
    addA();
    addB();
    addA();
    return sum;
}

ArkTools.arkSteedCompileSync(call_compiled_lambda_6);
print(call_compiled_lambda_6(3, 5));
print(call_compiled_lambda_6(7, 2));
print(call_compiled_lambda_6(1, 10));
