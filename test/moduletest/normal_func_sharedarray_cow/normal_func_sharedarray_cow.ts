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
 * @tc.name:normal_func_sharedarray_cow
 * @tc.desc: JSSharedArray element store IC hit path vs COW elements. The
 *           second SendableArray.from(same intern string) attaches the cached
 *           shared COW template zero-copy (StringToListResultCache ->
 *           CreateArrayFromList). The store hit path must deopt on COW
 *           elements (never write the shared template in the fast path); the
 *           site keeps mono while each store lands on the slow path, which
 *           owns the current write semantics — behavior identical to pre-IC.
 *           Single-array assertions only: cross-array template visibility
 *           belongs to the upcoming independent slow-path COW-copy fix.
 * @tc.type: FUNC
 */

// @ts-nocheck
declare function print(str: any): string;

// First from() builds the elements on the spot (a plain shared TaggedArray);
// the same-string second from() hits the result cache and attaches the shared
// COW template (SCOW) zero-copy.
let first = SendableArray.from("abc");
let cached = SendableArray.from("abc");

// Distill the element store site mono on the non-COW array first.
// Site is stobjbyvalue slot 1 (calibrated, same loop shape as
// normal_func_sharedarray_store.ts), ICKind 3 = StoreIC.
function storeSlot(arr, v) {
    for (let i = 0; i < 2000; ++i) { arr[0] = v; }
}
storeSlot(first, 1);
print("warm ic: " + ArkTools.getICState(storeSlot, 1, 3));

// Store into the COW-backed array through the warmed site: the hit path sees
// COW elements and deopts (result Hole, no fast-path write to the template);
// the slow path performs the store. The site stays mono — the re-distillation
// of the same hclass is a mode-merge no-op.
storeSlot(cached, 9);
print("cow store applied: " + (cached[0] === 9));
print("cow site ic kept: " + ArkTools.getICState(storeSlot, 1, 3));

// A repeated store keeps deopting to the same slow-path semantics: the value
// still lands and the site does not degrade. This deopt-loop is by design
// until the independent slow-path COW-copy fix lands.
storeSlot(cached, 11);
print("cow store repeat: " + (cached[0] === 11));
print("cow site ic final: " + ArkTools.getICState(storeSlot, 1, 3));

// The load side of the same COW array is read-only and unaffected: the READ
// protocol bounds check plus a plain element read.
// Site is ldobjbyvalue slot 1 (same loop shape as
// normal_func_sharedarray_load.ts), ICKind 2 = LoadIC.
function loadSlot(arr) {
    let r;
    for (let i = 0; i < 2000; ++i) { r = arr[1]; }
    return r;
}
print("cow load: " + loadSlot(cached));

print("normal func sharedarray cow over");
