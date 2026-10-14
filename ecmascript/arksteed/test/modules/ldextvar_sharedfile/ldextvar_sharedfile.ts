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

// Test: ldexternalmodulevar inside a "use shared" module (multi-file case;
// see sharedlib.ts for the sendable guard analysis). The ordinary function
// reading the import is not a shared function and its module is an ordinary
// (shared-heap) SourceTextModule, so every compile-time guard passes and the
// inline load chain must be used — the optimization must not misjudge shared
// modules as sendable function modules.

declare function print(arg: any): void;

declare class ArkTools {
    static arkSteedCompileSync<T extends Function>(func: T): T;
}

//! METHOD      normalReader
//! HAS_NOT     CallRuntime      LdExternalModuleVarByIndexOnJSFunc
//! HAS         LoadTaggedField
//! HAS         LoadTaggedElement
//! METHOD normalReader
//! COMPILE_COUNT_GE *normalReader* 1
//! EXEC_COUNT_GE *normalReader* 1
//! DEOPT_COUNT *Deoptimize* 0

import { normalReader } from './sharedlib';

ArkTools.arkSteedCompileSync(normalReader);
print(normalReader());       // 42: inline load chain, live binding
