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
//! METHOD allocateWithPressure
//! COUNT AllocateInYoung 6
//! COUNT InitializeEmptyObject 6
//! HAS_NOT CallCommonStub NewJSObject
//! HAS_NOT CallRuntime CreateEmptyObject

function allocateWithPressure(seed, count) {
    const root0 = {};
    const root1 = {};
    const root2 = {};
    const root3 = {};
    root0.tag = seed;
    root1[0] = seed + 1;
    root2.peer = root3;

    const i0 = seed + 10;
    const i1 = seed + 11;
    const i2 = seed + 12;
    const i3 = seed + 13;
    const i4 = seed + 14;
    const i5 = seed + 15;
    const i6 = seed + 16;
    const i7 = seed + 17;

    const d0 = seed * 1.125 + 0.25;
    const d1 = seed * 1.25 + 0.5;
    const d2 = seed * 1.375 + 0.75;
    const d3 = seed * 1.5 + 1.0;
    const d4 = seed * 1.625 + 1.25;
    const d5 = seed * 1.75 + 1.5;
    const d6 = seed * 1.875 + 1.75;
    const d7 = seed * 2.0 + 2.0;

    let checksum = 0;
    for (let i = 0; i < count; i++) {
        const item = {};
        if (item === root0 || item === root1 || item === root2 || item === root3) {
            return Number.NaN;
        }
        checksum = (checksum + (i & 255)) | 0;
    }

    if (root0.tag !== seed || root1[0] !== seed + 1 || root2.peer !== root3) {
        return Number.NaN;
    }

    return checksum + i0 + i1 + i2 + i3 + i4 + i5 + i6 + i7 +
        d0 + d1 + d2 + d3 + d4 + d5 + d6 + d7;
}

function expectedResult(seed, count) {
    let checksum = 0;
    for (let i = 0; i < count; i++) {
        checksum = (checksum + (i & 255)) | 0;
    }

    const integerValues = seed + 10 + seed + 11 + seed + 12 + seed + 13 +
        seed + 14 + seed + 15 + seed + 16 + seed + 17;
    const doubleValues = seed * 1.125 + 0.25 +
        seed * 1.25 + 0.5 + seed * 1.375 + 0.75 +
        seed * 1.5 + 1.0 + seed * 1.625 + 1.25 +
        seed * 1.75 + 1.5 + seed * 1.875 + 1.75 +
        seed * 2.0 + 2.0;
    return checksum + integerValues + doubleValues;
}

for (let i = 0; i < 64; i++) {
    allocateWithPressure(i & 7, 1);
}

ArkTools.arkSteedCompileSync(allocateWithPressure);
ArkTools.waitJitCompileFinish(allocateWithPressure);

let ok = ArkTools.isAOTCompiled(allocateWithPressure);
const seed = 33;
const count = 1 << 20;
ok = ok && allocateWithPressure(seed, count) === expectedResult(seed, count);

print(ok ? "PASS" : "FAIL");
