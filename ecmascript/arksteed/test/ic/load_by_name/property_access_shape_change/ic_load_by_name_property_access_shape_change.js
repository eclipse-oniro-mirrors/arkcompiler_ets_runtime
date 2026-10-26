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

// G11.1 (P0) shape change: the same mono-property site is warmed and installed
// with exactly one receiver shape, so the compiled code carries a single
// resolved HClass and the mono guard. A receiver of another shape then enters
// the installed code, the mono guard fails, and the eager transition restores
// interpreter execution with the value of the cold receiver. One class and a
// set of classes are different contracts: the leaf keeps the mono guard and
// must not be satisfied by `DeoptIfHClassNotIn`.

//! PARAMS --compiler-jit-hotness-threshold=60000

//! METHOD loadShape
//! HAS DeoptIfHClassMismatch
//! HAS LoadTaggedField
//! HAS_NOT DeoptIfHClassNotIn
function loadShape(record) {
    return record.field;
}

let mono = { field: 42 };
let other = { padding: 1, field: 77 };

let warm = 0;
for (let i = 0; i < 20; i++) {
    warm += loadShape(mono);
}

ArkTools.arkSteedCompileSync(loadShape);

//! COMPILE_COUNT_GE *loadShape* 1
//! EXEC_COUNT_GE *loadShape* 1
//! DEOPT_COUNT_GE *Deoptimize* 1

print(loadShape(mono));
print(loadShape(other));
print(loadShape(other));
print(loadShape(mono));
print(warm);
