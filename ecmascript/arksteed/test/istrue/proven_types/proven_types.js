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

//! PARAMS --compiler-jit-hotness-threshold=60000 --enable-pgo-profiler=true

// Uint32 array length has NUMBER type and no Float64 alternative.
//! METHOD numberTruthy
//! COUNT NumberToBoolean 1
//! HAS_NOT TaggedNumberToBoolean
//! HAS_NOT CallCommonStub ToBoolean*
function numberTruthy(array) {
    return array.length ? 1 : 0;
}

//! METHOD numberFalsy
//! COUNT NumberToBoolean 1
//! HAS_NOT TaggedNumberToBoolean
//! HAS_NOT CallCommonStub ToBoolean*
function numberFalsy(array) {
    return !array.length;
}

//! METHOD numberReuse
//! COUNT NumberToBoolean 1
//! HAS_NOT TaggedNumberToBoolean
function numberReuse(array) {
    const number = array.length;
    const first = !number;
    const second = !number;
    return +first + +second;
}

// The value and branch uses share the same falsiness conversion.
//! METHOD numberOpposite
//! COUNT NumberToBoolean 1
//! HAS_NOT TaggedNumberToBoolean
function numberOpposite(array) {
    const number = array.length;
    const falsy = !number;
    return number ? +falsy : +falsy + 1;
}

// The first conversion guards the parameter; the second use reuses its Boolean result.
//! METHOD doubleAfterCheck
//! COUNT TaggedDoubleToBoolean 1
//! HAS_NOT DoubleToBoolean
//! HAS_NOT TaggedNumberToBoolean
//! HAS_NOT CallCommonStub ToBoolean*
function doubleAfterCheck(value) {
    const first = !value;
    const second = !value;
    return +first + +second;
}

//! METHOD doubleOpposite
//! COUNT TaggedDoubleToBoolean 1
//! HAS_NOT DoubleToBoolean
//! HAS_NOT TaggedNumberToBoolean
function doubleOpposite(value) {
    const falsy = !value;
    return value ? +falsy : +falsy + 1;
}

// Arithmetic already records its Float64 result as an alternative.
//! METHOD float64Reuse
//! HAS Float64ToBoolean
//! HAS_NOT NumberToBoolean
//! HAS_NOT DoubleToBoolean
//! HAS_NOT TaggedNumberToBoolean
//! HAS_NOT TaggedDoubleToBoolean
function float64Reuse(value) {
    const number = value * 0.5;
    return !number;
}

// String addition checks the parameter's type before returning the known string.
//! METHOD stringTruthy
//! COUNT StringToBoolean 1
//! HAS_NOT CallCommonStub ToBoolean*
function stringTruthy(value) {
    const text = value + "";
    return text ? 1 : 0;
}

//! METHOD stringFalsy
//! COUNT StringToBoolean 1
//! HAS_NOT CallCommonStub ToBoolean*
function stringFalsy(value) {
    const text = value + "";
    return !text;
}

//! METHOD stringReuse
//! COUNT StringToBoolean 1
//! HAS_NOT CallCommonStub ToBoolean*
function stringReuse(value) {
    const text = value + "";
    const first = !text;
    const second = !text;
    return +first + +second;
}

//! METHOD nullAfterCheck
//! COUNT DeoptIfNotUndefinedOrNull 1
//! HAS_NOT CallCommonStub ToBoolean*
function nullAfterCheck(value) {
    const first = !value;
    const second = !value;
    return +first + +second;
}

// Mixed numeric feedback selects a checked conversion for the first use.
//! METHOD unknownNumber
//! COUNT TaggedNumberToBoolean 1
//! HAS_NOT NumberToBoolean
//! HAS_NOT DoubleToBoolean
//! HAS_NOT StringToBoolean
function unknownNumber(value) {
    return !value;
}

// NUMBER and STRING proofs on separate Phi inputs do not prove either type at the use.
//! METHOD mergedTypes
//! HAS CallCommonStub ToBoolean*
//! HAS_NOT NumberToBoolean
//! HAS_NOT DoubleToBoolean
//! HAS_NOT StringToBoolean
function mergedTypes(flag, number, text) {
    const numeric = +number;
    const string = text + "";
    const value = flag ? numeric : string;
    return !value;
}

const empty = [];
const nonempty = [1, 2];
for (let iteration = 0; iteration < 2000; ++iteration) {
    const even = iteration % 2 === 0;
    const array = even ? empty : nonempty;
    const text = even ? "" : "text";
    numberTruthy(array);
    numberFalsy(array);
    numberReuse(array);
    numberOpposite(array);
    doubleAfterCheck(iteration + 0.5);
    doubleOpposite(iteration + 0.5);
    float64Reuse(iteration + 0.5);
    stringTruthy(text);
    stringFalsy(text);
    stringReuse(text);
    nullAfterCheck(even ? null : undefined);
    unknownNumber(even ? 1 : 2.5);
    mergedTypes(even, even ? 0 : 2.5, text);
}

const methods = [numberTruthy, numberFalsy, numberReuse, numberOpposite, doubleAfterCheck, doubleOpposite, float64Reuse,
                 stringTruthy, stringFalsy, stringReuse, nullAfterCheck, unknownNumber, mergedTypes];
for (const method of methods) {
    ArkTools.arkSteedCompileSync(method);
    print(ArkTools.arkSteedIsCompiled(method));
}

print(numberTruthy(empty));
print(numberTruthy(nonempty));
print(numberFalsy(empty));
print(numberFalsy(nonempty));
print(numberReuse(empty));
print(numberReuse(nonempty));
print(numberOpposite(empty));
print(numberOpposite(nonempty));
const large = [];
large.length = 2147483648;
print(numberTruthy(large));
print(numberFalsy(large));
print(numberReuse(large));
print(numberOpposite(large));

print(doubleAfterCheck(0.5));
print(doubleAfterCheck(-0));
print(doubleAfterCheck(NaN));
print(doubleAfterCheck(Infinity));
print(doubleAfterCheck(""));
print(doubleOpposite(0.5));
print(doubleOpposite(-0));
print(doubleOpposite(NaN));
print(doubleOpposite(-Infinity));
print(doubleOpposite(Number.MIN_VALUE));
print(doubleOpposite("text"));
print(float64Reuse(0));
print(float64Reuse(-0));
print(float64Reuse(NaN));
print(float64Reuse(Infinity));
print(float64Reuse(-Number.MIN_VALUE * 2));

const joined = "a".repeat(64) + "b".repeat(64);
const sliced = joined.substring(16, 112);
print(stringTruthy(""));
print(stringTruthy("text"));
print(stringTruthy("中文"));
print(stringTruthy(joined));
print(stringTruthy(sliced));
print(stringFalsy(""));
print(stringFalsy("text"));
print(stringFalsy(joined));
print(stringFalsy(sliced));
print(stringReuse(""));
print(stringReuse(joined));
print(stringFalsy(0));
print(nullAfterCheck(null));
print(nullAfterCheck(undefined));
print(nullAfterCheck("text"));
print(unknownNumber(0));
print(unknownNumber(-0));
print(unknownNumber(NaN));
print(unknownNumber(Infinity));
print(unknownNumber(""));
print(mergedTypes(true, 0, "text"));
print(mergedTypes(true, 2.5, ""));
print(mergedTypes(false, 0, ""));
print(mergedTypes(false, 0, "text"));
