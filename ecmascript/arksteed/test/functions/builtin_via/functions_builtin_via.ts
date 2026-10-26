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

// G11.6 additive evidence (arksteed-g116-call-closure-and-additive-evidence
// #C1/#C3/#C4): the invoked builtin is reached through `Function.prototype.call`,
// and this leaf keeps its tier boundary - `ArkTools.arkSteedCompileSync` installs
// `call_builtin_function_via_call` before its first call, so the call sites carry
// no monomorphic target feedback at compilation time. The leaf's own dump of that
// method therefore shows the generic `Call` invocation form for those sites, and
// the address annotation names exactly the form present in the dump instead of an
// invented `CallKnownBuiltin`. The builtin invocation form has its own carrier:
// the T1 pair `functions/call/call_closure_env_slot`.
// `COMPILE_COUNT_GE`/`EXEC_COUNT_GE` attribute compilation and entry of the
// claimed target method (E1/E2), and `DEOPT_COUNT *Deoptimize* 0` asserts that the
// normal path does not silently fall back into the interpreter.
// The target writes no captured variable: its dump contains no `StoreEnvSlot`
// node and no lexical-environment slot load, and its only `LoadTaggedField` nodes
// are the function-context and object-property loads that C3 excludes from
// captured-binding evidence. The conditional captured-slot annotation is
// therefore not applicable and is not fabricated.
//! METHOD call_builtin_function_via_call
//! HAS Call
//! COMPILE_COUNT_GE *call_builtin_function_via_call* 1
//! EXEC_COUNT_GE *call_builtin_function_via_call* 1
//! DEOPT_COUNT *Deoptimize* 0

function call_builtin_function_via_call() {
    print(Array.prototype.push.call([], 10));

    let arrayLike = { length: 0 };
    print(Array.prototype.push.call(arrayLike, 7));
    print(arrayLike[0]);
    print(arrayLike.length);
}

ArkTools.arkSteedCompileSync(call_builtin_function_via_call);


call_builtin_function_via_call();
