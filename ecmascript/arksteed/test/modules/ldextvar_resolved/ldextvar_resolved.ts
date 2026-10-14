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

// Test: ldexternalmodulevar with a resolved binding must be inlined by arksteed.
// The module imports its own exports (aliased to avoid redeclaration), so every
// read of `imp` compiles to ldexternalmodulevar. Once the binding is resolved,
// the compiled IR loads the value directly from the module dictionary instead
// of calling the LdExternalModuleVarByIndexOnJSFunc runtime stub. Live-binding
// semantics must be kept: mutations of `counter` through `bump` (i.e. through
// the imported alias `step`) are observed by later reads of `imp`.

declare function print(arg: string): void;

declare class ArkTools {
    static arkSteedCompileSync<T extends Function>(func: T): T;
}

//! METHOD      reader
//! HAS_NOT     CallRuntime      LdExternalModuleVarByIndexOnJSFunc
//! HAS         LoadTaggedField
//! HAS         LoadTaggedElement
//! METHOD reader
//! COMPILE_COUNT_GE *reader* 1
//! EXEC_COUNT_GE *reader* 1
//! DEOPT_COUNT *Deoptimize* 0

import { counter as imp, bump as step } from './ldextvar_resolved.ts';

export let counter = 0;

export function bump(): number {
    return ++counter;
}

function reader(): string {
    return imp + ',' + imp;
}

ArkTools.arkSteedCompileSync(reader);

print(reader());      // 0,0
step();
print(reader());      // 1,1
step();
step();
print(reader());      // 3,3
print(step());        // 4
