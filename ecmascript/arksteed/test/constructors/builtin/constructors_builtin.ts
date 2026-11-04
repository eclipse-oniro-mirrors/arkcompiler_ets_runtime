/*
 * Copyright (c) 2025 Huawei Device Co., Ltd.
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

// Compile the original builtin targets explicitly, then verify normal JIT
// construction before replacing the global constructors. Each replacement
// must trigger one target-guard deopt and produce an instance of C.
//! PARAMS --compiler-jit-hotness-threshold=60000
//! COMPILE_COUNT_GE testBoolean 1
//! EXEC_COUNT *testBoolean 2
//! COMPILE_COUNT_GE testNumber 1
//! EXEC_COUNT *testNumber 2
//! COMPILE_COUNT_GE testProxy 1
//! EXEC_COUNT *testProxy 2
//! COMPILE_COUNT_GE testDate 1
//! EXEC_COUNT *testDate 2
//! COMPILE_COUNT_GE testArray 1
//! EXEC_COUNT *testArray 2
//! COMPILE_COUNT_GE testSet 1
//! EXEC_COUNT *testSet 2
//! COMPILE_COUNT_GE testMap 1
//! EXEC_COUNT *testMap 2
//! COMPILE_COUNT_GE testObject 1
//! EXEC_COUNT *testObject 2
//! COMPILE_COUNT_GE testError 1
//! EXEC_COUNT *testError 2
//! COMPILE_COUNT_GE testInt8Array 1
//! EXEC_COUNT *testInt8Array 2
//! COMPILE_COUNT_GE testUint8Array 1
//! EXEC_COUNT *testUint8Array 2
//! COMPILE_COUNT_GE testUint8ClampedArray 1
//! EXEC_COUNT *testUint8ClampedArray 2
//! COMPILE_COUNT_GE testInt16Array 1
//! EXEC_COUNT *testInt16Array 2
//! COMPILE_COUNT_GE testUint16Array 1
//! EXEC_COUNT *testUint16Array 2
//! COMPILE_COUNT_GE testInt32Array 1
//! EXEC_COUNT *testInt32Array 2
//! COMPILE_COUNT_GE testUint32Array 1
//! EXEC_COUNT *testUint32Array 2
//! COMPILE_COUNT_GE testFloat32Array 1
//! EXEC_COUNT *testFloat32Array 2
//! COMPILE_COUNT_GE testFloat64Array 1
//! EXEC_COUNT *testFloat64Array 2
//! COMPILE_COUNT_GE testBigInt64Array 1
//! EXEC_COUNT *testBigInt64Array 2
//! COMPILE_COUNT_GE testBigUint64Array 1
//! EXEC_COUNT *testBigUint64Array 2
//! DEOPT_COUNT *Deoptimize* 20
//! METHOD testBoolean
//! COUNT CallCommonStub ArkSteedNewObjectFromPrimitive 1
//! COUNT DeoptIfReferenceNotEqual 1
//! HAS_NOT CallRuntime OptNewObjRange
//! METHOD testNumber
//! COUNT CallCommonStub ArkSteedNewObjectFromPrimitive 1
//! COUNT DeoptIfReferenceNotEqual 1
//! HAS_NOT CallRuntime OptNewObjRange
//! METHOD testProxy
//! COUNT CallKnownBuiltin 1
//! COUNT DeoptIfReferenceNotEqual 1
//! HAS_NOT CallRuntime OptNewObjRange
//! METHOD testDate
//! COUNT CallKnownBuiltin 1
//! COUNT DeoptIfReferenceNotEqual 1
//! HAS_NOT CallRuntime OptNewObjRange
//! METHOD testArray
//! COUNT CallKnownBuiltin 1
//! COUNT DeoptIfReferenceNotEqual 1
//! HAS_NOT CallRuntime OptNewObjRange
//! METHOD testSet
//! COUNT CallKnownBuiltin 1
//! COUNT DeoptIfReferenceNotEqual 1
//! HAS_NOT CallRuntime OptNewObjRange
//! METHOD testMap
//! COUNT CallKnownBuiltin 1
//! COUNT DeoptIfReferenceNotEqual 1
//! HAS_NOT CallRuntime OptNewObjRange
//! METHOD testObject
//! COUNT InitializeEmptyObject 1
//! COUNT DeoptIfReferenceNotEqual 1
//! HAS_NOT CallRuntime OptNewObjRange
//! METHOD testError
//! COUNT CallKnownBuiltin 1
//! COUNT DeoptIfReferenceNotEqual 1
//! HAS_NOT CallRuntime OptNewObjRange
//! METHOD testInt8Array
//! COUNT CallKnownBuiltin 1
//! COUNT DeoptIfReferenceNotEqual 1
//! HAS_NOT CallRuntime OptNewObjRange
//! METHOD testUint8Array
//! COUNT CallKnownBuiltin 1
//! COUNT DeoptIfReferenceNotEqual 1
//! HAS_NOT CallRuntime OptNewObjRange
//! METHOD testUint8ClampedArray
//! COUNT CallKnownBuiltin 1
//! COUNT DeoptIfReferenceNotEqual 1
//! HAS_NOT CallRuntime OptNewObjRange
//! METHOD testInt16Array
//! COUNT CallKnownBuiltin 1
//! COUNT DeoptIfReferenceNotEqual 1
//! HAS_NOT CallRuntime OptNewObjRange
//! METHOD testUint16Array
//! COUNT CallKnownBuiltin 1
//! COUNT DeoptIfReferenceNotEqual 1
//! HAS_NOT CallRuntime OptNewObjRange
//! METHOD testInt32Array
//! COUNT CallKnownBuiltin 1
//! COUNT DeoptIfReferenceNotEqual 1
//! HAS_NOT CallRuntime OptNewObjRange
//! METHOD testUint32Array
//! COUNT CallKnownBuiltin 1
//! COUNT DeoptIfReferenceNotEqual 1
//! HAS_NOT CallRuntime OptNewObjRange
//! METHOD testFloat32Array
//! COUNT CallCommonStub NewFloat32ArrayWithNoArgs 1
//! COUNT DeoptIfReferenceNotEqual 1
//! HAS_NOT CallRuntime OptNewObjRange
//! METHOD testFloat64Array
//! COUNT CallKnownBuiltin 1
//! COUNT DeoptIfReferenceNotEqual 1
//! HAS_NOT CallRuntime OptNewObjRange
//! METHOD testBigInt64Array
//! COUNT CallKnownBuiltin 1
//! COUNT DeoptIfReferenceNotEqual 1
//! HAS_NOT CallRuntime OptNewObjRange
//! METHOD testBigUint64Array
//! COUNT CallKnownBuiltin 1
//! COUNT DeoptIfReferenceNotEqual 1
//! HAS_NOT CallRuntime OptNewObjRange

function testBoolean() {
    return new Boolean();
}
function testNumber() {
    return new Number();
}
function testProxy() {
    const handler = {};
    return new Proxy({}, handler);
}
function testDate() {
    return new Date();
}
function testArray() {
    return new Array();
}
function testSet() {
    return new Set();
}
function testMap() {
    return new Map();
}
function testObject() {
    return new Object();
}
function testError() {
    return new Error();
}
function testInt8Array() {
    return new Int8Array();
}
function testUint8Array() {
    return new Uint8Array();
}
function testUint8ClampedArray() {
    return new Uint8ClampedArray();
}
function testInt16Array() {
    return new Int16Array();
}
function testUint16Array() {
    return new Uint16Array();
}
function testInt32Array() {
    return new Int32Array();
}
function testUint32Array() {
    return new Uint32Array();
}
function testFloat32Array() {
    return new Float32Array();
}
function testFloat64Array() {
    return new Float64Array();
}
function testBigInt64Array() {
    return new BigInt64Array();
}
function testBigUint64Array() {
    return new BigUint64Array();
}

testBoolean();
testNumber();
testProxy();
testDate();
testArray();
testSet();
testMap();
testObject();
testError();
testInt8Array();
testUint8Array();
testUint8ClampedArray();
testInt16Array();
testUint16Array();
testInt32Array();
testUint32Array();
testFloat32Array();
testFloat64Array();
testBigInt64Array();
testBigUint64Array();


ArkTools.arkSteedCompileSync(testBoolean);
ArkTools.waitJitCompileFinish(testBoolean);
ArkTools.arkSteedCompileSync(testNumber);
ArkTools.waitJitCompileFinish(testNumber);
ArkTools.arkSteedCompileSync(testProxy);
ArkTools.waitJitCompileFinish(testProxy);
ArkTools.arkSteedCompileSync(testDate);
ArkTools.waitJitCompileFinish(testDate);
ArkTools.arkSteedCompileSync(testArray);
ArkTools.waitJitCompileFinish(testArray);
ArkTools.arkSteedCompileSync(testSet);
ArkTools.waitJitCompileFinish(testSet);
ArkTools.arkSteedCompileSync(testMap);
ArkTools.waitJitCompileFinish(testMap);
ArkTools.arkSteedCompileSync(testObject);
ArkTools.waitJitCompileFinish(testObject);
ArkTools.arkSteedCompileSync(testError);
ArkTools.waitJitCompileFinish(testError);
ArkTools.arkSteedCompileSync(testInt8Array);
ArkTools.waitJitCompileFinish(testInt8Array);
ArkTools.arkSteedCompileSync(testUint8Array);
ArkTools.waitJitCompileFinish(testUint8Array);
ArkTools.arkSteedCompileSync(testUint8ClampedArray);
ArkTools.waitJitCompileFinish(testUint8ClampedArray);
ArkTools.arkSteedCompileSync(testInt16Array);
ArkTools.waitJitCompileFinish(testInt16Array);
ArkTools.arkSteedCompileSync(testUint16Array);
ArkTools.waitJitCompileFinish(testUint16Array);
ArkTools.arkSteedCompileSync(testInt32Array);
ArkTools.waitJitCompileFinish(testInt32Array);
ArkTools.arkSteedCompileSync(testUint32Array);
ArkTools.waitJitCompileFinish(testUint32Array);
ArkTools.arkSteedCompileSync(testFloat32Array);
ArkTools.waitJitCompileFinish(testFloat32Array);
ArkTools.arkSteedCompileSync(testFloat64Array);
ArkTools.waitJitCompileFinish(testFloat64Array);
ArkTools.arkSteedCompileSync(testBigInt64Array);
ArkTools.waitJitCompileFinish(testBigInt64Array);
ArkTools.arkSteedCompileSync(testBigUint64Array);
ArkTools.waitJitCompileFinish(testBigUint64Array);

// These outputs validate construction while the original targets are intact.
print(testBoolean().valueOf() === false);
print(testNumber().valueOf() === 0);
const proxy = testProxy();
print(Object.getPrototypeOf(proxy) === Object.prototype);
print(testDate() instanceof Date);
const array = testArray();
print(Array.isArray(array) && array.length === 0);
const set = testSet();
print(set instanceof Set && set.size === 0);
const map = testMap();
print(map instanceof Map && map.size === 0);
const object = testObject();
print(Object.getPrototypeOf(object) === Object.prototype);
print(testError() instanceof Error);
const int8Array = testInt8Array();
print(int8Array instanceof Int8Array && int8Array.length === 0);
const uint8Array = testUint8Array();
print(uint8Array instanceof Uint8Array && uint8Array.length === 0);
const uint8ClampedArray = testUint8ClampedArray();
print(uint8ClampedArray instanceof Uint8ClampedArray && uint8ClampedArray.length === 0);
const int16Array = testInt16Array();
print(int16Array instanceof Int16Array && int16Array.length === 0);
const uint16Array = testUint16Array();
print(uint16Array instanceof Uint16Array && uint16Array.length === 0);
const int32Array = testInt32Array();
print(int32Array instanceof Int32Array && int32Array.length === 0);
const uint32Array = testUint32Array();
print(uint32Array instanceof Uint32Array && uint32Array.length === 0);
const float32Array = testFloat32Array();
print(float32Array instanceof Float32Array && float32Array.length === 0);
const float64Array = testFloat64Array();
print(float64Array instanceof Float64Array && float64Array.length === 0);
const bigInt64Array = testBigInt64Array();
print(bigInt64Array instanceof BigInt64Array && bigInt64Array.length === 0);
const bigUint64Array = testBigUint64Array();
print(bigUint64Array instanceof BigUint64Array && bigUint64Array.length === 0);

// Object is replaced below too, so retain the intrinsic used by the checks.
const originalGetPrototypeOf = Object.getPrototypeOf;

class C {};
Boolean = C;
Number = C;
Proxy = C;
Date = C;
Array = C;
Set = C;
Map = C;
Object = C;
Error = C;
Int8Array = C;
Uint8Array = C;
Uint8ClampedArray = C;
Int16Array = C;
Uint16Array = C;
Int32Array = C;
Uint32Array = C;
Float32Array = C;
Float64Array = C;
BigInt64Array = C;
BigUint64Array = C;

function checkReplacement(value, name) {
    if (!(value instanceof C) || originalGetPrototypeOf(value) !== C.prototype) {
        throw name + ": wrong constructor after replacement";
    }
}

checkReplacement(testBoolean(), "Boolean");
checkReplacement(testNumber(), "Number");
checkReplacement(testProxy(), "Proxy");
checkReplacement(testDate(), "Date");
checkReplacement(testArray(), "Array");
checkReplacement(testSet(), "Set");
checkReplacement(testMap(), "Map");
checkReplacement(testObject(), "Object");
checkReplacement(testError(), "Error");
checkReplacement(testInt8Array(), "Int8Array");
checkReplacement(testUint8Array(), "Uint8Array");
checkReplacement(testUint8ClampedArray(), "Uint8ClampedArray");
checkReplacement(testInt16Array(), "Int16Array");
checkReplacement(testUint16Array(), "Uint16Array");
checkReplacement(testInt32Array(), "Int32Array");
checkReplacement(testUint32Array(), "Uint32Array");
checkReplacement(testFloat32Array(), "Float32Array");
checkReplacement(testFloat64Array(), "Float64Array");
checkReplacement(testBigInt64Array(), "BigInt64Array");
checkReplacement(testBigUint64Array(), "BigUint64Array");
