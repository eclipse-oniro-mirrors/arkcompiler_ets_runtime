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
//! METHOD peelStringReceiver
//! HAS_NOT Deopt
//! COUNT JumpLoop 1

// Keep result-array guards outside the method whose deopts are checked.
function packResults(length, rest) {
    return [length, rest];
}

function peelStringReceiver() {
    let rest = 0, length = 0, count = 1;
    for (; count--;) {
        ({"length": length, ...rest} = "123456789");
    }
    return packResults(length, rest);
}

peelStringReceiver();
ArkTools.arkSteedCompileSync(peelStringReceiver);
const result = peelStringReceiver();
print(result[0] === 9 && result[1][0] === "1" && result[1][8] === "9" ? "PASS" : "FAIL");

//! METHOD peelDeoptBackedge
//! HAS Deopt
//! HAS_NOT JumpLoop
function peelDeoptBackedge(receiver, count) {
    let current = receiver;
    let value = 0;
    for (let i = 0; i < count; ++i) {
        value = current.value;
        current = 1;
    }
    return value;
}

const receiver = {value: 7};
for (let i = 0; i < 16; ++i) {
    peelDeoptBackedge(receiver, 1);
}
ArkTools.arkSteedCompileSync(peelDeoptBackedge);
print(peelDeoptBackedge(receiver, 0) === 0 && peelDeoptBackedge(receiver, 1) === 7 &&
      peelDeoptBackedge(receiver, 2) === undefined ? "PASS" : "FAIL");

//! METHOD coldOuterBody
//! HAS Deopt
//! HAS JumpLoop
function coldOuterBody(receiver, count) {
    let sum = 0;
    for (let i = 0; i < count; ++i) {
        for (let j = 0; j < 1; ++j) {
            sum += j;
        }
        sum += receiver.value;
    }
    return sum;
}

//! METHOD coldOuterUpdate
//! HAS Deopt
//! HAS JumpLoop
function coldOuterUpdate(receiver, count) {
    let sum = 0;
    for (let i = 0; i < count; i += receiver.step) {
        for (let j = 0; j < 1; ++j) {
            sum += 1;
        }
    }
    return sum;
}

// The outer loops are not peeled. Their cold property loads cut off a body
// or a backedge, but liveness still needs the structural JumpLoop block.
const coldReceiver = {value: 7, step: 1};
for (let i = 0; i < 16; ++i) {
    coldOuterBody(coldReceiver, 0);
    coldOuterUpdate(coldReceiver, 0);
}
ArkTools.arkSteedCompileSync(coldOuterBody);
ArkTools.arkSteedCompileSync(coldOuterUpdate);
print(coldOuterBody(coldReceiver, 2) === 14 && coldOuterUpdate(coldReceiver, 2) === 2 ? "PASS" : "FAIL");
