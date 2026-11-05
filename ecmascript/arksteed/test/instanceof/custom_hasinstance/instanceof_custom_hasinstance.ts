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

//! METHOD      check_custom
//! HAS         CallCommonStub InstanceofJit

//! METHOD      check_bound
//! HAS         CallCommonStub InstanceofJit

class InstanceofCustomPoint {
    x: number;

    constructor(x: number) {
        this.x = x;
    }
}

class InstanceofCustomBox {}

function check_custom(o: Object): number {
    return o instanceof InstanceofCustomBox ? 1 : 0;
}

function check_bound(o: Object): number {
    return o instanceof boundTarget ? 1 : 0;
}

// User-provided @@hasInstance must run its own call, so the site keeps the generic stub.
Object.defineProperty(InstanceofCustomBox, Symbol.hasInstance, {
    value: function (v: Object): boolean {
        return typeof v === "number";
    }
});

const boundTarget = InstanceofCustomPoint.bind(null);

for (let i = 0; i < 50; i++) {
    check_custom(1 as Object);
    check_bound(new InstanceofCustomPoint(i));
}

ArkTools.arkSteedCompileSync(check_custom);
ArkTools.arkSteedCompileSync(check_bound);

print("customNum=" + check_custom(1 as Object));
print("customObj=" + check_custom(new InstanceofCustomBox()));
print("bound=" + check_bound(new InstanceofCustomPoint(1)));
print("boundMiss=" + check_bound(new Object()));
