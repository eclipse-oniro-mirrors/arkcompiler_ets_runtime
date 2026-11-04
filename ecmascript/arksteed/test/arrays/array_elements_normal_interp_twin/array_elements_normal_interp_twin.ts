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

//! PARAMS --compiler-enable-jit=false

// Interpreter twin of `arrays/array_elements_normal` (arrays family, A5/E6).
// The same builders, readers, writers and probes run with the JIT disabled, and
// the expectation file is byte-identical to the primary case, so the
// differential comparison covers the specified element values of the normal
// array path. The primary oracle stays ECMA-262 2021 10.4.2 (ArrayCreate) and
// 23.1.3.1 (element access); interpreter agreement is a necessary but not
// sufficient check. The twin declares only the JIT-off option, carries no JIT
// annotation and omits the installation calls.

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
