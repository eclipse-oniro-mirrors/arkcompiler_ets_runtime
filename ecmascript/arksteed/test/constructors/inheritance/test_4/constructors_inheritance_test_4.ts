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
class Base {
    foo: number;

    constructor(foo: number) {
        this.foo = foo;
    }

    value(): number {
        return this.foo;
    }
}

class Derived extends Base {
    bar: number;

    constructor(foo: number, bar: number) {
        super(foo);
        this.bar = bar;
    }

    value(): number {
        return this.foo + this.bar;
    }
}

// G11.6 additive evidence (arksteed-g116-call-closure-and-additive-evidence#C1,#C3):
// every compiled target of this leaf keeps its own compile/entry counters;
// the super-call form is annotated in the derived constructor's own dump and
// the actual construction form in the compiled same-domain caller's dump.
// The deliberately compiled class roles and all printed output are unchanged.
//! COMPILE_COUNT_GE Derived 1
//! EXEC_COUNT_GE *Derived 1
//! COMPILE_COUNT_GE probeInheritanceConstruction 1
//! EXEC_COUNT_GE *probeInheritanceConstruction 1
//! DEOPT_COUNT *Deoptimize* 0
//! METHOD Derived
//! HAS CallRuntime OptSuperCall
//! METHOD probeInheritanceConstruction
//! HAS CallRuntime OptNewObjRange

ArkTools.arkSteedCompileSync(Derived);
ArkTools.waitJitCompileFinish(Derived);


function probeInheritanceConstruction() {
    return [new Base(11), new Derived(22, 33)];
}
ArkTools.arkSteedCompileSync(probeInheritanceConstruction);
ArkTools.waitJitCompileFinish(probeInheritanceConstruction);

const b = new Base(11);
print(b.value());

const d = new Derived(22, 33);
print(d.value());

// Bounded silent preparation: the leaf's construction(s) once more through the
// compiled same-domain caller. It prints nothing and changes no earlier observation.
probeInheritanceConstruction();
