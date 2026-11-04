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
 * super() reached through an arrow function inside the constructor body
 * (SUPERCALLARROWRANGE). The lowering is slow-only: one OptSuperCall whose
 * func comes from the acc (captured out of the lexical environment) and
 * whose newTarget comes from the arrow frame's own param slot. A
 * normally-called arrow receives no newTarget there, and the runtime
 * defaults it to the direct super constructor, so the instance takes the
 * direct super constructor's prototype and the defining class drops out of
 * the chain. The assertions pin this interpreter-matching rule for both
 * roles:
 *   - base role: fields visible, instanceof M false, instanceof A true,
 *     A.prototype methods reachable
 *   - derived role: the plain-super middle propagates the defaulted
 *     newTarget, so the proto lands on Mid - instanceof Mid true,
 *     instanceof Leaf false, Mid.prototype methods reachable
 *   - a swapped-away [[Prototype]] must fail through the runtime fallback
 */

class A {
    constructor(x) {
        this.x = x;
    }
    aMethod() {
        return "am";
    }
}

let baseArrow = null;

class M extends A {
    constructor(x) {
        baseArrow = () => super(x);
        baseArrow();
        this.m = true;
    }
}

class Mid extends A {
    constructor(x) {
        super(x);
        this.m = true;
    }
    midMethod() {
        return "mm";
    }
}

let derivedArrow = null;

class Leaf extends Mid {
    constructor(x) {
        derivedArrow = () => super(x);
        derivedArrow();
        this.l = true;
    }
}

new M(1);
ArkTools.arkSteedCompileSync(baseArrow);
const m = new M(4);
print("arrow-base: " + m.x + " " + m.m + " " + (m instanceof M) + " " + (m instanceof A) + " " + m.aMethod());

new Leaf(2);
ArkTools.arkSteedCompileSync(derivedArrow);
const l = new Leaf(6);
print("arrow-derived: " + l.x + " " + l.m + " " + l.l + " " + (l instanceof Mid) + " " + (l instanceof Leaf) + " " + l.midMethod());

Object.setPrototypeOf(M, null);
try {
    new M(1);
    print("arrow-slow: NO THROW (bug)");
} catch (e) {
    print("arrow-slow: " + (e instanceof TypeError));
}
