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
//! METHOD storeElement
//! HAS StoreTaggedElementWithBarrier
//! HAS DeoptIfElementsUnstable
//! HAS DeoptIfCOWElements
//! COMPILE_COUNT_GE storeElement 1
//! EXEC_COUNT_GE *storeElement 1
//! DEOPT_COUNT_GE Lazy Deoptimize* 1

let mutateReceiver = false;

function makeArray() {
    return [{ tag: 1 }, { tag: 2 }];
}

function invalidateInsideCall(receiver) {
    if (mutateReceiver) {
        receiver.extra = 1;
    }
}

function storeElement(receiver, index, value) {
    receiver[index] = value;
    invalidateInsideCall(receiver);
    return value;
}

for (let i = 0; i < 20000; i++) {
    storeElement(makeArray(), 0, { tag: i });
}

ArkTools.arkSteedCompileSync(storeElement);
ArkTools.waitJitCompileFinish(storeElement);
print(`${"compiled before mutation:"} ${ArkTools.arkSteedIsCompiled(storeElement)}`);

let probe = makeArray();
let probeValue = { tag: 7 };
let probeResult = storeElement(probe, 1, probeValue);
print(`${"probe result identity:"} ${probeResult === probeValue}`);
print(`${"probe element identity:"} ${probe[1] === probeValue}`);

mutateReceiver = true;
let receiver = makeArray();
let stored = { tag: 42 };
let result = storeElement(receiver, 0, stored);
print(`${"post result identity:"} ${result === stored}`);
print(`${"post element identity:"} ${receiver[0] === stored}`);
print(`${"post element tag:"} ${receiver[0].tag}`);
print(`${"post untouched element tag:"} ${receiver[1].tag}`);
print(`${"post mutated property:"} ${receiver.extra}`);
print(`${"compiled after mutation:"} ${ArkTools.arkSteedIsCompiled(storeElement)}`);
