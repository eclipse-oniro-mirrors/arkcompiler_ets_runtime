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
//! METHOD callKnownTarget
//! HAS Call
//! HAS_NOT CallKnownJSFunction
//! HAS_NOT BranchIfReferenceEqual
//! HAS_NOT DeoptIfReferenceNotEqual

function addWithDefault(left, right = 7) {
    if (left < 0) {
        throw new Error("negative");
    }
    let result = left;
    for (let index = 0; index < 1; index++) {
        result += right;
    }
    return result;
}

function callKnownTarget(value) {
    try {
        return addWithDefault(value);
    } catch (error) {
        return error.message;
    }
}

for (let index = 0; index < 200; index++) {
    callKnownTarget(index);
}

ArkTools.arkSteedCompileSync(callKnownTarget);
print(ArkTools.arkSteedIsCompiled(callKnownTarget));
print(callKnownTarget(5));
print(callKnownTarget(-1));

ArkTools.arkSteedCompileSync(addWithDefault);
print(callKnownTarget(8));
print(callKnownTarget(-2));
