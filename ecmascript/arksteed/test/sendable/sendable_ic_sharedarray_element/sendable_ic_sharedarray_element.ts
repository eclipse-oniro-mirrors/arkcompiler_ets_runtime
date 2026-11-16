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

// Test: JSSharedArray element load IC — in-bounds element reads hit the
// S_ELEMENT handler after distillation, and the value stays correct after
// ArkSteed compilation. Out-of-bounds reads deopt to the slow path (undefined).
//! METHOD loadElement
//! HAS LoadTaggedField
//! COMPILE_COUNT_GE *loadElement* 1

let sarr = new SendableArray(1, 2, 3, 4, 5, 6, 7, 8);

function loadElement(arr) {
    let r;
    for (let i = 0; i < 200; i++) { r = arr[i & 7]; }
    return r;
}

print("before compile: " + loadElement(sarr));
ArkTools.arkSteedCompileSync(loadElement);
ArkTools.waitJitCompileFinish(loadElement);
print(ArkTools.isAOTCompiled(loadElement));
print("after compile: " + loadElement(sarr));
print("oob after compile: " + (sarr[100] === undefined));
