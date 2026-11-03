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
//! EXEC_COUNT_GE *reuseUint32Number 12
//! EXEC_COUNT_GE *reuseUint32Twice 12
//! EXEC_COUNT_GE *reuseUint32AfterNumber 12
//! EXEC_COUNT_GE *extendedReuseRight 12
//! EXEC_COUNT_GE *extendedReuseBoth 12
//! EXEC_COUNT_GE *extendedReuseRightTwice 12
//! DEOPT_COUNT * 0
declare function print(arg: any): void;

// Truncation is local to each bitwise consumer, never an exact I32 alternative for x.
//! METHOD reuseUint32Number
//! HAS TypedArrayDoubleLoadElement
//! HAS F64ToI32Trunc
function reuseUint32Number(a, i) {
    const x = a[i]; const bits = x & 255; return x + 2147483648 + bits;
}

//! METHOD reuseUint32Twice
//! HAS TypedArrayDoubleLoadElement
//! HAS F64ToI32Trunc
function reuseUint32Twice(a, i) {
    const x = a[i]; const bits = x & 255; const shifted = x >> 8; return x + 2147483648 + bits + shifted;
}

//! METHOD reuseUint32AfterNumber
//! HAS TypedArrayDoubleLoadElement
//! HAS F64ToI32Trunc
function reuseUint32AfterNumber(a, i) {
    const x = a[i]; const number = x + 2147483648; const bits = x ^ 1437226410; return number + bits;
}

// The original unsigned Numbers remain available to other consumers.
//! METHOD extendedReuseRight
//! HAS TypedArrayDoubleLoadElement
//! COUNT_GE F64ToI32Trunc 1
function extendedReuseRight(a, b, i, k) {
    const x = a[i]; const bits = 255 & x; return x + 2147483648 + bits;
}

//! METHOD extendedReuseBoth
//! HAS TypedArrayDoubleLoadElement
//! COUNT_GE F64ToI32Trunc 2
function extendedReuseBoth(a, b, i, k) {
    const x = a[i]; const y = b[i]; const bits = x ^ y; return x + 2147483648 + y + bits;
}

//! METHOD extendedReuseRightTwice
//! HAS TypedArrayDoubleLoadElement
//! COUNT_GE F64ToI32Trunc 1
function extendedReuseRightTwice(a, b, i, k) {
    const x = a[i]; const bits = 255 & x; const shifted = k >> x; return x + 2147483648 + bits + shifted;
}

const a = new Uint32Array([0, 1]);
for (let n = 0; n < 1000; n++) {
    reuseUint32Number(a, n & 1);
    reuseUint32Twice(a, n & 1);
    reuseUint32AfterNumber(a, n & 1);
}
ArkTools.arkSteedCompileSync(reuseUint32Number);
ArkTools.arkSteedCompileSync(reuseUint32Twice);
ArkTools.arkSteedCompileSync(reuseUint32AfterNumber);
const values = [0, 1, 255, 2147483647, 2147483648, 2147483649, 4294967295, 305419896, 2193914387, 3909840005, 1832331801, 4294901761];
for (let i = 0; i < values.length; i++) {
    a[0] = values[i];
    print(values[i] + ":" + [reuseUint32Number(a, 0), reuseUint32Twice(a, 0), reuseUint32AfterNumber(a, 0)].join(","));
}

const b = new Uint32Array(2);
const index = 0;
a[index] = 0; a[index + 1] = 1; b[index] = 1; b[index + 1] = 0;
for (let n = 0; n < 1000; n++) {
    extendedReuseRight(a, b, n & 1, 305419896);
    extendedReuseBoth(a, b, n & 1, 305419896);
    extendedReuseRightTwice(a, b, n & 1, 305419896);
}
ArkTools.arkSteedCompileSync(extendedReuseRight);
ArkTools.arkSteedCompileSync(extendedReuseBoth);
ArkTools.arkSteedCompileSync(extendedReuseRightTwice);
for (let n = 0; n < values.length; n++) {
    a[0] = values[n]; b[0] = values[(n + 3) % values.length];
    print(values[n] + ":" + [extendedReuseRight(a, b, 0, 305419896), extendedReuseBoth(a, b, 0, 305419896), extendedReuseRightTwice(a, b, 0, 305419896)].join(","));
}
