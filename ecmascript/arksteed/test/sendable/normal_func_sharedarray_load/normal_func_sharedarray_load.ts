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
 * @tc.name:normal_func_sharedarray_load
 * @tc.desc: JSSharedArray element load IC in normal functions (US-3, load half)
 * @tc.type: FUNC
 */

// @ts-nocheck
declare function print(str: any): string;

let sarr = new SendableArray(1, 2, 3);

// AC-3.1: in-bounds element read installs element IC (mono).
// Site is ldobjbyvalue slot 1 (calibrated, es2abc 2026-09-18), ICKind 2 = LoadIC.
function loadElem(arr) {
    let r;
    for (let i = 0; i < 2000; ++i) { r = arr[0]; }
    return r;
}
print("elem value: " + loadElem(sarr));
print("elem ic: " + ArkTools.getICState(loadElem, 1, 2));

// AC-3.3: out-of-bounds read returns undefined via slow path (behavior kept)
function loadElemByVar(arr, k) {
    let r;
    for (let i = 0; i < 2000; ++i) { r = arr[k]; }
    return r;
}
print("oob read: " + loadElemByVar(sarr, 5));
print("inbounds byvar: " + loadElemByVar(sarr, 1));

ArkTools.arkSteedCompileSync(loadElem);
ArkTools.waitJitCompileFinish(loadElem);
print(ArkTools.isAOTCompiled(loadElem));
ArkTools.arkSteedCompileSync(loadElemByVar);
ArkTools.waitJitCompileFinish(loadElemByVar);
print(ArkTools.isAOTCompiled(loadElemByVar));

print("elem after compile: " + loadElem(sarr));
print("oob after compile: " + (sarr[100] === undefined));
print("byvar inbounds after compile: " + loadElemByVar(sarr, 1));
print("byvar oob after compile: " + loadElemByVar(sarr, 5));

print("normal func sharedarray load over");
