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
//! METHOD exactTransitionFacts
//! HAS TransitionHClassWithBarrier
//! HAS_NOT CallRuntime UpdateHClassForElementsKind
//! COUNT LoadTaggedField 2
//! METHOD runtimeTransitionFacts
//! HAS CallRuntime UpdateHClassForElementsKind
//! COUNT LoadTaggedField 2
//! METHOD transitionAliases
//! HAS CallRuntime UpdateHClassForElementsKind
//! HAS_NOT EnsureWritableElements
//! COUNT LoadTaggedField 4
//! COUNT_GE LoadTaggedElement 4
//! METHOD loopTransitionFacts
//! COUNT CallRuntime UpdateHClassForElementsKind 1
//! COUNT JumpLoop 1
//! METHOD loopAliasTransitions
//! HAS StoreTaggedElement
//! COUNT_GE LoadTaggedElement 3
//! METHOD loopMutateArray
//! HAS StoreTaggedElement
//! COUNT_GE LoadInt32Field 2

function exactTransitionFacts(holder, arr, i, value) {
    const before = holder.value;
    arr[i] = value;
    return before + holder.value;
}

function runtimeTransitionFacts(holder, arr, i, value) {
    const before = holder.value;
    arr[i] = value;
    return before + holder.value;
}

function transitionAliases(arr, alias, other, i, value) {
    const before = alias[i];
    const otherBefore = other[i];
    arr[i] = value;
    return before + alias[i] + otherBefore + other[i];
}

function loopTransitionFacts(holder, arr, n, value) {
    let result = 0;
    for (let i = 0; i < n; ++i) {
        result += holder.value;
        arr[i & 1] = value;
        result += holder.value;
    }
    return result;
}

function loopAliasTransitions(arr, alias, other, n, value) {
    let result = 0;
    for (let i = 0; i < n; ++i) {
        const index = i & 1;
        const before = alias[index];
        arr[index] = value;
        result += before + alias[index] + other[0];
    }
    return result;
}

function loopMutateArray(arr, n, callback) {
    let result = 0;
    for (let i = 0; i < n; ++i) {
        const before = arr.length;
        arr[0] = i + 0.5;
        callback(arr, i);
        result += before + arr.length;
    }
    return result;
}

function noOp() {}

const holder = {value: 9};
const ints = [1, 2, 3];
const doubles = [1.5, 2.5, 3.5];
const other = [7.5, 8.5, 9.5]; // Already at the transition target, not one of its sources.
for (let i = 0; i < 20; ++i) {
    exactTransitionFacts(holder, [1, 2, 3], 1, i + 0.5);
    const arr = i & 1 ? doubles : ints;
    const value = i & 1 ? i + 0.5 : i;
    runtimeTransitionFacts(holder, arr, 1, value);
    transitionAliases(arr, arr, other, 1, value);
    loopTransitionFacts(holder, arr, 8, value);
    loopAliasTransitions(arr, arr, other, 4, value);
    loopMutateArray([1.5, 2.5, 3.5], 4, noOp);
}
ArkTools.arkSteedCompileSync(exactTransitionFacts);
ArkTools.arkSteedCompileSync(runtimeTransitionFacts);
ArkTools.arkSteedCompileSync(transitionAliases);
ArkTools.arkSteedCompileSync(loopTransitionFacts);
ArkTools.arkSteedCompileSync(loopAliasTransitions);
ArkTools.arkSteedCompileSync(loopMutateArray);

let arr = [1, 2, 3];
arr[0] = 1;
let result = exactTransitionFacts(holder, arr, 1, 41.5);
print(result === 18 && arr[1] === 41.5 &&
      ArkTools.getElementsKind(arr) === ArkTools.getElementsKind(doubles) ? "PASS" : "FAIL");
arr = [1, 2, 3];
arr[0] = 1;
result = runtimeTransitionFacts(holder, arr, 1, 42.5);
print(result === 18 && arr[1] === 42.5 &&
      ArkTools.getElementsKind(arr) === ArkTools.getElementsKind(doubles) ? "PASS" : "FAIL");
arr = [1, 2, 3];
arr[0] = 1;
result = transitionAliases(arr, arr, other, 1, 43.5);
print(result === 62.5 && arr[1] === 43.5 && other[1] === 8.5 ? "PASS" : "FAIL");

let ok = true;
for (const n of [0, 1, 2, 8]) {
    arr = [1, 2, 3];
    arr[0] = 1;
    result = loopTransitionFacts(holder, arr, n, 45.5);
    ok = ok && result === 18 * n && arr.length === 3;
    ok = ok && arr[0] === (n > 0 ? 45.5 : 1) && arr[1] === (n > 1 ? 45.5 : 2);
}
print(ok ? "PASS" : "FAIL");

arr = [1, 2, 3];
result = loopAliasTransitions(arr, arr, other, 4, 0.5);
print(result === 36 && arr[0] === 0.5 && arr[1] === 0.5 && other[0] === 7.5 ? "PASS" : "FAIL");

arr = [1.5, 2.5, 3.5];
result = loopMutateArray(arr, 4, (array, i) => {
    if (i === 1) {
        array.length = 1;
    } else if (i === 2) {
        array.push(3);
    }
});
print(result === 17 && arr.length === 2 && arr[0] === 3.5 ? "PASS" : "FAIL");
