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
//! METHOD peelEnv
//! COUNT JumpLoop 1
//! HAS StoreEnvSlot
//! COUNT LoadTaggedField 2
//! METHOD peelTypedStore
//! COUNT JumpLoop 1
//! HAS StoreIntTypedArrayElement
//! COUNT LoadTaggedField 1
//! COUNT DeoptIfHClassMismatch 2
//! METHOD peelAliasedTypedStore
//! COUNT JumpLoop 1
//! HAS StoreIntTypedArrayElement

function makeEnv() {
    var fixed = 7;
    var changing = 0;
    function peelEnv(n) {
        changing = 0;
        let sum = 0;
        for (let i = 0; i < n; ++i) {
            changing = i;
            sum += fixed;
        }
        return sum + changing;
    }
    return peelEnv;
}

function peelTypedStore(a, holder, n) {
    let sum = 0;
    for (let i = 0; i < n; ++i) {
        a[i & 3] = i;
        sum += holder.value;
    }
    return sum;
}

function peelAliasedTypedStore(a, b, n) {
    let sum = 0;
    for (let i = 0; i < n; ++i) {
        const before = b[0];
        a[0] = i;
        sum += b[0] - before;
    }
    return sum;
}

const peelEnv = makeEnv();
const a = new Int32Array(4);
const b = new Int32Array(a.buffer);
const holder = {value: 9};
for (let i = 0; i < 8; ++i) {
    peelEnv(4);
    peelTypedStore(a, holder, 4);
    peelAliasedTypedStore(a, b, 4);
}
ArkTools.arkSteedCompileSync(peelEnv);
ArkTools.arkSteedCompileSync(peelTypedStore);
ArkTools.arkSteedCompileSync(peelAliasedTypedStore);
for (let n of [0, 1, 2, 8]) {
    print(peelEnv(n));
    print(peelTypedStore(a, holder, n));
    a[0] = 20;
    print(peelAliasedTypedStore(a, b, n));
}
