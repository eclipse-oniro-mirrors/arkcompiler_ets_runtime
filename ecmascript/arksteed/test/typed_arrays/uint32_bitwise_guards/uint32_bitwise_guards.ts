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

// @ts-nocheck
//! PARAMS --compiler-jit-hotness-threshold=10000
//! EXEC_COUNT_GE *guardUint32Bounds 1
//! EXEC_COUNT_GE *guardUint32Negative 1
//! EXEC_COUNT_GE *guardUint32Fraction 1
//! EXEC_COUNT_GE *guardUint32NegativeFraction 1
//! EXEC_COUNT_GE *guardUint32NaN 1
//! EXEC_COUNT_GE *guardUint32Infinity 1
//! EXEC_COUNT_GE *guardUint32Array 1
//! EXEC_COUNT_GE *guardUint32Detached 1
//! EXEC_COUNT_GE *extendedGuardBounds 1
//! EXEC_COUNT_GE *extendedGuardNegative 1
//! EXEC_COUNT_GE *extendedGuardFraction 1
//! EXEC_COUNT_GE *extendedGuardArray 1
//! EXEC_COUNT_GE *extendedGuardLeftScalarString 1
//! EXEC_COUNT_GE *extendedGuardLeftScalarFraction 1
//! EXEC_COUNT_GE *extendedGuardLeftScalarUndefined 1
//! EXEC_COUNT_GE *extendedGuardRightScalarString 1
//! EXEC_COUNT_GE *extendedGuardRightScalarFraction 1
//! EXEC_COUNT_GE *extendedGuardRightScalarUndefined 1
//! EXEC_COUNT_GE *extendedGuardRightEffect 1
//! EXEC_COUNT_GE *extendedGuardLeftEffect 1
//! EXEC_COUNT_GE *extendedGuardLeftShiftEffect 1
//! EXEC_COUNT_GE *extendedGuardBothIndexEffect 1
//! EXEC_COUNT_GE *extendedGuardThrowLeft 1
//! EXEC_COUNT_GE *extendedGuardThrowRight 1
//! EXEC_COUNT_GE *extendedGuardDetached 1
//! DEOPT_COUNT * 25
declare function print(arg: any): void;

// Each target gets exactly one exceptional post-compilation call in a fresh compiled method.
// Guard deopts are deliberately separated from the normal zero-deopt cases.
//! METHOD guardUint32Bounds
//! HAS TypedArrayDoubleLoadElement
//! HAS F64ToI32Trunc
function guardUint32Bounds(a, i) {
    return a[i] & 255;
}

//! METHOD guardUint32Negative
//! HAS TypedArrayDoubleLoadElement
//! HAS F64ToI32Trunc
function guardUint32Negative(a, i) {
    return a[i] & 255;
}

//! METHOD guardUint32Fraction
//! HAS TypedArrayDoubleLoadElement
//! HAS F64ToI32Trunc
function guardUint32Fraction(a, i) {
    return a[i] & 255;
}

//! METHOD guardUint32NegativeFraction
//! HAS TypedArrayDoubleLoadElement
//! HAS F64ToI32Trunc
function guardUint32NegativeFraction(a, i) {
    return a[i] & 255;
}

//! METHOD guardUint32NaN
//! HAS TypedArrayDoubleLoadElement
//! HAS F64ToI32Trunc
function guardUint32NaN(a, i) {
    return a[i] & 255;
}

//! METHOD guardUint32Infinity
//! HAS TypedArrayDoubleLoadElement
//! HAS F64ToI32Trunc
function guardUint32Infinity(a, i) {
    return a[i] & 255;
}

//! METHOD guardUint32Array
//! HAS TypedArrayDoubleLoadElement
//! HAS F64ToI32Trunc
function guardUint32Array(a, i) {
    return a[i] & 255;
}

//! METHOD guardUint32Detached
//! HAS TypedArrayDoubleLoadElement
//! HAS F64ToI32Trunc
function guardUint32Detached(a, i) {
    return a[i] & 255;
}

// Right-side receiver coverage uses a representative Float64 value; the left-side
// cases above cover negative fractions, NaN and Infinity as well.
// Effectful conversions below also cover objects returning a plain integer.
//! METHOD extendedGuardBounds
//! HAS TypedArrayDoubleLoadElement
//! COUNT_GE F64ToI32Trunc 1
function extendedGuardBounds(a, b, i, k) {
    return 255 & a[i];
}

//! METHOD extendedGuardNegative
//! HAS TypedArrayDoubleLoadElement
//! COUNT_GE F64ToI32Trunc 1
function extendedGuardNegative(a, b, i, k) {
    return 255 & a[i];
}

//! METHOD extendedGuardFraction
//! HAS TypedArrayDoubleLoadElement
//! COUNT_GE F64ToI32Trunc 1
function extendedGuardFraction(a, b, i, k) {
    return 255 & a[i];
}

//! METHOD extendedGuardArray
//! HAS TypedArrayDoubleLoadElement
//! COUNT_GE F64ToI32Trunc 1
function extendedGuardArray(a, b, i, k) {
    return 255 & a[i];
}

//! METHOD extendedGuardLeftScalarString
//! HAS TypedArrayDoubleLoadElement
//! COUNT_GE F64ToI32Trunc 1
function extendedGuardLeftScalarString(a, b, i, k) {
    return k & a[i];
}

//! METHOD extendedGuardLeftScalarFraction
//! HAS TypedArrayDoubleLoadElement
//! COUNT_GE F64ToI32Trunc 1
function extendedGuardLeftScalarFraction(a, b, i, k) {
    return k & a[i];
}

//! METHOD extendedGuardLeftScalarUndefined
//! HAS TypedArrayDoubleLoadElement
//! COUNT_GE F64ToI32Trunc 1
function extendedGuardLeftScalarUndefined(a, b, i, k) {
    return k & a[i];
}

//! METHOD extendedGuardRightScalarString
//! HAS TypedArrayDoubleLoadElement
//! COUNT_GE F64ToI32Trunc 1
function extendedGuardRightScalarString(a, b, i, k) {
    return a[i] & k;
}

//! METHOD extendedGuardRightScalarFraction
//! HAS TypedArrayDoubleLoadElement
//! COUNT_GE F64ToI32Trunc 1
function extendedGuardRightScalarFraction(a, b, i, k) {
    return a[i] & k;
}

//! METHOD extendedGuardRightScalarUndefined
//! HAS TypedArrayDoubleLoadElement
//! COUNT_GE F64ToI32Trunc 1
function extendedGuardRightScalarUndefined(a, b, i, k) {
    return a[i] & k;
}

//! METHOD extendedGuardRightEffect
//! HAS TypedArrayDoubleLoadElement
//! COUNT_GE F64ToI32Trunc 1
function extendedGuardRightEffect(a, b, i, k) {
    return a[i] & k;
}

//! METHOD extendedGuardLeftEffect
//! HAS TypedArrayDoubleLoadElement
//! COUNT_GE F64ToI32Trunc 1
function extendedGuardLeftEffect(a, b, i, k) {
    return k & a[i];
}

//! METHOD extendedGuardLeftShiftEffect
//! HAS TypedArrayDoubleLoadElement
//! COUNT_GE F64ToI32Trunc 1
function extendedGuardLeftShiftEffect(a, b, i, k) {
    return k << a[i];
}

//! METHOD extendedGuardBothIndexEffect
//! HAS TypedArrayDoubleLoadElement
//! COUNT_GE F64ToI32Trunc 2
function extendedGuardBothIndexEffect(a, b, i, k) {
    return a[i] & b[k];
}

//! METHOD extendedGuardThrowLeft
//! HAS TypedArrayDoubleLoadElement
//! COUNT_GE F64ToI32Trunc 1
function extendedGuardThrowLeft(a, b, i, k) {
    return k & a[i];
}

//! METHOD extendedGuardThrowRight
//! HAS TypedArrayDoubleLoadElement
//! COUNT_GE F64ToI32Trunc 1
function extendedGuardThrowRight(a, b, i, k) {
    return a[i] & k;
}

//! METHOD extendedGuardDetached
//! HAS TypedArrayDoubleLoadElement
//! COUNT_GE F64ToI32Trunc 1
function extendedGuardDetached(a, b, i, k) {
    return 255 & a[i];
}

const a = new Uint32Array([0, 1]);
const offHeap = new Uint32Array(5000);
offHeap[4097] = 1;
for (let n = 0; n < 1000; n++) {
    guardUint32Bounds(a, n & 1);
    guardUint32Negative(a, n & 1);
    guardUint32Fraction(a, n & 1);
    guardUint32NegativeFraction(a, n & 1);
    guardUint32NaN(a, n & 1);
    guardUint32Infinity(a, n & 1);
    guardUint32Array(a, n & 1);
    guardUint32Detached(offHeap, 4096 + (n & 1));
}
ArkTools.arkSteedCompileSync(guardUint32Bounds);
ArkTools.arkSteedCompileSync(guardUint32Negative);
ArkTools.arkSteedCompileSync(guardUint32Fraction);
ArkTools.arkSteedCompileSync(guardUint32NegativeFraction);
ArkTools.arkSteedCompileSync(guardUint32NaN);
ArkTools.arkSteedCompileSync(guardUint32Infinity);
ArkTools.arkSteedCompileSync(guardUint32Array);
ArkTools.arkSteedCompileSync(guardUint32Detached);
print('bounds=' + guardUint32Bounds(a, a.length));
print('negative=' + guardUint32Negative(a, -1));
print('fraction=' + guardUint32Fraction(new Float64Array([3.75]), 0));
print('negative-fraction=' + guardUint32NegativeFraction(new Float64Array([-2.75]), 0));
print('nan=' + guardUint32NaN(new Float64Array([NaN]), 0));
print('infinity=' + guardUint32Infinity(new Float64Array([Infinity]), 0));
print('array=' + guardUint32Array([4294967295], 0));
ArkTools.arrayBufferDetach(offHeap.buffer);
let detachedThrows = false;
try {
    guardUint32Detached(offHeap, 4096);
} catch (e) {
    detachedThrows = e instanceof TypeError;
}
print('detached=' + detachedThrows);

const b = new Uint32Array(2);
const index = 0;
a[index] = 0; a[index + 1] = 1; b[index] = 1; b[index + 1] = 0;
const offA = new Uint32Array(5000); const offB = new Uint32Array(5000);
offA[4097] = 1; offB[4096] = 1;
for (let n = 0; n < 1000; n++) {
    extendedGuardBounds(a,b,n&1,1);
    extendedGuardNegative(a,b,n&1,1);
    extendedGuardFraction(a,b,n&1,1);
    extendedGuardArray(a,b,n&1,1);
    extendedGuardLeftScalarString(a,b,n&1,1);
    extendedGuardLeftScalarFraction(a,b,n&1,1);
    extendedGuardLeftScalarUndefined(a,b,n&1,1);
    extendedGuardRightScalarString(a,b,n&1,1);
    extendedGuardRightScalarFraction(a,b,n&1,1);
    extendedGuardRightScalarUndefined(a,b,n&1,1);
    extendedGuardRightEffect(a,b,n&1,1);
    extendedGuardLeftEffect(a,b,n&1,1);
    extendedGuardLeftShiftEffect(a,b,n&1,1);
    extendedGuardBothIndexEffect(a,b,n&1,0);
    extendedGuardThrowLeft(a,b,n&1,1);
    extendedGuardThrowRight(a,b,n&1,1);
    extendedGuardDetached(offA,offB,4096+(n&1),1);
}
ArkTools.arkSteedCompileSync(extendedGuardBounds);
ArkTools.arkSteedCompileSync(extendedGuardNegative);
ArkTools.arkSteedCompileSync(extendedGuardFraction);
ArkTools.arkSteedCompileSync(extendedGuardArray);
ArkTools.arkSteedCompileSync(extendedGuardLeftScalarString);
ArkTools.arkSteedCompileSync(extendedGuardLeftScalarFraction);
ArkTools.arkSteedCompileSync(extendedGuardLeftScalarUndefined);
ArkTools.arkSteedCompileSync(extendedGuardRightScalarString);
ArkTools.arkSteedCompileSync(extendedGuardRightScalarFraction);
ArkTools.arkSteedCompileSync(extendedGuardRightScalarUndefined);
ArkTools.arkSteedCompileSync(extendedGuardRightEffect);
ArkTools.arkSteedCompileSync(extendedGuardLeftEffect);
ArkTools.arkSteedCompileSync(extendedGuardLeftShiftEffect);
ArkTools.arkSteedCompileSync(extendedGuardBothIndexEffect);
ArkTools.arkSteedCompileSync(extendedGuardThrowLeft);
ArkTools.arkSteedCompileSync(extendedGuardThrowRight);
ArkTools.arkSteedCompileSync(extendedGuardDetached);
{

    print("extendedGuardBounds=" + extendedGuardBounds(a,b,2,1));
}
{

    print("extendedGuardNegative=" + extendedGuardNegative(a,b,-1,1));
}
{
    const changed = new Float64Array([3.75]);
    print("extendedGuardFraction=" + extendedGuardFraction(changed,b,0,1));
}
{
    const changed = [4294967295];
    print("extendedGuardArray=" + extendedGuardArray(changed,b,0,1));
}
{
    a[0] = 4294967295;
    print("extendedGuardLeftScalarString=" + extendedGuardLeftScalarString(a,b,0,"3"));
}
{
    a[0] = 4294967295;
    print("extendedGuardLeftScalarFraction=" + extendedGuardLeftScalarFraction(a,b,0,3.75));
}
{
    a[0] = 4294967295;
    print("extendedGuardLeftScalarUndefined=" + extendedGuardLeftScalarUndefined(a,b,0,undefined));
}
{
    a[0] = 4294967295;
    print("extendedGuardRightScalarString=" + extendedGuardRightScalarString(a,b,0,"3"));
}
{
    a[0] = 4294967295;
    print("extendedGuardRightScalarFraction=" + extendedGuardRightScalarFraction(a,b,0,3.75));
}
{
    a[0] = 4294967295;
    print("extendedGuardRightScalarUndefined=" + extendedGuardRightScalarUndefined(a,b,0,undefined));
}
{
    a[0] = 4294967295; let calls = 0; const scalar = {valueOf() { calls++; a[0] = 0; return 3; }};
    const result = extendedGuardRightEffect(a,b,0,scalar); print("extendedGuardRightEffect=" + [result,calls,a[0]].join(","));
}
{
    a[0] = 4294967295; let calls = 0; const scalar = {valueOf() { calls++; a[0] = 0; return 3; }};
    const result = extendedGuardLeftEffect(a,b,0,scalar); print("extendedGuardLeftEffect=" + [result,calls,a[0]].join(","));
}
{
    a[0] = 2; let calls = 0; const scalar = {valueOf() { calls++; a[0] = 0; return 3; }};
    const result = extendedGuardLeftShiftEffect(a,b,0,scalar); print("extendedGuardLeftShiftEffect=" + [result,calls,a[0]].join(","));
}
{
    a[0] = 4294967295; b[0] = 3; let calls = 0; const key = {toString() { calls++; a[0] = 0; return "0"; }};
    const result = extendedGuardBothIndexEffect(a,b,0,key); print("extendedGuardBothIndexEffect=" + [result,calls,a[0]].join(","));
}
{
    a[0] = 4294967295; let calls = 0; const scalar = {valueOf() { calls++; a[0] = 0; throw new TypeError("conversion"); }};
    let threw = false; try { extendedGuardThrowLeft(a,b,0,scalar); } catch (e) { threw = e instanceof TypeError; } print("extendedGuardThrowLeft=" + [threw,calls,a[0]].join(","));
}
{
    a[0] = 4294967295; let calls = 0; const scalar = {valueOf() { calls++; a[0] = 0; throw new TypeError("conversion"); }};
    let threw = false; try { extendedGuardThrowRight(a,b,0,scalar); } catch (e) { threw = e instanceof TypeError; } print("extendedGuardThrowRight=" + [threw,calls,a[0]].join(","));
}
{
    ArkTools.arrayBufferDetach(offA.buffer);
    let threw = false; try { extendedGuardDetached(offA,offB,4096,1); } catch (e) { threw = e instanceof TypeError; } print("extendedGuardDetached=" + threw);
}
