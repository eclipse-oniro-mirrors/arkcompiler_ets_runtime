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

//! PARAMS --compiler-arksteed-enable-cse=false --compiler-jit-hotness-threshold=60000 --enable-pgo-profiler=true --compiler-enable-jit-lazy-deopt=false

//! METHOD repeatedRead
//! COUNT TypedArrayDoubleLoadElement 2
function repeatedRead(array, index, flag) {
    const first = array[index];
    let extra;
    if (flag) extra = 3;
    else extra = 7;
    return first + array[index] + extra;
}

function check(condition, message) {
    if (!condition) throw new Error(message);
}

const array = new Float64Array([2.5, 4.5]);
for (let i = 0; i < 2000; ++i) {
    repeatedRead(array, i & 1, (i & 1) === 0);
}

ArkTools.arkSteedCompileSync(repeatedRead);
ArkTools.waitJitCompileFinish(repeatedRead);
check(ArkTools.arkSteedIsCompiled(repeatedRead), "compile");

for (let i = 0; i < 100; ++i) {
    const index = i & 1;
    const flag = (i & 2) === 0;
    array[index] = i + 0.5;
    const expected = array[index] * 2 + (flag ? 3 : 7);
    check(repeatedRead(array, index, flag) === expected, "result");
}
check(ArkTools.arkSteedIsCompiled(repeatedRead), "stays compiled");
print("PASS");
