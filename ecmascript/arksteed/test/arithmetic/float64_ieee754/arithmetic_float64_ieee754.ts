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

// G10.2 (P0): IEEE-754 semantics of ArkSteed float64 arithmetic -- NaN,
// +-Infinity, negative zero and the sign of a computed result. The four
// targets below are separate dynamic-number methods on purpose: every operand
// is a parameter, so no operand is a compile-time constant, constant folding
// cannot reduce the operation and the CFG address evidence of `F64Add`,
// `F64Mul`, `F64Div` and `F64Neg` really names the executed operation
// (arksteed-jsts-p0-semantic-rows#S3).
//
// The warm-up runs on non-constant double operands, so the recorded operation
// feedback of every target is NUMBER and the compiled target takes the
// float64 path instead of the int32 or the generic stub path. The hotness
// threshold stays out of reach of the warm-up call count, so the targets are
// compiled by `ArkTools.arkSteedCompileSync` and not by the profile stub.
//
// The printed zero of a double does not carry its sign, so the expectation
// stores booleans computed from the results with `Object.is` (never from the
// source literals) plus the preserved `-0 === 0` / `NaN === NaN`
// observations. `0/0`, `1/0`, `-1/0`, `1/-0`, `-0 + +0`, `-0 * -1` and the
// negation of a dynamic `+0` are the specified ECMA-262 2021 / IEEE-754
// outcomes of §6.1.6.1.1, §6.1.6.1.4, §6.1.6.1.5, §6.1.6.1.7 and
// §20.1.2.15.
//
// G10.1 carries the numbers-family interpreter twin; this row has no twin of
// its own (arksteed-jsts-p0-semantic-rows#S3, #S1).

//! PARAMS --compiler-jit-hotness-threshold=60000

//! METHOD f64_div
//! HAS F64Div
//! METHOD f64_mul
//! HAS F64Mul
//! METHOD f64_add
//! HAS F64Add
//! METHOD f64_neg
//! HAS F64Neg
//! COMPILE_COUNT_GE *f64_div* 1
//! COMPILE_COUNT_GE *f64_mul* 1
//! COMPILE_COUNT_GE *f64_add* 1
//! COMPILE_COUNT_GE *f64_neg* 1
//! EXEC_COUNT_GE *f64_div* 1
//! EXEC_COUNT_GE *f64_mul* 1
//! EXEC_COUNT_GE *f64_add* 1
//! EXEC_COUNT_GE *f64_neg* 1
//! DEOPT_COUNT *Deoptimize* 0

function f64_div(a: number, b: number): number {
    return a / b;
}

function f64_mul(a: number, b: number): number {
    return a * b;
}

function f64_add(a: number, b: number): number {
    return a + b;
}

function f64_neg(a: number): number {
    return -a;
}

// Warm up on dynamic double operands: the recorded operation feedback of all
// four targets becomes NUMBER, so the compiled graph holds the checked float64
// vertices instead of an int32 or a generic stub path.
let warmUpSink = 0;
for (let i = 0; i < 8; i++) {
    warmUpSink += f64_div(6.5 + i, 2.25 + i);
    warmUpSink += f64_mul(1.5 + i, 3.25 + i);
    warmUpSink += f64_add(0.5 + i, 0.125 + i);
    warmUpSink += f64_neg(2.5 + i);
}

ArkTools.arkSteedCompileSync(f64_div);
ArkTools.arkSteedCompileSync(f64_mul);
ArkTools.arkSteedCompileSync(f64_add);
ArkTools.arkSteedCompileSync(f64_neg);

let positiveZero = 0;
let negativeZero = -0;
let minusOne = -1;

// 0/0 is NaN: NaN is the only value that is not equal to itself.
let notANumber = f64_div(0, 0);
print(notANumber);
print(Object.is(notANumber, NaN));
print(notANumber === notANumber);

// 1/0 is +Infinity; -1/0 and 1/-0 are -Infinity.
let positiveInfinity = f64_div(1, 0);
print(positiveInfinity);
let negativeInfinityFromMinusOne = f64_div(-1, 0);
print(negativeInfinityFromMinusOne);
let negativeInfinityFromNegativeZero = f64_div(1, negativeZero);
print(negativeInfinityFromNegativeZero);

// -0 + +0 is +0: Number::add drops the sign of the surviving zero.
let zeroSum = f64_add(negativeZero, positiveZero);
print(zeroSum);
print(Object.is(zeroSum, 0));
print(Object.is(zeroSum, -0));

// -0 * -1 is +0 as well; the two sign checks are the same.
let zeroProduct = f64_mul(negativeZero, minusOne);
print(zeroProduct);
print(Object.is(zeroProduct, 0));
print(Object.is(zeroProduct, -0));

// Unary negation of a dynamic +0 on the F64Neg path produces a genuine -0.
let negatedZero = f64_neg(positiveZero);
print(Object.is(negatedZero, -0));
print(Object.is(negatedZero, 0));

// Preserved comparison observations.
print(-0 === 0);
print(NaN === NaN);
