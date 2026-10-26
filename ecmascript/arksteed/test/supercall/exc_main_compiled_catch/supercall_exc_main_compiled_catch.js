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
 * An exception from the compiled super body at the MAIN call, into a catch
 * the warmup executed (compiled-catch mode). The main call's pending-
 * exception check jumps to the inline compiled catch: the frame never
 * deoptimizes and stays installed.
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
            throw new RangeError("mid");
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

const REP = 20000;
for (let i = 0; i < REP; i++) {
    new Leaf();
}
evil = true;
for (let i = 0; i < REP; i++) {
    new Leaf();
}
evil = false;

ArkTools.arkSteedCompileSync(Mid);
ArkTools.arkSteedCompileSync(Leaf);

evil = true;
const r = new Leaf();
print("main-e-compiled-catch: " + r.caught + " " + ArkTools.arkSteedIsCompiled(Leaf));
