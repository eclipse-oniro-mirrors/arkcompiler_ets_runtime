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
//! METHOD pair
//! COUNT CheckedI32Mod 1
//! METHOD acrossCall
//! COUNT CheckedI32Mod 1
//! METHOD siblings
//! COUNT CheckedI32Mod 3
//! METHOD invariant
//! COUNT CheckedI32Mod 1
//! METHOD changing
//! COUNT_GE CheckedI32Mod 2
//! METHOD zeroDivisor
//! COUNT CheckedI32Mod 1
//! METHOD minusZero
//! COUNT CheckedI32Mod 1
//! METHOD overflowPair
//! COUNT CheckedI32Mod 1

declare function print(value: any): void;

function pair(x, y) {
    return (x % y) + (x % y);
}

function acrossCall(x, y, callback, state) {
    const first = x % y;
    callback(state);
    return first + (x % y);
}

function increment(state) {
    state.count++;
}

function siblings(x, y, flag) {
    let first;
    if (flag) {
        first = x % y;
    } else {
        first = x % y;
    }
    return first + (x % y);
}

function invariant(x, n) {
    let result = 0;
    for (let i = 0; i < n; i++) {
        result += (x % 3) + (x % 3);
    }
    return result;
}

function changing(x, n) {
    let result = 0;
    for (let i = 0; i < n; i++) {
        result += (x % 3) + (x % 3);
        x++;
    }
    return result;
}

function zeroDivisor(x, y, state) {
    const first = x % y;
    state.count++;
    return first + (x % y);
}

function minusZero(x, y, state) {
    const first = x % y;
    state.count++;
    return first + (x % y);
}

function overflowPair(x, y, state) {
    const first = x % y;
    state.count++;
    return first + (x % y);
}

const state = {count: 0};
for (let i = 0; i < 100; i++) {
    const x = i + 4;
    pair(x, 3);
    acrossCall(x, 3, increment, state);
    siblings(x, 3, (i & 1) === 0);
    invariant(x, 4);
    changing(x, 4);
    zeroDivisor(x, 3, state);
    minusZero(x, 3, state);
    overflowPair(x, 3, state);
}

const methods = [pair, acrossCall, siblings, invariant, changing, zeroDivisor, minusZero, overflowPair];
for (const method of methods) {
    ArkTools.arkSteedCompileSync(method);
    ArkTools.waitJitCompileFinish(method);
}
let ok = true;
for (const method of methods) {
    ok = ArkTools.isAOTCompiled(method) && ok;
}

state.count = 0;
ok = pair(8, 3) === 4 && pair(-8, 3) === -4 && ok;
ok = acrossCall(8, 3, increment, state) === 4 && state.count === 1 && ok;
ok = siblings(8, 3, true) === 4 && siblings(7, 3, false) === 2 && ok;
ok = invariant(8, 0) === 0 && invariant(8, 1) === 4 && invariant(8, 7) === 28 && ok;
ok = changing(8, 7) === 16 && ok;
ok = Number.isNaN(zeroDivisor(8, 0, state)) && state.count === 2 && ok;
ok = Object.is(minusZero(-6, 3, state), -0) && state.count === 3 && ok;
ok = Object.is(overflowPair(-2147483648, -1, state), -0) && state.count === 4 && ok;
print(ok ? "PASS" : "FAIL");
