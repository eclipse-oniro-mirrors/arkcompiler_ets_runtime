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
// G11.6 additive evidence (S11/C1/C3/C4): the compiled constructor performs no
// JS call, so the actual construction form of this leaf and the
// optional-parameter method invocation are carried by its own silent
// invocation carrier `callDefaultParamsUndefinedMethod`, whose dump holds
// `CallRuntime OptNewObjRange` and the generic `Call` of `compute`. The
// original constructor keeps its own positive compile/entry evidence, the
// probes and the output bytes are unchanged, and the normal path must not
// deopt. The `_GE` lower bound allows the installation and recompilation
// multiplicity of the same method.
//! METHOD callDefaultParamsUndefinedMethod
//! HAS CallRuntime OptNewObjRange
//! HAS Call
//! COMPILE_COUNT_GE callDefaultParamsUndefinedMethod 1
//! EXEC_COUNT_GE *callDefaultParamsUndefinedMethod* 1
//! METHOD DefaultParamUndefinedMethod
//! COMPILE_COUNT_GE DefaultParamUndefinedMethod 1
//! EXEC_COUNT_GE *DefaultParamUndefinedMethod* 1
//! DEOPT_COUNT *Deoptimize* 0

class DefaultParamUndefinedMethod {
    scale: number;

    constructor(scale: number) {
        this.scale = scale;
    }

    compute(value?: number, extra?: number): number {
        let left = value === undefined ? 100 : value * this.scale;
        let right = extra === undefined ? 200 : extra + this.scale;
        return left + right;
    }
}

function callDefaultParamsUndefinedMethod(): number {
    let probe = new DefaultParamUndefinedMethod(3);
    return probe.compute() + probe.compute(2) + probe.compute(2, 5);
}

ArkTools.arkSteedCompileSync(DefaultParamUndefinedMethod);
ArkTools.arkSteedCompileSync(callDefaultParamsUndefinedMethod);


let worker = new DefaultParamUndefinedMethod(3);
print(worker.compute());
print(worker.compute(2));
print(worker.compute(2, 5));
callDefaultParamsUndefinedMethod();
