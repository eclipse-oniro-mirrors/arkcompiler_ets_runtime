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

/*
 * @tc.name:normal_func_inheritance
 * @tc.desc: sendable inheritance chains through every IC kind enabled by this feature:
 *           proto method load (holder = parent prototype), proto getter, not-found
 *           (NON_EXIST), own/inherited field store (S_FIELD + SSharedBit) and
 *           mixed parent/child receiver sites (mono -> poly).
 * @tc.type: FUNC
 */

// @ts-nocheck
declare function print(str: any): string;

// Two-level sendable inheritance chain.
class Parent {
    pf: number = 0;
    constructor() { "use sendable"; }
    pmethod(): number { return 42; }
    get pget(): number { return 7; }
    set pset(v: number) { this.pf = v; }
}

class Child extends Parent {
    cf: number = 0;
    constructor() { "use sendable"; super(); }
    cmethod(): number { return 43; }
}

// Three-level chain: the method lives two links up the prototype chain.
class GrandChild extends Child {
    gf: number = 0;
    constructor() { "use sendable"; super(); }
}

let parent = new Parent();
let child = new Child();
let grand = new GrandChild();

// AC-I1: proto method load through inheritance - the holder is Parent.prototype,
// the receiver is a Child instance. Distills a proto handler whose cell stays
// Undefined for shared receivers.
function loadInheritedMethod(o) {
    let r;
    for (let i = 0; i < 2000; ++i) { r = o.pmethod; }
    return r;
}
print("child inherited method found: " + (loadInheritedMethod(child) !== undefined));
print("child inherited method ic: " + ArkTools.getICState(loadInheritedMethod, 1, 0));

// AC-I2: the same method is callable and this-binding works through the hit path.
print("inherited method call: " + child.pmethod());

// AC-I3: proto method two links up (GrandChild -> Child -> Parent).
print("grand inherited method call: " + grand.pmethod());

// AC-I4: proto getter through inheritance (accessor on Parent.prototype).
function loadInheritedGetter(o) {
    let r;
    for (let i = 0; i < 2000; ++i) { r = o.pget; }
    return r;
}
print("child inherited getter: " + loadInheritedGetter(child));
print("child inherited getter ic: " + ArkTools.getICState(loadInheritedGetter, 1, 0));

// AC-I5: proto setter through inheritance writes the parent-declared instance field.
for (let i = 0; i < 2000; ++i) { child.pset = 5; }
print("inherited setter field: " + child.pf);

// AC-I6: not-found on a shared instance distills NON_EXIST (sealed chain, stable answer).
function loadMissingOnChild(o) {
    let r;
    for (let i = 0; i < 2000; ++i) { r = o.nosuch; }
    return r;
}
print("child not-found: " + loadMissingOnChild(child));
print("child not-found ic: " + ArkTools.getICState(loadMissingOnChild, 1, 0));

// AC-I7: own field store/load on the child (S_FIELD + SSharedBit hit chain).
function storeChildField(o, v) {
    for (let i = 0; i < 2000; ++i) { o.cf = v; }
    return o.cf;
}
print("child own field store/load: " + storeChildField(child, 11));

// AC-I8: parent-declared field through the child instance (layout inherited from
// Parent's hclass; the child's own hclass mono match covers it).
function storeInheritedField(o, v) {
    for (let i = 0; i < 2000; ++i) { o.pf = v; }
    return o.pf;
}
print("child inherited field via store: " + storeInheritedField(child, 33));

// AC-I9: mixed receivers at one site - Child and Parent have different hclasses,
// the site goes mono -> poly; both answers stay correct.
function loadMixedProto(o) {
    let r;
    for (let i = 0; i < 2000; ++i) { r = o.pmethod; }
    return r;
}
print("mixed site first (parent): " + (loadMixedProto(parent) !== undefined));
print("mixed site second (child): " + (loadMixedProto(child) !== undefined));
print("mixed site ic: " + ArkTools.getICState(loadMixedProto, 1, 0));

// AC-I10: grandchild own field + inherited method in the same loop body - two sites
// live at once, both hit.
let sink = 0;
for (let i = 0; i < 2000; ++i) { grand.gf = i; sink = grand.pmethod(); }
print("grand combined: " + (grand.gf === 1999) + " " + (sink === 42));

ArkTools.arkSteedCompileSync(loadInheritedMethod);
ArkTools.waitJitCompileFinish(loadInheritedMethod);
print(ArkTools.isAOTCompiled(loadInheritedMethod));
ArkTools.arkSteedCompileSync(loadInheritedGetter);
ArkTools.waitJitCompileFinish(loadInheritedGetter);
print(ArkTools.isAOTCompiled(loadInheritedGetter));
ArkTools.arkSteedCompileSync(loadMissingOnChild);
ArkTools.waitJitCompileFinish(loadMissingOnChild);
print(ArkTools.isAOTCompiled(loadMissingOnChild));
ArkTools.arkSteedCompileSync(storeChildField);
ArkTools.waitJitCompileFinish(storeChildField);
print(ArkTools.isAOTCompiled(storeChildField));
ArkTools.arkSteedCompileSync(storeInheritedField);
ArkTools.waitJitCompileFinish(storeInheritedField);
print(ArkTools.isAOTCompiled(storeInheritedField));
ArkTools.arkSteedCompileSync(loadMixedProto);
ArkTools.waitJitCompileFinish(loadMixedProto);
print(ArkTools.isAOTCompiled(loadMixedProto));

print("inherited after compile: " + (loadInheritedMethod(child) !== undefined));
print("method call after compile: " + child.pmethod());
print("getter after compile: " + loadInheritedGetter(child));
print("notfound after compile: " + loadMissingOnChild(child));
print("child field after compile: " + storeChildField(child, 22));
print("inherited field after compile: " + storeInheritedField(child, 44));
print("mixed after compile: " + (loadMixedProto(parent) !== undefined) + " " +
    (loadMixedProto(child) !== undefined));

print("normal func inheritance ic over");
