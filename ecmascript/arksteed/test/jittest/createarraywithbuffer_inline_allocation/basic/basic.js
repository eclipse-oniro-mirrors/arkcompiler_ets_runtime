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

// Disable the global cold-bytecode deopt policy to exercise the literal-cache fallback itself.
//! PARAMS --enable-elements-kind=true --enable-mutant-array=true --compiler-arksteed-deopt-on-insufficient-profile=false

//! METHOD createInts
//! HAS_NOT BranchIfReferenceEqual
//! HAS_NOT BranchIfInt32Compare
//! COUNT Phi 1
//! COUNT CallRuntime AllocateInYoung 1
//! COUNT CanAllocateInYoung 1
//! COUNT BranchIfTrue 1
//! HAS_NOT LoadTaggedField
//! HAS_NOT LoadInt32Field
//! CODEGEN_COUNT * CheckYoungAllocation * 0
//! CODEGEN_COUNT * CheckAllocationSize * 0
//! HAS_NOT CallRuntime CreateArrayWithBuffer
//! COUNT AllocateInYoung 1
//! CODEGEN_COUNT *CallRuntime*RTStub_AllocateInYoung* 1
//! BYTECODE_COUNT createarraywithbuffer 0x0,* 1
//! HAS HeapConstant
//! COUNT InitializeArrayWithBuffer 1
//! HAS_NOT CallCommonStub CreateArrayWithBuffer
//! CODEGEN_COUNT AllocateInYoung bump allocation 1
//! CODEGEN_COUNT * InitializeArrayWithBuffer * 1
//! CODEGEN_COUNT AllocateJSArrayWithBuffer fallback sentinel 0

//! METHOD createDoubles
//! HAS_NOT CallRuntime CreateArrayWithBuffer
//! COUNT AllocateInYoung 1
//! CODEGEN_COUNT *CallRuntime*RTStub_AllocateInYoung* 1
//! COUNT InitializeArrayWithBuffer 1
//! HAS_NOT CallCommonStub CreateArrayWithBuffer

//! METHOD createMixed
//! HAS_NOT CallRuntime CreateArrayWithBuffer
//! COUNT AllocateInYoung 1
//! CODEGEN_COUNT *CallRuntime*RTStub_AllocateInYoung* 1
//! COUNT InitializeArrayWithBuffer 1
//! HAS_NOT CallCommonStub CreateArrayWithBuffer

//! METHOD createLarge
//! HAS_NOT CallRuntime CreateArrayWithBuffer
//! COUNT AllocateInYoung 1
//! CODEGEN_COUNT *CallRuntime*RTStub_AllocateInYoung* 1
//! COUNT InitializeArrayWithBuffer 1

//! METHOD createDynamic
//! HAS_NOT CallRuntime CreateArrayWithBuffer
//! COUNT AllocateInYoung 1
//! CODEGEN_COUNT *CallRuntime*RTStub_AllocateInYoung* 1
//! COUNT InitializeArrayWithBuffer 1

//! METHOD createCold
//! HAS_NOT AllocateInYoung
//! BYTECODE_COUNT createarraywithbuffer 1
//! HAS_NOT InitializeArrayWithBuffer
//! COUNT CallCommonStub CreateArrayWithBuffer 1

function createInts() {
    return [1, 2, 3];
}

function createDoubles() {
    return [1.25, -0, 3.5];
}

function createMixed() {
    return ["literal", true, null, 42, 1.5];
}

function createLarge() {
    return [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15];
}

function createDynamic(value) {
    return [10, 20, value, , 50];
}

function createCold(takeLiteral) {
    if (takeLiteral) {
        return [98761, "cold-template"];
    }
    return null;
}

function compile(func) {
    ArkTools.arkSteedCompileSync(func);
    ArkTools.waitJitCompileFinish(func);
    return ArkTools.arkSteedIsCompiled(func);
}

let interpreted = createInts();
let doublesBefore = createDoubles();
let mixedBefore = createMixed();
let largeBefore = createLarge();
let payload = { value: 99 };
let dynamicBefore = createDynamic(payload);
createCold(false);
let compiled = compile(createInts);
compiled = compile(createDoubles) && compiled;
compiled = compile(createMixed) && compiled;
compiled = compile(createLarge) && compiled;
compiled = compile(createDynamic) && compiled;
compiled = compile(createCold) && compiled;
print("compiled:" + compiled);

let first = createInts();
let second = createInts();
let intsOk = first !== second && first !== interpreted && first.length === 3 &&
    first.join(",") === interpreted.join(",") && Object.getPrototypeOf(first) === Array.prototype &&
    ArkTools.getElementsKind(first) === ArkTools.getElementsKind(interpreted);
first[0] = 77;
first.push(4);
second.length = 1;
let fresh = createInts();
print("ints-and-cow:" + (intsOk && first[0] === 77 && first.length === 4 && second[0] === 1 &&
    second.length === 1 && fresh.join(",") === "1,2,3" && interpreted.join(",") === "1,2,3"));

let doubles = createDoubles();
let doubleKind = ArkTools.getElementsKind(doubles);
let doublesOk = doubles.length === 3 && doubles[0] === 1.25 && Object.is(doubles[1], -0) &&
    doubles[2] === 3.5 && doubleKind === ArkTools.getElementsKind(doublesBefore);
doubles[0] = payload;
delete doubles[2];
let doubleFresh = createDoubles();
print("doubles-and-cow:" + (doublesOk && doubles[0] === payload && !(2 in doubles) &&
    doubleFresh[0] === 1.25 && Object.is(doubleFresh[1], -0) && doubleFresh[2] === 3.5));

let mixed = createMixed();
let mixedOk = mixed.join("|") === mixedBefore.join("|") && mixed[0] === "literal" &&
    mixed[1] === true && mixed[2] === null && mixed[4] === 1.5;
mixed[0] = payload;
print("mixed-and-cow:" + (mixedOk && mixed[0] === payload && createMixed()[0] === "literal"));

let large = createLarge();
large[15] = -1;
print("large-and-cow:" + (large.length === 16 && large[15] === -1 &&
    createLarge()[15] === 15 && largeBefore[15] === 15));

let dynamic = createDynamic(payload);
let dynamicOther = createDynamic("second");
print("dynamic-and-holes:" + (dynamicBefore.length === 5 && dynamic.length === 5 &&
    dynamic[0] === 10 && dynamic[1] === 20 && dynamic[2] === payload && !(3 in dynamic) &&
    dynamic[4] === 50 && dynamicOther[2] === "second" && !(3 in dynamicOther)));

let descriptor = Object.getOwnPropertyDescriptor(createInts(), "length");
let lengthArray = createInts();
Object.defineProperty(lengthArray, "length", { value: 1, writable: false });
print("length-accessor:" + (descriptor.value === 3 && descriptor.writable && !descriptor.enumerable &&
    !descriptor.configurable && lengthArray.length === 1 && !(1 in lengthArray) &&
    !Object.getOwnPropertyDescriptor(lengthArray, "length").writable && createInts().length === 3));

// The first true call must materialize the template through the original stub.
let coldFirst = createCold(true);
let coldSecond = createCold(true);
coldFirst[0] = 0;
print("cold-fallback:" + (coldFirst !== coldSecond && coldSecond[0] === 98761 &&
    coldSecond[1] === "cold-template" && ArkTools.arkSteedIsCompiled(createCold)));
