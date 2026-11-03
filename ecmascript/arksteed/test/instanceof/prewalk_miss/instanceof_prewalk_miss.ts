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

//! METHOD      check_miss
//! HAS         BranchIfReferenceEqual
//! HAS         DeoptIfHClassMismatch
//! HAS_NOT     HasInPrototypeChain
//! HAS_NOT     CallCommonStub OrdinaryHasInstance
//! HAS         CallCommonStub InstanceofJit

class PrewalkMissBase {
    x: number;

    constructor(x: number) {
        this.x = x;
    }
}

class PrewalkMissOther extends PrewalkMissBase {}

class PrewalkMissUnrelated {
    x: number;

    constructor(x: number) {
        this.x = x;
    }
}

// The LHS chain (other.prototype -> base.prototype -> null) is null-terminated
// within the prewalk depth, so a full miss folds to constant false without any
// runtime chain walk.
function check_miss(o: PrewalkMissOther): number {
    let v = o.x;
    return (o instanceof PrewalkMissUnrelated) ? 1 : v;
}

let sink: number = 0;
for (let i = 0; i < 100; i++) {
    sink += check_miss(new PrewalkMissOther(i));
}

ArkTools.arkSteedCompileSync(check_miss);

print("miss=" + check_miss(new PrewalkMissOther(3)));
print("sink=" + sink);
