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
 * A D settlement whose ConstructorCheck continuation throws. The compiled
 * super constructor mutates the prototype chain mid-[[Construct]] (the caller
 * has an obj.x dependency, so its return address is patched to the lazy-deopt
 * trampoline while the raw call is on the stack) and then returns a
 * primitive. The D resume leaves pc at the supercall and publishes the ctor
 * continuation; the trampoline's ConstructorCheck throws for the derived
 * primitive return, switches to the exception settlement, and the never-
 * executed catch runs. The invalidation uninstalls the caller's code.
 */

//! PARAMS --compiler-jit-hotness-threshold=60000

//! METHOD Leaf
//! HAS         CallBaseConstructor
//! HAS         CallDerivedConstructor

class Root {
}

let evil = false;

class Mid extends Root {
    constructor() {
        super();
        if (evil) {
            Mid.prototype.x = 99;
            return 7;
        }
    }
}

class Leaf extends Mid {
    constructor() {
        let dep = obj.x;
        try {
            super();
        } catch (e) {
            return {caught: e instanceof TypeError, dep: dep};
        }
    }
}

Root.prototype.x = 1;
// not new Leaf(): the constructor reads obj, which would still be in TDZ here
let obj = Object.create(Leaf.prototype);

const REP = 60000;
for (let i = 0; i < REP; i++) {
    new Leaf();
}

ArkTools.arkSteedCompileSync(Mid);
ArkTools.arkSteedCompileSync(Leaf);

evil = true;
const r = new Leaf();
print("d-cont-throw: " + r.caught + " " + !ArkTools.arkSteedIsCompiled(Leaf));
