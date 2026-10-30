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

//! PARAMS --compiler-jit-hotness-threshold=0 --compiler-jit-ic-feedback-threshold=0 --compiler-max-inline-bytecodes=0

function target(x) { return x + 1; }
print(ArkTools.arkSteedIsCompiled(target));
let sum = 0;
for (let i = 0; i < 1000; i++) { sum += target(i); }
ArkTools.waitAllJitCompileFinish();
print(sum);
print(ArkTools.arkSteedIsCompiled(target));
print(target(41));
