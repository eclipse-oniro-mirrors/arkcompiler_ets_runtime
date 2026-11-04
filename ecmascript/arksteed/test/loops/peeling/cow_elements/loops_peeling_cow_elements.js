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

//! PARAMS --compiler-jit-hotness-threshold=60000 --enable-force-gc=true
//! METHOD accumulateCOW
//! HAS EnsureWritableElements
//! HAS_NOT CallCommonStub CopyCOWElements
//! HAS StoreTaggedElement
//! HAS JumpLoop
//! HAS_NOT CallCommonStub SetPropertyByValue
function accumulateCOW(array, alias, count) {
    let sum = 0;
    for (let i = 0; i < count; ++i) {
        const index = i % 3;
        array[index] += 0.5;
        sum += alias[index];
    }
    return sum;
}

function literal() {
    return [0, 0, 0];
}

for (let i = 0; i < 20; ++i) {
    const array = literal();
    accumulateCOW(array, array, 12);
}
ArkTools.arkSteedCompileSync(accumulateCOW);
let ok = true;
for (const count of [0, 1, 2, 3, 12, 200]) {
    const array = literal();
    const sibling = literal();
    const expected = new Float64Array(3);
    let sum = 0;
    for (let i = 0; i < count; ++i) {
        expected[i % 3] += 0.5;
        sum += expected[i % 3];
    }
    const result = accumulateCOW(array, array, count);
    ArkTools.forceFullGC();
    ok = ok && result === sum && array.every((v, i) => v === expected[i]) &&
        sibling.every(v => v === 0) && array.length === 3;
}
print(ok);
print(ArkTools.arkSteedIsCompiled(accumulateCOW));
