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

//! PARAMS --gc-long-paused-time=0 --compiler-jit-hotness-threshold=60000

// G4.3j: the write-barrier value-kind pass rewrites the retained stores of this
// leaf and skips the barrier for a known NonHeap value. The address bounds are
// per target method, and the compile/entry counters are method-specific and
// exact (`COMPILE_COUNT`/`EXEC_COUNT`, not lower bounds), because the
// construction fixes their multiplicity: the hotness threshold 60000 prevents
// premature compilation, each target method is installed by exactly one
// synchronous `ArkTools.arkSteedCompileSync`, and the post-install calls are
// fixed at one per method. Each store is a rewritten `StoreTaggedField` without
// `StoreTaggedFieldWithBarrier`/`SetValueWithBarrier`, each of the four target
// methods (`storeSmi`, `storeRoot`, `storePhiSmi`, `storeLoopPhiSmi`, the last
// one retaining a tagged Phi of two int32 constants) carries its own barrier
// assertions, is installed and entered in machine code exactly once, the normal
// path does not deopt, and the bounded collection window (exactly two forced
// full collections after the compiled stores, with `--gc-long-paused-time=0`
// making every collection report a `gc`-component headline record) keeps the
// stored values intact.
//! METHOD storeSmi
//! HAS StoreTaggedField
//! HAS_NOT StoreTaggedFieldWithBarrier
//! HAS_NOT SetValueWithBarrier
//! METHOD storeRoot
//! HAS StoreTaggedField
//! HAS_NOT StoreTaggedFieldWithBarrier
//! HAS_NOT SetValueWithBarrier
//! METHOD storePhiSmi
//! HAS StoreTaggedField
//! HAS_NOT StoreTaggedFieldWithBarrier
//! HAS_NOT SetValueWithBarrier
//! METHOD storeLoopPhiSmi
//! HAS StoreTaggedField
//! HAS_NOT StoreTaggedFieldWithBarrier
//! HAS_NOT SetValueWithBarrier
//! COMPILE_COUNT storeSmi 1
//! EXEC_COUNT *storeSmi 1
//! COMPILE_COUNT storeRoot 1
//! EXEC_COUNT *storeRoot 1
//! COMPILE_COUNT storePhiSmi 1
//! EXEC_COUNT *storePhiSmi 1
//! COMPILE_COUNT storeLoopPhiSmi 1
//! EXEC_COUNT *storeLoopPhiSmi 1
//! DEOPT_COUNT *Deoptimize* 0
//! GC_COUNT_GE *CompressGC*Trigger by JS* 1

class Holder {
    constructor() {
        this.value = 0;
        this.other = null;
    }
}

function storeSmi(holder) {
    holder.value = 123;
}

function storeRoot(holder) {
    holder.other = null;
}

function storePhiSmi(holder, flag) {
    let value = 1;
    if (flag) {
        value = 2;
    }
    holder.value = value;
}

function storeLoopPhiSmi(holder, count) {
    let value = 1;
    for (let i = 0; i < count; i++) {
        holder.value = value;
        value = 2;
    }
    return holder.value;
}

let holder = new Holder();
for (let i = 0; i < 20000; i++) {
    storeSmi(holder);
    storeRoot(holder);
    storePhiSmi(holder, (i & 1) === 0);
    storeLoopPhiSmi(holder, 2);
}

let ok = ArkTools.getICState(storeSmi, 0, 1) === "mono" &&
    ArkTools.getICState(storeRoot, 0, 1) === "mono";
ArkTools.arkSteedCompileSync(storeSmi);
ArkTools.arkSteedCompileSync(storeRoot);
ArkTools.arkSteedCompileSync(storePhiSmi);
ArkTools.arkSteedCompileSync(storeLoopPhiSmi);
ArkTools.waitJitCompileFinish(storeSmi);
ArkTools.waitJitCompileFinish(storeRoot);
ArkTools.waitJitCompileFinish(storePhiSmi);
ArkTools.waitJitCompileFinish(storeLoopPhiSmi);
ok = ok && ArkTools.isAOTCompiled(storeSmi) && ArkTools.isAOTCompiled(storeRoot) &&
    ArkTools.isAOTCompiled(storePhiSmi) && ArkTools.isAOTCompiled(storeLoopPhiSmi);

storeSmi(holder);
storeRoot(holder);
storePhiSmi(holder, true);
ok = ok && storeLoopPhiSmi(holder, 3) === 2;

// Bounded collection window: exactly two forced full collections after the
// compiled stores. Both profiles must report the collections and must keep the
// stored values (the leaf store is an int32, the other one is the root `null`).
ArkTools.forceFullGC();
ArkTools.forceFullGC();

ok = ok && holder.value === 2 && holder.other === null;

print(ok ? "PASS" : "FAIL");
