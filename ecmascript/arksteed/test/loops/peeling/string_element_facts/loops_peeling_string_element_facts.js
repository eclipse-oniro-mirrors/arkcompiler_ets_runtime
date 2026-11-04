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
//! METHOD charFacts
//! HAS LineStringLoadElement
//! HAS LoadSingleCharTableElement
//! HAS CallCommonStub CreateStringBySingleCharCode
//! HAS_NOT CallCommonStub GetPropertyByValue
//! COUNT LoadTaggedField 4 # GlobalEnv, holder.value once, packing function and its environment.
//! METHOD peelCharFacts
//! HAS LineStringLoadElement
//! HAS CallCommonStub CreateStringBySingleCharCode
//! COUNT JumpLoop 1
//! HAS_NOT Deopt
//! COUNT LoadTaggedField 4
//! METHOD charThenCall
//! HAS LineStringLoadElement
//! HAS CallKnownJSFunction
//! COUNT_GE LoadTaggedField 3
//! METHOD charEnvFacts
//! HAS CallCommonStub CreateStringBySingleCharCode
//! COUNT LoadTaggedField 7 # GlobalEnv, captured slot, two TDZ-path loads and three packing-call loads.
//! COUNT CallKnownJSFunction 1
//! COUNT BranchIfReferenceEqual 1 # One TDZ check.

// Keep result-array initialization outside the methods whose loads are counted.
function packResults(before, character, after) {
    return [before, character, after];
}

function packLoopResults(sum, character) {
    return [sum, character];
}

function charFacts(holder, text, index) {
    const before = holder.value;
    const character = text[index];
    const after = holder.value;
    return packResults(before, character, after);
}
function peelCharFacts(holder, text, index, n) {
    let sum = 0;
    let character = "";
    for (let i = 0; i < n; ++i) {
        sum += holder.value;
        character = text[index];
        sum += holder.value;
    }
    return packLoopResults(sum, character);
}
function charThenCall(holder, text, index, callback) {
    const before = holder.value;
    const character = text[index];
    callback(holder);
    return packResults(before, character, holder.value);
}
function makeCharReader() {
    let captured = 7;
    function charEnvFacts(text, index) {
        const before = captured;
        const character = text[index];
        const after = captured;
        return packResults(before, character, after);
    }
    function set(value) { captured = value; }
    return [charEnvFacts, set];
}
function mutate(holder) { holder.value = 19; }

const holder = {value: 7};
const env = makeCharReader();
const text = "A\u4e2d"; // One cached character and one allocated character.
for (let i = 0; i < 16; ++i) {
    const index = i % text.length;
    charFacts(holder, text, index);
    peelCharFacts(holder, text, index, 4);
    charThenCall(holder, text, index, mutate);
    env[0](text, index);
    holder.value = 7;
}
for (const fn of [charFacts, peelCharFacts, charThenCall, env[0]]) {
    ArkTools.arkSteedCompileSync(fn);
}
let ok = true;
for (let index = 0; index < text.length; ++index) {
    const result = charFacts(holder, text, index);
    ok = ok && result[0] === 7 && result[1] === text[index] && result[2] === 7;
}
print(ok ? "PASS" : "FAIL");

ok = true;
for (const n of [0, 1, 2, 8]) {
    const result = peelCharFacts(holder, text, 1, n);
    ok = ok && result[0] === n * 14 && result[1] === (n === 0 ? "" : "\u4e2d");
}
print(ok ? "PASS" : "FAIL");

env[1](23);
ArkTools.forceFullGC();
const captured = env[0](text, 1);
print(captured[0] === 23 && captured[1] === "\u4e2d" && captured[2] === 23 ? "PASS" : "FAIL");
const mutated = charThenCall(holder, text, 1, mutate);
print(mutated[0] === 7 && mutated[1] === "\u4e2d" && mutated[2] === 19 ? "PASS" : "FAIL");

// Bounds guards must still deopt; fallback string/property semantics are unchanged.
const oob = charFacts(holder, text, text.length);
const negative = charFacts(holder, text, -1);
print(oob[0] === 19 && oob[1] === undefined && oob[2] === 19 &&
      negative[1] === undefined ? "PASS" : "FAIL");
