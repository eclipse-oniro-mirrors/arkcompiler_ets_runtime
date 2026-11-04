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
 * Exception branches of the arksteed SUPERCALLTHISRANGE guard chain, mirroring
 * the interpreter's SlowRuntimeStub::SuperCall TypeErrors. D swaps its own
 * [[Prototype]] right before super(), so each value makes one specific guard
 * fail inside the compiled frame and the OptSuperCall fallback must raise the
 * interpreter-identical TypeError:
 *   - null prototype            -> superCtor is not a heap object
 *   - plain object prototype    -> superCtor is not a JSFunction
 *   - arrow function prototype  -> superCtor is a JSFunction but not a constructor
 * (A non-object non-null prototype cannot occur: Object.setPrototypeOf itself
 * rejects it before the guard chain is reached, so that guard branch is only a
 * defensive backstop.)
 */

//! METHOD D
//! HAS         FastSuperAllocateThis
//! HAS         CallBaseConstructor
//! HAS         CallDerivedConstructor
//! COUNT       CallRuntime OptSuperCall 1

class A {
    constructor() {
    }
}

let C = A;

class D extends A {
    constructor() {
        Object.setPrototypeOf(D, C);
        super();
    }
}

ArkTools.arkSteedCompileSync(D);

new D();  // fast warm-up on a valid super constructor

function attempt(label) {
    try {
        new D();
        return label + ": NO THROW (bug)";
    } catch (e) {
        return label + ": " + (e instanceof TypeError) + " | " + e.message;
    }
}

C = null;
print(attempt("null-proto"));

C = {};
print(attempt("object-proto"));

C = () => 1;
print(attempt("arrow-proto"));
