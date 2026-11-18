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
 * @tc.name:normal_func_gc_survival
 * @tc.desc: sendable IC handlers/hclasses survive multiple full GCs (AC-4.1)
 * @tc.type: FUNC
 */

// @ts-nocheck
declare function print(str: any): string;

class SPoint {
    x: number = 1;
    constructor() { "use sendable"; }
}

let sp = new SPoint();
let sarr = new SendableArray(1, 2, 3);

// Scenario 1: not-found IC reaches steady state, then survives full GCs
function loadNotFound(obj) {
    let r;
    for (let i = 0; i < 2000; ++i) { r = obj.nosuch; }
    return r;
}
print("nf before gc: " + loadNotFound(sp));
print("nf ic before gc: " + ArkTools.getICState(loadNotFound, 1, 0));
print("gc: " + ArkTools.forceFullGC());
print("gc: " + ArkTools.forceFullGC());
print("gc: " + ArkTools.forceFullGC());
print("gc: " + ArkTools.forceFullGC());
print("gc: " + ArkTools.forceFullGC());
print("nf after gc: " + loadNotFound(sp));
print("nf ic after gc: " + ArkTools.getICState(loadNotFound, 1, 0));

// Scenario 2: prototype IC (getter on proto) survives full GCs
class SG {
    x: number = 7;
    constructor() { "use sendable"; }
    get gx(): number { return this.x; }
}
let sg = new SG();
function loadGetter(obj) {
    let r;
    for (let i = 0; i < 2000; ++i) { r = obj.gx; }
    return r;
}
print("getter before gc: " + loadGetter(sg));
print("getter ic before gc: " + ArkTools.getICState(loadGetter, 1, 0));
for (let i = 0; i < 5; ++i) { ArkTools.forceFullGC(); }
print("getter after gc: " + loadGetter(sg));
print("getter ic after gc: " + ArkTools.getICState(loadGetter, 1, 0));

// Scenario 3: JSSharedArray element load/store IC survives full GCs
function loadElem(arr) {
    let r;
    for (let i = 0; i < 2000; ++i) { r = arr[0]; }
    return r;
}
function storeElem(arr, v) {
    for (let i = 0; i < 2000; ++i) { arr[0] = v; }
}
storeElem(sarr, 9);
print("elem before gc: " + loadElem(sarr));
print("elem load ic before gc: " + ArkTools.getICState(loadElem, 1, 2));
print("elem store ic before gc: " + ArkTools.getICState(storeElem, 1, 3));
for (let i = 0; i < 5; ++i) { ArkTools.forceFullGC(); }
storeElem(sarr, 11);
print("elem after gc: " + loadElem(sarr));
print("elem load ic after gc: " + ArkTools.getICState(loadElem, 1, 2));
print("elem store ic after gc: " + ArkTools.getICState(storeElem, 1, 3));

// Cross-check semantics after GC: not-found still undefined, store contract still on
try {
    sarr[1] = {};
    print("contract after gc: no throw");
} catch (e) {
    print("contract after gc throws TypeError: " + (e instanceof TypeError));
}
print("oob read after gc: " + (sarr[5] === undefined));

print("normal func gc survival over");
