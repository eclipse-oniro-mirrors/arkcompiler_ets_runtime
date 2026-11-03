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
//! EXEC_COUNT_GE *normalMaskUint32 12
//! EXEC_COUNT_GE *normalOrUint32 12
//! EXEC_COUNT_GE *normalXorUint32 12
//! EXEC_COUNT_GE *normalShlUint32 12
//! EXEC_COUNT_GE *normalSarUint32 12
//! EXEC_COUNT_GE *normalShrUint32 12
//! DEOPT_COUNT * 0
declare function print(arg: any): void;

// Small-value INT feedback must not cause deopt even for 0 and 1.
// Every post-compilation call must stay in the generated code.
//! METHOD normalMaskUint32
//! HAS TypedArrayDoubleLoadElement
//! HAS F64ToI32Trunc
function normalMaskUint32(a, i) {
    return a[i] & 255;
}

//! METHOD normalOrUint32
//! HAS TypedArrayDoubleLoadElement
//! HAS F64ToI32Trunc
function normalOrUint32(a, i) {
    return a[i] | 0;
}

//! METHOD normalXorUint32
//! HAS TypedArrayDoubleLoadElement
//! HAS F64ToI32Trunc
function normalXorUint32(a, i) {
    return a[i] ^ 1437226410;
}

//! METHOD normalShlUint32
//! HAS TypedArrayDoubleLoadElement
//! HAS F64ToI32Trunc
function normalShlUint32(a, i) {
    return a[i] << 1;
}

//! METHOD normalSarUint32
//! HAS TypedArrayDoubleLoadElement
//! HAS F64ToI32Trunc
function normalSarUint32(a, i) {
    return a[i] >> 8;
}

//! METHOD normalShrUint32
//! HAS TypedArrayDoubleLoadElement
//! HAS F64ToI32Trunc
function normalShrUint32(a, i) {
    return a[i] >>> 16;
}

const a = new Uint32Array([0, 1]);
for (let n = 0; n < 1000; n++) {
    normalMaskUint32(a, n & 1);
    normalOrUint32(a, n & 1);
    normalXorUint32(a, n & 1);
    normalShlUint32(a, n & 1);
    normalSarUint32(a, n & 1);
    normalShrUint32(a, n & 1);
}
ArkTools.arkSteedCompileSync(normalMaskUint32);
ArkTools.arkSteedCompileSync(normalOrUint32);
ArkTools.arkSteedCompileSync(normalXorUint32);
ArkTools.arkSteedCompileSync(normalShlUint32);
ArkTools.arkSteedCompileSync(normalSarUint32);
ArkTools.arkSteedCompileSync(normalShrUint32);
const values = [0, 1, 255, 2147483647, 2147483648, 2147483649, 4294967295, 305419896, 2193914387, 3909840005, 1832331801, 4294901761];
for (let i = 0; i < values.length; i++) {
    a[0] = values[i];
    print(values[i] + ":" + [normalMaskUint32(a, 0), normalOrUint32(a, 0), normalXorUint32(a, 0), normalShlUint32(a, 0), normalSarUint32(a, 0), normalShrUint32(a, 0)].join(","));
}
