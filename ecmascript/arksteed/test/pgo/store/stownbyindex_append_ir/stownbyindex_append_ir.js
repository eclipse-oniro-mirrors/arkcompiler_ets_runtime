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
//! METHOD makeTriple
//! COUNT StoreTaggedElement 2                      # (1 per fast path) * 2
//! HAS StoreInt32Field

// Array literal with a non-constant element: the constant prefix comes from
// createarraywithbuffer and every stownbyindex appends at index == length.
// The store must be lowered to inline element stores (plus a grow slow path),
// not to the StOwnByIndex common stub.
function makeTriple(x)
{
    return [1, x, 3];
}

for (let i = 0; i < 20000; i++) {
    makeTriple(i);
}

ArkTools.arkSteedCompileSync(makeTriple);
ArkTools.waitJitCompileFinish(makeTriple);

let ok = ArkTools.isAOTCompiled(makeTriple);
for (let i = 0; i < 2048; i++) {
    let r = makeTriple(i);
    ok = ok && r.length === 3 && r[0] === 1 && r[1] === i && r[2] === 3;
    if ((i & 127) === 0) {
        ArkTools.gc();
    }
}
print("compiled: " + ArkTools.isAOTCompiled(makeTriple));
print(ok ? "PASS" : "FAIL");
