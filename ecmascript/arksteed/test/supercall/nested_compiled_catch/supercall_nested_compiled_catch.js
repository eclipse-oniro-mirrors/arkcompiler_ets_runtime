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
 * The cold ConstructorCheck throw (compiled derived super returning a
 * primitive) crossing a catchless compiled frame into a compiled caller
 * whose catch the warmup executed: the caller's own pending-exception path
 * handles it inline and both frames stay installed.
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

function outer() {
    try {
        new Leaf();
        return "outer-inline:false";
    } catch (e) {
        return "outer-inline:" + (e instanceof TypeError);
    }
}

const REP = 20000;
for (let i = 0; i < REP; i++) {
    outer();
}
evil = true;
for (let i = 0; i < REP; i++) {
    outer();
}
evil = false;

ArkTools.arkSteedCompileSync(Mid);
ArkTools.arkSteedCompileSync(Leaf);
ArkTools.arkSteedCompileSync(outer);

evil = true;
print("nested-compiled-catch: " + outer() + " " + ArkTools.arkSteedIsCompiled(outer));
