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
 * Slow-arm entries of the newTarget admission guard. It runs only on the base
 * arm (after BranchIfBase, behind the identity deopt in the speculation form);
 * its failure edges branch to the deferred OptSuperCall fallback, never deopt.
 * The proto-or-hclass slot holds the prototype object until the first
 * allocation through that newTarget caches the instance hclass, so a site
 * compiled before its first construction runs the slow arm once and
 * self-heals:
 *   - Cold: compiled before any construction; the first new allocates via
 *     OptSuperCall (caching the hclass into the slot), the second takes the
 *     fast arm - both must bind this identically
 *   - NT: a Proxy newTarget from Reflect.construct is a constructor but not a
 *     JSFunction, so the base-arm admission rejects it on every call; the
 *     runtime allocation must honour the newTarget's "prototype" get trap
 *   - NTD: the same proxy newTarget through a derived super - the derived arm
 *     has no admission and forwards it untouched; the chain bottom's base
 *     constructor runs the trap through its own slow path
 * (An undefined newTarget - a derived constructor entered without new - and a
 * non-heap proto-or-hclass slot are defensive backstops: a class constructor
 * cannot be invoked without new, and the slot always holds a heap object.)
 */

//! METHOD Cold
//! HAS         BranchIfNewTargetHClassReady
//! HAS         FastSuperAllocateThis
//! HAS         CallBaseConstructor
//! HAS         CallDerivedConstructor
//! COUNT       CallRuntime OptSuperCall 1

//! METHOD NT
//! HAS         BranchIfNewTargetHClassReady
//! HAS_NOT     BranchIfJSConstructorFunction
//! HAS         FastSuperAllocateThis
//! HAS         CallBaseConstructor
//! HAS         CallDerivedConstructor
//! COUNT       CallRuntime OptSuperCall 1

//! METHOD MidNT
//! HAS         BranchIfNewTargetHClassReady
//! HAS_NOT     BranchIfJSConstructorFunction
//! HAS         FastSuperAllocateThis
//! HAS         CallBaseConstructor
//! HAS         CallDerivedConstructor
//! COUNT       CallRuntime OptSuperCall 1

//! METHOD NTD
//! HAS         BranchIfNewTargetHClassReady
//! HAS_NOT     BranchIfJSConstructorFunction
//! HAS         FastSuperAllocateThis
//! HAS         CallBaseConstructor
//! HAS         CallDerivedConstructor
//! COUNT       CallRuntime OptSuperCall 1

class Base {
    constructor() {
    }
}

class Cold extends Base {
    constructor() {
        super();
        this.tag = "cold";
    }
}

class Marker {
}

const newTargetProxy = new Proxy(function Replaced() {}, {
    get(target, key, receiver) {
        if (key === "prototype") {
            return Marker.prototype;
        }
        return Reflect.get(target, key, receiver);
    },
});

class NT extends Base {
    constructor() {
        super();
        this.tag = "nt";
    }
}

class MidNT extends Base {
    constructor() {
        super();
        this.tag = "midnt";
    }
}

class NTD extends MidNT {
    constructor() {
        super();
        this.extra = true;
    }
}

ArkTools.arkSteedCompileSync(Cold);

const cold1 = new Cold();
const cold2 = new Cold();
print("cold: " + cold1.tag + " " + (cold1 instanceof Cold) + " " + cold2.tag + " " + (cold2 instanceof Cold));

new NT();
ArkTools.arkSteedCompileSync(NT);

const viaProxy = Reflect.construct(NT, [], newTargetProxy);
print("proxy-newtarget: " + viaProxy.tag + " " + (viaProxy instanceof Marker) + " " + (viaProxy instanceof NT));

new NTD();
ArkTools.arkSteedCompileSync(MidNT);
ArkTools.arkSteedCompileSync(NTD);

const viaProxyD = Reflect.construct(NTD, [], newTargetProxy);
print("proxy-newtarget-derived: " + viaProxyD.tag + " " + viaProxyD.extra + " " +
      (viaProxyD instanceof Marker) + " " + (viaProxyD instanceof NTD));
