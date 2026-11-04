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
// spread `new` form of this leaf (ECMA-262 2021 10.2.2 [[Construct]], 13.3.8.1
// ArgumentListEvaluation). The counters are lower bounds because
// `ArkTools.arkSteedCompileSync` returns true unconditionally and does not
// report whether it installed code. The constructor observes
// `arguments.length`; no captured variable is written, so no env-slot evidence
// is claimed. Program output and the existing probes are unchanged.
//! COMPILE_COUNT_GE *CallCompiledConstructorArgumentsLengthBox 1
//! EXEC_COUNT_GE *CallCompiledConstructorArgumentsLengthBox 1
//! COMPILE_COUNT_GE *call_compiled_constructor_arguments_length_2 1
//! EXEC_COUNT_GE *call_compiled_constructor_arguments_length_2 1
//! DEOPT_COUNT *Deoptimize* 0
//! METHOD call_compiled_constructor_arguments_length_2
//! HAS CallRuntime NewObjApply

class CallCompiledConstructorArgumentsLengthBox {
    len: number;

    constructor(x?: number, y?: number) {
        this.len = arguments.length;
    }
}

function call_compiled_constructor_arguments_length_2(
    ...args: number[]): CallCompiledConstructorArgumentsLengthBox {
    return new CallCompiledConstructorArgumentsLengthBox(...args);
}

ArkTools.arkSteedCompileSync(CallCompiledConstructorArgumentsLengthBox);
ArkTools.arkSteedCompileSync(call_compiled_constructor_arguments_length_2);


print(call_compiled_constructor_arguments_length_2().len);
print(call_compiled_constructor_arguments_length_2(4).len);
print(call_compiled_constructor_arguments_length_2(4, 5).len);
