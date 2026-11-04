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
 * The cold ConstructorCheck stub throwing into an executed (compiled) catch.
 * The warmup runs both quiet and throwing rounds, so the profile records the
 * catch as executed and the codegen keeps it inline: the throw surfaces
 * through the inline pending check, the frame never deoptimizes, and the
 * function stays compiled.
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
        try {
            super();
        } catch (e) {
            return {caught: e instanceof TypeError};
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
print("cold-compiled-catch: " + r.caught + " " + ArkTools.arkSteedIsCompiled(Leaf));
