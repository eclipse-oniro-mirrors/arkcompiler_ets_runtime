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
//! EXEC_COUNT_GE *maskUint32 6
//! EXEC_COUNT_GE *signedShiftUint32 6
//! EXEC_COUNT_GE *unsignedShiftUint32 6
//! EXEC_COUNT_GE *orUint32 6
//! EXEC_COUNT_GE *xorUint32 6
//! EXEC_COUNT_GE *leftShiftUint32 6
//! EXEC_COUNT_GE *unsignedIdentityUint32 1
//! EXEC_COUNT_GE *maskOffHeapUint32 1
declare function print(arg: any): string;

// Warm with tagged Int values so that the bitwise feedback is INT. The JIT
// Uint32Array load still produces Float64, including for these small values.
//! METHOD maskUint32
//! HAS TypedArrayDoubleLoadElement
//! HAS F64ToI32Trunc
function maskUint32(values, index) {
    return values[index] & 0xff;
}

//! METHOD signedShiftUint32
//! HAS F64ToI32Trunc
function signedShiftUint32(values, index) {
    return values[index] >> 8;
}

//! METHOD unsignedShiftUint32
//! HAS F64ToI32Trunc
function unsignedShiftUint32(values, index) {
    return values[index] >>> 16;
}

//! METHOD orUint32
//! HAS F64ToI32Trunc
function orUint32(values, index) {
    return values[index] | 0;
}

//! METHOD xorUint32
//! HAS F64ToI32Trunc
function xorUint32(values, index) {
    return values[index] ^ 0x55aa55aa;
}

//! METHOD leftShiftUint32
//! HAS F64ToI32Trunc
function leftShiftUint32(values, index) {
    return values[index] << 1;
}

//! METHOD unsignedIdentityUint32
//! HAS F64ToI32Trunc
function unsignedIdentityUint32(values, index) {
    return values[index] >>> 0;
}

const values = new Uint32Array([0, 1, 0x7fffffff, 0x80000000, 0xffffffff, 0x12345678]);
for (let i = 0; i < 1000; i++) {
    const index = i & 1;
    maskUint32(values, index);
    signedShiftUint32(values, index);
    unsignedShiftUint32(values, index);
    orUint32(values, index);
    xorUint32(values, index);
    leftShiftUint32(values, index);
    unsignedIdentityUint32(values, index);
}

ArkTools.arkSteedCompileSync(maskUint32);
ArkTools.arkSteedCompileSync(signedShiftUint32);
ArkTools.arkSteedCompileSync(unsignedShiftUint32);
ArkTools.arkSteedCompileSync(orUint32);
ArkTools.arkSteedCompileSync(xorUint32);
ArkTools.arkSteedCompileSync(leftShiftUint32);
ArkTools.arkSteedCompileSync(unsignedIdentityUint32);
for (let i = 0; i < values.length; i++) {
    print(values[i] + ':' + [maskUint32(values, i), signedShiftUint32(values, i),
        unsignedShiftUint32(values, i), orUint32(values, i), xorUint32(values, i),
        leftShiftUint32(values, i), unsignedIdentityUint32(values, i)].join(','));
}

// Receiver and bounds guards must still handle changes after compilation.
print('out-of-bounds=' + maskUint32(values, values.length));
print('negative-index=' + maskUint32(values, -1));
print('float64=' + maskUint32(new Float64Array([3.75, -2.75, NaN, Infinity]), 1));

//! METHOD maskOffHeapUint32
//! HAS TypedArrayDoubleLoadElement
//! HAS F64ToI32Trunc
function maskOffHeapUint32(array, index) {
    return array[index] & 0xff;
}

const offHeap = new Uint32Array(5000);
offHeap[4096] = 0;
offHeap[4097] = 1;
for (let i = 0; i < 1000; i++) {
    maskOffHeapUint32(offHeap, 4096 + (i & 1));
}
ArkTools.arkSteedCompileSync(maskOffHeapUint32);
offHeap[4097] = 0xffffffff;
print('off-heap=' + maskOffHeapUint32(offHeap, 4097));
