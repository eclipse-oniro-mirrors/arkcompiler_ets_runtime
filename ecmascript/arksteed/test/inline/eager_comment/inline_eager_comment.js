/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

//! PARAMS --compiler-trace-inline=true --compiler-arksteed-enable-inline=true

//! METHOD inlineEagerCommentCaller
//! CODEGEN_COUNT EagerInline begin * 1
//! CODEGEN_COUNT EagerInline end * 1
//! CODEGEN_COUNT EagerInline guard 1
//! COUNT DeoptIfTaggedCondition 1
//! HAS_NOT CallKnownJSFunction
//! HAS_NOT Call

"use strict";

// Monomorphic, loop-free and well under the inline bytecode budget, so the
// caller always inlines it. The generated-code comments must then contain a
// balanced EagerInline begin/end pair and the two known-call guard markers.
function inlineEagerCommentTarget(value) {
    return value + 1;
}

function inlineEagerCommentCaller(value) {
    return inlineEagerCommentTarget(value);
}

for (let i = 0; i < 64; i++) {
    inlineEagerCommentCaller(i);
}

let ok = ArkTools.arkSteedCompileSync(inlineEagerCommentCaller);
ArkTools.waitJitCompileFinish(inlineEagerCommentCaller);
ok = ArkTools.isAOTCompiled(inlineEagerCommentCaller) && ok;
ok = inlineEagerCommentCaller(41) === 42 && ok;
print(ok ? "PASS" : "FAIL");
