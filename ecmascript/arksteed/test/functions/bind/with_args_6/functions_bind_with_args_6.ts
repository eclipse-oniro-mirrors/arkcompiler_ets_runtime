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

// G11.6 (P0) additive call evidence for `functions/bind`. `this.run.bind(this, ...values)` prefixes
// the bound arguments in their iteration order before the supplied ones, and the caller forwards its
// own rest arguments with `run(...args)`, so each printed product uses the first `values.length`
// supplied arguments as the bound prefix and the remaining six slots follow in order (ECMA-262 2021
// 10.4.1.1 and 13.3.8.1 ArgumentListEvaluation); all seven calls print 4560. The compiled caller
// performs the spread invocation through its `CallRuntime CallSpread` runtime call, because the call
// site carries no feedback when the method is installed before its first execution; the compiled
// `run` target is entered through the bound [[Call]]. `COMPILE_COUNT_GE`/`EXEC_COUNT_GE` name the two
// claimed methods and `DEOPT_COUNT *Deoptimize* 0` rules out a silent deopt into the interpreter. The
// expected output and the observed argument order are unchanged.

//! PARAMS --compiler-jit-hotness-threshold=60000

//! METHOD call_compiled_bind_expression_with_args_6
//! HAS CallRuntime CallSpread     # the spread invocation of the bound function

//! COMPILE_COUNT_GE *call_compiled_bind_expression_with_args_6* 1
//! EXEC_COUNT_GE *call_compiled_bind_expression_with_args_6* 1
//! COMPILE_COUNT_GE *run* 1
//! EXEC_COUNT_GE *run* 1
//! DEOPT_COUNT *Deoptimize* 0

class BoundClosureTarget {
    values: number[]

    constructor(...values: number[]) {
        this.values = values;
        return this.run.bind(this, ...values);
    }

    run(x: number, y: number, z: number, a: number, b: number, c: number): number {
        return a * x + b * y + c * z;
    }
}

function call_compiled_bind_expression_with_args_6(run: (...args: number[]) => number,
                                                   ...args: number[]): number {
    return run(...args);
}

ArkTools.arkSteedCompileSync(BoundClosureTarget.prototype.run);
ArkTools.arkSteedCompileSync(call_compiled_bind_expression_with_args_6);


print(call_compiled_bind_expression_with_args_6(new BoundClosureTarget(), 1000, 100, 10, 4, 5, 6));
print(call_compiled_bind_expression_with_args_6(new BoundClosureTarget(1000), 100, 10, 4, 5, 6));
print(call_compiled_bind_expression_with_args_6(new BoundClosureTarget(1000, 100), 10, 4, 5, 6));
print(call_compiled_bind_expression_with_args_6(new BoundClosureTarget(1000, 100, 10), 4, 5, 6));
print(call_compiled_bind_expression_with_args_6(new BoundClosureTarget(1000, 100, 10, 4), 5, 6));
print(call_compiled_bind_expression_with_args_6(new BoundClosureTarget(1000, 100, 10, 4, 5), 6));
print(call_compiled_bind_expression_with_args_6(new BoundClosureTarget(1000, 100, 10, 4, 5, 6)));
