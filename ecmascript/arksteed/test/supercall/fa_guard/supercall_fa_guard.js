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
 * Guard and newTarget-admission paths of the FORWARDALLARGS structure. The
 * super call target is read from the constructor's [[Prototype]] at call
 * time, so repointing it after compilation to a callable that is not a
 * constructor (Function.prototype, an arrow) must surface the interpreter's
 * TypeError from the compiled default constructor too - whichever arm the
 * in-frame guards pick, the not-a-constructor verdict comes from the
 * [[Construct]] entry. Reflect.construct with a foreign newTarget forces the
 * admission slow path: this is allocated from the newTarget's hclass, so the
 * instance lands on its prototype chain while only the written constructor
 * body runs.
 */

//! METHOD LeafG
//! HAS         AllocateInYoung
//! HAS         CallBaseConstructorForwardAllArgs
//! HAS         CallDerivedConstructorForwardAllArgs
//! COUNT       CallRuntime OptSuperCall* 1

class BaseG {
    constructor(a) {
        this.a = a;
    }
}

class LeafG extends BaseG {
}

class MutBase {
}

ArkTools.arkSteedCompileSync(BaseG);
ArkTools.arkSteedCompileSync(LeafG);
ArkTools.arkSteedCompileSync(MutBase);

print("plain: " + new LeafG(3).a);

const alt = class AltG {
    constructor() {
        this.via = "alt";
    }
};
const r = Reflect.construct(LeafG, [42], alt);
print("reflect: " + r.a + " " + (r instanceof alt) + " " + r.via);

class Mut1 extends MutBase {
}
ArkTools.arkSteedCompileSync(Mut1);
Object.setPrototypeOf(Mut1, Function.prototype);
try {
    new Mut1();
    print("mut1: no throw");
} catch (e) {
    print("mut1: " + (e instanceof TypeError));
}

const arrow = () => {};
class Mut2 extends MutBase {
}
ArkTools.arkSteedCompileSync(Mut2);
Object.setPrototypeOf(Mut2, arrow);
try {
    new Mut2();
    print("mut2: no throw");
} catch (e) {
    print("mut2: " + (e instanceof TypeError));
}

print("still: " + new LeafG(4).a);
