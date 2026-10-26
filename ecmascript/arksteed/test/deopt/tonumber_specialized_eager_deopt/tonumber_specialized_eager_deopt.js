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

//! PARAMS --compiler-jit-hotness-threshold=2000 --enable-pgo-profiler=true

//! METHOD boolSpec
//! HAS DeoptIfNotBoolean

function boolSpec(x) { return +x; }

for (let i = 0; i < 2000; i++) {
    boolSpec(i % 2 === 0);
}
ArkTools.arkSteedCompileSync(boolSpec);

print(boolSpec(true));
print(boolSpec(false));
print(boolSpec("12"));
print(boolSpec(null));
let calls = 0;
let obj = { valueOf() { calls++; return 42; } };
print(boolSpec(obj));
print(calls);
for (let i = 0; i < 100; i++) {
    if (boolSpec("3") !== 3) {
        print("FAIL");
    }
}
print(boolSpec(true));
print("OK");
