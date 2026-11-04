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
 * Lazy deopt at the derived supercall with a LIVE proto-chain dependency:
 * `dep` is consumed after super() so the obj.x load survives DCE and the
 * compiled FDer keeps a registered dependency on Mid.prototype. PDer mutates
 * that chain mid-[[Construct]] and returns a primitive, so the caller's
 * return address is patched to the lazy-deopt trampoline and the derived
 * continuation stub throws on the raw 7. This is the only test whose
 * deopt lands on the supercall safepoint with a primitive result — the
 * other ret_primitive variants DCE the dependency and throw from the
 * compiled cold path instead.
 */

//! METHOD FDer
//! HAS CallDerivedConstructor

class Root {}
Root.prototype.x = 1;
class Mid extends Root {}
class Leaf extends Mid {}
const obj = new Leaf();

let mode = 0;

class PBase { constructor() {} }
class PDer extends PBase {
    constructor() {
        super();
        if (mode >= 1) {
            Mid.prototype.x = 2;   // invalidate FDer's live obj.x dependency mid-[[Construct]]
            return 7;              // primitive return: derived continuation must throw
        }
    }
}

class FDer extends PDer {
    constructor() {
        let dep = obj.x;           // LIVE: consumed after super() -> survives DCE -> registers dep
        super();
        this.tag = dep;            // (the original test's dep was unused -> DCE -> path never fired)
    }
}

// warmup, quiet mode
for (let i = 0; i < 3; i++) { new FDer(); }
ArkTools.arkSteedCompileSync(PDer);
ArkTools.arkSteedCompileSync(FDer);
print("compiled=" + ArkTools.arkSteedIsCompiled(FDer));

mode = 1;
try {
    new FDer();
    print("BUG: no throw");
} catch (e) {
    print("caught " + (e instanceof TypeError) + " msg=" + e.message +
          " decompiled=" + !ArkTools.arkSteedIsCompiled(FDer));
}
print("done");
