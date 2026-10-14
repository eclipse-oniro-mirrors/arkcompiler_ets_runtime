/*
 * Copyright (c) 2024 Huawei Device Co., Ltd.
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
//! METHOD      f
//! HAS         Call
//! HAS         CallCommonStub  GetUnmappedArgs
//! METHOD      printArgs
//! HAS         Call
//! HAS         CallCommonStub  GetUnmappedArgs
//! COMPILE_COUNT_GE *f* 1
//! EXEC_COUNT_GE *f* 1
//! COMPILE_COUNT_GE *printArgs* 1
//! EXEC_COUNT_GE *printArgs* 1
//! METHOD      invokeFTarget
//! HAS         CallKnownJSFunction
//! COMPILE_COUNT_GE *invokeFTarget* 1
//! EXEC_COUNT_GE *invokeFTarget* 1
//! METHOD      invokePrintArgsTarget
//! HAS         CallKnownJSFunction
//! COMPILE_COUNT_GE *invokePrintArgsTarget* 1
//! EXEC_COUNT_GE *invokePrintArgsTarget* 1
//! DEOPT_COUNT *Deoptimize* 0

let f = function(v0, v1, v2, v3, v4, v5, v6, v7, v8, v9) {
    let sum = 0;
    for (let i = 0; i < arguments.length; i++) {
        sum = sum + arguments[i]
    }
    print(sum)
}

f(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13)

function printArgs(...rest:any)
{
    print(arguments[0]);
    print(arguments[1]);
}

printArgs("aaa", 55555);

ArkTools.arkSteedCompileSync(f);
ArkTools.arkSteedCompileSync(printArgs);
let res = true;
f(0, 1, 2)
printArgs("aaa", 55555);
print(res)

// Additive G11.6 evidence: silent same-domain callers invoke each compiled
// original target through a known-JS call plan (the call arity matches the
// target's compiled parameter count), so this leaf's own dump carries the
// actual invocation form. stdout is suppressed while the callers are warmed and
// entered and `print` is restored afterwards, so every original probe and every
// expectation byte is preserved.
function invokeFTarget() {
    return f(0, 1, 2, 3, 4, 5, 6, 7, 8, 9);
}
function invokePrintArgsTarget() {
    return printArgs(0);
}
let savedPrint = print;
print = function () {};
for (let round = 0; round < 4; round++) {
    invokeFTarget();
    invokePrintArgsTarget();
}
ArkTools.arkSteedCompileSync(invokeFTarget);
ArkTools.arkSteedCompileSync(invokePrintArgsTarget);
invokeFTarget();
invokePrintArgsTarget();

print = savedPrint;
