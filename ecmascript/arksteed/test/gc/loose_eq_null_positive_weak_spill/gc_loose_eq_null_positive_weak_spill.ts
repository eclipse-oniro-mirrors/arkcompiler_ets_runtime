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

// Companion of gc/loose_eq_null_weak_spill for the positive equality
// polarity: `x == null` lowers to the same `(x | 1) === VALUE_NULL` chain
// and produced the same RawI64ToTagged weak-reencoding before the fix (the
// negative polarity merely wrapped the result in TaggedNotEqual). The
// checks sit at the top of a for-loop body; their `x | 1` values are CSE'd
// into single definitions that cross the in-loop allocation call on every
// backedge, and the pressure objects read after the call force them into
// tagged spill slots. The two-argument String.fromCharCode always allocates
// through ObjectFactory, and --enable-force-gc turns each allocation into a
// young+old GC that walks this frame while the checks are live. With the
// fix the comparison stays in the int64 domain (I64ConditionCheck) and only
// legitimate tagged values ever reach a tagged slot.
//
//! PARAMS --enable-force-gc=true

// @ts-nocheck
declare function print(value: any): void;

class Box {
    v: number = 3;
}

//! METHOD probeNullEqual
//! COMPILE_COUNT_GE *probeNullEqual* 1
//! EXEC_COUNT_GE *probeNullEqual* 1
function probeNullEqual(value: object | null | undefined): boolean {
    return value == null;
}

//! METHOD looseNullEqFor
//! COMPILE_COUNT_GE *looseNullEqFor* 1
//! EXEC_COUNT_GE *looseNullEqFor* 1
function looseNullEqFor(
    o1: Box | null, o2: Box | null, o3: Box | null,
    o4: Box | null, o5: Box | null, o6: Box | null,
    n: number,
): number {
    const p1 = new Box();
    const p2 = new Box();
    const p3 = new Box();
    const p4 = new Box();
    let acc = 0;
    for (let i = 0; i < n; i++) {
        // Checks first, allocation call after: the six normalized `x | 1`
        // values are live from their first evaluation across every
        // subsequent iteration's call, sharing the crossing with p1..p4.
        if (o1 == null || o2 == null || o3 == null ||
            o4 == null || o5 == null || o6 == null) {
            acc -= 1; // never taken with Box operands
        } else {
            acc += 1;
        }
        acc += String.fromCharCode(48 + (i % 10), 48 + ((i + 5) % 10)).charCodeAt(0);
        acc += p1.v + p2.v + p3.v + p4.v;
    }
    return acc;
}

function main() {
    const o1 = new Box();
    const o2 = new Box();
    const o3 = new Box();
    const o4 = new Box();
    const o5 = new Box();
    const o6 = new Box();
    // Interpreted warmup first: feedback for the loop, the nullish checks,
    // the builtin call and the field loads, then synchronous compilation.
    const warm = looseNullEqFor(o1, o2, o3, o4, o5, o6, 3);
    ArkTools.arkSteedCompileSync(probeNullEqual);
    ArkTools.arkSteedCompileSync(looseNullEqFor);
    // Compiled runs: every in-loop factory allocation now walks this frame
    // with a GC while the normalized checks are still live.
    print(probeNullEqual(null) ? 1 : 0);
    print(probeNullEqual(o1) ? 1 : 0);
    print(warm);
    print(looseNullEqFor(o1, o2, o3, o4, o5, o6, 10));
}

main();
