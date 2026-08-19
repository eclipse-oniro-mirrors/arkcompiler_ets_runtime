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

//! METHOD identityConstruct
//! HAS BranchIfECMAObject
//! COUNT CallCommonStub ArkSteedNewObjectFromPrimitive 1
//! HAS_NOT CallKnownBuiltin
//! HAS_NOT InitializeEmptyObject
//! HAS_NOT CallNewObjRangeBase
//! HAS_NOT CallNewObjRangeDerived

function check(value, message) {
    if (!value) throw new Error(message);
}
let effects = 0;
function identityConstruct(value) {
    return new (Object as any)(value, ++effects);
}
const obj = { v: 1 };
for (let i = 0; i < 32; i++) identityConstruct(obj);
ArkTools.arkSteedCompileSync(identityConstruct);
const revoked = Proxy.revocable({}, {});
revoked.revoke();
for (const value of [obj, [], function f() {}, new Uint8Array(2), revoked.proxy]) {
    const before = effects;
    check(identityConstruct(value) === value && effects === before + 1, "identity with evaluated args");
}
check(identityConstruct(12).valueOf() === 12, "boxed number");
check(identityConstruct("abc").valueOf() === "abc", "boxed string");
check(typeof identityConstruct(null) === "object", "null creates object");
check(typeof identityConstruct(undefined) === "object", "undefined creates object");
check(ArkTools.arkSteedIsCompiled(identityConstruct), "canonical target retained");
print("object_identity passed");
