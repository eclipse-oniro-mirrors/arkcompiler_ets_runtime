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
//! METHOD      VaArgsConstructor
//! HAS         CallRuntime  OptCopyRestArgs
//! COMPILE_COUNT_GE *VaArgsConstructor* 1
//! EXEC_COUNT_GE *VaArgsConstructor* 1
//! METHOD      constructTargetObject
//! HAS         CallRuntime  OptNewObjRange
//! COMPILE_COUNT_GE *constructTargetObject* 1
//! EXEC_COUNT_GE *constructTargetObject* 1
//! DEOPT_COUNT *Deoptimize* 0
class VaArgsConstructor {
    count: number;
    total: number;

    constructor(...args: number[]) {
        this.count = args.length;
        this.total = 0;
        for (let i = 0; i < args.length; i++) {
            this.total += args[i];
        }
    }
}

ArkTools.arkSteedCompileSync(VaArgsConstructor);


let a = new VaArgsConstructor();
print(a.count);
print(a.total);

let b = new VaArgsConstructor(4, 5, 6);
print(b.count);
print(b.total);

// Additive G11.6 evidence: a silent caller of the same domain proves the actual
// construction form of the compiled original target in this case's own dump.
function constructTargetObject() {
    return new VaArgsConstructor(4, 5, 6);
}

for (let round = 0; round < 4; round++) {
    constructTargetObject();
}
ArkTools.arkSteedCompileSync(constructTargetObject);
constructTargetObject();
