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
//! METHOD integer
//! COUNT TypedArrayIntLoadElement 2
//! COUNT LoadInt32Field 1
//! COUNT CheckedTaggedIntToI32 1
//! HAS_NOT TaggedIntToI32
//! METHOD floating
//! COUNT TypedArrayDoubleLoadElement 2
//! COUNT LoadInt32Field 1
//! HAS_NOT CheckedNumberToF64
//! METHOD unsigned
//! HAS TypedArrayDoubleLoadElement
//! HAS_NOT CheckedNumberToF64
//! METHOD loop
//! COUNT LoadInt32Field 1
//! HAS StoreIntTypedArrayElement
//! METHOD twoReceivers
//! COUNT LoadInt32Field 2
//! METHOD aroundCall
//! COUNT LoadInt32Field 1
//! COUNT TypedArrayIntLoadElement 2
//! COUNT DeoptIfTaggedCondition 2
//! METHOD polymorphic
//! HAS BranchIfHClassIn
//! COUNT_GE TypedArrayIntLoadElement 2

declare function print(value: any): void;

function integer(a, i) {
    const value = a[i];
    a[i] = value + 1;
    return a[i];
}
function floating(a, i) {
    const value = a[i];
    a[i] = value + value;
    return a[i];
}
function unsigned(a, i) {
    const value = a[i];
    return value + value;
}
function loop(a, n) {
    let result = 0;
    for (let i = 0; i < n; i++) {
        a[i] = a[i] + 1;
        result += a[i];
    }
    return result;
}
function twoReceivers(a, b, i) { return a[i] + b[i]; }
function polymorphic(a, i) {
    const first = a[i];
    return first + a[i];
}
function aroundCall(a, i, callback) {
    const first = a[i];
    callback(a);
    return first + a[i];
}
function noOp(a) { return a; }
function mutate(a) { a[1] = 40; }
function detach(a) { ArkTools.arrayBufferDetach(a.buffer); }

const ints = new Int32Array([1, 2, 3, 4]);
const doubles = new Float64Array([0.5, 1.5, 2.5, -0]);
const uints = new Uint32Array([0, 2147483648, 4294967295]);
const offHeap = new Int32Array(5000);
offHeap[1] = 6;
const short = new Int32Array([10, 20]);
for (let i = 0; i < 100; i++) {
    integer(ints, i & 3);
    floating(doubles, i & 3);
    unsigned(uints, i % 3);
    loop(ints, 4);
    twoReceivers(ints, short, i & 1);
    aroundCall(offHeap, 1, noOp);
    polymorphic((i & 1) ? ints : offHeap, 1);
}
const methods = [integer, floating, unsigned, loop, twoReceivers, aroundCall, polymorphic];
let ok = true;
for (const method of methods) {
    ArkTools.arkSteedCompileSync(method);
    ArkTools.waitJitCompileFinish(method);
    ok = ArkTools.isAOTCompiled(method) && ok;
}
ints.set([1, 2, 3, 4]);
ok = polymorphic(ints, 1) === 4 && polymorphic(offHeap, 1) === 12 && ok;
ok = Number.isNaN(polymorphic(ints, 4)) && ok;
ok = integer(ints, 1) === 3 && ok;
ok = unsigned(uints, 1) === 4294967296 && unsigned(uints, 2) === 8589934590 && ok;
doubles[0] = -0;
ok = Object.is(floating(doubles, 0), -0) && ok;
doubles[1] = NaN;
ok = Number.isNaN(floating(doubles, 1)) && ok;
ints.set([1, 2, 3, 4]);
ok = loop(ints, 0) === 0 && loop(ints, 1) === 2 && loop(ints, 4) === 15 && ok;
ok = twoReceivers(ints, short, 1) === 23 && ok;
ok = Number.isNaN(twoReceivers(ints, short, 3)) && ok;
Object.defineProperty(ints, "length", {value: 0});
ok = integer(ints, 3) === 6 && ok;
ok = aroundCall(offHeap, 1, mutate) === 46 && ok;
let detached = false;
try {
    aroundCall(offHeap, 1, detach);
} catch (error) {
    detached = error instanceof TypeError;
}
ok = detached && ok;
print(ok ? "PASS" : "FAIL");
