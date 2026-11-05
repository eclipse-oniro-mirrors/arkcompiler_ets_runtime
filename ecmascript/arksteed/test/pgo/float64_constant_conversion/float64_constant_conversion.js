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

//! METHOD floatConstants
//! COUNT CheckedNumberToF64 1
//! COUNT F64Mul 2
//! COUNT F64Add 1
function floatConstants(value) {
    return value * 0.1 + value * 0.2;
}

//! METHOD zeroConstants
//! COUNT CheckedNumberToF64 1
function zeroConstants(value) {
    const positive = value * 0;
    const negative = value * -0;
    return 1 / positive + 1 / negative;
}

//! METHOD nanConstants
//! COUNT CheckedNumberToF64 1
function nanConstants(value) {
    return value * 0.1 + value * (0 / 0);
}

for (let iteration = 0; iteration < 2000; iteration++) {
    floatConstants(2.5);
    zeroConstants(2.5);
    nanConstants(2.5);
}

ArkTools.arkSteedCompileSync(floatConstants);
ArkTools.arkSteedCompileSync(zeroConstants);
ArkTools.arkSteedCompileSync(nanConstants);

print(ArkTools.arkSteedIsCompiled(floatConstants));
print(ArkTools.arkSteedIsCompiled(zeroConstants));
print(ArkTools.arkSteedIsCompiled(nanConstants));
print(floatConstants(10));
print(Number.isNaN(zeroConstants(2.5)));
print(Number.isNaN(nanConstants(2.5)));
print(floatConstants("10"));
