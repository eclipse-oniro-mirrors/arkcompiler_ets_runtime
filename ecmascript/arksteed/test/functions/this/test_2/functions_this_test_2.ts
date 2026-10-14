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
// below invoke the compiled `this_2` with a primitive Number receiver and a
// Number argument and keep their original results (strict mode does not coerce
// `this`; ECMA-262 2021 10.2.1.2, 13.3.6.2). A silent caller of the same domain
// then invokes the compiled original target through a known-JS call plan, so
// this leaf's own dump carries the actual invocation form; the target's own
// entry stays proven by its own EXEC_COUNT_GE.
//! PARAMS --compiler-jit-hotness-threshold=60000

//! METHOD      invokeThis2Target
//! HAS         CallKnownJSFunction
//! COMPILE_COUNT_GE *invokeThis2Target* 1
//! EXEC_COUNT_GE *invokeThis2Target* 1
//! COMPILE_COUNT_GE *this_2* 1
//! EXEC_COUNT_GE *this_2* 1
//! DEOPT_COUNT *Deoptimize* 0
function this_2(a: number) {
    a += this;
    return a;
}

ArkTools.arkSteedCompileSync(this_2);

// TODO: Replace the spin loop with:
// let res = ArkTools.waitJitCompileFinish(this_2);

print(this_2.call(42, 100));
print(this_2.call(42, 200));
print(this_2.call(42, 300));

// The silent caller runs after the three original probes, prints nothing and
// therefore preserves every observation and every expectation byte.
function invokeThis2Target(value: number) {
    return this_2(value);
}

for (let round = 0; round < 4; round++) {
    invokeThis2Target(0);
}
ArkTools.arkSteedCompileSync(invokeThis2Target);
invokeThis2Target(0);
