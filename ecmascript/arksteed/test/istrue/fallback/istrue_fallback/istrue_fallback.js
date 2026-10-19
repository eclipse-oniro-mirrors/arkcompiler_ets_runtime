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

//! METHOD strDoubleBang
//! COMPILE_COUNT_GE *strDoubleBang 1
//! EXEC_COUNT_GE *strDoubleBang 1
//! HAS CallCommonStub ToBoolean*
//! HAS_NOT DeoptIfNotNumber
//! HAS_NOT DeoptIfNotUndefinedOrNull

//! METHOD objDoubleBang
//! COMPILE_COUNT_GE *objDoubleBang 1
//! EXEC_COUNT_GE *objDoubleBang 1
//! HAS CallCommonStub ToBoolean*
//! HAS_NOT DeoptIfNotNumber
//! HAS_NOT DeoptIfNotUndefinedOrNull

//! METHOD bigintDoubleBang
//! COMPILE_COUNT_GE *bigintDoubleBang 1
//! EXEC_COUNT_GE *bigintDoubleBang 1
//! HAS CallCommonStub ToBoolean*
//! HAS_NOT DeoptIfNotNumber

//! METHOD mixedDoubleBang
//! COMPILE_COUNT_GE *mixedDoubleBang 1
//! EXEC_COUNT_GE *mixedDoubleBang 1
//! HAS CallCommonStub ToBoolean*
//! HAS_NOT DeoptIfNotNumber

//! METHOD strNegate
//! COMPILE_COUNT_GE *strNegate 1
//! EXEC_COUNT_GE *strNegate 1
//! HAS CallCommonStub ToBoolean*
//! HAS_NOT DeoptIfNotNumber
//! HAS_NOT DeoptIfNotUndefinedOrNull

//! METHOD objNegate
//! COMPILE_COUNT_GE *objNegate 1
//! EXEC_COUNT_GE *objNegate 1
//! HAS CallCommonStub ToBoolean*
//! HAS_NOT DeoptIfNotNumber
//! HAS_NOT DeoptIfNotUndefinedOrNull

function strDoubleBang(x) { return !!x; }
function objDoubleBang(x) { return !!x; }
function bigintDoubleBang(x) { return !!x; }
function mixedDoubleBang(x) { return !!x; }
function strNegate(x) { return !x; }
function objNegate(x) { return !x; }

let obj = { kind: "object" };
for (let i = 0; i < 2000; i++) {
    strDoubleBang(i % 2 === 0 ? "abc" : "0");
    objDoubleBang(obj);
    bigintDoubleBang(i % 2 === 0 ? 1n : 5n);
    mixedDoubleBang(i % 2 === 0 ? "abc" : true);
    strNegate(i % 2 === 0 ? "abc" : "0");
    objNegate(obj);
}

ArkTools.arkSteedCompileSync(strDoubleBang);
ArkTools.arkSteedCompileSync(objDoubleBang);
ArkTools.arkSteedCompileSync(bigintDoubleBang);
ArkTools.arkSteedCompileSync(mixedDoubleBang);
ArkTools.arkSteedCompileSync(strNegate);
ArkTools.arkSteedCompileSync(objNegate);

print(strDoubleBang("abc"));
print(strDoubleBang(""));
print(strDoubleBang("0"));
print(objDoubleBang(obj));
print(objDoubleBang([]));
print(bigintDoubleBang(1n));
print(bigintDoubleBang(0n));
print(mixedDoubleBang("abc"));
print(mixedDoubleBang(true));
print(strNegate("abc"));
print(strNegate(""));
print(strNegate("0"));
print(objNegate(obj));
