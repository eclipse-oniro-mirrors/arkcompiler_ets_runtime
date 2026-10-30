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

//! PARAMS --compiler-jit-hotness-threshold=60000 --compiler-enable-jit-lazy-deopt=true
//! METHOD unsignedEager
//! HAS Phi
//! HAS U32ToF64
//! HAS U32ToTaggedNumber
//! METHOD unsignedLazy
//! HAS Phi
//! HAS U32ToF64
//! HAS U32ToTaggedNumber
//! METHOD fractionalColdDeopt
//! HAS Phi
//! HAS DoubleToInt32
//! HAS Deopt

function unsignedEager(input, flag, receiver) {
    const unsigned = input.length;
    const merged = flag ? unsigned : 0;
    const number = merged + 0.5;
    const field = receiver.value;
    return unsigned + number + field;
}

let invalidate = false;
let invalidatedInsideCall = false;
function changeReceiver(receiver) {
    if (invalidate) {
        receiver.extra = 1;
        invalidatedInsideCall = !ArkTools.arkSteedIsCompiled(unsignedLazy);
    }
}

function unsignedLazy(input, flag, receiver) {
    const unsigned = input.length;
    const merged = flag ? unsigned : 0;
    receiver.value = merged + 0.5;
    changeReceiver(receiver);
    return unsigned + receiver.value;
}

function fractionalColdDeopt(input, flag, cold, receiver) {
    // Computed values, not float constants: exercise truncation eligibility.
    const original = flag ? input * 1.0 : input + 0.5;
    const truncated = original | 0;
    if (cold) {
        const field = receiver.untrained;
        return original + field;
    }
    return truncated;
}

function compile(method) {
    ArkTools.arkSteedCompileSync(method);
    ArkTools.waitJitCompileFinish(method);
    if (!ArkTools.arkSteedIsCompiled(method)) {
        throw new Error("not compiled: " + method.name);
    }
}

// A sparse array length supplies U32ToTaggedNumber without allocating its elements.
// Unlike >>> 0, this does not insert a non-negative Int32 guard before the Phi.
const large = new Array(0x80000000);
ArkTools.toFastProperties(large);
for (let i = 0; i < 100; i++) {
    const flag = (i & 1) === 0;
    unsignedEager(large, flag, {value: 1});
    unsignedLazy(large, flag, {value: 0.5});
    fractionalColdDeopt(1.25, flag, false, {untrained: 0});
}

compile(unsignedEager);
const unsignedResult = unsignedEager(large, true, {different: 0, value: 1});
print(unsignedResult === 4294967297.5 &&
      (ArkTools.isAOTDeoptimized(unsignedEager) || !ArkTools.arkSteedIsCompiled(unsignedEager)) ? "PASS" : "FAIL eager");

compile(unsignedLazy);
invalidate = true;
const lazyResult = unsignedLazy(large, true, {value: 0.5});
print(lazyResult === 4294967296.5 && invalidatedInsideCall &&
      !ArkTools.arkSteedIsCompiled(unsignedLazy) ? "PASS" : "FAIL lazy");

compile(fractionalColdDeopt);
const fraction = fractionalColdDeopt(1.25, true, true, {untrained: 0});
print(fraction === 1.25 &&
      (ArkTools.isAOTDeoptimized(fractionalColdDeopt) || !ArkTools.arkSteedIsCompiled(fractionalColdDeopt)) ?
      "PASS" : "FAIL fraction");
