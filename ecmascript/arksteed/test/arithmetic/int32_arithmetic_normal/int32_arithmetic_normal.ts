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

// G10.1 (P0): normal path of the checked int32 arithmetic. The operands are
// dynamic parameters with int32 feedback, so the compiler emits the checked
// `I32*WithOverflow` / `CheckedI32Mod` vertices instead of folding the
// operations. Every ordinary input stays inside the int32 domain, so the
// compiled code must not leave it: the whole case asserts `DEOPT_COUNT
// *Deoptimize* 0`. The overflow transitions of the same mechanism are covered
// by `arithmetic/int32_overflow_boundary`, whose interpreter twin is
// `arithmetic/int32_overflow_boundary_interp_twin`; the numbers family twin is
// that pair. The expected values below follow ECMA-262 2021 6.1.6.1 Number and
// 13.4.3/13.4.5 (prefix/postfix increment and decrement) and are derived by
// hand, not from the interpreter.

//! PARAMS --compiler-jit-hotness-threshold=60000

//! METHOD normal_add
//! HAS I32AddWithOverflow

//! METHOD normal_sub
//! HAS I32SubWithOverflow

//! METHOD normal_mul
//! HAS I32MulWithOverflow

//! METHOD normal_div
//! HAS I32DivWithOverflow

//! METHOD normal_mod
//! HAS CheckedI32Mod

//! METHOD normal_inc
//! HAS I32IncWithOverflow

//! METHOD normal_dec
//! HAS I32DecWithOverflow

//! METHOD normal_neg
//! HAS I32NegWithOverflow

//! COMPILE_COUNT_GE *normal_add* 1
//! COMPILE_COUNT_GE *normal_sub* 1
//! COMPILE_COUNT_GE *normal_mul* 1
//! COMPILE_COUNT_GE *normal_div* 1
//! COMPILE_COUNT_GE *normal_mod* 1
//! COMPILE_COUNT_GE *normal_inc* 1
//! COMPILE_COUNT_GE *normal_dec* 1
//! COMPILE_COUNT_GE *normal_neg* 1
//! EXEC_COUNT_GE *normal_add* 1
//! EXEC_COUNT_GE *normal_sub* 1
//! EXEC_COUNT_GE *normal_mul* 1
//! EXEC_COUNT_GE *normal_div* 1
//! EXEC_COUNT_GE *normal_mod* 1
//! EXEC_COUNT_GE *normal_inc* 1
//! EXEC_COUNT_GE *normal_dec* 1
//! EXEC_COUNT_GE *normal_neg* 1
//! DEOPT_COUNT *Deoptimize* 0

function normal_add(a: number, b: number): number {
    return a + b;
}

function normal_sub(a: number, b: number): number {
    return a - b;
}

function normal_mul(a: number, b: number): number {
    return a * b;
}

function normal_div(a: number, b: number): number {
    return a / b;
}

function normal_mod(a: number, b: number): number {
    return a % b;
}

function normal_inc(a: number): number {
    let x = a;
    ++x;
    return x;
}

function normal_dec(a: number): number {
    let x = a;
    --x;
    return x;
}

function normal_neg(a: number): number {
    return -a;
}

// Warm up with dynamic int32 values that never overflow, then install the
// compiled code explicitly. `ArkTools.arkSteedCompileSync` is the existing
// installation path and is not itself evidence of compilation.
for (let i = 1; i < 2000; i++) {
    normal_add(i, i + 3);
    normal_sub(i + 3, i);
    normal_mul(i, 3);
    normal_div(i * 6, 3);
    normal_mod(i * 6, 5);
    normal_inc(i);
    normal_dec(i);
    normal_neg(i);
}

ArkTools.arkSteedCompileSync(normal_add);
ArkTools.arkSteedCompileSync(normal_sub);
ArkTools.arkSteedCompileSync(normal_mul);
ArkTools.arkSteedCompileSync(normal_div);
ArkTools.arkSteedCompileSync(normal_mod);
ArkTools.arkSteedCompileSync(normal_inc);
ArkTools.arkSteedCompileSync(normal_dec);
ArkTools.arkSteedCompileSync(normal_neg);

print(normal_add(100, 23));
print(normal_sub(100, 23));
print(normal_mul(7, 6));
print(normal_div(84, 4));
print(normal_mod(100, 7));
print(normal_inc(41));
print(normal_dec(43));
print(normal_neg(42));
