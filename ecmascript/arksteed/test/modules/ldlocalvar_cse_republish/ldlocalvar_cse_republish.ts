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

//! PARAMS --compiler-jit-hotness-threshold=10000
//! DEOPT_COUNT *Deoptimize* 0
//! METHOD localEightRepublish
//! HAS_NOT CallRuntime LdLocalModuleVarByIndexOnJSFunc
//! COMPILE_COUNT_GE *localEightRepublish* 1
//! EXEC_COUNT_GE *localEightRepublish* 1

declare function print(value: number): void;
declare class ArkTools { static arkSteedCompileSync<T extends Function>(func: T): T; }
export let localValue = 7;
function localEightRepublish(seed: number): number {
 let value = seed;
 value = (value + localValue) | 0;
 value = (value + localValue) | 0;
 value = (value + localValue) | 0;
 value = (value + localValue) | 0;
 value = (value + localValue) | 0;
 value = (value + localValue) | 0;
 value = (value + localValue) | 0;
 value = (value + localValue) | 0;
 return value;
}
for (let i=0;i<64;i++) { localEightRepublish(1); }
ArkTools.arkSteedCompileSync(localEightRepublish);
print(localEightRepublish(1));
localValue=19;
print(localEightRepublish(1));
