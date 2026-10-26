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
//! PARAMS --compiler-jit-hotness-threshold=60000
//! METHOD Test
//! COUNT CallNewObjRangeBase 1
//! COUNT CallNewObjRangeDerived 1
//! HAS_NOT CallRuntime OptNewObjRange
//! COMPILE_COUNT_GE *Test* 1
//! EXEC_COUNT *Test* 2
//! DEOPT_COUNT *Deoptimize* 1

class MyClass {}
class MyClass2 {}
function Test(ctor) {
    return new ctor();
}

Test(MyClass);
ArkTools.arkSteedCompileSync(Test);
ArkTools.waitJitCompileFinish(Test);

// The original target must finish in JIT before the target-change probe.
const original = Test(MyClass);
print(original instanceof MyClass && Object.getPrototypeOf(original) === MyClass.prototype);

// Changing the monomorphic target must deopt and construct the new class.
const changed = Test(MyClass2);
if (!(changed instanceof MyClass2) || Object.getPrototypeOf(changed) !== MyClass2.prototype) {
    throw new Error("newobjrange target change returned the wrong object");
}
