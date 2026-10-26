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

// G2.3j (P0) named-access feedback beyond the poly limit: the named-access
// site of `loadBeyond` observes five receiver shapes. The fifth distinct shape
// grows the recorded case array past the named-IC poly capacity
// (`MAX_NAMED_IC_POLY_CASES = 4`, `arksteed_pgo_access_info.h:29-31`; the slot
// pair keeps `CACHE_MAX_LEN = 8` entries, `profile_type_info.h:254`), so the
// site leaves the poly state and the slot pair becomes the mega-class state of
// the delivery revision: `ProfileTypeInfoNexus::AddWithoutKeyPoly` writes
// `Hole` into the first slot and the observed key into the second one, which
// `ProfileTypeInfoNexus::GetMegaState` reports as `ic_mega`
// (`ProfileTypeInfoNexus::SetAsMega` with `enableICMega_` set and a string key,
// `profile_type_info.cpp`; the state name is printed below and pinned by
// `expected_output.txt`). `TryGetICPolySnapshot` rejects that pair on its own
// state precondition (`first.IsWeak() || !first.IsHeapObject() ||
// !first.IsTaggedArray() || !second.IsHole()`), the feedback reader returns no
// snapshot, and the named load is lowered to the IC-bearing
// `CallCommonStub GetPropertyByName` instead of a resolved guard plus a tagged
// field load. The defensive `caseCount > MAX_NAMED_IC_POLY_CASES` guard of
// `ReadNamedAccessFeedback` is not the rejection point: the reader never sees a
// poly snapshot here. The raised hotness threshold keeps the warm-up in the
// interpreter. The IC-state precondition is read, printed and compared before
// `ArkTools.arkSteedCompileSync` installs the code, and it is bound to the real
// stimulus by two independent oracles over the actual warm-up:
// `ArkTools.getHClass` identity is computed for every warm-up receiver (the
// `warmShapes` list the warm-up loop iterates), and the target itself counts its
// own invocations in a test-local counter placed after the field load. The mega
// state is absorbing, so `ic_mega` alone does not fix the population size: the
// printed predicate requires the `ic_mega` state, exactly five distinct warm
// HClasses and exactly the 500 warm-up invocations; a fourth distinct layout
// fails the state and the population oracle, a sixth distinct layout fails the
// population oracle, and an extra warm-up receiver call fails the invocation
// counter even though the mega state itself would not change. The installed code
// is entered only with the five warmed shapes, so no deopt is produced
// (`DEOPT_COUNT *Deoptimize* 0`). The construction fixes the event multiplicity
// exactly: the raised threshold keeps the warm-up interpreter-only,
// `ArkTools.arkSteedCompileSync` compiles the target once synchronously, and the
// field-load target has no recursion and no other compilation hook, so the case
// carries the exact `COMPILE_COUNT *loadBeyond* 1` and `EXEC_COUNT *loadBeyond*
// 5` beside the required per-target lower bounds.

//! PARAMS --compiler-jit-hotness-threshold=60000

//! METHOD loadBeyond
//! HAS CallCommonStub GetPropertyByName
//! HAS_NOT DeoptIfHClassNotIn
//! HAS_NOT DeoptIfHClassMismatch
let loadBeyondCalls = 0;

function loadBeyond(record) {
    const value = record.x;
    loadBeyondCalls = loadBeyondCalls + 1;
    return value;
}

// Independent population oracle: counts the distinct HClasses of the receivers
// that the warm-up loop actually passes to the target. It uses the existing
// `ArkTools.getHClass` observation, not a delayed or derived observation.
function countDistinctHClasses(receivers) {
    const hclasses = [];
    for (let i = 0; i < receivers.length; i++) {
        hclasses.push(ArkTools.getHClass(receivers[i]));
    }
    let distinct = 0;
    for (let i = 0; i < hclasses.length; i++) {
        let seenBefore = false;
        for (let j = 0; j < i; j++) {
            if (hclasses[i] === hclasses[j]) {
                seenBefore = true;
                break;
            }
        }
        if (!seenBefore) {
            distinct++;
        }
    }
    return distinct;
}

const shapeA = { x: 10 };
const shapeB = { x: 20, extra: 1 };
const shapeC = { x: 30, extra: 2, more: 3 };
const shapeD = { x: 40, extra: 4, more: 5, last: 6 };
const shapeE = { x: 50, extra: 7, more: 8, last: 9, tail: 10 };
const warmShapes = [shapeA, shapeB, shapeC, shapeD, shapeE];

let warm = 0;
for (let i = 0; i < 100; i++) {
    for (let s = 0; s < warmShapes.length; s++) {
        warm += loadBeyond(warmShapes[s]);
    }
}

const stateBeforeCompile = ArkTools.getICState(loadBeyond, 0, 0);
const distinctWarmShapes = countDistinctHClasses(warmShapes);
const warmUpCalls = loadBeyondCalls;
print("state beyond limit: " + stateBeforeCompile);
print("distinct warm shapes: " + distinctWarmShapes);
print("warm-up calls: " + warmUpCalls);
print(stateBeforeCompile === "ic_mega" && distinctWarmShapes === 5 && warmUpCalls === 500 ? "PASS" : "FAIL");

ArkTools.arkSteedCompileSync(loadBeyond);

//! COMPILE_COUNT_GE *loadBeyond* 1
//! EXEC_COUNT_GE *loadBeyond* 1
//! COMPILE_COUNT *loadBeyond* 1
//! EXEC_COUNT *loadBeyond* 5
//! DEOPT_COUNT *Deoptimize* 0

print(warm);
print(loadBeyond(shapeA));
print(loadBeyond(shapeB));
print(loadBeyond(shapeC));
print(loadBeyond(shapeD));
print(loadBeyond(shapeE));
