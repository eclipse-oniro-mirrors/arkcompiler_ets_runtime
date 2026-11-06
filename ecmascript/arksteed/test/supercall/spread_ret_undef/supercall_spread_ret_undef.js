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
 * The base arm's only live undefined path, end to end over a spread
 * array: a base super constructor with an explicit `return undefined`
 * yields the bound this (the inline select falls back to thisObj). A
 * `return null` rides the same fallback arm.
 */

//! METHOD DU
//! HAS         CallBaseConstructorSpread
//! HAS         AllocateInYoung
//! HAS         BranchIfFastSpreadArray
//! COUNT       CallRuntime SuperCallSpreadWithNewTarget 1

//! METHOD DN
//! HAS         CallBaseConstructorSpread
//! HAS         AllocateInYoung
//! HAS         BranchIfFastSpreadArray
//! COUNT       CallRuntime SuperCallSpreadWithNewTarget 1

class PUndef {
    constructor(x) {
        if (x === undefined) {
            return undefined;
        }
        this.tag = x;
    }
}

class PNull {
    constructor(x) {
        if (x === null) {
            return null;
        }
        this.tag = x;
    }
}

class DU extends PUndef {
    constructor(arr) {
        super(...arr);
    }
}

class DN extends PNull {
    constructor(arr) {
        super(...arr);
    }
}

ArkTools.arkSteedCompileSync(DU);
ArkTools.arkSteedCompileSync(DN);

const du = new DU([]);
print("spread-undef: " + (du instanceof DU) + " " + du.tag);
const dn = new DN([null]);
print("spread-null: " + (dn instanceof DN) + " " + dn.tag);

// explicit return undefined with arguments spread — same fallback
const du2 = new DU([]);
print("spread-undef-again: " + (du2 instanceof DU));
