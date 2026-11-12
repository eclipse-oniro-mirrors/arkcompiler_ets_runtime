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

// Script var/function bindings are non-configurable and script let/const/class
// bindings live in the global record, so their PropertyBox is never cleared to
// Hole: reads and stores need no check, even across calls. A const or a
// non-writable var is folded to a constant.

//! COMPILE_MODE script

//! METHOD readAcrossCall
//! HAS LoadTaggedField
//! HAS_NOT DeoptIfTaggedCondition

//! METHOD store
//! HAS_NOT DeoptIfTaggedCondition

//! METHOD readConst
//! HAS_NOT LoadTaggedField

//! METHOD readConstObject
//! HAS_NOT LoadTaggedField

//! METHOD readReadOnlyVar
//! HAS_NOT LoadTaggedField

var scriptVar = 1;
let scriptLet = 2;
var readOnlyVar = 3;
Object.defineProperty(globalThis, "readOnlyVar", { writable: false });
const LIMIT = parseInt("1000");  // a literal initializer is folded by the frontend already
const TABLE = { k: 5 };

let mode = 0;
function change() {
    if (mode === 1) {
        scriptVar = 10;
        scriptLet = 20;
    } else if (mode === 2) {
        try {
            delete globalThis.scriptVar;  // throws: the binding is non-configurable
        } catch (e) {
        }
        Object.defineProperty(globalThis, "scriptVar", { value: 40 });
    }
}

function readAcrossCall() {
    const before = scriptVar + scriptLet;
    change();
    return before * 100 + scriptVar + scriptLet;
}

function store(value) {
    scriptVar = value;
    scriptLet = value + 1;
}

function readConst(x) {
    return x + LIMIT;
}

function readConstObject() {
    return TABLE;
}

function readReadOnlyVar() {
    return readOnlyVar;
}

for (let i = 0; i < 100; i++) {
    readAcrossCall();
    store(1);
    readConst(i);
    readConstObject();
    readReadOnlyVar();
}
for (const target of [readAcrossCall, store, readConst, readConstObject, readReadOnlyVar]) {
    ArkTools.arkSteedCompileSync(target);
}

mode = 1;
print(readAcrossCall());
mode = 2;
print(readAcrossCall());
mode = 0;
store(5);
print(readAcrossCall());
print(readConst(1));
print(readConstObject() === TABLE);
print(readReadOnlyVar());
