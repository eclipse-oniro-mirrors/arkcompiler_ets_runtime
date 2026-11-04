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

// G10.1 (P0): overflow boundary of the checked int32 arithmetic. Every probe
// method is warmed with dynamic int32 operands, then installed through
// `ArkTools.arkSteedCompileSync`, so its checked `I32*WithOverflow` /
// `CheckedI32Mod` vertices are compiled (and addressed by the method-scoped
// `HAS` annotations) before the first overflowing input reaches it. The
// overflowing probes leave the generated code through the eager overflow exit
// (`DEOPT_COUNT_GE *Deoptimize* 1`), and the interpreter supplies the
// specified Number result. The exact results follow ECMA-262 2021 6.1.6.1
// Number and 13.4.3/13.4.5; they are derived by hand, not read from the
// runtime. The postfix form is observed through two methods per operator:
// the expression-result method returns the old value, the slot method returns
// the updated value, so the boundary record distinguishes them. The
// interpreter twin is `arithmetic/int32_overflow_boundary_interp_twin` and
// prints byte-identical expectations.

//! PARAMS --compiler-jit-hotness-threshold=60000

//! METHOD boundary_add
//! HAS I32AddWithOverflow

//! METHOD boundary_sub
//! HAS I32SubWithOverflow

//! METHOD boundary_mul
//! HAS I32MulWithOverflow

//! METHOD boundary_neg
//! HAS I32NegWithOverflow

//! METHOD boundary_inc
//! HAS I32IncWithOverflow

//! METHOD boundary_dec
//! HAS I32DecWithOverflow

//! METHOD boundary_div
//! HAS I32DivWithOverflow

//! METHOD boundary_mod
//! HAS CheckedI32Mod

//! METHOD boundary_post_inc_expr
//! HAS I32IncWithOverflow

//! METHOD boundary_post_inc_slot
//! HAS I32IncWithOverflow

//! METHOD boundary_post_dec_expr
//! HAS I32DecWithOverflow

//! METHOD boundary_post_dec_slot
//! HAS I32DecWithOverflow

//! COMPILE_COUNT_GE *boundary_add* 1
//! COMPILE_COUNT_GE *boundary_sub* 1
//! COMPILE_COUNT_GE *boundary_mul* 1
//! COMPILE_COUNT_GE *boundary_neg* 1
//! COMPILE_COUNT_GE *boundary_inc* 1
//! COMPILE_COUNT_GE *boundary_dec* 1
//! COMPILE_COUNT_GE *boundary_div* 1
//! COMPILE_COUNT_GE *boundary_mod* 1
//! COMPILE_COUNT_GE *boundary_post_inc_expr* 1
//! COMPILE_COUNT_GE *boundary_post_inc_slot* 1
//! COMPILE_COUNT_GE *boundary_post_dec_expr* 1
//! COMPILE_COUNT_GE *boundary_post_dec_slot* 1
//! EXEC_COUNT_GE *boundary_add* 1
//! EXEC_COUNT_GE *boundary_sub* 1
//! EXEC_COUNT_GE *boundary_mul* 1
//! EXEC_COUNT_GE *boundary_neg* 1
//! EXEC_COUNT_GE *boundary_inc* 1
//! EXEC_COUNT_GE *boundary_dec* 1
//! EXEC_COUNT_GE *boundary_div* 1
//! EXEC_COUNT_GE *boundary_mod* 1
//! EXEC_COUNT_GE *boundary_post_inc_expr* 1
//! EXEC_COUNT_GE *boundary_post_inc_slot* 1
//! EXEC_COUNT_GE *boundary_post_dec_expr* 1
//! EXEC_COUNT_GE *boundary_post_dec_slot* 1
//! DEOPT_COUNT_GE *Deoptimize* 1

const INT32_MAX: number = 2147483647;

// Build INT_MIN at run time from an int32 operand, so the boundary methods
// receive a tagged int32 value rather than a double constant.
function computeIntMin(seed: number): number {
    return seed - 1;
}

const INT32_MIN: number = computeIntMin(-2147483647);

function boundary_add(a: number, b: number): number {
    return a + b;
}

function boundary_sub(a: number, b: number): number {
    return a - b;
}

function boundary_mul(a: number, b: number): number {
    return a * b;
}

function boundary_neg(a: number): number {
    return -a;
}

function boundary_inc(a: number): number {
    let x = a;
    ++x;
    return x;
}

function boundary_dec(a: number): number {
    let x = a;
    --x;
    return x;
}

// Postfix increment: expression result is the old value, the slot is updated.
function boundary_post_inc_expr(a: number): number {
    let x = a;
    return x++;
}

function boundary_post_inc_slot(a: number): number {
    let x = a;
    x++;
    return x;
}

// Postfix decrement: expression result is the old value, the slot is updated.
function boundary_post_dec_expr(a: number): number {
    let x = a;
    return x--;
}

function boundary_post_dec_slot(a: number): number {
    let x = a;
    x--;
    return x;
}

function boundary_div(a: number, b: number): number {
    return a / b;
}

function boundary_mod(a: number, b: number): number {
    return a % b;
}

// Warm up every probe with dynamic int32 values, then install it. No probe
// overflows before its own installation, so each method keeps int32 feedback
// until it is compiled.
for (let i = 1; i < 2000; i++) {
    boundary_add(i, i + 3);
    boundary_sub(i + 3, i);
    boundary_mul(i, 3);
    boundary_neg(i);
    boundary_inc(i);
    boundary_dec(i);
    boundary_post_inc_expr(i);
    boundary_post_inc_slot(i);
    boundary_post_dec_expr(i);
    boundary_post_dec_slot(i);
    boundary_div(i * 6, 3);
    boundary_mod(i * 6, 5);
}

ArkTools.arkSteedCompileSync(boundary_add);
ArkTools.arkSteedCompileSync(boundary_sub);
ArkTools.arkSteedCompileSync(boundary_mul);
ArkTools.arkSteedCompileSync(boundary_neg);
ArkTools.arkSteedCompileSync(boundary_inc);
ArkTools.arkSteedCompileSync(boundary_dec);
ArkTools.arkSteedCompileSync(boundary_post_inc_expr);
ArkTools.arkSteedCompileSync(boundary_post_inc_slot);
ArkTools.arkSteedCompileSync(boundary_post_dec_expr);
ArkTools.arkSteedCompileSync(boundary_post_dec_slot);
ArkTools.arkSteedCompileSync(boundary_div);
ArkTools.arkSteedCompileSync(boundary_mod);

print(boundary_add(INT32_MAX, 1));          // 2147483647 + 1 -> 2147483648
print(boundary_sub(INT32_MIN, 1));          // -2147483648 - 1 -> -2147483649
print(boundary_mul(INT32_MAX, 2));          // 2147483647 * 2 -> 4294967294
print(boundary_neg(INT32_MIN));             // -(-2147483648) -> 2147483648
print(boundary_add(INT32_MIN, 1));          // -2147483648 + 1 -> -2147483647
print(boundary_inc(INT32_MIN));             // prefix ++x -> -2147483647
print(boundary_dec(INT32_MIN));             // prefix --x -> -2147483649
print(boundary_post_inc_expr(INT32_MIN));   // postfix x++ expression -> -2147483648
print(boundary_post_inc_slot(INT32_MIN));   // updated slot -> -2147483647
print(boundary_post_dec_expr(INT32_MIN));   // postfix x-- expression -> -2147483648
print(boundary_post_dec_slot(INT32_MIN));   // updated slot -> -2147483649
print(boundary_div(INT32_MIN, -1));         // -2147483648 / -1 -> 2147483648
print(boundary_mod(-5, 2));                 // -5 % 2 -> -1
