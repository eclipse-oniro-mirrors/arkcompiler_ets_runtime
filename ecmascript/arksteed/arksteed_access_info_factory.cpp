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

#include "ecmascript/arksteed/arksteed_access_info_factory.h"

#include "ecmascript/ic/ic_handler.h"
#include "ecmascript/js_array.h"
#include "ecmascript/js_hclass.h"
#include "ecmascript/js_hclass-inl.h"
#include "ecmascript/tagged_array.h"

namespace panda::ecmascript::arksteed {

namespace {
constexpr size_t NAMED_STORE_INPUT_COUNT = 3;
constexpr size_t NAMED_ACCESS_SLOT_INPUT = 0;
constexpr size_t NAMED_ACCESS_NAME_INPUT = 1;
constexpr size_t NAMED_STORE_RECEIVER_INPUT = 2;
constexpr size_t ELEMENT_STORE_INPUT_COUNT = 3;
constexpr size_t ELEMENT_STORE_SLOT_INPUT = 0;
constexpr size_t ELEMENT_STORE_RECEIVER_INPUT = 1;
constexpr size_t ELEMENT_STORE_KEY_INPUT = 2;
constexpr size_t THIS_ELEMENT_STORE_INPUT_COUNT = 2;
constexpr size_t THIS_ELEMENT_STORE_KEY_INPUT = 1;

using ParsedStoreHandler = ArkSteedHeapBroker::StoreHandlerData;
using ParsedLoadHandler = ArkSteedHeapBroker::LoadHandlerData;

bool IsNamedLoadBytecode(kungfu::EcmaOpcode opcode)
{
    return opcode == kungfu::EcmaOpcode::LDOBJBYNAME_IMM8_ID16 || opcode == kungfu::EcmaOpcode::LDOBJBYNAME_IMM16_ID16;
}

bool IsValueLoadBytecode(kungfu::EcmaOpcode opcode)
{
    return opcode == kungfu::EcmaOpcode::LDOBJBYVALUE_IMM8_V8 || opcode == kungfu::EcmaOpcode::LDOBJBYVALUE_IMM16_V8 ||
           opcode == kungfu::EcmaOpcode::LDTHISBYVALUE_IMM8 || opcode == kungfu::EcmaOpcode::LDTHISBYVALUE_IMM16;
}

bool IsNamedStoreInputShape(const kungfu::BytecodeInfo &bytecodeInfo)
{
    return bytecodeInfo.inputs.size() == NAMED_STORE_INPUT_COUNT &&
           std::holds_alternative<kungfu::ICSlotId>(bytecodeInfo.inputs[NAMED_ACCESS_SLOT_INPUT]) &&
           std::holds_alternative<kungfu::ConstDataId>(bytecodeInfo.inputs[NAMED_ACCESS_NAME_INPUT]) &&
           std::holds_alternative<kungfu::VirtualRegister>(bytecodeInfo.inputs[NAMED_STORE_RECEIVER_INPUT]);
}

bool IsElementStoreInputShape(const kungfu::BytecodeInfo &bytecodeInfo)
{
    bool isStoreByValue = bytecodeInfo.GetOpcode() == kungfu::EcmaOpcode::STOBJBYVALUE_IMM8_V8_V8 ||
                          bytecodeInfo.GetOpcode() == kungfu::EcmaOpcode::STOBJBYVALUE_IMM16_V8_V8;
    if (isStoreByValue) {
        return bytecodeInfo.inputs.size() == ELEMENT_STORE_INPUT_COUNT &&
               std::holds_alternative<kungfu::ICSlotId>(bytecodeInfo.inputs[ELEMENT_STORE_SLOT_INPUT]) &&
               std::holds_alternative<kungfu::VirtualRegister>(bytecodeInfo.inputs[ELEMENT_STORE_RECEIVER_INPUT]) &&
               std::holds_alternative<kungfu::VirtualRegister>(bytecodeInfo.inputs[ELEMENT_STORE_KEY_INPUT]);
    }

    bool isStoreOwnByIndex = bytecodeInfo.GetOpcode() == kungfu::EcmaOpcode::STOWNBYINDEX_IMM8_V8_IMM16 ||
                             bytecodeInfo.GetOpcode() == kungfu::EcmaOpcode::STOWNBYINDEX_IMM16_V8_IMM16;
    if (isStoreOwnByIndex) {
        // Same slot/receiver layout as STOBJBYVALUE, but the key is an immediate index.
        return bytecodeInfo.inputs.size() == ELEMENT_STORE_INPUT_COUNT &&
               std::holds_alternative<kungfu::ICSlotId>(bytecodeInfo.inputs[ELEMENT_STORE_SLOT_INPUT]) &&
               std::holds_alternative<kungfu::VirtualRegister>(bytecodeInfo.inputs[ELEMENT_STORE_RECEIVER_INPUT]) &&
               std::holds_alternative<kungfu::Immediate>(bytecodeInfo.inputs[ELEMENT_STORE_KEY_INPUT]);
    }

    bool isStoreThisByValue = bytecodeInfo.GetOpcode() == kungfu::EcmaOpcode::STTHISBYVALUE_IMM8_V8 ||
                              bytecodeInfo.GetOpcode() == kungfu::EcmaOpcode::STTHISBYVALUE_IMM16_V8;
    return isStoreThisByValue && bytecodeInfo.inputs.size() == THIS_ELEMENT_STORE_INPUT_COUNT &&
           std::holds_alternative<kungfu::ICSlotId>(bytecodeInfo.inputs[ELEMENT_STORE_SLOT_INPUT]) &&
           std::holds_alternative<kungfu::VirtualRegister>(bytecodeInfo.inputs[THIS_ELEMENT_STORE_KEY_INPUT]);
}

bool IsSupportedTypedArrayType(JSType type)
{
    switch (type) {
        case JSType::JS_INT8_ARRAY:
        case JSType::JS_UINT8_ARRAY:
        case JSType::JS_UINT8_CLAMPED_ARRAY:
        case JSType::JS_INT16_ARRAY:
        case JSType::JS_UINT16_ARRAY:
        case JSType::JS_INT32_ARRAY:
        case JSType::JS_UINT32_ARRAY:
        case JSType::JS_FLOAT32_ARRAY:
        case JSType::JS_FLOAT64_ARRAY:
            return true;
        default:
            return false;
    }
}

bool CanApplyExplicitElementTransition(JSHClass *source, JSHClass *target)
{
    if (source == nullptr || target == nullptr || !source->IsJSArray() || !target->IsJSArray() ||
        source->IsPrototype() || target->IsPrototype()) {
        return false;
    }
    return source == target || Elements::MergeElementsKind(source->GetElementsKind(), target->GetElementsKind()) ==
                                   target->GetElementsKind();
}

bool BuildElementStoreTransitionGroups(const ArkSteedHeapBroker *broker,
                                       const std::array<JSHClass *, MAX_ELEMENT_IC_POLY_CASES> &hclasses,
                                       const std::array<JSHClass *, MAX_ELEMENT_IC_POLY_CASES> &explicitTargets,
                                       const std::array<ArkSteedHClassRef,
                                                        MAX_ELEMENT_IC_POLY_CASES> &explicitTargetRefs,
                                       ElementStoreAccessInfo *access)
{
    std::array<JSHClass *, MAX_ELEMENT_IC_POLY_CASES> selectedTargets {};
    std::array<ArkSteedHClassRef, MAX_ELEMENT_IC_POLY_CASES> selectedTargetRefs {};
    std::array<bool, MAX_ELEMENT_IC_POLY_CASES> selectedTargetsAreExplicit {};
    for (uint32_t sourceIndex = 0; sourceIndex < access->caseCount; ++sourceIndex) {
        if (explicitTargets[sourceIndex] != nullptr) {
            if (!CanApplyExplicitElementTransition(hclasses[sourceIndex], explicitTargets[sourceIndex])) {
                return false;
            }
            selectedTargets[sourceIndex] = explicitTargets[sourceIndex];
            selectedTargetRefs[sourceIndex] = explicitTargetRefs[sourceIndex];
            selectedTargetsAreExplicit[sourceIndex] = true;
            continue;
        }

        std::array<uint32_t, MAX_ELEMENT_IC_POLY_CASES> candidates {};
        uint32_t candidateCount = 0;
        candidates[candidateCount++] = sourceIndex;
        for (uint32_t targetIndex = 0; targetIndex < access->caseCount; ++targetIndex) {
            if (broker->CanTransitionElementsKind(hclasses[sourceIndex], hclasses[targetIndex])) {
                candidates[candidateCount++] = targetIndex;
            }
        }

        uint32_t widestTarget = sourceIndex;
        uint32_t widestTargetCount = 0;
        for (uint32_t candidateIndex = 0; candidateIndex < candidateCount; ++candidateIndex) {
            uint32_t possibleTarget = candidates[candidateIndex];
            bool containsAllCandidates = true;
            for (uint32_t otherIndex = 0; otherIndex < candidateCount; ++otherIndex) {
                uint32_t possibleSource = candidates[otherIndex];
                if (possibleSource != possibleTarget &&
                    !broker->CanTransitionElementsKind(hclasses[possibleSource], hclasses[possibleTarget])) {
                    containsAllCandidates = false;
                    break;
                }
            }
            if (containsAllCandidates) {
                widestTarget = possibleTarget;
                widestTargetCount++;
            }
        }
        if (widestTargetCount != 1) {
            widestTarget = sourceIndex;
        }
        selectedTargets[sourceIndex] = hclasses[widestTarget];
        selectedTargetRefs[sourceIndex] = access->cases[widestTarget].expectedHClass;
    }

    // A group stores its target once and lists every source that must transition to it before the element store.
    std::array<JSHClass *, MAX_ELEMENT_IC_POLY_CASES> groupTargets {};
    for (uint32_t caseIndex = 0; caseIndex < access->caseCount; ++caseIndex) {
        JSHClass *target = selectedTargets[caseIndex];
        uint32_t groupIndex = 0;
        for (; groupIndex < access->transitionGroupCount; ++groupIndex) {
            if (groupTargets[groupIndex] == target) {
                break;
            }
        }
        if (groupIndex == access->transitionGroupCount) {
            if (access->transitionGroupCount >= MAX_ELEMENT_IC_POLY_CASES) {
                return false;
            }
            ElementStoreTransitionGroup &group = access->transitionGroups[access->transitionGroupCount++];
            group.targetHClass = selectedTargetRefs[caseIndex];
            group.targetElementsKind = target->GetElementsKind();
            group.useExactHClassTransition = selectedTargetsAreExplicit[caseIndex];
            groupTargets[groupIndex] = target;
        } else {
            access->transitionGroups[groupIndex].useExactHClassTransition =
                access->transitionGroups[groupIndex].useExactHClassTransition || selectedTargetsAreExplicit[caseIndex];
        }
        if (hclasses[caseIndex] == target) {
            continue;
        }
        ElementStoreTransitionGroup &group = access->transitionGroups[groupIndex];
        if (group.sourceCount >= MAX_ELEMENT_IC_POLY_CASES) {
            return false;
        }
        group.transitionSources[group.sourceCount++] = access->cases[caseIndex].expectedHClass;
    }
    return access->transitionGroupCount > 0;
}

void SetFieldLocation(uint64_t handlerInfo, PropertyAccessInfo *info)
{
    info->fieldIndex = HandlerBase::GetOffset(handlerInfo);
    info->fieldStorage =
        HandlerBase::IsInlinedProps(handlerInfo) ? AccessFieldStorage::IN_OBJECT : AccessFieldStorage::PROPERTIES_ARRAY;
    uint32_t baseOffset = info->fieldStorage == AccessFieldStorage::IN_OBJECT ? 0 : TaggedArray::DATA_OFFSET;
    info->fieldOffset = static_cast<int32_t>(baseOffset + info->fieldIndex * JSTaggedValue::TaggedTypeSize());
    Representation representation = HandlerBase::RepresentationBit::Get(handlerInfo);
    if (representation == Representation::TAGGED) {
        info->fieldRepresentation = AccessFieldRepresentation::TAGGED;
    } else if (representation == Representation::INT) {
        info->fieldRepresentation = AccessFieldRepresentation::INT32;
    } else if (representation == Representation::DOUBLE) {
        info->fieldRepresentation = AccessFieldRepresentation::DOUBLE;
    }
    info->fieldType = AccessFieldType::ANY;
}

bool SetFieldRepresentation(Representation representation, PropertyAccessInfo *info)
{
    if (representation == Representation::TAGGED) {
        info->fieldRepresentation = AccessFieldRepresentation::TAGGED;
    } else if (representation == Representation::INT) {
        info->fieldRepresentation = AccessFieldRepresentation::INT32;
    } else if (representation == Representation::DOUBLE) {
        info->fieldRepresentation = AccessFieldRepresentation::DOUBLE;
    } else {
        return false;
    }
    info->fieldType = AccessFieldType::ANY;
    return true;
}

bool TrySetStoreFieldLocationFromHClass(const ArkSteedHeapBroker *broker,
                                        const NamedAccessCaseFeedback &caseFeedback,
                                        const ParsedStoreHandler &parsed, ArkSteedNameRef name,
                                        PropertyAccessInfo *info)
{
    if (broker->TryGetHClass(caseFeedback.expectedHClass) == nullptr) {
        return false;
    }
    ArkSteedHClassRef fieldRef = parsed.hasTransitionHClass ? parsed.transitionHClass :
        (parsed.holderIsReceiver ? caseFeedback.expectedHClass : parsed.holderHClass);
    auto result = broker->TryLookupProperty(broker->TryGetHClass(fieldRef), name);
    if (!result.has_value()) {
        return false;
    }
    PropertyLookupResult lookup = result.value();
    if (!lookup.IsFound() || !lookup.IsLocal() || lookup.IsAccessor() || !lookup.IsWritable() ||
        !SetFieldRepresentation(lookup.GetRepresentation(), info)) {
        return false;
    }

    info->fieldHClass = fieldRef;
    info->hasFieldHClass = true;
    info->fieldOwnerHClass = info->fieldHClass;
    info->fieldIndex = lookup.GetOffset();
    if (lookup.IsInlinedProps()) {
        info->fieldStorage = AccessFieldStorage::IN_OBJECT;
        info->fieldOffset = static_cast<int32_t>(lookup.GetOffset());
    } else {
        info->fieldStorage = AccessFieldStorage::PROPERTIES_ARRAY;
        info->fieldOffset =
            static_cast<int32_t>(TaggedArray::DATA_OFFSET + lookup.GetOffset() * JSTaggedValue::TaggedTypeSize());
    }
    return true;
}

void FillNamedAccessInfo(const NamedAccessCaseFeedback &caseFeedback, uint64_t handlerInfo,
                         ArkSteedProtoCellRef protoCell, bool hasProtoCell, PropertyAccessInfo *info)
{
    info->expectedHClass = caseFeedback.expectedHClass;
    info->protoCell = protoCell;
    info->handlerInfo = handlerInfo;
    info->hasProtoCell = hasProtoCell;
    info->guards.expectedHClass = caseFeedback.expectedHClass;
    info->guards.protoCell = protoCell;
    info->guards.hasHClassGuard = true;
    info->guards.hasProtoCellGuard = hasProtoCell;
    info->dependencies.hclassDependency = AccessDependencyKind::HCLASS;
    if (hasProtoCell) {
        info->dependencies.protoChainDependency = AccessDependencyKind::PROTOTYPE_CHAIN;
    }
}
}  // namespace

bool ArkSteedAccessInfoFactory::TryMakeNamedStoreAccessInfo(const NamedAccessCaseFeedback &caseFeedback,
                                                            ArkSteedNameRef name, NamedStoreAccessInfo *info) const
{
    *info = {};
    if (!caseFeedback.expectedHClass.IsSafeForCompile() || !caseFeedback.handler.IsSafeForCompile()) {
        LOG_COMPILER(DEBUG) << "ArkSteedPGO: named store unsafe feedback hclass="
                            << caseFeedback.expectedHClass.IsSafeForCompile()
                            << " handler=" << caseFeedback.handler.IsSafeForCompile();
        return false;
    }

    ParsedStoreHandler parsed;
    if (!broker_->TryReadStoreHandler(caseFeedback.handler, &parsed)) {
        LOG_COMPILER(DEBUG) << "ArkSteedPGO: named store cannot read handler";
        return false;
    }

    bool isSharedStore = HandlerBase::IsStoreShared(parsed.handlerInfo);

    uint64_t fieldHandlerInfo = parsed.handlerInfo;
    if (isSharedStore) {
        HandlerBase::ClearSharedStoreKind(fieldHandlerInfo);
    }
    bool isField = HandlerBase::IsNonSharedStoreField(fieldHandlerInfo);
    Representation representation = HandlerBase::RepresentationBit::Get(fieldHandlerInfo);
    bool hasSupportedFieldRep = representation == Representation::TAGGED || representation == Representation::INT ||
                                representation == Representation::DOUBLE;
    bool isAccessor = HandlerBase::IsAccessor(parsed.handlerInfo) && !isSharedStore;
    if (isSharedStore &&
        (!isField || HandlerBase::IsAccessor(parsed.handlerInfo) || representation != Representation::TAGGED)) {
        LOG_COMPILER(DEBUG) << "ArkSteedPGO: named store unsupported shared handler handler=0x" << std::hex
                            << parsed.handlerInfo << std::dec << " isField=" << isField
                            << " accessor=" << HandlerBase::IsAccessor(parsed.handlerInfo)
                            << " rep=" << static_cast<int>(representation);
        return false;
    }
    if (isSharedStore && (parsed.hasProtoCell || !parsed.holderIsReceiver)) {
        LOG_COMPILER(DEBUG) << "ArkSteedPGO: named store unsupported shared proto/holder handler=0x" << std::hex
                            << parsed.handlerInfo << std::dec << " hasProtoCell=" << parsed.hasProtoCell
                            << " holderIsReceiver=" << parsed.holderIsReceiver;
        return false;
    }
    if (!(isField && hasSupportedFieldRep) && !isAccessor && !isSharedStore) {
        LOG_COMPILER(DEBUG) << "ArkSteedPGO: named store unsupported handler handler=0x" << std::hex
                            << parsed.handlerInfo << std::dec << " isField=" << isField
                            << " rep=" << static_cast<int>(representation) << " isAccessor=" << isAccessor
                            << " isShared=" << isSharedStore;
        return false;
    }

    info->mode = AccessMode::NAMED_STORE;
    info->kind = isAccessor ? AccessKind::ACCESSOR
                            : (parsed.hasTransitionHClass
                                   ? AccessKind::TRANSITION
                                   : (parsed.hasProtoCell ? AccessKind::PROTOTYPE_FIELD : AccessKind::FIELD));
    info->holder = parsed.holder;
    info->holderHClass = parsed.holderIsReceiver ? caseFeedback.expectedHClass : parsed.holderHClass;
    info->holderIsReceiver = parsed.holderIsReceiver;
    info->guards.holder = parsed.holder;
    info->guards.hasHolder = !parsed.holderIsReceiver && parsed.holder.IsSafeForCompile();
    info->guards.holderIsReceiver = parsed.holderIsReceiver;
    FillNamedAccessInfo(caseFeedback, parsed.handlerInfo, parsed.protoCell, parsed.hasProtoCell, info);
    if (parsed.hasTransitionHClass) {
        info->dependencies.hclassDependency = AccessDependencyKind::NONE;
        info->dependencies.protoChainDependency = AccessDependencyKind::PROTOTYPE_CHAIN;
        info->dependencies.notPrototypeDependency = AccessDependencyKind::NOT_PROTOTYPE;
    }
    info->transitionHClass = parsed.transitionHClass;
    info->hasTransitionHClass = parsed.hasTransitionHClass;
    info->isSharedStore = isSharedStore;
    if (isField && !TrySetStoreFieldLocationFromHClass(broker_, caseFeedback, parsed, name, info)) {
        LOG_COMPILER(DEBUG) << "ArkSteedPGO: named store field no longer matches guarded hclass";
        return false;
    }
    if (isAccessor) {
        SetFieldLocation(fieldHandlerInfo, info);
        info->fieldHClass = info->holderHClass;
        info->hasFieldHClass = info->fieldHClass.IsSafeForCompile();
        if (!info->hasFieldHClass) {
            return false;
        }
        info->fieldOwnerHClass = info->fieldHClass;
    }
    return true;
}

bool ArkSteedAccessInfoFactory::TryMakeNamedLoadAccessInfo(const NamedAccessCaseFeedback &caseFeedback,
                                                           ArkSteedNameRef name,
                                                           NamedLoadAccessInfo *info) const
{
    *info = {};
    if (!caseFeedback.expectedHClass.IsSafeForCompile() || !caseFeedback.handler.IsSafeForCompile()) {
        return false;
    }

    ParsedLoadHandler parsed;
    if (!broker_->TryReadLoadHandler(caseFeedback.handler, &parsed)) {
        return false;
    }
    if (parsed.holderIsReceiver && HandlerBase::IsAccessor(parsed.handlerInfo)) {
        if (broker_->IsJSArrayLengthAccessor(caseFeedback.expectedHClass, name)) {
            // Normalize the built-in accessor to its mutable uint32 field, not the accessor slot.
            info->kind = AccessKind::DATA_FIELD;
            info->fieldStorage = AccessFieldStorage::IN_OBJECT;
            info->fieldOffset = JSArray::LENGTH_OFFSET;
            info->fieldRepresentation = AccessFieldRepresentation::UINT32;
            info->fieldType = AccessFieldType::NUMBER;
            info->holderHClass = caseFeedback.expectedHClass;
            info->fieldOwnerHClass = caseFeedback.expectedHClass;
            FillNamedAccessInfo(caseFeedback, parsed.handlerInfo, {}, false, info);
            // Length has a fixed layout even on non-leaf HClasses; the receiver guard is sufficient.
            info->dependencies.hclassDependency = AccessDependencyKind::NONE;
            return true;
        }
    }
    if (!parsed.holderIsReceiver) {
        if (!parsed.holder.IsSafeForCompile() || !parsed.holderHClass.IsSafeForCompile()) {
            return false;
        }
        parsed.holderDepth = 1;
    }

    bool isTypedArrayLength = broker_->IsTypedArrayLengthAccessor(caseFeedback.expectedHClass, parsed, name);
    if (!isTypedArrayLength && !HandlerBase::IsNonExist(parsed.handlerInfo)) {
        if (!HandlerBase::IsField(parsed.handlerInfo) || HandlerBase::IsAccessor(parsed.handlerInfo) ||
            HandlerBase::RepresentationBit::Get(parsed.handlerInfo) != Representation::TAGGED) {
            return false;
        }
        SetFieldLocation(parsed.handlerInfo, info);
    }

    info->mode = AccessMode::NAMED_LOAD;
    info->kind = HandlerBase::IsNonExist(parsed.handlerInfo) ? AccessKind::NON_EXIST :
        (parsed.hasProtoCell ? AccessKind::PROTOTYPE_FIELD : AccessKind::FIELD);
    if (isTypedArrayLength) {
        info->kind = AccessKind::TYPED_ARRAY_LENGTH;
    }
    info->holder = parsed.holder;
    info->holderHClass = parsed.holderIsReceiver ? caseFeedback.expectedHClass : parsed.holderHClass;
    info->fieldOwnerHClass = parsed.holderIsReceiver ? caseFeedback.expectedHClass : parsed.holderHClass;
    info->fieldHClass = info->fieldOwnerHClass;
    info->hasFieldHClass = info->fieldOwnerHClass.IsSafeForCompile();
    info->holderDepth = parsed.holderDepth;
    info->holderIsReceiver = parsed.holderIsReceiver;
    info->hasNotFoundProtoCellGuard = parsed.hasProtoCell && HandlerBase::IsNonExist(parsed.handlerInfo);
    info->guards.holder = parsed.holder;
    info->guards.hasHolder = !parsed.holderIsReceiver && parsed.holder.IsSafeForCompile();
    info->guards.holderIsReceiver = parsed.holderIsReceiver;
    info->guards.hasNotFoundProtoCellGuard = info->hasNotFoundProtoCellGuard;
    FillNamedAccessInfo(caseFeedback, parsed.handlerInfo, parsed.protoCell, parsed.hasProtoCell, info);
    if (isTypedArrayLength) {
        info->dependencies.hclassDependency = AccessDependencyKind::NONE;
    }
    return true;
}

bool ArkSteedAccessInfoFactory::TryBuildNamedStoreAccessInfo(int slotIndex, NamedStoreAccessSet *access) const
{
    *access = {};
    if (!IsNamedStoreInputShape(bytecodeInfo_)) {
        return false;
    }
    NamedAccessFeedback feedback;
    if (!broker_->GetFeedbackForNamedAccess(feedbackReader_, slotIndex, &feedback)) {
        return false;
    }
    return ComputeNamedStoreAccessInfo(feedback, access);
}

bool ArkSteedAccessInfoFactory::TryBuildElementStoreAccessInfo(int slotIndex, ElementStoreAccessInfo *access) const
{
    *access = {};
    if (!IsElementStoreInputShape(bytecodeInfo_)) {
        return false;
    }

    ElementAccessFeedback feedback;
    if (!broker_->GetFeedbackForElementAccess(feedbackReader_, slotIndex, &feedback)) {
        return false;
    }

    access->feedback = feedback.base.source;
    bool hasTaggedArrayBacking = env_ != nullptr && !env_->GetJSOptions().IsEnableMutantArray();
    std::array<JSHClass *, MAX_ELEMENT_IC_POLY_CASES> hclasses {};
    std::array<JSHClass *, MAX_ELEMENT_IC_POLY_CASES> explicitTargets {};
    std::array<ArkSteedHClassRef, MAX_ELEMENT_IC_POLY_CASES> explicitTargetRefs {};
    std::array<bool, MAX_ELEMENT_IC_POLY_CASES> needsProtoDependency {};
    for (uint32_t i = 0; i < feedback.caseCount; ++i) {
        const ElementAccessCaseFeedback &elementCase = feedback.cases[i];
        ParsedStoreHandler parsed;
        JSHClass *hclass = broker_->TryGetHClass(elementCase.expectedHClass);
        if (hclass == nullptr || !broker_->TryReadStoreHandler(elementCase.handler, &parsed)) {
            *access = {};
            return false;
        }

        uint64_t handlerInfo = parsed.handlerInfo;
        // STOWNBYINDEX stores a compile-time constant index into the array built by the surrounding
        // literal, so its handlers are always marked store-out-of-bounds: the store appends at or
        // beyond the current length. That deterministic append case is lowered explicitly, so the
        // bit only rejects stores with a runtime-computed key.
        bool isOwnByIndexStore = bytecodeInfo_.GetOpcode() == kungfu::EcmaOpcode::STOWNBYINDEX_IMM8_V8_IMM16 ||
                                 bytecodeInfo_.GetOpcode() == kungfu::EcmaOpcode::STOWNBYINDEX_IMM16_V8_IMM16;
        if ((HandlerBase::IsStoreOutOfBounds(handlerInfo) && !isOwnByIndexStore) || hclass->IsJSShared()) {
            *access = {};
            return false;
        }

        ElementStoreKind caseKind = ElementStoreKind::UNSUPPORTED;
        ElementsKind elementsKind = ElementsKind::NONE;
        JSType typedArrayType = JSType::INVALID;
        OnHeapMode onHeapMode = OnHeapMode::NONE;
        if (hclass->IsJSArray() && !hclass->IsPrototype() && hclass->IsStableElements() && hasTaggedArrayBacking &&
            !hclass->IsDictionaryElement() && HandlerBase::IsNormalElement(handlerInfo) &&
            HandlerBase::IsJSArray(handlerInfo)) {
            caseKind = ElementStoreKind::JS_ARRAY;
            access->handlesCOW |= HandlerBase::StoreHandlesCOW(handlerInfo);
            elementsKind = hclass->GetElementsKind();
            if (parsed.hasTransitionHClass) {
                JSHClass *transitionHClass = broker_->TryGetHClass(parsed.transitionHClass);
                if (transitionHClass == nullptr || !transitionHClass->IsJSArray() || transitionHClass->IsPrototype() ||
                    !transitionHClass->IsStableElements() || transitionHClass->IsDictionaryElement()) {
                    *access = {};
                    return false;
                }
                explicitTargets[access->caseCount] = transitionHClass;
                explicitTargetRefs[access->caseCount] = parsed.transitionHClass;
            }
            needsProtoDependency[access->caseCount] =
                parsed.hasProtoCell || hclass->IsJSArrayPrototypeModifiedFromBitField();
        } else if (hclass->IsTypedArray() && HandlerBase::IsTypedArrayElement(handlerInfo) &&
                   IsSupportedTypedArrayType(hclass->GetObjectType()) && !parsed.hasTransitionHClass &&
                   !parsed.hasProtoCell) {
            caseKind = ElementStoreKind::TYPED_ARRAY;
            typedArrayType = hclass->GetObjectType();
            bool isOnHeap = HandlerBase::IsOnHeap(handlerInfo);
            if (isOnHeap != hclass->IsOnHeapFromBitField()) {
                *access = {};
                return false;
            }
            onHeapMode = isOnHeap ? OnHeapMode::ON_HEAP : OnHeapMode::NOT_ON_HEAP;
        } else {
            *access = {};
            return false;
        }

        if (access->caseCount == 0) {
            access->kind = caseKind;
            access->typedArrayType = typedArrayType;
            access->onHeapMode = onHeapMode;
        } else if (caseKind != access->kind ||
                   (caseKind == ElementStoreKind::TYPED_ARRAY && typedArrayType != access->typedArrayType)) {
            *access = {};
            return false;
        } else if (caseKind == ElementStoreKind::TYPED_ARRAY) {
            access->onHeapMode = OnHeap::Merge(access->onHeapMode, onHeapMode);
        }

        for (uint32_t caseIndex = 0; caseIndex < access->caseCount; ++caseIndex) {
            if (hclasses[caseIndex] == hclass) {
                *access = {};
                return false;
            }
        }
        hclasses[access->caseCount] = hclass;
        access->cases[access->caseCount++] = ElementStoreAccessCase {elementCase.expectedHClass, elementsKind};
    }

    if (access->caseCount == 0) {
        return false;
    }
    if (access->IsJSArray() &&
        !BuildElementStoreTransitionGroups(broker_, hclasses,
                                           explicitTargets, explicitTargetRefs, access)) {
        *access = {};
        return false;
    }
    if (access->IsJSArray() && !dependencyRecorder_.InstallArrayDetector()) {
        *access = {};
        return false;
    }
    for (uint32_t i = 0; i < access->caseCount; ++i) {
        if (!dependencyRecorder_.InstallStableHClass(access->cases[i].expectedHClass) ||
            (access->IsJSArray() && !dependencyRecorder_.InstallNotPrototype(access->cases[i].expectedHClass)) ||
            (needsProtoDependency[i] &&
             !dependencyRecorder_.InstallStableProtoChain(access->cases[i].expectedHClass))) {
            *access = {};
            return false;
        }
    }
    std::array<JSHClass *, MAX_ELEMENT_IC_POLY_CASES> installedExplicitTargets {};
    uint32_t installedExplicitTargetCount = 0;
    for (uint32_t i = 0; access->IsJSArray() && i < access->caseCount; ++i) {
        JSHClass *target = explicitTargets[i];
        auto feedbackEnd = hclasses.begin() + access->caseCount;
        auto installedEnd = installedExplicitTargets.begin() + installedExplicitTargetCount;
        bool isFeedbackHClass = target != nullptr && std::find(hclasses.begin(), feedbackEnd, target) != feedbackEnd;
        bool isAlreadyInstalled =
            target != nullptr && std::find(installedExplicitTargets.begin(), installedEnd, target) != installedEnd;
        if (target == nullptr || isFeedbackHClass || isAlreadyInstalled) {
            continue;
        }
        if (!dependencyRecorder_.InstallStableHClass(explicitTargetRefs[i]) ||
            !dependencyRecorder_.InstallNotPrototype(explicitTargetRefs[i])) {
            *access = {};
            return false;
        }
        installedExplicitTargets[installedExplicitTargetCount++] = target;
    }
    return true;
}

bool ArkSteedAccessInfoFactory::ComputeNamedStoreAccessInfo(const NamedAccessFeedback &feedback,
                                                            NamedStoreAccessSet *access) const
{
    *access = {};
    access->slotId = feedback.base.source.slotId;
    access->isPoly = feedback.base.source.isPoly;
    access->feedback = feedback.base.source;
    access->feedback.slotKind = AccessFeedbackSlotKind::NAMED_STORE;

    for (uint32_t i = 0; i < feedback.caseCount && access->caseCount < MAX_NAMED_IC_POLY_CASES; ++i) {
        NamedStoreAccessInfo info;
        if (!TryMakeNamedStoreAccessInfo(feedback.cases[i], feedback.name, &info)) {
            *access = {};
            return false;
        }
        access->cases[access->caseCount] = info;
        access->cases[access->caseCount].name = feedback.name;
        access->caseCount++;
    }
    bool success = feedback.base.source.isPoly ? access->caseCount >= 2 : access->caseCount == 1;
    if (!success) {
        *access = {};
        return false;
    }
    if (!RegisterDependencies(access)) {
        *access = {};
        return false;
    }
    return true;
}

bool ArkSteedAccessInfoFactory::TryBuildNamedLoadAccessInfo(int slotIndex, NamedLoadAccessSet *access) const
{
    *access = {};
    if (!IsNamedLoadBytecode(bytecodeInfo_.GetOpcode())) {
        return false;
    }
    NamedAccessFeedback feedback;
    if (!broker_->GetFeedbackForNamedAccess(feedbackReader_, slotIndex, &feedback)) {
        return false;
    }
    return ComputeNamedLoadAccessInfo(feedback, AccessFeedbackSlotKind::NAMED_LOAD, access);
}

bool ArkSteedAccessInfoFactory::ComputeNamedLoadAccessInfo(const NamedAccessFeedback &feedback,
                                                           AccessFeedbackSlotKind slotKind,
                                                           NamedLoadAccessSet *access) const
{
    *access = {};
    access->slotId = feedback.base.source.slotId;
    access->isPoly = feedback.base.source.isPoly;
    access->feedback = feedback.base.source;
    access->feedback.slotKind = slotKind;

    for (uint32_t i = 0; i < feedback.caseCount && access->caseCount < MAX_NAMED_IC_POLY_CASES; ++i) {
        NamedLoadAccessInfo info;
        if (!TryMakeNamedLoadAccessInfo(feedback.cases[i], feedback.name, &info)) {
            *access = {};
            return false;
        }
        access->cases[access->caseCount] = info;
        access->cases[access->caseCount].name = feedback.name;
        access->caseCount++;
    }
    bool success = feedback.base.source.isPoly ? access->caseCount >= 2 : access->caseCount == 1;
    if (!success) {
        *access = {};
        return false;
    }
    if (!RegisterDependencies(access)) {
        *access = {};
        return false;
    }
    return true;
}

bool ArkSteedAccessInfoFactory::TryBuildValueLoadAccessInfo(ValueLoadAccessSet *access) const
{
    *access = {};
    if (!IsValueLoadBytecode(bytecodeInfo_.GetOpcode())) {
        return false;
    }

    ValueAccessFeedback feedback;
    if (!broker_->GetFeedbackForValueAccess(feedbackReader_, &feedback)) {
        return false;
    }

    access->feedback = feedback.base.source;
    access->feedback.slotKind = AccessFeedbackSlotKind::VALUE_LOAD;
    if (feedback.kind == ValueAccessFeedbackKind::NAMED) {
        NamedAccessFeedback namedFeedback;
        namedFeedback.base = feedback.base;
        namedFeedback.name = feedback.key;
        namedFeedback.cases = feedback.cases;
        namedFeedback.caseCount = feedback.caseCount;
        if (!ComputeNamedLoadAccessInfo(namedFeedback, AccessFeedbackSlotKind::VALUE_LOAD, &access->named)) {
            *access = {};
            return false;
        }
        access->kind = ValueLoadAccessKind::NAMED;
        access->key = feedback.key;
        return true;
    }

    if (feedback.kind != ValueAccessFeedbackKind::ELEMENT) {
        *access = {};
        return false;
    }
    if (feedback.caseCount == 0 || feedback.caseCount > access->elements.size()) {
        *access = {};
        return false;
    }
    for (uint32_t index = 0; index < feedback.caseCount; ++index) {
        const auto &handler = feedback.cases[index].handler;
        if (!handler.IsInt()) {
            *access = {};
            return false;
        }
        uint64_t handlerInfo = handler.Value().GetLargeUInt();
        ElementLoadKind kind = ElementLoadKind::UNSUPPORTED;
        if (HandlerBase::IsNormalElement(handlerInfo)) {
            kind = ElementLoadKind::NORMAL;
        } else if (HandlerBase::IsStringElement(handlerInfo)) {
            kind = ElementLoadKind::STRING;
        } else if (HandlerBase::IsTypedArrayElement(handlerInfo)) {
            kind = ElementLoadKind::TYPED_ARRAY;
        } else {
            *access = {};
            return false;
        }
        access->elements[access->elementCount++] = {
            .expectedHClass = feedback.cases[index].expectedHClass,
            .handlerInfo = handlerInfo,
            .kind = kind,
        };
    }
    if (access->elementCount == 0) {
        *access = {};
        return false;
    }
    access->kind = ValueLoadAccessKind::ELEMENT;
    return true;
}

bool ArkSteedAccessInfoFactory::RegisterDependencies(PropertyAccessSet *access) const
{
    return dependencyRecorder_.Install(access);
}

}  // namespace panda::ecmascript::arksteed
