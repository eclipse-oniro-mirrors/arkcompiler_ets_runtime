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

//! PARAMS --compiler-jit-hotness-threshold=60000
//! METHOD twoStores
//! COUNT StoreTaggedElement 2
//! COUNT LoadInt32Field 1
//! COUNT LoadTaggedField 1
//! METHOD peelArrayStores
//! COUNT JumpLoop 1
//! HAS StoreTaggedElement
//! COUNT LoadInt32Field 1
//! HAS_NOT EnsureWritableElements
//! COUNT LoadTaggedField 1
//! METHOD barrierStores
//! COUNT StoreTaggedElementWithBarrier 2
//! COUNT LoadInt32Field 1
//! COUNT LoadTaggedField 1
//! METHOD aliasedElement
//! COUNT LoadTaggedElement 2
//! HAS StoreTaggedElement

function twoStores(arr, i, j) {
    arr[i] = 31;
    arr[j] = 37;
}

function peelArrayStores(arr, n) {
    for (let i = 0; i < n; ++i) {
        arr[i & 3] = i;
    }
}

function barrierStores(arr, i, j, value) {
    arr[i] = value;
    arr[j] = value;
}

function aliasedElement(arr, alias, i) {
    const before = alias[i];
    arr[i] = 73;
    return before * 100 + alias[i];
}

const arr = [1, 2, 3, 4];
arr[0] = 1; // Materialize the literal's writable backing before collecting feedback.
const object = {value: 9};
const objects = [object, object, object, object];
for (let i = 0; i < 16; ++i) {
    twoStores(arr, 0, 1);
    peelArrayStores(arr, 8);
    barrierStores(objects, 0, 1, object);
    aliasedElement(arr, arr, 0);
}
ArkTools.arkSteedCompileSync(twoStores);
ArkTools.arkSteedCompileSync(peelArrayStores);
ArkTools.arkSteedCompileSync(barrierStores);
ArkTools.arkSteedCompileSync(aliasedElement);

twoStores(arr, 2, 3);
print(arr[2] === 31 && arr[3] === 37 && arr.length === 4 ? "PASS" : "FAIL");
arr[0] = 6;
print(aliasedElement(arr, arr, 0) === 673 ? "PASS" : "FAIL");

let ok = true;
for (const n of [0, 1, 2, 8]) {
    const input = [10, 11, 12, 13];
    input[0] = 10;
    const expected = [10, 11, 12, 13];
    for (let i = 0; i < n; ++i) {
        expected[i & 3] = i;
    }
    peelArrayStores(input, n);
    ok = ok && input.length === 4 && input.every((v, i) => v === expected[i]);
}
print(ok ? "PASS" : "FAIL");

ArkTools.forceFullGC();
const young = {value: 19};
barrierStores(objects, 2, 3, young);
ArkTools.forceFullGC();
print(objects[2] === young && objects[3] === young && objects[2].value === 19 ? "PASS" : "FAIL");
