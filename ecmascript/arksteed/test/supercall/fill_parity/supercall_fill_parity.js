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

/*
 * super() inline-allocation shape parity across the fill ladder.
 *
 * es2abc attaches _ESExpectedPropertyCountAnnotation to constructor methods
 * that store fields (Base below carries 0x2), and CalcuExpotedOfProperties
 * sums the counts along the constructor's [[Prototype]] chain, so GA's
 * derived hclass (0 + 2 + DEFAULT_CAPACITY) starts slack tracking: the
 * constructions inside the window fill the slack tail with the free-object
 * hclass and run the counter protocol, the settled ones fill undefined —
 * one inlined ladder, the interpreter's. A name some constructor calls
 * store and others skip (c here) is never an own property of the skipping
 * instance, whatever the settled size reserves: an unnamed slot is absent
 * from name lookup, so the read resolves through the prototype chain and
 * the fill value of unstored inline slots stays name-invisible. This pins
 * that observable shape end to end: identical keys, own-property verdicts
 * and reads across the tracking and settled constructions.
 */

//! METHOD GA
//! HAS         AllocateInYoung
//! HAS         CanAllocateInYoung
//! HAS         GetObjectSizeFromHClass
//! HAS         InitializeEmptyObjectWithSize
//! HAS         BranchIfNewTargetHClassReady
//! HAS         CallBaseConstructor
//! HAS_NOT     CallCommonStub FastSuperAllocateThis

class Base {
    constructor() {
        this.a = 1;
        this.b = 2;
    }
}
class GA extends Base {
    constructor(store) {
        super();
        if (store) {
            this.c = 3;
        }
    }
}
GA.prototype.c = 'PC';

ArkTools.arkSteedCompileSync(GA);

for (let i = 0; i < 50; i++) {
    new GA(true);
}

const warm = new GA(true);
const skip = new GA(false);
print("warm.keys=" + Object.keys(warm));
print("skip.keys=" + Object.keys(skip));
print("skip.hasC=" + skip.hasOwnProperty('c'));
print("c=" + skip.c);
