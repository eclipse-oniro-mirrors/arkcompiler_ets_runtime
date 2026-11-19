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
//! METHOD readOwn
//! HAS_NOT CallCommonStub GetPropertyByName
//! METHOD readKey
//! HAS_NOT CallCommonStub GetPropertyByValue
//! METHOD readThisKey
//! HAS_NOT CallCommonStub GetPropertyByValue
//! METHOD readPoly
//! HAS_NOT CallCommonStub GetPropertyByName
//! METHOD readWide
//! HAS_NOT CallCommonStub GetPropertyByName

function check(ok, message) {
    if (!ok) {
        throw new Error(message);
    }
}

function compile(f) {
    ArkTools.arkSteedCompileSync(f);
    ArkTools.waitJitCompileFinish(f);
    check(ArkTools.arkSteedIsCompiled(f), "compile " + f.name);
}

function make(value) {
    return { value: value };
}

function makeOther(value) {
    return { pad: 1, value: value };
}

function readOwn(o) { return o.value; }
function readKey(o, key) { return o[key]; }
function readThisKey(key) { return this[key]; }
function readPoly(o) { return o.value; }
function readWide(o) { return o.value; }

const a = make(7), sibling = make(9);
check(ArkTools.haveSameMap(a, sibling), "shared HClass");
sibling.extra = 1;
check(!ArkTools.isStableHClass(a), "non-leaf HClass");
const b = makeOther(11), otherSibling = makeOther(13);
otherSibling.extra = 1;
check(!ArkTools.isStableHClass(b), "second non-leaf HClass");
for (let i = 0; i < 200; i++) {
    readOwn(a);
    readKey(a, "value");
    readThisKey.call(a, "value");
    readPoly(i & 1 ? a : b);
}
compile(readOwn);
compile(readKey);
compile(readThisKey);
compile(readPoly);
check(readOwn(a) === 7 && readKey(a, "value") === 7 && readThisKey.call(a, "value") === 7,
      "named reads");
check(readPoly(a) === 7 && readPoly(b) === 11, "polymorphic reads");
a.value = 17;
check(readOwn(a) === 17 && readKey(a, "value") === 17, "fresh value");
print("PASS own/key/this/poly/value-update");

function makeWide(value) {
    const o = {};
    for (let i = 0; i < 24; i++) {
        o["p" + i] = i;
    }
    o.value = value;
    return o;
}
const wide = makeWide(23), wideSibling = makeWide(29);
wideSibling.extra = 1;
check(!ArkTools.isStableHClass(wide), "wide non-leaf");
for (let i = 0; i < 200; i++) { readWide(wide); }
compile(readWide);
check(readWide(wide) === 23, "out-of-object field");
print("PASS out-of-object");

let mode = 0;
let alias;
function mutate(o) {
    if (mode === 1) { o.value = 31; }
    if (mode === 2) { delete o.value; }
    if (mode === 3) { Object.defineProperty(o, "value", { get() { return 37; }, configurable: true }); }
    if (mode === 4) { alias.value = 41; }
    if (mode === 5) { o.value = 43; throw 1; }
}
function aroundCall(o) {
    const before = o.value;
    mutate(o);
    return before + ":" + o.value;
}
function aroundCatch(o) {
    const before = o.value;
    try { mutate(o); } catch (_) { return before + ":" + o.value; }
    return before + ":" + o.value;
}
function aroundDelete(o) {
    const before = o.value;
    mutate(o);
    return before + ":" + o.value;
}
function aroundGetter(o) {
    const before = o.value;
    mutate(o);
    return before + ":" + o.value;
}
for (let i = 0; i < 200; i++) {
    aroundCall(a);
    aroundCatch(a);
    aroundDelete(a);
    aroundGetter(a);
}
compile(aroundCall);
compile(aroundCatch);
compile(aroundDelete);
compile(aroundGetter);
mode = 1;
check(aroundCall(make(7)) === "7:31", "cross-call value update");
mode = 4;
alias = make(7);
check(aroundCall(alias) === "7:41", "cross-call alias update");
mode = 5;
check(aroundCatch(make(7)) === "7:43", "exception path");
mode = 2;
check(ArkTools.arkSteedIsCompiled(aroundDelete), "delete starts compiled");
check(aroundDelete(make(7)) === "7:undefined", "delete and dictionary transition");
mode = 3;
check(ArkTools.arkSteedIsCompiled(aroundGetter), "getter starts compiled");
check(aroundGetter(make(7)) === "7:37", "getter after call");
print("PASS call/alias/catch/delete/getter");

let changeInLoop = false;
let compiledAtChange = false;
function mutateInLoop(o, i) {
    if (changeInLoop && i === 3) {
        compiledAtChange = ArkTools.arkSteedIsCompiled(readLoop);
        o.extra = 1;
        o.value = 11;
    }
}
function readLoop(o) {
    let sum = 0;
    for (let i = 0; i < 8; i++) {
        const before = o.value;
        mutateInLoop(o, i);
        sum += before + o.value;
    }
    return sum;
}
const loopObject = make(7);
// Populate the mutator's taken branch before inlining it into the loop.
// Otherwise a cold-branch deopt can happen before the shape transition itself.
changeInLoop = true;
mutateInLoop(make(7), 3);
changeInLoop = false;
for (let i = 0; i < 200; i++) { readLoop(loopObject); }
compile(readLoop);
compiledAtChange = false;
changeInLoop = true;
check(readLoop(loopObject) === 148, "loop shape and value change");
check(compiledAtChange, "shape transition reached from compiled loop");
print("PASS compiled-loop-shape-change");

const getterObject = { get value() { return 47; } };
const dictionaryObject = { value: 53, discard: 1 };
delete dictionaryObject.discard;
const proxyObject = new Proxy({}, { get(target, key) { return key === "value" ? 59 : undefined; } });
check(readOwn(getterObject) === 47, "accessor fallback");
check(ArkTools.arkSteedIsCompiled(readKey), "dictionary starts compiled");
check(readKey(dictionaryObject, "value") === 53, "dictionary fallback");
check(ArkTools.arkSteedIsCompiled(readThisKey), "proxy starts compiled");
check(readThisKey.call(proxyObject, "value") === 59, "proxy fallback");
print("PASS accessor/dictionary/proxy-fallback");
