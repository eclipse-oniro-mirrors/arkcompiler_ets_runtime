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
 * @tc.name:normal_func_not_found
 * @tc.desc: not-found load IC on sendable receivers in normal functions (US-1)
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

// AC-1.1 + AC-1.2: not-found load returns undefined and installs IC (mono)
function loadNotFound(obj) {
    let r;
    for (let i = 0; i < 100000; ++i) {
        r = obj.nosuch;
    }
    return r;
}
print("notfound value: " + loadNotFound(sp));
// Slot calibration (es2abc, 2026-09-18): the ldobjbyname site in loadNotFound sits at
// slot 1 (slot 0 stays uninit); ICKind 0 = NamedLoadIC. Recalibrate if the frontend
// changes slot numbering.
print("notfound ic: " + ArkTools.getICState(loadNotFound, 1, 0));

// AC-1.3: by-value mixed keys, behavioral correctness (element IC for JSSharedArray
// arrives in later tasks; conflict-complete form is covered by
// normal_func_sharedarray_store.ts)
function loadByKey(o, k) {
    return o[k];
}
print("mix elem: " + loadByKey(sarr, 0));
print("mix notfound: " + loadByKey(sp, "nosuch"));
print("mix elem again: " + loadByKey(sarr, 0));
print("mix notfound again: " + loadByKey(sp, "nosuch"));
print("mix notfound once more: " + loadByKey(sp, "nosuch"));

// AC-1.4: not-found store keeps TypeError (error path has no IC value)
try {
    sp.nosuch = 1;
    print("store notfound: no throw");
} catch (e) {
    print("store notfound throws TypeError: " + (e instanceof TypeError));
}

ArkTools.arkSteedCompileSync(loadNotFound);
ArkTools.waitJitCompileFinish(loadNotFound);
print(ArkTools.isAOTCompiled(loadNotFound));
ArkTools.arkSteedCompileSync(loadByKey);
ArkTools.waitJitCompileFinish(loadByKey);
print(ArkTools.isAOTCompiled(loadByKey));

print("after compile: " + loadNotFound(sp));
print("mix after compile: " + loadByKey(sarr, 0));
print("notfound after compile: " + loadByKey(sp, "nosuch"));

print("normal func not-found ic over");
