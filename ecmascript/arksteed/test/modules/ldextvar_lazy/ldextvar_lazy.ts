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

// Test: callruntime.ldlazymodulevar must keep calling the lazy stub.
//
// `import lazy { v }` compiles the import read to
// callruntime.ldlazymodulevar, whose runtime (RuntimeLdLazyExternalModuleVar ->
// GetModuleValueOuterInternal<true>) evaluates the target module before
// reading the value. The inline fast path (TryBuildLoadExternalModuleVar) has
// no module-evaluation step, and the non-lazy stub
// (LdExternalModuleVarByIndexOnJSFunc) skips it too — so lazy imports must
// always take the runtime call LdLazyExternalModuleVarByIndex and must never
// be lowered to the inline load chain.
//
// This case guards the lazy-vs-non-lazy dispatch fix: before it, the lazy
// opcodes fell into LowerLdExternalModuleVar and were routed to the non-lazy
// stub, silently skipping module evaluation on first read.

declare function print(arg: any): void;

declare class ArkTools {
    static arkSteedCompileSync<T extends Function>(func: T): T;
}

//! METHOD      reader
//! HAS         CallRuntime      LdLazyExternalModuleVarByIndex
//! HAS_NOT     CallRuntime      LdExternalModuleVarByIndexOnJSFunc
//! HAS_NOT     LoadTaggedElement

import lazy { v as lazyVal } from './lazylib';
import { w as eagerVal } from './lazylib';

// Reads the lazy import: each read is callruntime.ldlazymodulevar and must
// reach the lazy runtime stub (which evaluates the target module if needed).
export function reader(): number {
    return lazyVal;
}

// Reads the same module through an ordinary import: once the binding is
// resolved this is lowered to the inline load chain, proving the dispatch
// splits the two import flavors instead of uniformly falling back.
export function eagerReader(): number {
    return eagerVal;
}

// Bump the lazy-imported binding so the second read observes the live value.
export function bump(): number {
    return bumpLocal();
}

import lazy { bump as bumpLocal } from './lazylib';

ArkTools.arkSteedCompileSync(reader);
ArkTools.arkSteedCompileSync(eagerReader);

print(reader());            // 42: lazy stub evaluated the module, live value
print(eagerReader());       // 43: eager import still inlined (LoadTaggedElement)
