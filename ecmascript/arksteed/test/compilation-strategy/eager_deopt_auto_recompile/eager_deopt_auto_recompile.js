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

//! PARAMS --compiler-jit-hotness-threshold=400 --compiler-jit-ic-feedback-threshold=20 --compiler-max-inline-bytecodes=0

function read(object) { return object.value + 1; }
const first = { value: 10 };
const second = { extra: true, value: 20 };
for (let i = 0; i < 32; i++) { read(first); }
ArkTools.arkSteedCompileSync(read);
print(ArkTools.arkSteedIsCompiled(read));
print(read(second));
print(ArkTools.arkSteedIsCompiled(read));
// No explicit compilation after deoptimization: stable feedback must
// permit the reset method to tier up again through ordinary calls.
let sum = 0;
for (let i = 0; i < 10000; i++) { sum += read(i % 2 === 0 ? first : second); }
ArkTools.waitAllJitCompileFinish();
print(sum);
print(ArkTools.arkSteedIsCompiled(read));
print(read(first));
print(read(second));
