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
//! METHOD concatFacts
//! HAS CallCommonStub StringAdd
//! COUNT LoadTaggedField 4 # GlobalEnv, holder.value once, packing function and its environment.
//! METHOD numberStringFacts
//! HAS CallRuntime NumberToString
//! HAS CallCommonStub StringAdd
//! HAS BranchIfTaggedString
//! COUNT LoadTaggedField 4
//! METHOD knownNumberStringFacts
//! HAS CallRuntime NumberToString
//! HAS_NOT BranchIfTaggedString
//! COUNT LoadTaggedField 3 # No GlobalEnv load is needed for the known-number path.
//! METHOD peelConcatFacts
//! HAS CallCommonStub StringAdd
//! COUNT JumpLoop 1
//! COUNT LoadTaggedField 4

// Keep result-array initialization outside the methods whose loads are counted.
function packResults(before, text, after) {
    return [before, text, after];
}

function packLoopResults(sum, text) {
    return [sum, text];
}

function concatFacts(holder, left, right) {
    const before = holder.value;
    const text = left + right;
    const after = holder.value;
    return packResults(before, text, after);
}

function numberStringFacts(holder, value) {
    const before = holder.value;
    const text = "n=" + value;
    const after = holder.value;
    return packResults(before, text, after);
}

function knownNumberStringFacts(holder, value) {
    const number = value | 0;
    const before = holder.value;
    const text = "" + number;
    const after = holder.value;
    return packResults(before, text, after);
}

function peelConcatFacts(holder, left, right, n) {
    let sum = 0;
    let text = "";
    for (let i = 0; i < n; ++i) {
        sum += holder.value;
        text = left + right;
        sum += holder.value;
    }
    return packLoopResults(sum, text);
}

const holder = {value: 7};
for (let i = 0; i < 16; ++i) {
    concatFacts(holder, "prefix-", "suffix");
    numberStringFacts(holder, i);
    numberStringFacts(holder, "text");
    knownNumberStringFacts(holder, i);
    peelConcatFacts(holder, "prefix-", "suffix", 4);
}
ArkTools.arkSteedCompileSync(concatFacts);
ArkTools.arkSteedCompileSync(numberStringFacts);
ArkTools.arkSteedCompileSync(knownNumberStringFacts);
ArkTools.arkSteedCompileSync(peelConcatFacts);

const concat = concatFacts(holder, "prefix-", "suffix");
print(concat[0] === 7 && concat[1] === "prefix-suffix" && concat[2] === 7 ? "PASS" : "FAIL");

let ok = true;
for (const value of [-23, 1.5, "text"]) {
    const result = numberStringFacts(holder, value);
    ok = ok && result[0] === 7 && result[1] === "n=" + value && result[2] === 7;
}
const known = knownNumberStringFacts(holder, -123);
print(ok && known[0] === 7 && known[1] === "-123" && known[2] === 7 ? "PASS" : "FAIL");

ok = true;
for (const n of [0, 1, 2, 8]) {
    const result = peelConcatFacts(holder, "prefix-", "suffix", n);
    ok = ok && result[0] === n * 14 && result[1] === (n === 0 ? "" : "prefix-suffix");
}
print(ok ? "PASS" : "FAIL");

// An object must leave the checked string/number path before running conversion.
const object = {valueOf() { holder.value = 19; return 5; }};
const deopt = numberStringFacts(holder, object);
print(deopt[0] === 7 && deopt[1] === "n=5" && deopt[2] === 19 ? "PASS" : "FAIL");
