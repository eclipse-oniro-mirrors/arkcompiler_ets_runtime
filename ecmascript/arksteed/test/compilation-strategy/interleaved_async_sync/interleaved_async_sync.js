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

//! PARAMS --compiler-jit-hotness-threshold=60000 --compiler-max-inline-bytecodes=0

function addSeries(value) {
    let result = 0;
    for (let i = 0; i < 32; i++) { result += value + i; }
    return result;
}
function subtractSeries(value) {
    let result = 0;
    for (let i = 0; i < 16; i++) { result += value - i; }
    return result;
}
function multiplySeries(value) {
    let result = 0;
    for (let i = 1; i <= 8; i++) { result += value * i; }
    return result;
}
for (let i = 0; i < 16; i++) {
    addSeries(i);
    subtractSeries(i);
    multiplySeries(i);
}
ArkTools.arkSteedCompileAsync(addSeries);
ArkTools.arkSteedCompileAsync(subtractSeries);
ArkTools.arkSteedCompileAsync(multiplySeries);
ArkTools.arkSteedCompileAsync(subtractSeries);
// Wait in a different order from submission. Assert each method's own
// installation immediately after its synchronous request, before a global
// wait could hide an incorrectly associated task or delayed installation.
ArkTools.arkSteedCompileSync(subtractSeries);
print(ArkTools.arkSteedIsCompiled(subtractSeries));
print(subtractSeries(10));
ArkTools.arkSteedCompileSync(addSeries);
print(ArkTools.arkSteedIsCompiled(addSeries));
print(addSeries(3));
ArkTools.arkSteedCompileSync(multiplySeries);
print(ArkTools.arkSteedIsCompiled(multiplySeries));
print(multiplySeries(5));
ArkTools.waitAllJitCompileFinish();
print(addSeries(4));
print(subtractSeries(20));
print(multiplySeries(7));
