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

// @ts-nocheck
//! PARAMS --compiler-jit-hotness-threshold=60000
//! METHOD peelSharedPrepare
//! COUNT JumpLoop 1
//! HAS PrepareSharedStoreField
//! HAS StoreSharedFieldWithBarrier
//! COUNT LoadTaggedField 1
//! METHOD sharedTypeError
//! HAS PrepareSharedStoreField

class SharedText {
    text: string = "initial";
    constructor() {
        "use sendable";
    }
}

function peelSharedPrepare(shared: SharedText, fixed, text: string, n: number): number {
    let sum = 0;
    for (let i = 0; i < n; ++i) {
        sum += fixed.value;
        shared.text = text;
        sum += fixed.value;
    }
    return sum;
}

function sharedTypeError(shared: SharedText, text, fixed): number {
    try {
        shared.text = text;
    } catch (e) {
        fixed.value = 13;
    }
    return fixed.value;
}

const shared = new SharedText();
const fixed = {value: 7};
const prefix = "shared-loop-prefix-with-a-long-string-";
for (let i = 0; i < 8; ++i) {
    peelSharedPrepare(shared, fixed, prefix + i, 4);
    sharedTypeError(shared, "warm", fixed);
}
ArkTools.arkSteedCompileSync(peelSharedPrepare);
ArkTools.arkSteedCompileSync(sharedTypeError);
for (const n of [0, 1, 2, 8]) {
    const text = prefix + n;
    const before = shared.text;
    const result = peelSharedPrepare(shared, fixed, text, n);
    ArkTools.triggerSharedGC("shared_full");
    print(result === 14 * n && shared.text === (n === 0 ? before : text) ? "PASS" : "FAIL");
}
print(sharedTypeError(shared, {}, fixed) === 13 ? "PASS" : "FAIL");
