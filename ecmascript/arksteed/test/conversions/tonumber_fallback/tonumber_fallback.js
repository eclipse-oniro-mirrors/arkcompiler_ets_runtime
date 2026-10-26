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

//! METHOD strPlain
//! HAS CallRuntime ToNumber
//! HAS_NOT DeoptIfNotNumber
//! HAS_NOT DeoptIfNotBoolean
//! HAS_NOT DeoptIfNotUndefinedOrNull
//! HAS_NOT DeoptIfNotBigInt

//! METHOD objPlain
//! HAS CallRuntime ToNumber
//! HAS_NOT DeoptIfNotNumber
//! HAS_NOT DeoptIfNotBoolean
//! HAS_NOT DeoptIfNotUndefinedOrNull
//! HAS_NOT DeoptIfNotBigInt

//! METHOD bigintPlain
//! HAS CallRuntime ToNumber
//! HAS_NOT DeoptIfNotNumber
//! HAS_NOT DeoptIfNotBoolean
//! HAS_NOT DeoptIfNotUndefinedOrNull
//! HAS_NOT DeoptIfNotBigInt

function strPlain(x) { return +x; }
function objPlain(x) { return +x; }
function bigintPlain(x) { return +x; }

let obj = { valueOf() { return 7; } };
for (let i = 0; i < 2000; i++) {
    strPlain(i % 3 === 0 ? "12" : "1.5");
    objPlain(obj);
    try {
        bigintPlain(10n);
    } catch (e) {
    }
}

ArkTools.arkSteedCompileSync(strPlain);
ArkTools.arkSteedCompileSync(objPlain);
ArkTools.arkSteedCompileSync(bigintPlain);

print(strPlain("12"));
print(strPlain("abc"));
print(objPlain(obj));
try {
    print(bigintPlain(10n));
} catch (e) {
    print("caught:" + (e instanceof TypeError));
}
