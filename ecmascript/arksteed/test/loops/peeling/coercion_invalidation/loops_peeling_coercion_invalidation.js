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
//! METHOD genericAddFacts
//! HAS CallCommonStub Add
//! COUNT_GE LoadTaggedField 3
//! METHOD looseEqualFacts
//! HAS CallCommonStub Equal
//! COUNT_GE LoadTaggedField 3
//! METHOD relationalFacts
//! HAS CallCommonStub Less
//! COUNT_GE LoadTaggedField 3

function genericAddFacts(holder, value) {
    const before = holder.value;
    const result = value + 1;
    const after = holder.value;
    return [before, result, after];
}
function looseEqualFacts(holder, value) {
    const before = holder.value;
    const result = value == 5;
    const after = holder.value;
    return [before, result, after];
}
function relationalFacts(holder, value) {
    const before = holder.value;
    const result = value < 6;
    const after = holder.value;
    return [before, result, after];
}
const holder = {value: 7};
let hooks = 0;
const object = {valueOf() { ++hooks; holder.value = 19; return 5; }};
for (let i = 0; i < 16; ++i) {
    genericAddFacts(holder, object);
    looseEqualFacts(holder, object);
    relationalFacts(holder, object);
    holder.value = 7;
}
for (const fn of [genericAddFacts, looseEqualFacts, relationalFacts]) {
    ArkTools.arkSteedCompileSync(fn);
}
hooks = 0;
let ok = true;
for (const fn of [genericAddFacts, looseEqualFacts, relationalFacts]) {
    holder.value = 7;
    const result = fn(holder, object);
    ok = ok && result[0] === 7 && result[1] === (fn === genericAddFacts ? 6 : true) && result[2] === 19;
}
print(ok && hooks === 3 ? "PASS" : "FAIL");

const throwing = {valueOf() { holder.value = 31; throw new Error("conversion"); }};
ok = true;
for (const fn of [genericAddFacts, looseEqualFacts, relationalFacts]) {
    holder.value = 7;
    let caught = false;
    try {
        fn(holder, throwing);
    } catch (error) {
        caught = error.message === "conversion";
    }
    ok = ok && caught && holder.value === 31;
}
print(ok ? "PASS" : "FAIL");
