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

//! METHOD boolPlain
//! HAS DeoptIfNotBoolean
//! COUNT DeoptIfNotBoolean 1
//! COUNT I64BitwiseBinary 2
//! HAS_NOT CallRuntime ToNumber

//! METHOD nullPlain
//! HAS DeoptIfNotUndefinedOrNull
//! COUNT DeoptIfNotUndefinedOrNull 1
//! COUNT BranchIfInt64Compare 1
//! HAS_NOT CallRuntime ToNumber

//! METHOD undefPlain
//! HAS DeoptIfNotUndefinedOrNull
//! COUNT DeoptIfNotUndefinedOrNull 1
//! COUNT BranchIfInt64Compare 1
//! HAS_NOT CallRuntime ToNumber

//! METHOD mixedPlain
//! HAS DeoptIfNotUndefinedOrNull
//! COUNT DeoptIfNotUndefinedOrNull 1
//! COUNT BranchIfInt64Compare 1
//! HAS_NOT CallRuntime ToNumber

//! METHOD arith
//! HAS_NOT CallRuntime ToNumber

//! METHOD pgoNumber
//! HAS DeoptIfNotNumber
//! COUNT DeoptIfNotNumber 1
//! HAS_NOT CallRuntime ToNumber

function boolPlain(x) { return +x; }
function nullPlain(x) { return +x; }
function undefPlain(x) { return +x; }
function mixedPlain(x) { return +x; }
function arith(a, b) { const x = a * b; return +x; }
function pgoNumber(x) { return +x; }

for (let i = 0; i < 2000; i++) {
    boolPlain(i % 2 === 0);
    nullPlain(null);
    undefPlain(undefined);
    mixedPlain(i % 2 === 0 ? null : undefined);
    arith(6, 7);
    pgoNumber(i % 2 === 0 ? 1 : 2.5);
}

ArkTools.arkSteedCompileSync(boolPlain);
ArkTools.arkSteedCompileSync(nullPlain);
ArkTools.arkSteedCompileSync(undefPlain);
ArkTools.arkSteedCompileSync(mixedPlain);
ArkTools.arkSteedCompileSync(arith);
ArkTools.arkSteedCompileSync(pgoNumber);

print(boolPlain(true));
print(boolPlain(false));
print(nullPlain(null));
print(undefPlain(undefined));
print(arith(6, 7));
print(mixedPlain(null));
print(mixedPlain(undefined));
print(pgoNumber(2.5));
print(pgoNumber(3));
