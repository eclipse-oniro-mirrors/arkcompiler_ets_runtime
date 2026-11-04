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

//! PARAMS --compiler-jit-hotness-threshold=60000 --enable-pgo-profiler=true

//! METHOD coldNum
//! HAS CallRuntime ToNumeric
//! HAS_NOT DeoptIfNotNumber
//! HAS_NOT DeoptIfNotBoolean
//! HAS_NOT DeoptIfNotUndefinedOrNull
//! HAS_NOT DeoptIfNotBigInt

function coldNum(x) { let y = x++; return y; }

// Compiled before the site ever executes: no profile, runtime fallback.
ArkTools.arkSteedCompileSync(coldNum);

print(coldNum(41));
print(coldNum(true));
print(coldNum(10n) === 10n);
