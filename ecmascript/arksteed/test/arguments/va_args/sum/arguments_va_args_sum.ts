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
//! METHOD      va_args_sum
//! HAS         Call
//! HAS         CallRuntime  OptCopyRestArgs
//! COMPILE_COUNT_GE *va_args_sum* 1
//! EXEC_COUNT_GE *va_args_sum* 1
//! METHOD      invokeSumTarget
//! HAS         CallKnownJSFunction
//! COMPILE_COUNT_GE *invokeSumTarget* 1
//! EXEC_COUNT_GE *invokeSumTarget* 1
//! DEOPT_COUNT *Deoptimize* 0
function va_args_sum(...args: number[]) {
    let sum = 0;
    for (let i = 0; i < args.length; i++) {
        sum += args[i];
    }
    print(sum);
}

ArkTools.arkSteedCompileSync(va_args_sum);


va_args_sum();
va_args_sum(4);
va_args_sum(1, 2, 3, 4);

// Additive G11.6 evidence: silent same-domain callers invoke each compiled
// original target through a known-JS call plan (the call arity matches the
// target's compiled parameter count), so this leaf's own dump carries the
// actual invocation form. stdout is suppressed while the callers are warmed and
// entered and `print` is restored afterwards, so every original probe and every
// expectation byte is preserved.
function invokeSumTarget() {
    return va_args_sum(0);
}
let savedPrint = print;
print = function () {};
for (let round = 0; round < 4; round++) {
    invokeSumTarget();
}
ArkTools.arkSteedCompileSync(invokeSumTarget);
invokeSumTarget();

print = savedPrint;
