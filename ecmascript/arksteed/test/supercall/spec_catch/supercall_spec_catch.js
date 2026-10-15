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
 * A speculation identity miss deopting into the frame's own never-executed
 * catch: the eager deopt re-executes the supercall in the interpreter, the
 * null [[Prototype]] raises the interpreter-identical TypeError, and the
 * interpreted catch runs. An eager deopt is not an invalidation: the code
 * stays installed.
 */

//! METHOD W
//! HAS_NOT     BranchIfJSConstructorFunction
//! HAS         CallBaseConstructor
//! HAS         CallDerivedConstructor

class A {
}

let C = A;

class W extends A {
    constructor() {
        Object.setPrototypeOf(W, C);
        try {
            super();
        } catch (e) {
            return {caught: e instanceof TypeError};
        }
    }
}

new W();
ArkTools.arkSteedCompileSync(W);

C = null;
const r = new W();
print("spec-catch: " + r.caught + " " + ArkTools.arkSteedIsCompiled(W));
