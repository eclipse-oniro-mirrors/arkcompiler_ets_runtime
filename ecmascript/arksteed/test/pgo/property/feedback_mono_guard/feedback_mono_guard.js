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

// G2.3j (P0) mono named-access feedback decision: the named-access site of
// `loadMono` observes exactly one receiver shape during the warm-up, so the
// recorded slot holds a mono snapshot (one weak HClass in the first slot). The
// feedback reader resolves that single HClass and the compiler guards it with
// `DeoptIfHClassMismatch` before the field load, not with the membership check
// `DeoptIfHClassNotIn` that a poly snapshot would produce. The raised hotness
// threshold keeps the warm-up in the interpreter. The IC-state precondition is
// read, printed and compared before `ArkTools.arkSteedCompileSync` installs the
// code, and it is bound to the real stimulus by two independent oracles over the
// actual warm-up: `ArkTools.getHClass` identity is computed for every warm-up
// receiver (the `warmShapes` list the warm-up loop iterates), and the target
// itself counts its own invocations in a test-local counter placed after the
// field load. The printed predicate therefore requires the mono state, exactly
// one distinct warm HClass and exactly the 100 warm-up invocations, so a wrong
// slot state, a wrong population of receiver shapes or an extra warm-up receiver
// call turns the case red even if the printed values stay correct. The target
// method is then entered only with the warmed shape, so the generated code stays
// on its normal path (`DEOPT_COUNT *Deoptimize* 0`). The construction fixes the
// event multiplicity exactly: the raised threshold keeps the warm-up
// interpreter-only, `ArkTools.arkSteedCompileSync` compiles the target once
// synchronously, and the field-load target has no recursion and no other
// compilation hook, so the case carries the exact `COMPILE_COUNT *loadMono* 1`
// and `EXEC_COUNT *loadMono* 2` beside the required per-target lower bounds.

//! PARAMS --compiler-jit-hotness-threshold=60000

//! METHOD loadMono
//! HAS DeoptIfHClassMismatch
//! HAS_NOT DeoptIfHClassNotIn
//! HAS LoadTaggedField
let loadMonoCalls = 0;

function loadMono(record) {
    const value = record.x;
    loadMonoCalls = loadMonoCalls + 1;
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

const monoRecord = { x: 7, y: 1 };
const warmShapes = [monoRecord];

let warm = 0;
for (let i = 0; i < 100; i++) {
    for (let s = 0; s < warmShapes.length; s++) {
        warm += loadMono(warmShapes[s]);
    }
}

const stateBeforeCompile = ArkTools.getICState(loadMono, 0, 0);
const distinctWarmShapes = countDistinctHClasses(warmShapes);
const warmUpCalls = loadMonoCalls;
print("mono precondition: " + stateBeforeCompile);
print("distinct warm shapes: " + distinctWarmShapes);
print("warm-up calls: " + warmUpCalls);
print(stateBeforeCompile === "mono" && distinctWarmShapes === 1 && warmUpCalls === 100 ? "PASS" : "FAIL");

ArkTools.arkSteedCompileSync(loadMono);

//! COMPILE_COUNT_GE *loadMono* 1
//! EXEC_COUNT_GE *loadMono* 1
//! COMPILE_COUNT *loadMono* 1
//! EXEC_COUNT *loadMono* 2
//! DEOPT_COUNT *Deoptimize* 0

print(warm);
monoRecord.x = 123;
print(loadMono(monoRecord));
monoRecord.x = 456;
print(loadMono(monoRecord));
