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

// Dead arithmetic coverage combines graph-builder skipping with DCE. Once the
// operands are known numeric, unused consumers can be skipped before IR creation.
// The first checked addition still establishes the numeric facts, while its tagged
// result survives only when returned. Compile/execute and no-deopt witnesses keep
// the final-IR checks tied to optimized execution; the interpreter twin retains
// the independent expected-output oracle.

//! PARAMS --compiler-jit-hotness-threshold=60000

// Dead value: the checked addition is built from the discarded expression
// statement, its guard is `Required`, and its tagged result is dead.
//! METHOD dce_dead_value
//! HAS I32AddWithOverflow
//! HAS_NOT I32ToTaggedInt
//! COMPILE_COUNT dce_dead_value 1
//! EXEC_COUNT_GE *dce_dead_value* 1

// The numeric dead multiplication is skipped during graph building, leaving
// the checked addition but no tagged arithmetic result.
//! METHOD dce_dead_chain
//! HAS I32AddWithOverflow
//! HAS_NOT I32MulWithOverflow
//! HAS_NOT I32ToTaggedInt
//! COMPILE_COUNT dce_dead_chain 1
//! EXEC_COUNT_GE *dce_dead_chain* 1

// The dead numeric subtraction is skipped; the returned sum keeps its tagged result.
//! METHOD dce_shared_live_operand
//! HAS I32AddWithOverflow
//! HAS_NOT I32SubWithOverflow
//! COUNT I32ToTaggedInt 1
//! COMPILE_COUNT dce_shared_live_operand 1
//! EXEC_COUNT_GE *dce_shared_live_operand* 1

// Skipping the dead multiplication also removes its frame-state use of the
// producer's tagged result; only the checked addition remains.
//! METHOD dce_exposed_dead_producer
//! HAS I32AddWithOverflow
//! HAS_NOT I32MulWithOverflow
//! HAS_NOT I32ToTaggedInt
//! COMPILE_COUNT dce_exposed_dead_producer 1
//! EXEC_COUNT_GE *dce_exposed_dead_producer* 1

//! DEOPT_COUNT * 0

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

// Deterministic int warm-up below the hotness threshold: it installs int32
// feedback on every Add2/Sub2/Mul2 site so the compiled graph uses the checked
// int32 arithmetic rather than the generic runtime call.
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
