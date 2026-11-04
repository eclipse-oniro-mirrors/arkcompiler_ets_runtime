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
 * The base arm's only live undefined path, end to end: a base super constructor
 * with an explicit `return undefined` emits the raw value in bytecode
 * (`ldundefined; return` — the frontend only rewrites implicit returns into
 * `lda a2; return`), so the compiled direct entry hands a raw undefined to the
 * caller and the inline base select must fall back to the bound this.
 * `return null` rides the same fallback arm.
 */

//! METHOD DU
//! HAS         FastSuperAllocateThis
//! HAS         CallBaseConstructor
//! HAS         CallDerivedConstructor
//! COUNT       CallRuntime OptSuperCall 1

//! METHOD DN
//! HAS         FastSuperAllocateThis
//! HAS         CallBaseConstructor
//! HAS         CallDerivedConstructor
//! COUNT       CallRuntime OptSuperCall 1

class PUndef { constructor() { return undefined; } }
class PNull { constructor() { return null; } }

class DU extends PUndef {
    constructor() { super(); this.via = "undef"; }
}
class DN extends PNull {
    constructor() { super(); this.via = "null"; }
}

new DU();
new DN();
ArkTools.arkSteedCompileSync(PUndef);
ArkTools.arkSteedCompileSync(PNull);
ArkTools.arkSteedCompileSync(DU);
ArkTools.arkSteedCompileSync(DN);

const du = new DU();
const dn = new DN();
print("base-undef: " + (du instanceof DU) + " " + du.via +
      " " + (dn instanceof DN) + " " + dn.via);
