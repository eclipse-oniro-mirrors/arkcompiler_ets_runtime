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

//! METHOD storeAndRead
//! HAS StoreTaggedFieldWithBarrier
//! HAS_NOT LoadTaggedField
//! HAS_NOT CallCommonStub GetPropertyByName
function storeAndRead(o, value) {
    o.target = value;
    return o.target;
}

//! METHOD storeOuterAndRead
//! HAS StoreTaggedFieldWithBarrier
//! COUNT LoadTaggedField 1
function storeOuterAndRead(o, value) {
    o.target = value;
    return o.target;
}

//! METHOD sameOffsetStoreAndRead
//! HAS StoreTaggedFieldWithBarrier
//! HAS_NOT LoadTaggedField
function sameOffsetStoreAndRead(o, value) {
    o.target = value;
    return o.target;
}

//! METHOD polyStoreAndRead
//! HAS StoreTaggedFieldByHClass
//! HAS_NOT LoadTaggedField
function polyStoreAndRead(o, value) {
    o.target = value;
    return o.target;
}

//! METHOD aliasStoreAndRead
//! COUNT LoadTaggedField 1
function aliasStoreAndRead(a, b, value, other) {
    a.target = value;
    b.target = other;
    return a.target;
}

//! METHOD callStoreAndRead
//! HAS CallKnownJSFunction
//! COUNT_GE LoadTaggedField 1
function callStoreAndRead(o, value, callback) {
    o.target = value;
    callback(o);
    return o.target;
}

//! METHOD keyedStoreAndRead
//! HAS CallCommonStub SetPropertyByValue
function keyedStoreAndRead(a, b, key, value, other) {
    a.target = value;
    b[key] = other;
    return a.target;
}

//! METHOD branchStoreAndRead
//! COUNT LoadTaggedField 1
function branchStoreAndRead(o, flag, left, right) {
    if (flag) {
        o.target = left;
    } else {
        o.target = right;
    }
    return o.target;
}

//! METHOD mixedStoreAndRead
//! HAS Call
function mixedStoreAndRead(o, value) {
    o.target = value;
    return o.target;
}

//! METHOD peelStoreAndRead
//! COUNT JumpLoop 1
//! HAS_NOT LoadTaggedField
function peelStoreAndRead(o, n) {
    let sum = 0;
    for (let i = 0; i < n; ++i) {
        o.target = i;
        sum += o.target;
    }
    return sum;
}

function overwrite(o) {
    o.target = 91;
}

function check(actual, expected) {
    if (actual !== expected) {
        throw new Error("wrong stored-property value");
    }
}

const first = {target: 0, first: 0};
const sameOffset = {target: 0, second: 0};
const differentOffset = {padding: 0, target: 0};
const outer = {};
for (let i = 0; i < 20; ++i) {
    outer["p" + i] = i;
}
outer.target = 0;
const accessor = {
    saved: 0,
    set target(value) { this.saved = value + 1; },
    get target() { return this.saved; }
};
const heapValue = {payload: 42};

for (let i = 0; i < 16; ++i) {
    storeAndRead(first, i);
    storeAndRead(first, heapValue);
    storeOuterAndRead(outer, i);
    sameOffsetStoreAndRead(i & 1 ? first : sameOffset, i);
    polyStoreAndRead(i & 1 ? first : differentOffset, i);
    aliasStoreAndRead(first, i & 1 ? first : sameOffset, i, i + 1);
    callStoreAndRead(first, i, overwrite);
    keyedStoreAndRead(first, i & 1 ? first : sameOffset, "target", i, i + 1);
    branchStoreAndRead(first, (i & 1) !== 0, i, i + 1);
    mixedStoreAndRead(i & 1 ? first : accessor, i);
    peelStoreAndRead(first, 4);
}

for (const fn of [storeAndRead, storeOuterAndRead, sameOffsetStoreAndRead, polyStoreAndRead,
                 aliasStoreAndRead, callStoreAndRead, keyedStoreAndRead, branchStoreAndRead,
                 mixedStoreAndRead, peelStoreAndRead]) {
    ArkTools.arkSteedCompileSync(fn);
}

for (const value of [37, heapValue, null]) {
    check(storeAndRead(first, value), value);
    check(storeOuterAndRead(outer, value), value);
    check(sameOffsetStoreAndRead(first, value), value);
    check(sameOffsetStoreAndRead(sameOffset, value), value);
    check(polyStoreAndRead(first, value), value);
    check(polyStoreAndRead(differentOffset, value), value);
    check(aliasStoreAndRead(first, first, value, 73), 73);
    check(aliasStoreAndRead(first, sameOffset, value, 73), value);
    check(callStoreAndRead(first, value, overwrite), 91);
    check(keyedStoreAndRead(first, first, "target", value, 73), 73);
    check(keyedStoreAndRead(first, sameOffset, "target", value, 73), value);
    check(branchStoreAndRead(first, true, value, 73), value);
    check(branchStoreAndRead(first, false, value, 73), 73);
}
check(mixedStoreAndRead(first, 37), 37);
check(mixedStoreAndRead(accessor, 37), 38);
for (const n of [0, 1, 2, 7]) {
    check(peelStoreAndRead(first, n), n * (n - 1) / 2);
}
print("PASS");
