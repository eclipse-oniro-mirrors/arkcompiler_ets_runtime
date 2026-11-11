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

//! METHOD metadataLoop
//! HAS_NOT CallRuntime LdExternalModuleVarByIndexOnJSFunc
//! COMPILE_COUNT_GE *metadataLoop* 1
//! EXEC_COUNT_GE *metadataLoop* 1

//! METHOD metadataCatch
//! HAS_NOT CallRuntime LdExternalModuleVarByIndexOnJSFunc
//! COMPILE_COUNT_GE *metadataCatch* 1
//! EXEC_COUNT_GE *metadataCatch* 1

declare function print(arg: number): void;
declare class ArkTools {
    static arkSteedCompileSync<T extends Function>(func: T): T;
}

import { primaryValue as first } from './ldextvar_metadata_control_flow.ts';
export let primaryValue = 7;

function advancePrimary(): void {
    primaryValue += 1;
}

function metadataLoop(count: number, mutate: () => void): number {
    let sum = 0;
    for (let i = 0; i < count; i++) {
        const before = first;
        mutate();
        sum += before * 100 + first;
    }
    return sum * 100 + first;
}

function metadataCatch(readBeforeThrow: boolean, mutate: () => void): number {
    let before = 0;
    try {
        if (readBeforeThrow) {
            before = first;
        }
        mutate();
        throw 1;
    } catch (e) {
        return before * 100 + first;
    }
}

for (let i = 0; i < 64; i++) {
    metadataLoop(0, advancePrimary);
    metadataLoop(1, advancePrimary);
    metadataLoop(3, advancePrimary);
    metadataCatch(false, advancePrimary);
    metadataCatch(true, advancePrimary);
    primaryValue = 7;
}
ArkTools.arkSteedCompileSync(metadataLoop);
ArkTools.arkSteedCompileSync(metadataCatch);

print(metadataLoop(0, advancePrimary));
primaryValue = 7;
print(metadataLoop(1, advancePrimary));
primaryValue = 7;
print(metadataLoop(3, advancePrimary));
primaryValue = 7;
print(metadataCatch(false, advancePrimary));
primaryValue = 7;
print(metadataCatch(true, advancePrimary));
