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

// G11.6 (P0) additive construction evidence for `functions/constructor`
// (arksteed-g116-call-closure-and-additive-evidence#C1,#C3,#C4): the compiled
// constructor target and the compiled caller keep their own positive
// compilation and entry evidence, and the caller's own CFG dump carries the
// `new` form of this leaf (ECMA-262 2021 10.2.2 [[Construct]], 13.3.5.1.1
// EvaluateNew, 10.2.10 FunctionDeclarationInstantiation). The counters are
// lower bounds because `ArkTools.arkSteedCompileSync` returns true
// unconditionally and does not report whether it installed code. No captured
// variable is written, so no env-slot evidence is claimed. Program output and
// the existing probes are unchanged.
//! COMPILE_COUNT_GE *CallCompiledConstructorDefaultParamLengthBox 1
//! EXEC_COUNT_GE *CallCompiledConstructorDefaultParamLengthBox 1
//! COMPILE_COUNT_GE *call_compiled_constructor_default_param_length_2 1
//! EXEC_COUNT_GE *call_compiled_constructor_default_param_length_2 1
//! DEOPT_COUNT *Deoptimize* 0
//! METHOD call_compiled_constructor_default_param_length_2
//! HAS CallRuntime OptNewObjRange

class CallCompiledConstructorDefaultParamLengthBox {
    len: number;
    value: number;

    constructor(x: number = 7) {
        this.len = arguments.length;
        this.value = x;
    }
}

function call_compiled_constructor_default_param_length_2(
    x?: number): CallCompiledConstructorDefaultParamLengthBox {
    return x === undefined ?
        new CallCompiledConstructorDefaultParamLengthBox() :
        new CallCompiledConstructorDefaultParamLengthBox(x);
}

ArkTools.arkSteedCompileSync(CallCompiledConstructorDefaultParamLengthBox);
ArkTools.arkSteedCompileSync(call_compiled_constructor_default_param_length_2);


let a = call_compiled_constructor_default_param_length_2();
print(a.len);
print(a.value);

let b = call_compiled_constructor_default_param_length_2(4);
print(b.len);
print(b.value);
