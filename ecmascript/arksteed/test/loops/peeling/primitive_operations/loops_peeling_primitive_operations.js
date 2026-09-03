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
//! METHOD truthFacts
//! HAS CallCommonStub ToBooleanTrue
//! COUNT LoadTaggedField 3 # holder.value once, packing function and its environment.
//! METHOD falseFacts
//! HAS CallCommonStub ToBooleanFalse
//! COUNT LoadTaggedField 3
//! METHOD typeofFacts
//! HAS CallCommonStub TypeOf
//! COUNT LoadTaggedField 3
//! METHOD strictFacts
//! HAS CallCommonStub StrictEqual
//! COUNT LoadTaggedField 4 # Also loads GlobalEnv for StrictEqual.
//! METHOD strictNotFacts
//! HAS CallCommonStub StrictNotEqual
//! COUNT LoadTaggedField 4
//! METHOD peelPrimitiveFacts
//! HAS CallCommonStub TypeOf
//! HAS CallCommonStub StrictEqual
//! COUNT JumpLoop 1
//! COUNT LoadTaggedField 4
//! METHOD envFacts
//! HAS CallCommonStub TypeOf
//! COUNT LoadTaggedField 6 # Captured slot, two TDZ-path loads, parent env, packing function and its env.
//! COUNT CallKnownJSFunction 1
//! COUNT BranchIfReferenceEqual 1 # One TDZ check.

// Keep result-array initialization outside the methods whose loads are counted.
function packResults(before, result, after) {
    return [before, result, after];
}

function truthFacts(holder, value) {
    const before = holder.value;
    const result = !!value;
    const after = holder.value;
    return packResults(before, result, after);
}
function falseFacts(holder, value) {
    const before = holder.value;
    const result = value && 1;
    const after = holder.value;
    return packResults(before, result, after);
}
function typeofFacts(holder, value) {
    const before = holder.value;
    const result = typeof value;
    const after = holder.value;
    return packResults(before, result, after);
}
function strictFacts(holder, left, right) {
    const before = holder.value;
    const result = left === right;
    const after = holder.value;
    return packResults(before, result, after);
}
function strictNotFacts(holder, left, right) {
    const before = holder.value;
    const result = left !== right;
    const after = holder.value;
    return packResults(before, result, after);
}
function peelPrimitiveFacts(holder, value, other, n) {
    let sum = 0;
    let type = "";
    let equal = false;
    for (let i = 0; i < n; ++i) {
        sum += holder.value;
        type = typeof value;
        equal = value === other;
        sum += holder.value;
    }
    return packResults(sum, type, equal);
}
function makeEnvReader() {
    let captured = 7;
    function envFacts(value) {
        const before = captured;
        const result = typeof value;
        const after = captured;
        return packResults(before, result, after);
    }
    function set(value) { captured = value; }
    return [envFacts, set];
}

const holder = {value: 7};
let hooks = 0;
const object = {
    valueOf() { ++hooks; holder.value = 99; return 1; },
    toString() { ++hooks; holder.value = 99; return "object"; }
};
const values = [undefined, null, false, 0, "text", 12n, Symbol("s"), object];
const pairs = [[object, object], [object, {}], [1, "1"], [NaN, NaN], [12n, 12n]];
const env = makeEnvReader();
for (let i = 0; i < 16; ++i) {
    const value = values[i % values.length];
    const pair = pairs[i % pairs.length];
    truthFacts(holder, value);
    falseFacts(holder, value);
    typeofFacts(holder, value);
    strictFacts(holder, pair[0], pair[1]);
    strictNotFacts(holder, pair[0], pair[1]);
    peelPrimitiveFacts(holder, pair[0], pair[1], 4);
    env[0](value);
}
for (const fn of [truthFacts, falseFacts, typeofFacts, strictFacts, strictNotFacts, peelPrimitiveFacts, env[0]]) {
    ArkTools.arkSteedCompileSync(fn);
}
let ok = true;
for (const value of values) {
    const truth = truthFacts(holder, value);
    const falsity = falseFacts(holder, value);
    const type = typeofFacts(holder, value);
    ok = ok && truth[0] === 7 && truth[1] === !!value && truth[2] === 7;
    ok = ok && falsity[0] === 7 && Object.is(falsity[1], value && 1) && falsity[2] === 7;
    ok = ok && type[0] === 7 && type[1] === typeof value && type[2] === 7;
}
print(ok && hooks === 0 ? "PASS" : "FAIL");

ok = true;
for (const pair of pairs) {
    const equal = strictFacts(holder, pair[0], pair[1]);
    const unequal = strictNotFacts(holder, pair[0], pair[1]);
    ok = ok && equal[0] === 7 && equal[1] === (pair[0] === pair[1]) && equal[2] === 7;
    ok = ok && unequal[0] === 7 && unequal[1] === (pair[0] !== pair[1]) && unequal[2] === 7;
}
print(ok && hooks === 0 ? "PASS" : "FAIL");

ok = true;
for (const n of [0, 1, 2, 8]) {
    const result = peelPrimitiveFacts(holder, object, object, n);
    ok = ok && result[0] === n * 14 && result[1] === (n === 0 ? "" : "object") && result[2] === (n !== 0);
}
print(ok && hooks === 0 ? "PASS" : "FAIL");

env[1](23);
const result = env[0](object);
print(result[0] === 23 && result[1] === "object" && result[2] === 23 ? "PASS" : "FAIL");
