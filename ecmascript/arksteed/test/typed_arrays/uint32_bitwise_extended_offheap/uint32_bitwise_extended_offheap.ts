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
//! EXEC_COUNT_GE *extendedAndLeftOffHeap 12
//! EXEC_COUNT_GE *extendedAndRightOffHeap 12
//! EXEC_COUNT_GE *extendedAndBothOffHeap 12
//! EXEC_COUNT_GE *extendedOrDynamicLeftOffHeap 12
//! EXEC_COUNT_GE *extendedXorDynamicRightOffHeap 12
//! EXEC_COUNT_GE *extendedShlRightOffHeap 12
//! EXEC_COUNT_GE *extendedSarBothOffHeap 12
//! EXEC_COUNT_GE *extendedShrDynamicRightOffHeap 12
//! DEOPT_COUNT * 0
declare function print(arg: any): void;

// Representative off-heap coverage: all six operators and all five input shapes.
// The full operator/input matrix is covered by the on-heap normal-path cases.
//! METHOD extendedAndLeftOffHeap
//! HAS TypedArrayDoubleLoadElement
//! COUNT_GE F64ToI32Trunc 1
function extendedAndLeftOffHeap(a, b, i, k) {
    return a[i] & 255;
}

//! METHOD extendedAndRightOffHeap
//! HAS TypedArrayDoubleLoadElement
//! COUNT_GE F64ToI32Trunc 1
function extendedAndRightOffHeap(a, b, i, k) {
    return 305419896 & a[i];
}

//! METHOD extendedAndBothOffHeap
//! HAS TypedArrayDoubleLoadElement
//! COUNT_GE F64ToI32Trunc 2
function extendedAndBothOffHeap(a, b, i, k) {
    return a[i] & b[i];
}

//! METHOD extendedOrDynamicLeftOffHeap
//! HAS TypedArrayDoubleLoadElement
//! COUNT_GE F64ToI32Trunc 1
function extendedOrDynamicLeftOffHeap(a, b, i, k) {
    return k | a[i];
}

//! METHOD extendedXorDynamicRightOffHeap
//! HAS TypedArrayDoubleLoadElement
//! COUNT_GE F64ToI32Trunc 1
function extendedXorDynamicRightOffHeap(a, b, i, k) {
    return a[i] ^ k;
}

//! METHOD extendedShlRightOffHeap
//! HAS TypedArrayDoubleLoadElement
//! COUNT_GE F64ToI32Trunc 1
function extendedShlRightOffHeap(a, b, i, k) {
    return 305419896 << a[i];
}

//! METHOD extendedSarBothOffHeap
//! HAS TypedArrayDoubleLoadElement
//! COUNT_GE F64ToI32Trunc 2
function extendedSarBothOffHeap(a, b, i, k) {
    return a[i] >> b[i];
}

//! METHOD extendedShrDynamicRightOffHeap
//! HAS TypedArrayDoubleLoadElement
//! COUNT_GE F64ToI32Trunc 1
function extendedShrDynamicRightOffHeap(a, b, i, k) {
    return a[i] >>> k;
}

const a = new Uint32Array(5000); const b = new Uint32Array(5000);
const index = 4096;
a[index] = 0; a[index + 1] = 1; b[index] = 1; b[index + 1] = 0;
for (let n = 0; n < 1000; n++) {
    extendedAndLeftOffHeap(a, b, index + (n & 1), 305419896);
    extendedAndRightOffHeap(a, b, index + (n & 1), 305419896);
    extendedAndBothOffHeap(a, b, index + (n & 1), 305419896);
    extendedOrDynamicLeftOffHeap(a, b, index + (n & 1), 305419896);
    extendedXorDynamicRightOffHeap(a, b, index + (n & 1), 1437226410);
    extendedShlRightOffHeap(a, b, index + (n & 1), 305419896);
    extendedSarBothOffHeap(a, b, index + (n & 1), 305419896);
    extendedShrDynamicRightOffHeap(a, b, index + (n & 1), 16);
}
ArkTools.arkSteedCompileSync(extendedAndLeftOffHeap);
ArkTools.arkSteedCompileSync(extendedAndRightOffHeap);
ArkTools.arkSteedCompileSync(extendedAndBothOffHeap);
ArkTools.arkSteedCompileSync(extendedOrDynamicLeftOffHeap);
ArkTools.arkSteedCompileSync(extendedXorDynamicRightOffHeap);
ArkTools.arkSteedCompileSync(extendedShlRightOffHeap);
ArkTools.arkSteedCompileSync(extendedSarBothOffHeap);
ArkTools.arkSteedCompileSync(extendedShrDynamicRightOffHeap);
const values = [0, 1, 255, 2147483647, 2147483648, 2147483649, 4294967295, 305419896, 2193914387, 3909840005, 1832331801, 4294901761];
const counts = [1, 8, 16, 31, 33, 4294967295, 9, 24, 5, 17, 3, 7];
for (let n = 0; n < values.length; n++) {
    a[index] = values[n]; b[index] = counts[n];
    print(values[n] + ":" + [
        extendedAndLeftOffHeap(a, b, index, 305419896),
        extendedAndRightOffHeap(a, b, index, 305419896),
        extendedAndBothOffHeap(a, b, index, 305419896),
        extendedOrDynamicLeftOffHeap(a, b, index, 305419896),
        extendedXorDynamicRightOffHeap(a, b, index, 1437226410),
        extendedShlRightOffHeap(a, b, index, 305419896),
        extendedSarBothOffHeap(a, b, index, 305419896),
        extendedShrDynamicRightOffHeap(a, b, index, 16),
    ].join(","));
}
