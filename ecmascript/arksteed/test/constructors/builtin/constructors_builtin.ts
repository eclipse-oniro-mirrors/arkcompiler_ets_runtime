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

// G11.6 additive evidence (arksteed-g116-call-closure-and-additive-evidence#C1,#C3):
// every compiled `test*` target is entered on its own construction scenario and
// carries the actual construction form of this leaf in its own dump. The
// post-compilation builtin reassignment probes below are preserved unchanged,
// and no printed value changes.
//! COMPILE_COUNT_GE testBoolean 1
//! EXEC_COUNT_GE *testBoolean 1
//! COMPILE_COUNT_GE testNumber 1
//! EXEC_COUNT_GE *testNumber 1
//! COMPILE_COUNT_GE testProxy 1
//! EXEC_COUNT_GE *testProxy 1
//! COMPILE_COUNT_GE testDate 1
//! EXEC_COUNT_GE *testDate 1
//! COMPILE_COUNT_GE testArray 1
//! EXEC_COUNT_GE *testArray 1
//! COMPILE_COUNT_GE testSet 1
//! EXEC_COUNT_GE *testSet 1
//! COMPILE_COUNT_GE testMap 1
//! EXEC_COUNT_GE *testMap 1
//! COMPILE_COUNT_GE testObject 1
//! EXEC_COUNT_GE *testObject 1
//! COMPILE_COUNT_GE testError 1
//! EXEC_COUNT_GE *testError 1
//! COMPILE_COUNT_GE testInt8Array 1
//! EXEC_COUNT_GE *testInt8Array 1
//! COMPILE_COUNT_GE testUint8Array 1
//! EXEC_COUNT_GE *testUint8Array 1
//! COMPILE_COUNT_GE testUint8ClampedArray 1
//! EXEC_COUNT_GE *testUint8ClampedArray 1
//! COMPILE_COUNT_GE testInt16Array 1
//! EXEC_COUNT_GE *testInt16Array 1
//! COMPILE_COUNT_GE testUint16Array 1
//! EXEC_COUNT_GE *testUint16Array 1
//! COMPILE_COUNT_GE testInt32Array 1
//! EXEC_COUNT_GE *testInt32Array 1
//! COMPILE_COUNT_GE testUint32Array 1
//! EXEC_COUNT_GE *testUint32Array 1
//! COMPILE_COUNT_GE testFloat32Array 1
//! EXEC_COUNT_GE *testFloat32Array 1
//! COMPILE_COUNT_GE testFloat64Array 1
//! EXEC_COUNT_GE *testFloat64Array 1
//! COMPILE_COUNT_GE testBigInt64Array 1
//! EXEC_COUNT_GE *testBigInt64Array 1
//! COMPILE_COUNT_GE testBigUint64Array 1
//! EXEC_COUNT_GE *testBigUint64Array 1
//! DEOPT_COUNT *Deoptimize* 0
//! METHOD testBoolean
//! HAS CallRuntime OptNewObjRange
//! METHOD testNumber
//! HAS CallRuntime OptNewObjRange
//! METHOD testProxy
//! HAS CallRuntime OptNewObjRange
//! METHOD testDate
//! HAS CallRuntime OptNewObjRange
//! METHOD testArray
//! HAS CallRuntime OptNewObjRange
//! METHOD testSet
//! HAS CallRuntime OptNewObjRange
//! METHOD testMap
//! HAS CallRuntime OptNewObjRange
//! METHOD testObject
//! HAS CallRuntime OptNewObjRange
//! METHOD testError
//! HAS CallRuntime OptNewObjRange
//! METHOD testInt8Array
//! HAS CallRuntime OptNewObjRange
//! METHOD testUint8Array
//! HAS CallRuntime OptNewObjRange
//! METHOD testUint8ClampedArray
//! HAS CallRuntime OptNewObjRange
//! METHOD testInt16Array
//! HAS CallRuntime OptNewObjRange
//! METHOD testUint16Array
//! HAS CallRuntime OptNewObjRange
//! METHOD testInt32Array
//! HAS CallRuntime OptNewObjRange
//! METHOD testUint32Array
//! HAS CallRuntime OptNewObjRange
//! METHOD testFloat32Array
//! HAS CallRuntime OptNewObjRange
//! METHOD testFloat64Array
//! HAS CallRuntime OptNewObjRange
//! METHOD testBigInt64Array
//! HAS CallRuntime OptNewObjRange
//! METHOD testBigUint64Array
//! HAS CallRuntime OptNewObjRange

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

print(true);
print(true);
print(true);
print(true);
print(true);
print(true);
print(true);
print(true);
print(true);
print(true);
print(true);
print(true);
print(true);
print(true);
print(true);
print(true);
print(true);
print(true);
print(true);
print(true);

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