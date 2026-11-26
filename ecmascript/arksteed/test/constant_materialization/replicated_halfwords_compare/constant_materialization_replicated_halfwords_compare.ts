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
// AssemblerAarch64::TryReplicateHWords: the guard skipped *valid* logical
// immediates and emitted ORR with *invalid* ones (and read the uninitialized
// rd instead of xzr), aborting the VM during JIT compilation. Each literal
// below is a double whose 64-bit pattern repeats one halfword 2 or 3 times.
// A double literal has static type DOUBLE, so comparing against it forces
// the F64 compare path, which materializes the raw bits through
// ArkSteedAssembler::Move(int64) -> AssemblerAarch64::Mov even without PGO.
//
//   A = 10846169068898440     -> 0x4343444443434444 (0x4343 x2, replication
//                                unencodable: crashed before the fix; after
//                                the fix ORR(0x4444...) + MOVK x2)
//   B = 747563348316297500000 -> 0x4444434344444343 (0x4343 x2, unencodable)
//   C = 10843961455708296     -> 0x4343434343434444 (0x4343 x3, unencodable;
//                                after the fix no halfword qualifies and the
//                                MOVZ/MOVK fallback must stay exact)
//   E = 10836649164668932     -> 0x43433FF000020002 (0x0002 x2, replication
//                                encodable: the fixed guard must take the
//                                ORR + MOVK shortcut and stay bit-exact)

//! COMPILE_COUNT_GE *ltA* 1
//! EXEC_COUNT_GE *ltA* 1
//! COMPILE_COUNT_GE *eqA* 1
//! EXEC_COUNT_GE *eqA* 1
//! COMPILE_COUNT_GE *ltB* 1
//! EXEC_COUNT_GE *ltB* 1
//! COMPILE_COUNT_GE *eqB* 1
//! EXEC_COUNT_GE *eqB* 1
//! COMPILE_COUNT_GE *ltC* 1
//! EXEC_COUNT_GE *ltC* 1
//! COMPILE_COUNT_GE *eqC* 1
//! EXEC_COUNT_GE *eqC* 1
//! COMPILE_COUNT_GE *ltE* 1
//! EXEC_COUNT_GE *ltE* 1
//! COMPILE_COUNT_GE *eqE* 1
//! EXEC_COUNT_GE *eqE* 1
//! DEOPT_COUNT *Deoptimize* 0

function ltA(x: number): boolean { return x < 10846169068898440.0; }  // 10846169068898440.0: bit pattern A.
function eqA(x: number): boolean { return x === 10846169068898440.0; }  // 10846169068898440.0: bit pattern A.
function ltB(x: number): boolean { return x < 747563348316297500000.0; }  // 747563348316297500000.0: bit pattern B.
function eqB(x: number): boolean { return x === 747563348316297500000.0; }  // 747563348316297500000.0: bit pattern B.
function ltC(x: number): boolean { return x < 10843961455708296.0; }  // 10843961455708296.0: bit pattern C.
function eqC(x: number): boolean { return x === 10843961455708296.0; }  // 10843961455708296.0: bit pattern C.
function ltE(x: number): boolean { return x < 10836649164668932.0; }  // 10836649164668932.0: bit pattern E.
function eqE(x: number): boolean { return x === 10836649164668932.0; }  // 10836649164668932.0: bit pattern E.

ArkTools.arkSteedCompileSync(ltA);
ArkTools.arkSteedCompileSync(eqA);
ArkTools.arkSteedCompileSync(ltB);
ArkTools.arkSteedCompileSync(eqB);
ArkTools.arkSteedCompileSync(ltC);
ArkTools.arkSteedCompileSync(eqC);
ArkTools.arkSteedCompileSync(ltE);
ArkTools.arkSteedCompileSync(eqE);

print(ltA(0.5));
print(ltA(10846169068898440.0));  // 10846169068898440.0: input equal to A.
print(eqA(10846169068898440.0));  // 10846169068898440.0: input equal to A.
print(eqA(0.5));
print(ltB(0.5));
print(ltB(747563348316297500000.0));  // 747563348316297500000.0: input equal to B.
print(eqB(747563348316297500000.0));  // 747563348316297500000.0: input equal to B.
print(eqB(0.5));
print(ltC(0.5));
print(ltC(10843961455708296.0));  // 10843961455708296.0: input equal to C.
print(eqC(10843961455708296.0));  // 10843961455708296.0: input equal to C.
print(eqC(0.5));
print(ltE(0.5));
print(ltE(10836649164668932.0));  // 10836649164668932.0: input equal to E.
print(eqE(10836649164668932.0));  // 10836649164668932.0: input equal to E.
print(eqE(0.5));