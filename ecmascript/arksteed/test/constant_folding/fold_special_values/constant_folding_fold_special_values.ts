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

// G4.1j (P0): observation of the ArkSteed compile-time fold of int32 and
// float64 constants in the compiled code. The frontend keeps every folded
// operation in the bytecode (a positive `BYTECODE_COUNT` of the arithmetic
// mnemonic on that method's own disassembly), while the compiled CFG of the
// same method does not contain the checked arithmetic vertex of the removed
// operation (`HAS_NOT I32SubWithOverflow`, `HAS_NOT F64Add`, `HAS_NOT F64Div`,
// `HAS_NOT F64Neg`). The raw constant that the typed consumer materializes for
// the folded int result is `Int32Constant` with the decoded value `7`: the
// consumer is the `x + c` bytecode whose only constant operand is the folded
// `10 - 3`. `TaggedConstant` itself is never used as fold evidence because the
// same vertex also carries ordinary literals.
//
// `fold_int_seven` is warmed on dynamic int32 operands so the recorded
// operation feedback of the consumer site is INT and the compiled target takes
// the checked int32 path that materializes `Int32Constant` from the folded
// `TaggedConstant`; the hotness threshold stays far above the warm-up count, so
// the warm-up does not compile anything.
//
// The double-valued folds use double operands that the frontend preserves in
// the disassembly (`fldai` of `0.5`, then `add2`/`sub2`/`div2`/`neg`), so the
// ArkSteed float64 fold fires (the int fold cannot: `TryFoldInt32Unary` refuses
// the negation of zero and int DIV is never folded). The oracle values are the
// specified ECMA-262 2021 / IEEE-754 outcomes of §6.1.6.1.1 (Infinity), the
// NaN-producing division of zero by zero and the negative-zero negation of a
// double zero; every printed line is derived by hand, not from the interpreter.

//! PARAMS --compiler-jit-hotness-threshold=60000

//! METHOD fold_int_seven
//! BYTECODE_COUNT sub2 0x0, v0 1
//! BYTECODE_COUNT add2 0x1, a3 1
//! HAS I32AddWithOverflow
//! HAS_NOT I32SubWithOverflow
//! COUNT Int32Constant 4

//! METHOD fold_infinity
//! BYTECODE_COUNT add2 0x0, v1 1
//! BYTECODE_COUNT sub2 0x1, v2 1
//! BYTECODE_COUNT div2 0x2, v1 1
//! HAS_NOT F64Add
//! HAS_NOT F64Sub
//! HAS_NOT F64Div

//! METHOD fold_nan
//! BYTECODE_COUNT sub2 0x0, v1 1
//! BYTECODE_COUNT div2 0x1, v0 1
//! HAS_NOT F64Sub
//! HAS_NOT F64Div

//! METHOD fold_negative_zero
//! BYTECODE_COUNT sub2 0x0, v1 1
//! BYTECODE_COUNT neg 0x1 1
//! HAS_NOT F64Sub
//! HAS_NOT F64Neg

// The construction fixes the compile and entry multiplicity of every target:
// the 1999 warm-up iterations of `fold_int_seven` stay below the case-local
// `--compiler-jit-hotness-threshold=60000`, so the warm-up compiles nothing;
// each of the four targets is then compiled exactly once by the explicit
// synchronous call, and the program calls `fold_int_seven` twice, `fold_nan`
// twice (the value print and the `Object.is` check) and `fold_infinity` and
// `fold_negative_zero` once. The exact counters below therefore state the
// measured multiplicity, while the lower bounds are retained as well.

//! COMPILE_COUNT *fold_int_seven* 1
//! COMPILE_COUNT *fold_infinity* 1
//! COMPILE_COUNT *fold_nan* 1
//! COMPILE_COUNT *fold_negative_zero* 1
//! COMPILE_COUNT_GE *fold_int_seven* 1
//! COMPILE_COUNT_GE *fold_infinity* 1
//! COMPILE_COUNT_GE *fold_nan* 1
//! COMPILE_COUNT_GE *fold_negative_zero* 1
//! EXEC_COUNT *fold_int_seven* 2
//! EXEC_COUNT *fold_infinity* 1
//! EXEC_COUNT *fold_nan* 2
//! EXEC_COUNT *fold_negative_zero* 1
//! EXEC_COUNT_GE *fold_int_seven* 1
//! EXEC_COUNT_GE *fold_infinity* 1
//! EXEC_COUNT_GE *fold_nan* 1
//! EXEC_COUNT_GE *fold_negative_zero* 1
//! DEOPT_COUNT *Deoptimize* 0

function fold_int_seven(x: number): number
{
    const A = 10;
    const B = 3;

    let c = A - B;   // 7, folded at graph build time
    return x + c;    // x + 7, dynamic operand + folded constant
}

function fold_infinity(): number
{
    const HALF: number = 0.5;

    let one = HALF + HALF;   // double 1.0
    let zero = HALF - HALF;  // double 0.0

    return one / zero;       // +Infinity
}

function fold_nan(): number
{
    const HALF: number = 0.5;

    let zero = HALF - HALF;  // double 0.0

    return zero / zero;      // NaN
}

function fold_negative_zero(): number
{
    const HALF: number = 0.5;

    let zero = HALF - HALF;  // double 0.0

    return -zero;            // -0.0
}

// Deterministic warm-up of the consumer site with dynamic int32 operands only.
let warmUpSink = 0;
for (let i = 1; i < 2000; i++) {
    warmUpSink += fold_int_seven(i);
}

ArkTools.arkSteedCompileSync(fold_int_seven);
ArkTools.arkSteedCompileSync(fold_infinity);
ArkTools.arkSteedCompileSync(fold_nan);
ArkTools.arkSteedCompileSync(fold_negative_zero);

print(fold_int_seven(0));
print(fold_int_seven(100));
print(fold_infinity());
print(fold_nan());
print(Object.is(fold_nan(), NaN));
let negativeZero = fold_negative_zero();
print(Object.is(negativeZero, -0));
print(1 / negativeZero === -Infinity);
