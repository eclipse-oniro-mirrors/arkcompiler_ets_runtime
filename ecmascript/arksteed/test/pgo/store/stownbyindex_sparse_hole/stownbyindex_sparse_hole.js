/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

//! PARAMS --compiler-jit-hotness-threshold=60000
//! METHOD holey
//! COUNT_GE StoreTaggedElement 2                   # (1 per fast path) * 2

// A hole inside an all-variable literal makes the next stownbyindex append
// sparsely (index 2 while the length is still 1): the hole must survive and
// the length must jump to index + 1.
function holey(a, b)
{
    return [a, , b];
}

for (let i = 0; i < 20000; i++) {
    holey(i, i + 2);
}

ArkTools.arkSteedCompileSync(holey);
ArkTools.waitJitCompileFinish(holey);

let ok = ArkTools.isAOTCompiled(holey);
for (let i = 0; i < 2048; i++) {
    let r = holey(i, i + 2);
    ok = ok && r.length === 3 && r[0] === i && r[2] === i + 2 && (1 in r) === false && (0 in r) && (2 in r);
    if ((i & 127) === 0) {
        ArkTools.gc();
    }
}
print("compiled: " + ArkTools.isAOTCompiled(holey));
print(ok ? "PASS" : "FAIL");
