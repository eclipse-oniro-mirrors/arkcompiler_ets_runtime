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

function buildString(n: number): string {
    var result = '';
    for (var i = 0; i < n; ++i) {
        result += String.fromCharCode(97 + (i % 26));
    }
    return result;
}

var s1 = buildString(10);
ArkTools.jitCompileAsync(buildString);
var compileRet = ArkTools.waitJitCompileFinish(buildString);
var s2 = buildString(10);
print(compileRet);
print(s1);
print(s2);
print(s1 === s2);
