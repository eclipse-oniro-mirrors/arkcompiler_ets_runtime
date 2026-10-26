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

//! METHOD peelLabelledExit
//! COUNT JumpLoop 2
//! COUNT_GE CallCommonStub Less 3
//! METHOD peelDoWhile
//! COUNT JumpLoop 1
//! COUNT_GE CallCommonStub Less 2

function peelLabelledExit(n, limit, mode) {
    let sum = 0;
    outer: for (let i = 0; i < n; ++i) {
        for (let j = 0; j < limit; ++j) {
            if (j === 1) {
                continue;
            }
            if (j === 3) {
                if (mode === 0) {
                    break outer;
                }
                if (mode === 1) {
                    continue outer;
                }
                break;
            }
            sum += i * 10 + j;
        }
        sum += 100;
    }
    return sum;
}

function peelDoWhile(n, stopAt) {
    let i = 0;
    let sum = 0;
    do {
        if (i === stopAt) {
            return sum + 100;
        }
        ++i;
        if (i === 2) {
            continue;
        }
        sum += i;
    } while (i < n);
    return sum;
}

ArkTools.arkSteedCompileSync(peelLabelledExit);
ArkTools.arkSteedCompileSync(peelDoWhile);
for (let n of [0, 1, 3]) {
    for (let mode of [0, 1, 2]) {
        print(peelLabelledExit(n, 5, mode));
    }
}
for (let n of [0, 1, 2, 5]) {
    print(peelDoWhile(n, -1));
    print(peelDoWhile(n, 1));
}
