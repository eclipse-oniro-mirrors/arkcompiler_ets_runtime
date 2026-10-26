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
// Additive G11.6 evidence for the `functions/this` family. The three probes
// below invoke the compiled `this_1` with a primitive Number receiver and keep
// their original results (strict mode does not coerce `this`; ECMA-262 2021
// 10.2.1.2 OrdinaryCallBindThis, 13.3.6.2 EvaluateCall). A silent caller of the
// same domain then invokes the compiled original target through a known-JS call
// plan, so this leaf's own dump carries the actual invocation form; the target's
// own entry stays proven by its own EXEC_COUNT_GE.
//! PARAMS --compiler-jit-hotness-threshold=60000

//! METHOD      invokeThis1Target
//! HAS         CallKnownJSFunction
//! COMPILE_COUNT_GE *invokeThis1Target* 1
//! EXEC_COUNT_GE *invokeThis1Target* 1
//! COMPILE_COUNT_GE *this_1* 1
//! EXEC_COUNT_GE *this_1* 1
//! DEOPT_COUNT *Deoptimize* 0
function this_1() {
    return this + 42;
}

ArkTools.arkSteedCompileSync(this_1);

print(this_1.call(100));
print(this_1.call(200));
print(this_1.call(300));

// The silent caller runs after the three original probes, prints nothing and
// therefore preserves every observation and every expectation byte.
function invokeThis1Target() {
    return this_1();
}

for (let round = 0; round < 4; round++) {
    invokeThis1Target();
}
ArkTools.arkSteedCompileSync(invokeThis1Target);
invokeThis1Target();
