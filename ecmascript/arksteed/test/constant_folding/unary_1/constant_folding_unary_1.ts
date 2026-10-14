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

// G4.1j (P0) additive evidence (arksteed-jsts-p0-pipeline-fact-rows#C6): the
// semantics and the expected output of this case are unchanged; the added
// assertions only show that the ArkSteed graph builder folds the constant
// unary negation, bitwise-not and the constant additions and subtractions of
// this method and that the compiled code is entered without a deopt. The
// frontend keeps every operation in the bytecode (`neg 0x0`, `not 0x1`,
// `add2 0x2, v0`, `sub2 0x3, v0` on the method's own disassembly), while the
// compiled CFG of the same method has no checked unary or arithmetic vertex
// for those removed operations. The case synchronously compiles the target once
// and calls it twice, so the compile and entry counters below are exact for
// this construction.

//! METHOD constant_folding_unary_1
//! BYTECODE_COUNT neg 0x0 1
//! BYTECODE_COUNT not 0x1 1
//! BYTECODE_COUNT add2 0x2, v0 1
//! BYTECODE_COUNT sub2 0x3, v0 1
//! HAS_NOT I32NegWithOverflow
//! HAS_NOT I32BNot
//! HAS_NOT I32AddWithOverflow
//! HAS_NOT I32SubWithOverflow
//! COMPILE_COUNT *constant_folding_unary_1* 1
//! COMPILE_COUNT_GE *constant_folding_unary_1* 1
//! EXEC_COUNT *constant_folding_unary_1* 2
//! EXEC_COUNT_GE *constant_folding_unary_1* 1
//! DEOPT_COUNT *Deoptimize* 0

function constant_folding_unary_1(x: number)
{
    const A = 10;

    let a = -A;      // -10  (NEG)
    let b = ~A;      // -11  (NOT - bitwise not)
    let c = A + 1;   // 11   (ADD2, tests that const propagates)
    let d = A - 1;   // 9    (SUB2)

    return x + a + b + c + d;   // x + (-10) + (-11) + 11 + 9 = x - 1
}

ArkTools.arkSteedCompileSync(constant_folding_unary_1);

print(constant_folding_unary_1(100));
print(constant_folding_unary_1(-100));
