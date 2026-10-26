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

//! COMPILE_COUNT_GE *registerPressure* 1
//! EXEC_COUNT *registerPressure* 256
//! DEOPT_COUNT * 0
//! METHOD registerPressure
//! BYTECODE_COUNT getunmappedargs 1
//! COUNT CanAllocateInYoung 2
//! COUNT AllocateInYoung 2
//! COUNT InitializeUnmappedArgumentsElements 1
//! COUNT InitializeUnmappedArguments 1
//! COUNT CallRuntime AllocateInYoung 2
//! HAS_NOT CallCommonStub GetUnmappedArgs

function registerPressure(seed, object0, object1, object2, object3) {
    let i0 = seed + 1;
    let i1 = seed + 2;
    let i2 = seed + 3;
    let i3 = seed + 4;
    let d0 = seed * 1.125 + 0.25;
    let d1 = seed * 1.25 + 0.5;
    let d2 = seed * 1.375 + 0.75;
    let d3 = seed * 1.5 + 1.0;
    let d4 = seed * 1.625 + 1.25;
    let d5 = seed * 1.75 + 1.5;
    let d6 = seed * 1.875 + 1.75;
    let d7 = seed * 2.0 + 2.0;
    let args = arguments;
    if (args.length !== 5 || args[0] !== seed || args[1] !== object0 || args[2] !== object1 ||
        args[3] !== object2 || args[4] !== object3) {
        return -1;
    }
    return i0 + i1 + i2 + i3 + object0.marker + object1.marker + object2.marker + object3.marker +
        d0 + d1 + d2 + d3 + d4 + d5 + d6 + d7;
}

function expectedPressure(seed, object0, object1, object2, object3) {
    return seed + 1 + seed + 2 + seed + 3 + seed + 4 +
        object0.marker + object1.marker + object2.marker + object3.marker +
        (seed * 1.125 + 0.25) + (seed * 1.25 + 0.5) + (seed * 1.375 + 0.75) +
        (seed * 1.5 + 1.0) + (seed * 1.625 + 1.25) + (seed * 1.75 + 1.5) +
        (seed * 1.875 + 1.75) + (seed * 2.0 + 2.0);
}

ArkTools.arkSteedCompileSync(registerPressure);
ArkTools.waitJitCompileFinish(registerPressure);
let pressureOk = ArkTools.arkSteedIsCompiled(registerPressure);
for (let i = 0; i < 256 && pressureOk; i++) {
    let seed = i + 0.125;
    let object0 = { marker: i + 10 };
    let object1 = { marker: i + 20 };
    let object2 = { marker: i + 30 };
    let object3 = { marker: i + 40 };
    pressureOk = Math.abs(registerPressure(seed, object0, object1, object2, object3) -
        expectedPressure(seed, object0, object1, object2, object3)) < 1e-9;
}
print("register-pressure:" + pressureOk);
