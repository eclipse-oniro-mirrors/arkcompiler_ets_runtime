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

//! PARAMS --compiler-jit-hotness-threshold=60000 --enable-pgo-profiler=true --compiler-enable-jit-lazy-deopt=false

//! METHOD dominatingRead
//! COUNT TypedArrayDoubleLoadElement 1
function dominatingRead(array, index, flag) {
    const first = array[index];
    let extra;
    if (flag) extra = 3;
    else extra = 7;
    return first + array[index] + extra;
}

//! METHOD branchLocalReads
//! COUNT TypedArrayDoubleLoadElement 3
function branchLocalReads(array, index, flag) {
    let first;
    if (flag) first = array[index] + 1;
    else first = array[index] + 2;
    return first + array[index];
}

//! METHOD oneBranchWrites
//! COUNT TypedArrayDoubleLoadElement 2
function oneBranchWrites(array, index, flag, value) {
    const first = array[index];
    if (flag) array[index] = value;
    return first + array[index];
}

//! METHOD bothBranchesWrite
//! COUNT TypedArrayDoubleLoadElement 2
function bothBranchesWrite(array, index, flag, value) {
    const first = array[index];
    if (flag) array[index] = value;
    else array[index] = value + 0.5;
    return first + array[index];
}

//! METHOD localReadAfterWrite
//! COUNT TypedArrayDoubleLoadElement 2
function localReadAfterWrite(array, index, value) {
    const first = array[index];
    array[index] = value;
    const second = array[index];
    return first + second + array[index];
}

//! METHOD differentInputs
//! COUNT TypedArrayDoubleLoadElement 2
function differentInputs(array, flag) {
    const first = array[0];
    const second = array[1];
    let extra;
    if (flag) extra = array[1];
    else extra = second;
    return first + extra + array[0];
}

//! METHOD readAcrossCall
//! COUNT TypedArrayDoubleLoadElement 2
function readAcrossCall(array, index, callback) {
    const first = array[index];
    callback(array, index);
    return first + array[index];
}

function changeElement(array, index) {
    array[index] += 2.5;
}

function readInLoop(array, count) {
    const first = array[0];
    let sum = 0;
    for (let i = 0; i < count; ++i) {
        sum += array[0];
        array[0] += 0.5;
    }
    return first + sum;
}

function throwAfterWrite(array) {
    array[0] += 1.5;
    throw 42;
}

function readInCatch(array, callback) {
    const first = array[0];
    try {
        callback(array);
    } catch (error) {
        return first + array[0] + error;
    }
    return first + array[0];
}

function getterReads(object, flag) {
    const first = object.value;
    let extra;
    if (flag) extra = 1;
    else extra = 2;
    return first + object.value + extra;
}

function check(condition, name) {
    if (!condition) throw new Error(name);
}

const array = new Float64Array([2.5, 4.5]);
let getterCount = 0;
const object = { get value() { return ++getterCount; } };
for (let i = 0; i < 2000; ++i) {
    const flag = (i & 1) === 0;
    const index = i & 1;
    dominatingRead(array, index, flag);
    branchLocalReads(array, index, flag);
    oneBranchWrites(array, index, flag, 3.5);
    bothBranchesWrite(array, index, flag, 6.5);
    localReadAfterWrite(array, index, 2.5);
    differentInputs(array, flag);
    readAcrossCall(array, index, changeElement);
    readInLoop(array, 3);
    readInCatch(array, throwAfterWrite);
    getterReads(object, flag);
}

const functions = [dominatingRead, branchLocalReads, oneBranchWrites, bothBranchesWrite,
    localReadAfterWrite, differentInputs, readAcrossCall, readInLoop, readInCatch, getterReads];
for (const fn of functions) {
    ArkTools.arkSteedCompileSync(fn);
    ArkTools.waitJitCompileFinish(fn);
    check(ArkTools.arkSteedIsCompiled(fn), fn.name + " compile");
}

for (let i = 0; i < 100; ++i) {
    const flag = (i & 1) === 0;
    const index = i & 1;
    array[0] = 2.5;
    array[1] = 4.5;
    const first = array[index];
    check(dominatingRead(array, index, flag) === first * 2 + (flag ? 3 : 7), "dominatingRead");
    check(branchLocalReads(array, index, flag) === first * 2 + (flag ? 1 : 2), "branchLocalReads");
    check(oneBranchWrites(array, index, flag, 8.5) === first + (flag ? 8.5 : first), "oneBranchWrites");
    array[index] = first;
    check(bothBranchesWrite(array, index, flag, 8.5) === first + (flag ? 8.5 : 9), "bothBranchesWrite");
    array[index] = first;
    check(localReadAfterWrite(array, index, 8.5) === first + 17, "localReadAfterWrite");
    array[0] = 2.5;
    array[1] = 4.5;
    check(differentInputs(array, flag) === 9.5, "differentInputs");
    check(readAcrossCall(array, index, changeElement) === first * 2 + 2.5, "readAcrossCall");
    array[0] = 2.5;
    check(readInLoop(array, 4) === 15.5, "readInLoop");
    check(array[0] === 4.5, "loop store");
    array[0] = 2.5;
    check(readInCatch(array, throwAfterWrite) === 48.5, "readInCatch");
    getterCount = 0;
    check(getterReads(object, flag) === 3 + (flag ? 1 : 2), "getterReads");
    check(getterCount === 2, "getter side effects");
}
for (const fn of functions) check(ArkTools.arkSteedIsCompiled(fn), fn.name + " stays compiled");
print("PASS");
