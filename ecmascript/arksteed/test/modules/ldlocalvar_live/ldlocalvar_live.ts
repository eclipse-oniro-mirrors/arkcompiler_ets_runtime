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

// Local module slots remain live across calls, writes and loop iterations.
// Initialized indexed dictionaries use direct loads; unsupported compilation states keep the runtime.

declare function print(arg: any): void;

declare class ArkTools {
    static arkSteedCompileSync<T extends Function>(func: T): T;
}

export let counter = 1;
export let token = {};
export let payload = token;
export let numberValue = 1.5;
export let stringValue = 'text';
export let nullValue = null;
export let undefinedValue = undefined;
export let booleanValue = true;
export let bigintValue = 7n;

function mutate(delta: number): void {
    counter += delta;
}

//! METHOD      readAcrossCall
//! HAS         LoadTaggedField
function readAcrossCall(): string {
    let before = counter;
    mutate(1);
    return before + ',' + counter;
}

//! METHOD      readAcrossStore
//! HAS         CallRuntime      StModuleVarByIndexOnJSFunc
//! HAS         LoadTaggedField
function readAcrossStore(): string {
    let before = counter;
    counter = before + 10;
    return before + ',' + counter;
}

//! METHOD      loopReader
//! HAS         LoadTaggedField
function loopReader(): number {
    let sum = 0;
    for (let i = 0; i < 3; i++) {
        mutate(1);
        sum += counter;
    }
    return sum;
}

//! METHOD      typedReader
//! HAS         LoadTaggedField
function typedReader(): string {
    return (payload === token) + ',' + numberValue + ',' + stringValue + ',' + nullValue + ',' +
        undefinedValue + ',' + booleanValue + ',' + bigintValue;
}

ArkTools.arkSteedCompileSync(mutate);
ArkTools.arkSteedCompileSync(readAcrossCall);
ArkTools.arkSteedCompileSync(readAcrossStore);
ArkTools.arkSteedCompileSync(loopReader);
ArkTools.arkSteedCompileSync(typedReader);
print(readAcrossCall());
print(readAcrossCall());
print(readAcrossStore());
print(loopReader());
print(counter);
print(typedReader());
payload = {};
numberValue = 2;
print(typedReader());
