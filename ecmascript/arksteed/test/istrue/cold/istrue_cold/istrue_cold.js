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

// Double negation converts once, then negates the known Boolean result.
//! METHOD coldBool
//! COUNT CallCommonStub ToBooleanFalse 1
//! COUNT TaggedNotEqual 1
//! HAS_NOT DeoptIfNotBoolean
//! HAS_NOT DeoptIfNotNumber
//! HAS_NOT DeoptIfNotUndefinedOrNull

//! METHOD coldNumber
//! COUNT CallCommonStub ToBooleanFalse 1
//! COUNT TaggedNotEqual 1
//! HAS_NOT DeoptIfNotNumber
//! HAS_NOT DeoptIfNotBoolean

//! METHOD coldNull
//! COUNT CallCommonStub ToBooleanFalse 1
//! COUNT TaggedNotEqual 1
//! HAS_NOT DeoptIfNotUndefinedOrNull
//! HAS_NOT DeoptIfNotBoolean

function coldBool(x) { return !!x; }
function coldNumber(x) { return !!x; }
function coldNull(x) { return !!x; }

// Compiled before any execution: the IC slots stay empty, so the lowering
// keeps the generic CommonStubCall fallback.
ArkTools.arkSteedCompileSync(coldBool);
ArkTools.arkSteedCompileSync(coldNumber);
ArkTools.arkSteedCompileSync(coldNull);

print(coldBool(true));
print(coldBool(0));
print(coldNumber(2.5));
print(coldNumber(NaN));
print(coldNull(null));
print(coldNull(undefined));
