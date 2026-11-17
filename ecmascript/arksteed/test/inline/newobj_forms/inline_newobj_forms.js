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

//! PARAMS --compiler-jit-hotness-threshold=150 --compiler-arksteed-enable-inline=true --compiler-min-inline-call-frequency=0.5 --compiler-max-inline-bytecodes=128 --compiler-arksteed-max-cumulative-inline-bytecode-size=256 --compiler-arksteed-max-constructor-inline-bytecode-size=256 --compiler-arksteed-max-graph-nodes-before-inline=1000 --compiler-arksteed-max-inline-actual-args=17

//! METHOD newMissing
//! HAS_NOT CallNewObjRangeBase
//! HAS_NOT CallNewObjRangeDerived

//! METHOD newWide
//! HAS_NOT CallNewObjRangeBase
//! HAS_NOT CallNewObjRangeDerived

//! METHOD newThrowCatch
//! HAS_NOT CallNewObjRangeBase
//! HAS_NOT CallNewObjRangeDerived

//! METHOD ForwardOuter
//! HAS_NOT CallNewObjRangeDerived
//! HAS_NOT CallNewObjRangeBase
//! COUNT CallRuntime OptSuperCallForwardAllArgs 0

class BaseCtor {
    constructor(value) {
        this.value = value + 1;
    }
}

class ManyCtor {
    constructor(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17) {
        this.total = a1 + a2 + a3 + a4 + a5 + a6 + a7 + a8 + a9 + a10 + a11 + a12 + a13 + a14 + a15 + a16 + a17;
        this.count = 17;
    }
}

const expectedError = new RangeError("expected");
class NewThrow {
    constructor() {
        throw expectedError;
    }
}

class ForwardBase {
}

class ForwardSubscriber extends ForwardBase {
    constructor(subscriber, info) {
        super();
        this.subscribers = new Set();
        if (subscriber) {
            this.subscribers.add(subscriber.id__());
        }
    }
    id__() {
        return 1;
    }
}

class ForwardMiddle extends ForwardSubscriber {
    constructor(subscriber, info) {
        super(subscriber, info);
    }
}

class ForwardValue extends ForwardMiddle {
    constructor(value, subscriber, info) {
        super(subscriber, info);
        this.value = value;
    }
}

class ForwardDefault extends ForwardValue {
}

class ForwardDefault2 extends ForwardDefault {
}

class ForwardOuter extends ForwardBase {
    constructor(value, params) {
        super();
        this.result = new ForwardDefault2(value, this, "message");
    }
    id__() {
        return 2;
    }
}

function newMissing() {
    return new BaseCtor();
}

function newWide() {
    return new ManyCtor(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17);
}

function newThrowCatch() {
    try {
        return new NewThrow();
    } catch (error) {
        return error instanceof RangeError;
    }
}

for (let i = 0; i < 64; i++) {
    newMissing();
    newWide();
    newThrowCatch();
    new ForwardOuter("Hello", {});
}

let compiled = true;
for (const method of [newMissing, newWide, newThrowCatch, ForwardOuter]) {
    compiled = ArkTools.arkSteedCompileSync(method) && compiled;
    ArkTools.waitJitCompileFinish(method);
}

const wide = newWide();
const forward = new ForwardOuter("Hello", {});
print("missing:" + Number.isNaN(newMissing().value));
print("wide:" + wide.count + " " + wide.total);
print("throw:" + newThrowCatch());
print("forward:" + forward.result.subscribers.size + " " + forward.result.value);
print("compiled:" + compiled);
