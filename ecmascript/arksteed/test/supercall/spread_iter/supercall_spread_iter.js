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
 * Array-admission failure entries of the spread fast path. The
 * BranchIfFastSpreadArray admission only admits stable tagged JSArrays
 * under a clean Array.prototype context; a Proxy or Map spread operand
 * (or a polluted @@iterator) routes to the SuperCallSpread runtime slow
 * block, which runs the full iterator protocol — the trap values must
 * arrive as arguments and the proxy's "prototype" get trap must shape
 * the result via the runtime allocation.
 */

//! METHOD PA
//! HAS         BranchIfFastSpreadArray
//! HAS         CallBaseConstructorSpread
//! HAS         AllocateInYoung
//! COUNT       CallRuntime SuperCallSpreadWithNewTarget 1

//! METHOD MA
//! HAS         BranchIfFastSpreadArray
//! HAS         CallBaseConstructorSpread
//! COUNT       CallRuntime SuperCallSpreadWithNewTarget 1

class Sump {
    constructor(a, b, c) {
        this.a = a;
        this.b = b;
        this.c = c;
    }
}

class PA extends Sump {
    constructor(spreadable) {
        super(...spreadable);
    }
}

class MA extends Sump {
    constructor(spreadable) {
        super(...spreadable);
    }
}

ArkTools.arkSteedCompileSync(PA);
ArkTools.arkSteedCompileSync(MA);

// spread of a Proxy over an array: the admission rejects it (not a JSArray),
// the slow block runs the iterator protocol, the trap observes each next()
const handler = {
    get(target, key, receiver) {
        return Reflect.get(target, key, receiver);
    },
};
const proxied = new Proxy([1, 2, 3], handler);
const p = new PA(proxied);
print("proxy-spread: " + p.a + " " + p.b + " " + p.c);

// spread of a Map (iterable of entries): slow block, entries arrive as [k, v] pairs
const m = new Map([["k1", 10], ["k2", 20]]);
const r = new MA(m);
print("map-spread: " + r.a[0] + "=" + r.a[1] + " " + r.b[0] + "=" + r.b[1] + " " + (r.c === undefined));

// custom iterable with a generator
function* gen() {
    yield "x";
    yield "y";
}
const g = new MA(gen());
print("gen-spread: " + g.a + " " + g.b + " " + (g.c === undefined));

// polluted @@iterator on Array.prototype: the admission's detector bit fails,
// the custom iterator runs through the slow block
const savedIterator = Array.prototype[Symbol.iterator];
Array.prototype[Symbol.iterator] = function* () {
    yield "custom";
    yield "items";
};
try {
    const c = new MA([1, 2, 3]);
    print("polluted: " + c.a + " " + c.b + " " + (c.c === undefined));
} finally {
    Array.prototype[Symbol.iterator] = savedIterator;
}

// restored @@iterator: the fast path resumes
const back = new MA([7, 8, 9]);
print("restored: " + back.a + " " + back.b + " " + back.c);
