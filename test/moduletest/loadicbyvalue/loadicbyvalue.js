/*
 * Copyright (c) 2021 Huawei Device Co., Ltd.
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
 * @tc.name:loadicbyvalue
 * @tc.desc:test loadicbyvalue
 * @tc.type: FUNC
 * @tc.require: issueI5NO8G
 */
var i = 3;
var obj = {3:"icsuccess"};
function func1(a)
{
    var b = a[i];
}
for (let j = 300; j > 0; j--)
{
    func1(obj);
}
print(obj[i]);

// PoC testcase
const arr = []
for (let i = 0; i < 20; i ++) {
    const v0 = "p" + i;
    const v1 = Symbol.iterator;
    const v2 = {
        [v1]() {},
    };
    arr[v0] = i;
}
const v3 = new Uint8Array(128);
v3[arr];
print("test successful !!!");

var obj1 = {
    0: 0,
    2147483647: 1,
    2147483648: 2,
    4294967295: 3,
    4294967296: 4,
}

for (let item in obj1) {
    print(item + " " + obj1[item]);
}