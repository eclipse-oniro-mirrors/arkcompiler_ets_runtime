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
// report whether it installed code. The rest parameter is a parameter
// binding, not a captured variable, so no env-slot evidence is claimed.
// Program output and the existing probes are unchanged.
//! COMPILE_COUNT_GE *CallCompiledConstructorRestLengthBox 1
//! EXEC_COUNT_GE *CallCompiledConstructorRestLengthBox 1
//! COMPILE_COUNT_GE *call_compiled_constructor_rest_length_2 1
//! EXEC_COUNT_GE *call_compiled_constructor_rest_length_2 1
//! DEOPT_COUNT *Deoptimize* 0
//! METHOD call_compiled_constructor_rest_length_2
//! HAS CallRuntime NewObjApply

class CallCompiledConstructorRestLengthBox {
    len: number;
    restLen: number;

    constructor(...args: number[]) {
        this.len = arguments.length;
        this.restLen = args.length;
    }
}

function call_compiled_constructor_rest_length_2(
    ...args: number[]): CallCompiledConstructorRestLengthBox {
    return new CallCompiledConstructorRestLengthBox(...args);
}

ArkTools.arkSteedCompileSync(CallCompiledConstructorRestLengthBox);
ArkTools.arkSteedCompileSync(call_compiled_constructor_rest_length_2);


let a = call_compiled_constructor_rest_length_2();
print(a.len);
print(a.restLen);

let b = call_compiled_constructor_rest_length_2(4, 5, 6);
print(b.len);
print(b.restLen);
