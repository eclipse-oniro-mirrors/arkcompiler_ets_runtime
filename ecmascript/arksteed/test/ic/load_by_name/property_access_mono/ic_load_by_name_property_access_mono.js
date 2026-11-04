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

// G11.1 (P0) stable mono property access: one receiver shape resolved by the IC
// feedback, so a single HClass guard is emitted before the property access.
// The leaf exercises the named load, the named store, the variable-index
// element access, the prototype-chain load and the access on a freshly created
// empty object. Every target method is warmed with dynamic operands while the
// raised hotness threshold keeps the warm-up in the interpreter, then the code
// is installed with the existing `ArkTools.arkSteedCompileSync` path and
// entered. The positive compilation/entry counters prove the generated code of
// each target ran, and the zero deopt counter rules out a silent bailout into
// the interpreter after the entry.

//! PARAMS --compiler-jit-hotness-threshold=60000

//! METHOD loadField
//! HAS LoadTaggedField
//! HAS DeoptIfHClassMismatch
function loadField(record) {
    return record.field;
}

//! METHOD storeField
//! HAS StoreTaggedField
//! HAS DeoptIfHClassMismatch
function storeField(record, value) {
    record.field = -value;
    return record.field;
}

//! METHOD loadElement
//! HAS LoadTaggedElement
function loadElement(box, index) {
    return box[index];
}

//! METHOD loadFromPrototype
//! HAS LoadTaggedField
function loadFromPrototype(receiver) {
    return receiver.field;
}

//! METHOD freshAccess
//! HAS AllocateInYoung
//! HAS InitializeEmptyObject
//! HAS LoadTaggedField
function freshAccess(value) {
    const record = {};
    record.field = value;
    return record.field;
}

let warmLeft = { field: 1, other: 1 };
let warmRight = { field: 2, other: 2 };

let holder = { field: 99 };
let receiver = Object.create(holder);
receiver.own = 1;

let box = { tag: 1 };
box[0] = 11;
box[1] = 22;

let sum = 0;
for (let i = 0; i < 2000; i++) {
    sum += loadField((i & 1) === 0 ? warmLeft : warmRight);
    storeField(warmLeft, i);
    sum += loadElement(box, i & 1);
    sum += loadFromPrototype(receiver);
    sum += freshAccess(i);
}

ArkTools.arkSteedCompileSync(loadField);
ArkTools.arkSteedCompileSync(storeField);
ArkTools.arkSteedCompileSync(loadElement);
ArkTools.arkSteedCompileSync(loadFromPrototype);
ArkTools.arkSteedCompileSync(freshAccess);

//! COMPILE_COUNT_GE *loadField* 1
//! EXEC_COUNT_GE *loadField* 1
//! COMPILE_COUNT_GE *storeField* 1
//! EXEC_COUNT_GE *storeField* 1
//! COMPILE_COUNT_GE *loadElement* 1
//! EXEC_COUNT_GE *loadElement* 1
//! COMPILE_COUNT_GE *loadFromPrototype* 1
//! EXEC_COUNT_GE *loadFromPrototype* 1
//! COMPILE_COUNT_GE *freshAccess* 1
//! EXEC_COUNT_GE *freshAccess* 1
//! DEOPT_COUNT *Deoptimize* 0

let reportLeft = { field: 42, other: 9 };
let reportRight = { field: 11, other: 8 };

print(loadField(reportLeft));
print(loadField(reportRight));
print(storeField(reportLeft, 5.5));
print(loadField(reportLeft));
print(loadElement(box, 0));
print(loadElement(box, 1));
print(loadFromPrototype(receiver));
print(freshAccess(3));
print(sum > 0 ? 1 : 0);
