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

// Interpreter twin of `ic/load_by_name/property_access_mono` (V3/E6): the same
// target methods, the same inputs and the same printed observations with the
// JIT disabled, so the differential comparison covers the property-access
// results. The primary oracle stays the specified ECMA-262 semantics and the
// hand-computed constants; interpreter agreement is a necessary but not
// sufficient check. The twin carries no JIT/IR/count annotation and no
// installation call: with `--compiler-enable-jit=false` nothing is compiled and
// no entry marker is printed, which is exactly the controlled violation the
// primary is sensitive to.
//! PARAMS --compiler-enable-jit=false

function loadField(record) {
    return record.field;
}

function storeField(record, value) {
    record.field = -value;
    return record.field;
}

function loadElement(box, index) {
    return box[index];
}

function loadFromPrototype(receiver) {
    return receiver.field;
}

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
