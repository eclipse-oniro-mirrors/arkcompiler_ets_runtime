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
//! DEOPT_COUNT Deoptimize* 10
//! DEOPT_COUNT Lazy* 0

//! METHOD unsignedResult
//! HAS TypedArrayDoubleLoadElement
//! HAS CheckedNonNegativeI32ToTaggedInt
function unsignedResult(array, index) { return array[index] >>> 0; }

const values = new Uint32Array([0, 1]);
const highUnsignedValue = 4294967295;
for (let round = 1; round <= 10; round++) {
    values[0] = 0;
    for (let i = 0; i < 32; i++) { unsignedResult(values, i & 1); }
    ArkTools.arkSteedCompileSync(unsignedResult);
    print(ArkTools.arkSteedIsCompiled(unsignedResult));
    // Both integer and Number feedback retain the unsigned result representation check.
    // Keep the receiver and index valid so only that check triggers eager deoptimization.
    values[0] = highUnsignedValue;
    const result = unsignedResult(values, 0);
    print(ArkTools.arkSteedIsCompiled(unsignedResult));
    if (result !== highUnsignedValue) {
        throw new Error("Unexpected unsigned result after deoptimization");
    }
}
// After the default limit of 10 eager deoptimizations, both requests must be rejected.
ArkTools.arkSteedCompileSync(unsignedResult);
print(ArkTools.arkSteedIsCompiled(unsignedResult));
ArkTools.arkSteedCompileAsync(unsignedResult);
ArkTools.waitAllJitCompileFinish();
print(ArkTools.arkSteedIsCompiled(unsignedResult));
