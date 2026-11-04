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
//! PARAMS --compiler-jit-hotness-threshold=60000

//! METHOD string_equal
//! HAS StringEqual
//! COMPILE_COUNT_GE *string_equal* 1
//! EXEC_COUNT_GE *string_equal* 1
function string_equal(left: string, right: string): boolean {
    return left === right;
}

//! METHOD string_not_equal
//! HAS StringEqual
//! HAS TaggedNotEqual
//! COMPILE_COUNT_GE *string_not_equal* 1
//! EXEC_COUNT_GE *string_not_equal* 1
function string_not_equal(left: string, right: string): boolean {
    return left !== right;
}

//! METHOD string_less
//! COMPILE_COUNT_GE *string_less* 1
//! EXEC_COUNT_GE *string_less* 1
function string_less(left: string, right: string): boolean {
    return left < right;
}

//! METHOD int_less
//! HAS I32ConditionCheck
//! COMPILE_COUNT_GE *int_less* 1
//! EXEC_COUNT_GE *int_less* 1
function int_less(left: number, right: number): boolean {
    return left < right;
}

//! METHOD number_less
//! HAS F64ConditionCheck
//! COMPILE_COUNT_GE *number_less* 1
//! EXEC_COUNT_GE *number_less* 1
function number_less(left: number, right: number): boolean {
    return left < right;
}

//! METHOD number_equal
//! HAS F64ConditionCheck
//! COMPILE_COUNT_GE *number_equal* 1
//! EXEC_COUNT_GE *number_equal* 1
function number_equal(left: number, right: number): boolean {
    return left === right;
}

//! METHOD number_not_equal
//! HAS F64ConditionCheck
//! COMPILE_COUNT_GE *number_not_equal* 1
//! EXEC_COUNT_GE *number_not_equal* 1
function number_not_equal(left: number, right: number): boolean {
    return left != right;
}

//! METHOD null_equal
//! HAS TaggedEqual
//! COMPILE_COUNT_GE *null_equal* 1
//! EXEC_COUNT_GE *null_equal* 1
function null_equal(value: object | null): boolean {
    return value === null;
}

//! METHOD null_not_equal
//! HAS TaggedNotEqual
//! COMPILE_COUNT_GE *null_not_equal* 1
//! EXEC_COUNT_GE *null_not_equal* 1
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

// Warm each target with dynamic operands of one type so the compiled method
// carries the feedback-selected comparison vertex; the hotness threshold keeps
// the warm-up in the interpreter until the explicit installation below.
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

ArkTools.arkSteedCompileSync(string_equal);
ArkTools.arkSteedCompileSync(string_not_equal);
ArkTools.arkSteedCompileSync(string_less);
ArkTools.arkSteedCompileSync(int_less);
ArkTools.arkSteedCompileSync(number_less);
ArkTools.arkSteedCompileSync(number_equal);
ArkTools.arkSteedCompileSync(number_not_equal);
ArkTools.arkSteedCompileSync(null_equal);
ArkTools.arkSteedCompileSync(null_not_equal);

//! DEOPT_COUNT *Deoptimize* 0

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
