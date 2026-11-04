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
// default-parameter method invocation are carried by its own silent invocation
// carrier `callDefaultParamsMethod`, whose dump holds
// `CallRuntime OptNewObjRange` and the generic `Call` of `compute`. The
// original constructor keeps its own positive compile/entry evidence, the
// probes and the output bytes are unchanged, and the normal path must not
// deopt. The `_GE` lower bound allows the installation and recompilation
// multiplicity of the same method.
//! METHOD callDefaultParamsMethod
//! HAS CallRuntime OptNewObjRange
//! HAS Call
//! COMPILE_COUNT_GE callDefaultParamsMethod 1
//! EXEC_COUNT_GE *callDefaultParamsMethod* 1
//! METHOD DefaultParamMethod
//! COMPILE_COUNT_GE DefaultParamMethod 1
//! EXEC_COUNT_GE *DefaultParamMethod* 1
//! DEOPT_COUNT *Deoptimize* 0

class DefaultParamMethod {
    base: number;

    constructor(base: number = 10) {
        this.base = base;
    }

    compute(delta: number = 1, factor: number = 2): number {
        return (this.base + delta) * factor;
    }
}

function callDefaultParamsMethod(): number {
    let a = new DefaultParamMethod();
    let b = new DefaultParamMethod(7);
    return a.compute() + a.compute(5) + b.compute(3, 4);
}

ArkTools.arkSteedCompileSync(DefaultParamMethod);
ArkTools.arkSteedCompileSync(callDefaultParamsMethod);


let a = new DefaultParamMethod();
print(a.compute());
print(a.compute(5));

let b = new DefaultParamMethod(7);
print(b.compute(3, 4));
callDefaultParamsMethod();
