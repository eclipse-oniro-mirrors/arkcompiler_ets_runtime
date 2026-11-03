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

//! METHOD      check_truncate
//! HAS         BranchIfReferenceEqual
//! HAS         DeoptIfHClassMismatch
//! HAS         HasInPrototypeChain
//! HAS_NOT     CallCommonStub OrdinaryHasInstance
//! HAS         CallCommonStub InstanceofJit

class PrewalkTruncL0 {}
class PrewalkTruncL1 extends PrewalkTruncL0 {}
class PrewalkTruncL2 extends PrewalkTruncL1 {}
class PrewalkTruncL3 extends PrewalkTruncL2 {}
class PrewalkTruncL4 extends PrewalkTruncL3 {}
class PrewalkTruncL5 extends PrewalkTruncL4 {}
class PrewalkTruncL6 extends PrewalkTruncL5 {}
class PrewalkTruncL7 extends PrewalkTruncL6 {}
class PrewalkTruncL8 extends PrewalkTruncL7 {}
class PrewalkTruncL9 extends PrewalkTruncL8 {}
class PrewalkTruncPoint extends PrewalkTruncL9 {
    x: number;

    constructor(x: number) {
        super();
        this.x = x;
    }
}

// The LHS chain is 11 hops deep, so the prewalk is truncated at 8 members.
// The chain-walk node starts from the eighth known member instead of replaying
// that prefix, and the hit at depth 10 stays within the 32-hop runtime budget.
function check_truncate(o: PrewalkTruncPoint): number {
    let v = o.x;
    return (o instanceof PrewalkTruncL0) ? v : 0;
}

let sink: number = 0;
for (let i = 0; i < 100; i++) {
    sink += check_truncate(new PrewalkTruncPoint(i));
}

ArkTools.arkSteedCompileSync(check_truncate);

print("deepHit=" + check_truncate(new PrewalkTruncPoint(5)));
print("sink=" + sink);
