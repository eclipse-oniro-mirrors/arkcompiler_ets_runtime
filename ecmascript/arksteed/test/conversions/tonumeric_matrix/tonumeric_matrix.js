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

//! METHOD postIncBool
//! HAS DeoptIfNotBoolean
//! HAS_NOT CallRuntime ToNumeric

//! METHOD postIncNull
//! HAS DeoptIfNotUndefinedOrNull
//! HAS_NOT CallRuntime ToNumeric

//! METHOD postIncUndef
//! HAS DeoptIfNotUndefinedOrNull
//! HAS_NOT CallRuntime ToNumeric

//! METHOD postIncInt
//! HAS DeoptIfNotNumber
//! HAS_NOT CallRuntime ToNumeric

//! METHOD postIncBigInt
//! HAS DeoptIfNotBigInt
//! HAS_NOT CallRuntime ToNumeric

//! METHOD postIncStr
//! HAS CallRuntime ToNumeric
//! HAS_NOT DeoptIfNotNumber
//! HAS_NOT DeoptIfNotBoolean
//! HAS_NOT DeoptIfNotUndefinedOrNull
//! HAS_NOT DeoptIfNotBigInt

//! METHOD postIncObj
//! HAS CallRuntime ToNumeric
//! HAS_NOT DeoptIfNotNumber
//! HAS_NOT DeoptIfNotBoolean
//! HAS_NOT DeoptIfNotUndefinedOrNull
//! HAS_NOT DeoptIfNotBigInt

function postIncBool(x) { let y = x++; return y; }
function postIncNull(x) { let y = x++; return y; }
function postIncUndef(x) { let y = x++; return y; }
function postIncInt(x) { let y = x++; return y; }
function postIncBigInt(x) { let y = x++; return y; }
function postIncStr(x) { let y = x++; return y; }
function postIncObj(x) { let y = x++; return y.id; }

let obj = { id: 3, valueOf() { return 7; } };
for (let i = 0; i < 2000; i++) {
    postIncBool(i % 2 === 0);
    postIncNull(null);
    postIncUndef(undefined);
    postIncInt(i);
    postIncBigInt(10n);
    postIncStr("5");
    postIncObj(obj);
}

ArkTools.arkSteedCompileSync(postIncBool);
ArkTools.arkSteedCompileSync(postIncNull);
ArkTools.arkSteedCompileSync(postIncUndef);
ArkTools.arkSteedCompileSync(postIncInt);
ArkTools.arkSteedCompileSync(postIncBigInt);
ArkTools.arkSteedCompileSync(postIncStr);
ArkTools.arkSteedCompileSync(postIncObj);

print(postIncBool(true));
print(postIncBool(false));
print(Object.is(postIncBool(false), -0));
print(postIncNull(null));
print(postIncUndef(undefined));
print(postIncInt(41));
print(postIncBigInt(10n) === 10n);
print(postIncStr("5"));
print(postIncObj(obj));
