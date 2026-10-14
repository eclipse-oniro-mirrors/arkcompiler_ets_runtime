
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

//! PARAMS --compiler-jit-hotness-threshold 60000

const REP = 60000;

function test() {
    function foo() { }
    var f = new foo();
    f.notfound;
}

for (var i = 0; i < REP; ++i) {
    test();
}
ArkTools.arkSteedCompileSync(test);

// G11.2 MEGA precondition: the named load of `foo` on the freshly created
// instance misses every property. The site is warmed while the raised hotness
// threshold keeps the warm-up interpreted, then installed; the installed code
// of `test` runs the second warm-up loop with the same miss and produces no
// deopt event.
//! COMPILE_COUNT_GE *test* 1
//! EXEC_COUNT_GE *test* 1
//! DEOPT_COUNT *Deoptimize* 0

for (var i = 0; i < REP; ++i) {
    test();
}
