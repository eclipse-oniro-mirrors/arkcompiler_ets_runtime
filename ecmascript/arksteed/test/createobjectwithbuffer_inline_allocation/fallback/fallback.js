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

// Keep the lack-profile guard disabled so the cold branch reaches the original stub.
//! PARAMS --asm-interpreter=true --compiler-jit-hotness-threshold=60000 --compiler-enable-baselinejit=false --enable-cmc-gc=false --compiler-arksteed-deopt-on-insufficient-profile=false

// Unsupported warmed templates must be rejected at compile time, with no partial fast path.
//! METHOD createString
//! HAS_NOT AllocateInYoung
//! HAS_NOT InitializeObjectWithBuffer
//! BYTECODE_COUNT createobjectwithbuffer 1
//! COUNT CallCommonStub CreateObjectHavingMethod 1

//! METHOD createMethods
//! HAS_NOT AllocateInYoung
//! HAS_NOT InitializeObjectWithBuffer
//! BYTECODE_COUNT createobjectwithbuffer 1
//! COUNT CallCommonStub CreateObjectHavingMethod 1

//! METHOD createAccessors
//! HAS_NOT AllocateInYoung
//! HAS_NOT InitializeObjectWithBuffer
//! BYTECODE_COUNT createobjectwithbuffer 1
//! COUNT CallCommonStub CreateObjectHavingMethod 1

//! METHOD createElements
//! HAS_NOT AllocateInYoung
//! HAS_NOT InitializeObjectWithBuffer
//! BYTECODE_COUNT createobjectwithbuffer 1
//! COUNT CallCommonStub CreateObjectHavingMethod 1

//! METHOD createCold
//! HAS_NOT AllocateInYoung
//! BYTECODE_COUNT createobjectwithbuffer 1
//! HAS_NOT InitializeObjectWithBuffer
//! COUNT CallCommonStub CreateObjectHavingMethod 1

function createString() {
    return { text: "heap-valued-template", count: 5 };
}

function createMethods(captured) {
    return { offset: 1, read() { return captured + this.offset + super.base; } };
}

function createAccessors(seed) {
    let state = seed;
    return {
        offset: 1,
        get value() { return state + this.offset + super.base; },
        set value(next) { state = next - this.offset - super.base; }
    };
}

function createElements() {
    return { 2: 22, 7: 77, label: 5 };
}

function createCold(takeLiteral) {
    if (takeLiteral) {
        return { cold: 98761, flag: true };
    }
    return null;
}

function compile(func) {
    ArkTools.arkSteedCompileAsync(func);
    ArkTools.waitJitCompileFinish(func);
    return ArkTools.arkSteedIsCompiled(func);
}

for (let i = 0; i < 32; ++i) {
    createString();
    createMethods(i);
    createAccessors(i);
    createElements();
    createCold(false);
}
let compiled = compile(createString);
compiled = compile(createMethods) && compiled;
compiled = compile(createAccessors) && compiled;
compiled = compile(createElements) && compiled;
compiled = compile(createCold) && compiled;
print("compiled:" + compiled);

let stringFirst = createString();
let stringSecond = createString();
stringFirst.text = { marker: 9 };
print("string-fallback:" + (stringFirst !== stringSecond && stringFirst.text.marker === 9 &&
    stringSecond.text === "heap-valued-template" && createString().text === stringSecond.text));

let baseFirst = { base: 100 };
let baseSecond = { base: 200 };
let methodFirst = createMethods(10);
let methodSecond = createMethods(20);
Object.setPrototypeOf(methodFirst, baseFirst);
Object.setPrototypeOf(methodSecond, baseSecond);
let methodsOk = methodFirst !== methodSecond && methodFirst.read !== methodSecond.read &&
    methodFirst.read() === 111 && methodSecond.read() === 221 &&
    methodFirst.read.call(methodSecond) === 111;
// super resolves through the method's own home object, not its call receiver.
Object.setPrototypeOf(methodFirst, { base: 300 });
print("methods-env-and-home-object:" + (methodsOk && methodFirst.read() === 311 &&
    methodFirst.read.call(methodSecond) === 311 && methodSecond.read() === 221));

let accessorFirst = createAccessors(10);
let accessorSecond = createAccessors(20);
Object.setPrototypeOf(accessorFirst, baseFirst);
Object.setPrototypeOf(accessorSecond, baseSecond);
let firstDescriptor = Object.getOwnPropertyDescriptor(accessorFirst, "value");
let secondDescriptor = Object.getOwnPropertyDescriptor(accessorSecond, "value");
let accessorsOk = accessorFirst.value === 111 && accessorSecond.value === 221 &&
    firstDescriptor.get !== secondDescriptor.get && firstDescriptor.set !== secondDescriptor.set &&
    firstDescriptor.enumerable && firstDescriptor.configurable && !("writable" in firstDescriptor);
accessorFirst.value = 150;
accessorsOk = accessorsOk && accessorFirst.value === 150 && accessorSecond.value === 221 &&
    firstDescriptor.get.call(accessorSecond) === 150;
Object.defineProperty(accessorFirst, "value", { value: 999 });
let accessorFresh = createAccessors(30);
Object.setPrototypeOf(accessorFresh, baseFirst);
print("accessors-env-and-descriptors:" + (accessorsOk && accessorFirst.value === 999 &&
    accessorSecond.value === 221 && accessorFresh.value === 131 &&
    Object.getOwnPropertyDescriptor(accessorSecond, "value").get === secondDescriptor.get));

let elementsFirst = createElements();
let elementsSecond = createElements();
let elementDescriptor = Object.getOwnPropertyDescriptor(elementsFirst, "2");
let elementsOk = elementsFirst !== elementsSecond && elementsFirst[2] === 22 &&
    elementsFirst[7] === 77 && Object.keys(elementsFirst).join(",") === "2,7,label" &&
    Object.getPrototypeOf(elementsFirst) === Object.prototype && elementDescriptor.writable &&
    elementDescriptor.enumerable && elementDescriptor.configurable;
elementsFirst[2] = { marker: 7 };
delete elementsFirst[7];
elementsSecond[7] = 88;
let elementsFresh = createElements();
print("elements-fallback:" + (elementsOk && elementsFirst[2].marker === 7 && !(7 in elementsFirst) &&
    elementsSecond[2] === 22 && elementsSecond[7] === 88 && elementsFresh[2] === 22 &&
    elementsFresh[7] === 77 && elementsFresh.label === 5));

// No true call is allowed before compilation: the literal cache must stay cold.
let coldFirst = createCold(true);
let coldSecond = createCold(true);
coldFirst.cold = 0;
print("cold-template-fallback:" + (coldFirst !== coldSecond && coldFirst.cold === 0 &&
    coldSecond.cold === 98761 && coldSecond.flag && createCold(false) === null &&
    ArkTools.arkSteedIsCompiled(createCold)));
print("still-compiled:" + (ArkTools.arkSteedIsCompiled(createString) &&
    ArkTools.arkSteedIsCompiled(createMethods) && ArkTools.arkSteedIsCompiled(createAccessors) &&
    ArkTools.arkSteedIsCompiled(createElements)));
