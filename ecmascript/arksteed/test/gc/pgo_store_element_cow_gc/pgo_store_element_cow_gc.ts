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

//! COMPILE_COUNT_GE *storeWithLiveValues* 1
//! EXEC_COUNT_GE *storeWithLiveValues* 1
//! DEOPT_COUNT *Deoptimize* 0

// @ts-nocheck
declare function print(value: any): void;

//! PARAMS --compiler-jit-hotness-threshold=60000 --enable-force-gc=true
// More than 50 elements makes CopyCOWElements allocate through NewTaggedArray's runtime path.
// With --enable-force-gc this exercises the caller's saved tagged roots during the copy.
function largeLiteral() {
    return [
        "first", "x", "x", "x", "x", "x", "x", "x",
        "x", "x", "x", "x", "x", "x", "x", "x",
        "x", "x", "x", "x", "x", "x", "x", "x",
        "x", "x", "x", "x", "x", "x", "x", "x",
        "x", "x", "x", "x", "x", "x", "x", "x",
        "x", "x", "x", "x", "x", "x", "x", "x",
        "x", "x", "x", "x", "x", "x", "x", "x",
        "x", "x", "x", "x", "x", "x", "x", "x"
    ];
}

// Keep heap pointers, an unboxed integer and a double live across the copying allocation.
//! METHOD storeWithLiveValues
//! HAS EnsureWritableElements
//! HAS_NOT CallCommonStub CopyCOWElements
function storeWithLiveValues(array, index, value, left, right, integer, number) {
    const rawInt = integer | 0;
    const rawDouble = number * 0.5;
    array[index] = value;
    return left.text + right.text + ":" + rawInt + ":" + rawDouble + ":" + array[index];
}

for (let i = 0; i < 20; ++i) {
    const array = largeLiteral();
    storeWithLiveValues(array, 0, "value", {text: "left"}, {text: "right"}, 123, 3.5);
}
ArkTools.arkSteedCompileSync(storeWithLiveValues);
ArkTools.waitJitCompileFinish(storeWithLiveValues);
let liveValuesOK = true;
for (let i = 0; i < 50; ++i) {
    const array = largeLiteral();
    const other = largeLiteral();
    const left = {text: "left" + i};
    const right = {text: "right" + i};
    const value = "value" + i;
    const result = storeWithLiveValues(array, 0, value, left, right, 123 + i, 3.5 + i);
    liveValuesOK = liveValuesOK &&
        result === left.text + right.text + ":" + (123 + i) + ":" + ((3.5 + i) * 0.5) + ":" + value &&
        other[0] === "first";
}
print(liveValuesOK);
print(ArkTools.arkSteedIsCompiled(storeWithLiveValues));
