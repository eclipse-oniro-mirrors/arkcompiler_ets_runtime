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

// @ts-nocheck
//! PARAMS --compiler-jit-hotness-threshold=10000
//! EXEC_COUNT_GE *extendedAndRight 12
//! EXEC_COUNT_GE *extendedAndBoth 12
//! EXEC_COUNT_GE *extendedAndDynamicRight 12
//! EXEC_COUNT_GE *extendedAndDynamicLeft 12
//! EXEC_COUNT_GE *extendedOrRight 12
//! EXEC_COUNT_GE *extendedOrBoth 12
//! EXEC_COUNT_GE *extendedOrDynamicRight 12
//! EXEC_COUNT_GE *extendedOrDynamicLeft 12
//! EXEC_COUNT_GE *extendedXorRight 12
//! EXEC_COUNT_GE *extendedXorBoth 12
//! EXEC_COUNT_GE *extendedXorDynamicRight 12
//! EXEC_COUNT_GE *extendedXorDynamicLeft 12
//! EXEC_COUNT_GE *extendedShlRight 12
//! EXEC_COUNT_GE *extendedShlBoth 12
//! EXEC_COUNT_GE *extendedShlDynamicRight 12
//! EXEC_COUNT_GE *extendedShlDynamicLeft 12
//! EXEC_COUNT_GE *extendedSarRight 12
//! EXEC_COUNT_GE *extendedSarBoth 12
//! EXEC_COUNT_GE *extendedSarDynamicRight 12
//! EXEC_COUNT_GE *extendedSarDynamicLeft 12
//! EXEC_COUNT_GE *extendedShrRight 12
//! EXEC_COUNT_GE *extendedShrBoth 12
//! EXEC_COUNT_GE *extendedShrDynamicRight 12
//! EXEC_COUNT_GE *extendedShrDynamicLeft 12
//! DEOPT_COUNT * 0
declare function print(arg: any): void;

// All right-side, two-load and checked-Int forms stay in JIT for U32 edge values.
//! METHOD extendedAndRight
//! HAS TypedArrayDoubleLoadElement
//! COUNT_GE F64ToI32Trunc 1
function extendedAndRight(a, b, i, k) {
    return 305419896 & a[i];
}

//! METHOD extendedAndBoth
//! HAS TypedArrayDoubleLoadElement
//! COUNT_GE F64ToI32Trunc 2
function extendedAndBoth(a, b, i, k) {
    return a[i] & b[i];
}

//! METHOD extendedAndDynamicRight
//! HAS TypedArrayDoubleLoadElement
//! COUNT_GE F64ToI32Trunc 1
function extendedAndDynamicRight(a, b, i, k) {
    return a[i] & k;
}

//! METHOD extendedAndDynamicLeft
//! HAS TypedArrayDoubleLoadElement
//! COUNT_GE F64ToI32Trunc 1
function extendedAndDynamicLeft(a, b, i, k) {
    return k & a[i];
}

//! METHOD extendedOrRight
//! HAS TypedArrayDoubleLoadElement
//! COUNT_GE F64ToI32Trunc 1
function extendedOrRight(a, b, i, k) {
    return 305419896 | a[i];
}

//! METHOD extendedOrBoth
//! HAS TypedArrayDoubleLoadElement
//! COUNT_GE F64ToI32Trunc 2
function extendedOrBoth(a, b, i, k) {
    return a[i] | b[i];
}

//! METHOD extendedOrDynamicRight
//! HAS TypedArrayDoubleLoadElement
//! COUNT_GE F64ToI32Trunc 1
function extendedOrDynamicRight(a, b, i, k) {
    return a[i] | k;
}

//! METHOD extendedOrDynamicLeft
//! HAS TypedArrayDoubleLoadElement
//! COUNT_GE F64ToI32Trunc 1
function extendedOrDynamicLeft(a, b, i, k) {
    return k | a[i];
}

//! METHOD extendedXorRight
//! HAS TypedArrayDoubleLoadElement
//! COUNT_GE F64ToI32Trunc 1
function extendedXorRight(a, b, i, k) {
    return 305419896 ^ a[i];
}

//! METHOD extendedXorBoth
//! HAS TypedArrayDoubleLoadElement
//! COUNT_GE F64ToI32Trunc 2
function extendedXorBoth(a, b, i, k) {
    return a[i] ^ b[i];
}

//! METHOD extendedXorDynamicRight
//! HAS TypedArrayDoubleLoadElement
//! COUNT_GE F64ToI32Trunc 1
function extendedXorDynamicRight(a, b, i, k) {
    return a[i] ^ k;
}

//! METHOD extendedXorDynamicLeft
//! HAS TypedArrayDoubleLoadElement
//! COUNT_GE F64ToI32Trunc 1
function extendedXorDynamicLeft(a, b, i, k) {
    return k ^ a[i];
}

//! METHOD extendedShlRight
//! HAS TypedArrayDoubleLoadElement
//! COUNT_GE F64ToI32Trunc 1
function extendedShlRight(a, b, i, k) {
    return 305419896 << a[i];
}

//! METHOD extendedShlBoth
//! HAS TypedArrayDoubleLoadElement
//! COUNT_GE F64ToI32Trunc 2
function extendedShlBoth(a, b, i, k) {
    return a[i] << b[i];
}

//! METHOD extendedShlDynamicRight
//! HAS TypedArrayDoubleLoadElement
//! COUNT_GE F64ToI32Trunc 1
function extendedShlDynamicRight(a, b, i, k) {
    return a[i] << k;
}

//! METHOD extendedShlDynamicLeft
//! HAS TypedArrayDoubleLoadElement
//! COUNT_GE F64ToI32Trunc 1
function extendedShlDynamicLeft(a, b, i, k) {
    return k << a[i];
}

//! METHOD extendedSarRight
//! HAS TypedArrayDoubleLoadElement
//! COUNT_GE F64ToI32Trunc 1
function extendedSarRight(a, b, i, k) {
    return 305419896 >> a[i];
}

//! METHOD extendedSarBoth
//! HAS TypedArrayDoubleLoadElement
//! COUNT_GE F64ToI32Trunc 2
function extendedSarBoth(a, b, i, k) {
    return a[i] >> b[i];
}

//! METHOD extendedSarDynamicRight
//! HAS TypedArrayDoubleLoadElement
//! COUNT_GE F64ToI32Trunc 1
function extendedSarDynamicRight(a, b, i, k) {
    return a[i] >> k;
}

//! METHOD extendedSarDynamicLeft
//! HAS TypedArrayDoubleLoadElement
//! COUNT_GE F64ToI32Trunc 1
function extendedSarDynamicLeft(a, b, i, k) {
    return k >> a[i];
}

//! METHOD extendedShrRight
//! HAS TypedArrayDoubleLoadElement
//! COUNT_GE F64ToI32Trunc 1
function extendedShrRight(a, b, i, k) {
    return 305419896 >>> a[i];
}

//! METHOD extendedShrBoth
//! HAS TypedArrayDoubleLoadElement
//! COUNT_GE F64ToI32Trunc 2
function extendedShrBoth(a, b, i, k) {
    return a[i] >>> b[i];
}

//! METHOD extendedShrDynamicRight
//! HAS TypedArrayDoubleLoadElement
//! COUNT_GE F64ToI32Trunc 1
function extendedShrDynamicRight(a, b, i, k) {
    return a[i] >>> k;
}

//! METHOD extendedShrDynamicLeft
//! HAS TypedArrayDoubleLoadElement
//! COUNT_GE F64ToI32Trunc 1
function extendedShrDynamicLeft(a, b, i, k) {
    return k >>> a[i];
}

const a = new Uint32Array(2); const b = new Uint32Array(2);
const index = 0;
a[index] = 0; a[index + 1] = 1; b[index] = 1; b[index + 1] = 0;
for (let n = 0; n < 1000; n++) {
    extendedAndRight(a, b, index + (n & 1), 305419896);
    extendedAndBoth(a, b, index + (n & 1), 305419896);
    extendedAndDynamicRight(a, b, index + (n & 1), 255);
    extendedAndDynamicLeft(a, b, index + (n & 1), 305419896);
    extendedOrRight(a, b, index + (n & 1), 305419896);
    extendedOrBoth(a, b, index + (n & 1), 305419896);
    extendedOrDynamicRight(a, b, index + (n & 1), 3);
    extendedOrDynamicLeft(a, b, index + (n & 1), 305419896);
    extendedXorRight(a, b, index + (n & 1), 305419896);
    extendedXorBoth(a, b, index + (n & 1), 305419896);
    extendedXorDynamicRight(a, b, index + (n & 1), 1437226410);
    extendedXorDynamicLeft(a, b, index + (n & 1), 305419896);
    extendedShlRight(a, b, index + (n & 1), 305419896);
    extendedShlBoth(a, b, index + (n & 1), 305419896);
    extendedShlDynamicRight(a, b, index + (n & 1), 1);
    extendedShlDynamicLeft(a, b, index + (n & 1), 305419896);
    extendedSarRight(a, b, index + (n & 1), 305419896);
    extendedSarBoth(a, b, index + (n & 1), 305419896);
    extendedSarDynamicRight(a, b, index + (n & 1), 8);
    extendedSarDynamicLeft(a, b, index + (n & 1), 305419896);
    extendedShrRight(a, b, index + (n & 1), 305419896);
    extendedShrBoth(a, b, index + (n & 1), 305419896);
    extendedShrDynamicRight(a, b, index + (n & 1), 16);
    extendedShrDynamicLeft(a, b, index + (n & 1), 305419896);
}
ArkTools.arkSteedCompileSync(extendedAndRight);
ArkTools.arkSteedCompileSync(extendedAndBoth);
ArkTools.arkSteedCompileSync(extendedAndDynamicRight);
ArkTools.arkSteedCompileSync(extendedAndDynamicLeft);
ArkTools.arkSteedCompileSync(extendedOrRight);
ArkTools.arkSteedCompileSync(extendedOrBoth);
ArkTools.arkSteedCompileSync(extendedOrDynamicRight);
ArkTools.arkSteedCompileSync(extendedOrDynamicLeft);
ArkTools.arkSteedCompileSync(extendedXorRight);
ArkTools.arkSteedCompileSync(extendedXorBoth);
ArkTools.arkSteedCompileSync(extendedXorDynamicRight);
ArkTools.arkSteedCompileSync(extendedXorDynamicLeft);
ArkTools.arkSteedCompileSync(extendedShlRight);
ArkTools.arkSteedCompileSync(extendedShlBoth);
ArkTools.arkSteedCompileSync(extendedShlDynamicRight);
ArkTools.arkSteedCompileSync(extendedShlDynamicLeft);
ArkTools.arkSteedCompileSync(extendedSarRight);
ArkTools.arkSteedCompileSync(extendedSarBoth);
ArkTools.arkSteedCompileSync(extendedSarDynamicRight);
ArkTools.arkSteedCompileSync(extendedSarDynamicLeft);
ArkTools.arkSteedCompileSync(extendedShrRight);
ArkTools.arkSteedCompileSync(extendedShrBoth);
ArkTools.arkSteedCompileSync(extendedShrDynamicRight);
ArkTools.arkSteedCompileSync(extendedShrDynamicLeft);
const values = [0, 1, 255, 2147483647, 2147483648, 2147483649, 4294967295, 305419896, 2193914387, 3909840005, 1832331801, 4294901761];
const counts = [1, 8, 16, 31, 33, 4294967295, 9, 24, 5, 17, 3, 7];
for (let n = 0; n < values.length; n++) {
    a[index] = values[n]; b[index] = counts[n];
    print(values[n] + ":" + [extendedAndRight(a, b, index, 305419896), extendedAndBoth(a, b, index, 305419896), extendedAndDynamicRight(a, b, index, 255), extendedAndDynamicLeft(a, b, index, 305419896), extendedOrRight(a, b, index, 305419896), extendedOrBoth(a, b, index, 305419896), extendedOrDynamicRight(a, b, index, 3), extendedOrDynamicLeft(a, b, index, 305419896), extendedXorRight(a, b, index, 305419896), extendedXorBoth(a, b, index, 305419896), extendedXorDynamicRight(a, b, index, 1437226410), extendedXorDynamicLeft(a, b, index, 305419896), extendedShlRight(a, b, index, 305419896), extendedShlBoth(a, b, index, 305419896), extendedShlDynamicRight(a, b, index, 1), extendedShlDynamicLeft(a, b, index, 305419896), extendedSarRight(a, b, index, 305419896), extendedSarBoth(a, b, index, 305419896), extendedSarDynamicRight(a, b, index, 8), extendedSarDynamicLeft(a, b, index, 305419896), extendedShrRight(a, b, index, 305419896), extendedShrBoth(a, b, index, 305419896), extendedShrDynamicRight(a, b, index, 16), extendedShrDynamicLeft(a, b, index, 305419896)].join(","));
}
