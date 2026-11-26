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

"use strict";

//! METHOD overwrite
//! HAS_NOT CallCommonStub SetPropertyByName

//! METHOD overwriteMixed
//! HAS_NOT CallCommonStub SetPropertyByName

function overwrite(o, v) {
    o.value = v;
}

function overwriteMixed(o, v) {
    o.value = v;
}

function compile(f) {
    ArkTools.arkSteedCompileSync(f);
    ArkTools.waitJitCompileFinish(f);
}

function make(v) {
    return { value: v };
}

const receiver = make(1);
make(2).extra = 0;
let ok = ArkTools.hasFastProperties(receiver) && !ArkTools.isStableHClass(receiver);
for (let i = 0; i < 200; ++i) {
    overwrite(receiver, i);
}
compile(overwrite);
const payload = {};
overwrite(receiver, payload);
ok = ok && receiver.value === payload;

let observed;
ok = ok && ArkTools.arkSteedIsCompiled(overwrite);
Object.defineProperty(receiver, "value", { set(v) { observed = v; } });
overwrite(receiver, 73);
print(ok && observed === 73 ? "PASS own field and setter" : "FAIL");

function makeWide() {
    const o = {};
    for (let i = 0; i < 22; ++i) {
        o["slot" + i] = i;
    }
    o.value = 0;
    return o;
}

// Exercise both a stable in-object field and an unstable properties-array field at one store site.
const stable = { untouched: 11, value: 0 };
const wide = makeWide();
makeWide().extra = 1;
ok = ArkTools.isStableHClass(stable) && ArkTools.hasFastProperties(wide) && !ArkTools.isStableHClass(wide);
for (let i = 0; i < 200; ++i) {
    overwriteMixed(i % 2 ? stable : wide, i);
}
compile(overwriteMixed);
overwriteMixed(wide, payload);
overwriteMixed(stable, 73);
ok = ok && wide.value === payload && wide.slot21 === 21 && stable.value === 73 && stable.untouched === 11;
print(ok ? "PASS mixed shapes and field storage" : "FAIL");

const compiledBeforeReadOnly = ArkTools.arkSteedIsCompiled(overwriteMixed);
Object.defineProperty(wide, "value", { writable: false });
let threw = false;
try {
    overwriteMixed(wide, 99);
} catch (e) {
    threw = e instanceof TypeError;
}
print(compiledBeforeReadOnly && threw && wide.value === payload ? "PASS read-only assignment" : "FAIL");
