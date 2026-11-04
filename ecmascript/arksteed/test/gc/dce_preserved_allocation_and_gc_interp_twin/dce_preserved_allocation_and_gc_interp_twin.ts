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

// Interpreter reference of `gc/dce_preserved_allocation_and_gc`: the same
// allocation, forced full collection, ordered effect journal and returned value
// with every compiled tier disabled. Semantic
// projection: no observation-only line exists in the twin, so the runner's
// full filtered stdout comparison is the projection and the collection record
// is filtered whole by the runner's own GC message filter, exactly as in the
// compiled case; no result or journal line is excluded or reordered. The
// zero-count annotations are the no-JIT evidence of this reference. The
// warm-up requests no collection, so the `Trigger by JS` witness of this case
// is the single interpreter invocation that does, like the compiled twin.

//! PARAMS --compiler-enable-jit=false --compiler-enable-baselinejit=false --compiler-enable-osr=false --compiler-enable-framework-aot=false --gc-long-paused-time=0
//! COMPILE_COUNT * 0
//! EXEC_COUNT * 0
//! GC_COUNT_GE *Trigger by JS* 1

// @ts-nocheck
declare function print(value: any): void;

let journal: string[] = [];

function dce_preserved_allocation(seed: number): number {
    let array = [seed, seed + 1, seed + 2];
    journal.push("a");
    ArkTools.forceFullGC();
    journal.push("g");
    return array[0] + array[2];
}

// Warm-up pressure: the same allocation in a method that requests no
// collection, so the only `Trigger by JS` collection of this reference is the
// single interpreted invocation below.
function dce_preserved_pressure(seed: number): number {
    let array = [seed, seed + 1, seed + 2];
    return array[0] + array[2];
}

for (let i = 1; i < 2000; i++) {
    dce_preserved_pressure(i);
}
journal = [];
ArkTools.arkSteedCompileSync(dce_preserved_allocation);

print(dce_preserved_allocation(20));
print(journal.join(","));
