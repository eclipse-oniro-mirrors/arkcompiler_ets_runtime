/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

//! PARAMS --compiler-jit-hotness-threshold=60000 --compiler-arksteed-enable-inline=false
//! METHOD int32Loop
//! COUNT_GE Phi 2
//! HAS I32AddWithOverflow
//! HAS I32ToTaggedInt
//! HAS_NOT TaggedIntToI32
//! HAS_NOT CheckedTaggedIntToI32
//! METHOD mixedLoop
//! HAS Phi
//! HAS CheckedTaggedIntToI32
//! METHOD float64Loop
//! HAS Phi
//! HAS F64ToTaggedDouble
//! COUNT CheckedNumberToF64 1
//! METHOD float64StoreLoop
//! HAS Phi
//! COUNT CheckedNumberToF64 1
//! HAS_NOT F64ToTaggedDouble
//! METHOD float64CallLoop
//! HAS Phi
//! HAS Call
//! HAS_NOT F64ToTaggedDouble
//! METHOD float64TaggedUseInLoop
//! HAS Phi
//! HAS CheckedNumberToF64
//! METHOD intToFloatMerge
//! COUNT_GE Phi 2
//! HAS I32ToF64
//! METHOD lazyDeoptLoop
//! COUNT_GE Phi 2
//! HAS CallKnownJSFunction
//! METHOD catchLoop
//! HAS Phi
//! HAS CheckedTaggedIntToI32
//! METHOD uint32PhiToFloat64
//! HAS Phi
//! HAS U32ToF64
//! METHOD uint32PhiToInt32
//! HAS Phi
//! HAS_NOT U32ToF64

function int32Loop()
{
    let sum = 0;
    for (let i = 0; i < 10; i++) {
        sum += i;
    }
    return sum;
}

function mixedLoop(switchAt)
{
    let value = 0;
    for (let i = 0; i < 4; i++) {
        if (i === switchAt) {
            value = "x";
        } else {
            value += 1;
        }
    }
    return value;
}

function float64Loop(step)
{
    let sum = 0.5;
    for (let i = 0; i < 6; i++) {
        sum += step;
    }
    return sum;
}

function float64StoreLoop(array, step)
{
    let sum = 0.5;
    for (let i = 0; i < 6; i++) {
        sum += step;
    }
    array[0] = sum;
    return true;
}

function float64CallLoop(array, callback)
{
    let sum = 0.5;
    for (let i = 0; i < 6; i++) {
        if (i === 2) {
            callback();
        }
        sum += 1.25;
    }
    array[0] = sum;
    return true;
}

function float64TaggedUseInLoop(step, callback)
{
    let sum = 0.5;
    for (let i = 0; i < 6; i++) {
        callback(sum);
        sum += step;
    }
    return sum;
}

function intToFloatMerge(array, useInteger)
{
    let integer = 0;
    for (let i = 0; i < 6; i++) {
        integer += i;
    }
    const number = useInteger ? integer : 0.5;
    array[0] = number;
    return true;
}

function lazyDeoptLoop(holder, callback)
{
    let result = 0;
    for (let i = 0; i < 5; i++) {
        if (i === 2) {
            callback(holder);
        }
        result += holder.value;
    }
    return result;
}

function catchLoop(failAt)
{
    let result = 0;
    for (let i = 0; i < 5; i++) {
        try {
            if (i === failAt) {
                throw i;
            }
            result += i;
        } catch (error) {
            result += error;
        }
    }
    return result;
}

function uint32PhiToFloat64(left, right, useLeft)
{
    const length = useLeft ? left.length : right.length;
    return length + 0.5;
}

function uint32PhiToInt32(left, right, useLeft)
{
    const length = useLeft ? left.length : right.length;
    return length | 0;
}

let callbackCount = 0;
let callbackSum = 0;

function recordCall(value)
{
    callbackCount++;
    if (typeof value === "number") {
        callbackSum += value;
    }
}

function recordOtherCall(value)
{
    recordCall(value);
}

function addProperty(holder)
{
    holder.changed = true;
}

let ok = true;
const holder = { value: 7 };
const doubleArray = new Float64Array(1);
const shortArray = [1, 2];
const longArray = [1, 2, 3, 4, 5];
for (let i = 0; i < 100; i++) {
    ok = int32Loop() === 45 && ok;
    ok = mixedLoop(-1) === 4 && ok;
    ok = float64Loop(1.25) === 8 && ok;
    ok = float64StoreLoop(doubleArray, 1.25) && doubleArray[0] === 8 && ok;
    const callback = (i & 1) === 0 ? recordCall : recordOtherCall;
    ok = float64CallLoop(doubleArray, callback) && doubleArray[0] === 8 && ok;
    ok = float64TaggedUseInLoop(1.25, recordCall) === 8 && ok;
    ok = intToFloatMerge(doubleArray, (i & 1) === 0) && ok;
    ok = lazyDeoptLoop(holder, recordCall) === 35 && ok;
    ok = catchLoop(-1) === 10 && ok;
    const useShortArray = (i & 1) === 0;
    ok = uint32PhiToFloat64(shortArray, longArray, useShortArray) === (useShortArray ? 2.5 : 5.5) && ok;
    ok = uint32PhiToInt32(shortArray, longArray, useShortArray) === (useShortArray ? 2 : 5) && ok;
}

ok = callbackCount === 800 && callbackSum === 2175 && ok;

const methods = [int32Loop, mixedLoop, float64Loop, float64StoreLoop, float64CallLoop, intToFloatMerge,
    float64TaggedUseInLoop, lazyDeoptLoop, catchLoop, uint32PhiToFloat64, uint32PhiToInt32];
for (const method of methods) {
    ArkTools.arkSteedCompileSync(method);
    ArkTools.waitJitCompileFinish(method);
    ok = ArkTools.isAOTCompiled(method) && ok;
}

ok = int32Loop() === 45 && ok;
ok = mixedLoop(1) === "x11" && ok;
ok = float64Loop(1.25) === 8 && ok;
ok = float64StoreLoop(doubleArray, 1.25) && doubleArray[0] === 8 && ok;
ok = float64CallLoop(doubleArray, recordCall) && doubleArray[0] === 8 && ok;
ok = float64TaggedUseInLoop(1.25, recordCall) === 8 && ok;
ok = intToFloatMerge(doubleArray, true) && doubleArray[0] === 15 && ok;
ok = intToFloatMerge(doubleArray, false) && doubleArray[0] === 0.5 && ok;
const genericArray = [0];
ok = float64StoreLoop(genericArray, 1.25) && genericArray[0] === 8 && ok;
ok = lazyDeoptLoop(holder, addProperty) === 35 && ok;
ok = catchLoop(2) === 10 && ok;
ok = uint32PhiToFloat64(shortArray, longArray, true) === 2.5 && ok;
ok = uint32PhiToFloat64(shortArray, longArray, false) === 5.5 && ok;
ok = uint32PhiToInt32(shortArray, longArray, true) === 2 && ok;
ok = uint32PhiToInt32(shortArray, longArray, false) === 5 && ok;
ok = callbackCount === 807 && callbackSum === 2196.75 && ok;
print(ok ? "PASS" : "FAIL");
