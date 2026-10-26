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
// Interpreter twin of `strings/comparison_matrix` (A5/E6): the same target
// methods, inputs and printed observations with the JIT disabled, so the
// differential comparison covers the comparison results. The primary oracle
// stays the specified ECMA-262 semantics and the hand-computed constants; the
// interpreter agreement is a necessary but not sufficient check. The twin
// carries no JIT/IR/count annotation and no installation call: with
// `--compiler-enable-jit=false` nothing is compiled and no entry marker is
// printed, which is exactly the controlled violation the primary is sensitive
// to.
//! PARAMS --compiler-enable-jit=false

function string_equal(left: string, right: string): boolean {
    return left === right;
}

function string_not_equal(left: string, right: string): boolean {
    return left !== right;
}

function string_less(left: string, right: string): boolean {
    return left < right;
}

function int_less(left: number, right: number): boolean {
    return left < right;
}

function number_less(left: number, right: number): boolean {
    return left < right;
}

function number_equal(left: number, right: number): boolean {
    return left === right;
}

function number_not_equal(left: number, right: number): boolean {
    return left != right;
}

function null_equal(value: object | null): boolean {
    return value === null;
}

function null_not_equal(value: object | null): boolean {
    return value !== null;
}

declare function print(arg: number): string;

function negative_zero(): number {
    return -0;
}

function nan_value(): number {
    return 0 / 0;
}

class Box {
    value: number = 0;
}

let first = new Box();
let leftText = "loop-effect-" + "constant";
let rightText = ["loop", "effect", "constant"].join("-");

for (let i = 0; i < 2000; i++) {
    string_equal(leftText, rightText);
    string_not_equal(leftText, rightText);
    string_less(leftText, rightText);
    int_less(i, i + 1);
    number_less(i + 1.5, i + 2.5);
    number_equal(i + 1.5, i + 1.5);
    number_not_equal(i + 1.5, i + 1.5);
    null_equal(null);
    null_equal(first);
    null_not_equal(null);
    null_not_equal(first);
}

print(string_equal("a", "a") ? 1 : 0);
print(string_equal("a", "b") ? 1 : 0);
print(string_equal("", "") ? 1 : 0);
print(string_not_equal("a", "b") ? 1 : 0);
print(string_less("a", "b") ? 1 : 0);
print(string_less("b", "a") ? 1 : 0);
print(int_less(-1, 0) ? 1 : 0);
print(number_less(1.5, 2.5) ? 1 : 0);
print(number_equal(negative_zero(), 0) ? 1 : 0);
print(number_equal(nan_value(), nan_value()) ? 1 : 0);
print(number_not_equal(nan_value(), nan_value()) ? 1 : 0);
print(null_equal(null) ? 1 : 0);
print(null_equal(first) ? 1 : 0);
print(null_not_equal(first) ? 1 : 0);
