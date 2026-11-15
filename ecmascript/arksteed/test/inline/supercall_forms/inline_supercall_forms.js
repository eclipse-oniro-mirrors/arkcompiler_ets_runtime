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

//! PARAMS --compiler-jit-hotness-threshold=150 --compiler-arksteed-enable-inline=true --compiler-min-inline-call-frequency=0.5 --compiler-max-inline-bytecodes=128 --compiler-arksteed-max-cumulative-inline-bytecode-size=256 --compiler-arksteed-max-constructor-inline-bytecode-size=256 --compiler-arksteed-max-graph-nodes-before-inline=1000 --compiler-arksteed-max-inline-actual-args=17

//! METHOD Wide
//! HAS_NOT CallBaseConstructor
//! HAS_NOT CallDerivedConstructor

//! METHOD Spread
//! HAS_NOT CallBaseConstructor
//! HAS_NOT CallDerivedConstructor
//! HAS_NOT CallRuntime OptSuperCallSpread
//! COUNT CallRuntime SuperCallSpreadWithNewTarget 1

//! METHOD Forward
//! HAS_NOT CallBaseConstructor
//! HAS_NOT CallDerivedConstructor
//! COUNT CallRuntime OptSuperCallForwardAllArgs 1

//! METHOD baseArrow
//! HAS CallBaseConstructor
//! HAS CallDerivedConstructor
//! COUNT CallRuntime OptSuperCall 1

//! METHOD derivedArrow
//! HAS CallBaseConstructor
//! HAS CallDerivedConstructor
//! COUNT CallRuntime OptSuperCall 1

//! METHOD NativeArray
//! HAS CallBaseConstructor
//! HAS CallDerivedConstructor

//! METHOD Swapped
//! HAS_NOT CallBaseConstructor
//! HAS_NOT CallDerivedConstructor

//! METHOD throwingArrow
//! COUNT CallRuntime OptSuperCall 1

//! METHOD ThrowDirect
//! HAS_NOT CallBaseConstructor
//! HAS_NOT CallDerivedConstructor
//! COUNT CallRuntime OptSuperCall 1

//! DEOPT_COUNT_GE * 1

class ManyBase {
    constructor(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17) {
        this.total = a1 + a2 + a3 + a4 + a5 + a6 + a7 + a8 + a9 + a10 + a11 + a12 + a13 + a14 + a15 + a16 + a17;
        this.count = 17;
    }
}

class Wide extends ManyBase {
    constructor() {
        super(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17);
    }
}

class Sum {
    constructor(...values) {
        this.total = values.reduce((left, right) => left + right, 0);
    }
}

class Spread extends Sum {
    constructor(values) {
        super(...values);
    }
}

class Forward extends Sum {
}

class BaseA {
    constructor(value) {
        this.value = value;
    }
}

let baseArrow = null;
class ArrowBase extends BaseA {
    constructor(value) {
        baseArrow = () => super(value);
        baseArrow();
    }
}

class MiddleA extends BaseA {
    constructor(value) {
        super(value);
    }
}

let derivedArrow = null;
class ArrowDerived extends MiddleA {
    constructor(value) {
        derivedArrow = () => super(value);
        derivedArrow();
    }
}

class NativeArray extends Array {
    constructor(length) {
        super(length);
    }
}

class Initial {
    constructor(value) {
        this.initial = value;
    }
}

class Replacement {
    constructor(value) {
        this.replacement = value;
    }
}

class Swapped extends Initial {
    constructor(value) {
        super(value);
    }
}

class AlwaysThrow {
    constructor() {
        throw new RangeError("expected");
    }
}

let throwingArrow = null;
class ArrowThrow extends AlwaysThrow {
    constructor() {
        throwingArrow = () => super();
        throwingArrow();
    }
}

class ThrowDirect extends AlwaysThrow {
    constructor() {
        super();
    }
}

for (let i = 0; i < 64; i++) {
    new Wide();
    new Spread([1, 2, 3]);
    new Forward(4, 5);
    new ArrowBase(i);
    new ArrowDerived(i);
    new NativeArray(2);
    new Swapped(i);
    try {
        new ArrowThrow();
    } catch (error) {
    }
    try {
        new ThrowDirect();
    } catch (error) {
    }
}

const compiled = ArkTools.arkSteedCompileSync(Wide) && ArkTools.arkSteedCompileSync(Spread) &&
    ArkTools.arkSteedCompileSync(Forward) && ArkTools.arkSteedCompileSync(baseArrow) &&
    ArkTools.arkSteedCompileSync(derivedArrow) && ArkTools.arkSteedCompileSync(NativeArray) &&
    ArkTools.arkSteedCompileSync(Swapped) && ArkTools.arkSteedCompileSync(throwingArrow) &&
    ArkTools.arkSteedCompileSync(ThrowDirect);
for (const method of [Wide, Spread, Forward, baseArrow, derivedArrow, NativeArray, Swapped, throwingArrow, ThrowDirect]) {
    ArkTools.waitJitCompileFinish(method);
}

const wide = new Wide();
print("wide:" + wide.count + " " + wide.total);
print("spread:" + new Spread([1, 2, 3]).total);
print("forward:" + new Forward(4, 5).total);
const arrowBaseResult = new ArrowBase(4);
print("arrow-base:" + (arrowBaseResult instanceof BaseA) + " " + (arrowBaseResult instanceof ArrowBase));
const arrowDerivedResult = new ArrowDerived(6);
print("arrow-derived:" + (arrowDerivedResult instanceof MiddleA) + " " + (arrowDerivedResult instanceof ArrowDerived));
const nativeResult = new NativeArray(3);
print("native:" + nativeResult.length + " " + (nativeResult instanceof NativeArray));
Object.setPrototypeOf(Swapped, Replacement);
const swappedResult = new Swapped(9);
print("swap:" + swappedResult.replacement + " " + compiled);
let throwFallback = false;
try {
    new ArrowThrow();
} catch (error) {
    throwFallback = error instanceof RangeError;
}
print("throw-fallback:" + throwFallback);
let throwDirect = false;
try {
    new ThrowDirect();
} catch (error) {
    throwDirect = error instanceof RangeError;
}
print("throw-direct:" + throwDirect);
