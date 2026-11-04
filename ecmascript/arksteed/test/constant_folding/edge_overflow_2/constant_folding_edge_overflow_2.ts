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

// G10.1 additive evidence (A2, no weakened assertion): the out-of-int32
// constant `MIN - 1` is not folded into an int32 operation; the compiled
// method keeps it as a double constant and adds it through the generic stub,
// so the whole scenario stays on the normal path without a deopt.

//! METHOD constant_folding_edge_overflow_2
//! HAS CallCommonStub Add
//! COMPILE_COUNT_GE *constant_folding_edge_overflow_2* 1
//! EXEC_COUNT_GE *constant_folding_edge_overflow_2* 1
//! DEOPT_COUNT *Deoptimize* 0

function constant_folding_edge_overflow_2(x: number)
{
    const MIN: number = -2147483648;

    // Overflows int32: should NOT be folded, runtime produces double -2147483649
    let a = MIN - 1;

    return x + a;
}

ArkTools.arkSteedCompileSync(constant_folding_edge_overflow_2);

print(constant_folding_edge_overflow_2(100));
print(constant_folding_edge_overflow_2(-100));
