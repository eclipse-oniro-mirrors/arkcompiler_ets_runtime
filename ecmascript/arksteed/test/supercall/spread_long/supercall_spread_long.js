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
 * Elements-length cap of the spread fast path: both spread call arms size
 * their machine-stack slot reservation from the elements backing's length
 * with no sp check at the call site (the cross-tier call convention), so the
 * BranchIfFastSpreadArray admission rejects backings longer than
 * MAX_FAST_SPREAD_ELEMENTS (100) to the runtime SuperCallSpreadWithNewTarget
 * path, which materializes argv on the heap under its own stack checks. An
 * array of exactly 100 elements stays on the fast path; one past the cap
 * routes to the runtime while still delivering every argument — a regression
 * that reads the wrong length field, compares against the wrong bound, or
 * drops args on the slow-path handoff breaks the printed argc/first/last.
 */

//! METHOD GA
//! HAS         BranchIfFastSpreadArray
//! HAS         CallBaseConstructorSpread
//! HAS         AllocateInYoung
//! COUNT       CallRuntime SuperCallSpreadWithNewTarget 1

class Base {
    constructor(...rest) {
        this.first = rest[0];
        this.last = rest[rest.length - 1];
        this.argc = rest.length;
    }
}
class GA extends Base {
    constructor(arr) {
        super(...arr);
    }
}

ArkTools.arkSteedCompileSync(GA);

// push growth (2x) lands the backing above the cap, so this rides the runtime
// path as well; the ==100 boundary itself is output-invisible
const at = [];
for (let i = 0; i < 100; i++) {
    at.push(i);
}
const a = new GA(at);
print("at-cap: " + a.first + ".." + a.last + " argc=" + a.argc);

// one past the cap: rejected to the runtime slow path, args delivered whole
const over = [];
for (let i = 0; i < 101; i++) {
    over.push(i);
}
const o = new GA(over);
print("over-cap: " + o.first + ".." + o.last + " argc=" + o.argc);
