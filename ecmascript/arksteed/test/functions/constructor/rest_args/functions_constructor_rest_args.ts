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
// unconditionally and does not report whether it installed code. The rest
// parameter is a parameter binding, not a captured variable, so no env-slot
// evidence is claimed. Program output and the existing probes are unchanged.
//! COMPILE_COUNT_GE *CallCompiledConstructorRestArgsBox 1
//! EXEC_COUNT_GE *CallCompiledConstructorRestArgsBox 1
//! COMPILE_COUNT_GE *call_compiled_constructor_rest_args 1
//! EXEC_COUNT_GE *call_compiled_constructor_rest_args 1
//! DEOPT_COUNT *Deoptimize* 0
//! METHOD call_compiled_constructor_rest_args
//! HAS CallRuntime OptNewObjRange

class CallCompiledConstructorRestArgsBox {
    total: number = 0;

    constructor(...args: number[]) {
        let sum = 0;
        for (let i = 0; i < args.length; i++) {
            sum += args[i];
        }
        this.total = sum;
    }
}

function call_compiled_constructor_rest_args(): void {
    const a = new CallCompiledConstructorRestArgsBox(1, 2, 3);
    const b = new CallCompiledConstructorRestArgsBox(5);
    print(a.total);
    print(b.total);
}

ArkTools.arkSteedCompileSync(CallCompiledConstructorRestArgsBox);
ArkTools.arkSteedCompileSync(call_compiled_constructor_rest_args);


call_compiled_constructor_rest_args();
