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
class L0 {
    a: number;

    constructor(a: number) {
        this.a = a;
    }

    getA(): number {
        return this.a;
    }
}

class L1 extends L0 {
    b: number;

    constructor(a: number, b: number) {
        super(a);
        this.b = b;
    }

    getB(): number {
        return this.b;
    }
}

class L2 extends L1 {
    c: number;

    constructor(a: number, b: number, c: number) {
        super(a, b);
        this.c = c;
    }

    getC(): number {
        return this.c;
    }
}

class L3 extends L2 {
    d: number;

    constructor(a: number, b: number, c: number, d: number) {
        super(a, b, c);
        this.d = d;
    }

    total(): number {
        return this.a + this.b + this.c + this.d;
    }
}

// G11.6 additive evidence (arksteed-g116-call-closure-and-additive-evidence#C1,#C3):
// every compiled target of this leaf keeps its own compile/entry counters;
// the super-call form is annotated in the derived constructor's own dump and
// the actual construction form in the compiled same-domain caller's dump.
// The deliberately compiled class roles and all printed output are unchanged.
//! COMPILE_COUNT_GE L0 1
//! EXEC_COUNT_GE *L0 1
//! COMPILE_COUNT_GE L1 1
//! EXEC_COUNT_GE *L1 1
//! COMPILE_COUNT_GE L2 1
//! EXEC_COUNT_GE *L2 1
//! COMPILE_COUNT_GE L3 1
//! EXEC_COUNT_GE *L3 1
//! COMPILE_COUNT_GE probeInheritanceConstruction 1
//! EXEC_COUNT_GE *probeInheritanceConstruction 1
//! DEOPT_COUNT *Deoptimize* 0
//! METHOD L1
//! HAS CallRuntime OptSuperCall
//! METHOD L2
//! HAS CallRuntime OptSuperCall
//! METHOD L3
//! HAS CallRuntime OptSuperCall
//! METHOD probeInheritanceConstruction
//! HAS CallRuntime OptNewObjRange

ArkTools.arkSteedCompileSync(L0);
ArkTools.waitJitCompileFinish(L0);
ArkTools.arkSteedCompileSync(L1);
ArkTools.waitJitCompileFinish(L1);
ArkTools.arkSteedCompileSync(L2);
ArkTools.waitJitCompileFinish(L2);
ArkTools.arkSteedCompileSync(L3);
ArkTools.waitJitCompileFinish(L3);


function probeInheritanceConstruction() {
    return new L3(1, 2, 3, 4);
}
ArkTools.arkSteedCompileSync(probeInheritanceConstruction);
ArkTools.waitJitCompileFinish(probeInheritanceConstruction);

let obj = new L3(1, 2, 3, 4);
print(obj.getA());
print(obj.getB());
print(obj.getC());
print(obj.total());

// Bounded silent preparation: the leaf's construction(s) once more through the
// compiled same-domain caller. It prints nothing and changes no earlier observation.
probeInheritanceConstruction();
