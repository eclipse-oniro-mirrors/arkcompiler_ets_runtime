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

// newobjrange specialization: shared allocation slow path / young-space
// exhaustion while the compiled caller prepares the receiver.
//
// Trigger: 16 warm-ups fill the constructor feedback and finish size tracking;
// the caller is compiled. A bounded module-level allocation loop (4096 objects)
// then churns the young space so the next bump-pointer check in the compiled
// CanAllocateInYoung predicate may select the GC-capable AllocateInYoung runtime
// instead of the inline bump allocation. Both paths merge before InitializeEmptyObject.
// `--enable-force-gc=true` collects at debug runtime allocation hooks during
// warm-up and pressure. Inline bump allocation has no such hook, so this flag does not
// prove that its capacity-miss edge was taken. JS checks initialization and
// exactly-once execution; capacity-miss coverage needs a separate machine-code
// breakpoint or controlled execution trace.
//
// Expected: the constructed object is complete and fully initialized (all
// fields, correct prototype) whether the fast or the runtime path executed, the
// constructor body ran exactly once, and the shared allocation nodes are present in
// the dumped graph.

//! METHOD callPressure
//! HAS CanAllocateInYoung
//! HAS AllocateInYoung
//! HAS InitializeEmptyObject
//! HAS CallRuntime AllocateInYoung
//! HAS CallNewObjRangeBase
//! HAS CallNewObjRangeDerived

let ctorRuns = 0;

class PressureCtor {
    a: number;
    b: number;
    c: number;

    constructor(a: number, b: number, c: number) {
        ctorRuns++;
        this.a = a;
        this.b = b;
        this.c = c;
    }
}

function callPressure(x: number) {
    return new PressureCtor(x, x + 1, x + 2);
}

for (let i = 0; i < 16; i++) {
    callPressure(i);
}
ArkTools.arkSteedCompileSync(callPressure);

let sink = 0;
for (let i = 0; i < 4096; i++) {
    const tmp = { v: i, w: i + 1 };
    sink += tmp.v;
}

const before = ctorRuns;
const obj = callPressure(7);
print(obj.a);
print(obj.b);
print(obj.c);
print(Object.getPrototypeOf(obj) === PressureCtor.prototype);
print(ctorRuns - before);
print(sink > 0);
print(ArkTools.arkSteedIsCompiled(callPressure));
