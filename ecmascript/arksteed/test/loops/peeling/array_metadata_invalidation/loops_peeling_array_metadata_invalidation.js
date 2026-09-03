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
//! METHOD storesAcrossCall
//! HAS Call
//! COUNT LoadInt32Field 2
//! COUNT StoreTaggedElement 2
//! METHOD storesAcrossBranch
//! HAS CallKnownJSFunction
//! HAS StoreTaggedElement
//! METHOD loopWithMutation
//! HAS Call
//! HAS JumpLoop

function storesAcrossCall(arr, i, mutate) {
    arr[i] = 11;
    mutate(arr);
    arr[i] = 29;
}

function storesAcrossBranch(arr, i, mutate, change) {
    arr[i] = 11;
    if (change) {
        mutate(arr);
    }
    arr[i] = 29;
}

function loopWithMutation(arr, n, mutate) {
    for (let i = 0; i < n; ++i) {
        arr[i & 3] = i;
        mutate(arr, i);
    }
}

function grow(arr) { arr[100] = 101; }
function unchanged(arr) {}
function shrink(arr) { arr.length = 0; }

for (let i = 0; i < 16; ++i) {
    const arr = [1, 2, 3, 4];
    storesAcrossCall(arr, 2, i & 1 ? grow : unchanged);
    storesAcrossBranch(arr, 2, grow, (i & 1) !== 0);
    loopWithMutation(arr, 4, unchanged);
}
ArkTools.arkSteedCompileSync(storesAcrossCall);
ArkTools.arkSteedCompileSync(storesAcrossBranch);
ArkTools.arkSteedCompileSync(loopWithMutation);

let arr = [1, 2, 3, 4];
arr[0] = 1;
storesAcrossCall(arr, 2, grow);
print(arr.length === 101 && arr[2] === 29 && arr[100] === 101 ? "PASS" : "FAIL");
arr = [1, 2, 3, 4];
arr[0] = 1;
storesAcrossBranch(arr, 2, grow, true);
print(arr.length === 101 && arr[2] === 29 && arr[100] === 101 ? "PASS" : "FAIL");
arr = [1, 2, 3, 4];
arr[0] = 1;
storesAcrossCall(arr, 2, shrink);
print(arr.length === 3 && arr[0] === undefined && arr[2] === 29 ? "PASS" : "FAIL");
arr = [1, 2, 3, 4];
arr[0] = 1;
storesAcrossBranch(arr, 2, shrink, true);
print(arr.length === 3 && arr[0] === undefined && arr[2] === 29 ? "PASS" : "FAIL");

arr = [1, 2, 3, 4];
arr[0] = 1;
loopWithMutation(arr, 8, (a, i) => { if (i === 0) a.length = 0; });
print(arr.length === 4 && arr[0] === 4 && arr[3] === 7 ? "PASS" : "FAIL");

arr = [1, 2, 3, 4];
arr[0] = 1;
let caught = false;
try {
    storesAcrossCall(arr, 2, a => { a.length = 0; throw new Error("stop"); });
} catch (error) {
    caught = error.message === "stop";
}
print(caught && arr.length === 0 ? "PASS" : "FAIL");
