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

// Interpreter reference of `objects/dce_preserved_property_effects`: the same
// computation, inputs and effect schedule with every compiled tier disabled.
// Semantic projection: the twin has no observation-only lines, so the runner's
// full filtered stdout comparison is the projection and no line is excluded or
// reordered; the ordered effect journal and the receiver state are compared
// directly. The zero-count annotations are the no-JIT evidence of this
// reference; it is a successful semantic run, never the failing JIT-off
// sensitivity control.

//! PARAMS --compiler-enable-jit=false --compiler-enable-baselinejit=false --compiler-enable-osr=false --compiler-enable-framework-aot=false
//! COMPILE_COUNT * 0
//! EXEC_COUNT * 0

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
