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

/*
 * Length-grown spread arrays: growing the length property beyond the
 * current backing grows the elements backing to the new length with
 * hole-filled slots (JSArray::SetCapacity -> GrowElementsCapacity;
 * sparse growth converts to dictionary elements and is rejected by the
 * admission instead). A length-grown fast array therefore stays admitted
 * to BranchIfFastSpreadArray, and the copy loop must map the grown holes
 * to undefined — matching the spec's iterator semantics for absent
 * indices — while argc reflects the grown length. The first construction
 * also covers the lazy initial-hclass path: the first newTarget makes the
 * readiness guard fail and the slow runtime arm run; the following calls
 * take the fast path.
 */

//! METHOD GA
//! HAS         BranchIfFastSpreadArray
//! HAS         CallBaseConstructorSpread
//! HAS         AllocateInYoung
//! COUNT       CallRuntime SuperCallSpreadWithNewTarget 1

// expects 5 formals; the arrays below deliver 3, 6 and 9 arguments
class Wide {
    constructor(a, b, c, d, e) {
        this.a = a;
        this.b = b;
        this.c = c;
        this.d = d;
        this.e = e;
        this.argc = arguments.length;
    }
}

class GA extends Wide {
    constructor(arr) {
        super(...arr);
    }
}

ArkTools.arkSteedCompileSync(GA);

// in-capacity warmup: admitted to the fast path
const w = new GA([1, 2, 3]);
print("warm: " + w.a + " " + w.b + " " + w.c + " " + (w.d === undefined) + " " +
    (w.e === undefined) + " " + w.argc);

// length grown beyond the backing (3 slots, length 6): the trailing three
// arguments must be undefined, not backing-store garbage
const grown = [1, 2, 3];
grown.length = 6;
const g = new GA(grown);
print("grow: " + g.a + " " + g.b + " " + g.c + " " + (g.d === undefined) + " " +
    (g.e === undefined) + " " + g.argc);

// grown far beyond the backing (1 slot, length 9)
const far = [7];
far.length = 9;
const f = new GA(far);
print("grow-far: " + f.a + " " + (f.b === undefined) + " " + (f.e === undefined) + " " + f.argc);
