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

//! PARAMS --compiler-jit-hotness-threshold=60000      # Keep arkSteedCompileSync the only compile

//! METHOD      instanceof_inline_check
//! HAS         BranchIfReferenceEqual
//! HAS         HasInPrototypeChain
//! HAS         CallCommonStub InstanceofJit

class InstanceofInlinePoint {
    x: number;

    constructor(x: number) {
        this.x = x;
    }
}

function instanceof_inline_check(o: Object): number {
    return o instanceof InstanceofInlinePoint ? 1 : 0;
}

// Warm up first so the instanceof IC slot records the target hclass before
// compilation; the optimized code then inlines the prototype-chain walk.
let sink: number = 0;
for (let i = 0; i < 100; i++) {
    sink += instanceof_inline_check(new InstanceofInlinePoint(i));
}

ArkTools.arkSteedCompileSync(instanceof_inline_check);

print("hit=" + instanceof_inline_check(new InstanceofInlinePoint(1)));
print("miss=" + instanceof_inline_check(new Object()));
print("primitive=" + instanceof_inline_check(5 as Object));
print("protoSelf=" + (InstanceofInlinePoint.prototype instanceof InstanceofInlinePoint ? 1 : 0));
print("sink=" + sink);
