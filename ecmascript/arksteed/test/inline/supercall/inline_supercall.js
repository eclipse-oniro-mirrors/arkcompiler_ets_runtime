/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

//! PARAMS --compiler-jit-hotness-threshold=150 --compiler-arksteed-enable-inline=true --compiler-min-inline-call-frequency=0.5 --compiler-max-inline-bytecodes=128 --compiler-arksteed-max-cumulative-inline-bytecode-size=256 --compiler-arksteed-max-constructor-inline-bytecode-size=64 --compiler-arksteed-max-graph-nodes-before-inline=1000

//! METHOD DerivedLeaf
//! HAS_NOT CallBaseConstructor
//! HAS_NOT CallDerivedConstructor

//! METHOD FurtherLeaf
//! HAS_NOT CallBaseConstructor
//! HAS_NOT CallDerivedConstructor

//! METHOD CatchesPrimitive
//! HAS_NOT CallBaseConstructor
//! HAS_NOT CallDerivedConstructor
//! COUNT CallCommonStub ConstructorCheck 2

class BaseValue {
    constructor(value) {
        this.base = value + 1;
    }
}

class DerivedLeaf extends BaseValue {
    constructor(value) {
        super(value);
        this.extra = this.base + 2;
    }
}

class FurtherLeaf extends DerivedLeaf {
    constructor(value) {
        super(value);
        this.total = this.extra + 3;
    }
}

class ReturnsPrimitive extends BaseValue {
    constructor() {
        super(0);
        return 1;
    }
}

class CatchesPrimitive extends ReturnsPrimitive {
    constructor() {
        try {
            super();
        } catch (error) {
            return { caught: error instanceof TypeError };
        }
    }
}

for (let i = 0; i < 64; i++) {
    new DerivedLeaf(i);
    new FurtherLeaf(i);
    new CatchesPrimitive();
}

const baseCompiled = ArkTools.arkSteedCompileSync(DerivedLeaf);
const derivedCompiled = ArkTools.arkSteedCompileSync(FurtherLeaf);
const catchCompiled = ArkTools.arkSteedCompileSync(CatchesPrimitive);
ArkTools.waitJitCompileFinish(DerivedLeaf);
ArkTools.waitJitCompileFinish(FurtherLeaf);
ArkTools.waitJitCompileFinish(CatchesPrimitive);

const baseResult = new DerivedLeaf(7).extra;
const derivedResult = new FurtherLeaf(7).total;
const caughtResult = new CatchesPrimitive().caught;
print(baseResult === 10 && derivedResult === 13 && caughtResult ? "PASS" : "FAIL");
print(baseCompiled && derivedCompiled && catchCompiled ? "PASS" : "FAIL");
