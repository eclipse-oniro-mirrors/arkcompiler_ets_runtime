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

// Regression for the ARM64 immediate-materialization bug in
// AssemblerAarch64::TryReplicateHWords (inverted LogicalImmediate guard),
// exercised through the PGO-specialized F64 arithmetic path: with
// double-only feedback the binops become F64Mul/F64Add/F64Sub/F64Div and
// materialize the literal's raw bits via ArkSteedAssembler::Move(int64) ->
// AssemblerAarch64::Mov. This mirrors how the test262 DataView getFloat64
// cases crashed under JIT preheating. The constants are the same poison
// patterns as in replicated_halfwords_compare:
//   A = 10846169068898440     -> 0x4343444443434444 (0x4343 x2, unencodable)
//   B = 747563348316297500000 -> 0x4444434344444343 (0x4343 x2, unencodable)
//   C = 10843961455708296     -> 0x4343434343434444 (0x4343 x3, unencodable)
//   F = 710670423110275600000 -> 0x4443434444434444 (0x4443 x2, unencodable)
// The printed results are chosen so none equals its source constant, i.e.
// a wrong materialization cannot hide behind an absorbing operand.

//! PARAMS --compiler-jit-hotness-threshold=2000 --enable-pgo-profiler=true

//! METHOD mulA
//! HAS F64Mul

//! METHOD addB
//! HAS F64Add

//! METHOD subC
//! HAS F64Sub

//! METHOD divF
//! HAS F64Div

//! COMPILE_COUNT_GE *mulA* 1
//! EXEC_COUNT_GE *mulA* 1
//! COMPILE_COUNT_GE *addB* 1
//! EXEC_COUNT_GE *addB* 1
//! COMPILE_COUNT_GE *subC* 1
//! EXEC_COUNT_GE *subC* 1
//! COMPILE_COUNT_GE *divF* 1
//! EXEC_COUNT_GE *divF* 1
//! DEOPT_COUNT *Deoptimize* 0

function mulA(x: number): number { return x * 10846169068898440.0; }  // 10846169068898440.0: bit pattern A.
function addB(x: number): number { return x + 747563348316297500000.0; }  // 747563348316297500000.0: bit pattern B.
function subC(x: number): number { return x - 10843961455708296.0; }  // 10843961455708296.0: bit pattern C.
function divF(x: number): number { return x / 710670423110275600000.0; }  // 710670423110275600000.0: bit pattern F.

for (let i = 0; i < 2000; i++) {  // 2000: warmup iterations to collect double feedback.
    mulA(1.5);  // 1.5: non-integral input for double feedback.
    addB(131072.0);  // 131072.0: one ULP at B, so adding it changes the result.
    subC(1.5);  // 1.5: non-integral input for double feedback.
    divF(1.5);  // 1.5: non-integral input for double feedback.
}

ArkTools.arkSteedCompileSync(mulA);
ArkTools.arkSteedCompileSync(addB);
ArkTools.arkSteedCompileSync(subC);
ArkTools.arkSteedCompileSync(divF);

print(mulA(1.5));  // 1.5: use the same double input as during warmup.
print(mulA(0.5));
print(addB(131072.0));  // 131072.0: add one ULP at B.
print(addB(262144.0));  // 262144.0: add two ULPs at B.
print(subC(1.5));  // 1.5: use the same double input as during warmup.
print(subC(0.5));
print(divF(1.5));  // 1.5: use the same double input as during warmup.
print(divF(3.0));