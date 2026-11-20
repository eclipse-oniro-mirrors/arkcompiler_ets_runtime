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

// A configurable global's PropertyBox is cleared to Hole when the property is
// deleted or redefined, so its value is checked again after anything that may
// run JS: a call or a getter. A value reused with no such effect in between is
// checked once.

//! COMPILE_MODE script

//! METHOD readTwice
//! COUNT_LE DeoptIfTaggedCondition 1  # both reads in probe === probe share one PropertyBox Hole check

//! METHOD readAcrossCall
//! COUNT_LE DeoptIfTaggedCondition 2  # one PropertyBox Hole check on probe before change() and one after

//! METHOD readThroughGetter
//! COUNT_GE DeoptIfTaggedCondition 2  # the getter may delete getterProbe between the reads

let deleting = false;
globalThis.probe = 1;
globalThis.getterProbe = {
    get v() {
        if (deleting) {
            delete globalThis.getterProbe;
        }
        return 1;
    }
};

function change() {
    if (deleting) {
        delete globalThis.probe;
    }
}

function readTwice() {
    return probe === probe;
}

function readAcrossCall() {
    const first = probe;
    change();
    return first + probe;
}

function readThroughGetter() {
    return getterProbe.v + getterProbe.v;
}

for (let i = 0; i < 100; i++) {
    readTwice();
    readAcrossCall();
    readThroughGetter();
}
for (const target of [readTwice, readAcrossCall, readThroughGetter]) {
    ArkTools.arkSteedCompileSync(target);
}

print(readTwice());
deleting = true;
try {
    print(readAcrossCall());
} catch (e) {
    print(e.name);
}
try {
    print(readThroughGetter());
} catch (e) {
    print(e.name);
}
try {
    print(readTwice());  // change() deleted probe before this call.
} catch (e) {
    print(e.name);
}
