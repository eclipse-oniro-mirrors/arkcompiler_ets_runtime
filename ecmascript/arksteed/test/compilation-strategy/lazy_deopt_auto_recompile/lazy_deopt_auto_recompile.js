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

//! PARAMS --compiler-jit-hotness-threshold=400 --compiler-jit-ic-feedback-threshold=20 --compiler-max-inline-bytecodes=0

const prototype = [];
function makeReceiver() {
    const receiver = [0, , 2];
    Object.setPrototypeOf(receiver, prototype);
    return receiver;
}
function store(receiver, index, value) { receiver[index] = value; }
for (let i = 0; i < 32; i++) { store(makeReceiver(), 0, i); }
ArkTools.arkSteedCompileSync(store);
print(ArkTools.arkSteedIsCompiled(store));
let setterCalls = 0;
let observedReceiver = null;
Object.defineProperty(prototype, "1", {
    set(value) { setterCalls++; observedReceiver = this; },
    configurable: true
});
// Dependency invalidation must clear code before the next call.
print(ArkTools.arkSteedIsCompiled(store));
const receiver = makeReceiver();
store(receiver, 1, 42);
print(setterCalls);
print(observedReceiver === receiver);
print(Object.prototype.hasOwnProperty.call(receiver, "1"));
delete prototype[1];
for (let i = 0; i < 10000; i++) { store(makeReceiver(), 0, i); }
ArkTools.waitAllJitCompileFinish();
print(ArkTools.arkSteedIsCompiled(store));
store(receiver, 1, 43);
print(receiver[1]);
print(setterCalls);
