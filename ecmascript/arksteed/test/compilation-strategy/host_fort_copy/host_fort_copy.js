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

//! PARAMS --compiler-enable-async-copytofort=false --compiler-jit-hotness-threshold=60000 --enable-force-gc=true
//! COMPILE_COUNT asyncFallback 1
//! COMPILE_COUNT syncFallback 1

function asyncFallback(x) { return x + 7; }
function syncFallback(x) { return x * 3; }

for (let i = 0; i < 32; ++i) {
    asyncFallback(i);
    syncFallback(i);
}
let ok = ArkTools.arkSteedCompileAsync(asyncFallback);
ArkTools.forceFullGC();
ok = ArkTools.arkSteedCompileSync(syncFallback) && ok;
ArkTools.waitAllJitCompileFinish();
ArkTools.forceFullGC();
ok = ArkTools.arkSteedIsCompiled(asyncFallback) && ArkTools.arkSteedIsCompiled(syncFallback) && ok;
ok = asyncFallback(12) === 19 && syncFallback(12) === 36 && ok;
print(ok ? "PASS" : "FAIL");
