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
 * Lazy deoptimization at the spread super-call site, mirroring
 * generic_deopt for SUPERCALLTHISRANGE: the compiled method holds a
 * prototype-chain dependency from the pre-super load, the interpreted base
 * constructor invalidates it while the compiled frame is on-stack, the
 * return address is patched to LazyDeoptEntry, and the continuation
 * normalizes the raw acc (base: object? result : this) with the resume pc
 * after the bytecode. The this binding, argument delivery and the
 * pre-super load's value must be indistinguishable from the compiled
 * execution.
 */

//! METHOD DP
//! HAS         CallBaseConstructorSpread
//! HAS         AllocateInYoung
//! HAS         BranchIfFastSpreadArray
//! COUNT       CallRuntime SuperCallSpreadWithNewTarget 1
//! DEOPT_COUNT Lazy Deoptimize* 1

class DepRoot {
}

class DepMid extends DepRoot {
}

DepRoot.prototype.x = 1;
let depObj = Object.create(DepMid.prototype);

let evil = false;

class DepBase {
    constructor(a) {
        // the deopt trigger: mutating the prototype chain of a compiled
        // method's dependency invalidates its compiled code while it is
        // on-stack
        if (evil) {
            DepMid.prototype.x = 2;
        }
        this.a = a;
    }
}

class DP extends DepBase {
    constructor(arr) {
        let dep = depObj.x;
        super(...arr);
        this.mark = dep === 1 ? "dp" : "dp2";
    }
}

const REP = 60000;
for (let i = 0; i < REP; i++) {
    new DP([i]);
}

ArkTools.arkSteedCompileSync(DP);

evil = true;
const r = new DP([41]);
print("deopt: " + r.a + " " + r.mark + " " + (r instanceof DP) + " " + !ArkTools.arkSteedIsCompiled(DP));

evil = false;
const r2 = new DP([99]);
print("after: " + r2.a + " " + r2.mark + " " + ArkTools.arkSteedIsCompiled(DP));
