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
//! METHOD peelPropertiesGrowth
//! COUNT JumpLoop 1
//! HAS EnsurePropertiesCapacity
//! HAS TransitionHClassWithBarrier

function makeObjects(n) {
    const objects = [];
    for (let i = 0; i < n; ++i) {
        objects.push({base: 7});
    }
    return objects;
}

function peelPropertiesGrowth(objects, n) {
    let sum = 0;
    for (let i = 0; i < n; ++i) {
        const object = objects[i];
        const before = object.base;
        object.a = i;
        object.b = i + 1;
        object.c = i + 2;
        object.d = i + 3;
        object.e = i + 4;
        object.f = i + 5;
        sum += before + object.base + object.a + object.f;
    }
    return sum;
}

for (let i = 0; i < 8; ++i) {
    peelPropertiesGrowth(makeObjects(4), 4);
}
ArkTools.arkSteedCompileSync(peelPropertiesGrowth);
for (const n of [0, 1, 2, 8]) {
    const objects = makeObjects(n);
    let ok = peelPropertiesGrowth(objects, n) === n * 19 + n * (n - 1);
    ArkTools.gc();
    for (let i = 0; i < n; ++i) {
        const object = objects[i];
        ok = ok && object.base === 7 && object.a === i && object.b === i + 1 &&
            object.c === i + 2 && object.d === i + 3 && object.e === i + 4 && object.f === i + 5;
    }
    print(ok ? "PASS" : "FAIL");
}
