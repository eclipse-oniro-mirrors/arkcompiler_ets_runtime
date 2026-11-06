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

/*
 * Graph-level lowering of the base-super this allocation: AllocateSuperThis
 * reuses the shared young-allocation pipeline (hclass from newTarget's
 * proto-or-hclass slot, GetObjectSizeFromHClass decoding the instance size,
 * AllocateInYoung bump, InitializeEmptyObjectWithSize fill) with the
 * interpreter fill ladder inlined — no FastSuperAllocateThis stub arm. The
 * early constructions of a fresh class run under the slack-tracking counter:
 * the free-object fill and the ObjSizeTrackingStep counter protocol, whose
 * retiring arm calls FinishObjSizeTracking to settle the layout; the settled
 * ones fill undefined. Both arms must produce indistinguishable objects:
 * stored fields readable, unstored slack fields name-invisible, prototype
 * chain and instanceof intact through the hclass.
 */

//! METHOD GA
//! HAS         AllocateInYoung
//! HAS         CanAllocateInYoung
//! HAS         GetObjectSizeFromHClass
//! HAS         IsObjSizeTrackingInProgress
//! HAS         InitializeEmptyObjectWithSize
//! HAS         BranchIfNewTargetHClassReady
//! HAS         CallBaseConstructor
//! COUNT       ObjSizeTrackingStep 1
//! HAS_NOT     CallCommonStub FastSuperAllocateThis

class Base {
    constructor(a, b) {
        this.a = a;
        this.b = b;
        this.self = this instanceof GA;
    }
}
class GA extends Base {
    constructor(a, b) {
        super(a, b);
    }
}

ArkTools.arkSteedCompileSync(GA);

// enough constructions to cross the slack-tracking window (if any) into the
// inline arm; every object must be indistinguishable across the arms
let ok = 0;
for (let i = 0; i < 30; i++) {
    const o = new GA(i, i + 1);
    if (o.a === i && o.b === i + 1 && o.self && o instanceof Base && o.c === undefined) {
        ok++;
    }
}
print("alloc: " + ok + "/30");

const last = new GA(7, 8);
print("last: " + last.a + " " + last.b);
