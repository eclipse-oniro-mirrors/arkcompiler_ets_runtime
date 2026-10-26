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
 * Primitive and object returns from a COMPILED super through the direct arm of
 * the CallConstructor fast path: the caller calls the super's code entry raw,
 * so the inline [[Construct]] result normalization decides on the raw value.
 * ret_primitive covers the same returns with uncompiled base supers (its
 * derived-primitive supers are compiled), where the
 * generic arm's trampoline pre-normalizes and the tree only confirms.
 *   - base super returning a primitive -> the allocated this is kept
 *   - base super returning undefined -> the allocated this is kept
 *   - base super returning an object -> the object wins over the allocated this
 *   - derived super returning a primitive -> TypeError at the super()
 */

class BInt {
    constructor() {
        return 42;
    }
}

class BUndef {
    constructor() {
        return undefined;
    }
}

class BObj {
    constructor() {
        return { replaced: 1 };
    }
}

class BBase {
    constructor() {
    }
}

class BDer extends BBase {
    constructor() {
        super();
        return 7;
    }
}

class DInt extends BInt {
    constructor() {
        super();
        this.tag = "int";
    }
}

class DUndef extends BUndef {
    constructor() {
        super();
        this.tag = "undef";
    }
}

class DObj extends BObj {
    constructor() {
        super();
        this.tag = "obj";
    }
}

class DDer extends BDer {
    constructor() {
        super();
        this.tag = "never";
    }
}

new DInt();
new DUndef();
new DObj();
try { new DDer(); } catch (e) { /* derived ctor returning a primitive throws */ }

ArkTools.arkSteedCompileSync(BInt);
ArkTools.arkSteedCompileSync(BUndef);
ArkTools.arkSteedCompileSync(BObj);
ArkTools.arkSteedCompileSync(BDer);
ArkTools.arkSteedCompileSync(DInt);
ArkTools.arkSteedCompileSync(DUndef);
ArkTools.arkSteedCompileSync(DObj);
ArkTools.arkSteedCompileSync(DDer);

const di = new DInt();
print("direct-int: " + (di instanceof DInt) + " " + di.tag);
const du = new DUndef();
print("direct-undef: " + (du instanceof DUndef) + " " + du.tag);
const dobj = new DObj();
print("direct-obj: " + (dobj instanceof DObj) + " " + dobj.replaced + " " + dobj.tag);
try {
    new DDer();
    print("direct-derived: no-throw");
} catch (e) {
    print("direct-derived: caught " + (e instanceof TypeError) + " " + e.message);
}
