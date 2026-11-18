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

/*
 * @tc.name:normal_func_sharedarray_store
 * @tc.desc: JSSharedArray element store IC in normal functions (US-3, store half)
 * @tc.type: FUNC
 */

// @ts-nocheck
declare function print(str: any): string;

class SBox {
    n: number = 0;
    constructor() { "use sendable"; }
}

let sarr = new SendableArray(1, 2, 3);
let sbox = new SBox();

// AC-3.2 (same-thread half): in-bounds store installs element IC.
// Site is stobjbyvalue slot 1 (calibrated, es2abc 2026-09-18), ICKind 3 = StoreIC.
function storeElem(arr, v) {
    for (let i = 0; i < 2000; ++i) { arr[0] = v; }
}
storeElem(sarr, 9);
print("store applied: " + (sarr[0] === 9));
print("store ic: " + ArkTools.getICState(storeElem, 1, 3));

// AC-3.4: out-of-bounds store throws via slow path (no auto-grow). The existing error
// is a ContainerError BusinessError (RANGE_ERROR class, name "BusinessError", message
// "The value of index is out of range."), not a RangeError instance — asserted as-is.
try {
    sarr[5] = 1;
    print("oob store: no throw");
} catch (e) {
    print("oob store throws Error: " + (e instanceof Error));
    print("oob store err name: " + e.name);
    print("oob store msg ok: " + (e.message.indexOf("out of range") >= 0));
}

// AC-3.5: value contract on the IC hit path — warm with a legal value, then store
// a local object: the hit path deopts and the slow path throws the contract error
function storeValue(arr, v) {
    for (let i = 0; i < 2000; ++i) { arr[1] = v; }
}
storeValue(sarr, 1);
try {
    storeValue(sarr, {});
    print("contract: no throw");
} catch (e) {
    print("contract throws TypeError: " + (e instanceof TypeError));
}

// AC-3.2 (shared object value half): a sendable instance satisfies the value
// contract. Note the site already went Mega at the TypeError above, so this
// store takes the generic slow path (which accepts it: elements use the NONE
// field type and the instance is JSShared) — the answer is what is asserted.
storeValue(sarr, sbox);
print("store shared obj ok: " + (sarr[1] === sbox));

// AC-1.3 (conflict-complete form): warm the site into element MONO (also crosses the
// PTI creation threshold), then a not-found named miss at the same by-value site
// resolves the layout conflict to Mega while both access kinds stay correct
function accessByKey(o, k) {
    return o[k];
}
for (let i = 0; i < 100; ++i) { accessByKey(sarr, 0); }
print("mix elem: " + accessByKey(sarr, 0));
print("mix notfound: " + accessByKey(sarr, "nosuch"));
print("mix elem after: " + accessByKey(sarr, 0));
// Site is ldobjbyvalue slot 0 (no-loop function shape, es2abc 2026-09-18), ICKind 2 = LoadIC
print("mix site ic: " + ArkTools.getICState(accessByKey, 0, 2));

// F1 regression (review-2): capacity-slack window. push grows the elements capacity
// with slack (highGrowth 2x) while length only covers written slots; a store into a
// grown-but-unused Hole slot (index >= length, index < capacity) must throw the
// out-of-range BusinessError via the locked slow path — the IC hit path must deopt
// on the Hole slot instead of silently writing a phantom element beyond length.
let grown = new SendableArray(1, 2, 3);
grown.push(4); // length 4; capacity grown with slack, slots [4, capacity) stay Hole
function storeWin(arr, idx, v) {
    for (let i = 0; i < 2000; ++i) { arr[idx] = v; }
}
storeWin(grown, 0, 42); // distill the site mono (same loop shape as storeElem → slot 1)
print("window site ic: " + ArkTools.getICState(storeWin, 1, 3));
try {
    storeWin(grown, 5, 7); // 5 ∈ [length=4, capacity): Hole slot → must throw
    print("window store: no throw");
} catch (e) {
    print("window store throws Error: " + (e instanceof Error));
    print("window store err name: " + e.name);
    print("window store msg ok: " + (e.message.indexOf("out of range") >= 0));
}
print("window site ic after: " + ArkTools.getICState(storeWin, 1, 3));
print("window length kept: " + (grown.length === 4));
print("window slot clean: " + (grown[5] === undefined));
print("window warm kept: " + (grown[0] === 42));

ArkTools.arkSteedCompileSync(storeElem);
ArkTools.waitJitCompileFinish(storeElem);
print(ArkTools.isAOTCompiled(storeElem));
ArkTools.arkSteedCompileSync(storeValue);
ArkTools.waitJitCompileFinish(storeValue);
print(ArkTools.isAOTCompiled(storeValue));
ArkTools.arkSteedCompileSync(storeWin);
ArkTools.waitJitCompileFinish(storeWin);
print(ArkTools.isAOTCompiled(storeWin));
ArkTools.arkSteedCompileSync(accessByKey);
ArkTools.waitJitCompileFinish(accessByKey);
print(ArkTools.isAOTCompiled(accessByKey));

storeElem(sarr, 88);
print("store after compile: " + (sarr[0] === 88));
storeValue(sarr, 5);
print("value store after compile: " + (sarr[1] === 5));
try {
    storeValue(sarr, {});
    print("value contract after compile: no throw");
} catch (e) {
    print("value contract after compile: " + (e instanceof TypeError));
}
storeWin(grown, 0, 55);
print("window warm after compile: " + (grown[0] === 55));
try {
    storeWin(grown, 5, 7);
    print("window after compile: no throw");
} catch (e) {
    print("window after compile: " + (e instanceof Error));
}
print("mix after compile: " + accessByKey(sarr, 0));
print("mix notfound after compile: " + accessByKey(sarr, "nosuch"));

print("normal func sharedarray store over");
