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

// Test: an inlined ldexternalmodulevar must yield the same object reference
// as the local binding of the export (identity is preserved, not a copy), and
// mutations through the imported alias must be visible both through the alias
// and through the local module-level binding.

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

import { obj as o, setTag as mutate } from './ldextvar_object.ts';

export let obj = { tag: 'none', nested: { deep: 0 } };

export function setTag(t: string): void {
    obj.tag = t;
}

function reader(n: number): string {
    let acc = 0;
    for (let i = 0; i < n; i++) {
        o.nested.deep = i;
        acc += o.nested.deep;
    }
    return o.tag + ',' + o.nested.deep + ',' + acc + ',' + (o === obj);
}

ArkTools.arkSteedCompileSync(reader);

mutate('A');
print(reader(10));   // A,9,45,true
mutate('B');
print(reader(0));    // B,9,0,true
print(obj.tag);      // B (visible through the local binding as well)
