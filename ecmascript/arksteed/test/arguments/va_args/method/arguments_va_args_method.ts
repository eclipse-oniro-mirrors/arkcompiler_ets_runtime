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
//! METHOD      VaArgsMethod
//! COMPILE_COUNT_GE *VaArgsMethod* 1
//! EXEC_COUNT_GE *VaArgsMethod* 1
//! METHOD      constructTargetObject
//! COUNT       CallNewObjRangeBase 1
//! COUNT       CallNewObjRangeDerived 1
//! HAS_NOT     CallRuntime OptNewObjRange
//! COMPILE_COUNT_GE *constructTargetObject* 1
//! EXEC_COUNT_GE *constructTargetObject* 1
//! DEOPT_COUNT *Deoptimize* 0
class VaArgsMethod {
    factor: number;

    constructor(factor: number) {
        this.factor = factor;
    }

    compute(...args: number[]): number {
        let sum = 0;
        for (let i = 0; i < args.length; i++) {
            sum += args[i];
        }
        return sum * this.factor;
    }
}

ArkTools.arkSteedCompileSync(VaArgsMethod);


let worker = new VaArgsMethod(2);
print(worker.compute());
print(worker.compute(3));
print(worker.compute(1, 2, 3));

// Additive G11.6 evidence: a silent caller of the same domain proves the actual
// construction form of the compiled original target in this case's own dump.
function constructTargetObject() {
    return new VaArgsMethod(2);
}

for (let round = 0; round < 4; round++) {
    constructTargetObject();
}
ArkTools.arkSteedCompileSync(constructTargetObject);
constructTargetObject();
