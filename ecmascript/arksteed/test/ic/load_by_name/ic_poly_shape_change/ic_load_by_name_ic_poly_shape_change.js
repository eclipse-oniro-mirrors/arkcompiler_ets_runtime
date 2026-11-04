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

// G11.2 (P0) poly assumption change: the same named-access site keeps the
// set-of-HClasses contract of `ic/load_by_name/ic_poly_stable` — the warmed
// feedback is a poly snapshot, so the compiler emits one membership check for
// the resolved HClasses, not the single resolved HClass of a mono site. The
// site is warmed with three shapes, installed while that set is still valid and
// entered on the warmed shapes, which is the evidence-before part. A receiver
// outside the recorded set is then supplied: the membership check fails, the
// eager transition leaves the generated code and the interpreter returns the
// value of the cold receiver. The change is exercised on dynamic operands after
// the installation call, and the raised hotness threshold keeps the warm-up in
// the interpreter.

//! PARAMS --compiler-jit-hotness-threshold=60000

//! METHOD loadPoly
//! HAS DeoptIfHClassNotIn
//! HAS LoadTaggedField
function loadPoly(record) {
    return record.field;
}

let shapeA = { field: 10 };
let shapeB = { field: 20, extra: 1 };
let shapeC = { field: 30, extra: 2, more: 3 };
let shapes = [shapeA, shapeB, shapeC];
let cold = { pad: 1, field: 99 };

let warm = 0;
for (let i = 0; i < 200; i++) {
    for (let s = 0; s < 3; s++) {
        warm += loadPoly(shapes[s]);
    }
}

ArkTools.arkSteedCompileSync(loadPoly);

//! COMPILE_COUNT_GE *loadPoly* 1
//! EXEC_COUNT_GE *loadPoly* 1
//! DEOPT_COUNT_GE *Deoptimize* 1

print(loadPoly(shapeA));
print(loadPoly(shapeB));
print(loadPoly(shapeC));
print(loadPoly(cold));
print(loadPoly(shapeA));
print(warm);
