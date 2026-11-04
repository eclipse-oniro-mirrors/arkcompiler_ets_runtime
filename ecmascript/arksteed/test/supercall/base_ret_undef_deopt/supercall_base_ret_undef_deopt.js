/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * Lazy deopt at the raw super call while the compiled base super returns a raw
 * undefined: the parked frame's return address is patched, the ctor runs to
 * completion under the compiled caller, and the deopt trampoline's base
 * continuation must select the bound this (published as the CONSTRUCTOR_THIS
 * pseudo vreg) for the raw undefined acc instead of leaking it into the
 * interpreter frame's acc. The proto-chain mutation inside the ctor is the
 * invalidation trigger, mirroring the d_plus_e shape.
 */

//! PARAMS --compiler-jit-hotness-threshold=60000

//! METHOD F
//! HAS         FastSuperAllocateThis
//! HAS         CallBaseConstructor
//! HAS         CallDerivedConstructor
//! COUNT       CallRuntime OptSuperCall 1

let mode = 0;

class PU {
    constructor() {
        if (mode >= 1) {
            PU.prototype.x = 2; // ADD x to PU.prototype (shape change): invalidates F's
                                // inlined proto-chain load mid-call, like d_plus_e
            return undefined;   // the raw value the base continuation must absorb
        }
        return {};
    }
}

class F extends PU {
    constructor() {
        let dep = obj.x;
        super();
        this.tag = dep;
    }
}

Object.prototype.x = 1; // dep's compile-time holder: deep in obj's proto chain
// not new F(): the constructor reads obj, which would still be in TDZ here
let obj = Object.create(F.prototype);

const REP = 60000;
for (let i = 0; i < REP; i++) {
    new F();
}

ArkTools.arkSteedCompileSync(PU);
ArkTools.arkSteedCompileSync(F);

mode = 1;
const f = new F();
print("base-undef-deopt: " + (f instanceof F) + " " + f.tag +
      " " + !ArkTools.arkSteedIsCompiled(F));
