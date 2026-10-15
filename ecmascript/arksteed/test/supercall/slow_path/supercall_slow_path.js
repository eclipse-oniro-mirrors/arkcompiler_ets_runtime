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
 * Slow path coverage of the arksteed SUPERCALLTHISRANGE lowering. B swaps its
 * own [[Prototype]] right before super(), so every invocation of the
 * already-compiled frame re-evaluates the guard chain against the current
 * value and a failing guard must branch to the deferred OptSuperCall arm
 * (raising the interpreter-identical TypeError) instead of deoptimizing:
 *   - null prototype            -> superCtor is not a heap object
 *   - plain object prototype    -> superCtor is not a JSFunction
 *   - arrow function prototype  -> superCtor is not a constructor
 *   - restoring the constructor -> the same compiled frame returns to the fast arm
 * S additionally catches the failure of the first super() inside the compiled
 * derived frame and retries: the second super() must succeed and bind `this`.
 */

//! METHOD B
//! HAS         FastSuperAllocateThis
//! HAS         CallBaseConstructor
//! HAS         CallDerivedConstructor
//! COUNT       CallRuntime OptSuperCall 1

//! METHOD S
//! HAS         FastSuperAllocateThis
//! COUNT       CallBaseConstructor 2
//! COUNT       CallDerivedConstructor 2
//! COUNT       CallRuntime OptSuperCall 2

class A {
    constructor() {
    }
}

let C = A;

class B extends A {
    constructor() {
        Object.setPrototypeOf(B, C);
        super();
    }
}

ArkTools.arkSteedCompileSync(B);

function attempt(label) {
    try {
        return label + ": " + (new B() instanceof B);
    } catch (e) {
        return label + ": caught " + (e instanceof TypeError);
    }
}

print(attempt("warm-fast"));

C = null;
print(attempt("null-proto"));

C = {};
print(attempt("object-proto"));

C = () => 1;
print(attempt("arrow-proto"));

C = A;
print(attempt("restored"));

// slow-path throw caught inside the compiled derived frame, then a second
// super() on the restored prototype must succeed and bind this
class S extends A {
    constructor(flag) {
        if (flag) {
            Object.setPrototypeOf(S, null);
            try {
                super();
                return;
            } catch (e) {
            }
            Object.setPrototypeOf(S, A);
        }
        super();
    }
}

ArkTools.arkSteedCompileSync(S);

const s = new S(true);
print("in-frame-catch: " + (s instanceof S));
