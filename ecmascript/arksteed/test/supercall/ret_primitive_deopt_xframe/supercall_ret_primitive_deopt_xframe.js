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
 * Cross-frame lazy deopt x exception on the super-call chain. Both Mid and
 * Leaf are arksteed-compiled; Mid's CallConstructor (into the compiled Bad)
 * is the patched suspension point, and Leaf's own CallConstructor (into the
 * compiled Mid) is patched as well when Bad invalidates the prototype-chain
 * dependencies of both frames. Bad is a derived constructor returning a
 * primitive, so Mid's lazy-deopt resume normalizes to a TypeError
 * (NormalizeLazyDeoptAcc throw arm): the pending exception is set with Mid's
 * interpreter frame resumed AT the supercall bytecode, the exception unwinds
 * out of Mid, crosses the invalidated-and-patched Leaf frame, and must be
 * caught by the try-catch INSIDE Leaf (this stays in TDZ after a failed
 * super, so the handler only records into an outer variable).
 *
 * - deopt happens: Leaf/Mid leave the compiled state (arkSteedIsCompiled false)
 * - Bad's eager deopt on the cold branch also uninstalls its compiled code
 * - inner catch wins: innerCaught carries the TypeError message, no exception
 *   escapes to the outer try-catch
 */

//! METHOD Mid
//! HAS         FastSuperAllocateThis
//! HAS         CallBaseConstructor
//! COUNT       CallRuntime OptSuperCall 1

//! METHOD Leaf
//! HAS         FastSuperAllocateThis
//! HAS         CallDerivedConstructor
//! COUNT       CallRuntime OptSuperCall 1

class Base {
    constructor() {
    }
}

// The offending super: derived + primitive return -> TypeError at Mid's
// lazy-deopt resume; also mutates the prototype chain (invalidating Mid and
// Leaf's dependencies read before their super calls).
class Bad extends Base {
    constructor() {
        super();
        if (mode >= 1) {
            Mid.prototype.x = 2;
            return 7;
        }
    }
}

class Mid extends Bad {
    constructor() {
        let dep = obj.x;
        super();
        this.tag = "mid";
    }
}

class Leaf extends Mid {
    constructor() {
        let dep = obj.x;
        try {
            super();
        } catch (e) {
            innerCaught = e.message;
        }
    }
}

Base.prototype.x = 1;
// obj's prototype chain is Mid.prototype -> ... -> Base.prototype, so
// Mid.prototype.x = 2 shadows the base slot and invalidates the obj.x
// dependencies; Object.create avoids running any constructor that reads obj.
let obj = Object.create(Mid.prototype);
let mode = 0;
let innerCaught = "none";

// Warm-up (interpreted) first so the obj.x loads in Mid/Leaf collect the
// prototype-chain feedback the compiled code installs dependencies on.
new Leaf();

ArkTools.arkSteedCompileSync(Bad);
ArkTools.arkSteedCompileSync(Mid);
ArkTools.arkSteedCompileSync(Leaf);

mode = 1;
let outerCaught = "none";
try {
    new Leaf();
} catch (e) {
    outerCaught = e.message;
}

print("inner: " + innerCaught + " outer: " + outerCaught);
print("leaf-deopted: " + !ArkTools.arkSteedIsCompiled(Leaf) +
      " mid-deopted: " + !ArkTools.arkSteedIsCompiled(Mid) +
      " bad-deopted: " + !ArkTools.arkSteedIsCompiled(Bad));
