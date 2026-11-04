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
 * A D settlement whose main CallConstructor took the generic arm (the super
 * constructor stays interpreted, so the callee normalizes its primitive
 * return before returning). The trampoline's ConstructorCheck continuation
 * must be idempotent on the already-normalized object and resume past the
 * supercall; the mid-call invalidation uninstalls the caller's code.
 */

//! METHOD FInt
//! HAS         CallBaseConstructor
//! HAS         CallDerivedConstructor

class Root {
}

class Mid extends Root {
}

Root.prototype.x = 1;
let obj = Object.create(Mid.prototype);

let mode = 0;

class PInt {
    constructor() {
        if (mode >= 1) {
            Mid.prototype.x = 2;
        }
        return 42;
    }
}

class FInt extends PInt {
    constructor() {
        let dep = obj.x;
        super();
        this.tag = dep === 1 ? "int" : "other";
    }
}

new FInt();
ArkTools.arkSteedCompileSync(FInt);

mode = 1;
const fi = new FInt();
print("generic-deopt: " + (fi instanceof FInt) + " " + fi.tag + " " + !ArkTools.arkSteedIsCompiled(FInt));
