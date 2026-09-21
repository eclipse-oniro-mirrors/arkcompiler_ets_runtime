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
//! METHOD makeWide
//! COUNT StoreTaggedElement 8                      # (1 per fast path) * 8

// A long all-variable literal keeps appending past every grown capacity
// (0 -> 3 -> 6 -> 12 -> 24), exercising the grow slow path repeatedly while
// the remaining stores stay inline.
function makeWide(a, b, c, d, e, f, g, h)
{
    return [a, b, c, d, e, f, g, h];
}

for (let i = 0; i < 20000; i++) {
    makeWide(i, i + 1, i + 2, i + 3, i + 4, i + 5, i + 6, i + 7);
}

ArkTools.arkSteedCompileSync(makeWide);
ArkTools.waitJitCompileFinish(makeWide);

let ok = ArkTools.isAOTCompiled(makeWide);
for (let i = 0; i < 2048; i++) {
    let r = makeWide(i, i + 1, i + 2, i + 3, i + 4, i + 5, i + 6, i + 7);
    ok = ok && r.length === 8;
    for (let j = 0; j < 8; j++) {
        ok = ok && r[j] === i + j;
    }
    if ((i & 127) === 0) {
        ArkTools.gc();
    }
}
print("compiled: " + ArkTools.isAOTCompiled(makeWide));
print(ok ? "PASS" : "FAIL");
