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

// "use shared" sibling module exercising the sendable guard of
// CheckModuleValueOutterResolved (!module.IsSendableFunctionModule()).
//
// normalReader is an ordinary function, so es2abc compiles its import read to
// a plain ldexternalmodulevar. The module lives in the shared heap but is an
// ordinary SourceTextModule (not a sendable function module), the binding is
// a ResolvedIndexBinding and the name dictionary is a TaggedArray — every
// guard passes and arksteed must inline the load chain.
//
// Why no 'use sendable' reader here: current es2abc compiles a 'use sendable'
// function's import reads to the dedicated
// callruntime.ldsendableexternalmodulevar bytecode (reading the SendableEnv),
// which never reaches the ldexternalmodulevar inline path — so the
// IsSendableFunctionModule guard is defensive, protecting the fast path
// against legacy abc where sendable functions still carried plain
// ldexternalmodulevar. A sendable reader can also not be kept as a functional
// regression: a 'use sendable' function reading an import hits pre-existing
// runtime defects on its own (self-import: interpreter stack overflow in
// RuntimeLdExternalModuleVarWithModule; multi-file:
// AddSendableModuleToCache registering an empty recordName, assertable in the
// pure interpreter and unrelated to this optimization).

"use shared"
import { val as v } from './lib';

export function normalReader(): number {
    return v;
}
