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
// JS call, so the actual construction form of this leaf is carried by its own
// silent invocation carrier `callDefaultParamsUndefinedConstructor`, whose dump
// holds `CallRuntime OptNewObjRange` for the omitted, partially supplied and
// fully supplied constructions. The original constructor keeps its own
// positive compile/entry evidence, the probes and the output bytes are
// unchanged, and the normal path must not deopt. The `_GE` lower bound allows
// the installation and recompilation multiplicity of the same method.
//! METHOD callDefaultParamsUndefinedConstructor
//! HAS CallRuntime OptNewObjRange
//! COMPILE_COUNT_GE callDefaultParamsUndefinedConstructor 1
//! EXEC_COUNT_GE *callDefaultParamsUndefinedConstructor* 1
//! METHOD DefaultParamUndefinedConstructor
//! COMPILE_COUNT_GE DefaultParamUndefinedConstructor 1
//! EXEC_COUNT_GE *DefaultParamUndefinedConstructor* 1
//! DEOPT_COUNT *Deoptimize* 0

class DefaultParamUndefinedConstructor {
    firstIsMissing: number;
    secondIsMissing: number;

    constructor(first?: string, second?: number) {
        this.firstIsMissing = first === undefined ? 1 : 0;
        this.secondIsMissing = second === undefined ? 1 : 0;
    }
}

function callDefaultParamsUndefinedConstructor(): number {
    let missing = new DefaultParamUndefinedConstructor().firstIsMissing;
    let partial = new DefaultParamUndefinedConstructor("ok").secondIsMissing;
    let full = new DefaultParamUndefinedConstructor("ok", 8).secondIsMissing;
    return missing + partial + full;
}

ArkTools.arkSteedCompileSync(DefaultParamUndefinedConstructor);
ArkTools.arkSteedCompileSync(callDefaultParamsUndefinedConstructor);


let a = new DefaultParamUndefinedConstructor();
print(a.firstIsMissing);
print(a.secondIsMissing);

let b = new DefaultParamUndefinedConstructor("ok");
print(b.firstIsMissing);
print(b.secondIsMissing);

let c = new DefaultParamUndefinedConstructor("ok", 8);
print(c.firstIsMissing);
print(c.secondIsMissing);
callDefaultParamsUndefinedConstructor();
