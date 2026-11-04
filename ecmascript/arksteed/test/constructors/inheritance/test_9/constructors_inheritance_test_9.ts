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
    a: number;

    constructor(a: number) {
        this.a = a;
    }

    getValue(): number {
        return this.a;
    }
}

class Left extends Base {
    b: number;

    constructor(a: number, b: number) {
        super(a);
        this.b = b;
    }

    getAB(): number {
        return this.a + this.b;
    }
}

class Right extends Base {
    c: number;

    constructor(a: number, c: number) {
        super(a);
        this.c = c;
    }

    getAC(): number {
        return this.a + this.c;
    }
}

class Combined extends Left {
    d: number;

    constructor(a: number, b: number, c: number, d: number) {
        super(a, b);
        this.c_value = c;
        this.d = d;
    }

    c_value: number;

    getABCD(): number {
        return this.a + this.b + this.c_value + this.d;
    }
}

// G11.6 additive evidence (arksteed-g116-call-closure-and-additive-evidence#C1,#C3):
// every compiled target of this leaf keeps its own compile/entry counters;
// the super-call form is annotated in the derived constructor's own dump and
// the actual construction form in the compiled same-domain caller's dump.
// The deliberately compiled class roles and all printed output are unchanged.
//! COMPILE_COUNT_GE Base 1
//! EXEC_COUNT_GE *Base 1
//! COMPILE_COUNT_GE Left 1
//! EXEC_COUNT_GE *Left 1
//! COMPILE_COUNT_GE Right 1
//! EXEC_COUNT_GE *Right 1
//! COMPILE_COUNT_GE Combined 1
//! EXEC_COUNT_GE *Combined 1
//! COMPILE_COUNT_GE probeInheritanceConstruction 1
//! EXEC_COUNT_GE *probeInheritanceConstruction 1
//! DEOPT_COUNT *Deoptimize* 0
//! METHOD Left
//! HAS CallRuntime OptSuperCall
//! METHOD Right
//! HAS CallRuntime OptSuperCall
//! METHOD Combined
//! HAS CallRuntime OptSuperCall
//! METHOD probeInheritanceConstruction
//! HAS CallRuntime OptNewObjRange

ArkTools.arkSteedCompileSync(Base);
ArkTools.waitJitCompileFinish(Base);
ArkTools.arkSteedCompileSync(Left);
ArkTools.waitJitCompileFinish(Left);
ArkTools.arkSteedCompileSync(Right);
ArkTools.waitJitCompileFinish(Right);
ArkTools.arkSteedCompileSync(Combined);
ArkTools.waitJitCompileFinish(Combined);


function probeInheritanceConstruction() {
    return [new Left(1, 2), new Right(1, 3), new Combined(1, 2, 3, 4)];
}
ArkTools.arkSteedCompileSync(probeInheritanceConstruction);
ArkTools.waitJitCompileFinish(probeInheritanceConstruction);

let obj = new Combined(1, 2, 3, 4);
print(obj.getValue());
print(obj.getAB());
print(obj.getABCD());

// Bounded silent preparation: the leaf's construction(s) once more through the
// compiled same-domain caller. It prints nothing and changes no earlier observation.
probeInheritanceConstruction();
