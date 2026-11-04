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
}

class Derived extends Base {
    bar: number;

    constructor(foo: number, bar: number) {
        super(foo);
        this.bar = bar;
    }
}

// Compile Derived only
// G11.6 additive evidence (arksteed-g116-call-closure-and-additive-evidence#C1,#C3):
// every compiled target of this leaf keeps its own compile/entry counters;
// the super-call form is annotated in the derived constructor's own dump and
// the actual construction form in the compiled same-domain caller's dump.
// The deliberately compiled class roles and all printed output are unchanged.
//! COMPILE_COUNT_GE Derived 1
//! EXEC_COUNT_GE *Derived 1
//! COMPILE_COUNT_GE constructor_inheritance_1 1
//! EXEC_COUNT_GE *constructor_inheritance_1 1
//! DEOPT_COUNT *Deoptimize* 0
//! METHOD Derived
//! HAS CallRuntime OptSuperCall
//! METHOD constructor_inheritance_1
//! HAS CallRuntime OptNewObjRange

ArkTools.arkSteedCompileSync(Derived);
ArkTools.waitJitCompileFinish(Derived);


function constructor_inheritance_1(): void {
    let d = new Derived(24, 36);
    print(d.foo);
    print(d.bar);
}

ArkTools.arkSteedCompileSync(constructor_inheritance_1);
ArkTools.waitJitCompileFinish(constructor_inheritance_1);
constructor_inheritance_1();
