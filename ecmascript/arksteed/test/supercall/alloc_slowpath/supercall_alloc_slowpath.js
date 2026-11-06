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
 * super() inline this-allocation cold arm under allocation pressure.
 *
 * Trigger: the warm constructions settle the newTarget hclass's slack
 * tracking and GA is compiled; a bounded module-level allocation loop (4096
 * objects) then churns the young space so the compiled CanAllocateInYoung
 * check can fail and route the this-allocation to the AllocateInYoung
 * runtime call, whose result InitializeEmptyObjectWithSize finishes
 * identically to the inline bump (header, undefined tail fill). With
 * `--enable-force-gc=true` the debug runtime collects at allocation hooks
 * during warm-up and pressure; the inline bump allocation has no such hook,
 * so the flag alone does not prove its capacity-miss edge was taken — the
 * churn loop provides the real pressure. JS checks field completeness, the
 * prototype, exactly-once construction and survival of the compiled code.
 */

//! METHOD GA
//! HAS         AllocateInYoung
//! HAS         CanAllocateInYoung
//! HAS         InitializeEmptyObjectWithSize
//! HAS         BranchIfNewTargetHClassReady
//! HAS         CallRuntime AllocateInYoung
//! HAS         CallBaseConstructor

//! PARAMS --enable-force-gc=true

let ctorRuns = 0;

class Base {
    constructor(a, b) {
        ctorRuns++;
        this.a = a;
        this.b = b;
    }
}
class GA extends Base {
    constructor(a, b) {
        super(a, b);
        this.self = this instanceof GA;
    }
}

for (let i = 0; i < 30; i++) {
    new GA(i, i + 1);
}
ArkTools.arkSteedCompileSync(GA);

let sink = 0;
for (let i = 0; i < 4096; i++) {
    const tmp = { v: i, w: i + 1 };
    sink += tmp.v;
}

const before = ctorRuns;
const last = new GA(7, 8);
print("last: " + last.a + " " + last.b + " " + last.self);
print("ctor: " + (ctorRuns - before));
print("proto: " + (Object.getPrototypeOf(last) === GA.prototype));
print("compiled: " + ArkTools.arkSteedIsCompiled(GA));
print("sink: " + (sink > 0));
