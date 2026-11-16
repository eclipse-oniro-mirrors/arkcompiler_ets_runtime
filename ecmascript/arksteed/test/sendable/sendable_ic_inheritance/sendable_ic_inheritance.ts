/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
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

// Test: sendable inheritance proto IC — a child sendable class instance
// loading a parent prototype method hits the PrototypeHandler (holder =
// Parent.prototype, cross-level chain) after distillation, and the answer
// stays correct after ArkSteed compilation.
//! METHOD loadInheritedMethod
//! HAS LoadTaggedField
//! COMPILE_COUNT_GE *loadInheritedMethod* 1

class Parent {
    p: number = 3;
    constructor() { "use sendable"; }
    pmethod(): number { return 42; }
}

class Child extends Parent {
    c: number = 5;
    constructor() { "use sendable"; super(); }
}

let child = new Child();

function loadInheritedMethod(o) {
    let r;
    for (let i = 0; i < 200; i++) { r = o.pmethod; }
    return r;
}

print("before compile: " + (loadInheritedMethod(child) !== undefined));
ArkTools.arkSteedCompileSync(loadInheritedMethod);
ArkTools.waitJitCompileFinish(loadInheritedMethod);
print(ArkTools.isAOTCompiled(loadInheritedMethod));
print("after compile: " + (loadInheritedMethod(child) !== undefined));
print("method call: " + child.pmethod());
