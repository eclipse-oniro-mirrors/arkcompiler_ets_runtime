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

// An import initialized when the function is compiled can never return to the
// TDZ, so its check is dropped and every read still loads the live value. An
// import still in the TDZ at compile time keeps its check.

//! METHOD readImports
//! COUNT_LE BranchIfReferenceEqual 1  # the return reads importedLate, still in the TDZ at compile time

declare function print(arg: string): void;

declare class ArkTools {
    static arkSteedCompileSync<T extends Function>(func: T): T;
}

// Importing this module itself forms a cycle, so the frontend keeps the TDZ checks.
import { live as importedLive, late as importedLate } from './ldextvar_initialized';

export let live = 1;

function bump(): void {
    live++;
}

function readImports(): string {
    const before = importedLive;
    bump();
    return before + ":" + importedLive + ":" + importedLate;
}

for (let i = 0; i < 100; i++) {
    try {
        readImports();
    } catch (e) {
    }
}
ArkTools.arkSteedCompileSync(readImports);
try {
    print(readImports());
} catch (e) {
    print((e as Error).name);
}
export let late = 10;
print(readImports());
