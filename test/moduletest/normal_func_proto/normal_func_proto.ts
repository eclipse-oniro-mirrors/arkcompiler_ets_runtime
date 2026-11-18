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
 * @tc.name:normal_func_proto
 * @tc.desc: prototype IC expansion to all sendable types in normal functions (US-2)
 * @tc.type: FUNC
 */

// @ts-nocheck
declare function print(str: any): string;

class SPointG {
    x: number = 7;
    constructor() { "use sendable"; }
    get gx(): number { return this.x; }
}

class SPointS {
    x: number = 0;
    constructor() { "use sendable"; }
    set sx(v: number) { this.x = v; }
}

class SPointM {
    constructor() { "use sendable"; }
    m(): number { return 1; }
}

let spg = new SPointG();
let sps = new SPointS();
let spm = new SPointM();
let sta = new SendableUint8Array(5);
let sarr = new SendableArray(1, 2, 3);
let smap = new SendableMap();

// AC-2.1: prototype method load on a non-whitelist shared type (SharedTypedArray,
// non-BigInt variant: BigInt64/BigUint64 arrays are HasOrdinaryGet by design and
// never install named ICs, shared or not).
function loadProtoMethod(arr) {
    let r;
    for (let i = 0; i < 2000; ++i) { r = arr.fill; }
    return r;
}
print("sta proto method found: " + (loadProtoMethod(sta) !== undefined));
print("sta proto ic: " + ArkTools.getICState(loadProtoMethod, 1, 0));

// AC-2.2: prototype getter load with receiver binding (getter reads this.x)
function loadGetter(obj) {
    let r;
    for (let i = 0; i < 100000; ++i) { r = obj.gx; }
    return r;
}
print("getter value: " + loadGetter(spg));
print("getter ic: " + ArkTools.getICState(loadGetter, 1, 0));

// AC-2.3: prototype setter store (setter writes this.x)
function storeSetter(obj, v) {
    for (let i = 0; i < 100000; ++i) { obj.sx = v; }
}
storeSetter(sps, 9);
print("setter applied: " + (sps.x === 9));
print("setter ic: " + ArkTools.getICState(storeSetter, 1, 1));

// AC-2.4: whitelist types (JSSharedArray/JSSharedMap) regression
function loadWhitelistArr(arr) {
    let r;
    for (let i = 0; i < 100000; ++i) { r = arr.containsAll; }
    return r;
}
function loadWhitelistMap(map) {
    let r;
    for (let i = 0; i < 100000; ++i) { r = map.set; }
    return r;
}
print("whitelist arr found: " + (loadWhitelistArr(sarr) !== undefined));
print("whitelist arr ic: " + ArkTools.getICState(loadWhitelistArr, 1, 0));
print("whitelist map found: " + (loadWhitelistMap(smap) !== undefined));
print("whitelist map ic: " + ArkTools.getICState(loadWhitelistMap, 1, 0));

// AC-2.5: store to non-writable prototype data property (a method) keeps TypeError
try {
    spm.m = function () { return 2; };
    print("proto store: no throw");
} catch (e) {
    print("proto store throws TypeError: " + (e instanceof TypeError));
}

print("normal func proto ic over");
