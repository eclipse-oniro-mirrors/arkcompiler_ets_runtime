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
//! METHOD floatPhiWithTruncation
//! HAS Phi
//! HAS DoubleToInt32
//! METHOD intPhiFeedsFloat
//! COUNT_GE Phi 2
//! HAS I32ToF64
//! METHOD knownNumberEdge
//! HAS Phi
//! HAS CheckedNumberToF64
//! HAS_NOT TaggedNumberToF64
//! METHOD knownIntEdge
//! HAS TaggedIntToI32
//! METHOD taggedAlternativeMerge
//! COUNT_GE Phi 2
//! HAS I32ToTaggedInt
//! METHOD toBooleanPhi
//! HAS BranchIfInt32Compare
//! HAS_NOT I32ConditionCheck
//! HAS_NOT TaggedNumberToBoolean
//! METHOD negativeZeroPhi
//! HAS Phi
//! HAS F64Div
//! METHOD largeIntPhi
//! HAS Phi
//! HAS I32ToF64
//! METHOD floatConstantPhi
//! HAS Phi
//! HAS DoubleToInt32
//! METHOD deoptFrameRewrite
//! HAS Phi
//! HAS CallKnownJSFunction
//! METHOD storeRetaggedPhi
//! HAS StoreTaggedField
//! HAS_NOT StoreTaggedFieldWithBarrier

function floatPhiWithTruncation(value)
{
    const number = value + 1;
    const merged = value ? number : 0;
    const floatUse = merged + 1.5;
    const truncatedUse = merged ^ 25;
    return floatUse + truncatedUse;
}

function intPhiFeedsFloat(value, holder)
{
    const integer = value | 0;
    const intPhi = value ? integer : 42;
    holder.value = intPhi + 1;
    const floatPhi = value ? intPhi : integer;
    return floatPhi + 4.5;
}

function knownNumberEdge(value, chooseOriginal)
{
    const checked = value * 1.0;
    const merged = chooseOriginal ? value : checked;
    return merged + 0.5;
}

function knownIntEdge(value, chooseOriginal)
{
    const checked = value | 0;
    const merged = chooseOriginal ? value : checked;
    return (merged | 0) + 1;
}

function taggedAlternativeMerge(useFirst, callback)
{
    let sum = 0;
    for (let i = 0; i < 6; i++) {
        sum += i;
    }
    if (useFirst) {
        callback(sum);
    } else {
        callback(sum);
    }
    callback(sum);
    return sum;
}

function toBooleanPhi(limit)
{
    let value = 0;
    for (let i = 0; i < limit; i++) {
        value += i;
    }
    return value ? 1 : 0;
}

function negativeZeroPhi(useNegativeZero)
{
    const value = useNegativeZero ? -0.0 : 1.0;
    return 1.0 / value;
}

function largeIntPhi(useSmall)
{
    const value = useSmall ? 42 : 2000000000;
    const floatUse = value + 1.5;
    const truncatedUse = value ^ 25;
    return floatUse + truncatedUse;
}

function floatConstantPhi(useFirst)
{
    const value = useFirst ? 0.5 : 1.5;
    const truncatedUse = value ^ 2;
    const floatUse = value + 2.5;
    return truncatedUse + floatUse;
}

function deoptFrameRewrite(value, callback)
{
    const largeInt = value ^ 1073741824;
    const merged = value ? largeInt : 4;
    const truncated = merged | 0;
    callback();
    return truncated;
}

function storeRetaggedPhi(holder, chooseZero)
{
    let value = chooseZero ? 0 : 4.2;
    value |= 0;
    holder.value = value;
    return value;
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

let ok = true;
const holder = { value: "tagged" };
for (let i = 0; i < 100; i++) {
    const flag = (i & 1) === 0;
    ok = floatPhiWithTruncation(1.5) === 31 && ok;
    ok = intPhiFeedsFloat(7, holder) === 11.5 && ok;
    ok = knownNumberEdge(2.25, flag) === 2.75 && ok;
    ok = knownIntEdge(7, flag) === 8 && ok;
    ok = taggedAlternativeMerge(flag, recordCall) === 15 && ok;
    ok = toBooleanPhi(6) === 1 && ok;
    ok = negativeZeroPhi(flag) === (flag ? -Infinity : 1) && ok;
    ok = largeIntPhi(flag) === ((flag ? 42 : 2000000000) + 1.5 + ((flag ? 42 : 2000000000) ^ 25)) && ok;
    ok = floatConstantPhi(flag) === (flag ? 5 : 7) && ok;
    ok = deoptFrameRewrite(1, recordCall) === 1073741825 && ok;
    ok = storeRetaggedPhi(holder, flag) === (flag ? 0 : 4) && ok;
}

ok = callbackCount === 300 && callbackSum === 3000 && ok;

const methods = [floatPhiWithTruncation, intPhiFeedsFloat, knownNumberEdge, knownIntEdge,
    taggedAlternativeMerge, toBooleanPhi, negativeZeroPhi, largeIntPhi, floatConstantPhi,
    deoptFrameRewrite, storeRetaggedPhi];
for (const method of methods) {
    ArkTools.arkSteedCompileSync(method);
    ArkTools.waitJitCompileFinish(method);
    ok = ArkTools.isAOTCompiled(method) && ok;
}

ok = floatPhiWithTruncation(1.5) === 31 && ok;
ok = floatPhiWithTruncation(0) === 26.5 && ok;
ok = intPhiFeedsFloat(7, holder) === 11.5 && ok;
ok = intPhiFeedsFloat(0, holder) === 4.5 && holder.value === 43 && ok;
ok = knownNumberEdge(2.25, true) === 2.75 && ok;
ok = knownNumberEdge(2.25, false) === 2.75 && ok;
ok = knownNumberEdge("2.25", true) === "2.250.5" && ok;
ok = knownIntEdge(7, true) === 8 && ok;
ok = knownIntEdge(7, false) === 8 && ok;
ok = taggedAlternativeMerge(true, recordCall) === 15 && ok;
ok = taggedAlternativeMerge(false, recordCall) === 15 && ok;
ok = toBooleanPhi(0) === 0 && ok;
ok = toBooleanPhi(6) === 1 && ok;
ok = negativeZeroPhi(true) === -Infinity && ok;
ok = negativeZeroPhi(false) === 1 && ok;
ok = largeIntPhi(true) === 94.5 && ok;
ok = largeIntPhi(false) === (2000000001.5 + (2000000000 ^ 25)) && ok;
ok = floatConstantPhi(true) === 5 && ok;
ok = floatConstantPhi(false) === 7 && ok;
ok = deoptFrameRewrite(1, recordCall) === 1073741825 && ok;
ok = deoptFrameRewrite(0, recordCall) === 4 && ok;
ok = storeRetaggedPhi(holder, true) === 0 && holder.value === 0 && ok;
ok = storeRetaggedPhi(holder, false) === 4 && holder.value === 4 && ok;

ok = callbackCount === 306 && callbackSum === 3060 && ok;
print(ok ? "PASS" : "FAIL");
