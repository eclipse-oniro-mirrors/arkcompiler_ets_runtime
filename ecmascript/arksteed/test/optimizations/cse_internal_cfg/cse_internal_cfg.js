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

//! METHOD repeatedMixedString
//! COUNT BranchIfTaggedString 2
function repeatedMixedString(value) {
    const first = "" + value;
    const second = "" + value;
    return first + ":" + second;
}

//! METHOD mixedStringThenAdd
//! HAS BranchIfTaggedString
function mixedStringThenAdd(value) {
    const text = "" + value;
    return text + ":" + (value + 1);
}

//! METHOD repeatedStringTruthiness
//! COUNT StringToBoolean 1
function repeatedStringTruthiness(value) {
    const text = value + "";
    const first = !text;
    const second = !text;
    return +first + +second;
}

function typedStorePaths(array, value, flag) {
    const first = array[0];
    if (flag) array[0] = value;
    return first + array[0];
}

function polymorphicLoad(object, flag) {
    const first = object.value;
    const second = object.value;
    if (flag) return first + second;
    return second - first;
}

function differentOffsets(object, flag) {
    const first = object.left;
    if (flag) return first + object.right;
    return object.right - first;
}

function accessorStore(object, value, flag) {
    const first = object.backing;
    if (flag) object.value = value;
    return first + object.backing;
}

function check(condition, name) {
    if (!condition) throw new Error(name);
}

function First(value) { this.value = value; this.marker = 1; }
function Second(value) { this.marker = 2; this.value = value; }
const firstObject = new First(3.5);
const secondObject = new Second(7.5);
const offsets = { left: 2.5, right: 6.5 };
const array = new Int32Array([2]);
const values = [0.5, -3.5, NaN, Infinity, -Infinity, 1e30];
const strings = ["", "abc", "0", "-0"];
let setterCount = 0;
const accessor = {
    backing: 2.5,
    set value(value) { setterCount++; this.backing = value; }
};
for (let i = 0; i < 2000; ++i) {
    const flag = (i & 1) === 0;
    const value = flag ? strings[i % strings.length] : values[i % values.length];
    repeatedMixedString(value);
    mixedStringThenAdd(value);
    repeatedStringTruthiness(strings[i % strings.length]);
    typedStorePaths(array, values[i % values.length], flag);
    polymorphicLoad(flag ? firstObject : secondObject, flag);
    differentOffsets(offsets, flag);
    accessorStore(accessor, 3.5, flag);
}

const functions = [repeatedMixedString, mixedStringThenAdd, repeatedStringTruthiness,
    typedStorePaths, polymorphicLoad, differentOffsets, accessorStore];
for (const fn of functions) {
    ArkTools.arkSteedCompileSync(fn);
    ArkTools.waitJitCompileFinish(fn);
    check(ArkTools.arkSteedIsCompiled(fn), fn.name + " compile");
}
for (const value of [...strings, ...values, -0, 2, -2]) {
    check(repeatedMixedString(value) === String(value) + ":" + String(value), "repeatedMixedString");
    check(mixedStringThenAdd(value) === String(value) + ":" + String(value + 1), "mixedStringThenAdd");
}
for (const text of strings) {
    check(repeatedStringTruthiness(text) === (text ? 0 : 2), "repeatedStringTruthiness");
}
for (const value of values) {
    for (const flag of [true, false]) {
        array[0] = 2;
        check(typedStorePaths(array, value, flag) === 2 + (flag ? value | 0 : 2), "typedStorePaths");
        check(polymorphicLoad(firstObject, flag) === (flag ? 7 : 0), "first polymorphicLoad");
        check(polymorphicLoad(secondObject, flag) === (flag ? 15 : 0), "second polymorphicLoad");
        check(differentOffsets(offsets, flag) === (flag ? 9 : 4), "differentOffsets");
        accessor.backing = 2.5;
        setterCount = 0;
        check(accessorStore(accessor, 8.5, flag) === (flag ? 11 : 5), "accessorStore");
        check(setterCount === (flag ? 1 : 0), "setter side effects");
    }
}
for (const fn of functions) check(ArkTools.arkSteedIsCompiled(fn), fn.name + " stays compiled");
print("PASS");
