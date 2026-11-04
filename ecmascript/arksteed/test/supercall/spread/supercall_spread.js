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
 * Argument-delivery variants of the super call. Spreading a runtime array
 * (super(...args)) and forwarding the caller's arguments
 * (super(...arguments)) have no fast path: the whole site lowers to a
 * single runtime call with no FastSuperAllocateThis / CallConstructor. The
 * direct form with 17 arguments selects the WIDE_SUPERCALLTHISRANGE opcode
 * and lowers through the ordinary fast/slow structure:
 *   - spread / forward: argument contents reach the rest-parameter base
 *   - wide: all 17 arguments arrive in order (sum 153)
 */

//! METHOD Sp
//! HAS_NOT     FastSuperAllocateThis
//! HAS_NOT     CallBaseConstructor
//! HAS_NOT     CallDerivedConstructor

//! METHOD Fwd
//! HAS_NOT     FastSuperAllocateThis
//! HAS_NOT     CallBaseConstructor
//! HAS_NOT     CallDerivedConstructor

//! METHOD Many
//! HAS         FastSuperAllocateThis
//! HAS         CallBaseConstructor
//! HAS         CallDerivedConstructor
//! COUNT       CallRuntime OptSuperCall 1

class Sum {
    constructor(...xs) {
        this.sum = xs.reduce((a, b) => a + b, 0);
        this.n = xs.length;
    }
}

class Sp extends Sum {
    constructor(args) {
        super(...args);
    }
}

class Fwd extends Sum {
    constructor() {
        super(...arguments);
    }
}

class Many extends Sum {
    constructor() {
        super(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17);
    }
}

ArkTools.arkSteedCompileSync(Sp);
ArkTools.arkSteedCompileSync(Fwd);
ArkTools.arkSteedCompileSync(Many);

print("spread: " + new Sp([1, 2, 3]).sum);
print("forward: " + new Fwd(4, 5).sum);
const many = new Many();
print("wide: " + many.n + " " + many.sum);
