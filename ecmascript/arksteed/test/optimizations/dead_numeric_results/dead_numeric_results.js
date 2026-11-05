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

//! METHOD intBranch
//! HAS BranchIfInt32Compare
//! HAS_NOT I32ConditionCheck
function intBranch(x, y) {
    x = x | 0;
    y = y | 0;
    if (x < y) {
        return 11;
    }
    return 12;
}

//! METHOD doubleBranch
//! HAS BranchIfFloat64Compare
//! HAS_NOT F64ConditionCheck
function doubleBranch(x, y) {
    x = x * 0.5;
    y = y * 0.5;
    if (x < y) {
        return 21;
    }
    return 22;
}

function doubleRelations(x, y) {
    x = x * 0.5;
    y = y * 0.5;
    let result = 0;
    if (x <= y) result += 1;
    if (x > y) result += 2;
    if (x >= y) result += 4;
    if (x === y) result += 8;
    if (x !== y) result += 16;
    return result;
}

//! METHOD usedCompare
//! HAS I32ConditionCheck
function usedCompare(x, y) {
    x = x | 0;
    y = y | 0;
    const result = x < y;
    if (result) {
        return result;
    }
    return result;
}

//! METHOD deadResults
//! COUNT F64Mul 1
//! HAS_NOT F64Add
//! HAS_NOT F64Neg
//! HAS_NOT F64ConditionCheck
function deadResults(x) {
    const value = x * 0.5;
    value + 2;
    value * 3;
    value < 7;
    -value;
    return value;
}

//! METHOD doubleIdentity
//! COUNT F64Mul 1
//! COUNT F64ToTaggedDouble 1
function doubleIdentity(x) {
    const value = x * 0.5;
    return ((value * 1) / 1) - 0;
}

//! METHOD unusedBooleanCompare
//! HAS_NOT TaggedNotEqual
//! CODEGEN_COUNT *TaggedNotEqual* 0
//! CODEGEN_COUNT *BranchIfReferenceEqual* 1
function unusedBooleanCompare(value) {
    if (value) return 31;
    return 32;
}

//! METHOD guardedIdentity
//! HAS TaggedDoubleToBoolean
//! HAS_NOT TaggedDoubleToF64
//! HAS_NOT F64ToTaggedDouble
function guardedIdentity(value) {
    if (value) return value * 1;
    return value * 1;
}

function discardUnknown(value) {
    value + 1;
    return 41;
}

function compareAcrossCall(x, y, callback) {
    x = x | 0;
    y = y | 0;
    const comparison = x < y;
    callback();
    return comparison;
}

function deadInCatch(value) {
    try {
        value + 1;
    } catch (error) {
        return error;
    }
    return 51;
}

function noop() {}
function check(condition) {
    if (!condition) throw new Error("DCE result mismatch");
}

let conversions = 0;
const object = {valueOf() { ++conversions; return 2; }};
for (let i = 0; i < 2000; ++i) {
    intBranch(i & 1, 1);
    doubleBranch(i & 1 ? 1.5 : 3.5, 2.5);
    doubleRelations(i & 1 ? 1.5 : 3.5, 2.5);
    doubleRelations(2.5, 2.5);
    doubleRelations(NaN, 2.5);
    usedCompare(i & 1, 1);
    deadResults(2.5);
    doubleIdentity(2.5);
    guardedIdentity(2.5);
    unusedBooleanCompare((i & 1) === 0);
    discardUnknown(object);
    compareAcrossCall(i & 1, 1, noop);
    deadInCatch(object);
}
const methods = [intBranch, doubleBranch, doubleRelations, usedCompare, deadResults,
    doubleIdentity, guardedIdentity, unusedBooleanCompare, discardUnknown, compareAcrossCall, deadInCatch];
for (const method of methods) {
    ArkTools.arkSteedCompileSync(method);
    check(ArkTools.arkSteedIsCompiled(method));
}
check(intBranch(1, 2) === 11);
check(intBranch(2, 1) === 12);
check(intBranch(-2147483648, 2147483647) === 11);
check(usedCompare(1, 2) === true);
check(usedCompare(2, 1) === false);
check(unusedBooleanCompare(true) === 31);
check(unusedBooleanCompare(false) === 32);
const values = [-Infinity, -3.5, -0, 0, 2.5, Infinity, NaN];
for (const x of values) {
    check(Object.is(deadResults(x), x * 0.5));
    check(Object.is(doubleIdentity(x), x * 0.5));
    for (const y of values) {
        check(doubleBranch(x, y) === (x < y ? 21 : 22));
        const expected = (x <= y ? 1 : 0) + (x > y ? 2 : 0) + (x >= y ? 4 : 0) +
            (x === y ? 8 : 0) + (x !== y ? 16 : 0);
        check(doubleRelations(x, y) === expected);
    }
}
check(ArkTools.arkSteedIsCompiled(doubleBranch));
check(ArkTools.arkSteedIsCompiled(doubleRelations));
check(guardedIdentity(2.5) === 2.5);
check(Object.is(guardedIdentity(-0), -0));
check(Number.isNaN(guardedIdentity(NaN)));
const before = conversions;
check(discardUnknown(object) === 41);
check(deadInCatch(object) === 51);
check(conversions === before + 2);
const marker = {};
const throwing = {valueOf() { throw marker; }};
check(deadInCatch(throwing) === marker);
let caught = false;
try {
    discardUnknown(throwing);
} catch (error) {
    caught = error === marker;
}
check(caught);
check(compareAcrossCall(1, 2, noop) === true);
check(compareAcrossCall(2, 1, noop) === false);
check(doubleBranch("1", "2") === 21);
check(deadResults("2.5") === 1.25);
check(doubleIdentity("2.5") === 1.25);
print("PASS");
