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

// Interpreter twin of `ic/load_by_name/ic_poly_stable` (V3/E6): the same named
// access on the same four receiver shapes, the same warm-up with dynamic
// operands and the same printed observations with the JIT disabled, so the
// differential comparison covers the poly-site results. The primary oracle
// stays the specified ECMA-262 semantics and the hand-computed constants;
// interpreter agreement is a necessary but not sufficient check. The twin
// carries no JIT/IR/count annotation and no installation call: with
// `--compiler-enable-jit=false` nothing is compiled and no entry marker is
// printed, which is exactly the controlled violation the primary is sensitive
// to.
//! PARAMS --compiler-enable-jit=false

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

print(loadPoly(shapeA));
print(loadPoly(shapeB));
print(loadPoly(shapeC));
print(loadPoly(shapeD));
print(warm);
