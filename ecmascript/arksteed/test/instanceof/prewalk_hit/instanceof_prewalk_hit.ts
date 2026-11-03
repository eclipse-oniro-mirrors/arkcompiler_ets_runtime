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

//! METHOD      check_hit
//! HAS         BranchIfReferenceEqual
//! HAS         DeoptIfHClassMismatch
//! HAS_NOT     HasInPrototypeChain
//! HAS         CallCommonStub InstanceofJit

class PrewalkHitPoint {
    x: number;

    constructor(x: number) {
        this.x = x;
    }
}

class PrewalkHitOther {
    x: number;

    constructor(x: number) {
        this.x = x;
    }
}

// The named load below warms its IC and records the LHS hclass in facts, so the
// compiler prewalks the LHS chain and folds the hit to a constant true result.
function check_hit(o: PrewalkHitPoint): number {
    let v = o.x;
    return (o instanceof PrewalkHitPoint) ? v : 0;
}

let sink: number = 0;
for (let i = 0; i < 100; i++) {
    sink += check_hit(new PrewalkHitPoint(i));
}

ArkTools.arkSteedCompileSync(check_hit);

print("hit=" + check_hit(new PrewalkHitPoint(1)));
print("miss=" + check_hit(new PrewalkHitOther(2) as Object as PrewalkHitPoint));
print("sink=" + sink);
