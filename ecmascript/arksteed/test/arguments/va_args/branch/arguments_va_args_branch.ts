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
//! METHOD      va_args_branch
//! HAS         Call
//! HAS         CallRuntime  OptCopyRestArgs
//! COMPILE_COUNT_GE *va_args_branch* 1
//! EXEC_COUNT_GE *va_args_branch* 1
//! METHOD      invokeBranchTarget
//! HAS         CallKnownJSFunction
//! COMPILE_COUNT_GE *invokeBranchTarget* 1
//! EXEC_COUNT_GE *invokeBranchTarget* 1
//! DEOPT_COUNT *Deoptimize* 0
function va_args_branch(...args: number[]) {
    if (args.length === 0) {
        print(10);
    } else if (args.length === 1) {
        print(args[0]);
    } else {
        print(args[0] + args[args.length - 1]);
    }
}

ArkTools.arkSteedCompileSync(va_args_branch);


va_args_branch();
va_args_branch(7);
va_args_branch(3, 4, 5);

// Additive G11.6 evidence: silent same-domain callers invoke each compiled
// original target through a known-JS call plan (the call arity matches the
// target's compiled parameter count), so this leaf's own dump carries the
// actual invocation form. stdout is suppressed while the callers are warmed and
// entered and `print` is restored afterwards, so every original probe and every
// expectation byte is preserved.
function invokeBranchTarget() {
    return va_args_branch(0);
}
let savedPrint = print;
print = function () {};
for (let round = 0; round < 4; round++) {
    invokeBranchTarget();
}
ArkTools.arkSteedCompileSync(invokeBranchTarget);
invokeBranchTarget();

print = savedPrint;
