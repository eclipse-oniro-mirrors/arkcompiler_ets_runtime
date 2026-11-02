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

// Interpreter reference of `arithmetic/dce_dead_arithmetic_value`: the same
// computations, inputs and warm-up schedule with every compiled tier disabled, so the
// ArkSteed removal evidence is compared against a successful semantic run of
// the same program. `COMPILE_COUNT * 0` and `EXEC_COUNT * 0` require that no
// ArkSteed compilation or entry marker appears in the run, which is the
// zero-JIT evidence of this reference. Semantic projection: the twin has no
// observation-only lines, so the runner's full filtered stdout comparison is
// the projection and no line is excluded or reordered. The independent oracle
// stays the hand-computed arithmetic semantics (3, 5, 15, 9); the agreement of
// the reference is a necessary but not sufficient check. This is not the
// failing JIT-off sensitivity control, which is reported separately.

//! PARAMS --compiler-enable-jit=false --compiler-enable-baselinejit=false --compiler-enable-osr=false --compiler-enable-framework-aot=false
//! COMPILE_COUNT * 0
//! EXEC_COUNT * 0

// @ts-nocheck
declare function print(value: any): void;

function dce_dead_value(a: number, b: number): number {
    let dead = a + b;
    return a;
}

function dce_dead_chain(a: number, b: number): number {
    let dead0 = a + b;
    let dead1 = dead0 * dead0;
    return a;
}

function dce_shared_live_operand(a: number, b: number): number {
    let shared = a + b;
    let dead = shared - b;
    return shared;
}

function dce_exposed_dead_producer(a: number, b: number): number {
    let producer = a + b;
    let consumer = producer * producer;
    return a;
}

for (let i = 1; i < 2000; i++) {
    dce_dead_value(i, i + 1);
    dce_dead_chain(i, i + 1);
    dce_shared_live_operand(i, i + 1);
    dce_exposed_dead_producer(i, i + 1);
}

ArkTools.arkSteedCompileSync(dce_dead_value);
ArkTools.arkSteedCompileSync(dce_dead_chain);
ArkTools.arkSteedCompileSync(dce_shared_live_operand);
ArkTools.arkSteedCompileSync(dce_exposed_dead_producer);

print(dce_dead_value(3, 4));
print(dce_dead_chain(5, 6));
print(dce_shared_live_operand(7, 8));
print(dce_exposed_dead_producer(9, 10));
