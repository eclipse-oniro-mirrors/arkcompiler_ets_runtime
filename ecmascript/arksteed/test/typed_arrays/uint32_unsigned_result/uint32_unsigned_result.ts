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
//! EXEC_COUNT_GE *uint32UnsignedIdentityResult 1
//! EXEC_COUNT_GE *extendedUnsignedRightResult 1
//! EXEC_COUNT_GE *extendedUnsignedBothResult 1
//! DEOPT_COUNT * 3
declare function print(arg: any): void;

// High unsigned results require the existing result-side representation check.
//! METHOD uint32UnsignedIdentityResult
//! HAS TypedArrayDoubleLoadElement
//! HAS F64ToI32Trunc
//! HAS CheckedNonNegativeI32ToTaggedInt
function uint32UnsignedIdentityResult(a, i) {
    return a[i] >>> 0;
}

//! METHOD extendedUnsignedRightResult
//! HAS TypedArrayDoubleLoadElement
//! COUNT_GE F64ToI32Trunc 1
//! HAS CheckedNonNegativeI32ToTaggedInt
function extendedUnsignedRightResult(a, b, i, k) {
    return k >>> a[i];
}

//! METHOD extendedUnsignedBothResult
//! HAS TypedArrayDoubleLoadElement
//! COUNT_GE F64ToI32Trunc 2
//! HAS CheckedNonNegativeI32ToTaggedInt
function extendedUnsignedBothResult(a, b, i, k) {
    return a[i] >>> b[i];
}

const a = new Uint32Array([0, 1]);
for (let n = 0; n < 1000; n++) {
    uint32UnsignedIdentityResult(a, n & 1);
}
ArkTools.arkSteedCompileSync(uint32UnsignedIdentityResult);
a[0] = 4294967295;
print(uint32UnsignedIdentityResult(a, 0));

const b = new Uint32Array(2);
const index = 0;
a[index] = 0; a[index + 1] = 1; b[index] = 1; b[index + 1] = 0;
for (let n = 0; n < 1000; n++) {
    extendedUnsignedRightResult(a,b,n&1,1);
    extendedUnsignedBothResult(a,b,n&1,1);
}
ArkTools.arkSteedCompileSync(extendedUnsignedRightResult);
ArkTools.arkSteedCompileSync(extendedUnsignedBothResult);
a[0] = 0;
print(extendedUnsignedRightResult(a,b,0,-1));
a[0] = 4294967295; b[0] = 0;
print(extendedUnsignedBothResult(a,b,0,1));
