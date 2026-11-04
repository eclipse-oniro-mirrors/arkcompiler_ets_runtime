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

// Interpreter twin of `arithmetic/int32_overflow_boundary` (numbers family,
// A5/E6). The same computations and inputs run with the JIT disabled; the
// expectation file is byte-identical to the boundary case, so the
// differential comparison covers the specified results of the overflow
// boundary. The primary oracle stays ECMA-262 2021 6.1.6.1 Number and
// 13.4.3/13.4.5; interpreter agreement is a necessary but not sufficient
// check. The twin declares no JIT annotation, omits `ArkTools.arkSteedCompileSync`
// and does not print any JIT state.

//! PARAMS --compiler-enable-jit=false

const INT32_MAX: number = 2147483647;

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

function boundary_post_inc_expr(a: number): number {
    let x = a;
    return x++;
}

function boundary_post_inc_slot(a: number): number {
    let x = a;
    x++;
    return x;
}

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
