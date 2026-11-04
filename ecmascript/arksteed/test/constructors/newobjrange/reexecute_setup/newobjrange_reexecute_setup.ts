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

//! PARAMS --compiler-jit-hotness-threshold=60000 --enable-force-gc=true

// The shared allocation runtime may run GC; target and arguments must remain available so
// the constructor still runs exactly once.

//! METHOD callDeadInputs
//! HAS InitializeEmptyObject
//! HAS CallNewObjRangeBase
//! HAS CallNewObjRangeDerived
//! HAS     CallRuntime AllocateInYoung

let ctorRuns = 0;

class DeadInputsCtor {
    v: number;

    constructor(a: number, b: number) {
        ctorRuns++;
        this.v = a + b;
    }
}

function callDeadInputs(a: number, b: number) {
    return new DeadInputsCtor(a, b).v;
}

for (let i = 0; i < 16; i++) {
    callDeadInputs(i, i + 1);
}
ArkTools.arkSteedCompileSync(callDeadInputs);

let sink = 0;
for (let i = 0; i < 4096; i++) {
    const tmp = { x: i };
    sink += tmp.x;
}

const before = ctorRuns;
const v = callDeadInputs(20, 22);
print(v);
print(ctorRuns - before);
print(sink > 0);
print(ArkTools.arkSteedIsCompiled(callDeadInputs));
