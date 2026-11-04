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
// G11.6 additive evidence (S11/C1/C3/C4): the compiled method `buildLabel`
// performs no JS call, so the actual method invocation form of this leaf is
// carried by its own silent invocation carrier
// `callDefaultParamMethodPartialArgs`, whose dump holds the generic `Call` of
// the partially supplied `buildLabel` calls. The original method keeps its own
// positive compile/entry evidence, the probes and the output bytes are
// unchanged, and the normal path must not deopt. The `_GE` lower bound allows
// the installation and recompilation multiplicity of the same method.
//! METHOD callDefaultParamMethodPartialArgs
//! HAS Call
//! COMPILE_COUNT_GE callDefaultParamMethodPartialArgs 1
//! EXEC_COUNT_GE *callDefaultParamMethodPartialArgs* 1
//! METHOD buildLabel
//! COMPILE_COUNT_GE buildLabel 1
//! EXEC_COUNT_GE *buildLabel* 1
//! DEOPT_COUNT *Deoptimize* 0

class DefaultParamMethodPartialArgs {
    prefix: string;

    constructor(prefix: string) {
        this.prefix = prefix;
    }

    buildLabel(
        make: string = "Unknown",
        model: string = "Model",
        year: number = 2020
    ): string {
        return `${this.prefix}:${year}:${make}:${model}`;
    }
}

function callDefaultParamMethodPartialArgs(builder: DefaultParamMethodPartialArgs): string {
    return builder.buildLabel("Toyota") + builder.buildLabel("Honda", "Civic");
}

ArkTools.arkSteedCompileSync(DefaultParamMethodPartialArgs.prototype.buildLabel);
ArkTools.arkSteedCompileSync(callDefaultParamMethodPartialArgs);


let builder = new DefaultParamMethodPartialArgs("CAR");
print(builder.buildLabel("Toyota"));
print(builder.buildLabel("Honda", "Civic"));
callDefaultParamMethodPartialArgs(builder);
