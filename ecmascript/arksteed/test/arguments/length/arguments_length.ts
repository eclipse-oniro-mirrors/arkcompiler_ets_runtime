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
//! METHOD      arguments_length
//! HAS         Call
//! HAS         CallCommonStub  GetUnmappedArgs
//! COMPILE_COUNT_GE *arguments_length* 1
//! EXEC_COUNT_GE *arguments_length* 1
//! METHOD      invokeLengthTarget
//! HAS         CallKnownJSFunction
//! COMPILE_COUNT_GE *invokeLengthTarget* 1
//! EXEC_COUNT_GE *invokeLengthTarget* 1
//! DEOPT_COUNT *Deoptimize* 0
function arguments_length() {
    print(arguments.length);
}

ArkTools.arkSteedCompileSync(arguments_length);


arguments_length();
arguments_length(1);
arguments_length(1, 2, 3);

// Additive G11.6 evidence: silent same-domain callers invoke each compiled
// original target through a known-JS call plan (the call arity matches the
// target's compiled parameter count), so this leaf's own dump carries the
// actual invocation form. stdout is suppressed while the callers are warmed and
// entered and `print` is restored afterwards, so every original probe and every
// expectation byte is preserved.
function invokeLengthTarget() {
    return arguments_length();
}
let savedPrint = print;
print = function () {};
for (let round = 0; round < 4; round++) {
    invokeLengthTarget();
}
ArkTools.arkSteedCompileSync(invokeLengthTarget);
invokeLengthTarget();

print = savedPrint;
