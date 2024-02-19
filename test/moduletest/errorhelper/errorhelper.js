/*
 * Copyright (c) 2023 Huawei Device Co., Ltd.
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
 * @tc.name:error_helper
 * @tc.desc:test error
 * @tc.type: FUNC
 */

const v34 = [2147483648,238049746,1260900180,371215732,1655235460,-65536,-94405277,536870888,-1073741824,-65537];
class C37 {
}
const v40 = new C37();
try {
v40.constructor(v40, v34, C37, v40);
} catch(e45) {
const v46 = e45.toString;
const v50 = v46.call(v34);
print(v50);
}

// Be sure to put this case at the end because this case will change prototype of error constructor
try {
    0();
} catch (err) {
    err.constructor.prototype.name = 123456789;
}
try {
    0();
} catch (e) {}
