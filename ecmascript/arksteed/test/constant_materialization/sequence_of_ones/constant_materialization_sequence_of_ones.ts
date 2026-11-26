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

// Regression for AssemblerAarch64::TrySequenceOfOnes:
// 1. Interior chunk fill used (outside == 0) instead of (inside == 0), producing
//    an invalid LogicalImmediate (0xFFFFFFFF -> SIGILL) for both normal and
//    wraparound runs of ones:
//      NORMAL_MID = 88.01556830573463   -> 0x405600ff1234ff00 (start=0, end=32, mid=16)
//      WRAP_MID   = -2.0088882520602134 -> 0xc000123400ff5678 (start=48, end=16, mid=32)
// 2. ORR seeded from rd instead of xzr, silently leaking stale bits from rd into
//    zero bits of an adjacent-chunk sequence:
//      POISON     = 88.28448473718525   -> 0x40561234ff789aff (sets bits [31:24], [7:0])
//      ADJ_DIRTY  = 88.28442406653994   -> 0x4056123400ffff00 (start=0, end=16)

//! PARAMS --compiler-jit-hotness-threshold=2000 --enable-pgo-profiler=true

//! METHOD addNormalMid
//! HAS F64Add

//! METHOD addWrapMid
//! HAS F64Add

//! METHOD checkAdjDirty
//! HAS F64Add

//! COMPILE_COUNT_GE *addNormalMid* 1
//! EXEC_COUNT_GE *addNormalMid* 1
//! COMPILE_COUNT_GE *addWrapMid* 1
//! EXEC_COUNT_GE *addWrapMid* 1
//! COMPILE_COUNT_GE *checkAdjDirty* 1
//! EXEC_COUNT_GE *checkAdjDirty* 1
//! DEOPT_COUNT *Deoptimize* 0

function addNormalMid(x: number): number {
    return x + 88.01556830573463;  // 88.01556830573463: non-wrapping run with an interior halfword.
}

function addWrapMid(x: number): number {
    return x + -2.0088882520602134;  // -2.0088882520602134: wrapping run with an interior halfword.
}

function checkAdjDirty(x: number): number {
    const a = x + 88.28448473718525;  // 88.28448473718525: leaves bits set in the scratch register.
    const b = x + 88.28442406653994;  // 88.28442406653994: adjacent halfwords that expose stale bits.
    return a - b;
}

for (let i = 0; i < 2000; i++) {  // 2000: warmup iterations to collect double feedback.
    addNormalMid(1.5);  // 1.5: non-integral input for double feedback.
    addWrapMid(1.5);  // 1.5: non-integral input for double feedback.
    checkAdjDirty(1.5);  // 1.5: non-integral input for double feedback.
}

ArkTools.arkSteedCompileSync(addNormalMid);
ArkTools.arkSteedCompileSync(addWrapMid);
ArkTools.arkSteedCompileSync(checkAdjDirty);

const f64 = new Float64Array(1);  // 1: one Float64 value to inspect as two Uint32 words.
const u32 = new Uint32Array(f64.buffer);
function toHex(v: number): string {
    f64[0] = v;
    const hi = u32[1].toString(16).padStart(8, "0");  // 1: high word. 16: hex radix. 8: hex digits per word.
    const lo = u32[0].toString(16).padStart(8, "0");  // 0: low word. 16: hex radix. 8: hex digits per word.
    return "0x" + hi + lo;
}

print(toHex(addNormalMid(1.5)));  // 1.5: use the same double input as during warmup.
print(toHex(addWrapMid(1.5)));  // 1.5: use the same double input as during warmup.
print(toHex(checkAdjDirty(1.5)));  // 1.5: use the same double input as during warmup.
