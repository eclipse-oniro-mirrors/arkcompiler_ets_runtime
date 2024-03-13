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
 * @tc.name:forin_non_empty_prototype
 * @tc.desc:test forin_non_empty_prototype
 * @tc.type: FUNC
 * @tc.require: issueI89SMQ
 */

declare function assert_equal(a: Object, b: Object):void;

let grandparent = {
    "a": 1,
    "d": undefined,
    1: 2,
    2: 3
}

let parent = {
    "c": undefined,
    "a": 1,
    "b": undefined,
    1: 2
}

let own = {
    "a": 1,
    "b": 1,
    1: 2,
}
own.__proto__ = parent
parent.__proto__ = grandparent

var testArrary = ["1", "a", "b", "c", "2", "d"];
var j = 0;
for (let i in own) {
    assert_equal(i, testArrary[j]);
    j++;
}
