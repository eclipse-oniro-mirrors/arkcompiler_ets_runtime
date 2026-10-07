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

// Core DCE preservation evidence: the property write, the observable
// property read and the call of the optimized method are `Required`, so the
// closure keeps them and the ordered effect journal with its multiplicity, the
// exact returned value and the observable receiver state are unchanged. The
// interpreter twin `dce_preserved_property_effects_interp_twin` prints the same
// journal and values with the JIT disabled.

//! PARAMS --compiler-jit-hotness-threshold=60000
//! METHOD dce_preserved_property_effects
//! HAS StoreTaggedFieldWithBarrier
//! HAS LoadTaggedField
//! HAS CallKnownJSFunction
//! COMPILE_COUNT dce_preserved_property_effects 1
//! EXEC_COUNT_GE *dce_preserved_property_effects* 1
//! DEOPT_COUNT * 0

// @ts-nocheck
declare function print(value: any): void;

let journal: string[] = [];

function note(tag: string): void {
    journal.push(tag);
}

function dce_preserved_property_effects(holder: any, value: number): number {
    holder.field = value;
    note("w");
    let read = holder.field;
    note("r");
    note("c");
    return read;
}

let holder = {field: 0};
for (let i = 1; i < 2000; i++) {
    dce_preserved_property_effects(holder, i);
}
journal = [];
ArkTools.arkSteedCompileSync(dce_preserved_property_effects);

print(dce_preserved_property_effects(holder, 41));
print(journal.join(","));
print(holder.field);
