/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * A JSBoundFunction as the super constructor, installed via setPrototypeOf
 * after class definition (extends boundFn throws at definition time because
 * bound functions have no prototype property). The fast path's
 * BranchIfJSConstructorFunction guard checks JSType in
 * [JS_FUNCTION_FIRST, JS_FUNCTION_LAST]; JS_BOUND_FUNCTION falls outside
 * that range, so every call must take the OptSuperCall slow path. The
 * bound function's [[Construct]] delegates to Base with newTarget
 * propagated, so semantics hold.
 */

//! METHOD BS
//! HAS         FastSuperAllocateThis
//! HAS         CallBaseConstructor
//! HAS         CallDerivedConstructor
//! COUNT       CallRuntime OptSuperCall 1

class Base {
    constructor() {
        this.tag = "base";
    }
}

const boundBase = Base.bind(null);

class BS extends Base {
    constructor() {
        super();
        this.via = "bound-super";
    }
}
// redirect the super constructor to the bound function
Object.setPrototypeOf(BS, boundBase);

new BS();
ArkTools.arkSteedCompileSync(BS);

const r = new BS();
print("bound-super: " + (r instanceof BS) + " " + r.tag + " " + r.via);
