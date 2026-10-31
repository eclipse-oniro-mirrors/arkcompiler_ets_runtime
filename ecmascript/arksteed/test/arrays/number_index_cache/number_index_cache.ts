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

//! PARAMS --compiler-jit-hotness-threshold=60000

//! METHOD cachedRepeatedRead
//! COUNT BranchIfInt64Compare 0
//! COUNT F64ToI32Trunc 1
//! COUNT LoadTaggedElement 2
//! COUNT_GE DeoptIfInt32Condition 2
//! COUNT_GE DeoptIfTaggedCondition 2
//! COMPILE_COUNT cachedRepeatedRead 1
//! EXEC_COUNT_GE *cachedRepeatedRead 2
function cachedRepeatedRead(a, b, key) {
    const x = a[key];
    const y = b[key];
    const z = a[key];
    return x + y + z;
}

//! METHOD cachedStoreThenRead
//! HAS_NOT BranchIfInt64Compare
//! COUNT F64ToI32Trunc 1
//! HAS StoreTaggedElement
//! COMPILE_COUNT cachedStoreThenRead 1
//! EXEC_COUNT_GE *cachedStoreThenRead 1
function cachedStoreThenRead(a, b, key, value) {
    a[key] = value;
    const x = a[key];
    const y = b[key];
    return x + y;
}

//! METHOD cachedChangedKey
//! COUNT BranchIfInt64Compare 0
//! COUNT F64ToI32Trunc 2
//! COMPILE_COUNT cachedChangedKey 1
//! EXEC_COUNT_GE *cachedChangedKey 2
function cachedChangedKey(a, key, next) {
    const x = a[key];
    key = next;
    return x + a[key];
}

//! METHOD cachedBranchMerge
//! COUNT BranchIfInt64Compare 0
//! COUNT F64ToI32Trunc 2
//! COMPILE_COUNT cachedBranchMerge 1
//! EXEC_COUNT_GE *cachedBranchMerge 2
function cachedBranchMerge(a, key, takeFirst) {
    let x = 0;
    if (takeFirst) {
        x = a[key];
    }
    return x + a[key];
}

//! METHOD cachedLoopKey
//! HAS Phi
//! HAS LoadTaggedElement
//! COMPILE_COUNT cachedLoopKey 1
//! EXEC_COUNT_GE *cachedLoopKey 2
function cachedLoopKey(a, left, right) {
    let key = left;
    let sum = 0;
    for (let i = 0; i < 4; i++) {
        const x = a[key];
        const y = a[key];
        sum += x + y;
        key = (i & 1) === 0 ? right : left;
    }
    return sum;
}

//! METHOD cachedSignedZero
//! COUNT BranchIfInt64Compare 0
//! COUNT F64ToI32Trunc 1
//! COMPILE_COUNT cachedSignedZero 1
//! EXEC_COUNT_GE *cachedSignedZero 1
function cachedSignedZero(a, key) {
    const x = a[key];
    const y = a[key];
    return (x + y) + ':' + 1 / (key + key);
}

const values = [10, 20, 30];
const other = [1, 2, 3];
const writable = [10, 20, 30];
const doubleKeys = new Float64Array([1, 2, -0]);
for (let i = 0; i < 64; i++) {
    cachedRepeatedRead(values, other, 1);
    cachedStoreThenRead(writable, other, 1, 50);
    cachedChangedKey(values, 1, 2);
    cachedBranchMerge(values, 1, (i & 1) === 0);
    cachedLoopKey(values, 0, 2);
    cachedSignedZero(values, 0);
}
ArkTools.arkSteedCompileSync(cachedRepeatedRead);
print(cachedRepeatedRead(values, other, 1));
print(cachedRepeatedRead(values, other, doubleKeys[1]));
print(ArkTools.arkSteedIsCompiled(cachedRepeatedRead));
ArkTools.arkSteedCompileSync(cachedStoreThenRead);
print(cachedStoreThenRead(writable, other, doubleKeys[1], 70));
print(writable[2]);
print(ArkTools.arkSteedIsCompiled(cachedStoreThenRead));
ArkTools.arkSteedCompileSync(cachedChangedKey);
print(cachedChangedKey(values, 1, doubleKeys[1]));
print(cachedChangedKey(values, doubleKeys[1], 0));
print(ArkTools.arkSteedIsCompiled(cachedChangedKey));
ArkTools.arkSteedCompileSync(cachedBranchMerge);
print(cachedBranchMerge(values, doubleKeys[1], false));
print(cachedBranchMerge(values, doubleKeys[0], true));
print(ArkTools.arkSteedIsCompiled(cachedBranchMerge));
ArkTools.arkSteedCompileSync(cachedLoopKey);
print(cachedLoopKey(values, 0, 2));
print(cachedLoopKey(values, doubleKeys[0], doubleKeys[1]));
print(ArkTools.arkSteedIsCompiled(cachedLoopKey));
ArkTools.arkSteedCompileSync(cachedSignedZero);
print(ArkTools.arkSteedIsCompiled(cachedSignedZero));
print(cachedSignedZero(values, doubleKeys[2]));
print('OK');
