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

// newobjrange branch dispatch: native builtin constructors versus the generic
// construct fallback versus the specialized base-class path.
//
// Trigger: 16 warm-ups per caller fill the constructor feedback (builtin ids
// for Array/Map, JSFunction targets for the classes); every caller is then
// compiled. Array and Map must take CallKnownBuiltinVertex (native C++
// constructor, no receiver allocation in the graph). A derived-class target is
// not a base constructor, so its site must fall back to OptNewObjRange even
// though the feedback is monomorphic. The base class must specialize.
//
// Expected: builtin construction produces correct native objects; derived
// construction runs super() and fills both classes' fields; the base class
// specializes.

//! METHOD callBuiltinArray
//! HAS     CallKnownBuiltin
//! HAS_NOT InitializeEmptyObject
//! HAS_NOT CallRuntime OptNewObjRange
//! METHOD callBuiltinMap
//! HAS     CallKnownBuiltin
//! HAS_NOT InitializeEmptyObject
//! HAS_NOT CallRuntime OptNewObjRange
//! METHOD callBase
//! HAS     InitializeEmptyObject
//! HAS     CallNewObjRangeBase
//! HAS     CallNewObjRangeDerived
//! HAS     CallRuntime AllocateInYoung
//! METHOD callDerived
//! HAS_NOT InitializeEmptyObject
//! HAS CallNewObjRangeBase
//! HAS CallNewObjRangeDerived
//! HAS_NOT CallRuntime OptNewObjRange

class GenericBase {
    b: number;

    constructor(b: number) {
        this.b = b;
    }
}

class GenericDerived extends GenericBase {
    d: number;

    constructor(b: number, d: number) {
        super(b);
        this.d = d;
    }
}

function callBuiltinArray(n) {
    return new Array(n);
}

function callBuiltinMap(n) {
    const m = new Map();
    m.set("k", n);
    return m;
}

function callBase(x) {
    return new GenericBase(x);
}

function callDerived(x) {
    return new GenericDerived(x, x + 1);
}

for (let i = 0; i < 16; i++) {
    callBuiltinArray(i);
    callBuiltinMap(i);
    callBase(i);
    callDerived(i);
}
ArkTools.arkSteedCompileSync(callBuiltinArray);
ArkTools.arkSteedCompileSync(callBuiltinMap);
ArkTools.arkSteedCompileSync(callBase);
ArkTools.arkSteedCompileSync(callDerived);

const arr = callBuiltinArray(3);
print(arr.length);
print(Array.isArray(arr));

const map = callBuiltinMap(5);
print(map.size);
print(map.get("k"));

const base = callBase(4);
print(base.b);
print(base instanceof GenericBase);

const derived = callDerived(6);
print(derived.b);
print(derived.d);
print(derived instanceof GenericBase);
print(derived instanceof GenericDerived);
