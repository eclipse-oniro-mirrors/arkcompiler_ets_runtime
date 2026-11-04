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
        print(`Base.constructor: foo = ${foo}`);
        this.foo = foo;
    }
}

class Derived extends Base {
    constructor(foo: number) {
        super(foo);
    }
}

// Compile Derived only
// G11.6 additive evidence (arksteed-g116-call-closure-and-additive-evidence#C1,#C3):
// `Derived` remains the deliberately compiled class (Base stays uncompiled); its
// own dump carries the real super-call form. The existing same-domain caller is
// additionally compiled as the test-side carrier of the actual construction form.
// All printed output is unchanged.
//! COMPILE_COUNT_GE Derived 1
//! EXEC_COUNT_GE *Derived 1
//! COMPILE_COUNT_GE constructor_inheritance_call_super_only 1
//! EXEC_COUNT_GE *constructor_inheritance_call_super_only 1
//! DEOPT_COUNT *Deoptimize* 0
//! METHOD Derived
//! HAS CallRuntime OptSuperCall
//! METHOD constructor_inheritance_call_super_only
//! HAS CallRuntime OptNewObjRange

ArkTools.arkSteedCompileSync(Derived);
ArkTools.waitJitCompileFinish(Derived);


function constructor_inheritance_call_super_only(): void {
    new Derived(24);
}

ArkTools.arkSteedCompileSync(constructor_inheritance_call_super_only);
ArkTools.waitJitCompileFinish(constructor_inheritance_call_super_only);
constructor_inheritance_call_super_only();
print("OK");
