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
 * An exception from the compiled super body at the MAIN call crossing a
 * catchless compiled frame into a compiled caller's never-executed catch:
 * pc-exact dispatch settles at the caller's own new-site lazy-deopt entry.
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
            throw new RangeError("m");
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
        return "outer-caught:false";
    } catch (e) {
        return "outer-caught:" + (e instanceof RangeError);
    }
}

const REP = 60000;
for (let i = 0; i < REP; i++) {
    new Leaf();
}
for (let i = 0; i < 1000; i++) {
    outer();
}

ArkTools.arkSteedCompileSync(Mid);
ArkTools.arkSteedCompileSync(Leaf);
ArkTools.arkSteedCompileSync(outer);

evil = true;
print("nested-main: " + outer());
