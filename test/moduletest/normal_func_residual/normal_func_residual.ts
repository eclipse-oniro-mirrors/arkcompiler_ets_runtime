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

/*
 * @tc.name:normal_func_residual
 * @tc.desc: single-thread semantics of the shared-array element IC end state:
 *           pop/length contract (the pop return value and the new length),
 *           trim-triggering pops (physically shrink the array),
 *           grow-region Hole slots (undefined for reads, range error for writes),
 *           value-contract boundaries through the hit path (string/bigint publish),
 *           by-value vs by-name named access consistency and the length getter IC.
 * @tc.type: FUNC
 */

// @ts-nocheck
declare function print(str: any): string;

// AC-R1: pop returns the last element and decrements the length. The residual-slot
// read/write after a single pop is intentionally NOT asserted here: the two builtin
// paths disagree on the initial element capacity (the IR constructor allocates
// exactly argc while the C++ constructor grows with slack), so ShouldTrim fires on
// one side and not the other - an upstream divergence, out of this requirement's scope.
let sarr = new SendableArray(1, 2, 3, 4, 5, 6);
let popped = sarr.pop();
print("pop returns last: " + (popped === 6));
print("after pop length: " + sarr.length);

// AC-R3: trimming pops (gap > 4) physically shrink the elements array on both
// builtin paths; the same index is then truly out of bounds and the slow path
// rejects the store.
let trimmed = new SendableArray(1, 2, 3, 4, 5, 6);
for (let i = 0; i < 6; i++) { trimmed.pop(); }
try {
    trimmed[5] = 7;
    print("trimmed store: no throw");
} catch (e) {
    print("trimmed store throws: " + (e instanceof Error));
    print("trimmed store err name: " + e.name);
}
print("trimmed residual read: " + trimmed[5]);

// AC-R4: grow fills Hole - the pushed slot is readable right after push, and a
// grown-but-unwritten Hole slot (the F1 window) reads undefined on the load side.
let grown = new SendableArray(1, 2, 3);
grown.push(4);
print("grown push readback: " + grown[3]);
print("grown hole slot read: " + grown[4]);

// AC-R5: writing into the grown-but-unwritten window is rejected with the
// out-of-range error (F1 fix, single-thread steady state).
try {
    grown[4] = 9;
    print("grown window store: no throw");
} catch (e) {
    print("grown window store err name: " + e.name);
}

// AC-R6: value contract through the hit path - a string deopts to the locked slow
// path which publishes it; the hit path then reads it back.
let val = new SendableArray(1, 2, 3);
for (let i = 0; i < 100; ++i) { val[0] = 1; }
val[0] = "str";
print("string publish readback: " + val[0]);

// AC-R7: bigint follows the same publish path.
val[1] = 123n;
print("bigint publish readback: " + val[1]);

// AC-R8: a local object still violates the contract (never lands in the array).
try {
    val[2] = {};
    print("local object store: no throw");
} catch (e) {
    print("local object store err: " + (e instanceof TypeError));
}

// AC-R9: by-value (string key) and by-name access to the same container prototype
// method agree - both write forms take the property access; the IC states of the
// two sites are independent but the answers are identical.
let named = new SendableArray(1, 2, 3);
let byValue = named["push"];
print("by-value proto found: " + (typeof byValue === "function"));
print("by-name proto found: " + (typeof named.push === "function"));
print("by-value not-found: " + named["nosuch"]);
print("by-name not-found: " + named.nosuch);

// AC-R10: length getter keeps the READ-registered builtin path on every access
// (the proto IC caches the accessor location; LengthGetter itself stays scoped).
let lens = new SendableArray(1, 2, 3, 4);
let l = 0;
for (let i = 0; i < 2000; ++i) { l = lens.length; }
print("length getter value: " + l);

print("normal func residual ic over");
