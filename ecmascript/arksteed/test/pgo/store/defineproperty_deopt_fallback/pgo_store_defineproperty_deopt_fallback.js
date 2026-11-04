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
//! HAS_NOT TransitionHClassWithBarrier
//! HAS_NOT StoreTaggedField

// A class field over a base-installed configurable accessor takes the generic DefineField path:
// the property is found by the interpreter's lookup, so the site never records IC feedback and
// the compiled code must keep it unoptimized. The accessor is converted to a data property
// without invoking the setter, and a non-extensible receiver still throws.

let setterCalls = 0;
class Base {
    constructor() {
        Object.defineProperty(this, 'value', {
            get: function () { return 1; },
            set: function (v) { setterCalls++; },
            configurable: true
        });
    }
}
class Derived extends Base {
    value = 5;
}

let ok = true;
for (let i = 0; i < 20000; i++) {
    let d = new Derived();
    ok = ok && d.value === 5 && setterCalls === 0;
}
ArkTools.arkSteedCompileSync(Derived);
ArkTools.waitJitCompileFinish(Derived);
ArkTools.waitAllJitCompileFinish();
for (let i = 0; i < 512; i++) {
    let d = new Derived();
    ok = ok && d.value === 5 && setterCalls === 0;
    if ((i & 63) === 0) {
        ArkTools.gc();
    }
}

let threw = false;
try {
    let frozen = Object.preventExtensions({});
    frozen.value = 1;
} catch (e) {
    threw = true;
}

print(ok && threw ? "PASS" : "FAIL");
