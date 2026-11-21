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

// Companion of gc/loose_eq_null_weak_spill against the other nullish root:
// `x != undefined` lowers to the same `(x | 1) === VALUE_NULL` chain
// (undefined|1 === null|1 === VALUE_NULL), so a heap-object operand fed the
// very same RawI64ToTagged weak-reencoding before the fix.
//
// Same mixed-polarity recipe on loop-invariant operands: negative checks
// before the in-loop call compute the weak chain early (CSE anchors), the
// positive checks after the call fuse their branches directly on the weak
// values, and the CSE-extended live ranges cross the factory allocation.
// The two-argument String.fromCharCode always allocates through
// ObjectFactory; with --enable-force-gc every such allocation runs a
// young+old GC while this frame is on the stack. With the fix the
// comparison stays in the int64 domain and never produces a tagged
// intermediate.
//
//! PARAMS --enable-force-gc=true

// @ts-nocheck
declare function print(value: any): void;

class Box {
    v: number = 2;
}

//! METHOD probeUndefinedNotEqual
//! COMPILE_COUNT_GE *probeUndefinedNotEqual* 1
//! EXEC_COUNT_GE *probeUndefinedNotEqual* 1
function probeUndefinedNotEqual(value: object | null | undefined): boolean {
    return value != undefined;
}

//! METHOD looseUndefinedFor
//! COMPILE_COUNT_GE *looseUndefinedFor* 1
//! EXEC_COUNT_GE *looseUndefinedFor* 1
function looseUndefinedFor(
    o1: Box | null | undefined, o2: Box | null | undefined,
    o3: Box | null | undefined,
    n: number,
): number {
    const p1 = new Box();
    const p2 = new Box();
    const p3 = new Box();
    const p4 = new Box();
    const p5 = new Box();
    const p6 = new Box();
    let acc = 0;
    for (let i = 0; i < n; i++) {
        // Negative checks: CSE anchors computing the weak chain `o | 1`.
        if (o1 != undefined && o2 != undefined && o3 != undefined) {
            acc += 1;
        }
        // Factory allocation: with --enable-force-gc this runs a young+old GC
        // while the frame is on the stack.
        acc += String.fromCharCode(48 + (i % 10), 48 + ((i + 7) % 10)).charCodeAt(1);
        // Pressure objects live across the call together with the weak
        // intermediates.
        acc += p1.v + p2.v + p3.v + p4.v + p5.v + p6.v;
        // Positive checks after the call: fused branches consuming the weak
        // values defined by the negative checks above.
        if (o1 == undefined || o2 == undefined || o3 == undefined) {
            acc -= 1; // never taken with Box operands
        } else {
            acc += 2;
        }
    }
    return acc;
}

function main() {
    const o1 = new Box();
    const o2 = new Box();
    const o3 = new Box();
    // Interpreted warmup first: feedback for the loop, the nullish checks,
    // the builtin call and the field loads, then synchronous compilation.
    const warm = looseUndefinedFor(o1, o2, o3, 3);
    ArkTools.arkSteedCompileSync(probeUndefinedNotEqual);
    ArkTools.arkSteedCompileSync(looseUndefinedFor);
    // Compiled runs: every in-loop factory allocation now walks this frame
    // with a GC while the weak intermediates are still live.
    print(probeUndefinedNotEqual(undefined) ? 1 : 0);
    print(probeUndefinedNotEqual(o1) ? 1 : 0);
    print(warm);
    print(looseUndefinedFor(o1, o2, o3, 10));
}

main();
