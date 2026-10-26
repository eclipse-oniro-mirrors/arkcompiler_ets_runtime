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

// G11.6 (P0) additive call evidence for `functions/bind`. `this.run.bind(this, 100)` fixes the
// bound receiver and prefixes the bound argument 100 before the supplied arguments, so the call
// `run(5)` reaches `run(a, b)` as `a = 100, b = 5` and prints 105 (ECMA-262 2021 10.4.1.1: the
// bound arguments come first and the remaining supplied arguments follow in order). The compiled
// caller invokes the bound function through its generic `Call` vertex, because the call site
// carries no feedback when the method is installed before its first execution; the underlying
// `run` target is compiled as well and entered through the bound [[Call]]. `COMPILE_COUNT_GE`/
// `EXEC_COUNT_GE` name the two claimed methods and `DEOPT_COUNT *Deoptimize* 0` rules out a
// silent deopt into the interpreter. The expected output and the observed argument order are
// unchanged.

//! PARAMS --compiler-jit-hotness-threshold=60000

//! METHOD call_compiled_bind_expression_with_args_2
//! HAS Call                        # the generic invocation of the bound function

//! COMPILE_COUNT_GE *call_compiled_bind_expression_with_args_2* 1
//! EXEC_COUNT_GE *call_compiled_bind_expression_with_args_2* 1
//! COMPILE_COUNT_GE *run* 1
//! EXEC_COUNT_GE *run* 1
//! DEOPT_COUNT *Deoptimize* 0

class BoundClosureTarget {
    value: number;

    constructor(value: number) {
        this.value = value;
        return this.run.bind(this, 100);
    }

    run(a: number, b: number): number {
        return a + b;
    }
}

function call_compiled_bind_expression_with_args_2(run: (a: number) => number): number {
    return run(5);
}

ArkTools.arkSteedCompileSync(BoundClosureTarget.prototype.run);
ArkTools.arkSteedCompileSync(call_compiled_bind_expression_with_args_2);


print(call_compiled_bind_expression_with_args_2(new BoundClosureTarget(42)));