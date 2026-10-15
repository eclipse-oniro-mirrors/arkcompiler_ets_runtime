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

/*
 * Sites compiled after a warm construction take the speculation form: the
 * profiled super ctor is embedded as an identity constant and one eager
 * deopt (NOTCALLTGT1) replaces the heap-object / JS-constructor guards, so
 * the graph carries no BranchIfJSConstructorFunction:
 *   - WP: identity holds; the base super returns a primitive, so the result
 *     must normalize to the allocated this (the super stays uncompiled, so
 *     this runs through the pre-normalizing generic arm; the raw-call
 *     ctorIsBase arm is covered by ret_primitive_direct)
 *   - LU: identity holds; the derived super explicitly returns undefined,
 *     which must normalize to the callee-bound this (undefined arm)
 *   - W: identity misses after Object.setPrototypeOf swaps the [[Prototype]]
 *     (null / plain object / arrow / another base ctor); every miss must
 *     deopt and re-execute the supercall in the interpreter, raising the
 *     interpreter-identical TypeError or constructing through the new base
 */

//! METHOD WP
//! HAS_NOT     BranchIfJSConstructorFunction
//! HAS         FastSuperAllocateThis
//! HAS         CallBaseConstructor
//! HAS         CallDerivedConstructor
//! COUNT       CallRuntime OptSuperCall 1

//! METHOD LU
//! HAS_NOT     BranchIfJSConstructorFunction
//! HAS         FastSuperAllocateThis
//! HAS         CallBaseConstructor
//! HAS         CallDerivedConstructor
//! COUNT       CallRuntime OptSuperCall 1

//! METHOD W
//! HAS_NOT     BranchIfJSConstructorFunction
//! HAS         FastSuperAllocateThis
//! HAS         CallBaseConstructor
//! HAS         CallDerivedConstructor
//! COUNT       CallRuntime OptSuperCall 1

class A {
    constructor() {
    }
}

class P5 {
    constructor() {
        return 5;
    }
}

class WP extends P5 {
    constructor() {
        super();
        this.tag = "wp";
    }
}

class MU extends A {
    constructor() {
        super();
        return undefined;
    }
}

class LU extends MU {
    constructor() {
        super();
        this.t = 1;
    }
}

let C = A;

class Other {
    constructor() {
        this.o = 9;
    }
}

class W extends A {
    constructor() {
        Object.setPrototypeOf(W, C);
        super();
    }
}

new WP();
ArkTools.arkSteedCompileSync(WP);
const wp1 = new WP();
const wp2 = new WP();
print("base-prim: " + wp1.tag + " " + (wp1 instanceof WP) + " " + wp2.tag + " " + (wp2 instanceof WP));

new LU();
ArkTools.arkSteedCompileSync(MU);
ArkTools.arkSteedCompileSync(LU);
const lu = new LU();
print("ret-undefined: " + lu.t + " " + (lu instanceof LU) + " " + (lu instanceof MU));

new W();
ArkTools.arkSteedCompileSync(W);

function attempt(label) {
    try {
        return label + ": " + (new W() instanceof W);
    } catch (e) {
        return label + ": caught " + (e instanceof TypeError) + " | " + e.message;
    }
}

print(attempt("identity-holds"));

C = null;
print(attempt("null-proto"));

C = {};
print(attempt("object-proto"));

C = () => 1;
print(attempt("arrow-proto"));

C = Other;
const swapped = new W();
print("ctor-swap: " + (swapped instanceof W) + " " + swapped.o);
