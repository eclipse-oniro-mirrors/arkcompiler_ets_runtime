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
//! EXEC_COUNT_GE *maskUint32KnownInt 12
//! EXEC_COUNT_GE *shlUint32KnownInt 12
//! EXEC_COUNT_GE *sarUint32KnownInt 12
//! DEOPT_COUNT * 0
declare function print(arg: any): void;

// The right operand has an Int type fact but is not a literal constant.
//! METHOD maskUint32KnownInt
//! HAS TypedArrayDoubleLoadElement
//! HAS F64ToI32Trunc
function maskUint32KnownInt(a, i, selector) {
    const mask = selector | 0; return a[i] & mask;
}

//! METHOD shlUint32KnownInt
//! HAS TypedArrayDoubleLoadElement
//! HAS F64ToI32Trunc
function shlUint32KnownInt(a, i, count) {
    const shift = count & 31; return a[i] << shift;
}

//! METHOD sarUint32KnownInt
//! HAS TypedArrayDoubleLoadElement
//! HAS F64ToI32Trunc
function sarUint32KnownInt(a, i, count) {
    const shift = count & 31; return a[i] >> shift;
}

const a = new Uint32Array([0, 1]);
for (let n = 0; n < 1000; n++) {
    maskUint32KnownInt(a, n & 1, 1);
    shlUint32KnownInt(a, n & 1, 1);
    sarUint32KnownInt(a, n & 1, 1);
}
ArkTools.arkSteedCompileSync(maskUint32KnownInt);
ArkTools.arkSteedCompileSync(shlUint32KnownInt);
ArkTools.arkSteedCompileSync(sarUint32KnownInt);
const values = [0, 1, 255, 2147483647, 2147483648, 2147483649, 4294967295, 305419896, 2193914387, 3909840005, 1832331801, 4294901761];
const counts = [-1, 0, 1, 8, 16, 31, 32, 33, -1, 31, 32, 33];
for (let i = 0; i < values.length; i++) {
    a[0] = values[i];
    const k = counts[i];
    print(values[i] + ":" + k + ":" + [maskUint32KnownInt(a, 0, k), shlUint32KnownInt(a, 0, k), sarUint32KnownInt(a, 0, k)].join(","));
}
