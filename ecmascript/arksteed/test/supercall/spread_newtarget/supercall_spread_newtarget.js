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
 * newTarget admission on the spread fast path, mirroring
 * newtarget_admission for SUPERCALLTHISRANGE: a Proxy newTarget from
 * Reflect.construct is a constructor but not a JSFunction, so the base
 * arm's BranchIfNewTargetHClassReady guard rejects it on every call —
 * the runtime allocation honours the newTarget's "prototype" get trap.
 */

//! METHOD NTS
//! HAS         BranchIfFastSpreadArray
//! HAS         BranchIfNewTargetHClassReady
//! HAS         AllocateInYoung
//! HAS         CallBaseConstructorSpread
//! HAS         CallDerivedConstructorSpread
//! COUNT       CallRuntime SuperCallSpreadWithNewTarget 1

class NTBase {
    constructor(a) {
        this.a = a;
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

class NTS extends NTBase {
    constructor(arr) {
        super(...arr);
    }
}

ArkTools.arkSteedCompileSync(NTS);

// warm the fast path with a normal construct
const warm = new NTS([1]);
print("warm: " + warm.a);

// proxy newTarget: the base arm's admission guard fails, the runtime
// allocation honours the trap
const viaProxy = Reflect.construct(NTS, [[2]], newTargetProxy);
print("proxy-newtarget: " + viaProxy.a + " " + (viaProxy instanceof Marker) + " " + (viaProxy instanceof NTS));
