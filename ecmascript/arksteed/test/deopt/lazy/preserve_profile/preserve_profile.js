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

//! PARAMS --compiler-jit-hotness-threshold=100000 --compiler-max-inline-bytecodes=0
//! DEOPT_COUNT Lazy* 2

class A {}
class B extends A {}
class C extends B {}
class D extends C {}
class E extends D {}
class F extends E {}
A.prototype.x = 10;
const receiver = new F();
function load(receiver) { return receiver.x; }
for (let i = 0; i < 32; i++) {
    load(receiver);
}
print(ArkTools.getICState(load, 0, 0));
ArkTools.arkSteedCompileSync(load);
print(ArkTools.arkSteedIsCompiled(load));
print(load(receiver));
C.prototype.x = 20;
// Inspect feedback before executing or compiling load again.
print(ArkTools.arkSteedIsCompiled(load));
print(ArkTools.isProfileTypeInfoValid(load));
print(ArkTools.getICState(load, 0, 0));
print(load(receiver));
ArkTools.arkSteedCompileSync(load);
print(ArkTools.arkSteedIsCompiled(load));
print(load(receiver));
E.prototype.x = 30;
print(ArkTools.arkSteedIsCompiled(load));
print(ArkTools.isProfileTypeInfoValid(load));
print(ArkTools.getICState(load, 0, 0));
print(load(receiver));
