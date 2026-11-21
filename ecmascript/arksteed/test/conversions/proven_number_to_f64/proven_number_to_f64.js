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

//! METHOD doubleAccumulator
//! HAS_NOT TaggedNumberToF64
function doubleAccumulator(value, count) {
    let sum = 0.5;
    for (let i = 0; i < count; ++i) {
        sum += value * 0.5;
    }
    return sum;
}

// The tagged unary result retains its exact Float64 alternative, so the
// subsequent multiplication consumes the F64Neg output directly.
//! METHOD numberAfterNegation
//! HAS F64Neg
//! HAS F64Mul
//! HAS_NOT TaggedNumberToF64
//! HAS_NOT TaggedDoubleToF64
//! COUNT CheckedNumberToF64 1
function numberAfterNegation(value) {
    const negated = -value;
    return negated * 0.5;
}

//! METHOD unknownNumber
//! COUNT CheckedNumberToF64 1
function unknownNumber(value) {
    return value * 0.5;
}

// Array length is UInt32 and is tagged as either Int32 or Double.
//! METHOD numericLength
//! HAS TaggedNumberToF64
//! HAS_NOT CheckedNumberToF64
function numericLength(array) {
    return array.length * 0.5;
}

const shortArray = [1, 2, 3];
for (let iteration = 0; iteration < 2000; ++iteration) {
    doubleAccumulator(2.5, 4);
    numberAfterNegation(2.5);
    unknownNumber(2.5);
    numericLength(shortArray);
}
ArkTools.arkSteedCompileSync(doubleAccumulator);
ArkTools.arkSteedCompileSync(numberAfterNegation);
ArkTools.arkSteedCompileSync(unknownNumber);
ArkTools.arkSteedCompileSync(numericLength);

print(ArkTools.arkSteedIsCompiled(doubleAccumulator));
print(ArkTools.arkSteedIsCompiled(numberAfterNegation));
print(ArkTools.arkSteedIsCompiled(unknownNumber));
print(ArkTools.arkSteedIsCompiled(numericLength));
print(doubleAccumulator(2.5, 4));
print(numberAfterNegation(2.5));
print(Object.is(numberAfterNegation(0), -0));
print(numberAfterNegation(Infinity) === -Infinity);
print(Number.isNaN(numberAfterNegation(NaN)));
print(unknownNumber(8));
print(unknownNumber("8"));
print(numericLength(shortArray));
const longArray = [];
longArray.length = 2147483648;
print(numericLength(longArray));
