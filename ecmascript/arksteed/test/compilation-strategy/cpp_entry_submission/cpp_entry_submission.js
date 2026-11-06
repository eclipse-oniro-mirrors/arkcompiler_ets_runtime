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

//! PARAMS --compiler-jit-hotness-threshold=20 --compiler-jit-ic-feedback-threshold=20 --compiler-max-inline-bytecodes=0

let calls = 0;
function replacer(key, value) {
    calls++;
    return value;
}
print(ArkTools.arkSteedIsCompiled(replacer));
let result;
for (let i = 0; i < 5000; i++) {
    // The JSON stringifier calls JSFunction::Call from C++, entering the
    // assembly interpreter without the call-site PGO submission check.
    result = JSON.stringify({value: i}, replacer);
}
ArkTools.waitAllJitCompileFinish();
print(result);
print(calls);
print(ArkTools.arkSteedIsCompiled(replacer));
