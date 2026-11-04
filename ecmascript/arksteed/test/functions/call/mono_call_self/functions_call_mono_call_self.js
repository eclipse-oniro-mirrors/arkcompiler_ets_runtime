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
//! METHOD recursiveSum
//! HAS CallSelf
//! COUNT DeoptIfReferenceNotEqual 1
//! HAS_NOT BranchIfReferenceEqual
//! HAS_NOT Call
//! HAS_NOT CallKnownJSFunction

//! METHOD recursiveMissingArg
//! HAS CallSelf
//! COUNT DeoptIfReferenceNotEqual 1
//! HAS_NOT BranchIfReferenceEqual
//! HAS_NOT Call
//! HAS_NOT CallKnownJSFunction

function recursiveSum(value, total) {
    if (value === 0) {
        return total;
    }
    return recursiveSum(value - 1, total + value);
}

function recursiveMissingArg(value, marker) {
    if (value === 0) {
        return marker === undefined;
    }
    return recursiveMissingArg(value - 1);
}

for (let index = 0; index < 200; index++) {
    recursiveSum(5, 0);
    recursiveMissingArg(3, index);
}

ArkTools.arkSteedCompileSync(recursiveSum);
ArkTools.arkSteedCompileSync(recursiveMissingArg);

print(ArkTools.arkSteedIsCompiled(recursiveSum));
print(ArkTools.arkSteedIsCompiled(recursiveMissingArg));
print(recursiveSum(10, 0));
print(recursiveMissingArg(5, 1));

const compiledRecursiveSum = recursiveSum;
recursiveSum = function (value, total) {
    return 1000 + value + total;
};
print(compiledRecursiveSum(2, 3));
print(ArkTools.arkSteedIsCompiled(compiledRecursiveSum));
