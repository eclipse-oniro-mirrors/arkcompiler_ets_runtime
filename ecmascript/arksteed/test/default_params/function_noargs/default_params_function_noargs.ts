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
// `callDefaultParamFunctionNoargs` performs this leaf's omitted-argument
// invocation, so its own dump carries the actual generic `Call` form of that
// call site instead of the callee-internal print calls. The original target
// keeps its own positive compile/entry evidence, the probes and the output
// bytes are unchanged, and the normal path must not deopt. The `_GE` lower
// bound allows the installation and recompilation multiplicity of the same
// method.
//! METHOD callDefaultParamFunctionNoargs
//! HAS Call
//! COMPILE_COUNT_GE callDefaultParamFunctionNoargs 1
//! EXEC_COUNT_GE *callDefaultParamFunctionNoargs* 1
//! METHOD default_param_function_noargs
//! COMPILE_COUNT_GE default_param_function_noargs 1
//! EXEC_COUNT_GE *default_param_function_noargs* 1
//! DEOPT_COUNT *Deoptimize* 0

function default_param_function_noargs(
    make: string = "Unknown",
    model: string = "Model",
    year: number = 2020
) {
    print(make);
    print(model);
    print(year);
}

function callDefaultParamFunctionNoargs(): void {
    default_param_function_noargs();
}

ArkTools.arkSteedCompileSync(default_param_function_noargs);
ArkTools.arkSteedCompileSync(callDefaultParamFunctionNoargs);

callDefaultParamFunctionNoargs();
