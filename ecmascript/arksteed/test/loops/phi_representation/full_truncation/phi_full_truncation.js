/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

//! METHOD truncatePhi
//! HAS Phi
//! HAS DoubleToInt32

function truncatePhi(value, chooseAdd, seed) {
    const number = chooseAdd ? value + 0.5 : value - 0.5;
    const liveDouble = seed * 0.5;
    return (number | 0) + seed + liveDouble;
}

function returnOriginal(value, chooseAdd, sink) {
    const number = chooseAdd ? value + 0.5 : value - 0.5;
    sink.value = number | 0;
    return number;
}

const inputs = [2 ** 64 + 4096, -(2 ** 64 + 4096), Infinity, -Infinity, NaN,
    2147483647, -2147483649, -0.5, 0.5];
const expected = [4108, -4084, 12, 12, 12, 2147483659, -2147483636, 12, 13];
const sink = {value: 0};
for (let i = 0; i < 100; i++) {
    for (const value of inputs) {
        truncatePhi(value, true, 8);
        truncatePhi(value, false, 8);
    }
    returnOriginal(3.25, (i & 1) === 0, sink);
}
for (const fn of [truncatePhi, returnOriginal]) {
    ArkTools.arkSteedCompileSync(fn);
    ArkTools.waitJitCompileFinish(fn);
}
let ok = ArkTools.isAOTCompiled(truncatePhi) && ArkTools.isAOTCompiled(returnOriginal);
for (let i = 0; i < inputs.length; i++) {
    ok = truncatePhi(inputs[i], true, 8) === expected[i] && ok;
}
ok = returnOriginal(3.25, true, sink) === 3.75 && sink.value === 3 && ok;
ok = returnOriginal(3.25, false, sink) === 2.75 && sink.value === 2 && ok;
print(ok ? "PASS" : "FAIL");
