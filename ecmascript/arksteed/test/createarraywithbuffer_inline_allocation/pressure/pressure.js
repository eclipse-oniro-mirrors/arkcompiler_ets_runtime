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
//! PARAMS --compiler-jit-hotness-threshold=60000 --compiler-enable-baselinejit=false

//! METHOD createWithPressure
//! COUNT AllocateInYoung 1
//! BYTECODE_COUNT createarraywithbuffer 1
//! COUNT InitializeArrayWithBuffer 1
//! HAS_NOT CallCommonStub CreateArrayWithBuffer
//! CODEGEN_COUNT AllocateInYoung bump allocation 1
//! CODEGEN_COUNT * InitializeArrayWithBuffer * 1

//! METHOD createImm16
//! COUNT AllocateInYoung 1
//! BYTECODE_COUNT createarraywithbuffer 0x11a,* 1
//! COUNT InitializeArrayWithBuffer 1
//! HAS_NOT CallCommonStub CreateArrayWithBuffer

function createWithPressure(a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, seed) {
    let intLive = (seed * 3) | 0;
    let doubleLive = seed + 0.25;
    let result = [101, 202, 303];
    let sum = a0.value + a1.value + a2.value + a3.value + a4.value + a5.value;
    sum += a6.value + a7.value + a8.value + a9.value + a10.value + a11.value + intLive;
    if (sum !== 108 || doubleLive + 0.5 !== 10.75) {
        return null;
    }
    return result;
}

// Property loads place the literal's feedback slot beyond the IMM8 range.
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
    return [31, 32, 33];
}

let live = [
    { value: 1 }, { value: 2 }, { value: 3 }, { value: 4 },
    { value: 5 }, { value: 6 }, { value: 7 }, { value: 8 },
    { value: 9 }, { value: 10 }, { value: 11 }, { value: 12 }
];
let wideSource = {};
let warmup = createWithPressure(
    live[0], live[1], live[2], live[3], live[4], live[5],
    live[6], live[7], live[8], live[9], live[10], live[11], 10);
let wideBefore = createImm16(wideSource);
ArkTools.arkSteedCompileSync(createWithPressure);
ArkTools.waitJitCompileFinish(createWithPressure);
ArkTools.arkSteedCompileSync(createImm16);
ArkTools.waitJitCompileFinish(createImm16);
print("compiled:" + (ArkTools.arkSteedIsCompiled(createWithPressure) && ArkTools.arkSteedIsCompiled(createImm16)));
let wide = createImm16(wideSource);
wide[0] = 0;
print("imm16:" + (wide !== wideBefore && wideBefore.join(",") === "31,32,33" &&
    createImm16(wideSource).join(",") === "31,32,33"));

const iterations = 60000;
const stride = 3000;
let survivors = [];
let previous = warmup;
let ok = warmup.join(",") === "101,202,303";
for (let i = 0; i < iterations; ++i) {
    let current = createWithPressure(
        live[0], live[1], live[2], live[3], live[4], live[5],
        live[6], live[7], live[8], live[9], live[10], live[11], 10);
    if (current === null || current === previous || current.length !== 3 ||
        current[0] !== 101 || current[1] !== 202 || current[2] !== 303) {
        ok = false;
        break;
    }
    if (i % stride === 0) {
        current[0] = { index: i };
        survivors.push(current);
    }
    previous = current;
}
// Capacity failure may eagerly deopt without changing allocation semantics.
print("register-pressure-and-exhaustion:" + (ok && survivors.length === iterations / stride));

function checkSurvivors() {
    for (let i = 0; i < survivors.length; ++i) {
        let array = survivors[i];
        if (array.length !== 3 || array[0].index !== i * stride || array[1] !== 202 || array[2] !== 303) {
            return false;
        }
    }
    return true;
}

if (ArkTools.GC !== undefined) {
    ArkTools.GC.startGC("young", undefined, true);
} else {
    ArkTools.gc();
}
print("young-gc-survivors:" + checkSurvivors());
ArkTools.forceFullGC();
if (ArkTools.GC !== undefined) {
    ArkTools.GC.startGC("full", undefined, true);
}
print("full-gc-survivors:" + checkSurvivors());
let afterGC = createWithPressure(
    live[0], live[1], live[2], live[3], live[4], live[5],
    live[6], live[7], live[8], live[9], live[10], live[11], 10);
print("template-after-gc:" + (afterGC.join(",") === "101,202,303"));
