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
 * Double trigger: the caller's obj.x dependency is invalidated mid-
 * [[Construct]] (the parked frame's return address is patched to the
 * lazy-deopt trampoline) and the super constructor then throws instead of
 * returning. The trampoline must observe the pending exception before the
 * ConstructorCheck continuation, settle as E at the parked main-call pc, and
 * run the never-executed catch interpreted; the invalidation still
 * uninstalls the caller's code.
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
            throw new RangeError("boom");
        }
    }
}

class Leaf extends Mid {
    constructor() {
        let dep = obj.x;
        try {
            super();
        } catch (e) {
            return {caught: e instanceof RangeError, dep: dep};
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
print("d-plus-e: " + r.caught + " " + r.dep + " " + !ArkTools.arkSteedIsCompiled(Leaf));
