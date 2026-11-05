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

//! PARAMS --compiler-jit-hotness-threshold=60000 --enable-pgo-profiler=true --compiler-arksteed-deopt-on-insufficient-profile=false

// Comparisons and logical negation produce Boolean values for numeric conversion.
//! METHOD compareToNumber
//! HAS I64BitwiseBinary
//! HAS_NOT CallRuntime ToNumber
//! HAS_NOT DeoptIfNotBoolean
function compareToNumber(left, right) {
    return +(left === right);
}

//! METHOD falsyToNumber
//! HAS I64BitwiseBinary
//! HAS_NOT CallRuntime ToNumber
//! HAS_NOT DeoptIfNotBoolean
function falsyToNumber(value) {
    return +(!value);
}

//! METHOD booleanToNumeric
//! HAS I64BitwiseBinary
//! HAS_NOT CallRuntime ToNumeric
//! HAS_NOT DeoptIfNotBoolean
function booleanToNumeric(left, right) {
    let value = left === right;
    const previous = value++;
    return previous;
}

// The numeric result must not replace the original Boolean's identity or type.
//! METHOD booleanIdentity
//! HAS_NOT CallRuntime ToNumber
//! HAS_NOT DeoptIfNotBoolean
function booleanIdentity(left, right) {
    const original = left === right;
    const converted = +original;
    return original === converted;
}

//! METHOD booleanArithmetic
//! HAS_NOT CallRuntime ToNumber
//! HAS_NOT CheckedTaggedIntToI32
function booleanArithmetic(left, right) {
    return +(left === right) * 2 + 1;
}

// Only !value is warmed up. Its guard proves nullable at the cold conversion site.
//! METHOD nullableToNumber
//! COUNT DeoptIfNotUndefinedOrNull 1
//! COUNT BranchIfInt64Compare 1
//! HAS_NOT CallRuntime ToNumber
function nullableToNumber(value, convert) {
    const falsy = !value;
    if (convert) {
        return +value;
    }
    return falsy;
}

//! METHOD nullableToNumeric
//! COUNT DeoptIfNotUndefinedOrNull 1
//! COUNT BranchIfInt64Compare 1
//! HAS_NOT CallRuntime ToNumeric
function nullableToNumeric(value, convert) {
    const falsy = !value;
    if (convert) {
        const previous = value++;
        return previous;
    }
    return falsy;
}

//! METHOD nullableIdentity
//! COUNT DeoptIfNotUndefinedOrNull 1
//! HAS_NOT CallRuntime ToNumber
function nullableIdentity(value, convert) {
    const falsy = !value;
    if (convert) {
        const converted = +value;
        return value === converted;
    }
    return falsy;
}

//! METHOD nullConstant
//! HAS_NOT CallRuntime ToNumber
//! HAS_NOT BranchIfInt64Compare
function nullConstant() {
    return 1 / (+null);
}

//! METHOD undefinedConstant
//! HAS_NOT CallRuntime ToNumber
//! HAS_NOT BranchIfInt64Compare
function undefinedConstant() {
    return +undefined;
}

// Cold, unproved parameters retain the generic conversion and its exception behavior.
//! METHOD unknownToNumber
//! HAS CallRuntime ToNumber
//! HAS_NOT DeoptIfNotBoolean
//! HAS_NOT DeoptIfNotUndefinedOrNull
function unknownToNumber(value) {
    return +value;
}

//! METHOD unknownToNumeric
//! HAS CallRuntime ToNumeric
//! HAS_NOT DeoptIfNotBoolean
//! HAS_NOT DeoptIfNotUndefinedOrNull
function unknownToNumeric(value) {
    const previous = value++;
    return previous;
}

// This ordinary Phi has no Boolean proof; the string arm must still produce 2.
//! METHOD mixedPhi
//! HAS CallRuntime ToNumber
function mixedPhi(flag, left, right) {
    const value = flag ? left === right : "2";
    return +value;
}

for (let i = 0; i < 2000; i++) {
    const value = i % 2 === 0 ? null : undefined;
    nullableToNumber(value, false);
    nullableToNumeric(value, false);
    nullableIdentity(value, false);
}

const methods = [
    compareToNumber, falsyToNumber, booleanToNumeric, booleanIdentity, booleanArithmetic,
    nullableToNumber, nullableToNumeric, nullableIdentity, nullConstant, undefinedConstant,
    unknownToNumber, unknownToNumeric, mixedPhi
];
for (const method of methods) {
    ArkTools.arkSteedCompileSync(method);
}

print(compareToNumber(1, 1));
print(compareToNumber(1, "1"));
print(falsyToNumber(0));
print(falsyToNumber(-0));
print(falsyToNumber(NaN));
print(falsyToNumber(Infinity));
print(falsyToNumber(""));
print(falsyToNumber("0"));
print(booleanToNumeric(1, 1));
print(booleanToNumeric(1, "1"));
print(booleanIdentity(1, 1));
print(booleanIdentity(1, "1"));
print(booleanArithmetic(1, 1));
print(booleanArithmetic(1, "1"));
print(nullableToNumber(null, true));
print(1 / nullableToNumber(null, true));
print(Number.isNaN(nullableToNumber(undefined, true)));
print(nullableToNumeric(null, true));
print(1 / nullableToNumeric(null, true));
print(Number.isNaN(nullableToNumeric(undefined, true)));
print(nullableIdentity(null, true));
print(nullableIdentity(undefined, true));
print(nullConstant());
print(Number.isNaN(undefinedConstant()));
print(unknownToNumber(true));
print(unknownToNumber(null));
print(Number.isNaN(unknownToNumber(undefined)));
print(unknownToNumber("2"));
print(unknownToNumeric(false));
print(unknownToNumeric("5"));
print(unknownToNumeric(10n) === 10n);
print(mixedPhi(true, 1, 1));
print(mixedPhi(true, 1, "1"));
print(mixedPhi(false, 1, 1));

let conversions = 0;
const object = {
    valueOf() {
        conversions++;
        return 7;
    }
};
// Type changes deopt at the original nullable guard, before the conversion.
print(nullableToNumber(object, true));
print(conversions);
print(nullableToNumeric(object, true));
print(conversions);
print(unknownToNumber(object));
print(conversions);
print(unknownToNumeric(object));
print(conversions);
try {
    unknownToNumber(10n);
} catch (error) {
    print(error instanceof TypeError);
}
try {
    unknownToNumber(Symbol("number"));
} catch (error) {
    print(error instanceof TypeError);
}
try {
    unknownToNumeric(Symbol("numeric"));
} catch (error) {
    print(error instanceof TypeError);
}
