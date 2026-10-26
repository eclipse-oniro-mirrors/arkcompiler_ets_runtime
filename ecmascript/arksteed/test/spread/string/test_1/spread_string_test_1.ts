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

//! COMPILE_COUNT_GE *spread_string_helper* 1
//! EXEC_COUNT_GE *spread_string_helper* 1
//! COMPILE_COUNT_GE *spread_string_1* 1
//! EXEC_COUNT_GE *spread_string_1* 1
//! DEOPT_COUNT *Deoptimize* 0

function spread_string_helper(a: string, b: string, c: string): string {
    return a + b + c;
}

//! METHOD spread_string_1
//! HAS CallRuntime CallSpread
//! HAS CallRuntime StArraySpread
function spread_string_1(s: string): string {
    return spread_string_helper(...s);
}

ArkTools.arkSteedCompileSync(spread_string_helper);
ArkTools.arkSteedCompileSync(spread_string_1);


print(spread_string_1("abc"));
