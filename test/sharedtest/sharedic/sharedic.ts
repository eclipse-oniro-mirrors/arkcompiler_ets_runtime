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
 * @tc.name:sharedic
 * @tc.desc:test sharedic
 * @tc.type: FUNC
 * @tc.require: issueI8QUU0
 */

// @ts-nocheck
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
 * @tc.name:sharedic
 * @tc.desc:test sharedic
 * @tc.type: FUNC
 * @tc.require: issueI8QUU0
 */

// @ts-nocheck
declare function print(str: any): string;

class SFoo {
  propString: string = "I'm simple sendable's propString";
  constructor() {
    "use sendable"
  }
  testic() {
    let a = this;
    for (var i: number = 0; i < 100; i++) {
      a.propString = "a" ;
    }
  }
  
}
let t = new SFoo();
t.testic(); // sharedfunction not enter ic

class SClass {
  str: string = "I'm simple sendable's propString";
  constructor() {
    "use sendable"
  }
}

let s = new SClass()
for (var i: number = 0; i < 100; i++) {
  s.str;
}

let sobj = new SClass();
for (let i = 0; i < 200; i++) {
}
sobj.__proto__; // not SetProtoCell
{
  let arr = new SendableBigUint64Array(5);
  arr[0]=1n;
  print(arr[0]);
}

/*
 * @tc.name:sharedset_proto_ic
 * @tc.desc:test SharedSet proto ic (method lookup and size accessor after whitelist enabled)
 * @tc.type: FUNC
 * @tc.require: issueI8QUU0
 */
// Multi-round SendableSet method calls and size reads: proto IC must be installed after
// whitelist enabled (ic_runtime.cpp / napi_ic_runtime.cpp), and cached lookup must keep
// exactly the same behavior as the uncached C++ path.
{
  let ss = new SendableSet();
  for (let i = 0; i < 200; i++) {
    ss.add(i);
  }
  print(ss.size);
  print(ss.has(100));
  print(ss.has(1000));
  let sum = 0;
  ss.forEach(function (v: number) { sum += v; });
  print(sum);
  let vals = ss.values();
  let cnt = 0;
  let entry = vals.next();
  while (!entry.done) {
    cnt++;
    entry = vals.next();
  }
  print(cnt);
  print(ss.delete(100));
  print(ss.has(100));
  print(ss.size);
  ss.clear();
  print(ss.size);
  // repeat method lookups after mutation to re-exercise cached proto handlers
  for (let i = 0; i < 50; i++) {
    ss.add(i);
  }
  print(ss.size);
  print(ss.has(49));
}

print("shared ic over");