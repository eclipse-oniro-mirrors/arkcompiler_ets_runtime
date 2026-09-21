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

//! METHOD instance_initializer
//! HAS DeoptIfHClassMismatch
//! HAS TransitionHClassWithBarrier
//! HAS StoreTaggedField

// The field initializer site records a transition handler from plain receivers (the base ctor
// installs nothing). Compiled, the site becomes a guarded direct store. Receiving the
// accessor-installed shape fails the hclass guard and lazily deopts to the generic path, which
// converts the configurable accessor into a data property without invoking its setter.

let setterCalls = 0;
class Base {
    constructor(withAccessor) {
        if (withAccessor) {
            Object.defineProperty(this, 'value', {
                get: function () { return 1; },
                set: function (v) { setterCalls++; },
                configurable: true
            });
        }
    }
}
class Derived extends Base {
    value = 5;
}

function make(withAccessor) {
    return new Derived(withAccessor);
}

// Warmup with plain receivers only: the site records a transition handler.
for (let i = 0; i < 20000; i++) {
    make(false);
}
ArkTools.arkSteedCompileSync(make);
ArkTools.waitJitCompileFinish(make);
ArkTools.waitAllJitCompileFinish();

let ok = true;
// Guard passes: plain receivers keep taking the direct store.
for (let i = 0; i < 256; i++) {
    let d = make(false);
    ok = ok && d.value === 5 && setterCalls === 0;
}
// Guard fails: accessor receivers deopt to the generic path and convert the property.
for (let i = 0; i < 256; i++) {
    let d = make(true);
    ok = ok && d.value === 5 && setterCalls === 0;
    let desc = Object.getOwnPropertyDescriptor(d, 'value');
    ok = ok && desc.value === 5 && desc.writable && desc.enumerable && desc.configurable;
    if ((i & 63) === 0) {
        ArkTools.gc();
    }
}

print(ok ? "PASS" : "FAIL");
