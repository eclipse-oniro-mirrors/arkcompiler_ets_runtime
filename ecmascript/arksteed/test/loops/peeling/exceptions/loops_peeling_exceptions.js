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
//! METHOD peelTryInside
//! COUNT JumpLoop 1
//! COUNT_GE LoadException 2
//! METHOD peelTryOutside
//! COUNT JumpLoop 1
//! COUNT LoadException 1
//! METHOD peelRethrow
//! COUNT JumpLoop 1
//! COUNT_GE LoadException 3

function throwAt(i, failAt) {
    if (i === failAt) {
        throw i;
    }
    return i + 1;
}

function peelTryInside(n, failAt) {
    let sum = 0;
    for (let i = 0; i < n; ++i) {
        try {
            sum += throwAt(i, failAt);
        } catch (e) {
            sum += 100 + e;
        }
    }
    return sum;
}

function peelTryOutside(n, failAt) {
    let sum = 7;
    try {
        for (let i = 0; i < n; ++i) {
            sum += i;
            throwAt(i, failAt);
        }
    } catch (e) {
        return sum + 1000 + e;
    }
    return sum;
}

function peelRethrow(n, failAt) {
    let sum = 0;
    try {
        for (let i = 0; i < n; ++i) {
            try {
                sum += throwAt(i, failAt);
            } catch (e) {
                sum += 10;
                if (e < 2) {
                    continue;
                }
                throw e + 100;
            }
        }
    } catch (e) {
        sum += e;
    }
    return sum;
}

for (let warm = 0; warm < 4; ++warm) {
    for (let failAt of [-1, 0, 1, 3]) {
        peelTryInside(5, failAt);
        peelTryOutside(5, failAt);
        peelRethrow(5, failAt);
    }
}
ArkTools.arkSteedCompileSync(peelTryInside);
ArkTools.arkSteedCompileSync(peelTryOutside);
ArkTools.arkSteedCompileSync(peelRethrow);

for (let n of [0, 1, 2, 5]) {
    for (let failAt of [-1, 0, 1, 3]) {
        print(peelTryInside(n, failAt) + ":" +
              peelTryOutside(n, failAt) + ":" + peelRethrow(n, failAt));
    }
}
