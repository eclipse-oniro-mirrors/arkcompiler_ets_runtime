/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under an applicable law or distributed under the License is
 * distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
 * either express or implied. See the License for the specific language governing
 * permissions and limitations under the License.
 */

/*
 * Fast-path structure for SUPERCALLSPREAD, mirroring fast_path for
 * SUPERCALLTHISRANGE. The spread operand is admitted by
 * BranchIfFastSpreadArray (stable tagged JSArray + clean Array.prototype
 * context), then the ordinary super-call structure applies: BranchIfBase
 * splits, the base arm allocates this in-frame, the derived arm forwards
 * an undefined placeholder. The slow block is a single SuperCallSpread
 * runtime call (the full iterator protocol lives inside it).
 *
 * Behavioral checks: argument contents reach the callee across spread
 * arrays of length 0/1/many and rest-parameter arrays; argument adaptation
 * fills missing positional parameters with undefined.
 */

//! METHOD MidS
//! HAS         AllocateInYoung
//! HAS         CallBaseConstructorSpread
//! HAS         CallDerivedConstructorSpread
//! HAS         BranchIfFastSpreadArray
//! HAS         LoadPrototypeFromNonJSProxyObject
//! HAS_NOT     CallCommonStub ConstructorCheck
//! HAS_NOT     CallCommonStub GetCallSpreadArgs
//! HAS         BranchIfTaggedHeapObject
//! HAS         BranchIfJSConstructorFunction
//! HAS         BranchIfBase
//! COUNT       CallRuntime SuperCallSpreadWithNewTarget 1

//! METHOD LeafS
//! HAS         AllocateInYoung
//! HAS         CallBaseConstructorSpread
//! HAS         CallDerivedConstructorSpread
//! HAS         BranchIfFastSpreadArray
//! COUNT       CallRuntime SuperCallSpreadWithNewTarget 1

class Base {
    constructor(a, b, c) {
        this.a = a;
        this.b = b;
        this.c = c;
    }
}

class MidS extends Base {
    constructor(arr) {
        super(...arr);
        this.via = "mid";
    }
}

class LeafS extends MidS {
    constructor(...xs) {
        super(...xs);
    }
}

class RestBase {
    constructor(...xs) {
        this.sum = xs.reduce((acc, x) => acc + x, 0);
        this.n = xs.length;
    }
}

class RestS extends RestBase {
    constructor(...xs) {
        super(...xs);
    }
}

ArkTools.arkSteedCompileSync(MidS);
ArkTools.arkSteedCompileSync(LeafS);
ArkTools.arkSteedCompileSync(RestS);

// zero-argument spread: the callee's positional params read undefined
const z = new MidS([]);
print("zero: " + z.a + " " + z.b + " " + z.c + " " + z.via);

// one-argument spread
const o = new MidS([11]);
print("one: " + o.a + " " + o.b + " " + o.c);

// full spread
const f = new MidS([1, 2, 3]);
print("full: " + f.a + " " + f.b + " " + f.c + " " + f.via);

// derived chain through spread (LeafS -> MidS -> Base, both spread sites compiled)
const l = new LeafS([7, 8, 9]);
print("chain: " + l.a + " " + l.b + " " + l.c);

// rest-parameter spread (a rest array is a fresh JSArray -> admitted)
const r0 = new RestS();
const r1 = new RestS(5);
const r3 = new RestS(1, 2, 3);
print("rest: " + r0.n + " " + r0.sum + " " + r1.n + " " + r1.sum + " " + r3.n + " " + r3.sum);

// spread with more arguments than the callee's formals: extras are observable
// through the arguments object but the positional reads stay correct
const m = new MidS([10, 20, 30, 40, 50]);
print("extra: " + m.a + " " + m.b + " " + m.c);
