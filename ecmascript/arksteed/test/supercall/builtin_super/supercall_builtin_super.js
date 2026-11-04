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
 * super() of builtin constructors. Array / Error / Map / Date subclasses keep
 * the site on the OptSuperCall fallback: their instance hclass is not
 * JS_OBJECT, so the newTarget admission rejects every call and the
 * [[Construct]] semantics must come from the runtime (the in-frame
 * FastSuperAllocateThis allocation only covers plain objects):
 *   - Array: elements written in the derived frame survive
 *   - Error: message set by the builtin is visible through this.message-less
 *     accessors (error.message comes from the prototype getter)
 *   - Map: instance methods usable immediately after super()
 *   - Date: the builtin-provided this must not be replaced (Date pins
 *     [[Construct]] to a fresh instance)
 * An Object subclass produces plain JS_OBJECT instances, so its site is the
 * one builtin super the admission keeps on the fast arm: the CallConstructor
 * vertex enters the never-compiled builtin through the generic [[Construct]]
 * entry with the caller's newTarget.
 */

//! METHOD ExtArray
//! HAS         CallBaseConstructor
//! HAS         CallDerivedConstructor
//! COUNT       CallRuntime OptSuperCall 1

//! METHOD ExtObject
//! HAS         CallBaseConstructor
//! HAS         CallDerivedConstructor
//! COUNT       CallRuntime OptSuperCall 1

class ExtArray extends Array {
    constructor() {
        super();
        this[0] = 11;
        this[2] = 33;
    }
}

class ExtError extends Error {
    constructor(msg) {
        super(msg);
        this.ext = true;
    }
}

class ExtMap extends Map {
    constructor(pairs) {
        super(pairs);
    }
}

class ExtDate extends Date {
    constructor(ms) {
        super(ms);
    }
}

class ExtObject extends Object {
    constructor() {
        super();
        this.x = 7;
    }
}

ArkTools.arkSteedCompileSync(ExtArray);
ArkTools.arkSteedCompileSync(ExtError);
ArkTools.arkSteedCompileSync(ExtMap);
ArkTools.arkSteedCompileSync(ExtDate);
new ExtObject();
ArkTools.arkSteedCompileSync(ExtObject);

const a = new ExtArray();
print("array: " + a.length + " " + a[0] + " " + a[1] + " " + a[2] + " " + (a instanceof ExtArray));

const e = new ExtError("boom");
print("error: " + (e instanceof ExtError) + " " + (e instanceof Error) + " " + e.message + " " + e.ext);

const m = new ExtMap([["k", 42]]);
print("map: " + m.size + " " + m.get("k") + " " + (m instanceof Map));

const d = new ExtDate(86400000);
print("date: " + d.getTime() + " " + (d instanceof ExtDate) + " " + (d instanceof Date));

const o = new ExtObject();
print("object: " + o.x + " " + (o instanceof ExtObject) + " " + (o instanceof Object));
