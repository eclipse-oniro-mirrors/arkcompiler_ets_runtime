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

//! METHOD instance_initializer
//! HAS TransitionHClassWithBarrier
//! COUNT StoreTaggedField 2

// Class instance field initializers emit DEFINEPROPERTYBYNAME on freshly created instances.
// The feedback of such sites can only be transition handlers (property-not-found adds), which
// replay as a guarded hclass transition plus a direct field store.

class Ctor {
    x = 1;
    y = 2;
}

function makeInstance(i) {
    let c = new Ctor();
    c.x = i;
    return c.x + c.y;
}

for (let i = 0; i < 20000; i++) {
    makeInstance(i);
}

ArkTools.arkSteedCompileSync(makeInstance);
ArkTools.waitJitCompileFinish(makeInstance);
ArkTools.waitAllJitCompileFinish();
let ok = ArkTools.isAOTCompiled(makeInstance);

for (let i = 0; i < 512; i++) {
    ok = ok && makeInstance(i) === i + 2;
    if ((i & 63) === 0) {
        ArkTools.gc();
    }
}

print(ok ? "PASS" : "FAIL");
