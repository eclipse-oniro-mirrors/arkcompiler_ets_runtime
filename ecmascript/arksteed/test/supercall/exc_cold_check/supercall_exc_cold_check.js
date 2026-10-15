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
 * A never-executed catch around a super() whose derived super constructor
 * returns a primitive. The compiled fast path routes the raw primitive into
 * the cold ConstructorCheck stub, which throws the TypeError for the derived
 * return; the exception must surface through the compiled frame's own
 * (deoptimized) catch handler instead of escaping it.
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
            return 7;
        }
    }
}

let caughtMsg = "unreached";

class Leaf extends Mid {
    constructor() {
        // locals only: `this` is unbound until a super() succeeds
        try {
            super();
        } catch (e) {
            caughtMsg = "caught:" + (e instanceof TypeError);
        }
    }
}

const REP = 60000;
for (let i = 0; i < REP; i++) {
    new Leaf();
}

ArkTools.arkSteedCompileSync(Mid);
ArkTools.arkSteedCompileSync(Leaf);

evil = true;
let escaped = "none";
try {
    new Leaf();
} catch (e) {
    escaped = e.name;
}
// With the catch swallowing the TypeError, the constructor falls through to its
// implicit `return this`, whose this binding is still uninitialized — the escaped
// ReferenceError is spec behavior, not a failure.
print("cold-check: " + caughtMsg + " " + escaped);
