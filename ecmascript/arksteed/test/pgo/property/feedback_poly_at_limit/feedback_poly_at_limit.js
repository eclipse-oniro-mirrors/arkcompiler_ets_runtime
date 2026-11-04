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

// G2.3j (P0) poly named-access feedback decision at the delivered limit: the
// named-access site of `loadPoly` observes exactly four receiver shapes, which
// is the named-IC poly capacity (`MAX_NAMED_IC_POLY_CASES = MAX_IC_POLY_CASES
// = 4`, `arksteed_pgo_access_info.h:29-31`). The recorded slot therefore holds
// a poly snapshot of four cases and the feedback reader resolves the whole set,
// so the compiler guards it with the membership check `DeoptIfHClassNotIn`
// instead of the single-class `DeoptIfHClassMismatch`. The raised hotness
// threshold keeps the warm-up in the interpreter. The IC-state precondition is
// read, printed and compared before `ArkTools.arkSteedCompileSync` installs the
// code, and it is bound to the real stimulus by two independent oracles over the
// actual warm-up: `ArkTools.getHClass` identity is computed for every warm-up
// receiver (the `warmShapes` list the warm-up loop iterates), and the target
// itself counts its own invocations in a test-local counter placed after the
// field load. The slot state `poly` alone does not fix the population size, so
// the printed predicate requires the poly state, exactly four distinct warm
// HClasses and exactly the 400 warm-up invocations; a third distinct layout (two
// receivers sharing one HClass), a fifth distinct layout and an extra warm-up
// receiver call all turn the case red. After installation the site is entered
// only with the four warmed shapes, so the guard holds and the generated code
// stays on its normal path (`DEOPT_COUNT *Deoptimize* 0`). The construction fixes
// the event multiplicity exactly: the raised threshold keeps the warm-up
// interpreter-only, `ArkTools.arkSteedCompileSync` compiles the target once
// synchronously, and the field-load target has no recursion and no other
// compilation hook, so the case carries the exact `COMPILE_COUNT *loadPoly* 1`
// and `EXEC_COUNT *loadPoly* 4` beside the required per-target lower bounds.

//! PARAMS --compiler-jit-hotness-threshold=60000

//! METHOD loadPoly
//! HAS DeoptIfHClassNotIn
//! HAS_NOT DeoptIfHClassMismatch
//! HAS LoadTaggedField
let loadPolyCalls = 0;

function loadPoly(record) {
    const value = record.x;
    loadPolyCalls = loadPolyCalls + 1;
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
const warmShapes = [shapeA, shapeB, shapeC, shapeD];

let warm = 0;
for (let i = 0; i < 100; i++) {
    for (let s = 0; s < warmShapes.length; s++) {
        warm += loadPoly(warmShapes[s]);
    }
}

const stateBeforeCompile = ArkTools.getICState(loadPoly, 0, 0);
const distinctWarmShapes = countDistinctHClasses(warmShapes);
const warmUpCalls = loadPolyCalls;
print("poly precondition: " + stateBeforeCompile);
print("distinct warm shapes: " + distinctWarmShapes);
print("warm-up calls: " + warmUpCalls);
print(stateBeforeCompile === "poly" && distinctWarmShapes === 4 && warmUpCalls === 400 ? "PASS" : "FAIL");

ArkTools.arkSteedCompileSync(loadPoly);

//! COMPILE_COUNT_GE *loadPoly* 1
//! EXEC_COUNT_GE *loadPoly* 1
//! COMPILE_COUNT *loadPoly* 1
//! EXEC_COUNT *loadPoly* 4
//! DEOPT_COUNT *Deoptimize* 0

print(warm);
print(loadPoly(shapeA));
print(loadPoly(shapeB));
print(loadPoly(shapeC));
print(loadPoly(shapeD));
