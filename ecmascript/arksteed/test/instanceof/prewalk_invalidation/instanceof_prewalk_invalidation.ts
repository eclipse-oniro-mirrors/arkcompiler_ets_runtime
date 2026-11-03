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

class PrewalkInvPoint {
    x: number;

    constructor(x: number) {
        this.x = x;
    }
}

class PrewalkInvTarget {}

// The compiled fast path folds the prewalked miss to constant false; rewiring
// the chain after compilation invalidates the stable-proto-chain dependency,
// and the result must follow the new chain.
function check_invalidated(o: PrewalkInvPoint): number {
    let v = o.x;
    return (o instanceof PrewalkInvTarget) ? 1 : v;
}

let sink: number = 0;
for (let i = 0; i < 100; i++) {
    sink += check_invalidated(new PrewalkInvPoint(i));
}

ArkTools.arkSteedCompileSync(check_invalidated);

// Before: instance -> PrewalkInvPoint.prototype -> Object.prototype -> null.
print("before=" + check_invalidated(new PrewalkInvPoint(2)));

// Rewire: PrewalkInvPoint.prototype now points at PrewalkInvTarget.prototype,
// so the instanceof becomes true.
Object.setPrototypeOf(Object.getPrototypeOf(new PrewalkInvPoint(0)), PrewalkInvTarget.prototype);

print("after=" + check_invalidated(new PrewalkInvPoint(3)));
print("sink=" + sink);
