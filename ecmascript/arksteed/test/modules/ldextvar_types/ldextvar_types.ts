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

// Test: inlined ldexternalmodulevar must forward values of every tagged type
// unchanged. The reader sums / concatenates imported bindings of int, double,
// string, null, undefined, boolean and NaN; exact values are checked against
// expected_output.txt.

declare function print(arg: string): void;

declare class ArkTools {
    static arkSteedCompileSync<T extends Function>(func: T): T;
}

//! METHOD      reader
//! HAS_NOT     CallRuntime      LdExternalModuleVarByIndexOnJSFunc
//! HAS         LoadTaggedField
//! HAS         LoadTaggedElement

import { inum as a, dblnum as b, strv as c, nilv as d, undv as e,
         flagv as f, nanv as g } from './ldextvar_types.ts';

export const inum = 42;
export const dblnum = 2147483648.5;
export const strv = 'str';
export const nilv = null;
export const undv = undefined;
export const flagv = true;
export const nanv = NaN;

function reader(): string {
    return a + ',' + b + ',' + c + ',' + d + ',' + e + ',' + f + ',' + (g !== g);
}

ArkTools.arkSteedCompileSync(reader);

print(reader());
