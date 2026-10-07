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

// Core DCE preservation evidence: the allocation, the forced full collection
// and the ordered effect journal are `Required` even though the compiled result
// is a plain number, so the closure keeps them and the observable GC event
// still happens. `GC_COUNT_GE` asserts that the collection is really reported
// and the journal asserts its position in the effect order; the returned value
// is the independently computed sum. Interpreter twin:
// `gc/dce_preserved_allocation_and_gc_interp_twin`.
//
// The witness is scoped to the compiled invocation. The warm-up loop exercises
// the same three-element allocation through `dce_preserved_pressure`, which
// requests no collection, so the only `Trigger by JS` collection recorded for
// the whole case is the one the compiled `dce_preserved_allocation` performs:
// that reason is the reason `ArkTools.forceFullGC()` records
// (`GCReason::TRIGGER_BY_JS`, `GCStats::GCReasonToString`), while an
// allocation-driven warm-up collection carries a different reason and is not
// counted. The journal alone is not the witness, and no warm-up event can
// satisfy the annotation. `DEOPT_COUNT * 0` keeps the compiled invocation from
// falling back to the interpreter, so the counted collection cannot come from an
// interpreter fallback of the same call either.

//! PARAMS --compiler-jit-hotness-threshold=60000 --gc-long-paused-time=0
//! METHOD dce_preserved_allocation
//! HAS AllocateInYoung
//! HAS InitializeEmptyArray
//! COMPILE_COUNT dce_preserved_allocation 1
//! EXEC_COUNT_GE *dce_preserved_allocation* 1
//! DEOPT_COUNT * 0
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

// Warm-up pressure: the same three-element allocation in a method that requests
// no collection, so no collection of this case is a warm-up event and the
// compiled method below is the only source of the counted `Trigger by JS` record.
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
