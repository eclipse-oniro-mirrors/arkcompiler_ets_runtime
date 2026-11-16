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
 * @tc.name:normal_func_sharedarray_keyvariants
 * @tc.desc: element IC key-variant coverage for JSSharedArray: string keys
 *           (canonical index strings through the hit path), TreeString values
 *           (hit-path deopt to the flattening slow path), negative and
 *           non-integral double keys (hit-path deopt to the slow path),
 *           poly sites mixing a local array with a shared array, and
 *           SharedTypedArray out-of-bounds store semantics.
 * @tc.type: FUNC
 */

// @ts-nocheck
declare function print(str: any): string;

// ---------------------------------------------------------------------------
// K1: string keys - canonical index strings take the same element hit path
// (TryToElementsIndex -> StringToElementIndex), non-index strings deopt.
// ---------------------------------------------------------------------------

let sarr = new SendableArray(1, 2, 3);

// Store site warmed with an int key, then fed a canonical index string: the
// hit path parses the string and writes the same slot.
function storeByInt(arr, v) {
    for (let i = 0; i < 2000; ++i) { arr[1] = v; }
}
storeByInt(sarr, 5);
print("int store applied: " + (sarr[1] === 5));
sarr["1"] = 6;
print("string key store applied: " + (sarr[1] === 6));

// Load site warmed with an int key, then fed the same index as a string.
function loadByInt(arr) {
    let r;
    for (let i = 0; i < 2000; ++i) { r = arr[1]; }
    return r;
}
print("int load: " + loadByInt(sarr));
print("string key load: " + sarr["1"]);

// A string key site of its own (the compiler may keep it a separate by-value
// site): warms mono on the string form directly.
function storeByStringKey(arr, v) {
    for (let i = 0; i < 2000; ++i) { arr["2"] = v; }
}
storeByStringKey(sarr, 7);
print("string site store: " + (sarr[2] === 7));

// ---------------------------------------------------------------------------
// K2: TreeString value - a runtime-concatenated string deopts the hit path to
// the locked slow path, which flattens it before storing. Readback sees the
// flattened content either way.
// ---------------------------------------------------------------------------

function storeValue(arr, v) {
    for (let i = 0; i < 2000; ++i) { arr[0] = v; }
}
storeValue(sarr, 1);
// Dynamic concatenation defeats constant folding; repeated concatenation keeps
// producing fresh (tree or line) strings - the hit path must stay correct for
// both (line strings fast-path, tree strings deopt).
let concat = "";
for (let i = 0; i < 4; ++i) {
    concat = "a" + concat + "b";
    sarr[0] = concat;
    print("tree/line store readback " + i + ": " + (sarr[0] === concat));
}

// ---------------------------------------------------------------------------
// K3: poly element site - a local JSArray first, then the shared array. The
// site goes mono (local) -> poly; each hit dispatches through its own case and
// the shared receiver takes the scoped shared path.
// ---------------------------------------------------------------------------

let localArr = [10, 20, 30];
let polyShared = new SendableArray(100, 200, 300);

function polyLoad(o) {
    let r;
    for (let i = 0; i < 2000; ++i) { r = o[2]; }
    return r;
}
print("poly load local: " + polyLoad(localArr));
print("poly load shared: " + polyLoad(polyShared));

function polyStore(o, v) {
    for (let i = 0; i < 2000; ++i) { o[2] = v; }
}
polyStore(localArr, 31);
polyStore(polyShared, 301);
print("poly store local: " + (localArr[2] === 31));
print("poly store shared: " + (polyShared[2] === 301));

// Interleaved access after poly-ization: both stay correct.
print("poly interleave: " + (localArr[2] === 31) + " " + (polyShared[2] === 301));

// ---------------------------------------------------------------------------
// K4: negative and non-integral double keys - the hit path deopts
// (TryToElementsIndex < 0) and the slow path owns the semantics.
// ---------------------------------------------------------------------------

try {
    sarr[-1] = 1;
    print("negative store: no throw");
} catch (e) {
    print("negative store err name: " + e.name);
    print("negative store is TypeError: " + (e instanceof TypeError));
}
print("negative read: " + sarr[-1]);

try {
    sarr[1.5] = 1;
    print("fractional store: no throw");
} catch (e) {
    print("fractional store err name: " + e.name);
    print("fractional store is TypeError: " + (e instanceof TypeError));
}
print("fractional read: " + sarr[1.5]);

// ---------------------------------------------------------------------------
// K5: SharedTypedArray out-of-bounds store - the typed-array element hit path
// keeps the TypedArray OOB semantics (silently ignored, no grow).
// ---------------------------------------------------------------------------

let sta = new SendableUint8Array(4);
function staStore(arr, v) {
    for (let i = 0; i < 2000; ++i) { arr[0] = v; }
}
staStore(sta, 7);
sta[10] = 9;          // OOB: ignored per TypedArray semantics
print("sta oob ignored: " + (sta[10] === undefined));
print("sta inbounds kept: " + (sta[0] === 7));

// ---------------------------------------------------------------------------
// K6: undefined / null values - non-heap values take the fast path on the hit
// side and the NONE field type accepts them on the slow side; both must land.
// ---------------------------------------------------------------------------

let vals = new SendableArray(1, 2, 3);
function storeVals(arr, v) {
    for (let i = 0; i < 2000; ++i) { arr[0] = v; }
}
storeVals(vals, 1);
vals[0] = undefined;
print("undefined store readback: " + (vals[0] === undefined));
vals[0] = null;
print("null store readback: " + (vals[0] === null));

// ---------------------------------------------------------------------------
// K7: large index bounds - INT32_MAX itself trips the IR hit-path's
// greaterThanInt32Max deopt; 2^31 arrives as a double and fails the integral
// conversion. Both deopt to the slow path's out-of-range BusinessError.
// ---------------------------------------------------------------------------

try {
    sarr[2147483647] = 1;
    print("int32max store: no throw");
} catch (e) {
    print("int32max store err name: " + e.name);
}
try {
    sarr[2147483648] = 1;
    print("pow31 store: no throw");
} catch (e) {
    print("pow31 store err name: " + e.name);
}
print("int32max read: " + (sarr[2147483647] === undefined));

print("normal func sharedarray keyvariants over");
