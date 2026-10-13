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

// Keep one explicit compilation per factory, including after capacity deopt.
//! PARAMS --compiler-jit-hotness-threshold=0 --compiler-jit-call-threshold=60000 --compiler-enable-baselinejit=false

//! METHOD createEmptyArray
//! HAS_NOT BranchIfReferenceEqual
//! CODEGEN_COUNT * PrepareEmptyArray * 0
//! HAS_NOT BranchIfTaggedHeapObject
//! COUNT Phi 1
//! HAS_NOT LoadTaggedField
//! HAS_NOT LoadInt32Field
//! HAS_NOT BranchIfInt32Compare
//! CODEGEN_COUNT * CheckYoungAllocation * 0
//! CODEGEN_COUNT * LoadObjectSize * 0
//! CODEGEN_COUNT * CheckAllocationSize * 0
//! CODEGEN_COUNT * CheckArrayInitialization * 0
//! COUNT AllocateInYoung 1
//! COUNT InitializeEmptyArray 1
//! HAS_NOT CallCommonStub CreateEmptyArray

//! METHOD createEmptyArrayWithPressure
//! CODEGEN_COUNT * PrepareEmptyArray * 0
//! COUNT AllocateInYoung 1
//! BYTECODE_COUNT createemptyarray 0x3 1
//! COUNT InitializeEmptyArray 1
//! HAS_NOT CallCommonStub CreateEmptyArray
//! CODEGEN_COUNT AllocateInYoung bump allocation 1
//! CODEGEN_COUNT * InitializeEmptyArray * 1
//! CODEGEN_COUNT AllocateJSArray fallback sentinel 0

//! METHOD createEmptyArrayImm16
//! CODEGEN_COUNT * PrepareEmptyArray * 0
//! COUNT AllocateInYoung 1
//! BYTECODE_COUNT createemptyarray 0x11a 1
//! COUNT InitializeEmptyArray 1
//! HAS_NOT CallCommonStub CreateEmptyArray

function createEmptyArray() {
    return [];
}

// Keep twelve tagged values, an int and a double live across the allocation.
function createEmptyArrayWithPressure(a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, seed) {
    let intLive = (seed * 3) | 0;
    let doubleLive = seed + 0.25;
    let result = [];
    let sum = a0.value + a1.value + a2.value + a3.value + a4.value + a5.value;
    sum += a6.value + a7.value + a8.value + a9.value + a10.value + a11.value + intLive;
    let doubleAfterAllocation = doubleLive + 0.5;
    if (sum !== 108 || doubleAfterAllocation !== 10.75) {
        return null;
    }
    return result;
}

// Each property load consumes feedback slots, placing the following empty-array literal above slot 255.
function createEmptyArrayImm16(source) {
    const {
        p0, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19,
        p20, p21, p22, p23, p24, p25, p26, p27, p28, p29, p30, p31, p32, p33, p34, p35, p36, p37, p38, p39,
        p40, p41, p42, p43, p44, p45, p46, p47, p48, p49, p50, p51, p52, p53, p54, p55, p56, p57, p58, p59,
        p60, p61, p62, p63, p64, p65, p66, p67, p68, p69, p70, p71, p72, p73, p74, p75, p76, p77, p78, p79,
        p80, p81, p82, p83, p84, p85, p86, p87, p88, p89, p90, p91, p92, p93, p94, p95, p96, p97, p98, p99,
        p100, p101, p102, p103, p104, p105, p106, p107, p108, p109, p110, p111, p112, p113, p114, p115,
        p116, p117, p118, p119, p120, p121, p122, p123, p124, p125, p126, p127, p128, p129, p130, p131,
        p132, p133, p134, p135, p136, p137, p138, p139
    } = source;
    return [];
}

function checkLengthAccessor(array) {
    let descriptor = Object.getOwnPropertyDescriptor(array, "length");
    if (descriptor.value !== 0 || descriptor.writable !== true ||
        descriptor.enumerable !== false || descriptor.configurable !== false) {
        return false;
    }
    array.push(1, 2, 3);
    array.length = 1;
    if (array.length !== 1 || array[0] !== 1 || 1 in array || 2 in array) {
        return false;
    }
    Object.defineProperty(array, "length", { value: 0, writable: false });
    descriptor = Object.getOwnPropertyDescriptor(array, "length");
    return array.length === 0 && !(0 in array) && descriptor.value === 0 && descriptor.writable === false &&
        descriptor.enumerable === false && descriptor.configurable === false;
}

function forceYoungGC() {
    if (ArkTools.GC !== undefined) {
        ArkTools.GC.startGC("young", undefined, true);
        return;
    }
    ArkTools.gc();
}

function forceFullGC() {
    ArkTools.forceFullGC();
    if (ArkTools.GC !== undefined) {
        ArkTools.GC.startGC("full", undefined, true);
    }
}

function checkSurvivors(survivors, stride) {
    for (let i = 0; i < survivors.length; i++) {
        let array = survivors[i];
        let expected = i * stride;
        if (!Array.isArray(array) || array.length !== 2 ||
            array[0].index !== expected || array[1] !== expected + 0.5) {
            return false;
        }
    }
    return true;
}

let live = [
    { value: 1 }, { value: 2 }, { value: 3 }, { value: 4 },
    { value: 5 }, { value: 6 }, { value: 7 }, { value: 8 },
    { value: 9 }, { value: 10 }, { value: 11 }, { value: 12 }
];
let wideSource = {};

// Establish interpreter behavior and feedback before compiling the allocation sites.
let interpreted = createEmptyArray();
let pressureWarmup = createEmptyArrayWithPressure(
    live[0], live[1], live[2], live[3], live[4], live[5],
    live[6], live[7], live[8], live[9], live[10], live[11], 10);
let imm16Interpreted = createEmptyArrayImm16(wideSource);
let interpretedOk = Array.isArray(interpreted) && interpreted.length === 0 &&
    Object.getPrototypeOf(interpreted) === Array.prototype && !(0 in interpreted);
let interpretedLengthOk = checkLengthAccessor(createEmptyArray());

ArkTools.arkSteedCompileSync(createEmptyArray);
ArkTools.arkSteedCompileSync(createEmptyArrayWithPressure);
ArkTools.arkSteedCompileSync(createEmptyArrayImm16);
ArkTools.waitJitCompileFinish(createEmptyArray);
ArkTools.waitJitCompileFinish(createEmptyArrayWithPressure);
ArkTools.waitJitCompileFinish(createEmptyArrayImm16);

let compiled = ArkTools.arkSteedIsCompiled(createEmptyArray) &&
    ArkTools.arkSteedIsCompiled(createEmptyArrayWithPressure) &&
    ArkTools.arkSteedIsCompiled(createEmptyArrayImm16);
print("compiled:" + compiled);
print("interpreted-empty:" + interpretedOk);

let imm16Compiled = createEmptyArrayImm16(wideSource);
let imm16Ok = Array.isArray(imm16Interpreted) && imm16Interpreted.length === 0 &&
    Array.isArray(imm16Compiled) && imm16Compiled.length === 0 && imm16Compiled !== imm16Interpreted;
print("imm16-empty:" + imm16Ok);

let first = createEmptyArray();
let second = createEmptyArray();
let emptyOk = Array.isArray(first) && Array.isArray(second) && first.length === 0 && second.length === 0 &&
    Object.getPrototypeOf(first) === Array.prototype && !(0 in first) && first !== second && first !== interpreted;
first[0] = "first";
emptyOk = emptyOk && first.length === 1 && first[0] === "first" && second.length === 0 && !(0 in second);
print("empty-and-fresh:" + emptyOk);
print("length-accessor:" + (interpretedLengthOk && checkLengthAccessor(createEmptyArray())));

let objectValue = { kind: "object" };
let transitioned = createEmptyArray();
transitioned[0] = 7;
transitioned[1] = 1.25;
transitioned[2] = objectValue;
transitioned[1024] = "sparse";
let transitionOk = transitioned.length === 1025 && transitioned[0] === 7 && transitioned[1] === 1.25 &&
    transitioned[2] === objectValue && transitioned[1024] === "sparse" && !(3 in transitioned);
print("transitions:" + transitionOk);

// Even with one in-object slot (56 bytes total), 8192 arrays exceed one 256 KB young allocation region.
// Capacity failure may eagerly deopt; both pressure loops check semantics, not installed code.
const pressureIterations = 8192;
let pressured = null;
let pressureOk = Array.isArray(pressureWarmup) && pressureWarmup.length === 0;
for (let i = 0; i < pressureIterations; i++) {
    pressured = createEmptyArrayWithPressure(
        live[0], live[1], live[2], live[3], live[4], live[5],
        live[6], live[7], live[8], live[9], live[10], live[11], 10);
    if (!Array.isArray(pressured) || pressured.length !== 0) {
        pressureOk = false;
        break;
    }
}
print("register-pressure:" + pressureOk);

// These allocations cross the 256 KB young allocation region repeatedly.
const stressIterations = 60000;
const survivorStride = 3000;
let survivors = [];
let previous = null;
let stressOk = true;
for (let i = 0; i < stressIterations; i++) {
    let current = createEmptyArray();
    if (!Array.isArray(current) || current.length !== 0 || current === previous) {
        stressOk = false;
        break;
    }
    if (i % survivorStride === 0) {
        current[0] = { index: i };
        current[1] = i + 0.5;
        survivors.push(current);
    }
    previous = current;
}
stressOk = stressOk && survivors.length === stressIterations / survivorStride;
print("buffer-exhaustion:" + stressOk);

forceYoungGC();
print("young-gc-survivors:" + checkSurvivors(survivors, survivorStride));

forceFullGC();
print("full-gc-survivors:" + checkSurvivors(survivors, survivorStride));
