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

// G10.1 additive evidence (A2, no weakened assertion): `MAX + 1` is lowered
// to the checked increment of the constant boundary, so the compiled method
// carries the overflow vertex and its observed double result comes from the
// eager overflow exit after the generated code was entered.

//! METHOD constant_folding_edge_overflow_1
//! HAS I32IncWithOverflow
//! COMPILE_COUNT_GE *constant_folding_edge_overflow_1* 1
//! EXEC_COUNT_GE *constant_folding_edge_overflow_1* 1
//! DEOPT_COUNT_GE *Deoptimize* 1

function constant_folding_edge_overflow_1(x: number)
{
    const MAX: number = 2147483647;

    // Overflows int32: should NOT be folded, runtime produces double 2147483648
    let a = MAX + 1;

    return x + a;
}

ArkTools.arkSteedCompileSync(constant_folding_edge_overflow_1);

print(constant_folding_edge_overflow_1(100));
print(constant_folding_edge_overflow_1(-100));
