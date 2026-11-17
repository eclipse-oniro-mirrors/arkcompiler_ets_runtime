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

//! PARAMS --compiler-jit-hotness-threshold=100000 --compiler-max-inline-bytecodes=0
//! DEOPT_COUNT Lazy* 10
//! DEOPT_COUNT Deoptimize* 0

function read(receiver) { return receiver.value; }
// Keep the receiver shape stable so retained feedback does not become megamorphic.
const prototypes = [{ value: 0 }];
for (let i = 1; i <= 10; i++) {
    prototypes.push(Object.create(prototypes[i - 1]));
}
const receiver = Object.create(prototypes[10]);
for (let round = 1; round <= 10; round++) {
    for (let i = 0; i < 32; i++) { read(receiver); }
    ArkTools.arkSteedCompileSync(read);
    print(ArkTools.arkSteedIsCompiled(read));
    // Shadow the current holder on a nearer prototype to invalidate each compilation.
    prototypes[round].value = round;
    print(ArkTools.arkSteedIsCompiled(read));
    if (read(receiver) !== round) {
        throw new Error("Unexpected property value after deoptimization");
    }
}
// After the default limit of 10 invalidations, both explicit compilation requests must be rejected.
ArkTools.arkSteedCompileSync(read);
print(ArkTools.arkSteedIsCompiled(read));
ArkTools.arkSteedCompileAsync(read);
ArkTools.waitAllJitCompileFinish();
print(ArkTools.arkSteedIsCompiled(read));
