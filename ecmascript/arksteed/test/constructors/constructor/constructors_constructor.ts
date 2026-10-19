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

class Base {
  constructor(a, b) {
    this.a_ = a;
    this.b_ = b;
  }
};

class Derived extends Base {
};

class Derived1 extends Base {
  constructor(a, b) {
    super(a, b);
  }
};


function Test() {
  var b = new Base("sun", 100);
  var d = new Derived("zibo", 200);
  var d1 = new Derived1("zibobo", 300);
  print(b.a_);
  print(b.b_);
  print(d.a_);
  print(d.b_);
  print(d1.a_);
  print(d1.b_);
}

Test();
// G11.6 additive evidence (arksteed-g116-call-closure-and-additive-evidence#C1,#C3):
// the compiled caller `Test` constructs Base/Derived/Derived1 through its own
// dump, so its new-object form is the address evidence of this leaf. Program
// output is unchanged.
//! COMPILE_COUNT_GE Test 1
//! EXEC_COUNT_GE *Test 1
//! DEOPT_COUNT *Deoptimize* 0
//! METHOD Test
//! COUNT CallNewObjRangeBase 3
//! COUNT CallNewObjRangeDerived 3
//! HAS_NOT CallRuntime OptNewObjRange

ArkTools.arkSteedCompileSync(Test);
ArkTools.waitJitCompileFinish(Test);
print(true);
Test();