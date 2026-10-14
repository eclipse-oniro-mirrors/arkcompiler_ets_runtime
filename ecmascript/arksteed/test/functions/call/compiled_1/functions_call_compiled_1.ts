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

// G1.1 (P0): entry into the ArkSteed-compiled code of the target method, not
// only the installation of its machine code. `ArkTools.arkSteedCompileSync`
// remains the existing installation path of this case; the evidence
// annotations confirm the compilation of the target method and its entry into
// generated code through the E1 marker `>>> Running method: <record>.<method>`
// (E1/E2). The address constraint confirms the marker vertex in the CFG of
// this ordinary (non-OSR) compilation, and `DEOPT_COUNT *Deoptimize* 0` rules
// out a silent deopt into the interpreter after the entry. The interpreter
// twin of this case is `functions/call/compiled_1_interp_twin`; the result here
// must agree with that twin and with the hand-computed constants.

//! PARAMS --compiler-jit-hotness-threshold=60000

//! METHOD call_compiled_function_1
//! HAS CallRuntime PrintMethodName
//! COMPILE_COUNT_GE *call_compiled_function_1* 1
//! EXEC_COUNT_GE *call_compiled_function_1* 1
//! DEOPT_COUNT *Deoptimize* 0

function foo(x: number): number {
    return x * x;
}

function call_compiled_function_1(x: number): number {
    return foo(x);
}

ArkTools.arkSteedCompileSync(foo);  // Callee is JIT-compiled as well
ArkTools.arkSteedCompileSync(call_compiled_function_1);

print(call_compiled_function_1(-2.5));
print(call_compiled_function_1(15));
