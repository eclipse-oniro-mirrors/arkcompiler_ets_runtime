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

function factorial(n: number): number {
    if (n <= 1) {
        return 1;
    }
    return n * factorial(n - 1);
}

function fibonacci(n: number): number {
    if (n <= 1) {
        return n;
    }
    return fibonacci(n - 1) + fibonacci(n - 2);
}

var fact10 = factorial(10);
ArkTools.jitCompileAsync(factorial);
ArkTools.jitCompileAsync(fibonacci);
var compileRet1 = ArkTools.waitJitCompileFinish(factorial);
var compileRet2 = ArkTools.waitJitCompileFinish(fibonacci);
var fact10After = factorial(10);
var fib15 = fibonacci(15);
print(compileRet1);
print(compileRet2);
print(fact10);
print(fact10After);
print(fib15);
print(fact10 === fact10After);
