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

// Global-cell transition carrier of G11.7 (S12). The target performs a real
// global access whose bytecode lowers through the PropertyBox cell path
// (`TryBuildLoadGlobalCell`), so the compiled graph holds the inline
// `LoadTaggedField` of `PropertyBox::VALUE_OFFSET` and the `DeoptIfTaggedCondition`
// guard comparing that value with `VALUE_HOLE` (PROPERTYBOXINVALID).
// The cell must be non-empty at compilation time (ReadGlobalAccessFeedback
// rejects an invalid box), therefore the scenario initializes it, warms and
// compiles the target, prints the attached values, and only then brings the
// same box to the hole by deleting the global property. The next compiled
// access must deopt and the recovered interpreter path must raise the
// specified ReferenceError for the now unresolvable global reference.
//! PARAMS --compiler-jit-hotness-threshold=6000

//! METHOD readGlobalCell
//! HAS HeapConstant
//! HAS LoadTaggedField
//! HAS DeoptIfTaggedCondition
//! HAS_NOT CallCommonStub TryLdGlobalByName
//! COMPILE_COUNT_GE *readGlobalCell* 1
//! EXEC_COUNT_GE *readGlobalCell* 1
//! DEOPT_COUNT_GE *Deoptimize* 1

globalThis.gCell = 7;

function readGlobalCell(x)
{
    return gCell + x;
}

for (let i = 0; i < 20000; i++) {
    readGlobalCell(i);
}

ArkTools.arkSteedCompileSync(readGlobalCell);
ArkTools.waitJitCompileFinish(readGlobalCell);

// Attached state: the inlined compiled box read returns the live cell value.
const attached = readGlobalCell(0);
print(attached);                                  // 7

const typeBefore = typeof globalThis.gCell;
print(typeBefore === "number" ? 1 : 0);           // 1: the cell is still resolvable

// Clear the same PropertyBox to VALUE_HOLE and drop the global record.
const removed = delete globalThis.gCell;
print(removed ? 1 : 0);                           // 1: the deletion happened

// The compiled guard observes hole and deopts; recovery raises the specified
// ReferenceError instead of returning a stale value or a silent undefined.
let produced = -1;
let caught = "none";
try {
    produced = readGlobalCell(0);
} catch (e) {
    caught = e.name;
}
print(produced === -1 ? 1 : 0);                   // 1: no successful value
print(caught);                                    // ReferenceError

const typeAfter = typeof globalThis.gCell;
print(typeAfter === "undefined" ? 1 : 0);         // 1: post-state is unchanged
