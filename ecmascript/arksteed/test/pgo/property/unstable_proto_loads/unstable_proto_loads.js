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
//! METHOD readPrototype
//! HAS_NOT CallCommonStub GetPropertyByName
//! METHOD readUnstableHolder
//! HAS_NOT CallCommonStub GetPropertyByName

function check(ok, message) { if (!ok) { throw new Error(message); } }
function compile(f) {
    ArkTools.arkSteedCompileSync(f);
    ArkTools.waitJitCompileFinish(f);
    check(ArkTools.arkSteedIsCompiled(f), "compile " + f.name);
}
const proto = { value: 7 };
function make() { const o = Object.create(proto); o.tag = 1; return o; }
const a = make(), sibling = make();
sibling.extra = 1;
check(!ArkTools.isStableHClass(a), "unstable receiver");
function readPrototype(o) { return o.value; }
for (let i = 0; i < 200; i++) { readPrototype(a); }
compile(readPrototype);
check(readPrototype(a) === 7, "prototype field");
proto.value = 11;
check(readPrototype(a) === 11, "prototype value update");
check(ArkTools.arkSteedIsCompiled(readPrototype), "prototype mutation starts compiled");
Object.defineProperty(proto, "value", { get() { return 17; }, configurable: true });
check(readPrototype(a) === 17, "prototype accessor change");
Object.setPrototypeOf(a, { value: 19 });
check(readPrototype(a) === 19, "receiver prototype change");
print("PASS prototype-value/accessor/chain-change");

const root = { value: 23 };
const middle = Object.create(root);
function makeDeep() { const o = Object.create(middle); o.tag = 1; return o; }
const deep = makeDeep(), deepSibling = makeDeep();
deepSibling.extra = 1;
// An intermediate dictionary prototype can shadow the original holder.
middle.temporary = 1;
delete middle.temporary;
check(!ArkTools.isStableHClass(deep) && !ArkTools.hasFastProperties(middle), "dictionary prototype");
function readDictionaryPrototype(o) { return o.value; }
for (let i = 0; i < 200; i++) { readDictionaryPrototype(deep); }
compile(readDictionaryPrototype);
check(readDictionaryPrototype(deep) === 23, "dictionary prototype fallback");
middle.value = 29;
check(readDictionaryPrototype(deep) === 29, "intermediate shadowing");
delete middle.value;
check(readDictionaryPrototype(deep) === 23, "shadow removal");
delete root.value;
check(readDictionaryPrototype(deep) === undefined, "holder deletion");
print("PASS unstable-prototype/shadow/delete");

// Array prototypes share their initial prototype HClass. Unlike a dictionary
// conversion, the sibling transition makes the holder non-leaf but keeps fast properties.
const arrayHolder = [], arrayHolderSibling = [];
Object.create(arrayHolder);
Object.create(arrayHolderSibling);
arrayHolder.value = 31;
arrayHolderSibling.value = 37;
check(ArkTools.haveSameMap(arrayHolder, arrayHolderSibling), "shared prototype HClass");
arrayHolderSibling.extra = 1;
check(!ArkTools.isStableHClass(arrayHolder) && ArkTools.hasFastProperties(arrayHolder),
      "non-dictionary unstable holder");
const inherited = Object.create(arrayHolder), inheritedSibling = Object.create(arrayHolder);
inherited.tag = 1;
inheritedSibling.tag = 1;
inheritedSibling.extra = 1;
function readUnstableHolder(o) { return o.value; }
for (let i = 0; i < 200; i++) { readUnstableHolder(inherited); }
compile(readUnstableHolder);
check(readUnstableHolder(inherited) === 31, "checked prototype holder load");
arrayHolder.value = 41;
check(readUnstableHolder(inherited) === 41, "unstable holder fresh value");
check(ArkTools.arkSteedIsCompiled(readUnstableHolder), "holder mutation starts compiled");
Object.defineProperty(arrayHolder, "value", { get() { return 43; }, configurable: true });
check(readUnstableHolder(inherited) === 43, "holder accessor change");
print("PASS non-dictionary-unstable-holder");
