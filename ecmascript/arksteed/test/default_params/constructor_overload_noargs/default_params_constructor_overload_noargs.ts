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
// G11.6 additive evidence (S11/C1/C3/C4): the compiled overloaded constructor
// performs no JS call, so the actual construction form of this leaf is carried
// by its own silent invocation carrier
// `callDefaultParamsConstructorOverloadNoargs`, whose dump holds
// `CallRuntime OptNewObjRange`. The original constructor keeps its own positive
// compile/entry evidence, the probes and the output bytes are unchanged, and
// the normal path must not deopt. The `_GE` lower bound allows the installation
// and recompilation multiplicity of the same method.
//! METHOD callDefaultParamsConstructorOverloadNoargs
//! HAS CallRuntime OptNewObjRange
//! COMPILE_COUNT_GE callDefaultParamsConstructorOverloadNoargs 1
//! EXEC_COUNT_GE *callDefaultParamsConstructorOverloadNoargs* 1
//! METHOD DefaultParamConstructorOverloadNoargs
//! COMPILE_COUNT_GE DefaultParamConstructorOverloadNoargs 1
//! EXEC_COUNT_GE *DefaultParamConstructorOverloadNoargs* 1
//! DEOPT_COUNT *Deoptimize* 0

class DefaultParamConstructorOverloadNoargs {
    width: number;
    height: number;

    constructor();
    constructor(width: number);
    constructor(width: number, height: number);
    constructor(width?: number, height?: number) {
        this.width = width || 1;
        this.height = height || this.width;
    }
}

function callDefaultParamsConstructorOverloadNoargs(): DefaultParamConstructorOverloadNoargs {
    return new DefaultParamConstructorOverloadNoargs();
}

ArkTools.arkSteedCompileSync(DefaultParamConstructorOverloadNoargs);
ArkTools.arkSteedCompileSync(callDefaultParamsConstructorOverloadNoargs);


let rect = new DefaultParamConstructorOverloadNoargs();
print(rect.width);
print(rect.height);
print(rect.width * rect.height);
callDefaultParamsConstructorOverloadNoargs();
