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

//! PARAMS --enable-force-gc=true --compiler-jit-hotness-threshold=20 --compiler-jit-ic-feedback-threshold=20 --compiler-max-inline-bytecodes=0

// Exercise deferred submission and argument semantics with forced GC enabled.
// The receiver also has strong references outside the call arguments, and
// this test does not pin a collection to the runtime submission window.

let observed = null;
function target(object, text, missing) {
    observed = object;
    return object.value + text.length + (missing === undefined ? 1 : 1000);
}
function caller(object, text) { return target(object, text); }
const receiver = { value: 40 };
for (let i = 0; i < 3; i++) { caller(receiver, "abc"); }
ArkTools.arkSteedCompileSync(caller);
print(ArkTools.arkSteedIsCompiled(caller));
print(ArkTools.arkSteedIsCompiled(target));
let sum = 0;
for (let i = 0; i < 2000; i++) { sum += caller(receiver, "abc"); }
ArkTools.waitAllJitCompileFinish();
print(sum);
print(ArkTools.arkSteedIsCompiled(target));
print(observed === receiver);
print(caller(receiver, "abcd"));
