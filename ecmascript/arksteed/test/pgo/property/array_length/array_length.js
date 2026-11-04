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
//! METHOD readLength
//! COUNT LoadInt32Field 1
//! COUNT U32ToTaggedNumber 1
//! HAS_NOT CallCommonStub GetPropertyByName
//! METHOD repeatLength
//! COUNT LoadInt32Field 1
//! COUNT U32ToTaggedNumber 1
//! HAS StoreTaggedElement
//! HAS_NOT CallCommonStub GetPropertyByName
//! METHOD mixedLength
//! HAS BranchIfHClassIn
//! HAS U32ToTaggedNumber
//! HAS_NOT CallCommonStub GetPropertyByName
//! METHOD lengthByKey
//! HAS U32ToTaggedNumber
//! HAS_NOT CallCommonStub GetPropertyByValue
//! METHOD lengthLoop
//! HAS JumpLoop
//! HAS U32ToTaggedNumber
//! HAS_NOT CallCommonStub GetPropertyByName
//! METHOD resizeThroughAlias
//! COUNT LoadInt32Field 2
//! METHOD resizeThroughCall
//! COUNT LoadInt32Field 2
//! METHOD resizeInLoop
//! HAS U32ToTaggedNumber
//! HAS_NOT CallCommonStub GetPropertyByName
//! METHOD largeLength
//! COUNT U32ToTaggedNumber 1
//! HAS_NOT CheckedNonNegativeI32ToTaggedInt
//! HAS_NOT CallCommonStub GetPropertyByName

function readLength(arr) {
    return arr.length;
}

function repeatLength(arr) {
    const before = arr.length;
    arr[0] = 7;
    return before * 100 + arr.length;
}

function mixedLength(obj) {
    return obj.length;
}

function lengthByKey(arr, key) {
    return arr[key];
}

function lengthLoop(arr) {
    let n = 0;
    for (let i = 0; i < arr.length; ++i) {
        n += i;
    }
    return n;
}

function resizeThroughAlias(arr, alias) {
    const before = arr.length;
    alias.length = 2;
    return before * 100 + arr.length;
}

function resizeThroughCall(arr, callback) {
    const before = arr.length;
    callback(arr);
    return before * 100 + arr.length;
}

function resizeInLoop(arr, alias) {
    let n = 0;
    for (let i = 0; i < arr.length; ++i) {
        if (i === 0) {
            alias.length = 2;
        }
        ++n;
    }
    return n;
}

function largeLength(arr) {
    return arr.length;
}

function shorten(arr) {
    arr.length = 1;
}

const arr = [1, 2, 3, 4];
arr[0] = 1;
const storeArray = [1, 2, 3, 4];
storeArray.cacheTest = 1;
storeArray[0] = 1;
const other = [1, 2];
other.extra = 17;
const plain = {length: 9};
const large = new Array(0xffffffff);
// Keep sparse elements, but make named IC feedback available for the uint32 boundary checks.
ArkTools.toFastProperties(large);
for (let i = 0; i < 16; ++i) {
    readLength(arr);
    repeatLength(storeArray);
    mixedLength(arr);
    mixedLength(other);
    mixedLength(plain);
    lengthByKey(arr, "length");
    lengthLoop(arr);
    const input = [1, 2, 3, 4];
    resizeThroughAlias(input, input);
    input.length = 4;
    resizeThroughCall(input, shorten);
    input.length = 4;
    resizeInLoop(input, input);
    largeLength(large);
}
ArkTools.arkSteedCompileSync(readLength);
ArkTools.arkSteedCompileSync(repeatLength);
ArkTools.arkSteedCompileSync(mixedLength);
ArkTools.arkSteedCompileSync(lengthByKey);
ArkTools.arkSteedCompileSync(lengthLoop);
ArkTools.arkSteedCompileSync(resizeThroughAlias);
ArkTools.arkSteedCompileSync(resizeThroughCall);
ArkTools.arkSteedCompileSync(resizeInLoop);
ArkTools.arkSteedCompileSync(largeLength);

print(readLength(arr) === 4 && repeatLength(storeArray) === 404 ? "PASS" : "FAIL");
print(mixedLength(arr) === 4 && mixedLength(other) === 2 && mixedLength(plain) === 9 ? "PASS" : "FAIL");
print(lengthByKey(arr, "length") === 4 && lengthLoop(arr) === 6 ? "PASS" : "FAIL");
const input = [1, 2, 3, 4];
print(resizeThroughAlias(input, input) === 402 ? "PASS" : "FAIL");
input.length = 4;
print(resizeThroughCall(input, shorten) === 401 ? "PASS" : "FAIL");
input.length = 4;
print(resizeInLoop(input, input) === 2 ? "PASS" : "FAIL");

let ok = true;
for (const n of [0xffffffff, 0x80000000, 0x7fffffff, 0, 17]) {
    large.length = n;
    ok = ok && largeLength(large) === n;
}
print(ok ? "PASS" : "FAIL");

// A different key or receiver must not use the built-in length field.
print(lengthByKey(arr, "0") === 1 ? "PASS" : "FAIL");
let getterCalls = 0;
const getter = {get length() { ++getterCalls; return 13; }};
print(readLength(getter) === 13 && getterCalls === 1 ? "PASS" : "FAIL");
const proxy = new Proxy(arr, {get(target, key) { return key === "length" ? 23 : target[key]; }});
print(mixedLength(proxy) === 23 ? "PASS" : "FAIL");
