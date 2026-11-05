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

//! METHOD metadataSameIndex
//! HAS_NOT CallRuntime LdExternalModuleVarByIndexOnJSFunc
//! COMPILE_COUNT_GE *metadataSameIndex* 1
//! EXEC_COUNT_GE *metadataSameIndex* 1

//! METHOD metadataDifferentIndex
//! HAS_NOT CallRuntime LdExternalModuleVarByIndexOnJSFunc
//! COMPILE_COUNT_GE *metadataDifferentIndex* 1
//! EXEC_COUNT_GE *metadataDifferentIndex* 1

//! METHOD metadataAcrossCall
//! HAS_NOT CallRuntime LdExternalModuleVarByIndexOnJSFunc
//! COMPILE_COUNT_GE *metadataAcrossCall* 1
//! EXEC_COUNT_GE *metadataAcrossCall* 1

//! METHOD metadataBranchFirst
//! HAS_NOT CallRuntime LdExternalModuleVarByIndexOnJSFunc
//! COMPILE_COUNT_GE *metadataBranchFirst* 1
//! EXEC_COUNT_GE *metadataBranchFirst* 1

declare function print(arg: number): void;
declare class ArkTools {
    static arkSteedCompileSync<T extends Function>(func: T): T;
}

import { primaryValue as first, secondaryValue as second } from './ldextvar_metadata_live.ts';
export let primaryValue = 7;
export let secondaryValue = 13;

function advancePrimary(): void {
    primaryValue += 1;
}

function metadataSameIndex(): number {
    return first + first;
}

function metadataDifferentIndex(): number {
    return first + second;
}

function metadataAcrossCall(mutate: () => void): number {
    const before = first;
    mutate();
    return before * 100 + first;
}

function metadataBranchFirst(flag: boolean): number {
    let before = 0;
    if (flag) {
        before = first;
    }
    return before * 100 + first;
}

for (let i = 0; i < 64; i++) {
    metadataSameIndex();
    metadataDifferentIndex();
    metadataAcrossCall(advancePrimary);
    metadataBranchFirst(false);
    metadataBranchFirst(true);
    primaryValue = 7;
}

ArkTools.arkSteedCompileSync(metadataSameIndex);
ArkTools.arkSteedCompileSync(metadataDifferentIndex);
ArkTools.arkSteedCompileSync(metadataAcrossCall);
ArkTools.arkSteedCompileSync(metadataBranchFirst);

print(metadataSameIndex());
print(metadataDifferentIndex());
print(metadataBranchFirst(false));
print(metadataBranchFirst(true));
print(metadataAcrossCall(advancePrimary));
print(metadataSameIndex());
print(metadataDifferentIndex());
print(metadataBranchFirst(false));
print(metadataBranchFirst(true));
