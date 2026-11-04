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

//! COMPILE_COUNT_GE *storeElement* 1
//! EXEC_COUNT_GE *storeElement* 1
//! DEOPT_COUNT_GE *Deoptimize* 1

// @ts-nocheck
declare function print(value: any): void;

function storeElement(receiver, index, value) {
    receiver[index] = value;
}

let warmup = [0, , 2];
for (let i = 0; i < 20; i++) {
    storeElement(warmup, 0, i);
}

ArkTools.arkSteedCompileSync(storeElement);
ArkTools.waitJitCompileFinish(storeElement);
// G11.3 evidence: the compiled entry precedes the Array.prototype
// accessor installation. The warm-up receiver is writable and index 0
// is present, so the probe stays on the compiled path and changes no
// printed observation.
storeElement(warmup, 0, 1);
let compiledBeforeMutation = ArkTools.arkSteedIsCompiled(storeElement);
let setterCallCount = 0;
let setterReceiver = null;
Object.defineProperty(Array.prototype, "1", {
    set(value) {
        setterCallCount++;
        setterReceiver = this;
    },
    configurable: true
});

let invalidated = !ArkTools.arkSteedIsCompiled(storeElement);
let receiver = [0, , 2];
storeElement(receiver, 1, 42);
let receiverMatched = setterReceiver === receiver;
let hasOwnElement = Object.prototype.hasOwnProperty.call(receiver, "1");
delete Array.prototype[1];

print(compiledBeforeMutation);
print(invalidated);
print(setterCallCount);
print(receiverMatched);
print(hasOwnElement);
