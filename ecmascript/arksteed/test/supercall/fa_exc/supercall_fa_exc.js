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
 * Exception propagation through the FORWARDALLARGS structure. A throwing
 * base constructor surfaces as a pending exception on both callee shapes:
 * the compiled pair entered through the steed-call convention (first call -
 * cold-call safepoint - and post-warm call) and the compiled default
 * constructor over an uncompiled base, whose exception returns through the
 * generic [[Construct]] trampoline. Every catch must leave the frame healthy
 * for the constructs that follow it.
 */

//! METHOD WarmF
//! HAS         AllocateInYoung
//! HAS         CallBaseConstructorForwardAllArgs
//! COUNT       CallRuntime OptSuperCall* 1

//! METHOD ColdF
//! HAS         AllocateInYoung
//! HAS         CallBaseConstructorForwardAllArgs
//! COUNT       CallRuntime OptSuperCall* 1

class ThrowBase {
    constructor(x) {
        if (x < 0) {
            throw new Error("neg");
        }
        this.x = x;
    }
}

class WarmF extends ThrowBase {
}

class PlainBase {
    constructor(x) {
        if (x < 0) {
            throw new Error("neg2");
        }
        this.x = x;
    }
}

class ColdF extends PlainBase {
}

ArkTools.arkSteedCompileSync(ThrowBase);
ArkTools.arkSteedCompileSync(WarmF);
ArkTools.arkSteedCompileSync(ColdF);

try {
    new WarmF(-1);
    print("first: no throw");
} catch (e) {
    print("first: " + (e instanceof Error) + " " + e.message);
}
print("first-after: " + new WarmF(5).x);

for (let i = 0; i < 20000; i++) {
    new WarmF(i);
}
try {
    new WarmF(-2);
    print("warm: no throw");
} catch (e) {
    print("warm: " + (e instanceof Error) + " " + e.message);
}
print("warm-after: " + new WarmF(6).x);

try {
    new ColdF(-1);
    print("generic: no throw");
} catch (e) {
    print("generic: " + (e instanceof Error) + " " + e.message);
}
print("generic-after: " + new ColdF(7).x);
