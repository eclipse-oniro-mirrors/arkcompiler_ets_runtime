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
 * Holey-array admission: BranchIfFastSpreadArray admits holey tagged
 * elements (the elements-kind GENERIC disambiguation via the canonical
 * holey-tagged hclass) because the copy loop maps holes to undefined —
 * sound under the ArrayElementsGuardians admission bit (Array.prototype
 * carries no elements). A hole in a spread array therefore arrives as
 * undefined, matching the spec's iterator semantics for absent indices.
 */

//! METHOD HA
//! HAS         BranchIfFastSpreadArray
//! HAS         CallBaseConstructorSpread
//! HAS         AllocateInYoung
//! COUNT       CallRuntime SuperCallSpreadWithNewTarget 1

class HBase {
    constructor(a, b, c, d) {
        this.a = a;
        this.b = b;
        this.c = c;
        this.d = d;
    }
}

class HA extends HBase {
    constructor(arr) {
        super(...arr);
    }
}

ArkTools.arkSteedCompileSync(HA);

// holey array: [1, <hole>, 3, <hole>] -> [1, undefined, 3, undefined]
const sparse = [1, , 3, ,];
const h = new HA(sparse);
print("holey: " + h.a + " " + (h.b === undefined) + " " + h.c + " " + (h.d === undefined));
print("holey-len: " + sparse.length + " " + (h instanceof HA));

// new Array(n) creates a fully-holey array of length n
const empty3 = new Array(3);
const e = new HA(empty3);
print("empty-holes: " + (e.a === undefined) + " " + (e.b === undefined) + " " +
    (e.c === undefined) + " " + (e.d === undefined));

// assigning beyond the end creates holes in the middle
const gap = [1];
gap[3] = 4;
const g = new HA(gap);
print("gap: " + g.a + " " + (g.b === undefined) + " " + (g.c === undefined) + " " + g.d);
