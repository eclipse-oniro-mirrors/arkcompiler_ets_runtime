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

//! PARAMS --compiler-jit-hotness-threshold=60000 --compiler-arksteed-enable-inline=false
//! METHOD tightIntLoop
//! COUNT_GE Phi 2
//! HAS F64Add
//! HAS DoubleToInt32
//! HAS_NOT UnsafeF64ToI32

function tightIntLoop(iterations)
{
    let sum = 0;
    for (let i = 0; i < iterations; i++) {
        sum = (sum + i * 7 + 3) | 0;
    }
    return sum;
}

// Warm up past Int32 addition overflow to collect floating-point feedback.
let ok = tightIntLoop(30000) === -1144982296;
ArkTools.arkSteedCompileSync(tightIntLoop);
ok = ArkTools.arkSteedIsCompiled(tightIntLoop) && ok;
ok = tightIntLoop(30000) === -1144982296 && ok;
ok = tightIntLoop(0) === 0 && ok;
print(ok ? "PASS" : "FAIL");
