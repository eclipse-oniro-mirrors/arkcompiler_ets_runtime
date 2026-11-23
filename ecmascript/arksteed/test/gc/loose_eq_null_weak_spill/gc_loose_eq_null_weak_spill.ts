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

// ArkSteed lowers a loose comparison against a nullish constant (`x != null`,
// likewise for `undefined`) to `(x | 1) === VALUE_NULL` (VALUE_UNDEFINED=0x2,
// VALUE_NULL=0x3). For a heap-object operand the normalized value
// `x | TAG_WEAK` is a WEAK reference encoding. Before the weak-spill fix the
// lowering retagged it with RawI64ToTagged, so the TAGGED-represented
// intermediate could be spilled into a tagged stack slot by the register
// allocator; every safepoint of an ArkSteed frame treats all FP-relative
// tagged slots as strong GC roots, and the first GC walking such a frame
// tripped the `!value.IsWeak()` assertion in the GC root visitors
// (the original crasher was lodash_test, e.g. isObject's `value != null`
// feeding baseGetTag's fused `value == null` check).
//
// This case reproduces that shape with mixed polarity on the same
// loop-invariant operands: the negative checks before the in-loop call act
// as CSE anchors that compute the weak chain early, and the positive checks
// after the call fuse their branch directly on the weak value, so the
// CSE-extended live ranges of the weak intermediates cross the allocation
// call. The pressure objects read after the call force tagged values into
// spill slots. The two-argument String.fromCharCode always allocates through
// ObjectFactory (the one-argument ASCII form is single-char-table cached
// and never allocates); with --enable-force-gc every such allocation runs a
// young+old GC while this frame is on the stack, so the weak values meet the
// root scanner deterministically. With the fix the comparison stays in the
// int64 domain and only legitimate tagged values ever reach a tagged slot.
//
//! PARAMS --enable-force-gc=true

// @ts-nocheck
declare function print(value: any): void;

class Box {
    v: number = 1;
}

//! METHOD probeNullNotEqual
//! COMPILE_COUNT_GE *probeNullNotEqual* 1
//! EXEC_COUNT_GE *probeNullNotEqual* 1
function probeNullNotEqual(value: object | null | undefined): boolean {
    return value != null;
}

//! METHOD looseNullWhile
//! COMPILE_COUNT_GE *looseNullWhile* 1
//! EXEC_COUNT_GE *looseNullWhile* 1
function looseNullWhile(
    o1: Box | null, o2: Box | null, o3: Box | null,
    n: number,
): number {
    const p1 = new Box();
    const p2 = new Box();
    const p3 = new Box();
    const p4 = new Box();
    const p5 = new Box();
    const p6 = new Box();
    let acc = 0;
    let i = 0;
    while (i < n) {
        // Negative checks: each computes the weak chain `o | 1` early; alone
        // their weak values die immediately (the double-wrapped comparison
        // result is what stays live).
        if (o1 != null && o2 != null && o3 != null) {
            acc += 1;
        }
        // Factory allocation: with --enable-force-gc this runs a young+old GC
        // while the frame is on the stack.
        acc += String.fromCharCode(48 + (i % 10), 48 + ((i + 3) % 10)).charCodeAt(0);
        // Pressure objects live across the call together with the weak
        // intermediates.
        acc += p1.v + p2.v + p3.v + p4.v + p5.v + p6.v;
        // Positive checks after the call: these branches fuse directly on
        // the weak values, and CSE reuses the definitions from the negative
        // checks above, so the weak values were live across the call.
        if (o1 == null || o2 == null || o3 == null) {
            acc -= 1; // never taken with Box operands
        } else {
            acc += 2;
        }
        i++;
    }
    return acc;
}

function main() {
    const o1 = new Box();
    const o2 = new Box();
    const o3 = new Box();
    // Interpreted warmup first: feedback for the loop, the nullish checks,
    // the builtin call and the field loads, then synchronous compilation.
    const warm = looseNullWhile(o1, o2, o3, 3);
    ArkTools.arkSteedCompileSync(probeNullNotEqual);
    ArkTools.arkSteedCompileSync(looseNullWhile);
    // Compiled runs: every in-loop factory allocation now walks this frame
    // with a GC while the weak intermediates are still live.
    print(probeNullNotEqual(null) ? 1 : 0);
    print(probeNullNotEqual(o1) ? 1 : 0);
    print(warm);
    print(looseNullWhile(o1, o2, o3, 12));
}

main();
