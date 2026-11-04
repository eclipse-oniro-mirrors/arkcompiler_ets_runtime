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

// G10.4 (S5) additive evidence on the existing constant-folding bitwise case:
// the folded oracle and the expected output are unchanged; the annotations add
// the compilation of the target method, its entry into generated code and a
// whole-process zero-deopt counter. The constant operands are folded at graph
// build time, so this case stays auxiliary evidence of the folded result, not
// a substitute for the compiled `I32BitwiseBinary`/`I32BNot` path of
// `arithmetic/bitwise_int32`. The required positive evidence is the lower
// bound `COMPILE_COUNT_GE *constant_folding_bitwise_1 1` plus
// `EXEC_COUNT_GE *constant_folding_bitwise_1 1`; the construction bounds the
// multiplicity exactly (the synchronous compilation happens before the first
// call and the case then invokes the target exactly twice with zero deopts),
// so the exact `COMPILE_COUNT`/`EXEC_COUNT` counters are kept beside the lower
// bounds as additional bounded evidence (V2).

//! COMPILE_COUNT_GE *constant_folding_bitwise_1 1
//! EXEC_COUNT_GE *constant_folding_bitwise_1 1
//! COMPILE_COUNT *constant_folding_bitwise_1 1
//! EXEC_COUNT *constant_folding_bitwise_1 2
//! DEOPT_COUNT *Deoptimize* 0

function constant_folding_bitwise_1(x: number)
{
    const A: number = 0xFF;   // 255
    const B: number = 0x0F;   // 15

    let a = A & B;   // 255 & 15 = 15
    let b = A | B;   // 255 | 15 = 255
    let c = a ^ b;   // 15 ^ 255 = 240
    let d = A << 2;  // 255 << 2 = 1020
    let e = d >> 2;  // 1020 >> 2 = 255
    let f = c ^ e;   // 240 ^ 255 = 15
    let g = A ^ B;   // 255 ^ 15 = 240

    return x + a + b + c + d + e + f + g;
    // x + 15 + 255 + 240 + 1020 + 255 + 15 + 240
    // = x + 2040
}

ArkTools.arkSteedCompileSync(constant_folding_bitwise_1);

print(constant_folding_bitwise_1(100));
print(constant_folding_bitwise_1(-100));
