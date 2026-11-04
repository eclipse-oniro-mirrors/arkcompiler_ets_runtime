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
//! METHOD makeTripleVar
//! COUNT StoreTaggedElement 3                      # (1 per fast path) * 3

// All-variable array literal: createemptyarray plus three appends, so the
// elements kind transitions from the initial array HClass during warmup and
// the first store grows the backing store from capacity 0.
function makeTripleVar(a, b, c)
{
    return [a, b, c];
}

for (let i = 0; i < 20000; i++) {
    makeTripleVar(i, i + 1, i + 2);
}

ArkTools.arkSteedCompileSync(makeTripleVar);
ArkTools.waitJitCompileFinish(makeTripleVar);

let ok = ArkTools.isAOTCompiled(makeTripleVar);
for (let i = 0; i < 2048; i++) {
    let r = makeTripleVar(i, i + 1, i + 2);
    ok = ok && r.length === 3 && r[0] === i && r[1] === i + 1 && r[2] === i + 2;
    if ((i & 127) === 0) {
        ArkTools.gc();
    }
}
print("compiled: " + ArkTools.isAOTCompiled(makeTripleVar));
print(ok ? "PASS" : "FAIL");
