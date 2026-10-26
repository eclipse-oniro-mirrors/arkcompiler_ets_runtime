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
//! METHOD peelColdCatch
//! COUNT JumpLoop 1
//! HAS_NOT LoadException
//! METHOD peelCatchFallback
//! COUNT JumpLoop 1
//! COUNT_GE LoadException 2

function coldThrow(i, failAt) {
    if (i === failAt) {
        throw i + 100;
    }
    return i;
}

function peelColdCatch(n, failAt) {
    let sum = 5;
    try {
        for (let i = 0; i < n; ++i) {
            sum += i;
            coldThrow(i, failAt);
        }
    } catch (e) {
        sum += e;
    }
    return sum;
}

function peelCatchFallback(n) {
    let value = 0;
    for (let i = 0; i < n; ++i) {
        try {
            if (i === 1) {
                throw "x";
            }
            value += i;
        } catch (e) {
            value = e;
        }
    }
    return value;
}

for (let i = 0; i < 4; ++i) {
    peelColdCatch(5, -1);
}
ArkTools.arkSteedCompileSync(peelColdCatch);
ArkTools.arkSteedCompileSync(peelCatchFallback);
print(peelColdCatch(5, -1));
print(peelColdCatch(5, 3));
for (let n of [0, 1, 2, 4]) {
    print(peelCatchFallback(n));
}
