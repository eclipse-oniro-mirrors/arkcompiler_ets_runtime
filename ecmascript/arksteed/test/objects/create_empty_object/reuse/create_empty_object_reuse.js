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
//! METHOD makeTwoEmptyObjects
//! COUNT_GE AllocateInYoung 2
//! COUNT InitializeEmptyObject 2
//! HAS_NOT CallCommonStub NewJSObject
//! HAS_NOT CallRuntime CreateEmptyObject

function makeTwoEmptyObjects() {
    const first = {};
    const second = {};
    return [first, second];
}

for (let i = 0; i < 20000; i++) {
    makeTwoEmptyObjects();
}

ArkTools.arkSteedCompileSync(makeTwoEmptyObjects);
ArkTools.waitJitCompileFinish(makeTwoEmptyObjects);

let ok = ArkTools.isAOTCompiled(makeTwoEmptyObjects);
const pair = makeTwoEmptyObjects();
const first = pair[0];
const second = pair[1];

ok = ok && first !== second;
ok = ok && Object.getPrototypeOf(first) === Object.prototype;
ok = ok && Object.getPrototypeOf(second) === Object.prototype;
ok = ok && Object.keys(first).length === 0;
ok = ok && Object.keys(second).length === 0;

first.marker = 11;
ok = ok && first.marker === 11;
ok = ok && second.marker === undefined;
ok = ok && Object.keys(second).length === 0;

second.marker = 22;
ok = ok && first.marker === 11;
ok = ok && second.marker === 22;

print(ok ? "PASS" : "FAIL");
