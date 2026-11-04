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
//! METHOD peelStringCompare
//! COUNT JumpLoop 1
//! HAS StringEqual
//! COUNT LoadTaggedField 2 # GlobalEnv and holder.value, each loaded once.
//! METHOD peelStringLiteral
//! COUNT JumpLoop 1
//! HAS StringEqual
//! HAS HeapConstant
//! HAS_NOT CallCommonStub GetStringFromConstPool
//! COUNT LoadTaggedField 2 # GlobalEnv and holder.value, each loaded once.
//! METHOD peelMutatingCall
//! COUNT JumpLoop 1
//! HAS CallKnownJSFunction
//! COUNT_GE LoadTaggedField 2

function peelStringCompare(holder, left, right, n) {
    let sum = 0;
    for (let i = 0; i < n; ++i) {
        sum += holder.value;
        if (left === right) {
            sum += 1;
        }
        sum += holder.value;
    }
    return sum;
}

function peelStringLiteral(holder, text, n) {
    let sum = 0;
    for (let i = 0; i < n; ++i) {
        sum += holder.value;
        if (text === "loop-effect-constant") {
            sum += 1;
        }
        sum += holder.value;
    }
    return sum;
}

function mutate(holder) {
    holder.value += 1;
}

function peelMutatingCall(holder, callback, n) {
    let sum = 0;
    for (let i = 0; i < n; ++i) {
        sum += holder.value;
        callback(holder);
        sum += holder.value;
    }
    return sum;
}

const holder = {value: 7};
const left = "loop-effect-" + "constant";
const right = ["loop", "effect", "constant"].join("-");
for (let i = 0; i < 8; ++i) {
    peelStringCompare(holder, left, right, 4);
    peelStringCompare(holder, left, "different", 4);
    peelStringLiteral(holder, right, 4);
    peelMutatingCall(holder, mutate, 4);
    holder.value = 7;
}
ArkTools.arkSteedCompileSync(peelStringCompare);
ArkTools.arkSteedCompileSync(peelStringLiteral);
ArkTools.arkSteedCompileSync(peelMutatingCall);
for (const n of [0, 1, 2, 8]) {
    holder.value = 7;
    let ok = peelStringCompare(holder, left, right, n) === n * 15;
    ok = ok && peelStringCompare(holder, left, "different", n) === n * 14;
    ok = ok && peelStringLiteral(holder, right, n) === n * 15;
    ok = ok && peelStringLiteral(holder, "different", n) === n * 14;
    ok = ok && peelMutatingCall(holder, mutate, n) === n * 14 + n * n;
    print(ok ? "PASS" : "FAIL");
}
