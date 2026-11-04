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

//! PARAMS --compiler-jit-hotness-threshold=60000 --enable-force-gc=true

//! METHOD makeNumber
//! COUNT CallCommonStub ArkSteedStringToNumber 1
//! HAS CallCommonStub ArkSteedNewObjectFromPrimitive
//! HAS_NOT LineStringLoadElement
//! HAS_NOT CallRuntime ToNumber

//! METHOD makeKnownStringNumber
//! COUNT CallCommonStub ArkSteedStringToNumber 1
//! HAS_NOT LineStringLoadElement
//! HAS_NOT CallKnownBuiltin

//! METHOD makeTwoNumbers
//! COUNT CallCommonStub ArkSteedStringToNumber 2
//! HAS_NOT LineStringLoadElement

//! METHOD makeNumberInTry
//! COUNT CallCommonStub ArkSteedStringToNumber 1
//! HAS_NOT LineStringLoadElement

function makeNumber(value, keep) {
    const wrapped = new Number(value);
    return [wrapped, keep];
}

function makeKnownStringNumber() {
    return new Number("12.5");
}

function makeTwoNumbers(left, right) {
    return [new Number(left), new Number(right)];
}

function makeNumberInTry(value) {
    try {
        return new Number(value);
    } catch (error) {
        return error;
    }
}

function check(condition, message) {
    if (!condition) {
        throw new Error(message);
    }
}

function checkNumber(wrapper, expected, message) {
    check(wrapper instanceof Number, message + ": wrapper");
    check(Object.is(wrapper.valueOf(), expected), message + ": value");
}

const keep = { marker: 42 };
for (let i = 0; i < 16; i++) {
    makeNumber("123", keep);
    makeKnownStringNumber();
    makeTwoNumbers("123", "456");
    makeNumberInTry("123");
}
ArkTools.arkSteedCompileSync(makeNumber);
ArkTools.arkSteedCompileSync(makeKnownStringNumber);
ArkTools.arkSteedCompileSync(makeTwoNumbers);
ArkTools.arkSteedCompileSync(makeNumberInTry);

// Explicit expected values distinguish Number conversion from parseInt/parseFloat.
const cases = [
    ["0", 0],
    ["1", 1],
    ["9", 9],
    ["123456789", 123456789],
    ["999999999", 999999999],
    ["1000000000", 1000000000],
    ["2147483646", 2147483646],
    ["2147483647", 2147483647],
    ["2147483648", 2147483648],
    ["9999999999", 9999999999],
    ["18446744073709551616", 18446744073709551616],
    ["00", 0],
    ["0123", 123],
    ["-0", -0],
    ["-123", -123],
    ["+123", 123],
    ["12.5", 12.5],
    ["1e3", 1000],
    ["12x", NaN],
    ["12345678x", NaN],
    ["214748364x", NaN],
    ["2147483647x", NaN],
    ["", 0],
    [" \t\n", 0],
    [" 42 ", 42],
    ["\u300042\u3000", 42],
    ["Infinity", Infinity],
    ["-Infinity", -Infinity],
    ["NaN", NaN],
    ["0x10", 16],
    ["0b10", 2],
    ["0o10", 8],
    ["\u4e2d", NaN],
    ["123456789012345678901234567890", 1.2345678901234568e29],
];
for (let i = 0; i < cases.length; i++) {
    const pair = makeNumber(cases[i][0], keep);
    checkNumber(pair[0], cases[i][1], "case " + i);
    check(pair[1] === keep && pair[1].marker === 42, "live object across conversion");
    checkNumber(makeNumberInTry(cases[i][0]), cases[i][1], "try case " + i);
}
print("string conversions: ok");

// Long concatenations and substrings exercise the non-LineString fallback when
// those representations survive runtime string optimizations.
let longDigits = "1234567890123456";
for (let i = 0; i < 6; i++) {
    longDigits = longDigits + longDigits;
}
checkNumber(makeNumber(longDigits, keep)[0], Infinity, "long concatenation");
const padded = "prefix:" + longDigits + ":suffix";
checkNumber(makeNumber(padded.substring(7, padded.length - 7), keep)[0], Infinity, "substring");
print("long strings: ok");

checkNumber(makeKnownStringNumber(), 12.5, "known string");
const two = makeTwoNumbers("2147483647", "12.5");
checkNumber(two[0], 2147483647, "first site");
checkNumber(two[1], 12.5, "second site");
check(two[0] !== two[1], "distinct wrappers");
check(makeKnownStringNumber() !== makeKnownStringNumber(), "fresh wrapper");
print("wrapper allocation: ok");

checkNumber(makeNumber(17, keep)[0], 17, "number input");
checkNumber(makeNumberInTry(undefined), NaN, "undefined input");
checkNumber(makeNumberInTry(null), 0, "null input");
let conversions = 0;
const objectInput = {
    valueOf() {
        conversions++;
        return "12.5";
    },
};
checkNumber(makeNumberInTry(objectInput), 12.5, "object input");
check(conversions === 1, "conversion runs once");
checkNumber(makeNumberInTry(BigInt(7)), 7, "BigInt input");
check(makeNumberInTry(Symbol("number")) instanceof TypeError, "Symbol input");
print("other inputs: ok");

check(ArkTools.arkSteedIsCompiled(makeNumber), "makeNumber stays compiled");
check(ArkTools.arkSteedIsCompiled(makeKnownStringNumber), "known string stays compiled");
check(ArkTools.arkSteedIsCompiled(makeTwoNumbers), "two sites stay compiled");
print("compiled paths: ok");
