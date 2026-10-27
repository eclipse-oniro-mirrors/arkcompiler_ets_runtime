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

// The same compiled reader must handle an absent dictionary, then a live
// dictionary. A Hole in an existing dictionary still reaches the TDZ check.

declare function print(arg: any): void;

declare class ArkTools {
    static arkSteedCompileSync<T extends Function>(func: T): T;
}

//! METHOD      readerBeforeFirst
//! HAS         CallRuntime      LdLocalModuleVarByIndexOnJSFunc
function readerBeforeFirst(): number {
    return early;
}

ArkTools.arkSteedCompileSync(readerBeforeFirst);
try {
    print(readerBeforeFirst());
} catch (err) {
    print(err.name);
}
export let early = 1;
print(readerBeforeFirst());

//! METHOD      readerLate
//! HAS         LoadTaggedField
function readerLate(): number {
    return late;
}

ArkTools.arkSteedCompileSync(readerLate);
try {
    print(readerLate());
} catch (err) {
    print(err.name);
}
export let late = 7;
print(readerLate());
late = 9;
print(readerLate());
