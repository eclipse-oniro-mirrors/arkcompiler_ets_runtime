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

// G2.3j (P0) named-access assumption change: the named-access site of
// `loadShapeChange` is warmed with exactly four receiver shapes, so the
// recorded slot holds a poly snapshot and the compiler guards the resolved set
// of HClasses with the membership check `DeoptIfHClassNotIn`
// (`arksteed_pgo_access_info.h:29-31`). The raised hotness threshold keeps the
// warm-up in the interpreter. The IC-state precondition is read, printed and
// compared before `ArkTools.arkSteedCompileSync` installs the code, and it is
// bound to the real stimulus by two independent oracles over the actual warm-up:
// `ArkTools.getHClass` identity is computed for every warm-up receiver (the
// `warmShapes` list the warm-up loop iterates), and the target itself counts its
// own invocations in a test-local counter placed after the field load. The slot
// state `poly` alone does not fix the population size, so the printed predicate
// requires the poly state, exactly four distinct warm HClasses and exactly the
// 400 warm-up invocations; a third distinct layout (two receivers sharing one
// HClass), a fifth distinct layout and an extra warm-up receiver call all turn
// the case red. The code is installed while that set is still valid and the site
// is entered four times with the warmed shapes, which is the pre-change
// evidence. A receiver whose shape is outside the recorded set is then supplied:
// the membership check fails, the eager transition leaves the generated code and
// the interpreter returns the value of the cold receiver. The values printed
// before the change, the cold value and the value of a warmed receiver
// afterwards are the restored-state oracle, so the case does not rely on a
// printed JIT-state boolean. This is a transition leaf: it carries the positive
// direction `DEOPT_COUNT_GE *Deoptimize* 1` and no zero assertion of the same
// whole-process event.

//! PARAMS --compiler-jit-hotness-threshold=60000

//! METHOD loadShapeChange
//! HAS DeoptIfHClassNotIn
let loadShapeChangeCalls = 0;

function loadShapeChange(record) {
    const value = record.x;
    loadShapeChangeCalls = loadShapeChangeCalls + 1;
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
const cold = { pad: 1, x: 99 };

let warm = 0;
for (let i = 0; i < 100; i++) {
    for (let s = 0; s < warmShapes.length; s++) {
        warm += loadShapeChange(warmShapes[s]);
    }
}

const stateBeforeCompile = ArkTools.getICState(loadShapeChange, 0, 0);
const distinctWarmShapes = countDistinctHClasses(warmShapes);
const warmUpCalls = loadShapeChangeCalls;
print("poly precondition: " + stateBeforeCompile);
print("distinct warm shapes: " + distinctWarmShapes);
print("warm-up calls: " + warmUpCalls);
print(stateBeforeCompile === "poly" && distinctWarmShapes === 4 && warmUpCalls === 400 ? "PASS" : "FAIL");

ArkTools.arkSteedCompileSync(loadShapeChange);

//! COMPILE_COUNT_GE *loadShapeChange* 1
//! EXEC_COUNT_GE *loadShapeChange* 1
//! DEOPT_COUNT_GE *Deoptimize* 1

print(warm);
print(loadShapeChange(shapeA));
print(loadShapeChange(shapeB));
print(loadShapeChange(shapeC));
print(loadShapeChange(shapeD));
print(loadShapeChange(cold));
print(loadShapeChange(shapeA));
