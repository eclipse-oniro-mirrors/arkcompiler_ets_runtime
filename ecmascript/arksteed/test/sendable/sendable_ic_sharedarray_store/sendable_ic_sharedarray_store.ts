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

// Test: JSSharedArray element store IC — in-bounds element writes hit the
// S_ELEMENT handler after distillation. The written value is visible after
// ArkSteed compilation. Value contract violations deopt to the slow path
// (TypeError). Out-of-bounds writes deopt to the slow path (BusinessError).
//! METHOD storeElement
//! COMPILE_COUNT_GE *storeElement* 1

let sarr = new SendableArray(1, 2, 3, 4, 5, 6, 7, 8);

function storeElement(arr, v) {
    for (let i = 0; i < 200; i++) { arr[i & 7] = v; }
}

storeElement(sarr, 42);
print("before compile: " + (sarr[0] === 42));
ArkTools.arkSteedCompileSync(storeElement);
ArkTools.waitJitCompileFinish(storeElement);
print(ArkTools.isAOTCompiled(storeElement));
storeElement(sarr, 99);
print("after compile: " + (sarr[0] === 99));

// Value contract: local object store should throw TypeError
try {
    storeElement(sarr, {});
    print("contract: no throw");
} catch (e) {
    print("contract throws TypeError: " + (e instanceof TypeError));
}

// Out-of-bounds store should throw BusinessError
try {
    sarr[100] = 1;
    print("oob: no throw");
} catch (e) {
    print("oob throws: " + (e instanceof Error));
}
