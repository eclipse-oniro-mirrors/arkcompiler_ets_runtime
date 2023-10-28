/*
 * Copyright (c) 2022 Huawei Device Co., Ltd.
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

/*
 * @tc.name:builtins
 * @tc.desc:test builtins
 * @tc.type: FUNC
 * @tc.require: issueI5NO8G
 */
let result = Number.parseInt("16947500000");
print("builtins number start");
print("parseInt result = " + result);
print(1 / 0.75 * 0.6);
print(1 / (-1 * 0));

// math.atanh
try {
    const bigIntTest = -2147483647n;
    const test = Math.atanh(bigIntTest);
} catch(e) {
    print(e);
};

var s = (2.2250738585072e-308).toString(36)
print(s)

print(Number.parseInt("4294967294"))
print(Number.parseInt("2147483648"))

print("builtins number end");