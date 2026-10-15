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
 * The cold ConstructorCheck throw crossing two compiled frames. The
 * constructor has no catch of its own, so the pending exception returns
 * through it; the outer compiled function's new-call site carries a
 * never-executed catch, which must run after its own lazy-deopt settlement.
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

class Leaf extends Mid {
    constructor() {
        super();
    }
}

function outer()
{
    try {
        new Leaf();
        return "no-throw";
    } catch (e) {
        return "outer-caught:" + (e instanceof TypeError);
    }
}

const REP = 60000;
for (let i = 0; i < REP; i++) {
    outer();
}

ArkTools.arkSteedCompileSync(Mid);
ArkTools.arkSteedCompileSync(Leaf);
ArkTools.arkSteedCompileSync(outer);

evil = true;
print("nested: " + outer());
