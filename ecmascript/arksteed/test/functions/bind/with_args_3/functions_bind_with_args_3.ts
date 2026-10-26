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

// G11.6 (P0) additive call evidence for `functions/bind`. `this.run.bind(this, value)` fixes the
// bound receiver and prefixes the bound argument 42 before the supplied arguments, so the call
// `run(5, 3)` reaches `run(base, n)` as `base = 42, n = 5` and the extra supplied argument 3 is
// not bound to any parameter (ECMA-262 2021 10.4.1.1: bound arguments precede the supplied ones);
// the result is 42 + square(5) = 67. The compiled caller invokes the bound function through its
// generic `Call` vertex, because the call site carries no feedback when the method is installed
// before its first execution; the compiled `run` target is entered through the bound [[Call]] and
// itself calls `square` through a generic `Call` (`square` is not compiled). `COMPILE_COUNT_GE`/
// `EXEC_COUNT_GE` name the two claimed methods and `DEOPT_COUNT *Deoptimize* 0` rules out a silent
// deopt into the interpreter. The expected output and the observed argument order are unchanged.

//! PARAMS --compiler-jit-hotness-threshold=60000

//! METHOD call_compiled_bind_expression_with_args_3
//! HAS Call                        # the generic invocation of the bound function

//! METHOD run
//! HAS Call                        # the generic call of the uncompiled `square`

//! COMPILE_COUNT_GE *call_compiled_bind_expression_with_args_3* 1
//! EXEC_COUNT_GE *call_compiled_bind_expression_with_args_3* 1
//! COMPILE_COUNT_GE *run* 1
//! EXEC_COUNT_GE *run* 1
//! DEOPT_COUNT *Deoptimize* 0

function square(x: number): number { return x * x; }

class BoundClosureTarget {
    value: number;

    constructor(value: number) {
        this.value = value;
        return this.run.bind(this, value);
    }

    run(base: number, n: number): number {
        return base + square(n);
    }
}

function call_compiled_bind_expression_with_args_3(run: (base: number, n: number) => number): number {
    return run(5, 3);  // 3 is discarded
}

ArkTools.arkSteedCompileSync(BoundClosureTarget.prototype.run);
ArkTools.arkSteedCompileSync(call_compiled_bind_expression_with_args_3);


print(call_compiled_bind_expression_with_args_3(new BoundClosureTarget(42)));