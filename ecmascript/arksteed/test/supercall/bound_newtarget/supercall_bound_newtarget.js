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
 * A JSBoundFunction as newTarget via Reflect.construct: the
 * BranchIfNewTargetHClassReadyVertex admission guard checks that newTarget
 * is a JSFunction whose proto-or-hclass slot is ready; JS_BOUND_FUNCTION
 * is outside the JS_FUNCTION type range so the admission rejects it and
 * the runtime allocation path runs. The bound function has no `prototype`
 * property, so per spec the this-allocation falls back to Object.prototype:
 * the result is NOT instanceof D but the constructor body still ran.
 */

//! METHOD D
//! HAS         FastSuperAllocateThis
//! HAS         CallBaseConstructor
//! HAS         CallDerivedConstructor
//! COUNT       CallRuntime OptSuperCall 1

class Base {
    constructor() {
        this.tag = "base";
    }
}

class D extends Base {
    constructor() {
        super();
        this.via = "bound-newtarget";
    }
}

new D();
ArkTools.arkSteedCompileSync(D);

const boundNewTarget = D.bind(null);
const r = Reflect.construct(D, [], boundNewTarget);
// bound functions have no .prototype → this-allocation uses Object.prototype
// → r is NOT instanceof D, but the ctor body ran (tag/via are set)
print("bound-newtarget: " + (r instanceof D) + " " + r.tag + " " + r.via);
