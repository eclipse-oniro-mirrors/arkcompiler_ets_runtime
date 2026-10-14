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
// Additive G11.6 evidence for the `functions/this` family, including the
// family's mutable state. `this_3` reads and writes the module-level lexical
// binding `globalValue` inside the compiled method, so the leaf carries the
// captured-slot read/write evidence of its own dump; the three original probes
// run first, in order, and keep their results and their initial state
// (`globalValue` starts at 1; ECMA-262 2021 10.2.1.2, 13.3.6.2). A silent caller
// of the same domain then invokes the compiled original target through a
// known-JS call plan, so this leaf's own dump carries the actual invocation form;
// the target's own entry stays proven by its own EXEC_COUNT_GE.
//! PARAMS --compiler-jit-hotness-threshold=60000

//! METHOD      this_3
//! HAS         LoadTaggedField
//! HAS         StoreEnvSlot
//! METHOD      invokeThis3Target
//! HAS         CallKnownJSFunction
//! COMPILE_COUNT_GE *invokeThis3Target* 1
//! EXEC_COUNT_GE *invokeThis3Target* 1
//! COMPILE_COUNT_GE *this_3* 1
//! EXEC_COUNT_GE *this_3* 1
//! DEOPT_COUNT *Deoptimize* 0
let globalValue = 1;

function this_3(a: number) {
    a += globalValue;
    globalValue += this;
    return a > 0 ? this + a * 100 : this - a * 100;
}

ArkTools.arkSteedCompileSync(this_3);

// TODO: Replace the spin loop with:
// let res = ArkTools.waitJitCompileFinish(this_3);

print(this_3.call(1, 2));   // globalValue <- 3
print(this_3.call(2, -4));  // globalValue <- 5
print(this_3.call(3, -9));  // globalValue <- 8

// The silent caller runs after the three original probes, prints nothing and
// therefore preserves every observation, every expectation byte and the order
// and values of the three original `globalValue` mutations.
function invokeThis3Target(value: number) {
    return this_3(value);
}

for (let round = 0; round < 4; round++) {
    invokeThis3Target(1);
}
ArkTools.arkSteedCompileSync(invokeThis3Target);
invokeThis3Target(1);
