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

// Test: inlined ldexternalmodulevar must honour the TDZ hole check.
// All compile-time guards pass here: the binding is a ResolvedIndexBinding
// (created at module instantiation) and the name dictionary is already a
// TaggedArray because `export const early` has executed (the dictionary is
// created when the first export statement runs). `late` has not executed yet,
// so its dictionary slot still holds the hole — reader is compiled with the
// inline load chain and the compiled code must throw ReferenceError on the
// hole (throw.undefinedifholewithname after the load) and return the value
// after initialization.

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

import { late as l, early as e } from './ldextvar_hole.ts';

export const early = 1;      // executes first: name dictionary becomes a TaggedArray

function reader(): string {
    return l + ',' + e;
}

ArkTools.arkSteedCompileSync(reader);   // guards pass, `late` slot is still the hole → inline

try {
    print(reader());       // compiled code must throw on the hole
} catch (err) {
    print(err.name);
}

export let late = 1;

print(reader());           // 1,1
