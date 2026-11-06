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
 * Result tails of the FORWARDALLARGS arms, mirroring spread_ret_primitive:
 * a base super's primitive return keeps the bound this (the inline select
 * after the call), an object return wins; a derived chain's primitive
 * return is resolved to this inside the callee's own return tail, so the
 * derived arm just forwards whatever comes back.
 */

//! METHOD RP
//! HAS         AllocateInYoung
//! HAS         CallBaseConstructorForwardAllArgs
//! COUNT       CallRuntime OptSuperCall* 1

//! METHOD RD
//! HAS         CallDerivedConstructorForwardAllArgs
//! COUNT       CallRuntime OptSuperCall* 1

class RetBase {
    constructor(mode) {
        if (mode === "prim") {
            return 7;
        }
        if (mode === "obj") {
            return { marker: "obj" };
        }
    }
}

class RetDerived extends RetBase {
    constructor(mode) {
        super(mode);
    }
}

class RP extends RetBase {
}

class RD extends RetDerived {
}

ArkTools.arkSteedCompileSync(RetBase);
ArkTools.arkSteedCompileSync(RetDerived);
ArkTools.arkSteedCompileSync(RP);
ArkTools.arkSteedCompileSync(RD);

// base + primitive return: thisObj wins
const p = new RP("prim");
print("base-prim: " + (p instanceof RP));

// base + object return: the object wins
const o = new RP("obj");
print("base-obj: " + (o instanceof RP) + " " + o.marker);

// derived + primitive return: the callee's return tail resolves it to this
const dp = new RD("prim");
print("derived-prim: " + (dp instanceof RD));

// derived + object return: passes through
const d = new RD("obj");
print("derived-obj: " + (d instanceof RD) + " " + d.marker);
