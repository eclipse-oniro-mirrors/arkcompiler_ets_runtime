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
 * Primitive returns from the super constructor through the compiled fast path.
 * The [[Construct]] result normalization is inlined in the CallConstructor
 * codegen (EcmaObject return wins, everything else falls through to the cold
 * stub tail call), so the cold arm keeps the stub's thisObj-selection semantics:
 *   - base super constructor returning a primitive -> the allocated this is kept
 *   - derived super constructor returning a primitive -> TypeError at the super()
 * Note: the cold stub throws ThrowNonConstructorException ("function is
 * non-constructor"); the interpreted callee tail words the derived case
 * differently ("Derived constructor must return object or undefined"). The
 * compiled-caller wording is the pre-existing stub behaviour (the old JIT's
 * typed path calls the same stub) and is what is locked here.
 * The newTarget admission requires the proto-or-hclass slot to already hold the
 * instance hclass; the slot only transitions from the prototype object on the
 * first allocation through that newTarget. Only the throwing classes are
 * constructed once (interpreted) before compiling, so the locked wording comes
 * from the compiled path; the base-role classes' first compiled execution instead
 * takes the self-healing OptSuperCall slow arm, with the same observable
 * result.
 */

//! METHOD DNum
//! HAS         CallBaseConstructor
//! HAS         CallDerivedConstructor
//! HAS_NOT     CallCommonStub ConstructorCheck
//! HAS         FastSuperAllocateThis
//! COUNT       CallRuntime OptSuperCall 1

//! METHOD D7
//! HAS         CallBaseConstructor
//! HAS         CallDerivedConstructor
//! HAS_NOT     CallCommonStub ConstructorCheck
//! COUNT       CallRuntime OptSuperCall 1

// base supers returning primitives: the result is replaced by the allocated this
class Num { constructor() { return 5; } }
class Str { constructor() { return "prim"; } }
class Nul { constructor() { return null; } }
class DNum extends Num { constructor() { super(); this.tag = "num"; } }
class DStr extends Str { constructor() { super(); this.tag = "str"; } }
class DNul extends Nul { constructor() { super(); this.tag = "nul"; } }

// derived supers returning primitives: the super() call itself throws
class Base { constructor() {} }
class Mid7 extends Base { constructor() { super(); return 7; } }
class MidNul extends Base { constructor() { super(); return null; } }
class D7 extends Mid7 { constructor() { super(); this.tag = "never"; } }
class DNulMid extends MidNul { constructor() { super(); this.tag = "never"; } }

try { new D7(); } catch (e) { /* derived ctor returning a primitive throws */ }
try { new DNulMid(); } catch (e) { /* derived ctor returning a primitive throws */ }

ArkTools.arkSteedCompileSync(DNum);
ArkTools.arkSteedCompileSync(DStr);
ArkTools.arkSteedCompileSync(DNul);
ArkTools.arkSteedCompileSync(Mid7);
ArkTools.arkSteedCompileSync(MidNul);
ArkTools.arkSteedCompileSync(D7);
ArkTools.arkSteedCompileSync(DNulMid);

const dnum = new DNum();
print("base-num: " + (dnum instanceof DNum) + " " + dnum.tag);
const dstr = new DStr();
print("base-str: " + (dstr instanceof DStr) + " " + dstr.tag);
const dnul = new DNul();
print("base-null: " + (dnul instanceof DNul) + " " + dnul.tag);

function attempt(C, label) {
    try {
        const r = new C();
        print(label + ": no-throw " + (r instanceof C));
    } catch (e) {
        print(label + ": caught " + (e instanceof TypeError) + " " + e.message);
    }
}
attempt(D7, "derived-num");
attempt(DNulMid, "derived-null");
