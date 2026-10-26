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

// G10.4 (S5) additive evidence on the existing constant-folding shift case:
// the `1 << 31` folded oracle and the expected output are unchanged; the
// annotations add the compilation of the target method, its entry into
// generated code and a whole-process zero-deopt counter. The required positive
// evidence is the lower bound `COMPILE_COUNT_GE *constant_folding_edge_bitwise_1 1`
// plus `EXEC_COUNT_GE *constant_folding_edge_bitwise_1 1`; the construction
// bounds the multiplicity exactly (the synchronous compilation happens before
// the first call and the case then invokes the target exactly twice with zero
// deopts), so the exact `COMPILE_COUNT`/`EXEC_COUNT` counters are kept beside
// the lower bounds as additional bounded evidence (V2).

//! COMPILE_COUNT_GE *constant_folding_edge_bitwise_1 1
//! EXEC_COUNT_GE *constant_folding_edge_bitwise_1 1
//! COMPILE_COUNT *constant_folding_edge_bitwise_1 1
//! EXEC_COUNT *constant_folding_edge_bitwise_1 2
//! DEOPT_COUNT *Deoptimize* 0

function constant_folding_edge_bitwise_1(x: number)
{
    const A = 1;

    // 1 << 31 = -2147483648 (SHL max shift boundary)
    let a = A << 31;

    return x + a;
}

ArkTools.arkSteedCompileSync(constant_folding_edge_bitwise_1);

print(constant_folding_edge_bitwise_1(100));
print(constant_folding_edge_bitwise_1(-100));
