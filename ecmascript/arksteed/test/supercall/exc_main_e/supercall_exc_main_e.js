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
 * An exception thrown by the compiled super constructor body itself, with a
 * never-executed catch around the super() in the compiled caller: the main
 * CallConstructor call site carries the lazy-deopt kind, so the exception
 * must deoptimize the frame and dispatch into the (interpreted) catch.
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
            throw new RangeError("mid-throws");
        }
    }
}

class Leaf extends Mid {
    constructor() {
        try {
            super();
        } catch (e) {
            return {caught: e instanceof RangeError};
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
const r = new Leaf();
print("main-e: " + r.caught);
