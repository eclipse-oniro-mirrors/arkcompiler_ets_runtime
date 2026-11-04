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

//! PARAMS --compiler-jit-hotness-threshold=60000

// newobjrange specialization: a constructor that returns a primitive value
// must yield the allocated receiver. Covers undefined, number, string, null,
// boolean, symbol and bigint returns.
//
// Trigger: 16 interpreted warm-up constructions per caller fill the
// monomorphic constructor feedback and finish object-size tracking; every
// caller is then compiled with ArkTools.arkSteedCompileSync. Each caller has a
// single newobjrange site, so the specialized plan is taken.
//
// Expected: `result === lastReceiver` proves the construction result is the
// object captured as `this` inside the constructor, not the returned
// primitive. `result.k` proves the receiver's own state survived.

//! METHOD callUndefined
//! HAS InitializeEmptyObject
//! HAS CallNewObjRangeBase
//! HAS CallNewObjRangeDerived
//! HAS     CallRuntime AllocateInYoung
//! METHOD callInt
//! HAS InitializeEmptyObject
//! HAS CallNewObjRangeBase
//! HAS CallNewObjRangeDerived
//! HAS     CallRuntime AllocateInYoung
//! METHOD callString
//! HAS InitializeEmptyObject
//! HAS CallNewObjRangeBase
//! HAS CallNewObjRangeDerived
//! HAS     CallRuntime AllocateInYoung
//! METHOD callNull
//! HAS InitializeEmptyObject
//! HAS CallNewObjRangeBase
//! HAS CallNewObjRangeDerived
//! HAS     CallRuntime AllocateInYoung
//! METHOD callBoolean
//! HAS InitializeEmptyObject
//! HAS CallNewObjRangeBase
//! HAS CallNewObjRangeDerived
//! HAS     CallRuntime AllocateInYoung
//! METHOD callSymbol
//! HAS InitializeEmptyObject
//! HAS CallNewObjRangeBase
//! HAS CallNewObjRangeDerived
//! HAS     CallRuntime AllocateInYoung
//! METHOD callBigInt
//! HAS InitializeEmptyObject
//! HAS CallNewObjRangeBase
//! HAS CallNewObjRangeDerived
//! HAS     CallRuntime AllocateInYoung

let runs = 0;
let lastReceiver: any = null;

class RetUndefined {
    k: string;
    constructor() {
        runs++;
        lastReceiver = this;
        this.k = "u";
        return undefined;
    }
}

class RetInt {
    k: string;
    constructor() {
        runs++;
        lastReceiver = this;
        this.k = "i";
        return 42;
    }
}

class RetString {
    k: string;
    constructor() {
        runs++;
        lastReceiver = this;
        this.k = "s";
        return "not-an-object";
    }
}

class RetNull {
    k: string;
    constructor() {
        runs++;
        lastReceiver = this;
        this.k = "n";
        return null;
    }
}

class RetBoolean {
    k: string;
    constructor() {
        runs++;
        lastReceiver = this;
        this.k = "b";
        return false;
    }
}

class RetSymbol {
    k: string;
    constructor() {
        runs++;
        lastReceiver = this;
        this.k = "y";
        return Symbol("primitive");
    }
}

class RetBigInt {
    k: string;
    constructor() {
        runs++;
        lastReceiver = this;
        this.k = "g";
        return BigInt(7);
    }
}

function callUndefined() {
    return new RetUndefined();
}
function callInt() {
    return new RetInt();
}
function callString() {
    return new RetString();
}
function callNull() {
    return new RetNull();
}
function callBoolean() {
    return new RetBoolean();
}
function callSymbol() {
    return new RetSymbol();
}
function callBigInt() {
    return new RetBigInt();
}

for (let i = 0; i < 16; i++) {
    callUndefined();
    callInt();
    callString();
    callNull();
    callBoolean();
    callSymbol();
    callBigInt();
}
ArkTools.arkSteedCompileSync(callUndefined);
ArkTools.arkSteedCompileSync(callInt);
ArkTools.arkSteedCompileSync(callString);
ArkTools.arkSteedCompileSync(callNull);
ArkTools.arkSteedCompileSync(callBoolean);
ArkTools.arkSteedCompileSync(callSymbol);
ArkTools.arkSteedCompileSync(callBigInt);

const u = callUndefined();
print(u.k);
print(u === lastReceiver);

const i = callInt();
print(i.k);
print(i === lastReceiver);

const s = callString();
print(s.k);
print(s === lastReceiver);

const n = callNull();
print(n.k);
print(n === lastReceiver);

const b = callBoolean();
print(b.k);
print(b === lastReceiver);

const y = callSymbol();
print(y.k);
print(y === lastReceiver);

const g = callBigInt();
print(g.k);
print(g === lastReceiver);

print(runs > 0);
