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

//! METHOD newBase
//! HAS_NOT CallNewObjRangeBase
//! HAS_NOT CallNewObjRangeDerived

//! METHOD newDerived
//! HAS_NOT CallNewObjRangeBase
//! HAS_NOT CallNewObjRangeDerived
//! HAS_NOT CallDerivedConstructor

//! METHOD newBasePrimitive
//! HAS_NOT CallNewObjRangeBase
//! HAS_NOT CallNewObjRangeDerived

//! METHOD newDerivedPrimitiveCatch
//! HAS_NOT CallNewObjRangeBase
//! HAS_NOT CallNewObjRangeDerived
//! HAS_NOT CallDerivedConstructor
//! COUNT CallCommonStub ConstructorCheck 2

//! METHOD newDefault
//! HAS_NOT CallNewObjRangeDerived
//! HAS_NOT CallNewObjRangeBase
//! COUNT CallRuntime OptSuperCall 1

//! METHOD newPolymorphic
//! HAS CallNewObjRangeBase
//! HAS CallNewObjRangeDerived
//! COUNT CallRuntime OptNewObjRange 1

//! METHOD newSpread
//! HAS_NOT CallNewObjRangeBase
//! HAS_NOT CallNewObjRangeDerived
//! COUNT CallRuntime NewObjApply 1

//! METHOD newTargetSwap
//! HAS_NOT CallNewObjRangeBase
//! HAS_NOT CallNewObjRangeDerived

//! DEOPT_COUNT_GE * 1

class BaseCtor {
    constructor(value) {
        this.value = value + 1;
    }
}

class DerivedCtor extends BaseCtor {
    constructor(value) {
        super(value);
        this.extra = this.value + 1;
    }
}

class BasePrimitive {
    constructor() {
        this.marker = "base";
        return 1;
    }
}

class DerivedPrimitive extends BaseCtor {
    constructor() {
        super(0);
        return 1;
    }
}

class DefaultDerived extends BaseCtor {
}

class Replacement {
    constructor(value) {
        this.replacement = value;
    }
}

let TargetCtor = BaseCtor;

function newBase(value) {
    return new BaseCtor(value);
}

function newDerived(value) {
    return new DerivedCtor(value);
}

function newBasePrimitive() {
    return new BasePrimitive();
}

function newDerivedPrimitiveCatch() {
    try {
        return new DerivedPrimitive();
    } catch (error) {
        return error instanceof TypeError;
    }
}

function newDefault(value) {
    return new DefaultDerived(value);
}

function newPolymorphic(ctor, value) {
    return new ctor(value);
}

function newSpread(values) {
    return new BaseCtor(...values);
}

function newTargetSwap(value) {
    return new TargetCtor(value);
}

for (let i = 0; i < 64; i++) {
    newBase(i);
    newDerived(i);
    newBasePrimitive();
    newDerivedPrimitiveCatch();
    newDefault(i);
    newPolymorphic(i % 2 === 0 ? BaseCtor : Replacement, i);
    newSpread([i]);
    newTargetSwap(i);
}

const methods = [newBase, newDerived, newBasePrimitive, newDerivedPrimitiveCatch, newDefault, newPolymorphic, newSpread,
    newTargetSwap];
let compiled = true;
for (const method of methods) {
    compiled = ArkTools.arkSteedCompileSync(method) && compiled;
    ArkTools.waitJitCompileFinish(method);
}

print("base:" + newBase(7).value);
print("derived:" + newDerived(7).extra);
print("primitive:" + newBasePrimitive().marker + " " + newDerivedPrimitiveCatch());
print("default:" + newDefault(8).value);
print("poly:" + (newPolymorphic(BaseCtor, 1).value === 2 && newPolymorphic(Replacement, 2).replacement === 2));
print("spread:" + newSpread([4]).value);
TargetCtor = Replacement;
print("swap:" + newTargetSwap(9).replacement);
print("compiled:" + compiled);
