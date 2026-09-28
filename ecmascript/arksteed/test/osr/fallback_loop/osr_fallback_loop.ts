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

//! PARAMS --compiler-enable-osr=true --compiler-osr-hotness-threshold=1

declare function print(value: number): void;

// Safe fallback for OSR (restriction 9 / E8): a loop method run with OSR enabled
// must complete correctly in the current tier. On the pinned revision the OSR
// chain has no entry path (see test/coverage/osr-chain-audit.md), so the result
// is produced without entering installed OSR code.
function osrLoop(n: number): number {
    let sum = 0;
    let i = 0;
    while (i < n) {
        sum += i;
        i++;
    }
    return sum;
}

let ret = osrLoop(2000);
print(ret);
