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

// Boolean-to-number lowering proves INT without producing an INT32 alternative.
// An element index directly exercises BuildCheckedTaggedIntToI32 with that fact.
//! METHOD knownIntIndex
//! COUNT TaggedIntToI32 1
//! HAS_NOT CheckedTaggedIntToI32
//! HAS LoadTaggedElement
function knownIntIndex(array, flag) {
    return array[+flag];
}

//! METHOD knownIntCompareLeft
//! COUNT TaggedIntToI32 1
//! COUNT CheckedTaggedIntToI32 1
function knownIntCompareLeft(flag, limit) {
    return +flag < limit;
}

//! METHOD knownIntCompareRight
//! COUNT TaggedIntToI32 1
//! COUNT CheckedTaggedIntToI32 1
function knownIntCompareRight(limit, flag) {
    return limit < +flag;
}

//! METHOD knownIntBitwiseLeft
//! COUNT TaggedIntToI32 1
//! COUNT CheckedTaggedIntToI32 1
function knownIntBitwiseLeft(flag, value) {
    return +flag & value;
}

//! METHOD knownIntBitwiseRight
//! COUNT TaggedIntToI32 1
//! COUNT CheckedTaggedIntToI32 1
function knownIntBitwiseRight(value, flag) {
    return value >>> +flag;
}

// The second consumer must reuse the first untagging of number.
//! METHOD reuseKnownInt
//! COUNT TaggedIntToI32 1
//! COUNT CheckedTaggedIntToI32 1
function reuseKnownInt(flag, shift) {
    const number = +flag;
    return (number << shift) ^ number;
}

// Integer feedback alone must still guard both parameters.
//! METHOD unknownInts
//! COUNT CheckedTaggedIntToI32 2
//! HAS_NOT TaggedIntToI32
function unknownInts(value, mask) {
    return value & mask;
}

// ToNumber establishes NUMBER, which also allows tagged doubles.
//! METHOD numberIsNotInt
//! HAS DeoptIfNotNumber
//! COUNT CheckedTaggedIntToI32 2
//! HAS_NOT TaggedIntToI32
function numberIsNotInt(value, mask) {
    return +value & mask;
}

// Only one Phi input has an INT proof. The other accepts doubles.
//! METHOD mergedIntAndNumber
//! COUNT CheckedTaggedIntToI32 2
//! HAS_NOT TaggedIntToI32
function mergedIntAndNumber(useKnown, flag, other, mask) {
    const knownInt = +flag;
    const otherNumber = +other;
    const value = useKnown ? knownInt : otherNumber;
    return value | mask;
}

const array = [10, 20];
for (let iteration = 0; iteration < 2000; ++iteration) {
    const flag = iteration % 2 === 0;
    knownIntIndex(array, flag);
    knownIntCompareLeft(flag, 2);
    knownIntCompareRight(0, flag);
    knownIntBitwiseLeft(flag, 3);
    knownIntBitwiseRight(8, flag);
    reuseKnownInt(flag, 3);
    unknownInts(7, 3);
    numberIsNotInt(7, 3);
    // Warm ToNumber with doubles while the bitwise site sees only integers.
    mergedIntAndNumber(flag, flag, flag ? 2.5 : 7, 3);
}

ArkTools.arkSteedCompileSync(knownIntIndex);
ArkTools.arkSteedCompileSync(knownIntCompareLeft);
ArkTools.arkSteedCompileSync(knownIntCompareRight);
ArkTools.arkSteedCompileSync(knownIntBitwiseLeft);
ArkTools.arkSteedCompileSync(knownIntBitwiseRight);
ArkTools.arkSteedCompileSync(reuseKnownInt);
ArkTools.arkSteedCompileSync(unknownInts);
ArkTools.arkSteedCompileSync(numberIsNotInt);
ArkTools.arkSteedCompileSync(mergedIntAndNumber);

print(ArkTools.arkSteedIsCompiled(knownIntIndex));
print(ArkTools.arkSteedIsCompiled(knownIntCompareLeft));
print(ArkTools.arkSteedIsCompiled(knownIntCompareRight));
print(ArkTools.arkSteedIsCompiled(knownIntBitwiseLeft));
print(ArkTools.arkSteedIsCompiled(knownIntBitwiseRight));
print(ArkTools.arkSteedIsCompiled(reuseKnownInt));
print(ArkTools.arkSteedIsCompiled(unknownInts));
print(ArkTools.arkSteedIsCompiled(numberIsNotInt));
print(ArkTools.arkSteedIsCompiled(mergedIntAndNumber));

print(knownIntIndex(array, true));
print(knownIntIndex(array, false));
print(knownIntIndex([10], true));
print(knownIntCompareLeft(true, 2));
print(knownIntCompareLeft(false, 0));
print(knownIntCompareLeft(true, 0.5));
print(knownIntCompareLeft(true, "2"));
print(knownIntCompareRight(-2147483648, true));
print(knownIntCompareRight(2147483647, false));
print(knownIntCompareRight(0.5, true));
print(knownIntCompareRight("0", true));
print(knownIntBitwiseLeft(true, -1));
print(knownIntBitwiseLeft(false, -1));
print(knownIntBitwiseLeft(true, 2.5));
print(knownIntBitwiseLeft(true, "3"));
print(knownIntBitwiseRight(-2147483648, true));
print(knownIntBitwiseRight(-2147483648, false));
print(reuseKnownInt(true, 31));
print(reuseKnownInt(true, 32));
print(reuseKnownInt(true, -1));
print(reuseKnownInt(false, 31));
print(unknownInts(-2147483648, -1));
print(unknownInts(2147483647, -1));
print(unknownInts(2.5, 3));
print(unknownInts("7", 3));
print(numberIsNotInt(2.5, 3));
print(numberIsNotInt(NaN, 3));
print(numberIsNotInt(Infinity, 3));
print(numberIsNotInt(-0, 3));
print(numberIsNotInt("7", 3));
print(mergedIntAndNumber(true, true, 2.5, 0));
print(mergedIntAndNumber(false, true, 2.5, 0));
print(mergedIntAndNumber(false, true, "7", 0));
