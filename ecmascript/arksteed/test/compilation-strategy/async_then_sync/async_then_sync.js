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

function target(x) {
    let sum = 0;
    for (let i = 0; i < 32; i++) { sum += x + i; }
    return sum;
}
for (let i = 0; i < 16; i++) { target(i); }
print(ArkTools.arkSteedCompileAsync(target));
// A synchronous request must return with installed code, including when
// it encounters the preceding asynchronous compilation still in flight.
print(ArkTools.arkSteedCompileSync(target));
print(ArkTools.arkSteedIsCompiled(target));
print(target(3));
print(ArkTools.arkSteedCompileSync(target));
print(target(4));
ArkTools.waitAllJitCompileFinish();
