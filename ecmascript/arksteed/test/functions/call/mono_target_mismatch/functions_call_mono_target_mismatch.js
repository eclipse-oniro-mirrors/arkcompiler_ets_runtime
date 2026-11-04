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
//! METHOD callProfiledTarget
//! HAS CallKnownJSFunction
//! COUNT DeoptIfReferenceNotEqual 1
//! HAS_NOT Call

function addOne(value) {
    let result = value;
    for (let index = 0; index < 1; index++) {
        result++;
    }
    return result;
}

function addTen(value) {
    let result = value;
    for (let index = 0; index < 1; index++) {
        result += 10;
    }
    return result;
}

let target = addOne;

function callProfiledTarget(value) {
    return target(value);
}

for (let index = 0; index < 200; index++) {
    callProfiledTarget(index);
}

ArkTools.arkSteedCompileSync(callProfiledTarget);
print(ArkTools.arkSteedIsCompiled(callProfiledTarget));
print(callProfiledTarget(3));

target = addTen;
print(callProfiledTarget(3));
print(ArkTools.arkSteedIsCompiled(callProfiledTarget));
