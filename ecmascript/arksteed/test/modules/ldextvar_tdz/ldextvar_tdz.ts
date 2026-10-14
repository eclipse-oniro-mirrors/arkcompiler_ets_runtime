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

// Test: ldexternalmodulevar compiled while the export is still in its TDZ and
// no export statement has executed yet. The binding itself is already a
// ResolvedIndexBinding (created at module instantiation), but the module's
// name dictionary only becomes a TaggedArray when the first export statement
// runs — so CheckModuleValueOutterResolved fails on the dictionary check and
// arksteed must keep the conservative runtime-stub fallback (no inline load
// chain). The compiled code must still honour the hole check: it throws
// ReferenceError until the export is initialized and returns its value
// afterwards. (Compare ldextvar_hole: with one export already executed the
// dictionary exists, all guards pass and the inline chain loads the hole.)

declare function print(arg: string): void;

declare class ArkTools {
    static arkSteedCompileSync<T extends Function>(func: T): T;
}

//! METHOD      reader
//! HAS         CallRuntime      LdExternalModuleVarByIndexOnJSFunc
//! HAS         BranchIfReferenceEqual
//! HAS         LoadTaggedField
//! METHOD reader
//! COMPILE_COUNT_GE *reader* 1
//! EXEC_COUNT_GE *reader* 1
//! DEOPT_COUNT *Deoptimize* 0

import { late as l } from './ldextvar_tdz.ts';

function reader(): number {
    return l;
}

ArkTools.arkSteedCompileSync(reader);   // export not executed yet: unresolved, stub fallback

try {
    print(reader());             // compiled code must throw on the hole
} catch (e) {
    print(e.name);
}

export let late = 1;

print(reader());                 // 1
