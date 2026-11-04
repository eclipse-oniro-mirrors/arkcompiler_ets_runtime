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
//! METHOD makePlain
//! COUNT CallNewObjRangeBase 1
//! COUNT CallNewObjRangeDerived 1
//! CODEGEN_COUNT *CallNewObjRange*commonStub = ArkSteedCallConstructorBody* 0
//! COUNT InitializeEmptyObject 1
//! HAS CallRuntime AllocateInYoung

//! METHOD makeNumber
//! COUNT CallKnownBuiltin 1
//! HAS_NOT CallRuntime OptNewObjRange

//! METHOD makeObject
//! COUNT CallCommonStub ArkSteedNewObjectFromPrimitive 1
//! HAS_NOT CallKnownBuiltin
//! HAS_NOT InitializeEmptyObject
//! HAS_NOT CallRuntime OptNewObjRange

//! METHOD makeFloat32Array
//! HAS CallCommonStub NewFloat32Array
//! HAS_NOT CallKnownBuiltin
//! HAS_NOT CallRuntime OptNewObjRange

function Plain(left, right) {
    this.total = left + right;
    return 1;
}

function makePlain(ctor, left, right) {
    return new ctor(left, right);
}

for (let i = 0; i < 16; i++) makePlain(Plain, 1, 2);
ArkTools.arkSteedCompileSync(makePlain);
print(makePlain(Plain, 20, 22).total);

let replacementCalls = 0;
function Replacement(left, right) {
    replacementCalls++;
    this.total = left * right;
}
print(makePlain(Replacement, 6, 7).total);
print(replacementCalls);

function WithMutablePrototype(value) {
    this.value = value;
}
function makeWithMutablePrototype(ctor, value) {
    return new ctor(value);
}
for (let i = 0; i < 16; i++) makeWithMutablePrototype(WithMutablePrototype, 1);
ArkTools.arkSteedCompileSync(makeWithMutablePrototype);
WithMutablePrototype.prototype = { marker: "changed" };
const changed = makeWithMutablePrototype(WithMutablePrototype, 73);
print(changed.marker + ":" + changed.value);

function makeNumber(value) {
    return new Number(value);
}
function makeObject(value) {
    return new Object(value);
}
function makeBoolean(value) {
    return new Boolean(value);
}
function makeFloat32Array(length) {
    return new Float32Array(length);
}
function makeDate(value) {
    return new Date(value);
}

const existing = { marker: 9 };
makeNumber(1);
makeObject(existing);
makeBoolean(true);
makeFloat32Array(1);
makeDate(0);
ArkTools.arkSteedCompileSync(makeNumber);
ArkTools.arkSteedCompileSync(makeObject);
ArkTools.arkSteedCompileSync(makeBoolean);
ArkTools.arkSteedCompileSync(makeFloat32Array);
ArkTools.arkSteedCompileSync(makeDate);

print(makeNumber("12.5").valueOf());
print(makeObject(existing) === existing);
print(makeBoolean(0).valueOf());
const array = makeFloat32Array(4);
array[3] = 3.5;
print(array.length + ":" + array[3]);
print(makeDate(0).getTime());

let rangeError = false;
try {
    makeFloat32Array(-1);
} catch (error) {
    rangeError = error instanceof RangeError;
}
print(rangeError);

let numberReplacementCalls = 0;
function ReplacementNumber(value) {
    numberReplacementCalls++;
    this.value = value;
}
Number = ReplacementNumber;
const replacedNumber = makeNumber(9);
print(replacedNumber instanceof ReplacementNumber);
print(replacedNumber.value);
print(numberReplacementCalls);
