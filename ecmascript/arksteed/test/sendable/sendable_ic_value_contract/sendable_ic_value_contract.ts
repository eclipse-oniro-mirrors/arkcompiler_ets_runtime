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

// Test: sendable IC value contract — string and bigint values are fast-pathed
// (their hclass carries the IsJSShared bit globally), while local objects
// deopt to the slow path (TypeError). Verify correctness after ArkSteed
// compilation.
//! METHOD storeValue
//! COMPILE_COUNT_GE *storeValue* 1

let sarr = new SendableArray(1, 2, 3);

function storeValue(arr, v) {
    for (let i = 0; i < 200; i++) { arr[0] = v; }
}

// Number (fast-path, non-heap)
storeValue(sarr, 42);
print("number store: " + (sarr[0] === 42));

// String (fast-path, IsJSShared bit on hclass)
storeValue(sarr, "str");
print("string store: " + (sarr[0] === "str"));

// BigInt (fast-path, IsJSShared bit on hclass)
storeValue(sarr, 123n);
print("bigint store: " + (sarr[0] === 123n));

ArkTools.arkSteedCompileSync(storeValue);
ArkTools.waitJitCompileFinish(storeValue);
print(ArkTools.isAOTCompiled(storeValue));

// After compilation, fast-path values still work
storeValue(sarr, 99);
print("number after compile: " + (sarr[0] === 99));
storeValue(sarr, "abc");
print("string after compile: " + (sarr[0] === "abc"));
storeValue(sarr, 456n);
print("bigint after compile: " + (sarr[0] === 456n));

// Local object still throws TypeError after compilation
try {
    storeValue(sarr, {});
    print("object store: no throw");
} catch (e) {
    print("object store throws TypeError: " + (e instanceof TypeError));
}
