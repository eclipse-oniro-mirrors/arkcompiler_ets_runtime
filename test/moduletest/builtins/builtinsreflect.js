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
 * @tc.name:builtinsreflect
 * @tc.desc:test builtinsreflect.set
 * @tc.type: FUNC
 * @tc.require: 
 */


print("builtins reflect start");

const v0 = 102630708;
let v55 = [];
let v56 = Object.create(v55)
Reflect.set(v56, "length", v0)
Reflect.set(v55, "length", v0, v56)
print("v56.length",v56.length)

print("builtins reflect end");
