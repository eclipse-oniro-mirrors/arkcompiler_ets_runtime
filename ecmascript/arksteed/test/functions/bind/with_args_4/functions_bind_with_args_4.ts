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

// G11.6 (P0) additive call evidence for `functions/bind`. `obj.multiply.bind(obj, 10)` fixes the
// bound receiver and prefixes the bound argument 10 before the supplied argument 3, so
// `multiply(baseVal, x)` receives `baseVal = 10, x = 3` and computes
// `(10 + 10 + 3) * 5 = 115` (ECMA-262 2021 10.4.1.1: bound arguments precede the supplied ones).
// The compiled caller invokes the bound function through its generic `Call` vertex, because the
// call site carries no feedback when the methods are installed before their first execution; the
// compiled `multiply` target is entered through the bound [[Call]] and calls the compiled
// `compute` method through its own generic `Call` vertex. `COMPILE_COUNT_GE`/`EXEC_COUNT_GE` name
// the three claimed methods and `DEOPT_COUNT *Deoptimize* 0` rules out a silent deopt into the
// interpreter. The expected output and the observed argument order are unchanged.

//! PARAMS --compiler-jit-hotness-threshold=60000

//! METHOD call_compiled_bind_expression_with_args_4
//! HAS Call                        # the generic invocation of the bound function

//! METHOD multiply
//! HAS Call                        # the generic call of the compiled `compute`

//! COMPILE_COUNT_GE *call_compiled_bind_expression_with_args_4* 1
//! EXEC_COUNT_GE *call_compiled_bind_expression_with_args_4* 1
//! COMPILE_COUNT_GE *multiply* 1
//! EXEC_COUNT_GE *multiply* 1
//! COMPILE_COUNT_GE *compute* 1
//! EXEC_COUNT_GE *compute* 1
//! DEOPT_COUNT *Deoptimize* 0

class BaseClass {
    baseValue: number;

    constructor(baseValue: number) {
        this.baseValue = baseValue;
    }

    compute(a: number, b: number): number {
        return this.baseValue + a + b;
    }
}

class DerivedClass extends BaseClass {
    multiplier: number;

    constructor(baseValue: number, multiplier: number) {
        super(baseValue);
        this.multiplier = multiplier;
    }

    multiply(baseVal: number, x: number): number {
        return this.compute(baseVal, x) * this.multiplier;
    }
}

function call_compiled_bind_expression_with_args_4(run: (x: number) => number): number {
    return run(3);
}

ArkTools.arkSteedCompileSync(BaseClass.prototype.compute);
ArkTools.arkSteedCompileSync(DerivedClass.prototype.multiply);
ArkTools.arkSteedCompileSync(call_compiled_bind_expression_with_args_4);


let obj = new DerivedClass(10, 5);
let boundFn = obj.multiply.bind(obj, 10);
print(call_compiled_bind_expression_with_args_4(boundFn));