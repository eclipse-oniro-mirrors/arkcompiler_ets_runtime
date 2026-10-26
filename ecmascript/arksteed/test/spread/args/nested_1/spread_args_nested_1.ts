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

//! COMPILE_COUNT_GE *spread_args_nested_helper* 1
//! EXEC_COUNT_GE *spread_args_nested_helper* 1
//! COMPILE_COUNT_GE *spread_args_nested_1* 1
//! EXEC_COUNT_GE *spread_args_nested_1 1
//! DEOPT_COUNT *Deoptimize* 0

function spread_args_nested_helper(
    a: number, b: number, c: number, d: number, e: number, f: number): number {
    return a + 10 * b + 100 * c + 1000 * d + 10000 * e + 100000 * f;
}

//! METHOD spread_args_nested_1
//! HAS CallRuntime CallSpread
//! HAS CallRuntime StArraySpread
function spread_args_nested_1(): number {
    const inner: [number, number] = [3, 4];
    const outer: [number, number, number, number] = [1, 2, ...inner];
    return spread_args_nested_helper(5, ...outer, 6);
}

ArkTools.arkSteedCompileSync(spread_args_nested_helper);
ArkTools.arkSteedCompileSync(spread_args_nested_1);


print(spread_args_nested_1());
