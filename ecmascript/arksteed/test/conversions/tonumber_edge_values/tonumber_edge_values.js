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

//! METHOD numEdge
//! HAS DeoptIfNotNumber
//! COUNT DeoptIfNotNumber 1
//! HAS_NOT CallRuntime ToNumber

//! METHOD nullEdge
//! HAS DeoptIfNotUndefinedOrNull
//! COUNT DeoptIfNotUndefinedOrNull 1
//! HAS_NOT CallRuntime ToNumber

// print() goes through ToString, which prints both +0 and -0 as "0", so the
// sign of zero and NaN identity are asserted with Object.is instead.
function numEdge(x) { return +x; }
function nullEdge(x) { return +x; }

for (let i = 0; i < 2000; i++) {
    numEdge(i % 3 === 0 ? -0 : (i % 3 === 1 ? NaN : 1.5));
    nullEdge(null);
}
ArkTools.arkSteedCompileSync(numEdge);
ArkTools.arkSteedCompileSync(nullEdge);

print(Object.is(numEdge(-0), -0));
print(Object.is(numEdge(NaN), NaN));
print(Object.is(numEdge(0), -0));
print(Object.is(nullEdge(null), -0));
print(nullEdge(undefined));
