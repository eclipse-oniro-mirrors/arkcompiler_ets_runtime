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

// @ts-nocheck

// G10.4 (P0) normal bitwise path (S5): non-constant integer operands are
// warmed for `&`, `|`, `^`, `<<`, `>>`, `>>>` and `~`, then the target methods
// are compiled and entered on machine code. The four specified boundaries
// `1 << 31`, `-2147483648 >> 1`, `-1 >>> 1` and `~0` are evaluated by the
// compiled code. Every result is a negative-or-non-negative int32 that the
// plain `I32ToTaggedInt` tagging accepts, so this leaf asserts whole-process
// `DEOPT_COUNT *Deoptimize* 0`; the representation transition of `>>>` that
// overflows the signed range is a separate process,
// `arithmetic/bitwise_uint32_boundary`. Oracle: ECMA-262 2021 ToInt32 for SHL
// and SAR, which yield signed int32 results, and, for `>>>`, ToUint32 of both
// operands with an unsigned Number result, with the hand-computed constants
// -2147483648, -1073741824, 2147483647 and -1. The signed reinterpretation of a
// raw 0xffffffff as a negative int32 belongs only to the tagging guard
// `CheckedNonNegativeI32ToTaggedInt` in that separate transition process; it
// does not describe the language semantics. These are native ArkSteed cases; no
// V8 test is borrowed (V7).
//
// Every one of the seven targets carries its own separately matching positive
// compile/entry assertion, so the entry of one method can never mask
// interpreter-only execution of another required target: the required
// per-target lower bounds `COMPILE_COUNT_GE *<method> 1` and
// `EXEC_COUNT_GE *<method> 1` are named for each target instead of an
// aggregate pattern. The construction also bounds the multiplicity exactly --
// the warm-up stays below `--compiler-jit-hotness-threshold=60000`, so no
// hotness-triggered compilation happens, `ArkTools.arkSteedCompileSync`
// compiles the method once synchronously, and the single boundary probe enters
// the generated code exactly once -- so the exact `COMPILE_COUNT`/`EXEC_COUNT`
// counters are kept beside the required lower bounds as additional bounded
// evidence (V2). The whole-process zero-deopt assertion rules out the only
// recompilation source.

//! PARAMS --compiler-jit-hotness-threshold=60000

//! DEOPT_COUNT *Deoptimize* 0

//! METHOD bitwiseInt32And
//! HAS I32BitwiseBinary
//! COMPILE_COUNT_GE *bitwiseInt32And 1
//! EXEC_COUNT_GE *bitwiseInt32And 1
//! COMPILE_COUNT *bitwiseInt32And 1
//! EXEC_COUNT *bitwiseInt32And 1

//! METHOD bitwiseInt32Or
//! HAS I32BitwiseBinary
//! COMPILE_COUNT_GE *bitwiseInt32Or 1
//! EXEC_COUNT_GE *bitwiseInt32Or 1
//! COMPILE_COUNT *bitwiseInt32Or 1
//! EXEC_COUNT *bitwiseInt32Or 1

//! METHOD bitwiseInt32Xor
//! HAS I32BitwiseBinary
//! COMPILE_COUNT_GE *bitwiseInt32Xor 1
//! EXEC_COUNT_GE *bitwiseInt32Xor 1
//! COMPILE_COUNT *bitwiseInt32Xor 1
//! EXEC_COUNT *bitwiseInt32Xor 1

//! METHOD bitwiseInt32Shl
//! HAS I32BitwiseBinary
//! COMPILE_COUNT_GE *bitwiseInt32Shl 1
//! EXEC_COUNT_GE *bitwiseInt32Shl 1
//! COMPILE_COUNT *bitwiseInt32Shl 1
//! EXEC_COUNT *bitwiseInt32Shl 1

//! METHOD bitwiseInt32Shr
//! HAS I32BitwiseBinary
//! COMPILE_COUNT_GE *bitwiseInt32Shr 1
//! EXEC_COUNT_GE *bitwiseInt32Shr 1
//! COMPILE_COUNT *bitwiseInt32Shr 1
//! EXEC_COUNT *bitwiseInt32Shr 1

//! METHOD bitwiseInt32Ushr
//! HAS I32BitwiseBinary
//! COMPILE_COUNT_GE *bitwiseInt32Ushr 1
//! EXEC_COUNT_GE *bitwiseInt32Ushr 1
//! COMPILE_COUNT *bitwiseInt32Ushr 1
//! EXEC_COUNT *bitwiseInt32Ushr 1

//! METHOD bitwiseInt32Not
//! HAS I32BNot
//! COMPILE_COUNT_GE *bitwiseInt32Not 1
//! EXEC_COUNT_GE *bitwiseInt32Not 1
//! COMPILE_COUNT *bitwiseInt32Not 1
//! EXEC_COUNT *bitwiseInt32Not 1

function bitwiseInt32And(left, right) {
    return left & right;
}

function bitwiseInt32Or(left, right) {
    return left | right;
}

function bitwiseInt32Xor(left, right) {
    return left ^ right;
}

function bitwiseInt32Shl(left, right) {
    return left << right;
}

function bitwiseInt32Shr(left, right) {
    return left >> right;
}

function bitwiseInt32Ushr(left, right) {
    return left >>> right;
}

function bitwiseInt32Not(value) {
    return ~value;
}

// Non-constant integer operands keep every target in the int32 bitwise
// domain; the hotness threshold keeps the warm-up loop interpretive so the
// single compilation happens after the feedback is complete.
for (let i = 0; i < 2000; i++) {
    const left = 1000003 + i;
    const right = 7 + (i % 29);
    bitwiseInt32And(left, right);
    bitwiseInt32Or(left, right);
    bitwiseInt32Xor(left, right);
    bitwiseInt32Shl(left, right);
    bitwiseInt32Shr(left, right);
    bitwiseInt32Ushr(left, right);
    bitwiseInt32Not(left);
}

ArkTools.arkSteedCompileSync(bitwiseInt32And);
ArkTools.arkSteedCompileSync(bitwiseInt32Or);
ArkTools.arkSteedCompileSync(bitwiseInt32Xor);
ArkTools.arkSteedCompileSync(bitwiseInt32Shl);
ArkTools.arkSteedCompileSync(bitwiseInt32Shr);
ArkTools.arkSteedCompileSync(bitwiseInt32Ushr);
ArkTools.arkSteedCompileSync(bitwiseInt32Not);

// Specified boundaries evaluated by the compiled methods: 1 << 31,
// -2147483648 >> 1, -1 >>> 1 and ~0. INT_MIN is computed from int operands in
// the interpreter, because the source literal -2147483648 is materialized as
// a double and would trip the int guard of the compiled method instead of
// exercising its bitwise vertex.
const INT_MAX = 2147483647;
const INT_MIN = -2147483647 - 1;
print(bitwiseInt32And(-1, INT_MAX));
print(bitwiseInt32Or(0, INT_MIN));
print(bitwiseInt32Xor(-1, INT_MAX));
print(bitwiseInt32Shl(1, 31));
print(bitwiseInt32Shr(INT_MIN, 1));
print(bitwiseInt32Ushr(-1, 1));
print(bitwiseInt32Not(0));
