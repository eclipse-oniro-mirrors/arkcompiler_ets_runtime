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

// newobjrange specialization: a constructor returning an explicit object
// (plain object, array, function object) must yield that object instead of the
// allocated receiver.
//
// Trigger: 16 interpreted warm-up constructions per caller fill the
// monomorphic constructor feedback and finish object-size tracking; every
// caller is then compiled with ArkTools.arkSteedCompileSync. Each caller has a
// single newobjrange site, so the specialized plan is taken and the in-graph
// object check must select the raw returned object.
//
// Expected: the returned object is the explicit object (different identity from
// the captured receiver), with the expected shape. The array and function
// cases prove the selection covers the full ECMAScript object range.

//! METHOD callObject
//! HAS InitializeEmptyObject
//! HAS CallNewObjRangeBase
//! HAS CallNewObjRangeDerived
//! HAS     CallRuntime AllocateInYoung
//! METHOD callArray
//! HAS InitializeEmptyObject
//! HAS CallNewObjRangeBase
//! HAS CallNewObjRangeDerived
//! HAS     CallRuntime AllocateInYoung
//! METHOD callFunction
//! HAS InitializeEmptyObject
//! HAS CallNewObjRangeBase
//! HAS CallNewObjRangeDerived
//! HAS     CallRuntime AllocateInYoung

let runs = 0;
let lastReceiver: any = null;

class RetObject {
    k: string;
    constructor() {
        runs++;
        lastReceiver = this;
        this.k = "r";
        return { kind: "object" };
    }
}

class RetArray {
    k: string;
    constructor() {
        runs++;
        lastReceiver = this;
        this.k = "r";
        return [1, 2, 3];
    }
}

class RetFunction {
    k: string;
    constructor() {
        runs++;
        lastReceiver = this;
        this.k = "r";
        return function inner() {
            return 9;
        };
    }
}

function callObject() {
    return new RetObject();
}
function callArray() {
    return new RetArray();
}
function callFunction() {
    return new RetFunction();
}

for (let i = 0; i < 16; i++) {
    callObject();
    callArray();
    callFunction();
}
ArkTools.arkSteedCompileSync(callObject);
ArkTools.arkSteedCompileSync(callArray);
ArkTools.arkSteedCompileSync(callFunction);

const o = callObject();
print(o.kind);
print(o !== lastReceiver);
print(o instanceof RetObject);

const a = callArray();
print(a.length);
print(a[1]);
print(Array.isArray(a));

const f = callFunction();
print(typeof f);
print(f());
print(f !== lastReceiver);
