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
//! METHOD addPair
//! COUNT I32AddWithOverflow 1
//! METHOD subPair
//! COUNT I32SubWithOverflow 1
//! METHOD mulPair
//! COUNT I32MulWithOverflow 1
//! METHOD divPair
//! COUNT I32DivWithOverflow 1
//! METHOD constants
//! COUNT I32DivByConstWithCheck 2
//! COUNT I32IncWithOverflow 1
//! COUNT I32DecWithOverflow 1
//! METHOD acrossCall
//! COUNT I32AddWithOverflow 1
//! METHOD siblings
//! COUNT I32SubWithOverflow 3

declare function print(value: any): void;

function addPair(x, y) { return [x + y, x + y]; }
function subPair(x, y) { return [x - y, x - y]; }
function mulPair(x, y) { return [x * y, x * y]; }
function divPair(x, y) { return [x / y, x / y]; }

function constants(x) {
    return [x / 3, x / 3, x / 5, x / 5, x + 1, x + 1, x - 1, x - 1];
}

function acrossCall(x, y, callback, state) {
    const first = x + y;
    callback(state);
    return [first, x + y];
}

function increment(state) { state.count++; }

function siblings(x, y, flag) {
    let first;
    if (flag) {
        first = x - y;
    } else {
        first = x - y;
    }
    return [first, x - y];
}

function changing(x, n) {
    let sum = 0;
    for (let i = 0; i < n; ++i) {
        sum += (x + 2) + (x + 2);
        x++;
    }
    return sum;
}

const state = {count: 0};
for (let i = 1; i < 100; i++) {
    addPair(i, 3);
    subPair(i, 3);
    mulPair(i, 3);
    divPair(i * 3, 3);
    constants(i * 15);
    acrossCall(i, 3, increment, state);
    siblings(i, 3, (i & 1) === 0);
    changing(i, 4);
}
const methods = [addPair, subPair, mulPair, divPair, constants, acrossCall, siblings, changing];
let ok = true;
for (const method of methods) {
    ArkTools.arkSteedCompileSync(method);
    ArkTools.waitJitCompileFinish(method);
    ok = ArkTools.isAOTCompiled(method) && ok;
}
function pairEquals(pair, expected) {
    return Object.is(pair[0], expected) && Object.is(pair[1], expected);
}
ok = pairEquals(addPair(7, 3), 10) && pairEquals(subPair(7, 3), 4) && ok;
ok = pairEquals(mulPair(7, 3), 21) && pairEquals(divPair(21, 3), 7) && ok;
ok = constants(30).join() === "10,10,6,6,31,31,29,29" && ok;
ok = pairEquals(siblings(8, 3, true), 5) && pairEquals(siblings(9, 3, false), 6) && ok;
state.count = 0;
ok = pairEquals(acrossCall(7, 3, increment, state), 10) && state.count === 1 && ok;
ok = changing(4, 0) === 0 && changing(4, 1) === 12 && changing(4, 4) === 60 && ok;
ok = pairEquals(addPair(2147483647, 1), 2147483648) && ok;
ok = pairEquals(subPair(-2147483648, 1), -2147483649) && ok;
ok = pairEquals(mulPair(-7, 0), -0) && ok;
ok = pairEquals(divPair(7, 2), 3.5) && pairEquals(divPair(1, 0), Infinity) && ok;
ok = pairEquals(divPair(-2147483648, -1), 2147483648) && ok;
ok = constants(7).join() === [7 / 3, 7 / 3, 7 / 5, 7 / 5, 8, 8, 6, 6].join() && ok;
ok = pairEquals(acrossCall(2147483647, 1, increment, state), 2147483648) && state.count === 2 && ok;
let conversions = 0;
const object = {valueOf() { conversions++; return 8; }};
ok = pairEquals(addPair(object, 3), 11) && conversions === 2 && ok;
print(ok ? "PASS" : "FAIL");
