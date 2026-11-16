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

// Test: sendable prototype getter IC — a getter on a sendable class prototype
// hits the PrototypeHandler after distillation, and the getter executes with
// the correct receiver and returns the right value after ArkSteed compilation.
//! METHOD loadGetter
//! HAS LoadTaggedField
//! COMPILE_COUNT_GE *loadGetter* 1

class SPointG {
    x: number = 7;
    constructor() { "use sendable"; }
    get gx(): number { return this.x; }
}

let sg = new SPointG();

function loadGetter(o) {
    let r;
    for (let i = 0; i < 200; i++) { r = o.gx; }
    return r;
}

print("before compile: " + loadGetter(sg));
ArkTools.arkSteedCompileSync(loadGetter);
ArkTools.waitJitCompileFinish(loadGetter);
print(ArkTools.isAOTCompiled(loadGetter));
print("after compile: " + loadGetter(sg));
