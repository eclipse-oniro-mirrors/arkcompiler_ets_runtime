/*
 * Copyright (c) 2024 Huawei Device Co., Ltd.
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
 * @tc.name:typedarrayfilter 
 * @tc.desc:test TypedArray.filter 
 * @tc.type: FUNC
 * @tc.require: Issue:IAO7E8
 */

try {
    let obj={__proto__:Uint16Array.prototype}
    obj.filter(obj);
    assert_unreachable();
} catch (e) {
    assert_equal(e instanceof TypeError, true);
}

test_end();