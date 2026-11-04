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

// Positive lazy-deopt evidence (ISSUE-0018): modifying a prototype invalidates
// the compiled method, and the next entry leaves the generated code through a
// lazy deopt. The event is counted exactly once (DEOPT_COUNT); the annotation
// implies --compiler-enable-lazy-deopt-trace=true for the case, so lazy events
// are observable without any case-supplied option. The mutation happens inside
// a nested call, so the traced event is the multi-line log message (event line
// plus one `    at <fn> (<file>:<line>:<col>)` line per frame) that the runner
// filters out of the stdout comparison as one record.

//! PARAMS --compiler-jit-hotness-threshold=60000

//! METHOD Test
//! COMPILE_COUNT_GE Test 1
//! EXEC_COUNT_GE *Test 1
//! DEOPT_COUNT Lazy Deoptimize* 1

function Test(obj) {
    return obj.x;
}

class A {}
class B extends A {}
class C extends B {}

A.prototype.x = 1;
const c = new C();

Test(c);
ArkTools.arkSteedCompileSync(Test);
print(Test(c));
print(ArkTools.arkSteedIsCompiled(Test));

function level1() {
    return level2();
}

function level2() {
    return level3();
}

function level3() {
    B.prototype.x = 2;
    return 0;
}

level1();
print(Test(c));
print(ArkTools.arkSteedIsCompiled(Test));
print("OK");
