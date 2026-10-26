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

//! PARAMS --compiler-jit-hotness-threshold=60000

// newobjrange specialization: the target execution entry changes between
// interpreted and ArkSteed-compiled code while the caller stays compiled.
//
// Trigger: 16 warm-ups per caller fill the constructor feedback; the callers
// are compiled first while both targets are still interpreted (JSCallNew raw
// entry). Then SteedTarget is compiled with ArkTools.arkSteedCompileSync, so
// the next call must use the compiled-target raw trampoline with the same
// already allocated receiver.
//
// Expected: both calls construct the same class correctly (fields and
// prototype) through the different execution entries; the caller's receiver is
// the only object consumed by the constructor in both states. The sibling
// target_invalidation case exercises a stale MachineCode after invalidation.

//! METHOD callInterp
//! HAS InitializeEmptyObject
//! HAS CallNewObjRangeBase
//! HAS CallNewObjRangeDerived
//! HAS     CallRuntime AllocateInYoung
//! METHOD callSteed
//! HAS InitializeEmptyObject
//! HAS CallNewObjRangeBase
//! HAS CallNewObjRangeDerived
//! HAS     CallRuntime AllocateInYoung

class InterpTarget {
    z: number;
    constructor(z: number) {
        this.z = z;
    }
}

class SteedTarget {
    x: number;
    constructor(x: number) {
        this.x = x;
    }
}

function callInterp(z) {
    return new InterpTarget(z);
}
function callSteed(x) {
    return new SteedTarget(x);
}

for (let i = 0; i < 16; i++) {
    callInterp(i);
    callSteed(i);
}
ArkTools.arkSteedCompileSync(callInterp);
ArkTools.arkSteedCompileSync(callSteed);

print(callInterp(11).z);
print(ArkTools.arkSteedIsCompiled(InterpTarget));

ArkTools.arkSteedCompileSync(SteedTarget);
print(ArkTools.arkSteedIsCompiled(SteedTarget));

print(callSteed(22).x);
print(callSteed(33).x);
print(Object.getPrototypeOf(callSteed(44)) === SteedTarget.prototype);
