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

// G11.3 (P0): the normal path of JS-array element access. Every builder
// allocates its array inside the compiled method, so the graph contains the
// `InitializeEmptyArray` vertex of the `[]` allocation; every writer stores through
// the compiled element-store path (`StoreTaggedElement`) and every reader uses
// the compiled element load (`LoadTaggedElement`). The packed smi/double/
// object builders and writers keep exactly one element kind each, so the
// compiled targets stay on their path and the whole case asserts `DEOPT_COUNT
// *Deoptimize* 0`.
//
// The int-valued (smi) and double-valued stores are classified `NonHeap` and
// emit the barrier-less `StoreTaggedElement`; the object-valued store is a heap
// value and emits `StoreTaggedElementWithBarrier`. The two directions live in
// separate methods, so `HAS_NOT StoreTaggedElementWithBarrier` can name a
// method whose stores never need the barrier.
//
// The specified `undefined` of a hole is observed through `readHole`, which is
// deliberately left uncompiled: the compiled element load guards the hole with
// an eager deopt, so reading it through compiled code would contradict the
// zero-deopt obligation of this normal case. The compiled readers only read
// present elements, and the hole itself is observed interpretively.
//
// Expected values follow ECMA-262 2021 10.4.2 (ArrayCreate and its
// [[DefineOwnProperty]] for integer indices) and 23.1.3.1 (Array.prototype
// element access); every constant below is derived by hand and is independent
// of the runtime result.

//! PARAMS --compiler-jit-hotness-threshold=60000

//! METHOD buildPackedSmi
//! HAS AllocateInYoung
//! HAS InitializeEmptyArray

//! METHOD buildHoleySmi
//! HAS AllocateInYoung
//! HAS InitializeEmptyArray

//! METHOD buildPackedDouble
//! HAS AllocateInYoung
//! HAS InitializeEmptyArray

//! METHOD buildPackedObject
//! HAS AllocateInYoung
//! HAS InitializeEmptyArray

//! METHOD readPackedSmi
//! HAS LoadTaggedElement

//! METHOD readHoleySmi
//! HAS LoadTaggedElement

//! METHOD readPackedDouble
//! HAS LoadTaggedElement

//! METHOD readPackedObject
//! HAS LoadTaggedElement

//! METHOD writePackedSmi
//! HAS StoreTaggedElement
//! HAS_NOT StoreTaggedElementWithBarrier

//! METHOD writeHoleySmi
//! HAS StoreTaggedElement

//! METHOD writePackedDouble
//! HAS StoreTaggedElement
//! HAS_NOT StoreTaggedElementWithBarrier

//! METHOD writePackedObject
//! HAS StoreTaggedElementWithBarrier

//! COMPILE_COUNT_GE *buildPackedSmi* 1
//! COMPILE_COUNT_GE *buildHoleySmi* 1
//! COMPILE_COUNT_GE *buildPackedDouble* 1
//! COMPILE_COUNT_GE *buildPackedObject* 1
//! COMPILE_COUNT_GE *readPackedSmi* 1
//! COMPILE_COUNT_GE *readHoleySmi* 1
//! COMPILE_COUNT_GE *readPackedDouble* 1
//! COMPILE_COUNT_GE *readPackedObject* 1
//! COMPILE_COUNT_GE *writePackedSmi* 1
//! COMPILE_COUNT_GE *writeHoleySmi* 1
//! COMPILE_COUNT_GE *writePackedDouble* 1
//! COMPILE_COUNT_GE *writePackedObject* 1
//! EXEC_COUNT_GE *buildPackedSmi* 1
//! EXEC_COUNT_GE *buildHoleySmi* 1
//! EXEC_COUNT_GE *buildPackedDouble* 1
//! EXEC_COUNT_GE *buildPackedObject* 1
//! EXEC_COUNT_GE *readPackedSmi* 1
//! EXEC_COUNT_GE *readHoleySmi* 1
//! EXEC_COUNT_GE *readPackedDouble* 1
//! EXEC_COUNT_GE *readPackedObject* 1
//! EXEC_COUNT_GE *writePackedSmi* 1
//! EXEC_COUNT_GE *writeHoleySmi* 1
//! EXEC_COUNT_GE *writePackedDouble* 1
//! EXEC_COUNT_GE *writePackedObject* 1
//! DEOPT_COUNT *Deoptimize* 0

function buildPackedSmi(seed) {
    let array = [];
    array[0] = seed;
    array[1] = seed + 1;
    array[2] = seed + 2;
    array[3] = seed + 3;
    return array;
}

function buildHoleySmi(seed) {
    let array = [];
    array[0] = seed;
    array[2] = seed + 2;
    return array;
}

function buildPackedDouble(seed) {
    let array = [];
    array[0] = seed + 0.5;
    array[1] = seed + 1.5;
    array[2] = seed + 2.5;
    array[3] = seed + 3.5;
    return array;
}

function buildPackedObject(seed) {
    let array = [];
    array[0] = { marker: seed };
    array[1] = { marker: seed + 1 };
    array[2] = { marker: seed + 2 };
    array[3] = { marker: seed + 3 };
    return array;
}

function readPackedSmi(array, index) {
    return array[index];
}

function readHoleySmi(array, index) {
    return array[index];
}

function readPackedDouble(array, index) {
    return array[index];
}

function readPackedObject(array, index) {
    return array[index];
}

// Interpreted hole observation: not compiled, not installed.
function readHole(array) {
    return array[1];
}

function writePackedSmi(array, index, value) {
    array[index] = value;
    return array[index];
}

function writeHoleySmi(array, index, value) {
    array[index] = value;
    return array[index];
}

function writePackedDouble(array, index, value) {
    array[index] = value;
    return array[index];
}

function writePackedObject(array, index, value) {
    array[index] = value;
    return array[index];
}

// Warm every target with dynamic operands of one element kind, then install it.
// No target observes a foreign element kind before its own installation, so
// each keeps its monomorphic feedback until it is compiled.
for (let i = 1; i < 2000; i++) {
    const smi = buildPackedSmi(i);
    const holey = buildHoleySmi(i);
    const double = buildPackedDouble(i);
    const object = buildPackedObject(i);
    readPackedSmi(smi, i & 3);
    readHoleySmi(holey, (i & 1) * 2);
    readPackedDouble(double, i & 3);
    readPackedObject(object, i & 3);
    writePackedSmi(smi, i & 3, i * 2);
    writeHoleySmi(holey, 1, i * 3);
    writePackedDouble(double, i & 3, i + 0.25);
    writePackedObject(object, i & 3, { marker: i * 5 });
}

ArkTools.arkSteedCompileSync(buildPackedSmi);
ArkTools.waitJitCompileFinish(buildPackedSmi);
ArkTools.arkSteedCompileSync(buildHoleySmi);
ArkTools.waitJitCompileFinish(buildHoleySmi);
ArkTools.arkSteedCompileSync(buildPackedDouble);
ArkTools.waitJitCompileFinish(buildPackedDouble);
ArkTools.arkSteedCompileSync(buildPackedObject);
ArkTools.waitJitCompileFinish(buildPackedObject);
ArkTools.arkSteedCompileSync(readPackedSmi);
ArkTools.waitJitCompileFinish(readPackedSmi);
ArkTools.arkSteedCompileSync(readHoleySmi);
ArkTools.waitJitCompileFinish(readHoleySmi);
ArkTools.arkSteedCompileSync(readPackedDouble);
ArkTools.waitJitCompileFinish(readPackedDouble);
ArkTools.arkSteedCompileSync(readPackedObject);
ArkTools.waitJitCompileFinish(readPackedObject);
ArkTools.arkSteedCompileSync(writePackedSmi);
ArkTools.waitJitCompileFinish(writePackedSmi);
ArkTools.arkSteedCompileSync(writeHoleySmi);
ArkTools.waitJitCompileFinish(writeHoleySmi);
ArkTools.arkSteedCompileSync(writePackedDouble);
ArkTools.waitJitCompileFinish(writePackedDouble);
ArkTools.arkSteedCompileSync(writePackedObject);
ArkTools.waitJitCompileFinish(writePackedObject);

const smi = buildPackedSmi(70);                 // [70, 71, 72, 73]
print(readPackedSmi(smi, 2));                   // 72
print(writePackedSmi(smi, 2, 100));             // 100
print(smi[2]);                                  // 100

const holey = buildHoleySmi(80);                // [80, <hole>, 82]
print(readHoleySmi(holey, 0));                  // 80
print(readHoleySmi(holey, 2));                  // 82
print(readHole(holey));                         // undefined
print(writeHoleySmi(holey, 1, 55));             // 55
print(holey[1]);                                // 55

const double = buildPackedDouble(1);            // [1.5, 2.5, 3.5, 4.5]
print(readPackedDouble(double, 3));             // 4.5
print(writePackedDouble(double, 3, 9.5));       // 9.5
print(double[3]);                               // 9.5

const object = buildPackedObject(7);            // [{7}, {8}, {9}, {10}]
print(readPackedObject(object, 0).marker);      // 7
const replacement = { marker: 42 };
print(writePackedObject(object, 1, replacement) === replacement);  // true
print(object[1].marker);                        // 42
