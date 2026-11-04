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
// G11.6 additive evidence (S11/C1/C3/C4): the compiled invocation carrier
// `callDefaultParamUndefinedPair` performs this leaf's omitted, partially
// supplied and fully supplied invocations, so its own dump carries the actual
// generic `Call` form of those call sites instead of the callee-internal print
// calls. The original target keeps its own positive compile/entry evidence,
// the probes and the output bytes are unchanged, and the normal path must not
// deopt. The `_GE` lower bound allows the installation and recompilation
// multiplicity of the same method.
//! METHOD callDefaultParamUndefinedPair
//! HAS Call
//! COMPILE_COUNT_GE callDefaultParamUndefinedPair 1
//! EXEC_COUNT_GE *callDefaultParamUndefinedPair* 1
//! METHOD default_param_undefined_pair
//! COMPILE_COUNT_GE default_param_undefined_pair 1
//! EXEC_COUNT_GE *default_param_undefined_pair* 1
//! DEOPT_COUNT *Deoptimize* 0

function default_param_undefined_pair(a?: number, b?: number) {
    print(a === undefined ? 1 : 0);
    print(b === undefined ? 1 : 0);
}

function callDefaultParamUndefinedPair(): void {
    default_param_undefined_pair();
    default_param_undefined_pair(3);
    default_param_undefined_pair(3, 4);
}

ArkTools.arkSteedCompileSync(default_param_undefined_pair);
ArkTools.arkSteedCompileSync(callDefaultParamUndefinedPair);

callDefaultParamUndefinedPair();
