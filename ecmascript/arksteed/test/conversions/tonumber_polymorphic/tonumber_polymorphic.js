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

//! METHOD polySite
//! HAS CallRuntime ToNumber
//! HAS_NOT DeoptIfNotBoolean
//! HAS_NOT DeoptIfNotUndefinedOrNull
//! HAS_NOT DeoptIfNotNumber
//! HAS_NOT DeoptIfNotBigInt

function polySite(x) { return +x; }

for (let i = 0; i < 2000; i++) {
    polySite(i % 2 === 0 ? true : null);
}
ArkTools.arkSteedCompileSync(polySite);

print(polySite(true));
print(polySite(null));
print(polySite(undefined));
print(polySite("8"));
