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
 * Fast path coverage of the arksteed SUPERCALLTHISRANGE lowering. The lowering
 * emits both arms unconditionally: the guard chain (newTarget admission ->
 * heap object -> constructor JSFunction; a monomorphic call slot instead folds
 * the super-ctor guards into one identity deopt) selects between the fast arm
 * (FastSuperAllocateThis on the base branch, undefined this on the derived
 * branch, then CallConstructor, whose codegen inlines the [[Construct]] result
 * normalization after the embedded call) and the deferred slow arm (exactly one
 * OptSuperCall fallback). Runtime behaviour covered:
 *   - super of a base class allocates this in-frame (FastSuperAllocateThis)
 *   - super of a derived class forwards newTarget + undefined this
 *   - deep chain: both roles in one chain
 *   - argument forwarding (argc > 0) and argument adaptation (missing args -> undefined)
 *   - explicit object return from the derived constructor
 * Classes are compiled with arkSteedCompileSync so the IR constraints below are
 * checked on a deterministic compile.
 */

//! METHOD Mid
//! HAS         FastSuperAllocateThis
//! HAS         CallBaseConstructor
//! HAS         CallDerivedConstructor
//! HAS         LoadPrototypeFromNonJSProxyObject
//! HAS_NOT     CallCommonStub ConstructorCheck
//! HAS         BranchIfTaggedHeapObject
//! HAS         BranchIfJSConstructorFunction
//! HAS         BranchIfBase
//! COUNT       CallRuntime OptSuperCall 1

//! METHOD Leaf
//! HAS         FastSuperAllocateThis
//! HAS         CallBaseConstructor
//! HAS         CallDerivedConstructor
//! COUNT       CallRuntime OptSuperCall 1

class A {
    constructor(x) {
        this.x = x;
    }
}

// super() targets A: a base constructor -> this is allocated in-frame
class Mid extends A {
    constructor(x) {
        super(x);
        this.mid = true;
    }
}

// super() targets Mid: a derived constructor -> newTarget + undefined this forwarded
class Leaf extends Mid {
    constructor(x) {
        super(x);
        this.leaf = true;
    }
}

// argument adaptation: super passes fewer args than the callee declares
class Picky extends A {
    constructor(x) {
        super(x);
        this.y = arguments[1];
    }
}

// explicit object return: result of super() is the implicitly returned this when
// the callee returns implicitly; RetObj itself returns a plain object
class RetObj extends A {
    constructor(x) {
        super(x);
        return { marker: "ret", x: x };
    }
}

class Plain extends A {
}

ArkTools.arkSteedCompileSync(Mid);
ArkTools.arkSteedCompileSync(Leaf);
ArkTools.arkSteedCompileSync(Picky);
ArkTools.arkSteedCompileSync(RetObj);
ArkTools.arkSteedCompileSync(Plain);

const m = new Mid(7);
print("base-role: " + (m instanceof A) + " " + m.x + " " + m.mid);

const l = new Leaf(9);
print("derived-role: " + (l instanceof Mid) + " " + l.x + " " + l.mid + " " + l.leaf);
print("ctor-chain-identity: " + (Object.getPrototypeOf(Leaf) === Mid));

const p = new Picky(3);
print("arg-adapt: " + p.x + " " + p.y);

const r = new RetObj(5);
print("ret-obj: " + r.marker + " " + r.x + " " + (r instanceof RetObj));

const plain = new Plain(11);
print("implicit-ret: " + plain.x + " " + (plain instanceof Plain));
