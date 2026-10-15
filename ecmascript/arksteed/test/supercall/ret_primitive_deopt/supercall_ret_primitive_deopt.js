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
 * Lazy deopt at the CallConstructor raw call must normalize the raw super
 * return before it reaches acc. The derived constructors install a prototype
 * chain dependency (obj.x) before super(); the compiled base super mutates the
 * chain mid-[[Construct]], so the caller's return address is patched to the
 * lazy-deopt trampoline while the raw call is still on the stack. The resume
 * re-applies the ConstructorCheck tail to the raw return value:
 *   - base super returning a primitive -> the allocated this is kept
 *   - base super returning undefined -> the allocated this is kept
 *   - base super returning an object -> the object wins over the allocated this
 *   - derived super returning a primitive -> TypeError at the resume
 */

class Root {
}

class Mid extends Root {
}

class Leaf extends Mid {
}

Root.prototype.x = 1;
let obj = new Leaf();

// 0: quiet, 1: invalidate the chain, 2: invalidate and return a primitive
let mode = 0;

function resetChain()
{
    delete Mid.prototype.x;
    Root.prototype.x = 1;
}

class PInt {
    constructor() {
        if (mode >= 1) {
            Mid.prototype.x = 2;
        }
        return 42;
    }
}

class PUndef {
    constructor() {
        if (mode >= 1) {
            Mid.prototype.x = 2;
        }
        return undefined;
    }
}

class PObj {
    constructor() {
        if (mode >= 1) {
            Mid.prototype.x = 2;
        }
        return { replaced: 1 };
    }
}

class PBase {
    constructor() {
    }
}

class PDer extends PBase {
    constructor() {
        super();
        if (mode >= 2) {
            Mid.prototype.x = 2;
            return 7;
        }
    }
}

class FInt extends PInt {
    constructor() {
        let dep = obj.x;
        super();
        this.tag = "int";
    }
}

class FUndef extends PUndef {
    constructor() {
        let dep = obj.x;
        super();
        this.tag = "undef";
    }
}

class FObj extends PObj {
    constructor() {
        let dep = obj.x;
        super();
        this.tag = "obj";
    }
}

class FDer extends PDer {
    constructor() {
        let dep = obj.x;
        super();
        this.tag = "never";
    }
}

new FInt();
new FUndef();
new FObj();
new FDer();

ArkTools.arkSteedCompileSync(PInt);
ArkTools.arkSteedCompileSync(PUndef);
ArkTools.arkSteedCompileSync(PObj);
ArkTools.arkSteedCompileSync(PDer);
ArkTools.arkSteedCompileSync(FInt);
ArkTools.arkSteedCompileSync(FUndef);
ArkTools.arkSteedCompileSync(FObj);
ArkTools.arkSteedCompileSync(FDer);

mode = 1;
const fi = new FInt();
print("deopt-int: " + (fi instanceof FInt) + " " + fi.tag + " " + !ArkTools.arkSteedIsCompiled(FInt));
const fu = new FUndef();
print("deopt-undef: " + (fu instanceof FUndef) + " " + fu.tag + " " + !ArkTools.arkSteedIsCompiled(FUndef));
const fo = new FObj();
print("deopt-obj: " + (fo instanceof FObj) + " " + fo.replaced + " " + fo.tag + " " +
      !ArkTools.arkSteedIsCompiled(FObj));

mode = 2;
try {
    new FDer();
    print("deopt-derived: no-throw");
} catch (e) {
    print("deopt-derived: caught " + (e instanceof TypeError) + " " + e.message + " " +
          !ArkTools.arkSteedIsCompiled(FDer));
}
