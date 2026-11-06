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
 * Guard-failure entries of the spread fast path, mirroring slow_path for
 * SUPERCALLTHISRANGE. Mutating the class's [[Prototype]] to a
 * non-constructor after compilation routes the site through the
 * SuperCallSpread runtime slow block: the TypeError message and the
 * uncaught/caught behaviors must match the interpreter's.
 */

//! METHOD G
//! HAS         BranchIfFastSpreadArray
//! HAS         BranchIfTaggedHeapObject
//! HAS         BranchIfJSConstructorFunction
//! COUNT       CallRuntime SuperCallSpreadWithNewTarget 1

class Ok {
    constructor(v) {
        this.v = v;
    }
}

class G extends Ok {
    constructor(arr) {
        super(...arr);
    }
}

ArkTools.arkSteedCompileSync(G);

// warm the fast path first
const w = new G([1]);
print("warm: " + w.v);

// non-constructor prototype: TypeError through the slow block (uncaught)
Object.setPrototypeOf(G, {});
try {
    new G([2]);
    print("uncaught: NO THROW (bug)");
} catch (e) {
    print("uncaught: " + (e instanceof TypeError));
}

// null prototype: same slow-block TypeError, caught
Object.setPrototypeOf(G, null);
try {
    new G([3]);
    print("null: NO THROW (bug)");
} catch (e) {
    print("null: " + (e instanceof TypeError));
}

// restore a constructor prototype: the fast path resumes
Object.setPrototypeOf(G, Ok);
const r = new G([4]);
print("restored: " + r.v);
