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

// A high hotness threshold keeps feedback collection in the interpreter until explicit compilation.
//! PARAMS --asm-interpreter=true --compiler-jit-hotness-threshold=60000 --compiler-enable-baselinejit=false --enable-elements-kind=true --enable-mutant-array=true --enable-cmc-gc=false

//! METHOD createNumberFeedback
//! COUNT AllocateInYoung 1
//! BYTECODE_COUNT createarraywithbuffer 1
//! HAS HeapConstant
//! COUNT InitializeArrayWithBuffer 1
//! HAS_NOT CallCommonStub CreateArrayWithBuffer
//! HAS_NOT TransitionHClassWithBarrier

//! METHOD createTaggedFeedback
//! COUNT AllocateInYoung 1
//! BYTECODE_COUNT createarraywithbuffer 1
//! HAS HeapConstant
//! COUNT InitializeArrayWithBuffer 1
//! HAS_NOT CallCommonStub CreateArrayWithBuffer
//! HAS_NOT TransitionHClassWithBarrier

function createNumberFeedback() {
    return [1, 2, 3];
}

function createTaggedFeedback() {
    return [1, 2, 3];
}

function hasLiteralValues(array) {
    return array.length === 3 && array[0] === 1 && array[1] === 2 && array[2] === 3;
}

let numberWarm;
let taggedWarm;
for (let i = 0; i < 16; ++i) {
    numberWarm = createNumberFeedback();
    taggedWarm = createTaggedFeedback();
}
print("interpreted-warmup:" + (!ArkTools.arkSteedIsCompiled(createNumberFeedback) &&
    !ArkTools.arkSteedIsCompiled(createTaggedFeedback)));

// ElementsKind::INT is 2, NUMBER is 6, and TAGGED is 30.
let numberWasInt = ArkTools.getElementsKind(numberWarm) === 2;
let taggedWasInt = ArkTools.getElementsKind(taggedWarm) === 2;
let payload = { value: 42 };
// Change TrackInfo only after the FINAL factory calls. Calling either factory again
// before compilation could migrate its template and hide the representation mismatch.
numberWarm[0] = 1.5;
taggedWarm[0] = payload;
print("number-feedback:" + (numberWasInt && ArkTools.getElementsKind(numberWarm) === 6));
print("tagged-feedback:" + (taggedWasInt && ArkTools.getElementsKind(taggedWarm) === 30));

ArkTools.arkSteedCompileAsync(createNumberFeedback);
ArkTools.waitJitCompileFinish(createNumberFeedback);
ArkTools.arkSteedCompileAsync(createTaggedFeedback);
ArkTools.waitJitCompileFinish(createTaggedFeedback);
print("compiled:" + (ArkTools.arkSteedIsCompiled(createNumberFeedback) &&
    ArkTools.arkSteedIsCompiled(createTaggedFeedback)));

let numberFirst = createNumberFeedback();
let numberSecond = createNumberFeedback();
let taggedFirst = createTaggedFeedback();
let taggedSecond = createTaggedFeedback();
print("literal-values:" + (hasLiteralValues(numberFirst) && hasLiteralValues(numberSecond) &&
    hasLiteralValues(taggedFirst) && hasLiteralValues(taggedSecond)));

numberFirst[1] = 7.5;
taggedFirst[1] = payload;
print("cow-independence:" + (numberFirst !== numberSecond && numberFirst !== numberWarm &&
    taggedFirst !== taggedSecond && taggedFirst !== taggedWarm &&
    numberFirst[1] === 7.5 && taggedFirst[1] === payload &&
    numberWarm[0] === 1.5 && numberWarm[1] === 2 && numberWarm[2] === 3 &&
    taggedWarm[0] === payload && taggedWarm[1] === 2 && taggedWarm[2] === 3 &&
    hasLiteralValues(numberSecond) && hasLiteralValues(taggedSecond) &&
    hasLiteralValues(createNumberFeedback()) && hasLiteralValues(createTaggedFeedback())));

// Existing GC hook checks retained arrays and embedded templates, not forced allocation fallback.
// These factories still allocate inline, so capacity failure may eagerly deopt.
ArkTools.forceFullGC();
print("after-gc:" + (numberFirst[0] === 1 && numberFirst[1] === 7.5 && numberFirst[2] === 3 &&
    taggedFirst[0] === 1 && taggedFirst[1] === payload && taggedFirst[2] === 3 &&
    hasLiteralValues(numberSecond) && hasLiteralValues(taggedSecond) &&
    hasLiteralValues(createNumberFeedback()) && hasLiteralValues(createTaggedFeedback())));
