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
 * @tc.name:primitiveic
 * @tc.desc:test primitiveic
 * @tc.type: FUNC
 */

// for number_ic
const numObj1 = 10928;
const numObj2 = 123.456;
const numObj3 = new Number(42);
for (let i = 0; i < 100; i++) {
   let res1 = numObj1.toString();
   let res2 = numObj2.toPrecision(4);
   let res3 = numObj3.valueOf();
}

// for number_ic & string_ic hasAccessor
{
  function foo() { return -4096;}
  Object.defineProperty(String.prototype, "a", {get:foo});
  Object.defineProperty(Number.prototype, "b", {get:foo});
  for (let i = 0; i < 50; i++)
  {
      const num = 123456;
      const str = "normallize";
      num.a;
      str.b;
  }
  Object.defineProperty(Number.prototype, "c", {value:"123"});
  for (let i = 0; i < 50; i++)
  {
      const num = 123456;
      const str = "normallize";
      num.a;
      str.b;
  }
}

// for number_ic & string_ic notHasAccessor
{
  Object.defineProperty(String.prototype, "d", {value:"456"});
  Object.defineProperty(Number.prototype, "e", {value:"789"});
  for (let i = 0; i < 50; i++)
  {
      const num = 123456;
      const str = "normallize";
      num.d;
      str.e;
  }
}

print("primitiveic load success")

for(let i=0;i<2;i++){
  let obj={};
  let x=obj.__proto__;
  Object.freeze(x);
}
print("load ic by name test2 success!")
