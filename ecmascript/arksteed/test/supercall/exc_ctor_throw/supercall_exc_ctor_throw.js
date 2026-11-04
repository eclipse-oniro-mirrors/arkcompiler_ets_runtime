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
 * An exception thrown by the super constructor body through the compiled fast
 * path. The CallConstructor vertex invokes the base constructor like a call,
 * so a throw inside it must surface through the compiled derived frame:
 *   - uncaught: the error propagates to the `new` site unchanged
 *   - caught in the derived frame: this stays unbound (touching it throws a
 *     ReferenceError), a retrying super() binds this normally
 *   - caught and re-thrown with a wrapper: the wrapper reaches the caller
 */

//! METHOD D
//! HAS         FastSuperAllocateThis
//! HAS         CallBaseConstructor
//! HAS         CallDerivedConstructor
//! COUNT       CallRuntime OptSuperCall 1

//! METHOD Retry
//! COUNT       CallBaseConstructor 2
//! COUNT       CallDerivedConstructor 2
//! COUNT       FastSuperAllocateThis 2
//! COUNT       CallRuntime OptSuperCall 2

class Boom {
    constructor(shouldThrow) {
        if (shouldThrow) {
            throw new RangeError("base-ctor");
        }
        this.ok = true;
    }
}

class D extends Boom {
    constructor(shouldThrow) {
        super(shouldThrow);
    }
}

class Retry extends Boom {
    constructor(shouldThrow) {
        // locals only: `this` is unbound until a super() succeeds, so the
        // catch path must not touch it
        let firstMsg = null;
        let thisWasUnbound = false;
        try {
            super(true);
            this.unreachable = true;
        } catch (e) {
            firstMsg = e.message;
            thisWasUnbound = (() => {
                try {
                    return this;
                } catch (inner) {
                    return inner instanceof ReferenceError;
                }
            })();
        }
        super(false);
        this.first = firstMsg;
        this.thisUnbound = thisWasUnbound;
    }
}

class Wrap extends Boom {
    constructor() {
        try {
            super(true);
        } catch (e) {
            throw new TypeError("wrapped:" + e.message);
        }
    }
}

ArkTools.arkSteedCompileSync(D);
ArkTools.arkSteedCompileSync(Retry);
ArkTools.arkSteedCompileSync(Wrap);

try {
    new D(true);
    print("uncaught: NO THROW (bug)");
} catch (e) {
    print("uncaught: " + (e instanceof RangeError) + " " + e.message);
}
print("after-throw: " + new D(false).ok);

const r = new Retry(true);
print("retry: " + r.first + " " + r.thisUnbound + " " + r.ok + " " + (r.unreachable === undefined));

try {
    new Wrap();
    print("wrap: NO THROW (bug)");
} catch (e) {
    print("wrap: " + (e instanceof TypeError) + " " + e.message);
}
