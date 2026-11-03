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

//! PARAMS --compiler-jit-hotness-threshold=60000

//! METHOD      check_rebind
//! HAS         BranchIfReferenceEqual
//! HAS         CallCommonStub InstanceofJit

class PrewalkHealA {
    x: number;

    constructor(x: number) {
        this.x = x;
    }
}

class PrewalkHealB {
    x: number;

    constructor(x: number) {
        this.x = x;
    }
}

// The target is a module-level binding: the compiler resolves it through the
// lexical env slot into a constant function guarded by a reference check.
// Reassigning the binding after compilation must fail the guard, deopt, and
// follow the new target's semantics.
let PrewalkHealTarget: Object = PrewalkHealA;

function check_rebind(o: PrewalkHealA): number {
    let v = o.x;
    return (o instanceof PrewalkHealTarget) ? 1 : v;
}

let sink: number = 0;
for (let i = 0; i < 100; i++) {
    sink += check_rebind(new PrewalkHealA(i));
}

ArkTools.arkSteedCompileSync(check_rebind);

print("hitA=" + check_rebind(new PrewalkHealA(4)));

// Rebind the module slot to another class: the reference guard fails, the
// optimized code deopts, and the interpreter evaluates against HealB.
PrewalkHealTarget = PrewalkHealB;
print("afterRebind=" + check_rebind(new PrewalkHealA(6)));
print("reboundHit=" + check_rebind(new PrewalkHealB(8) as Object as PrewalkHealA));
print("sink=" + sink);
