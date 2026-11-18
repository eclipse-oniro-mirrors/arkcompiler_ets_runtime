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
 * @tc.name:normal_func_sharedtypedarray_element
 * @tc.desc: regression guard — a SharedTypedArray's hclass is also IsJSShared, but
 *           its element sites carry TYPED_ARRAY-kind handlers with a different
 *           layout (MOD_RECORD_OFFSET / LENGTH_OFFSET differ from JSSharedArray).
 *           The C++ store hit path must dispatch it through the typed-array element
 *           branch, never into the JSSharedArray SharedStoreElement path: a
 *           mistyped dispatch would CAS the mod-record at the wrong offset and
 *           read a garbage length. Element store + load IC hits on
 *           SendableUint8Array must stay mono and behave.
 * @tc.type: FUNC
 */

// @ts-nocheck
declare function print(str: any): string;

let sta = new SendableUint8Array(4);

// Store site: warms into element MONO with a TYPED_ARRAY-kind handler. Under the
// fixed dispatch the hit path goes StoreElement -> typed-array branch; the buggy
// IsJSShared-first gate would enter SharedStoreElement (debug: ASSERT abort,
// release: mod-record CAS at JSTypedArray::SIZE + garbage length read).
// Site is stobjbyvalue slot 1 (calibrated loop shape), ICKind 3 = StoreIC.
function staStore(arr, v) {
    for (let i = 0; i < 2000; ++i) { arr[0] = v; }
}
staStore(sta, 7);
print("sta store applied: " + (sta[0] === 7));
print("sta store ic: " + ArkTools.getICState(staStore, 1, 3));

// In-bounds writes land element-wise; the typed-array bounds contract is
// unchanged through the hit path.
staStore(sta, 9);
print("sta store repeat: " + (sta[0] === 9));

// Load site: warms into element MONO and reads back.
// Site is ldobjbyvalue slot 1 (same loop shape), ICKind 2 = LoadIC.
function staLoad(arr) {
    let r;
    for (let i = 0; i < 2000; ++i) { r = arr[1]; }
    return r;
}
sta[1] = 3;
print("sta load: " + staLoad(sta));
print("sta load ic: " + ArkTools.getICState(staLoad, 1, 2));

// Mixed-site sanity: the same by-value site sees a JSSharedArray after the
// typed array — different hclass, IC degrades but both receivers keep working
// through their own correct paths.
let sarr = new SendableArray(1, 2, 3);
function mixedStore(o, v) {
    for (let i = 0; i < 2000; ++i) { o[0] = v; }
}
mixedStore(sta, 5);
print("mixed sta store: " + (sta[0] === 5));
mixedStore(sarr, 6);
print("mixed sarr store: " + (sarr[0] === 6));
print("mixed site ic: " + ArkTools.getICState(mixedStore, 1, 3));

print("normal func sharedtypedarray element over");
