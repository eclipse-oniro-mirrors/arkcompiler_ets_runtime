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

// @ts-nocheck
//! PARAMS --compiler-jit-hotness-threshold=10000
//! METHOD repeatedLength
//! COUNT LoadInt32Field 1
//! COUNT DeoptIfArrayBufferDetached 2
//! HAS_NOT CallCommonStub GetPropertyByName
//! METHOD lengthAndElement
//! COUNT LoadInt32Field 1
//! HAS TypedArrayIntLoadElement
//! HAS_NOT CallCommonStub GetPropertyByName
//! METHOD lengthLoop
//! COUNT LoadInt32Field 1
//! HAS TypedArrayIntLoadElement
//! HAS_NOT CallCommonStub GetPropertyByName
//! METHOD twoLengths
//! COUNT LoadInt32Field 2
//! METHOD bigintLength
//! HAS CallCommonStub GetPropertyByName
//! METHOD keyedLength
//! COUNT LoadInt32Field 1
//! HAS TypedArrayIntLoadElement
//! HAS_NOT CallCommonStub GetPropertyByValue
//! METHOD mixedLength
//! HAS BranchIfHClassIn
//! COUNT_GE LoadInt32Field 2
//! HAS_NOT CallCommonStub GetPropertyByName
//! METHOD getterAroundCall
//! COUNT LoadInt32Field 1
//! COUNT DeoptIfPrototypeChanged 2
//! HAS_NOT CallCommonStub GetPropertyByName
//! METHOD customLength
//! HAS_NOT LoadInt32Field
//! METHOD staleLength
//! HAS CallCommonStub GetPropertyByName

declare function print(value: any): void;

function repeatedLength(a, callback) {
    const first = a.length;
    callback(a);
    return first + a.length;
}
function lengthAndElement(a, i) { return a.length + a[i]; }
function lengthLoop(a) {
    let sum = 0;
    for (let i = 0; i < a.length; i++) { sum += a[i]; }
    return sum;
}
function twoLengths(a, b) { return a.length + b.length; }
function bigintLength(a) { return a.length; }
function keyedLength(a, key) { return a[key] + a[0]; }
function mixedLength(a) { return a.length; }
function getterAroundCall(a, callback) {
    const first = a.length;
    callback(a);
    return first + a.length;
}
function customLength(a) { return a.length; }
function inheritedLength(a) { return a.length; }
function staleLength(a) { return a.length; }
function refreshLength(a) { return a.length; }
function noOp(a) { return a; }
function detach(a) { ArkTools.arrayBufferDetach(a.buffer); }

const small = new Int32Array([1, 2, 3, 4]);
const guarded = new Int32Array([1, 2, 3, 4]);
const other = new Int32Array([10, 20]);
const offHeap = new Int32Array(5000);
const bigints = new BigInt64Array(3);
const plain = {length: 17};
const array = [1, 2, 3];
let getterCalls = 0;
const custom = new Int32Array(6);
Object.defineProperty(custom, "length", {get() { getterCalls++; return 19; }});
const cases = [small, offHeap, plain, array];
const intervening = Object.create(Int32Array.prototype);
const stale = new Int32Array(4);
Object.setPrototypeOf(stale, intervening);
for (let i = 0; i < 100; i++) {
    repeatedLength(offHeap, noOp);
    lengthAndElement(small, i & 3);
    lengthLoop(small);
    twoLengths(small, other);
    bigintLength(bigints);
    keyedLength(small, "length");
    mixedLength(cases[i & 3]);
    getterAroundCall(guarded, noOp);
    customLength(custom);
    inheritedLength(small);
    staleLength(stale);
}
Object.defineProperty(intervening, "length", {value: 23});
for (let i = 0; i < 100; i++) { refreshLength(stale); }
const methods = [repeatedLength, lengthAndElement, lengthLoop, twoLengths, bigintLength, keyedLength,
                 mixedLength, getterAroundCall, customLength, inheritedLength, staleLength];
let ok = true;
for (const method of methods) {
    ArkTools.arkSteedCompileSync(method);
    ArkTools.waitJitCompileFinish(method);
    ok = ArkTools.isAOTCompiled(method) && ok;
}
ok = repeatedLength(offHeap, noOp) === 10000 && ok;
ok = lengthAndElement(small, 1) === 6 && lengthLoop(small) === 10 && ok;
ok = twoLengths(small, other) === 6 && bigintLength(bigints) === 3 && ok;
ok = keyedLength(small, "length") === 5 && ok;
ok = staleLength(stale) === 23 && ok;
ok = mixedLength(small) === 4 && mixedLength(offHeap) === 5000 &&
    mixedLength(plain) === 17 && mixedLength(array) === 3 && ok;
const beforeCalls = getterCalls;
ok = customLength(custom) === 19 && getterCalls === beforeCalls + 1 && ok;
let threw = false;
try { inheritedLength(Object.create(small)); } catch (e) { threw = e instanceof TypeError; }
ok = threw && ok;

// A named shadow must not replace the internal length used by element bounds checks.
Object.defineProperty(small, "length", {value: 1, configurable: true});
ok = lengthAndElement(small, 3) === 5 && ok;
delete small.length;
ok = repeatedLength(offHeap, detach) === 5000 && offHeap.length === 0 && ok;

const typedArrayPrototype = Object.getPrototypeOf(Int32Array.prototype);
const original = Object.getOwnPropertyDescriptor(typedArrayPrototype, "length");
function replaceGetter() {
    Object.defineProperty(typedArrayPrototype, "length", {get() { return 7; }, configurable: true});
}
try {
    ok = ArkTools.isAOTCompiled(getterAroundCall) && ok;
    ok = getterAroundCall(guarded, replaceGetter) === 11 && ok;
    ok = getterAroundCall(guarded, noOp) === 14 && ok;
} finally {
    Object.defineProperty(typedArrayPrototype, "length", original);
}
print(ok ? "PASS" : "FAIL");
