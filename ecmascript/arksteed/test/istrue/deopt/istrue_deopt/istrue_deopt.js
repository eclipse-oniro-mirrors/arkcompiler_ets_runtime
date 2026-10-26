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

//! METHOD boolSite
//! HAS DeoptIfNotBoolean
//! HAS_NOT CallCommonStub ToBoolean*

//! METHOD nullSite
//! HAS DeoptIfNotUndefinedOrNull
//! HAS_NOT CallCommonStub ToBoolean*

//! METHOD doubleSite
//! HAS TaggedDoubleToBoolean
//! HAS_NOT TaggedNumberToBoolean
//! HAS_NOT BranchIfFloat64Compare
//! HAS_NOT CallCommonStub ToBoolean*

function boolSite(x) { return !!x; }
function nullSite(x) { return !!x; }
function doubleSite(x) { return !!x; }

for (let i = 0; i < 2000; i++) {
    boolSite(i % 2 === 0);
    nullSite(i % 2 === 0 ? null : undefined);
    doubleSite(i + 0.5);
}

ArkTools.arkSteedCompileSync(boolSite);
ArkTools.arkSteedCompileSync(nullSite);
ArkTools.arkSteedCompileSync(doubleSite);

// Still on the specialized fast paths.
print(boolSite(true));
print(nullSite(null));
print(doubleSite(1.5));

// Type mismatch triggers the eager deopt guard; the interpreter re-executes
// ISTRUE from the current bytecode and produces the plain ToBoolean result.
print(boolSite(1));
print(boolSite(""));
print(boolSite(null));
print(nullSite(0));
print(nullSite("abc"));
print(doubleSite("abc"));
print(doubleSite(true));

// Semantics keep matching the interpreter for the falsy boundaries.
print(doubleSite(NaN));
print(doubleSite(-0.0));
print(doubleSite(0.0));
