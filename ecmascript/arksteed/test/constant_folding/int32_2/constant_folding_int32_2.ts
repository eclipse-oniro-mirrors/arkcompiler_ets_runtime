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
// int32 additions, subtractions and multiplications of this method and that
// the compiled code is entered without a deopt. The frontend keeps every
// operation in the bytecode (`add2 0x0, v0`, `sub2 0x1, v0`, `mul2 0x2, v2` on
// the method's own disassembly), while the compiled CFG of the same method has
// no checked arithmetic vertex for those removed operations. The case
// synchronously compiles the target once and calls it twice, so the compile and
// entry counters below are exact for this construction.

//! METHOD constant_folding_int32_2
//! BYTECODE_COUNT add2 0x0, v0 1
//! BYTECODE_COUNT sub2 0x1, v0 1
//! BYTECODE_COUNT mul2 0x2, v2 1
//! HAS_NOT I32AddWithOverflow
//! HAS_NOT I32SubWithOverflow
//! HAS_NOT I32MulWithOverflow
//! COMPILE_COUNT *constant_folding_int32_2* 1
//! COMPILE_COUNT_GE *constant_folding_int32_2* 1
//! EXEC_COUNT *constant_folding_int32_2* 2
//! EXEC_COUNT_GE *constant_folding_int32_2* 1
//! DEOPT_COUNT *Deoptimize* 0

function constant_folding_int32_2(x: number)
{
    const A = 11;
    const B = 7;

    let y = A + B;  // 18
    let z = A - B;  // 4
    y *= z;         // 72

    if (y > 80) {
        throw x;    // Unreachable
    }

    return x + y;   // x + 72
}

ArkTools.arkSteedCompileSync(constant_folding_int32_2);

print(constant_folding_int32_2(100));
print(constant_folding_int32_2(-100));
