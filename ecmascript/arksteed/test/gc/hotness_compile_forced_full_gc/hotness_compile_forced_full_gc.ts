/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

// A hotness-triggered ArkSteed compilation submits `RuntimeStubs::SubmitJitTask` while the
// asm interpreter's runtime call is on the stack. That pending call reports its arguments
// through two GC root ranges at once: the `InterpretedEntryFrame`'s EcmaRuntimeCallInfo range
// and the `LEAVE_FRAME_WITH_ARGV` argv range. With `--enable-force-gc=true` the compilation's
// own ProfileTypeInfo allocation starts a full GC in the middle of the call, and the same five
// argument slots are scanned twice. The first visit evacuates the target JSFunction and
// rewrites the root slot; the second visit found the fresh copy and evacuated it again. The
// stale old address stayed in the mark stack, and popping it later dereferenced the forwarding
// word as a class pointer, crashing the marker.
//
// The regression is that the forced full GC inside the hotness-triggered compilation survives
// and that the compiled comparator keeps producing the expected values afterwards.
//
//! PARAMS --enable-force-gc=true --compiler-jit-hotness-threshold=1
//! METHOD compareNumbers
//! HAS CheckedTaggedIntToI32
//! HAS I32SubWithOverflow
//! COMPILE_COUNT_GE *compareNumbers* 1
//! EXEC_COUNT_GE *compareNumbers* 1
//! DEOPT_COUNT * 0

// @ts-nocheck
declare function print(value: any): void;

function compareNumbers(a: number, b: number): number {
    return a - b;
}

function sortWithComparator(): number {
    const arr = [4, 3, 2, 1, 4, 3, 2, 1, 4, 3, 2, 1];
    arr.sort(compareNumbers);
    return arr[0];
}

let acc = 0;
for (let i = 0; i < 4; i++) {
    acc += sortWithComparator();
}
print(acc);
