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

// G10.4 (P0) unsigned-result representation transition (S5): the logical
// right shift is warmed and compiled on inputs whose uint32 result still fits
// a non-negative int32, so the compiled code tags the raw result through the
// checked `CheckedNonNegativeI32ToTaggedInt` guard. A later dynamic `(-1, 0)`
// produces the raw 0xffffffff, which the guard reads as a negative int32 and
// fails: the method leaves the generated code through an eager deopt and the
// interpreter recovers the specified Number 4294967295 (ECMA-262 2021
// Number::unsignedRightShift -> ToUint32). This probe lives in its own process
// because the eager event contradicts a whole-process zero-deopt counter; the
// stable inputs are covered by `arithmetic/bitwise_int32`. These are native
// ArkSteed cases; no V8 test is borrowed (V7).
//
// The required positive transition evidence is the per-target lower bound
// `COMPILE_COUNT_GE *bitwiseUnsignedBoundary 1`,
// `EXEC_COUNT_GE *bitwiseUnsignedBoundary 1` and the eager event
// `DEOPT_COUNT_GE *Deoptimize* 1`; all of them name this target and this event.
// The construction also bounds the multiplicity exactly:
// `ArkTools.arkSteedCompileSync` compiles the method once synchronously, the
// compiled method is entered exactly twice (the safe `(2147483647, 0)` probe
// and the `(-1, 0)` boundary probe), and the boundary probe raises exactly one
// eager deopt. No call follows the deopt, so the method is never recompiled in
// this process; the exact `COMPILE_COUNT`/`EXEC_COUNT`/`DEOPT_COUNT` counters
// are kept beside the required lower bounds as additional bounded evidence
// (V2).

//! PARAMS --compiler-jit-hotness-threshold=60000

//! METHOD bitwiseUnsignedBoundary
//! HAS I32BitwiseBinary
//! HAS CheckedNonNegativeI32ToTaggedInt
//! COMPILE_COUNT_GE *bitwiseUnsignedBoundary 1
//! EXEC_COUNT_GE *bitwiseUnsignedBoundary 1
//! DEOPT_COUNT_GE *Deoptimize* 1
//! COMPILE_COUNT *bitwiseUnsignedBoundary 1
//! EXEC_COUNT *bitwiseUnsignedBoundary 2
//! DEOPT_COUNT *Deoptimize* 1

function bitwiseUnsignedBoundary(value, shift) {
    return value >>> shift;
}

// Non-constant operands with a logical-right-shift result that stays in the
// non-negative int32 range: the tagging guard is not taken.
for (let i = 0; i < 2000; i++) {
    bitwiseUnsignedBoundary(2147483647 - i * 7, i % 31);
}

ArkTools.arkSteedCompileSync(bitwiseUnsignedBoundary);

// Entry into the compiled method on a safe input: the raw result 0x7fffffff is
// tagged without a deopt.
print(bitwiseUnsignedBoundary(2147483647, 0));

// Representation transition: (-1) >>> 0 is 0xffffffff raw, the tagging guard
// fails, and eager recovery yields the specified Number 4294967295.
print(bitwiseUnsignedBoundary(-1, 0));
print("OK");
