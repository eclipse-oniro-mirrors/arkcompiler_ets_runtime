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

//! PARAMS --enable-force-gc=true
//! METHOD createRetained
//! COUNT AllocateInYoung 1
//! HAS HeapConstant
//! COUNT InitializeArrayWithBuffer 1
//! HAS_NOT CallCommonStub CreateArrayWithBuffer

function createRetained() {
    return ["retained-template", 17, 2.5];
}

let before = createRetained();
ArkTools.arkSteedCompileSync(createRetained);
ArkTools.waitJitCompileFinish(createRetained);
print("compiled:" + ArkTools.arkSteedIsCompiled(createRetained));

// Collect between compilation and the first execution of the embedded template reference.
ArkTools.forceFullGC();
let first = createRetained();
let survivors = [];
let ok = first !== before && first.join(",") === before.join(",");
for (let i = 0; i < 8192; ++i) {
    let current = createRetained();
    if (current.length !== 3 || current[0] !== "retained-template" || current[1] !== 17 || current[2] !== 2.5) {
        ok = false;
        break;
    }
    if (i % 512 === 0) {
        current[1] = { index: i };
        survivors.push(current);
    }
}
ArkTools.forceFullGC();
for (let i = 0; i < survivors.length; ++i) {
    ok = ok && survivors[i][0] === "retained-template" && survivors[i][1].index === i * 512 &&
        survivors[i][2] === 2.5;
}
// Capacity failure may eagerly deopt; retained values must still survive GC.
print("force-gc:" + (ok && survivors.length === 16));
print("fresh-after-gc:" + (createRetained().join(",") === "retained-template,17,2.5"));
