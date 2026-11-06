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
 * Argument-count adaptation over the spread fast path: the steed arm's
 * optional-arg undefined fill covers [mandatory+1+length, reserved) so
 * a callee expecting more arguments than the spread provided reads
 * undefined; a callee expecting fewer simply ignores the extras. The
 * reserved-count formula is max(expected, length) + mandatory + padding,
 * matching the non-spread ComputeSteedCallSlotCount contract.
 */

//! METHOD FA
//! HAS         BranchIfFastSpreadArray
//! HAS         CallBaseConstructorSpread
//! HAS         AllocateInYoung
//! COUNT       CallRuntime SuperCallSpreadWithNewTarget 1

//! METHOD FE
//! HAS         BranchIfFastSpreadArray
//! HAS         CallDerivedConstructorSpread
//! COUNT       CallRuntime SuperCallSpreadWithNewTarget 1

// expects 5 formals; the spread arrays provide 0..7
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

class FA extends Wide {
    constructor(arr) {
        super(...arr);
    }
}

// expects 0 formals; receives long spreads — extras observable via arguments
class Narrow {
    constructor() {
        this.argc = arguments.length;
        this.first = arguments[0];
        this.last = arguments[arguments.length - 1];
    }
}

class FE extends Narrow {
    constructor(arr) {
        super(...arr);
    }
}

ArkTools.arkSteedCompileSync(FA);
ArkTools.arkSteedCompileSync(FE);

const f0 = new FA([]);
print("few-0: " + (f0.a === undefined) + " " + (f0.e === undefined) + " " + f0.argc);
const f2 = new FA([1, 2]);
print("few-2: " + f2.a + " " + f2.b + " " + (f2.c === undefined) + " " + (f2.e === undefined) + " " + f2.argc);
const f5 = new FA([1, 2, 3, 4, 5]);
print("exact-5: " + f5.a + " " + f5.e + " " + f5.argc);
const f7 = new FA([1, 2, 3, 4, 5, 6, 7]);
print("more-7: " + f7.a + " " + f7.e + " " + f7.argc);

const e0 = new FE([]);
print("narrow-0: " + e0.argc + " " + (e0.first === undefined));
const e3 = new FE(["p", "q", "r"]);
print("narrow-3: " + e3.argc + " " + e3.first + " " + e3.last);
