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

//! PARAMS --compiler-jit-hotness-threshold=2000 --enable-pgo-profiler=true

//! METHOD boolDoubleBang
//! COMPILE_COUNT_GE *boolDoubleBang 1
//! EXEC_COUNT_GE *boolDoubleBang 1
//! HAS DeoptIfNotBoolean
//! HAS_NOT CallCommonStub ToBoolean*

//! METHOD boolNegate
//! COMPILE_COUNT_GE *boolNegate 1
//! EXEC_COUNT_GE *boolNegate 1
//! HAS DeoptIfNotBoolean
//! HAS_NOT CallCommonStub ToBoolean*

//! METHOD intDoubleBang
//! COMPILE_COUNT_GE *intDoubleBang 1
//! EXEC_COUNT_GE *intDoubleBang 1
//! HAS CheckedTaggedIntToI32
//! HAS I32ConditionCheck
//! HAS_NOT CallCommonStub ToBoolean*

//! METHOD doubleDoubleBang
//! COMPILE_COUNT_GE *doubleDoubleBang 1
//! EXEC_COUNT_GE *doubleDoubleBang 1
//! HAS TaggedDoubleToBoolean
//! HAS_NOT CallCommonStub ToBoolean*

//! METHOD nullDoubleBang
//! COMPILE_COUNT_GE *nullDoubleBang 1
//! EXEC_COUNT_GE *nullDoubleBang 1
//! HAS DeoptIfNotUndefinedOrNull
//! HAS TaggedConstant
//! HAS_NOT CallCommonStub ToBoolean*

//! METHOD andLeft
//! COMPILE_COUNT_GE *andLeft 1
//! EXEC_COUNT_GE *andLeft 1
//! HAS DeoptIfNotBoolean
//! HAS_NOT CallCommonStub ToBoolean*

//! METHOD staticNumber
//! COMPILE_COUNT_GE *staticNumber 1
//! EXEC_COUNT_GE *staticNumber 1
//! HAS Float64ToBoolean
//! HAS_NOT DeoptIfNotNumber
//! HAS_NOT CallCommonStub ToBoolean*

//! METHOD staticInt
//! COMPILE_COUNT_GE *staticInt 1
//! EXEC_COUNT_GE *staticInt 1
//! HAS I32ConditionCheck
//! HAS_NOT DeoptIfNotNumber
//! HAS_NOT CallCommonStub ToBoolean*

//! METHOD receiverTruthy
//! COMPILE_COUNT_GE *receiverTruthy 1
//! EXEC_COUNT_GE *receiverTruthy 1
//! HAS TaggedConstant
//! HAS_NOT CallCommonStub ToBoolean*

//! METHOD constFold
//! COMPILE_COUNT_GE *constFold 1
//! EXEC_COUNT_GE *constFold 1
//! HAS TaggedConstant
//! HAS_NOT CallCommonStub ToBoolean*

//! METHOD specTruthy
//! COMPILE_COUNT_GE *specTruthy 1
//! EXEC_COUNT_GE *specTruthy 1
//! HAS TaggedDoubleToBoolean
//! HAS_NOT CallCommonStub ToBoolean*

//! METHOD specFalsy
//! COMPILE_COUNT_GE *specFalsy 1
//! EXEC_COUNT_GE *specFalsy 1
//! HAS TaggedDoubleToBoolean
//! HAS_NOT CallCommonStub ToBoolean*

function boolDoubleBang(x) { return !!x; }
function boolNegate(x) { return !x; }
function intDoubleBang(x) { return !!x; }
function doubleDoubleBang(x) { return !!x; }
function nullDoubleBang(x) { return !!x; }
function andLeft(x, y) { return x && y; }
function staticNumber(a, b) { const x = a * b; return !!x; }
function staticInt(x) { const i = x | 0; return !!i; }
function receiverTruthy(o) { const d = o.x; return !!o; }
function constFold() { return !!1; }
function specTruthy(x) { return !!x; }
function specFalsy(x) { return !x; }

let monoObj = { x: 1 };
for (let i = 0; i < 2000; i++) {
    boolDoubleBang(i % 2 === 0);
    boolNegate(i % 2 === 0);
    intDoubleBang(i + 1);
    doubleDoubleBang(i + 0.5);
    nullDoubleBang(null);
    andLeft(i % 2 === 0, i);
    staticNumber(i + 0.5, 2.0);
    staticInt(i);
    receiverTruthy(monoObj);
    specTruthy(i + 0.5);
    specFalsy(i + 0.5);
}

ArkTools.arkSteedCompileSync(boolDoubleBang);
ArkTools.arkSteedCompileSync(boolNegate);
ArkTools.arkSteedCompileSync(intDoubleBang);
ArkTools.arkSteedCompileSync(doubleDoubleBang);
ArkTools.arkSteedCompileSync(nullDoubleBang);
ArkTools.arkSteedCompileSync(andLeft);
ArkTools.arkSteedCompileSync(staticNumber);
ArkTools.arkSteedCompileSync(staticInt);
ArkTools.arkSteedCompileSync(receiverTruthy);
ArkTools.arkSteedCompileSync(constFold);
ArkTools.arkSteedCompileSync(specTruthy);
ArkTools.arkSteedCompileSync(specFalsy);

// Specialized paths must agree with the interpreter on every truthy/falsy input.
print(boolDoubleBang(true));
print(boolDoubleBang(false));
print(boolDoubleBang(1));
print(boolDoubleBang(0));
print(boolDoubleBang(-0.0));
print(boolDoubleBang(NaN));
print(boolDoubleBang(0.5));
print(boolDoubleBang(""));
print(boolDoubleBang("0"));
print(boolDoubleBang(null));
print(boolDoubleBang(undefined));
print(boolNegate(true));
print(boolNegate(false));
print(boolNegate(0));
print(boolNegate(""));
print(intDoubleBang(3));
print(intDoubleBang(0));
print(doubleDoubleBang(2.5));
print(doubleDoubleBang(-1.5));
print(doubleDoubleBang(NaN));
print(doubleDoubleBang(0.0));
print(doubleDoubleBang(-0.0));
print(nullDoubleBang(null));
print(nullDoubleBang(undefined));
print(andLeft(true, 5));
print(andLeft(false, 5));
print(staticNumber(1.5, 2.0));
print(staticNumber(0.0, 3.0));
print(staticNumber(NaN, 1.0));
print(staticInt(7));
print(staticInt(0));
print(staticInt(-3));
print(receiverTruthy(monoObj));
print(receiverTruthy([]));
print(constFold());

// Spec matrix: IsTrue/IsFalse for every falsy/truthy input class. The first
// undefined input exits the double-specialized path; subsequent rows check
// behavior after deoptimization rather than specialized-code execution.
print(specTruthy(undefined) + " " + specFalsy(undefined));
print(specTruthy(null) + " " + specFalsy(null));
print(specTruthy(false) + " " + specFalsy(false));
print(specTruthy(true) + " " + specFalsy(true));
print(specTruthy(0) + " " + specFalsy(0));
print(specTruthy(-0) + " " + specFalsy(-0));
print(specTruthy(1) + " " + specFalsy(1));
print(specTruthy(-1) + " " + specFalsy(-1));
print(specTruthy(42) + " " + specFalsy(42));
print(specTruthy(0.0) + " " + specFalsy(0.0));
print(specTruthy(3.14) + " " + specFalsy(3.14));
print(specTruthy(NaN) + " " + specFalsy(NaN));
print(specTruthy(Infinity) + " " + specFalsy(Infinity));
print(specTruthy("") + " " + specFalsy(""));
print(specTruthy("abc") + " " + specFalsy("abc"));
print(specTruthy("0") + " " + specFalsy("0"));
print(specTruthy(0n) + " " + specFalsy(0n));
print(specTruthy(1n) + " " + specFalsy(1n));
print(specTruthy(Symbol()) + " " + specFalsy(Symbol()));
print(specTruthy({}) + " " + specFalsy({}));
print(specTruthy([]) + " " + specFalsy([]));
print(specTruthy(function () {}) + " " + specFalsy(function () {}));
