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

//! PARAMS --compiler-jit-hotness-threshold=60000

// Non-ECMAObject prototypes must use the generic stub and preserve TypeError.
//! METHOD      checkStringProto
//! HAS         CallCommonStub InstanceofJit
//! HAS_NOT     HasInPrototypeChain
//! METHOD      checkSymbolProto
//! HAS         CallCommonStub InstanceofJit
//! HAS_NOT     HasInPrototypeChain
//! METHOD      checkBigIntProto
//! HAS         CallCommonStub InstanceofJit
//! HAS_NOT     HasInPrototypeChain

// Primitive and proxy inputs exercise the loop node without an LHS prewalk.
//! METHOD      checkPrimitive
//! HAS         HasInPrototypeChain
//! METHOD      checkStringPrewalk
//! HAS         HasInPrototypeChain
//! METHOD      checkProxy
//! HAS         HasInPrototypeChain
//! METHOD      checkDeep
//! HAS         HasInPrototypeChain

function StringProtoCtor(this: any) {}
StringProtoCtor.prototype = "x";
function SymbolProtoCtor(this: any) {}
SymbolProtoCtor.prototype = Symbol("s");
function BigIntProtoCtor(this: any) {}
BigIntProtoCtor.prototype = 1n;

function checkStringProto(o: Object): number {
    try {
        return o instanceof StringProtoCtor ? 1 : 0;
    } catch (e) {
        return 2;
    }
}

function checkSymbolProto(o: Object): number {
    try {
        return o instanceof SymbolProtoCtor ? 1 : 0;
    } catch (e) {
        return 2;
    }
}

function checkBigIntProto(o: Object): number {
    try {
        return o instanceof BigIntProtoCtor ? 1 : 0;
    } catch (e) {
        return 2;
    }
}

class PrimitiveTarget {}
class ProxyTarget {}
class DeepTarget {}

function checkPrimitive(o: Object): number {
    return o instanceof PrimitiveTarget ? 1 : 0;
}

function checkStringPrewalk(s: string): number {
    let first = s[0];
    return s instanceof String ? first.length : 0;
}

function checkBoxedString(o: Object): number {
    return o instanceof String ? 1 : 0;
}

function checkProxy(o: Object): number {
    try {
        return o instanceof ProxyTarget ? 1 : 0;
    } catch (e) {
        return 2;
    }
}

function makeChain(depth: number, prototype: Object): Object {
    let object = Object.create(prototype);
    for (let i = 1; i < depth; i++) {
        object = Object.create(object);
    }
    return object;
}

function checkDeep(o: Object): number {
    return o instanceof DeepTarget ? 1 : 0;
}

for (let i = 0; i < 50; i++) {
    checkStringProto({});
    checkSymbolProto({});
    checkBigIntProto({});
    checkPrimitive(new PrimitiveTarget());
    checkStringPrewalk("x");
    checkBoxedString("x");
    checkProxy(new ProxyTarget());
    checkDeep(makeChain(1, DeepTarget.prototype));
}

ArkTools.arkSteedCompileSync(checkStringProto);
ArkTools.arkSteedCompileSync(checkSymbolProto);
ArkTools.arkSteedCompileSync(checkBigIntProto);
ArkTools.arkSteedCompileSync(checkPrimitive);
ArkTools.arkSteedCompileSync(checkStringPrewalk);
ArkTools.arkSteedCompileSync(checkBoxedString);
ArkTools.arkSteedCompileSync(checkProxy);
ArkTools.arkSteedCompileSync(checkDeep);

print("stringProto=" + checkStringProto({}));
print("symbolProto=" + checkSymbolProto({}));
print("bigIntProto=" + checkBigIntProto({}));
print("stringPrimitive=" + checkPrimitive("x" as Object));
print("symbolPrimitive=" + checkPrimitive(Symbol("s") as Object));
print("bigIntPrimitive=" + checkPrimitive(1n as Object));
print("numberPrimitive=" + checkPrimitive(5 as Object));
print("primitiveHit=" + checkPrimitive(new PrimitiveTarget()));
print("stringPrewalk=" + checkStringPrewalk("x"));
print("boxedString=" + checkBoxedString(new String("x")));

let initialProxyTraps = 0;
const initialProxy = new Proxy({}, {
    getPrototypeOf(): Object {
        initialProxyTraps++;
        return ProxyTarget.prototype;
    }
});
print("initialProxy=" + checkProxy(initialProxy));
print("initialProxyTraps=" + initialProxyTraps);

let hitTraps = 0;
const hitProxy = new Proxy({}, {
    getPrototypeOf(): Object {
        hitTraps++;
        return ProxyTarget.prototype;
    }
});
print("proxyHit=" + checkProxy(Object.create(hitProxy)));
print("proxyHitTraps=" + hitTraps);

let missTraps = 0;
const missProxy = new Proxy({}, {
    getPrototypeOf(): Object {
        missTraps++;
        return {};
    }
});
print("proxyMiss=" + checkProxy(Object.create(missProxy)));
print("proxyMissTraps=" + missTraps);

let throwTraps = 0;
const throwProxy = new Proxy({}, {
    getPrototypeOf(): Object {
        throwTraps++;
        throw new Error("proxy");
    }
});
print("proxyThrow=" + checkProxy(Object.create(throwProxy)));
print("proxyThrowTraps=" + throwTraps);

print("deep8=" + checkDeep(makeChain(8, DeepTarget.prototype)));
print("deep9=" + checkDeep(makeChain(9, DeepTarget.prototype)));
print("deep10=" + checkDeep(makeChain(10, DeepTarget.prototype)));
print("deep32=" + checkDeep(makeChain(32, DeepTarget.prototype)));
print("deep33=" + checkDeep(makeChain(33, DeepTarget.prototype)));
print("deepMiss33=" + checkDeep(makeChain(33, {})));
