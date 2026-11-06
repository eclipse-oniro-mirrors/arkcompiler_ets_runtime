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
 * Fast arms of the FORWARDALLARGS super call: the empty default constructor
 * of `class X extends B {}` compiles CALLRUNTIME_SUPERCALLFORWARDALLARGS to
 * the in-frame forwarding structure - ActualArgc drives the caller-frame
 * copy loop, BranchIfBase splits, the base arm allocates this in-frame and
 * the derived arm forwards an undefined placeholder, and a compiled callee
 * is entered through the steed call convention. Both arms of one site live
 * in the same graph; the runtime branch picks one, so both vertices are
 * expected in each method. The slow block stays the single
 * OptSuperCallForwardAllArgs runtime call.
 *
 * Behavioral checks: forwarded arguments reach the base constructor across
 * argc 0/1/3/more, a derived chain (Mid -> Leaf -> Base) forwards through
 * the derived arm, and instances keep their prototype links.
 */

//! METHOD LeafF
//! HAS         AllocateInYoung
//! HAS         BranchIfNewTargetHClassReady
//! HAS         CallBaseConstructorForwardAllArgs
//! HAS         CallDerivedConstructorForwardAllArgs
//! HAS         ActualArgc
//! HAS_NOT     CallBaseConstructor
//! HAS_NOT     CallDerivedConstructor
//! COUNT       CallRuntime OptSuperCall* 1

//! METHOD MidF
//! HAS         AllocateInYoung
//! HAS         CallBaseConstructorForwardAllArgs
//! HAS         CallDerivedConstructorForwardAllArgs
//! HAS         ActualArgc
//! HAS_NOT     CallBaseConstructor
//! HAS_NOT     CallDerivedConstructor
//! COUNT       CallRuntime OptSuperCall* 1

class BaseF {
    constructor(a, b, c) {
        this.a = a;
        this.b = b;
        this.c = c;
    }
}

class LeafF extends BaseF {
}

class MidF extends LeafF {
}

ArkTools.arkSteedCompileSync(BaseF);
ArkTools.arkSteedCompileSync(LeafF);
ArkTools.arkSteedCompileSync(MidF);

const z = new LeafF();
print("zero: " + z.a + " " + z.b + " " + z.c + " " + (z instanceof LeafF));

const o = new LeafF(11);
print("one: " + o.a + " " + o.b + " " + o.c);

const f = new LeafF(1, 2, 3);
print("full: " + f.a + " " + f.b + " " + f.c);

const m = new LeafF(10, 20, 30, 40, 50);
print("extra: " + m.a + " " + m.b + " " + m.c);

const d = new MidF(7, 8, 9);
print("chain: " + d.a + " " + d.b + " " + d.c + " " + (d instanceof MidF) + " " + (d instanceof LeafF));
