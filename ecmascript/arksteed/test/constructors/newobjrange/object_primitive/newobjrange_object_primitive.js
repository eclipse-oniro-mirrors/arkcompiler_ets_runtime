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

//! PARAMS --compiler-jit-hotness-threshold=60000 --enable-force-gc=true

//! METHOD wrap
//! COUNT CallCommonStub ArkSteedNewObjectFromPrimitive 1
//! COUNT BranchIfECMAObject 1
//! COUNT Phi 1
//! HAS_NOT InitializeEmptyObject
//! HAS_NOT CallKnownBuiltin
//! HAS_NOT CallRuntime OptNewObjRange

//! METHOD wrapKeeping
//! COUNT CallCommonStub ArkSteedNewObjectFromPrimitive 1

//! METHOD wrapTwo
//! COUNT CallCommonStub ArkSteedNewObjectFromPrimitive 2

//! METHOD wrapInTry
//! COUNT CallCommonStub ArkSteedNewObjectFromPrimitive 1
//! HAS_NOT InitializeEmptyObject

//! METHOD knownNumber
//! HAS CallCommonStub ArkSteedNewObjectFromPrimitive
//! HAS_NOT InitializeEmptyObject

//! METHOD knownNull
//! HAS InitializeEmptyObject
//! HAS_NOT CallCommonStub ArkSteedNewObjectFromPrimitive

//! METHOD knownString
//! HAS CallKnownBuiltin
//! HAS_NOT CallCommonStub ArkSteedNewObjectFromPrimitive

//! METHOD noArguments
//! HAS InitializeEmptyObject
//! HAS_NOT CallCommonStub ArkSteedNewObjectFromPrimitive

//! METHOD wrapTarget
//! COUNT CallCommonStub ArkSteedNewObjectFromPrimitive 1
//! HAS DeoptIfReferenceNotEqual

function wrap(value) {
    return new Object(value);
}

function wrapKeeping(value, keep) {
    const result = new Object(value);
    return [result, keep, value];
}

function wrapTwo(left, right) {
    return [new Object(left), new Object(right)];
}

function wrapInTry(value) {
    try {
        return new Object(value);
    } catch (error) {
        return error;
    }
}

function knownNumber() {
    return new Object(17);
}

function knownNull() {
    return new Object(null);
}

function knownString() {
    return new Object("abc");
}

function noArguments() {
    return new Object();
}

function wrapTarget(target, value) {
    return new target(value);
}

function check(condition, message) {
    if (!condition) {
        throw new Error(message);
    }
}

const keep = { marker: 42 };
for (let i = 0; i < 16; i++) {
    wrap(i);
    wrapKeeping(i, keep);
    wrapTwo(i, i + 1);
    wrapInTry(i);
    knownNumber();
    knownNull();
    knownString();
    noArguments();
    wrapTarget(Object, i);
}
const compiled = [wrap, wrapKeeping, wrapTwo, wrapInTry, knownNumber, knownNull, knownString, noArguments];
for (const method of compiled) {
    ArkTools.arkSteedCompileSync(method);
}
ArkTools.arkSteedCompileSync(wrapTarget);

const cases = [
    [0, Number.prototype],
    [-0, Number.prototype],
    [17, Number.prototype],
    [1.5, Number.prototype],
    [NaN, Number.prototype],
    [Infinity, Number.prototype],
    [true, Boolean.prototype],
    [false, Boolean.prototype],
    [Symbol("boxed"), Symbol.prototype],
    [BigInt("12345678901234567890"), BigInt.prototype],
    ["", String.prototype],
    ["abc", String.prototype],
    ["\u4e2d\ud83d\ude00", String.prototype],
];
for (const entry of cases) {
    const value = entry[0];
    const first = wrap(value);
    const second = wrapInTry(value);
    check(first !== second, "fresh wrappers");
    check(Object.getPrototypeOf(first) === entry[1], "wrapper prototype");
    check(Object.is(first.valueOf(), value), "wrapped value");
    check(Object.is(second.valueOf(), value), "wrapped value in try");
    const pair = wrapTwo(value, value);
    check(pair[0] !== pair[1] && Object.is(pair[1].valueOf(), value), "two sites");
    if (typeof value === "string") {
        const length = Object.getOwnPropertyDescriptor(first, "length");
        check(length.value === value.length, "UTF-16 length");
        check(!length.writable && !length.enumerable && !length.configurable, "length attributes");
        check(Object.keys(first).length === value.length, "string keys");
        for (let i = 0; i < value.length; i++) {
            const index = Object.getOwnPropertyDescriptor(first, String(i));
            check(index.value === value[i], "string index value");
            check(!index.writable && index.enumerable && !index.configurable, "index attributes");
        }
    }
}
print("primitive wrappers: ok");

const empty = [wrap(null), wrap(undefined), knownNull(), noArguments()];
for (let i = 0; i < empty.length; i++) {
    check(Object.getPrototypeOf(empty[i]) === Object.prototype, "empty prototype");
    check(Object.getOwnPropertyNames(empty[i]).length === 0, "empty properties");
    for (let j = 0; j < i; j++) {
        check(empty[i] !== empty[j], "fresh empty objects");
    }
}
check(knownNumber().valueOf() === 17 && knownString().length === 3, "known types");
print("nullish and known types: ok");

let hooks = 0;
const object = {
    valueOf() {
        hooks++;
        throw new Error("unexpected valueOf");
    },
    [Symbol.toPrimitive]() {
        hooks++;
        throw new Error("unexpected conversion");
    },
};
const proxy = new Proxy(object, {
    get() {
        hooks++;
        throw new Error("unexpected trap");
    },
});
const revoked = Proxy.revocable({}, {});
revoked.revoke();
for (const value of [object, proxy, revoked.proxy, [], function f() {}, new Uint8Array(2), wrap(1)]) {
    check(wrap(value) === value && wrapInTry(value) === value, "object identity");
}
check(hooks === 0, "no user hooks");
print("object identity: ok");

const retained = [];
for (let i = 0; i < 32; i++) {
    const value =
        i % 3 === 0 ? Symbol("gc" + i) : i % 3 === 1 ? BigInt("12345678901234567890") + BigInt(i) : "gc-string-" + i;
    retained.push(wrapKeeping(value, keep));
}
ArkTools.forceFullGC();
for (const row of retained) {
    check(row[1] === keep && row[1].marker === 42, "live receiver across call");
    check(Object.is(row[0].valueOf(), row[2]), "heap primitive survives GC");
}
for (const method of compiled) {
    check(ArkTools.arkSteedIsCompiled(method), "compiled path retained");
}
print("GC and compiled paths: ok");

let constructorCalls = 0;
function Replacement(value) {
    constructorCalls++;
    this.value = value;
}
const replacement = wrapTarget(Replacement, 73);
check(replacement instanceof Replacement && replacement.value === 73, "changed target");
check(constructorCalls === 1, "constructor executes once after guard failure");
print("target guard: ok");
