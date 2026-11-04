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
 * super() whose target is a Proxy. A JSProxy is a heap object but not a
 * JSFunction, so the compiled guard chain always routes it to the OptSuperCall
 * fallback, where [[Construct]] trap semantics apply:
 *   - transparent trap (forwards newTarget): this bound to the trapped result
 *   - trap returning a different object: that object replaces this
 *   - trap throwing: the error propagates out of the compiled frame
 */

//! METHOD P
//! HAS         FastSuperAllocateThis
//! HAS         CallBaseConstructor
//! HAS         CallDerivedConstructor
//! COUNT       CallRuntime OptSuperCall 1

class A {
    constructor(v) {
        this.v = v;
    }
}

let trap = null;
const proxy = new Proxy(A, {
    construct(target, args, newTarget) {
        return trap(target, args, newTarget);
    },
});

class P extends A {
    constructor(v) {
        super(v);
    }
}
Object.setPrototypeOf(P, proxy);

ArkTools.arkSteedCompileSync(P);

trap = (target, args, newTarget) => Reflect.construct(target, args, newTarget);
const t = new P(1);
print("transparent: " + t.v + " " + (t instanceof P) + " " + (t instanceof A));

trap = () => ({ replaced: true });
const r = new P(2);
print("replaced: " + r.replaced + " " + (r instanceof P) + " " + (r instanceof A));

trap = () => {
    throw new RangeError("from-trap");
};
try {
    new P(3);
    print("throw: NO THROW (bug)");
} catch (e) {
    print("throw: " + (e instanceof RangeError) + " " + e.message);
}
