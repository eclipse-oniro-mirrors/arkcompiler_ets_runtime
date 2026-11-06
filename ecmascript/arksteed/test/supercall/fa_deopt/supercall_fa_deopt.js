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
 * Lazy deoptimization at the FORWARDALLARGS super call, mirroring
 * spread_deopt: the compiled super target invalidates compiled code from
 * inside its own body (prototype-chain mutation) while the compiled
 * default constructor's frame is below it on the stack, so the steed-call
 * return address is patched to the lazy-deopt entry and the continuation
 * must be indistinguishable from the interpreted execution - the derived
 * arm's placeholder-this protocol has to stay invisible.
 */

//! METHOD DP
//! HAS         AllocateInYoung
//! HAS         CallDerivedConstructorForwardAllArgs
//! HAS         ActualArgc
//! COUNT       CallRuntime OptSuperCall* 1

class DepBase {
    constructor(a) {
        this.a = a;
    }
}

let evil = false;

class Dep extends DepBase {
    constructor(a) {
        // the deopt trigger: mutating the prototype chain of a compiled
        // method's owner invalidates its compiled code while it is on-stack
        if (evil) {
            Object.setPrototypeOf(DepProtoHolder, {});
        }
        super(a);
        this.b = a + 1;
    }
}

class DepProtoHolder {
}

class DP extends Dep {
}

const REP = 60000;
for (let i = 0; i < REP; i++) {
    new DP(i);
}

ArkTools.arkSteedCompileSync(Dep);
ArkTools.arkSteedCompileSync(DP);

evil = true;
const r = new DP(41);
print("deopt: " + r.a + " " + r.b + " " + (r instanceof DP));

evil = false;
const r2 = new DP(99);
print("after: " + r2.a + " " + r2.b);
