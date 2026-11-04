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

//! COMPILE_COUNT_GE *captureArguments* 1
// 4 basic/fresh calls + 8 varying-argc calls + 30000 stress calls after compilation.
//! EXEC_COUNT_GE *captureArguments* 30012
//! DEOPT_COUNT * 0
//! METHOD captureArguments
//! BYTECODE_COUNT getunmappedargs 1
//! COUNT CanAllocateInYoung 2
//! COUNT AllocateInYoung 2
//! COUNT BranchIfTrue 2
//! COUNT RawI64ToTagged 1
//! COUNT I64BitwiseBinary 1
//! COUNT InitializeUnmappedArgumentsElements 1
//! COUNT InitializeUnmappedArguments 1
//! COUNT I64Sub 1
//! COUNT I64Add 1
//! COUNT I64ShiftLeft 1
//! COUNT LoadActualArgument 1
//! HAS_NOT StoreTaggedFieldWithBarrier
//! COUNT StoreTaggedElementWithBarrier 1
//! COUNT JumpLoop 1
//! COUNT Phi 3
//! HAS_NOT BranchIfInt64Compare
//! COUNT BranchIfInt32Compare 1
//! HAS_NOT I32ConditionCheck
//! HAS_NOT CallCommonStub GetUnmappedArgs
//! COUNT CallRuntime AllocateInYoung 2
//! CODEGEN_COUNT CanAllocateInYoung capacity predicate 2
//! CODEGEN_COUNT AllocateInYoung bump allocation 2
//! CODEGEN_COUNT *CallRuntime*RTStub_AllocateInYoung* 2
//! CODEGEN_COUNT InitializeUnmappedArgumentsElements 1
//! CODEGEN_COUNT InitializeUnmappedArgumentsObject 1

function captureArguments() {
    return arguments;
}

function forceYoungGC() {
    if (ArkTools.GC !== undefined) {
        ArkTools.GC.startGC("young", undefined, true);
        return;
    }
    ArkTools.gc();
}

function forceFullGC() {
    ArkTools.forceFullGC();
    if (ArkTools.GC !== undefined) {
        ArkTools.GC.startGC("full", undefined, true);
    }
}

let interpreted = captureArguments("interpreter", 1);
ArkTools.arkSteedCompileSync(captureArguments);
ArkTools.waitJitCompileFinish(captureArguments);

print("compiled:" + ArkTools.arkSteedIsCompiled(captureArguments));
print("interpreted:" +
    (interpreted.length === 2 && interpreted[0] === "interpreter" && interpreted[1] === 1));

let empty = captureArguments();
let objectValue = { marker: 42 };
let values = captureArguments(7, 1.25, "text", objectValue, undefined, null);
let basicOk = empty.length === 0 && values.length === 6 && values[0] === 7 && values[1] === 1.25 &&
    values[2] === "text" && values[3] === objectValue && values[4] === undefined && values[5] === null;
print("basic:" + basicOk);

let first = captureArguments("first");
let second = captureArguments("second");
first[0] = "changed";
print("fresh:" + (first !== second && first[0] === "changed" && second[0] === "second"));

// Reuse the same compiled function with zero, small, threshold and huge counts.
// 21843/21844 straddle the regular-object limit for a 256 KiB heap region.
let varyingOk = true;
let largeSurvivors = [];
for (let count of [0, 1, 7, 21843, 21844, 32768, 2, 0]) {
    let input = [];
    for (let i = 0; i < count; i++) {
        input.push(i % 2 === 0 ? objectValue : i + 0.5);
    }
    let current = captureArguments.apply(undefined, input);
    varyingOk = varyingOk && current.length === count && ArkTools.arkSteedIsCompiled(captureArguments);
    for (let i = 0; i < count; i++) {
        varyingOk = varyingOk && current[i] === input[i];
    }
    if (count >= 21843) {
        largeSurvivors.push(current);
    }
}
print("varying-argc:" + varyingOk);

const stressIterations = 30000;
const survivorStride = 1500;
let survivors = [];
let stressOk = true;
for (let i = 0; i < stressIterations; i++) {
    let value = { index: i };
    let current = captureArguments(value, i + 0.5);
    if (current.length !== 2 || current[0] !== value || current[1] !== i + 0.5) {
        stressOk = false;
        break;
    }
    if (i % survivorStride === 0) {
        survivors.push(current);
    }
}
print("buffer-exhaustion:" + stressOk);

function checkSurvivors() {
    if (survivors.length !== stressIterations / survivorStride || largeSurvivors.length !== 3) {
        return false;
    }
    let lengths = [21843, 21844, 32768];
    for (let i = 0; i < largeSurvivors.length; i++) {
        if (largeSurvivors[i].length !== lengths[i]) {
            return false;
        }
        for (let j = 0; j < lengths[i]; j++) {
            let expected = j % 2 === 0 ? objectValue : j + 0.5;
            if (largeSurvivors[i][j] !== expected) {
                return false;
            }
        }
    }
    for (let i = 0; i < survivors.length; i++) {
        let expected = i * survivorStride;
        if (survivors[i].length !== 2 || survivors[i][0].index !== expected ||
            survivors[i][1] !== expected + 0.5) {
            return false;
        }
    }
    return true;
}

forceYoungGC();
print("young-gc-survivors:" + checkSurvivors());
forceFullGC();
print("full-gc-survivors:" + checkSurvivors());
