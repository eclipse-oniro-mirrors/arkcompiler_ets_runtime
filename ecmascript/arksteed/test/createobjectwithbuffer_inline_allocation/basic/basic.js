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

// Warm the templates with profile collection enabled, without automatic compilation.
//! PARAMS --asm-interpreter=true --compiler-jit-hotness-threshold=60000 --compiler-enable-baselinejit=false --enable-cmc-gc=false

//! METHOD createPrimitives
//! COUNT AllocateInYoung 1
//! BYTECODE_COUNT createobjectwithbuffer 0x0,* 1
//! HAS HeapConstant
//! COUNT InitializeObjectWithBuffer 1
//! HAS_NOT CallCommonStub CreateObjectHavingMethod

function createPrimitives() {
    // Integer/fraction values cover numeric semantics; the runtime chooses their field representation.
    // undefined and unary -0 may be initialized by stores after the buffer clone.
    return { integer: 42, fraction: 1.25, yes: true, no: false, empty: null,
        missing: undefined, negativeZero: -0 };
}

function hasValues(object) {
    return object.integer === 42 && object.fraction === 1.25 && object.yes === true &&
        object.no === false && object.empty === null && object.missing === undefined &&
        Object.prototype.hasOwnProperty.call(object, "missing") && Object.is(object.negativeZero, -0);
}

let before;
for (let i = 0; i < 32; ++i) {
    before = createPrimitives();
}
print("interpreted:" + (!ArkTools.arkSteedIsCompiled(createPrimitives) && hasValues(before)));
ArkTools.arkSteedCompileAsync(createPrimitives);
ArkTools.waitJitCompileFinish(createPrimitives);
print("compiled:" + ArkTools.arkSteedIsCompiled(createPrimitives));

let first = createPrimitives();
let second = createPrimitives();
let identities = new Map();
identities.set(first, 11);
identities.set(second, 22);
print("values-and-identity:" + (hasValues(first) && hasValues(second) && first !== second &&
    first !== before && second !== before && identities.size === 2 &&
    identities.get(first) === 11 && identities.get(second) === 22));

// A small ordinary literal has only inline named properties. Grow an INSTANCE
// past its inline capacity; this tests independent external stores, not an
// broker rejection of external-properties templates (which needs a huge literal or native setup).
for (let i = 0; i < 16; ++i) {
    first["extra" + i] = i;
    second["extra" + i] = 100 + i;
}
let externalOk = Object.keys(first).length > ArkTools.getInlinedPropertiesCount(first);
for (let i = 0; i < 16; ++i) {
    externalOk = externalOk && first["extra" + i] === i && second["extra" + i] === 100 + i;
}
delete first.extra15;
let fresh = createPrimitives();
print("external-properties-independent:" + (externalOk && !("extra15" in first) &&
    second.extra15 === 115 && Object.keys(fresh).length === 7 && hasValues(fresh)));

let derived = Object.create(first);
first.integer = { marker: 99 };
first.fraction = 7.5;
delete first.yes;
Object.preventExtensions(second);
let last = createPrimitives();
// Descriptor and prototype isolation are covered by the metadata case.
print("mutations-independent:" + (derived.integer === first.integer && first.integer.marker === 99 &&
    first.fraction === 7.5 && !("yes" in first) &&
    !Object.isExtensible(second) && hasValues(second) && hasValues(before) && hasValues(last) &&
    Object.isExtensible(last)));
