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

// Test: ldexternalmodulevar whose import target is a CommonJS module — the
// "is resolved index binding" guard of CheckModuleValueOutterResolved fails.
// A CJS export resolves to a by-name ResolvedBinding (the CJS_MODULE branch
// of SourceTextModule::ResolveLocalExport), never to the ResolvedIndexBinding
// an ES module's local exports get at instantiation, so the compile-time walk
// does not set IsLdExtModuleVarResolved and arksteed must keep the
// conservative runtime-stub fallback. The compiled code must still read the
// live value through the slow path (the runtime resolves the CJS binding).

declare function print(arg: any): void;

declare class ArkTools {
    static arkSteedCompileSync<T extends Function>(func: T): T;
}

//! METHOD      reader
//! HAS         CallRuntime      LdExternalModuleVarByIndexOnJSFunc
//! HAS_NOT     LoadTaggedElement
//! HAS         LoadTaggedField
//! METHOD reader
//! COMPILE_COUNT_GE *reader* 1
//! EXEC_COUNT_GE *reader* 1
//! DEOPT_COUNT *Deoptimize* 0

import { val as cv } from './cjslib';

function reader(): number {
    return cv;
}

ArkTools.arkSteedCompileSync(reader);   // CJS binding: by-name, guard fails → stub fallback
print(reader());                        // 42: runtime slow path resolves the CJS export
