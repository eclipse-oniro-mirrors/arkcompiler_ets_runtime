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

let arr: number[] = [1, 2, 3, 4, 5, 6, 7, 8];

function doubleItems(items: number[]): number[] {
    return items.map(function (x: number) { return x * 2; });
}

function filterEven(items: number[]): number[] {
    return items.filter(function (x: number) { return x % 2 === 0; });
}

let doubled = doubleItems(arr);
ArkTools.jitCompileAsync(doubleItems);
ArkTools.jitCompileAsync(filterEven);
let compileRet1 = ArkTools.waitJitCompileFinish(doubleItems);
let compileRet2 = ArkTools.waitJitCompileFinish(filterEven);
let doubledAfter = doubleItems(arr);
let evens = filterEven(doubledAfter);
print(compileRet1);
print(compileRet2);
print(doubled.join(','));
print(doubledAfter.join(','));
print(evens.join(','));
