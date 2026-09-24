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

// @ts-nocheck
declare function print(value: any): void;

//! PARAMS --compiler-jit-hotness-threshold=60000 --enable-force-gc=true
//! METHOD incrementElement
//! HAS EnsureWritableElements
//! HAS_NOT DeoptIfCOWElements
//! CODEGEN_COUNT CopyCOWElements deferred call 1
//! HAS_NOT CallCommonStub CopyCOWElements
//! HAS StoreTaggedElement
//! HAS_NOT CallCommonStub SetPropertyByValue
//! HAS_NOT CallRuntime CheckAndCopyArray
function incrementElement(array, index) {
    let value = array[index] + 1;
    array[index] = value;
    return array[index];
}

function literal() {
    return [0, 0, 0, 0];
}

let warm = literal();
for (let i = 0; i < 20; i++) {
    incrementElement(warm, 1);
}
ArkTools.arkSteedCompileSync(incrementElement);

let fresh = literal();
let sibling = literal();
ArkTools.forceFullGC();
print(incrementElement(fresh, 1));
print(incrementElement(fresh, 1));
ArkTools.forceFullGC();
print(ArkTools.arkSteedIsCompiled(incrementElement));
print(fresh.join(",") === "0,2,0,0" && sibling.join(",") === "0,0,0,0");

//! METHOD storeAliasedString
//! HAS EnsureWritableElements
//! HAS_NOT DeoptIfCOWElements
//! CODEGEN_COUNT CopyCOWElements deferred call 1
//! HAS_NOT CallCommonStub CopyCOWElements
//! HAS StoreTaggedElementWithBarrier
//! HAS_NOT CallCommonStub SetPropertyByValue
function storeAliasedString(array, alias, value) {
    const before = alias[0];
    array[0] = value;
    return before + ":" + alias[0];
}

function strings() {
    return ["first", "second"];
}

let warmStrings = strings();
for (let i = 0; i < 20; ++i) {
    storeAliasedString(warmStrings, warmStrings, "warm");
}
ArkTools.arkSteedCompileSync(storeAliasedString);
let freshStrings = strings();
let otherStrings = strings();
ArkTools.forceFullGC();
let youngString = "x".repeat(100);
print(storeAliasedString(freshStrings, freshStrings, youngString) === "first:" + youngString);
ArkTools.forceFullGC();
print(freshStrings[0] === youngString && freshStrings[1] === "second" && otherStrings[0] === "first");
print(ArkTools.arkSteedIsCompiled(storeAliasedString));
