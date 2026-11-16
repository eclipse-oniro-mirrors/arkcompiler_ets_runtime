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

// Test: sendable not-found IC — a sendable class instance's missing property
// load hits the NON_EXIST handler after distillation, and the answer (undefined)
// stays correct after ArkSteed compilation.
//! METHOD loadMissing
//! HAS LoadTaggedField
//! COMPILE_COUNT_GE *loadMissing* 1

class SPoint {
    x: number = 1;
    constructor() { "use sendable"; }
}

let sp = new SPoint();

function loadMissing(o) {
    let r;
    for (let i = 0; i < 200; i++) { r = o.nosuch; }
    return r;
}

print("before compile: " + loadMissing(sp));
ArkTools.arkSteedCompileSync(loadMissing);
ArkTools.waitJitCompileFinish(loadMissing);
print(ArkTools.isAOTCompiled(loadMissing));
print("after compile: " + loadMissing(sp));
