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

//! METHOD peelNamedStore
//! COUNT JumpLoop 1
//! COUNT LoadTaggedField 1
function peelNamedStore(object, n) {
    let sum = 0;
    for (let i = 0; i < n; ++i) {
        object.changed = i;
        sum += object.fixed;
    }
    return sum;
}

//! METHOD peelAliasedField
//! COUNT JumpLoop 1
function peelAliasedField(a, b, n) {
    let sum = 0;
    for (let i = 0; i < n; ++i) {
        sum += a.changed;
        b.changed = i;
        sum += a.changed;
    }
    return sum;
}

//! METHOD peelKeyedAlias
//! COUNT JumpLoop 1
function peelKeyedAlias(object, key, n) {
    let sum = 0;
    for (let i = 0; i < n; ++i) {
        sum += object[key];
        object.changed = i;
        sum += object[key];
    }
    return sum;
}

const a = {fixed: 7, changed: 10};
const b = {fixed: 7, changed: 10};
for (let i = 0; i < 8; ++i) {
    peelNamedStore(a, 4);
    peelAliasedField(a, b, 4);
    peelAliasedField(a, a, 4);
    peelKeyedAlias(a, "changed", 4);
}
ArkTools.arkSteedCompileSync(peelNamedStore);
ArkTools.arkSteedCompileSync(peelAliasedField);
ArkTools.arkSteedCompileSync(peelKeyedAlias);
for (const n of [0, 1, 2, 5]) {
    print(peelNamedStore(a, n));
    a.changed = 10;
    b.changed = 10;
    print(peelAliasedField(a, b, n));
    a.changed = 10;
    print(peelAliasedField(a, a, n));
    a.changed = 10;
    print(peelKeyedAlias(a, "changed", n));
}
