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

// G11.6 (P0) additive call evidence for `functions/bind`. `this.add.bind(this, x)` and
// `this.multiply.bind(this, y)` fix the bound receiver and prefix one bound argument each, so the
// supplied argument 10 becomes the second (remaining) argument of both bound calls:
// `add(base = 5, n = 10) = 15` plus `multiply(base = 3, n = 10) = 30` prints 45 (ECMA-262 2021
// 10.4.1.1: bound arguments precede the supplied ones). The compiled caller invokes both bound
// functions through its generic `Call` vertices, because the call sites carry no feedback when
// the methods are installed before their first execution; the compiled `add` and `multiply`
// targets are entered through the bound [[Call]]. `COMPILE_COUNT_GE`/`EXEC_COUNT_GE` name the
// three claimed methods and `DEOPT_COUNT *Deoptimize* 0` rules out a silent deopt into the
// interpreter. The expected output and the observed argument order are unchanged.

//! PARAMS --compiler-jit-hotness-threshold=60000

//! METHOD call_compiled_bind_expression_with_args_5
//! HAS Call                        # the two generic invocations of the bound functions

//! COMPILE_COUNT_GE *call_compiled_bind_expression_with_args_5* 1
//! EXEC_COUNT_GE *call_compiled_bind_expression_with_args_5* 1
//! COMPILE_COUNT_GE *add* 1
//! EXEC_COUNT_GE *add* 1
//! COMPILE_COUNT_GE *multiply* 1
//! EXEC_COUNT_GE *multiply* 1
//! DEOPT_COUNT *Deoptimize* 0

class MultiBoundTarget {
    fn1: (a: number, b: number) => number;
    fn2: (a: number, b: number) => number;

    constructor(x: number, y: number) {
        this.fn1 = this.add.bind(this, x);
        this.fn2 = this.multiply.bind(this, y);
    }

    add(base: number, n: number): number {
        return base + n;
    }

    multiply(base: number, n: number): number {
        return base * n;
    }
}

function call_compiled_bind_expression_with_args_5(
    fn1: (base: number, n: number) => number,
    fn2: (base: number, n: number) => number,
    n: number
): number {
    return fn1(10, n) + fn2(10, n);
}

ArkTools.arkSteedCompileSync(MultiBoundTarget.prototype.add);
ArkTools.arkSteedCompileSync(MultiBoundTarget.prototype.multiply);
ArkTools.arkSteedCompileSync(call_compiled_bind_expression_with_args_5);


let target = new MultiBoundTarget(5, 3);
print(call_compiled_bind_expression_with_args_5(target.fn1, target.fn2, 4));