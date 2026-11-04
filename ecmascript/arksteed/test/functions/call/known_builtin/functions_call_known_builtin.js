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

//! PARAMS --compiler-jit-hotness-threshold=6000
//! METHOD appendOne
//! COUNT CallKnownBuiltin 1
//! COUNT DeoptIfReferenceNotEqual 2
//! HAS_NOT BranchIfReferenceEqual
//! HAS_NOT Call

//! METHOD appendFour
//! COUNT CallKnownBuiltin 1
//! COUNT DeoptIfReferenceNotEqual 2
//! HAS_NOT BranchIfReferenceEqual
//! HAS_NOT Call

function appendOne(array, value) {
    return array.push(value);
}

function appendFour(array, first, second, third, fourth) {
    return array.push(first, second, third, fourth);
}

for (let index = 0; index < 200; index++) {
    appendOne([], index);
    appendFour([], index, index + 1, index + 2, index + 3);
}

ArkTools.arkSteedCompileSync(appendOne);
ArkTools.arkSteedCompileSync(appendFour);

print(ArkTools.arkSteedIsCompiled(appendOne));
print(ArkTools.arkSteedIsCompiled(appendFour));

const oneValue = [];
print(appendOne(oneValue, 7));
print(oneValue.join(":"));

const fourValues = [];
print(appendFour(fourValues, 1, 2, 3, 4));
print(fourValues.join(":"));

let replacementCalls = 0;
const originalPush = Array.prototype.push;
Array.prototype.push = function (...values) {
    replacementCalls++;
    let result = 1000;
    for (let index = 0; index < values.length; index++) {
        result += values[index];
    }
    return result;
};
print(appendOne([], 9));
print(appendFour([], 1, 2, 3, 4));
print(replacementCalls);
print(ArkTools.arkSteedIsCompiled(appendOne));
print(ArkTools.arkSteedIsCompiled(appendFour));
Array.prototype.push = originalPush;
