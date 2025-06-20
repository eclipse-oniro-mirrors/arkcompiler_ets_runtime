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

// @ts-nocheck
//test taggedstringisobject intrinsic
declare function print(arg: any): string;

let a = {x:1, y:2};

function foo(obj, key) {
    return obj[key];
}

for (let i = 0; i < 1000; i++) {
    foo(a, "x");
}
ArkTools.jitCompileAsync(foo);
print(ArkTools.waitJitCompileFinish(foo));
print(foo(a, "x"));
//test isjscowarray intrinsics
let b = [1, 2, 3 , 4];

function foo2(obj, key, value) {
    return obj[key] = value;
}

for (let i = 0; i < 1000; i++) {
    foo2(b, 2, 5);
}
ArkTools.jitCompileAsync(foo2);
print(ArkTools.waitJitCompileFinish(foo2));
print(foo2(b, 2, 5));