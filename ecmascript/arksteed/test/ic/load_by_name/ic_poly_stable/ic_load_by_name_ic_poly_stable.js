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

// G11.2 (P0) stable poly named load: the same named-access site is warmed with
// exactly four receiver shapes, which is the delivered named-IC poly capacity
// (`MAX_NAMED_IC_POLY_CASES = MAX_IC_POLY_CASES = 4`). The recorded feedback
// therefore holds a two-to-four-case poly snapshot and the compiler guards the
// resolved set of HClasses with one membership check instead of the single
// resolved HClass of a mono site. After the code is installed the site is
// entered only with the warmed shapes, so the guard holds, the load executes in
// the generated code and no deopt event is produced. The work is done on
// dynamic operands (the receiver selector is an index into an array), and the
// raised hotness threshold keeps the warm-up in the interpreter.

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
let shapeD = { field: 40, extra: 4, more: 5, last: 6 };
let shapes = [shapeA, shapeB, shapeC, shapeD];

let warm = 0;
for (let i = 0; i < 200; i++) {
    for (let s = 0; s < 4; s++) {
        warm += loadPoly(shapes[s]);
    }
}

ArkTools.arkSteedCompileSync(loadPoly);

//! COMPILE_COUNT_GE *loadPoly* 1
//! EXEC_COUNT_GE *loadPoly* 1
//! DEOPT_COUNT *Deoptimize* 0

print(loadPoly(shapeA));
print(loadPoly(shapeB));
print(loadPoly(shapeC));
print(loadPoly(shapeD));
print(warm);
