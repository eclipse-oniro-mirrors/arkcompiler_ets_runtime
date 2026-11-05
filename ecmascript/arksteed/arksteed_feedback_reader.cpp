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

#include "ecmascript/arksteed/arksteed_feedback_reader.h"
#include "ecmascript/ic/ic_handler.h"
#include "ecmascript/ic/property_box.h"
#include "ecmascript/js_function.h"
#include "ecmascript/method.h"
#include "ecmascript/pgo_profiler/types/pgo_profiler_type.h"

namespace panda::ecmascript::arksteed {

constexpr uint32_t IC_POLY_CASE_WIDTH = 2;
constexpr int ACCESS_SLOT_INPUT = 0;

namespace {
kungfu::Bytecodes g_callFeedbackBytecodes;

bool IsReducibleFunctionKind(FunctionKind kind)
{
    return kind == FunctionKind::NORMAL_FUNCTION || kind == FunctionKind::ARROW_FUNCTION ||
        kind == FunctionKind::BASE_CONSTRUCTOR;
}

kungfu::BytecodeInfo DecodeBytecodeInfo(const uint8_t *pc, uint32_t pcOffset)
{
    kungfu::BytecodeInfo info;
    info.SetMetaData(g_callFeedbackBytecodes.GetBytecodeMetaData(pc));
    kungfu::BytecodeInfo::InitBytecodeInfo(info, pc, pcOffset, 0);
    return info;
}

bool TryAnalyzeConstantReturn(const kungfu::BytecodeInfo &first, CallFeedback *feedback)
{
    if (first.GetOpcode() == kungfu::EcmaOpcode::LDAI_IMM32) {
        if (first.inputs.size() != 1 || !std::holds_alternative<kungfu::Immediate>(first.inputs[0])) {
            return false;
        }
        int32_t value = static_cast<int32_t>(std::get<kungfu::Immediate>(first.inputs[0]).GetValue());
        feedback->reductionKind = CallReductionKind::TAGGED_CONSTANT;
        feedback->reducedConstant = JSTaggedValue(value).GetRawData();
        return true;
    }

    switch (first.GetOpcode()) {
        case kungfu::EcmaOpcode::LDUNDEFINED:
            feedback->reducedConstant = JSTaggedValue::VALUE_UNDEFINED;
            break;
        case kungfu::EcmaOpcode::LDNULL:
            feedback->reducedConstant = JSTaggedValue::VALUE_NULL;
            break;
        case kungfu::EcmaOpcode::LDTRUE:
            feedback->reducedConstant = JSTaggedValue::VALUE_TRUE;
            break;
        case kungfu::EcmaOpcode::LDFALSE:
            feedback->reducedConstant = JSTaggedValue::VALUE_FALSE;
            break;
        default:
            return false;
    }
    feedback->reductionKind = CallReductionKind::TAGGED_CONSTANT;
    return true;
}

void AnalyzeArgumentReturn(Method *method, const kungfu::BytecodeInfo &first, CallFeedback *feedback)
{
    if (first.GetOpcode() != kungfu::EcmaOpcode::LDA_V8 || first.inputs.size() != 1 ||
        !std::holds_alternative<kungfu::VirtualRegister>(first.inputs[0])) {
        return;
    }
    uint32_t vreg = std::get<kungfu::VirtualRegister>(first.inputs[0]).GetId();
    uint32_t firstUserArgVreg = method->GetNumVregsWithCallField() + method->GetNumRevervedArgs();
    uint32_t expectedArgc = method->GetNumArgsWithCallField();
    if (vreg < firstUserArgVreg || vreg - firstUserArgVreg >= expectedArgc) {
        return;
    }
    feedback->reductionKind = CallReductionKind::USER_ARGUMENT;
    feedback->reducedArgumentIndex = vreg - firstUserArgVreg;
}

void AnalyzeTrivialCallReduction(JSThread *thread, Method *method, CallFeedback *feedback)
{
    if (method->IsNativeWithCallField() || !IsReducibleFunctionKind(method->GetFunctionKind())) {
        return;
    }

    const uint8_t *start = method->GetBytecodeArray();
    const uint8_t *end = start + method->GetCodeSize(thread);
    if (start == end) {
        return;
    }

    kungfu::BytecodeInfo first = DecodeBytecodeInfo(start, 0);
    const uint8_t *secondPc = start + first.GetSize();
    if (first.IsBc(kungfu::EcmaOpcode::RETURNUNDEFINED) && secondPc == end) {
        feedback->reductionKind = CallReductionKind::TAGGED_CONSTANT;
        feedback->reducedConstant = JSTaggedValue::VALUE_UNDEFINED;
        return;
    }
    if (secondPc >= end) {
        return;
    }

    kungfu::BytecodeInfo second = DecodeBytecodeInfo(
        secondPc, static_cast<uint32_t>(secondPc - start));
    if (!second.IsBc(kungfu::EcmaOpcode::RETURN) || secondPc + second.GetSize() != end) {
        return;
    }
    if (TryAnalyzeConstantReturn(first, feedback)) {
        return;
    }
    AnalyzeArgumentReturn(method, first, feedback);
}
}  // namespace

bool ArkSteedFeedbackReader::TryGetFeedbackSlotId(int index, bool allowImmediate, uint32_t *slotId) const
{
    if (slotId == nullptr) {
        return false;
    }
    kungfu::ICSlotIdType slotID = bytecodeInfo_->slotId.GetId();
    if (index == ACCESS_SLOT_INPUT && !allowImmediate && slotID != kungfu::ICSlotId::INVALID_ID) {
        *slotId = static_cast<uint32_t>(slotID);
        return true;
    }

    if (index < 0 || static_cast<size_t>(index) >= bytecodeInfo_->inputs.size()) {
        return false;
    }

    const auto &input = bytecodeInfo_->inputs[index];
    if (std::holds_alternative<kungfu::ICSlotId>(input)) {
        *slotId = static_cast<uint32_t>(std::get<kungfu::ICSlotId>(input).GetId());
        return true;
    }
    if (allowImmediate && std::holds_alternative<kungfu::Immediate>(input)) {
        *slotId = static_cast<uint32_t>(std::get<kungfu::Immediate>(input).GetValue());
        return true;
    }
    return false;
}

bool ArkSteedFeedbackReader::TryGetFeedbackSlotId(uint32_t *slotId) const
{
    return TryGetFeedbackSlotId(ACCESS_SLOT_INPUT, false, slotId);
}

bool ArkSteedFeedbackReader::ReadConstructorFeedback(ConstructorFeedback *feedback) const
{
    if (feedback == nullptr) {
        return false;
    }
    *feedback = {};

    uint32_t slotId = 0;
    if (!TryGetFeedbackSlotId(&slotId)) {
        return false;
    }

    ProfileTypeInfo *profileTypeArray = nullptr;
    if (!broker_->TryGetProfileTypeInfo(&profileTypeArray)) {
        return false;
    }
    uint32_t slotLength = profileTypeArray->GetICSlotLength();
    if (slotId >= slotLength) {
        return false;
    }

    JSTaggedValue target = profileTypeArray->GetICSlot(compilerThread_, slotId);
    if (target.IsUndefined()) {
        return true;
    }
    if (target.IsInt()) {
        int64_t encodedBuiltinId = static_cast<int64_t>(target.GetInt());
        if (encodedBuiltinId < 0) {
            int64_t builtinId = -encodedBuiltinId;
            if (builtinId > kungfu::BuiltinsStubCSigns::NONE &&
                builtinId < kungfu::BuiltinsStubCSigns::ID::NUM_OF_BUILTINS_ID) {
                feedback->kind = ConstructorFeedbackKind::MONOMORPHIC_BUILTIN;
                feedback->builtinId = static_cast<kungfu::BuiltinsStubCSigns::ID>(builtinId);
                return true;
            }
        }
        return true;
    }
    if (!target.IsJSFunction()) {
        return true;
    }

    // The closure-family encoding (profiler_stub_builder) keeps a representative
    // function in the primary slot and parks the shared profile cell in the
    // secondary slot instead of an int call count. Identity speculation on the
    // representative would deopt on the next family member, so only an int
    // count (a genuinely unique target) qualifies as monomorphic here.
    if (slotLength - slotId <= 1 ||
        !profileTypeArray->GetICSlot(compilerThread_, slotId + 1).IsInt()) {
        return true;
    }

    feedback->target = broker_->MakeFunctionRef(target);
    if (!feedback->target.IsSafeForCompile()) {
        *feedback = {};
        return false;
    }
    feedback->kind = ConstructorFeedbackKind::MONOMORPHIC_JS_FUNCTION;
    return true;
}

bool ArkSteedFeedbackReader::TryGetConstDataId(int index, uint16_t *constDataId) const
{
    if (index < 0 || static_cast<size_t>(index) >= bytecodeInfo_->inputs.size()) {
        return false;
    }

    const auto &input = bytecodeInfo_->inputs[index];
    if (!std::holds_alternative<kungfu::ConstDataId>(input)) {
        return false;
    }
    *constDataId = std::get<kungfu::ConstDataId>(input).GetId();
    return true;
}

bool ArkSteedFeedbackReader::TryGetICMonoSnapshot(int slotIndex, ICMonoSnapshot *snapshot) const
{
    uint32_t slotId = 0;
    if (!TryGetFeedbackSlotId(slotIndex, false, &slotId)) {
        return false;
    }

    ProfileTypeInfo *profileTypeArray = nullptr;
    if (!broker_->TryGetProfileTypeInfo(&profileTypeArray)) {
        return false;
    }
    if (slotId >= profileTypeArray->GetLength() || profileTypeArray->GetLength() - slotId <= 1) {
        return false;
    }

    JSTaggedValue first = profileTypeArray->GetICSlot(compilerThread_, slotId);
    JSTaggedValue second = profileTypeArray->GetICSlot(compilerThread_, slotId + 1);
    if (!first.IsWeak()) {
        return false;
    }
    snapshot->expectedHClass = broker_->MakeWeakHClassRef(first);
    if (!snapshot->expectedHClass.IsSafeForCompile()) {
        return false;
    }
    snapshot->handler = second;
    snapshot->slotId = slotId;
    return true;
}

bool ArkSteedFeedbackReader::TryGetICPolySnapshot(int slotIndex, ICPolySnapshot *snapshot) const
{
    uint32_t slotId = 0;
    if (!TryGetFeedbackSlotId(slotIndex, false, &slotId)) {
        return false;
    }

    ProfileTypeInfo *profileTypeArray = nullptr;
    if (!broker_->TryGetProfileTypeInfo(&profileTypeArray)) {
        return false;
    }
    if (slotId >= profileTypeArray->GetLength() || profileTypeArray->GetLength() - slotId <= 1) {
        return false;
    }

    JSTaggedValue first = profileTypeArray->GetICSlot(compilerThread_, slotId);
    JSTaggedValue second = profileTypeArray->GetICSlot(compilerThread_, slotId + 1);
    if (first.IsWeak() || !first.IsHeapObject() || !first.IsTaggedArray() || !second.IsHole()) {
        return false;
    }

    auto *polyArray = TaggedArray::Cast(first.GetTaggedObject());
    if (polyArray->GetLength() == 0 || (polyArray->GetLength() % IC_POLY_CASE_WIDTH) != 0) {
        return false;
    }
    snapshot->polyArray = polyArray;
    snapshot->caseCount = polyArray->GetLength() / IC_POLY_CASE_WIDTH;
    snapshot->slotId = slotId;
    return snapshot->caseCount > 0;
}

bool ArkSteedFeedbackReader::TryGetICPolyCase(const ICPolySnapshot &snapshot, uint32_t caseIndex,
                                              ICCaseSnapshot *icCase) const
{
    if (snapshot.polyArray == nullptr || caseIndex >= snapshot.caseCount) {
        return false;
    }

    uint32_t index = caseIndex * IC_POLY_CASE_WIDTH;
    JSTaggedValue cachedHClass = snapshot.polyArray->Get(compilerThread_, index);
    if (!cachedHClass.IsWeak()) {
        return false;
    }
    icCase->expectedHClass = broker_->MakeWeakHClassRef(cachedHClass);
    if (!icCase->expectedHClass.IsSafeForCompile()) {
        return false;
    }
    icCase->handler = snapshot.polyArray->Get(compilerThread_, index + 1);
    return true;
}

bool ArkSteedFeedbackReader::TryReadNamedAccessName(ArkSteedNameRef *name) const
{
    uint16_t constDataId = 0;
    if (!TryGetConstDataId(1, &constDataId)) {
        return false;
    }
    return broker_->TryGetNameFromConstantPool(constDataId, name);
}

NamedAccessCaseFeedback ArkSteedFeedbackReader::MakeNamedAccessCaseFeedback(ArkSteedHClassRef expectedHClass,
                                                                            JSTaggedValue handler) const
{
    NamedAccessCaseFeedback feedback;
    feedback.expectedHClass = expectedHClass;
    feedback.handler = broker_->MakeHandlerRef(handler);
    if (!feedback.expectedHClass.IsSafeForCompile() || !feedback.handler.IsSafeForCompile()) {
        return feedback;
    }
    return feedback;
}

ElementAccessCaseFeedback ArkSteedFeedbackReader::MakeElementAccessCaseFeedback(ArkSteedHClassRef expectedHClass,
                                                                                JSTaggedValue handler) const
{
    ElementAccessCaseFeedback feedback;
    feedback.expectedHClass = expectedHClass;
    feedback.handler = broker_->MakeHandlerRef(handler);
    if (!feedback.expectedHClass.IsSafeForCompile() || !feedback.handler.IsSafeForCompile()) {
        return feedback;
    }
    return feedback;
}

bool ArkSteedFeedbackReader::ReadNamedAccessFeedback(int slotIndex, NamedAccessFeedback *feedback) const
{
    *feedback = {};
    if (!TryReadNamedAccessName(&feedback->name)) {
        *feedback = {};
        return false;
    }

    ICMonoSnapshot monoSnapshot;
    if (TryGetICMonoSnapshot(slotIndex, &monoSnapshot)) {
        feedback->base.kind = ProcessedFeedbackKind::NAMED_ACCESS;
        feedback->base.source = {monoSnapshot.slotId, false, AccessFeedbackSlotKind::UNKNOWN};
        feedback->cases[0] = MakeNamedAccessCaseFeedback(monoSnapshot.expectedHClass, monoSnapshot.handler);
        if (!feedback->cases[0].expectedHClass.IsSafeForCompile() || !feedback->cases[0].handler.IsSafeForCompile()) {
            *feedback = {};
            return false;
        }
        feedback->caseCount = 1;
        return true;
    }

    ICPolySnapshot polySnapshot;
    if (!TryGetICPolySnapshot(slotIndex, &polySnapshot)) {
        *feedback = {};
        return false;
    }
    if (polySnapshot.caseCount > MAX_NAMED_IC_POLY_CASES) {
        *feedback = {};
        return false;
    }

    feedback->base.kind = ProcessedFeedbackKind::NAMED_ACCESS;
    feedback->base.source = {polySnapshot.slotId, true, AccessFeedbackSlotKind::UNKNOWN};
    for (uint32_t i = 0; i < polySnapshot.caseCount && feedback->caseCount < MAX_NAMED_IC_POLY_CASES; ++i) {
        ICCaseSnapshot icCase;
        if (!TryGetICPolyCase(polySnapshot, i, &icCase)) {
            *feedback = {};
            return false;
        }
        NamedAccessCaseFeedback caseFeedback = MakeNamedAccessCaseFeedback(icCase.expectedHClass, icCase.handler);
        if (!caseFeedback.expectedHClass.IsSafeForCompile() || !caseFeedback.handler.IsSafeForCompile()) {
            *feedback = {};
            return false;
        }
        feedback->cases[feedback->caseCount] = caseFeedback;
        feedback->caseCount++;
    }
    if (feedback->caseCount == 0) {
        *feedback = {};
        return false;
    }
    return true;
}

bool ArkSteedFeedbackReader::ReadValueAccessFeedback(ValueAccessFeedback *feedback) const
{
    *feedback = {};

    uint32_t slotId = 0;
    if (!TryGetFeedbackSlotId(&slotId)) {
        return false;
    }

    ProfileTypeInfo *profileTypeArray = nullptr;
    if (!broker_->TryGetProfileTypeInfo(&profileTypeArray) || slotId >= profileTypeArray->GetLength() ||
        profileTypeArray->GetLength() - slotId <= 1) {
        return false;
    }

    JSTaggedValue first = profileTypeArray->GetICSlot(compilerThread_, slotId);
    JSTaggedValue second = profileTypeArray->GetICSlot(compilerThread_, slotId + 1);
    TaggedArray *caseArray = nullptr;
    bool isPoly = false;

    if (first.IsWeak()) {
        feedback->kind = ValueAccessFeedbackKind::ELEMENT;
        feedback->cases[0] = MakeNamedAccessCaseFeedback(broker_->MakeWeakHClassRef(first), second);
        if (!feedback->cases[0].expectedHClass.IsSafeForCompile() || !feedback->cases[0].handler.IsSafeForCompile()) {
            *feedback = {};
            return false;
        }
        feedback->caseCount = 1;
    } else if (first.IsTaggedArray() && second.IsHole()) {
        feedback->kind = ValueAccessFeedbackKind::ELEMENT;
        caseArray = TaggedArray::Cast(first.GetTaggedObject());
        isPoly = true;
    } else if ((first.IsString() || first.IsSymbol()) && second.IsTaggedArray()) {
        feedback->kind = ValueAccessFeedbackKind::NAMED;
        feedback->key = broker_->MakeNameRef(first);
        if (!feedback->key.IsSafeForCompile()) {
            *feedback = {};
            return false;
        }
        caseArray = TaggedArray::Cast(second.GetTaggedObject());
    } else {
        return false;
    }

    if (caseArray != nullptr) {
        uint32_t length = caseArray->GetLength();
        if (length == 0 || (length % IC_POLY_CASE_WIDTH) != 0 ||
            length / IC_POLY_CASE_WIDTH > MAX_NAMED_IC_POLY_CASES) {
            *feedback = {};
            return false;
        }
        for (uint32_t index = 0; index < length; index += IC_POLY_CASE_WIDTH) {
            JSTaggedValue cachedHClass = caseArray->Get(compilerThread_, index);
            if (!cachedHClass.IsWeak()) {
                *feedback = {};
                return false;
            }
            NamedAccessCaseFeedback icCase = MakeNamedAccessCaseFeedback(broker_->MakeWeakHClassRef(cachedHClass),
                                                                         caseArray->Get(compilerThread_, index + 1));
            if (!icCase.expectedHClass.IsSafeForCompile() || !icCase.handler.IsSafeForCompile()) {
                *feedback = {};
                return false;
            }
            feedback->cases[feedback->caseCount++] = icCase;
        }
    }

    if (feedback->caseCount == 0) {
        *feedback = {};
        return false;
    }
    feedback->base.kind = ProcessedFeedbackKind::VALUE_ACCESS;
    feedback->base.source = {slotId, isPoly || feedback->caseCount > 1, AccessFeedbackSlotKind::VALUE_LOAD};
    return true;
}

bool ArkSteedFeedbackReader::ReadElementAccessFeedback(int slotIndex, ElementAccessFeedback *feedback) const
{
    *feedback = {};
    ICMonoSnapshot monoSnapshot;
    if (TryGetICMonoSnapshot(slotIndex, &monoSnapshot)) {
        ElementAccessCaseFeedback elementCase =
            MakeElementAccessCaseFeedback(monoSnapshot.expectedHClass, monoSnapshot.handler);
        if (!elementCase.expectedHClass.IsSafeForCompile() || !elementCase.handler.IsSafeForCompile()) {
            return false;
        }
        feedback->base.kind = ProcessedFeedbackKind::ELEMENT_ACCESS;
        feedback->base.source = {monoSnapshot.slotId, false, AccessFeedbackSlotKind::ELEMENT_STORE};
        feedback->cases[0] = elementCase;
        feedback->caseCount = 1;
        return true;
    }

    ICPolySnapshot polySnapshot;
    if (!TryGetICPolySnapshot(slotIndex, &polySnapshot) || polySnapshot.caseCount > MAX_ELEMENT_IC_POLY_CASES) {
        return false;
    }
    feedback->base.kind = ProcessedFeedbackKind::ELEMENT_ACCESS;
    feedback->base.source = {polySnapshot.slotId, true, AccessFeedbackSlotKind::ELEMENT_STORE};
    for (uint32_t i = 0; i < polySnapshot.caseCount; ++i) {
        ICCaseSnapshot icCase;
        if (!TryGetICPolyCase(polySnapshot, i, &icCase)) {
            *feedback = {};
            return false;
        }
        ElementAccessCaseFeedback elementCase = MakeElementAccessCaseFeedback(icCase.expectedHClass, icCase.handler);
        if (!elementCase.expectedHClass.IsSafeForCompile() || !elementCase.handler.IsSafeForCompile()) {
            *feedback = {};
            return false;
        }
        feedback->cases[feedback->caseCount++] = elementCase;
    }
    return feedback->caseCount > 0;
}

bool ArkSteedFeedbackReader::ReadGlobalAccessFeedback(GlobalAccessFeedback *feedback) const
{
    *feedback = {};

    uint32_t slotId = 0;
    if (!TryGetFeedbackSlotId(&slotId)) {
        return false;
    }

    ProfileTypeInfo *profileTypeArray = nullptr;
    if (!broker_->TryGetProfileTypeInfo(&profileTypeArray) || slotId >= profileTypeArray->GetLength()) {
        return false;
    }

    // Global IC slots hold a single strong PropertyBox reference; undefined means the site has
    // never missed and Hole means MEGA.
    JSTaggedValue handler = profileTypeArray->GetICSlot(compilerThread_, slotId);
    if (!handler.IsHeapObject() || !handler.IsPropertyBox()) {
        return false;
    }
    auto *box = PropertyBox::Cast(handler.GetTaggedObject());
    if (box->IsInvalid(compilerThread_) || box->GetValue(compilerThread_).IsAccessorData()) {
        return false;
    }
    feedback->base.kind = ProcessedFeedbackKind::GLOBAL_ACCESS;
    feedback->base.source = {slotId, false, AccessFeedbackSlotKind::UNKNOWN};
    feedback->box = broker_->MakeObjectRef(handler);
    if (!feedback->box.IsSafeForCompile()) {
        *feedback = {};
        return false;
    }
    return true;
}

bool ArkSteedFeedbackReader::ReadInstanceofFeedback(InstanceofFeedback *feedback) const
{
    *feedback = {};
    uint32_t slotId = 0;
    if (!TryGetFeedbackSlotId(ACCESS_SLOT_INPUT, false, &slotId)) {
        *feedback = {};
        return false;
    }

    ProfileTypeInfo *profileTypeArray = nullptr;
    if (!broker_->TryGetProfileTypeInfo(&profileTypeArray)) {
        *feedback = {};
        return false;
    }
    if (slotId >= profileTypeArray->GetLength()) {
        *feedback = {};
        return false;
    }

    JSTaggedValue first = profileTypeArray->GetICSlot(compilerThread_, slotId);
    if (!first.IsWeak()) {
        *feedback = {};
        return false;
    }
    JSTaggedValue ctorFunc(first.GetWeakReferentUnChecked());
    if (!ctorFunc.IsJSFunction()) {
        *feedback = {};
        return false;
    }
    feedback->ctorFunc = broker_->MakeFunctionRef(ctorFunc);
    if (!feedback->ctorFunc.IsSafeForCompile()) {
        *feedback = {};
        return false;
    }
    return true;
}

ArkSteedOperationHint ArkSteedFeedbackReader::MakeOperationHint(uint32_t rawBits) const
{
    const auto prim = static_cast<pgo::PGOSampleType::Type>(rawBits);
    switch (prim) {
        case pgo::PGOSampleType::Type::NONE:
            return ArkSteedOperationHint::NONE;
        case pgo::PGOSampleType::Type::INT:
            return ArkSteedOperationHint::INT;
        case pgo::PGOSampleType::Type::INT_OVERFLOW:
        case pgo::PGOSampleType::Type::DOUBLE:
        case pgo::PGOSampleType::Type::NUMBER:
        case pgo::PGOSampleType::Type::NUMBER1:
            return ArkSteedOperationHint::NUMBER;
        case pgo::PGOSampleType::Type::STRING:
            return ArkSteedOperationHint::STRING;
        case pgo::PGOSampleType::Type::NUMBER_OR_STRING:
            return ArkSteedOperationHint::NUMBER_OR_STRING;
        case pgo::PGOSampleType::Type::BOOLEAN:
            return ArkSteedOperationHint::BOOLEAN;
        case pgo::PGOSampleType::Type::UNDEFINED_OR_NULL:
            return ArkSteedOperationHint::UNDEFINED_OR_NULL;
        case pgo::PGOSampleType::Type::BIG_INT:
            return ArkSteedOperationHint::BIG_INT;
        default:
            break;
    }
    return ArkSteedOperationHint::ANY;
}

bool ArkSteedFeedbackReader::ReadOperationFeedback(OperationFeedback *feedback) const
{
    *feedback = {};

    uint32_t slotId = 0;
    if (!TryGetFeedbackSlotId(&slotId)) {
        return false;
    }

    ProfileTypeInfo *profileTypeArray = nullptr;
    if (!broker_->TryGetProfileTypeInfo(&profileTypeArray) || slotId >= profileTypeArray->GetICSlotLength()) {
        return false;
    }

    feedback->slotId = slotId;
    JSTaggedValue slotValue = profileTypeArray->GetICSlot(compilerThread_, slotId);
    if (!slotValue.IsInt()) {
        return true;
    }

    pgo::PGOSampleType profile(static_cast<uint32_t>(slotValue.GetInt()));
    uint32_t rawBits = static_cast<uint32_t>(profile.GetPrimitiveType());
    feedback->rawTypeBits = rawBits;
    feedback->trueWeight = profile.GetTrueWeight();
    feedback->falseWeight = profile.GetFalseWeight();
    feedback->hint = MakeOperationHint(rawBits);
    return true;
}

bool ArkSteedFeedbackReader::ReadCallFeedback(CallFeedback *feedback) const
{
    *feedback = {};

    uint32_t slotId = 0;
    if (!TryGetFeedbackSlotId(&slotId)) {
        return false;
    }

    ProfileTypeInfo *profileTypeArray = nullptr;
    if (!broker_->TryGetProfileTypeInfo(&profileTypeArray)) {
        return false;
    }
    uint32_t length = profileTypeArray->GetICSlotLength();
    if (slotId >= length || length - slotId <= 1) {
        return false;
    }

    JSTaggedValue targetValue = profileTypeArray->GetICSlot(compilerThread_, slotId);
    JSTaggedValue countValue = profileTypeArray->GetICSlot(compilerThread_, slotId + 1);
    // Report the addressed slot even when the site is not usable, matching the
    // upstream reader's contract (the coverage tests assert on it).
    feedback->slotId = slotId;
    if (targetValue.IsInt()) {
        int64_t rawTarget = static_cast<int64_t>(targetValue.GetInt());
        if (rawTarget < 0) {
            int64_t rawBuiltinId = -rawTarget;
            if (rawBuiltinId > kungfu::BuiltinsStubCSigns::NONE &&
                rawBuiltinId < kungfu::BuiltinsStubCSigns::ID::NUM_OF_BUILTINS_ID) {
                feedback->slotId = slotId;
                feedback->isMonomorphicBuiltin = true;
                feedback->builtinId = static_cast<kungfu::BuiltinsStubCSigns::ID>(rawBuiltinId);
            }
        }
        return true;
    }
    if (!targetValue.IsJSFunction()) {
        return true;
    }

    auto *target = JSFunction::Cast(targetValue.GetTaggedObject());
    const bool isClassConstructor = target->IsClassConstructor();
    JSTaggedValue methodValue = target->GetMethod(compilerThread_);
    if (!methodValue.IsMethod()) {
        return true;
    }

    feedback->slotId = slotId;
    feedback->target = broker_->MakeObjectRef(targetValue);
    feedback->method = broker_->MakeObjectRef(methodValue);
    if (!feedback->target.IsSafeForCompile() || !feedback->method.IsSafeForCompile()) {
        *feedback = {};
        return false;
    }
    feedback->expectedArgc = Method::Cast(methodValue.GetTaggedObject())->GetNumArgsWithCallField();
    feedback->methodId = Method::Cast(methodValue.GetTaggedObject())->GetMethodId().GetOffset();
    if (countValue.IsInt() && countValue.GetInt() > 0) {
        feedback->callCount = static_cast<uint32_t>(countValue.GetInt());
    } else if (countValue.IsProfileTypeInfoCell() &&
               target->GetRawProfileTypeInfo(compilerThread_) == countValue) {
        feedback->isCompatibleClosure = true;
        feedback->closureFamily = broker_->MakeObjectRef(countValue);
        feedback->callCount = 1;
    } else {
        *feedback = {};
        return false;
    }
    if (isClassConstructor) {
        // Super-call speculation consumes constructor feedback; trivial-call
        // reduction and the regular known-call/inline paths must not.
        feedback->isClassConstructor = true;
        feedback->isMonomorphic = true;
        return true;
    }
    AnalyzeTrivialCallReduction(compilerThread_, Method::Cast(methodValue.GetTaggedObject()), feedback);
    feedback->isMonomorphic = true;
    return true;
}

}  // namespace panda::ecmascript::arksteed
