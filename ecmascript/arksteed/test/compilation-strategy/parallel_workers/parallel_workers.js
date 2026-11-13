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

//! PARAMS --compiler-enable-async-copytofort=true --compiler-jit-hotness-threshold=60000 --enable-force-gc=true
//! COMPILE_COUNT addWorker 1
//! COMPILE_COUNT multiplyWorker 1
//! COMPILE_COUNT constantWorker 1
//! COMPILE_COUNT syncWorker 1

function syncWorker(x) { return x - 5; }
function addWorker(x) { return x + 7; }
function multiplyWorker(x) { return x * 3; }
function constantWorker(x) { return "parallel-root:" + x; }

for (let i = 0; i < 32; ++i) {
    syncWorker(i);
    addWorker(i);
    multiplyWorker(i);
    constantWorker(i);
}
let ok = ArkTools.arkSteedCompileAsync(addWorker);
ok = ArkTools.arkSteedCompileAsync(multiplyWorker) && ok;
ok = ArkTools.arkSteedCompileAsync(constantWorker) && ok;
// Exercise roots both while workers compile and while detached results await install.
ArkTools.forceFullGC();
ok = ArkTools.arkSteedCompileSync(addWorker) && ok;
ok = ArkTools.arkSteedCompileSync(multiplyWorker) && ok;
ok = ArkTools.arkSteedCompileSync(constantWorker) && ok;
ok = ArkTools.arkSteedCompileSync(syncWorker) && ok;
ArkTools.waitAllJitCompileFinish();
ArkTools.forceFullGC();
ok = ArkTools.arkSteedIsCompiled(addWorker) && ok;
ok = ArkTools.arkSteedIsCompiled(multiplyWorker) && ok;
ok = ArkTools.arkSteedIsCompiled(constantWorker) && ok;
ok = addWorker(12) === 19 && multiplyWorker(12) === 36 && constantWorker(12) === "parallel-root:12" && ok;
ok = ArkTools.arkSteedIsCompiled(syncWorker) && syncWorker(12) === 7 && ok;
print(ok ? "PASS" : "FAIL");
