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

//! PARAMS --asm-interpreter=true --compiler-jit-hotness-threshold=60000 --compiler-enable-baselinejit=false --enable-cmc-gc=false

//! METHOD createWithPressure
//! COUNT AllocateInYoung 1
//! BYTECODE_COUNT createobjectwithbuffer 1
//! HAS HeapConstant
//! COUNT InitializeObjectWithBuffer 1
//! HAS_NOT CallCommonStub CreateObjectHavingMethod
//! CODEGEN_COUNT * InitializeObjectWithBuffer * 1
//! CODEGEN_COUNT AllocateInYoung bump allocation 1

//! METHOD createImm16
//! COUNT AllocateInYoung 1
//! BYTECODE_COUNT createobjectwithbuffer 0x11a,* 1
//! HAS HeapConstant
//! COUNT InitializeObjectWithBuffer 1
//! HAS_NOT CallCommonStub CreateObjectHavingMethod

function createWithPressure(a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, seed) {
    // Keep twelve heap roots, eight integer values and eight doubles live across
    // allocation. Use them only afterwards, including after capacity deopt.
    const i0 = (seed + 10) | 0;
    const i1 = (seed + 11) | 0;
    const i2 = (seed + 12) | 0;
    const i3 = (seed + 13) | 0;
    const i4 = (seed + 14) | 0;
    const i5 = (seed + 15) | 0;
    const i6 = (seed + 16) | 0;
    const i7 = (seed + 17) | 0;
    const d0 = seed * 1.125 + 0.25;
    const d1 = seed * 1.25 + 0.5;
    const d2 = seed * 1.375 + 0.75;
    const d3 = seed * 1.5 + 1.0;
    const d4 = seed * 1.625 + 1.25;
    const d5 = seed * 1.75 + 1.5;
    const d6 = seed * 1.875 + 1.75;
    const d7 = seed * 2.0 + 2.0;
    // All 33 fields must survive allocation with the values above still live.
    let result = {
        integer: 101, fraction: 2.5, flag: true,
        p3: 3.25, p4: 4.25, p5: 5.25, p6: 6.25, p7: 7.25,
        p8: 8.25, p9: 9.25, p10: 10.25, p11: 11.25, p12: 12.25, p13: 13.25, p14: 14.25, p15: 15.25,
        p16: 16.25, p17: 17.25, p18: 18.25, p19: 19.25, p20: 20.25, p21: 21.25, p22: 22.25, p23: 23.25,
        p24: 24.25, p25: 25.25, p26: 26.25, p27: 27.25, p28: 28.25, p29: 29.25, p30: 30.25, p31: 31.25,
        p32: 32.25
    };
    let sum = a0.value + a1.value + a2.value + a3.value + a4.value + a5.value +
        a6.value + a7.value + a8.value + a9.value + a10.value + a11.value;
    sum += i0 + i1 + i2 + i3 + i4 + i5 + i6 + i7;
    sum += d0 + d1 + d2 + d3 + d4 + d5 + d6 + d7;
    if (sum !== 195 + seed * 20.5) {
        return null;
    }
    return result;
}

// Use the existing 140-property-load idiom, not runtime-generated source.
// The exact disassembled slot operand must exceed 0xff, proving IMM16 encoding.
function createImm16(source) {
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
    return { integer: 31, fraction: 3.25, flag: false };
}

function hasUnchangedValues(object) {
    if (Object.keys(object).length !== 33 || object.fraction !== 2.5 || object.flag !== true) {
        return false;
    }
    for (let i = 3; i < 33; ++i) {
        if (object["p" + i] !== i + 0.25) {
            return false;
        }
    }
    return true;
}

function hasValues(object) {
    return object !== null && object.integer === 101 && hasUnchangedValues(object);
}

let live = [
    { value: 1 }, { value: 2 }, { value: 3 }, { value: 4 },
    { value: 5 }, { value: 6 }, { value: 7 }, { value: 8 },
    { value: 9 }, { value: 10 }, { value: 11 }, { value: 12 }
];
let wideSource = {};
let warmup;
let wideBefore;
for (let i = 0; i < 32; ++i) {
    warmup = createWithPressure(
        live[0], live[1], live[2], live[3], live[4], live[5],
        live[6], live[7], live[8], live[9], live[10], live[11], i & 7);
    wideBefore = createImm16(wideSource);
}
print("interpreted:" + (hasValues(warmup) && !ArkTools.arkSteedIsCompiled(createWithPressure) &&
    !ArkTools.arkSteedIsCompiled(createImm16)));
ArkTools.arkSteedCompileAsync(createWithPressure);
ArkTools.waitJitCompileFinish(createWithPressure);
ArkTools.arkSteedCompileAsync(createImm16);
ArkTools.waitJitCompileFinish(createImm16);
print("compiled:" + (ArkTools.arkSteedIsCompiled(createWithPressure) && ArkTools.arkSteedIsCompiled(createImm16)));

let wideFirst = createImm16(wideSource);
let wideSecond = createImm16(wideSource);
let wideOk = wideFirst !== wideSecond && wideFirst !== wideBefore && wideFirst.integer === 31 &&
    wideFirst.fraction === 3.25 && wideFirst.flag === false;
wideFirst.integer = 0;
print("imm16:" + (wideOk && wideSecond.integer === 31 && wideBefore.integer === 31 &&
    createImm16(wideSource).integer === 31 && Object.getPrototypeOf(wideSecond) === Object.prototype));

const iterations = 60000;
const stride = 3000;
let survivors = [];
let previous = warmup;
let ok = true;
// Natural allocation pressure is useful in release builds too. Neither this
// loop nor static IR constraints prove a particular allocation branch was hit.
// Either inline factory may eagerly deopt on capacity failure without changing its results.
for (let i = 0; i < iterations; ++i) {
    let current = createWithPressure(
        live[0], live[1], live[2], live[3], live[4], live[5],
        live[6], live[7], live[8], live[9], live[10], live[11], i & 7);
    if (!hasValues(current) || current === previous) {
        ok = false;
        break;
    }
    if (i % stride === 0) {
        current.integer = { index: i };
        survivors.push(current);
    }
    previous = current;
}
for (let i = 0; i < survivors.length; ++i) {
    ok = ok && survivors[i].integer.index === i * stride && hasUnchangedValues(survivors[i]);
}
print("register-and-allocation-pressure:" + (ok && survivors.length === iterations / stride &&
    hasValues(warmup)));
