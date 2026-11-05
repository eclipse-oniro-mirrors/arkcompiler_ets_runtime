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

//! METHOD metadataSameEightCse
//! HAS_NOT CallRuntime LdExternalModuleVarByIndexOnJSFunc
//! COMPILE_COUNT_GE *metadataSameEightCse* 1
//! EXEC_COUNT_GE *metadataSameEightCse* 1

//! METHOD metadataDirectExportWrite
//! HAS_NOT CallRuntime LdExternalModuleVarByIndexOnJSFunc
//! COMPILE_COUNT_GE *metadataDirectExportWrite* 1
//! EXEC_COUNT_GE *metadataDirectExportWrite* 1

declare function print(value: number): void;
declare class ArkTools {
    static arkSteedCompileSync<T extends Function>(func: T): T;
}
import { primaryValue as first } from './ldextvar_metadata_cse.ts';
export let primaryValue = 7;

function metadataSameEightCse(seed: number): number {
    let value = seed;
    value = (value + first) | 0;
    value = (value + first) | 0;
    value = (value + first) | 0;
    value = (value + first) | 0;
    value = (value + first) | 0;
    value = (value + first) | 0;
    value = (value + first) | 0;
    value = (value + first) | 0;
    return value;
}

function metadataDirectExportWrite(next: number): number {
    const before = first;
    primaryValue = next;
    return before * 100 + first;
}

for (let i = 0; i < 64; i++) {
    metadataSameEightCse(1);
    metadataDirectExportWrite(8);
    primaryValue = 7;
}
ArkTools.arkSteedCompileSync(metadataSameEightCse);
ArkTools.arkSteedCompileSync(metadataDirectExportWrite);
print(metadataSameEightCse(1));
print(metadataDirectExportWrite(8));
print(metadataSameEightCse(1));
primaryValue = 19;
print(metadataSameEightCse(1));
print(metadataDirectExportWrite(23));
print(metadataSameEightCse(1));
