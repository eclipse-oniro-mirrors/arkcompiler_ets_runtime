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

//! METHOD numSpec
//! HAS DeoptIfNotNumber
//! COUNT DeoptIfNotNumber 1
//! HAS_NOT CallRuntime ToNumber

//! METHOD nullSpec
//! HAS DeoptIfNotUndefinedOrNull
//! COUNT DeoptIfNotUndefinedOrNull 1
//! HAS_NOT CallRuntime ToNumber

//! METHOD bigSpec
//! HAS DeoptIfNotBigInt
//! COUNT DeoptIfNotBigInt 1
//! HAS_NOT CallRuntime ToNumeric

// Eager deopt of the number / undefined-or-null / bigint guards: each site is
// specialized on monomorphic feedback, then fed an input of a different type.
// The guards deopt before any side effect, so the interpreter re-executes the
// bytecode and user code (valueOf) runs exactly once.
function numSpec(x) { return +x; }
function nullSpec(x) { return +x; }
function bigSpec(x) { let y = x++; return y; }

for (let i = 0; i < 2000; i++) {
    numSpec(i % 2 === 0 ? 1 : 2.5);
    nullSpec(null);
    bigSpec(10n);
}
ArkTools.arkSteedCompileSync(numSpec);
ArkTools.arkSteedCompileSync(nullSpec);
ArkTools.arkSteedCompileSync(bigSpec);

let calls = 0;
let obj = { valueOf() { calls++; return 42; } };
print(numSpec(obj));
print(calls);
print(nullSpec(true));
print(bigSpec(5));
print(numSpec(7));
print(nullSpec(undefined));
print(bigSpec(20n) === 20n);
