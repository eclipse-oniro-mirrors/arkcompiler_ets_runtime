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

// newobjrange specialization: runtime target/HClass guard failures must fall
// back to the generic construct and still build the correct object.
//
// Trigger A (target change): 16 warm-ups with `chooseB == false` make TargetA
// the monomorphic feedback target; the caller is compiled, then `chooseB`
// flips to true so the identity guard fails on the next call.
//
// Trigger B (instance HClass change): 16 warm-ups create the planned instance
// HClass; after compiling the caller, `HClassMut.prototype` is replaced, which
// installs a new instance HClass in the constructor, so the layout guard fails.
//
// Expected: every call constructs the currently selected target with the
// correct prototype/fields. Both guard failures result in a correct generic
// construction, and the specialized graph is proven by the annotations.

//! METHOD callTargetChange
//! HAS InitializeEmptyObject
//! HAS CallNewObjRangeBase
//! HAS CallNewObjRangeDerived
//! METHOD callHClassChange
//! HAS InitializeEmptyObject
//! HAS CallNewObjRangeBase
//! HAS CallNewObjRangeDerived

class TargetA {
    tag: string;
    constructor() {
        this.tag = "A";
    }
}

class TargetB {
    tag: string;
    constructor() {
        this.tag = "B";
    }
}

let chooseB = false;

function callTargetChange() {
    const Ctor = chooseB ? TargetB : TargetA;
    return new Ctor();
}

for (let i = 0; i < 16; i++) {
    callTargetChange();
}
ArkTools.arkSteedCompileSync(callTargetChange);

print(callTargetChange().tag);
chooseB = true;
print(callTargetChange().tag);
chooseB = false;
print(callTargetChange().tag);

// A plain function, not a class: a class `prototype` property is not writable.
function HClassMut() {
    this.v = 1;
}

function callHClassChange() {
    return new HClassMut();
}

for (let i = 0; i < 16; i++) {
    callHClassChange();
}
ArkTools.arkSteedCompileSync(callHClassChange);

print(callHClassChange().v);
HClassMut.prototype = { extra: 5 };
const changed = callHClassChange();
print(changed.v);
print(changed.extra);
print(Object.getPrototypeOf(changed) === HClassMut.prototype);
print(changed instanceof HClassMut);
