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

//! METHOD newDerived
//! HAS CallNewObjRangeBase
//! HAS CallNewObjRangeDerived
//! HAS_NOT InitializeEmptyObject
//! METHOD dynamicDerived
//! COUNT CallCommonStub NewThisObjectChecked 1
//! COUNT CallNewObjRangeBase 1
//! COUNT CallNewObjRangeDerived 1
//! CODEGEN_COUNT *CallNewObjRange*commonStub = ArkSteedCallConstructorBody* 2
//! HAS CallRuntime OptNewObjRange

function check(value, message) {
    if (!value) throw new Error(message);
}
let runs = 0;
let fields = 0;
let seenTarget;
class Root {
    x;
    constructor(x) {
        runs++;
        seenTarget = new.target;
        this.x = x;
    }
}
class Middle extends Root {
    mark = ++fields;
    constructor(x) {
        super(x + 1);
    }
}
class Leaf extends Middle {
    leaf = 9;
    constructor(x) {
        super(x + 1);
    }
}
function newDerived(x) {
    return new Leaf(x);
}
function dynamicDerived(C, x) {
    return new C(x);
}
for (let i = 0; i < 32; i++) {
    newDerived(i);
    dynamicDerived(i % 2 ? Middle : Leaf, i);
}
ArkTools.arkSteedCompileSync(Root);
ArkTools.arkSteedCompileSync(Middle);
ArkTools.arkSteedCompileSync(Leaf);
ArkTools.arkSteedCompileSync(newDerived);
ArkTools.arkSteedCompileSync(dynamicDerived);
check(
    ArkTools.arkSteedIsCompiled(Root) &&
        ArkTools.arkSteedIsCompiled(Middle) &&
        ArkTools.arkSteedIsCompiled(Leaf) &&
        ArkTools.arkSteedIsCompiled(newDerived),
    "compiled super chain",
);
const before = runs;
const fieldBefore = fields;
const r = newDerived(10);
check(r.x === 12 && r.leaf === 9 && runs === before + 1 && fields === fieldBefore + 1, "super and fields");
check(seenTarget === Leaf && Object.getPrototypeOf(r) === Leaf.prototype, "original newTarget layout");
check(dynamicDerived(Middle, 4).x === 5 && dynamicDerived(Leaf, 4).x === 6, "dynamic derived");
const replacement = { replaced: true };
class NullRoot extends null {
    constructor(x) {
        return replacement;
    }
}
for (let i = 0; i < 20; i++) dynamicDerived(NullRoot, i);
ArkTools.arkSteedCompileSync(NullRoot);
check(dynamicDerived(NullRoot, 1) === replacement, "extends null explicit object");
class ReturnObject extends Root {
    constructor(x) {
        if (x < 0) super(x);
        return replacement;
    }
}
for (let i = 0; i < 20; i++) dynamicDerived(ReturnObject, i);
ArkTools.arkSteedCompileSync(ReturnObject);
check(dynamicDerived(ReturnObject, 1) === replacement, "derived object without super");
class Twice extends Root {
    constructor(x) {
        super(x);
        super(x + 1);
    }
}
class Missing extends Root {
    constructor(x) {
        if (x < 0) super(x);
        return undefined;
    }
}
class Primitive extends Root {
    constructor(x) {
        if (x < 0) super(x);
        return 3;
    }
}
for (const C of [Twice, Missing, Primitive]) {
    for (let i = 0; i < 16; i++) {
        try {
            dynamicDerived(C, i);
        } catch (_) {}
    }
    ArkTools.arkSteedCompileSync(C);
    const oldRuns = runs;
    let caught = false;
    try {
        dynamicDerived(C, 7);
    } catch (e) {
        caught = C === Primitive ? e instanceof TypeError : e instanceof ReferenceError;
    }
    check(caught && runs - oldRuns === (C === Twice ? 2 : 0), "derived abrupt completion");
}
class NewRoot {
    constructor(x) {
        this.x = x * 10;
        seenTarget = new.target;
    }
}
for (let i = 0; i < 16; i++) new NewRoot(i);
Object.setPrototypeOf(Middle, NewRoot);
const changed = newDerived(2);
check(
    changed.x === 40 && seenTarget === Leaf && Object.getPrototypeOf(changed) === Leaf.prototype,
    "parent guard invalidation",
);
print("derived_super passed");
