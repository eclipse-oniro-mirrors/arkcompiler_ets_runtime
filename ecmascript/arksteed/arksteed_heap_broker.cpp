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

#include "ecmascript/arksteed/arksteed_heap_broker.h"

#include "ecmascript/arksteed/arksteed_feedback_reader.h"
#include "ecmascript/accessor_data.h"
#include "ecmascript/builtins/builtins_typedarray.h"
#include "ecmascript/ic/ic_handler.h"
#include "ecmascript/js_hclass-inl.h"
#include "ecmascript/js_object-inl.h"
#include "ecmascript/layout_info-inl.h"
#include "ecmascript/compiler/lazy_deopt_dependency.h"
#include "ecmascript/ecma_string-inl.h"
#include "ecmascript/global_env.h"
#include "ecmascript/mem/region.h"

#include "ecmascript/arksteed/arksteed_opcode.h"
#include "ecmascript/builtin_entries.h"
#include "ecmascript/jspandafile/program_object.h"
#include "ecmascript/layout_info.h"
#include "ecmascript/mem/mem.h"
#include "ecmascript/module/js_module_manager.h"

namespace panda::ecmascript::arksteed {

namespace
{
using ParsedStoreHandler = ArkSteedHeapBroker::StoreHandlerData;
using ParsedLoadHandler = ArkSteedHeapBroker::LoadHandlerData;

bool TryReadPrototypeStoreHandler(const ArkSteedHeapBroker *broker, JSThread *compilerThread,
                                  JSTaggedValue cachedHandler, ParsedStoreHandler *result)
{
    if (cachedHandler.IsWeak() || !cachedHandler.IsPrototypeHandler()) {
        return false;
    }
    auto *prototypeHandler = PrototypeHandler::Cast(cachedHandler.GetTaggedObject());
    JSTaggedValue protoCellValue = prototypeHandler->GetProtoCell(compilerThread);
    if (protoCellValue.IsNull() || !protoCellValue.IsProtoChangeMarker()) {
        return false;
    }
    JSTaggedValue handlerInfoValue = prototypeHandler->GetHandlerInfo(compilerThread);
    if (!handlerInfoValue.IsInt()) {
        return false;
    }
    result->handlerInfo = handlerInfoValue.GetLargeUInt();
    JSTaggedValue holderValue = prototypeHandler->GetHolder(compilerThread);
    if (!holderValue.IsUndefined()) {
        if (!holderValue.IsHeapObject()) {
            return false;
        }
        result->holder = broker->MakeObjectRef(holderValue);
        result->holderHClass = broker->MakeHClassRef(JSTaggedValue(holderValue.GetTaggedObject()->GetClass()));
        result->holderIsReceiver = false;
    }
    result->protoCell = broker->MakeProtoCellRef(protoCellValue);
    result->hasProtoCell = true;
    bool hasValidHolder =
        result->holderIsReceiver || (result->holder.IsSafeForCompile() && result->holderHClass.IsSafeForCompile());
    return hasValidHolder && result->protoCell.IsSafeForCompile();
}

bool TryReadTransitionStoreHandler(const ArkSteedHeapBroker *broker, JSThread *compilerThread,
                                   JSTaggedValue cachedHandler, ParsedStoreHandler *result)
{
    if (cachedHandler.IsWeak() || !cachedHandler.IsTransitionHandler()) {
        return false;
    }
    auto *transitionHandler = TransitionHandler::Cast(cachedHandler.GetTaggedObject());
    JSTaggedValue handlerInfoValue = transitionHandler->GetHandlerInfo(compilerThread);
    if (!handlerInfoValue.IsInt()) {
        return false;
    }
    JSTaggedValue transitionHClassValue = transitionHandler->GetTransitionHClass(compilerThread);
    result->handlerInfo = handlerInfoValue.GetLargeUInt();
    result->transitionHClass = broker->MakeHClassRef(transitionHClassValue);
    result->hasTransitionHClass = true;
    return result->transitionHClass.IsJSHClass();
}

bool TryReadTransWithProtoStoreHandler(const ArkSteedHeapBroker *broker, JSThread *compilerThread,
                                       JSTaggedValue cachedHandler, ParsedStoreHandler *result)
{
    if (cachedHandler.IsWeak() || !cachedHandler.IsTransWithProtoHandler()) {
        return false;
    }
    auto *transWithProtoHandler = TransWithProtoHandler::Cast(cachedHandler.GetTaggedObject());
    JSTaggedValue protoCellValue = transWithProtoHandler->GetProtoCell(compilerThread);
    if (!protoCellValue.IsProtoChangeMarker()) {
        return false;
    }
    JSTaggedValue handlerInfoValue = transWithProtoHandler->GetHandlerInfo(compilerThread);
    if (!handlerInfoValue.IsInt()) {
        return false;
    }
    JSTaggedValue transitionHClassValue = transWithProtoHandler->GetTransitionHClass(compilerThread);
    result->handlerInfo = handlerInfoValue.GetLargeUInt();
    result->transitionHClass = broker->MakeHClassRef(transitionHClassValue);
    result->protoCell = broker->MakeProtoCellRef(protoCellValue);
    result->hasTransitionHClass = true;
    result->hasProtoCell = true;
    return result->transitionHClass.IsJSHClass() && result->protoCell.IsSafeForCompile();
}

bool TryReadPrototypeLoadHandler(const ArkSteedHeapBroker *broker, JSThread *compilerThread,
                                 JSTaggedValue cachedHandler, ParsedLoadHandler *result)
{
    if (cachedHandler.IsWeak() || !cachedHandler.IsPrototypeHandler()) {
        return false;
    }
    auto *prototypeHandler = PrototypeHandler::Cast(cachedHandler.GetTaggedObject());
    JSTaggedValue protoCellValue = prototypeHandler->GetProtoCell(compilerThread);
    if (!protoCellValue.IsProtoChangeMarker()) {
        return false;
    }
    JSTaggedValue handlerInfoValue = prototypeHandler->GetHandlerInfo(compilerThread);
    if (!handlerInfoValue.IsInt()) {
        return false;
    }
    result->handlerInfo = handlerInfoValue.GetLargeUInt();
    result->holderIsReceiver = false;
    JSTaggedValue holderValue = prototypeHandler->GetHolder(compilerThread);
    if (holderValue.IsHeapObject()) {
        result->holder = broker->MakeObjectRef(holderValue);
        result->holderHClass = broker->MakeHClassRef(JSTaggedValue(holderValue.GetTaggedObject()->GetClass()));
    }
    result->protoCell = broker->MakeProtoCellRef(protoCellValue);
    result->hasProtoCell = true;
    bool hasValidHolder = holderValue.IsUndefined() ? HandlerBase::IsNonExist(result->handlerInfo)
                                                    : result->holderHClass.IsSafeForCompile();
    return hasValidHolder && result->protoCell.IsSafeForCompile();
}

bool IsNonMovableHClass(JSHClass *hclass)
{
    if (g_isEnableCMCGC || hclass == nullptr) {
        return false;
    }
    auto *region = Region::ObjectAddressToRange(hclass);
    return region->InNonMovableSpace() || region->InReadOnlySpace() || region->InHugeObjectSpace() ||
           region->InSharedNonMovableSpace() || region->InSharedReadOnlySpace() || region->InSharedHugeObjectSpace();
}
} // namespace

bool ArkSteedHeapBroker::TryReadStoreHandler(const ArkSteedHandlerRef &handler, StoreHandlerData *result) const
{
    if (result == nullptr) {
        return false;
    }
    *result = {};
    if (handler.IsInt()) {
        result->handlerInfo = handler.Value().GetLargeUInt();
        return true;
    }
    if (!OwnsRef(handler) || !handler.IsHeapObject()) {
        return false;
    }
    NoGCScope scope(this);
    JSTaggedValue cachedHandler;
    if (!TryResolveRef(handler, &cachedHandler)) {
        return false;
    }
    if (TryReadTransitionStoreHandler(this, compilerThread_, cachedHandler, result)) {
        return true;
    }
    if (TryReadTransWithProtoStoreHandler(this, compilerThread_, cachedHandler, result)) {
        return true;
    }
    return TryReadPrototypeStoreHandler(this, compilerThread_, cachedHandler, result);
}

bool ArkSteedHeapBroker::TryReadLoadHandler(const ArkSteedHandlerRef &handler, LoadHandlerData *result) const
{
    if (result == nullptr) {
        return false;
    }
    *result = {};
    if (handler.IsInt()) {
        result->handlerInfo = handler.Value().GetLargeUInt();
        return true;
    }
    if (!OwnsRef(handler) || !handler.IsHeapObject()) {
        return false;
    }
    NoGCScope scope(this);
    JSTaggedValue cachedHandler;
    if (!TryResolveRef(handler, &cachedHandler)) {
        return false;
    }
    return TryReadPrototypeLoadHandler(this, compilerThread_, cachedHandler, result);
}

bool ArkSteedHeapBroker::IsTypedArrayLengthAccessor(const ArkSteedHClassRef &receiver, const LoadHandlerData &parsed,
                                                    const ArkSteedNameRef &name) const
{
    if (parsed.holderIsReceiver || !parsed.hasProtoCell || !HandlerBase::IsAccessor(parsed.handlerInfo)) {
        return false;
    }
    NoGCScope scope(this);
    JSThread *thread = compilerThread_;
    JSTaggedValue receiverValue;
    JSTaggedValue holderValue;
    JSTaggedValue nameValue;
    if (!TryResolveRef(receiver, &receiverValue) || !receiverValue.IsJSHClass() ||
        !IsFixedLengthTypedArrayType(JSHClass::Cast(receiverValue.GetTaggedObject())->GetObjectType()) ||
        !TryResolveRef(parsed.holder, &holderValue) || !holderValue.IsJSObject() || !TryResolveRef(name, &nameValue) ||
        !nameValue.IsString() ||
        !EcmaStringAccessor::StringsAreEqual(
            thread, EcmaString::Cast(nameValue.GetTaggedObject()),
            EcmaString::Cast(thread->GlobalConstants()->GetLengthString().GetTaggedObject()))) {
        return false;
    }
    // IC snapshots can predate a shadowing property added to an intermediate prototype.
    JSTaggedValue current = JSHClass::Cast(receiverValue.GetTaggedObject())->GetPrototype(thread);
    while (current != holderValue) {
        if (!current.IsJSObject() || current.IsJSProxy()) {
            return false;
        }
        auto *currentHClass = current.GetTaggedObject()->GetClass();
        if (currentHClass->IsDictionaryMode() || !currentHClass->GetLayout(thread).IsTaggedArray() ||
            JSHClass::FindPropertyEntry(thread, currentHClass, nameValue) >= 0) {
            return false;
        }
        current = currentHClass->GetPrototype(thread);
    }
    auto *holder = JSObject::Cast(holderValue.GetTaggedObject());
    auto *hclass = holder->GetClass();
    if (hclass->IsDictionaryMode() || !hclass->GetLayout(thread).IsTaggedArray()) {
        return false;
    }
    int entry = JSHClass::FindPropertyEntry(thread, hclass, nameValue);
    if (entry < 0) {
        return false;
    }
    auto attr = LayoutInfo::Cast(hclass->GetLayout(thread).GetTaggedObject())->GetAttr(thread, entry);
    if (!attr.IsAccessor()) {
        return false;
    }
    JSTaggedValue accessor = holder->GetProperty(thread, hclass, attr);
    if (!accessor.IsAccessorData() || accessor.IsInternalAccessor()) {
        return false;
    }
    JSTaggedValue getter = AccessorData::Cast(accessor.GetTaggedObject())->GetGetter(thread);
    if (!getter.IsJSFunction()) {
        return false;
    }
    auto *function = JSFunction::Cast(getter.GetTaggedObject());
    auto *method = Method::Cast(function->GetMethod(thread).GetTaggedObject());
    return method->IsNativeWithCallField() &&
           function->GetNativePointer() == reinterpret_cast<const void *>(builtins::BuiltinsTypedArray::GetLength);
}

bool ArkSteedHeapBroker::IsJSArrayLengthAccessor(const ArkSteedHClassRef &receiver, const ArkSteedNameRef &name) const
{
    if (!receiver.IsJSHClass() || !name.IsString()) {
        return false;
    }
    NoGCScope scope(this);
    JSTaggedValue hclass;
    JSTaggedValue key;
    return TryResolveRef(receiver, &hclass) && TryResolveRef(name, &key) &&
           JSHClass::Cast(hclass.GetTaggedObject())->IsJSArray() &&
           EcmaStringAccessor::StringsAreEqual(
               compilerThread_, EcmaString::Cast(key.GetTaggedObject()),
               EcmaString::Cast(compilerThread_->GlobalConstants()->GetLengthString().GetTaggedObject()));
}

bool ArkSteedHeapBroker::CanTransitionElementsKind(JSHClass *source, JSHClass *target) const
{
    if (!IsNonMovableHClass(source) || !IsNonMovableHClass(target)) {
        return false;
    }
    NoGCScope scope(this);
    JSThread *hostThread = env_->GetHostThread();
    if (hostThread == nullptr || source == nullptr || target == nullptr || source == target || !source->IsJSArray() ||
        !target->IsJSArray() || source->IsPrototype() || target->IsPrototype()) {
        return false;
    }

    ElementsKind sourceKind = source->GetElementsKind();
    ElementsKind targetKind = target->GetElementsKind();
    if (!JSHClass::IsInitialArrayHClassWithElementsKind(hostThread, source, sourceKind) ||
        !JSHClass::IsInitialArrayHClassWithElementsKind(hostThread, target, targetKind)) {
        return false;
    }
    return Elements::MergeElementsKind(sourceKind, targetKind) == targetKind;
}

bool ArkSteedHeapBroker::TryGetKnownConstructInfo(const ArkSteedFunctionRef &ref, KnownConstructInfo *info) const
{
    ASSERT(info != nullptr);
    *info = {};
    return TryReadRef(ref, [&](JSTaggedValue target) {
        if (!target.IsJSFunction() || !target.IsConstructor() || target.IsJSShared()) {
            return false;
        }
        auto *function = JSFunction::Cast(target.GetTaggedObject());
        JSTaggedValue methodValue = function->GetMethod(compilerThread_);
        if ((!function->IsBase(compilerThread_) && !function->IsDerivedConstructor(compilerThread_)) ||
            function->IsSharedFunction() || !methodValue.IsMethod()) {
            return false;
        }
        auto *method = Method::Cast(methodValue.GetTaggedObject());
        if (method->IsNativeWithCallField()) {
            return false;
        }
        info->formalArgc = method->GetNumArgsWithCallField();
        if (!function->IsBase(compilerThread_)) {
            return true;
        }
        auto layout = function->GetProtoOrHClass(compilerThread_);
        if (!layout.IsJSHClass()) {
            return false;
        }
        auto *hclass = JSHClass::Cast(layout.GetTaggedObject());
        if (hclass->GetObjectType() != JSType::JS_OBJECT || hclass->IsJSShared()) {
            return false;
        }
        if (hclass->IsObjSizeTrackingInProgress()) {
            return true;
        }
        info->objectSize = hclass->GetObjectSize();
        info->initialHClass = MakeHClassRef(layout);
        bool hasDirectSize = info->objectSize >= JSObject::SIZE && info->objectSize <= MAX_REGULAR_HEAP_OBJECT_SIZE &&
                             info->objectSize % JSTaggedValue::TaggedTypeSize() == 0;
        info->canAllocateDirectly = !G_USE_CMS_GC && !g_isEnableCMCGC && hasDirectSize && !hclass->IsAOT() &&
                                    !hclass->IsDictionaryMode() && !hclass->IsDictionaryElement() &&
                                    hclass->GetPrototype(compilerThread_).IsECMAObject();
        return info->initialHClass.IsSafeForCompile();
    });
}

uint32_t ArkSteedHeapBroker::GetArgumentsObjectSize() const
{
    NoGCScope scope(this);
    JSHandle<JSHClass> argumentsClass(env_->GetGlobalEnv()->GetArgumentsClassWithBarrier());
    return argumentsClass->GetObjectSize();
}

bool ArkSteedHeapBroker::CanLoadExternalModuleVar(int32_t index, bool installDependency) const
{
    NoGCScope scope(this);
    auto *hostThread = env_->GetHostThread();
    auto *function = *env_->GetJsFunction();
    if (function == nullptr || !ModuleManager::CheckModuleValueOutterResolved(hostThread, index, function)) {
        return false;
    }
    if (!installDependency) {
        return true;
    }
    auto *dependencies = env_->GetDependencies();
    return dependencies != nullptr && dependencies->DependOnNotHotReloadPatchMain(hostThread);
}

bool ArkSteedHeapBroker::CanCallFunction(const ArkSteedFunctionRef &ref, uint32_t actualArgc) const
{
    NoGCScope scope(this);
    JSTaggedValue target;
    if (!ref.IsJSFunction() || !TryResolveRef(ref, &target) || target.IsJSShared() || target.IsClassConstructor()) {
        return false;
    }
    auto *function = JSFunction::Cast(target.GetTaggedObject());
    JSTaggedValue methodValue = function->GetMethod(compilerThread_);
    if (function->IsSharedFunction() || !methodValue.IsMethod()) {
        return false;
    }
    auto *method = Method::Cast(methodValue.GetTaggedObject());
    return !method->IsNativeWithCallField() && actualArgc == method->GetNumArgsWithCallField();
}

ArkSteedObjectRef ArkSteedHeapBroker::GetBuiltinMethod(uint16_t builtinId, uint32_t globalEnvIndex) const
{
    NoGCScope scope(this);
    auto target = env_->GetGlobalEnv()->GetGlobalEnvObjectWithBarrierByIndex(globalEnvIndex);
    if (!target->IsJSFunction()) {
        return {};
    }
    auto methodValue = JSFunction::Cast(target->GetTaggedObject())->GetMethod(compilerThread_);
    if (!methodValue.IsMethod()) {
        return {};
    }
    auto *method = Method::Cast(methodValue.GetTaggedObject());
    if (!method->IsNativeWithCallField() || method->GetBuiltinId() != builtinId) {
        return {};
    }
    return MakeObjectRef(methodValue);
}

ArkSteedHClassRef ArkSteedHeapBroker::GetEmptyObjectHClass(uint32_t *objectSize) const
{
    ASSERT(objectSize != nullptr);
    NoGCScope scope(this);
    auto value = env_->GetGlobalEnv()->GetObjectFunctionClassWithBarrier().GetTaggedValue();
    if (!value.IsJSHClass()) {
        return {};
    }
    *objectSize = JSHClass::Cast(value.GetTaggedObject())->GetObjectSize();
    return MakeHClassRef(value);
}

bool ArkSteedHeapBroker::TryGetObjectType(const ArkSteedHeapRef &ref, JSType *type, bool *callable) const
{
    if (type == nullptr || !ref.IsHeapObject()) {
        return false;
    }
    return TryReadRef(ref, [&](JSTaggedValue value) {
        auto *hclass = value.GetTaggedObject()->GetClass();
        *type = hclass->GetObjectType();
        if (callable != nullptr) {
            *callable = hclass->IsCallable();
        }
        return true;
    });
}

bool ArkSteedHeapBroker::TryGetStringLength(const ArkSteedHeapRef &ref, uint32_t *length) const
{
    return length != nullptr && ref.IsString() && TryReadRef(ref, [&](JSTaggedValue value) {
               *length = EcmaStringAccessor(value).GetLength();
               return true;
           });
}

bool ArkSteedHeapBroker::TryGetStringChar(const ArkSteedHeapRef &ref, uint32_t index, uint16_t *character) const
{
    return character != nullptr && ref.IsString() && TryReadRef(ref, [&](JSTaggedValue value) {
               EcmaStringAccessor accessor(value);
               if (index >= accessor.GetLength()) {
                   return false;
               }
               *character = accessor.Get(compilerThread_, index);
               return true;
           });
}

JSHClass *ArkSteedHeapBroker::TryGetHClass(const ArkSteedHClassRef &ref) const
{
    if (!ref.IsJSHClass() || g_isEnableCMCGC) {
        return nullptr;
    }
    NoGCScope scope(this);
    JSTaggedValue value;
    if (!TryResolveRef(ref, &value)) {
        return nullptr;
    }
    auto *hclass = JSHClass::Cast(value.GetTaggedObject());
    return IsNonMovableHClass(hclass) ? hclass : nullptr;
}

JSHClass *ArkSteedHeapBroker::TryGetObjectHClass(const ArkSteedHeapRef &ref) const
{
    if (!ref.IsHeapObject() || g_isEnableCMCGC) {
        return nullptr;
    }
    NoGCScope scope(this);
    JSTaggedValue value;
    if (!TryResolveRef(ref, &value)) {
        return nullptr;
    }
    auto *hclass = value.GetTaggedObject()->GetClass();
    // The object's class can subsequently change. Root the observed HClass too.
    return IsNonMovableHClass(hclass) && MakeHClassRef(JSTaggedValue(hclass)).IsSafeForCompile() ? hclass : nullptr;
}

ArkSteedHClassRef ArkSteedHeapBroker::MakeHClassRef(JSHClass *hclass) const
{
    if (!IsNonMovableHClass(hclass)) {
        return {};
    }
    NoGCScope scope(this);
    return MakeHClassRef(JSTaggedValue(hclass));
}

std::optional<PropertyLookupResult> ArkSteedHeapBroker::TryLookupProperty(JSHClass *hclass,
                                                                          const ArkSteedNameRef &name) const
{
    if (!IsNonMovableHClass(hclass) || !name.IsName()) {
        return std::nullopt;
    }
    NoGCScope scope(this);
    JSTaggedValue key;
    if (!TryResolveRef(name, &key) || !hclass->GetLayout(compilerThread_).IsTaggedArray()) {
        return std::nullopt;
    }
    return JSHClass::LookupPropertyInPGOHClass(compilerThread_, hclass, key);
}

bool ArkSteedHeapBroker::TryGetPrototypeHClasses(const ArkSteedHClassRef &receiver, const ArkSteedHClassRef &holder,
                                                 std::vector<ArkSteedHClassRef> *chain) const
{
    if (chain == nullptr || !receiver.IsJSHClass() || !holder.IsJSHClass()) {
        return false;
    }
    NoGCScope scope(this);
    JSTaggedValue receiverValue;
    JSTaggedValue holderValue;
    if (!TryResolveRef(receiver, &receiverValue) || !TryResolveRef(holder, &holderValue)) {
        return false;
    }
    auto *receiverHClass = JSHClass::Cast(receiverValue.GetTaggedObject());
    if (!receiverHClass->GetLayout(compilerThread_).IsTaggedArray()) {
        return false;
    }
    std::vector<ArkSteedHClassRef> result;
    auto current = receiverHClass->GetPrototype(compilerThread_);
    while (current.IsHeapObject()) {
        auto *hclass = current.GetTaggedObject()->GetClass();
        auto ref = MakeHClassRef(JSTaggedValue(hclass));
        if (!ref.IsSafeForCompile()) {
            return false;
        }
        result.push_back(ref);
        if (ref.Equals(holder)) {
            *chain = std::move(result);
            return true;
        }
        current = hclass->GetPrototype(compilerThread_);
    }
    return false;
}

JSHClass *ArkSteedHeapBroker::TryGetGenericInitialArrayHClass(JSHClass *receiver, ArkSteedHClassRef *ref) const
{
    if (ref == nullptr || !IsNonMovableHClass(receiver)) {
        return nullptr;
    }
    NoGCScope scope(this);
    if (!receiver->IsJSArray() || receiver->GetElementsKind() == ElementsKind::GENERIC) {
        return nullptr;
    }
    auto globalEnv = env_->GetGlobalEnv();
    auto *hostThread = env_->GetHostThread();
    bool isPrototype = receiver->IsPrototype();
    auto *initial = hostThread->GetArrayInstanceHClass(globalEnv, receiver->GetElementsKind(), isPrototype,
                                                       JSThread::ThreadKind::JitThread);
    if (initial != receiver) {
        return nullptr;
    }
    auto *generic = hostThread->GetArrayInstanceHClass(globalEnv, ElementsKind::GENERIC, isPrototype,
                                                       JSThread::ThreadKind::JitThread);
    *ref = MakeHClassRef(generic);
    return ref->IsSafeForCompile() ? generic : nullptr;
}

bool ArkSteedHeapBroker::TryInstallStableHClassDependency(JSHClass *hclass, bool *installed) const
{
    if (installed == nullptr || !IsNonMovableHClass(hclass)) {
        return false;
    }
    NoGCScope scope(this);
    *installed = false;
    if (!kungfu::StableHClassDependency::IsValid(hclass)) {
        return true;
    }
    auto *dependencies = env_->GetDependencies();
    *installed = dependencies != nullptr && dependencies->DependOnStableHClass(hclass);
    return *installed;
}

bool ArkSteedHeapBroker::TryInstallNotPrototypeDependency(JSHClass *hclass) const
{
    if (!IsNonMovableHClass(hclass)) {
        return false;
    }
    NoGCScope scope(this);
    auto *dependencies = env_->GetDependencies();
    return dependencies != nullptr && dependencies->DependOnNotPrototype(hclass);
}

bool ArkSteedHeapBroker::CheckStableHClass(const ArkSteedHClassRef &ref) const
{
    if (!ref.IsJSHClass()) {
        return false;
    }
    return TryReadRef(ref, [](JSTaggedValue value) {
        return kungfu::LazyDeoptAllDependencies::CheckStableHClass(JSHClass::Cast(value.GetTaggedObject()));
    });
}

bool ArkSteedHeapBroker::DependOnStableHClass(const ArkSteedHClassRef &ref) const
{
    if (!ref.IsJSHClass()) {
        return false;
    }
    return TryReadRef(ref, [&](JSTaggedValue value) {
        if (value.IsInSharedHeap()) {
            return true;
        }
        if (!env_->GetJSOptions().IsEnableJitLazyDeopt()) {
            return false;
        }
        auto *dependencies = env_->GetDependencies();
        return dependencies != nullptr && dependencies->DependOnStableHClass(JSHClass::Cast(value.GetTaggedObject()));
    });
}

bool ArkSteedHeapBroker::DependOnStableProtoChain(const ArkSteedHClassRef &receiver,
                                                  const ArkSteedHClassRef *holder) const
{
    if (!receiver.IsJSHClass() || env_ == nullptr || !env_->GetJSOptions().IsEnableJitLazyDeopt()) {
        return false;
    }
    NoGCScope scope(this);
    JSTaggedValue receiverValue;
    if (!TryResolveRef(receiver, &receiverValue)) {
        return false;
    }
    JSHClass *holderHClass = nullptr;
    if (holder != nullptr) {
        JSTaggedValue holderValue;
        if (!holder->IsJSHClass() || !TryResolveRef(*holder, &holderValue)) {
            return false;
        }
        holderHClass = JSHClass::Cast(holderValue.GetTaggedObject());
    }
    if (receiverValue.IsInSharedHeap()) {
        return true;
    }
    auto *dependencies = env_->GetDependencies();
    return dependencies != nullptr &&
           dependencies->DependOnStableProtoChain(compilerThread_, JSHClass::Cast(receiverValue.GetTaggedObject()),
                                                  holderHClass, env_->GetGlobalEnv().GetObject<GlobalEnv>());
}

bool ArkSteedHeapBroker::DependOnNotPrototype(const ArkSteedHClassRef &ref) const
{
    if (!ref.IsJSHClass() || env_ == nullptr || !env_->GetJSOptions().IsEnableJitLazyDeopt()) {
        return false;
    }
    return TryReadRef(ref, [&](JSTaggedValue value) {
        auto *hclass = JSHClass::Cast(value.GetTaggedObject());
        if (value.IsInSharedHeap()) {
            return !hclass->IsPrototype();
        }
        auto *dependencies = env_->GetDependencies();
        return dependencies != nullptr && dependencies->DependOnNotPrototype(hclass);
    });
}

bool ArkSteedHeapBroker::DependOnArrayDetector() const
{
    if (env_ == nullptr || !env_->GetJSOptions().IsEnableJitLazyDeopt()) {
        return false;
    }
    NoGCScope scope(this);
    auto *dependencies = env_->GetDependencies();
    return dependencies != nullptr && dependencies->DependOnArrayDetector(env_->GetGlobalEnv().GetObject<GlobalEnv>());
}

bool ArkSteedHeapBroker::TryGetEmptyArrayLayout(ArkSteedObjectRef* globalEnv, uint32_t* objectSize) const
{
    NoGCScope scope(this);
    auto env = env_->GetGlobalEnv();
    JSHandle<JSFunction> constructor(env->GetArrayFunctionWithBarrier());
    auto value = constructor->GetProtoOrHClass(compilerThread_);
    // Built-in array classes have fixed slots. Prototype changes replace the class, not its layout.
    *objectSize = JSHClass::Cast(value.GetTaggedObject())->GetObjectSize();
    *globalEnv = MakeObjectRef(env.GetTaggedValue());
    return true;
}

bool ArkSteedHeapBroker::TryGetEmptyObjectLayout(ArkSteedHClassRef* hclass, uint32_t* objectSize) const
{
    NoGCScope scope(this);
    JSHandle<JSHClass> initial(env_->GetGlobalEnv()->GetObjectFunctionClassWithBarrier());
    *objectSize = initial->GetObjectSize();
    *hclass = MakeHClassRef(initial.GetTaggedValue());
    return true;
}

bool ArkSteedHeapBroker::TryGetArrayLiteralFromConstantPool(uint16_t cpIdx,
                                                            ArkSteedObjectRef* literal,
                                                            uint32_t* objectSize) const
{
    ASSERT(literal != nullptr);
    NoGCScope scope(this);
    if (env_->GetMethodLiteral() == nullptr) {
        return false;
    }
    uint32_t methodOffset = env_->GetMethodLiteral()->GetMethodId().GetOffset();
    JSTaggedValue constpool = env_->FindOrCreateUnsharedConstpool(methodOffset);
    if (!constpool.IsConstantPool()) {
        return false;
    }
    auto* pool = ConstantPool::Cast(constpool.GetTaggedObject());
    // AOT literals may select a different HClass through the constant-index mapping.
    if (pool->GetJSPandaFile()->IsLoadedAOT() || cpIdx >= pool->GetNumOfCacheElement()) {
        return false;
    }
    // JIT must not lazily materialize a literal on the compiler thread.
    JSTaggedValue value = env_->GetArrayLiteralFromCache(constpool, cpIdx, CString());
    if (!value.IsJSArray()) {
        return false;
    }
    auto* array = JSArray::Cast(value.GetTaggedObject());
    auto elements = array->GetElements(compilerThread_);
    auto empty = compilerThread_->GlobalConstants()->GetEmptyArray();
    // Materialization uses the built-in array layout, empty properties and local elements.
    // CloneArrayLiteral may install COW once, but never mutates an already-COW template store.
    if (elements != empty && !elements.IsCOWArray()) {
        return false;
    }
    *objectSize = array->GetJSHClass()->GetObjectSize();
    *literal = MakeObjectRef(value);
    return true;
}

bool ArkSteedHeapBroker::TryGetObjectLiteralFromConstantPool(uint16_t cpIdx,
                                                             Chunk* chunk,
                                                             ArkSteedObjectRef* literal,
                                                             CreateObjectWithBufferInfo* info) const
{
    ASSERT(chunk != nullptr && literal != nullptr && info != nullptr);
    NoGCScope scope(this);
    ASSERT(env_->GetMethodLiteral() != nullptr);
    uint32_t methodOffset = env_->GetMethodLiteral()->GetMethodId().GetOffset();
    auto poolValue = env_->NewJSHandle(env_->FindOrCreateUnsharedConstpool(methodOffset));
    if (!poolValue.GetTaggedValue().IsConstantPool()) {
        return false;
    }
    auto pool = JSHandle<ConstantPool>::Cast(poolValue);
    if (pool->GetJSPandaFile()->IsLoadedAOT() || cpIdx >= pool->GetNumOfCacheElement()) {
        return false;
    }
    // This JIT accessor only reads the cache; it never materializes a cold literal.
    auto value = env_->NewJSHandle(env_->GetObjectLiteralFromCache(pool.GetTaggedValue(), cpIdx, CString()));
    if (!value.GetTaggedValue().IsJSObject()) {
        return false;
    }
    auto object = JSHandle<JSObject>::Cast(value);
    auto hclass = JSHandle<JSHClass>::Cast(env_->NewJSHandle(JSTaggedValue(object->GetClass())));
    // The global literal class cache can contain AOT classes even for a non-AOT file.
    // Their spare slots need Hole initialization; non-empty backing stores need cloning.
    if (hclass->IsAOT() || !object->ElementsAndPropertiesIsEmpty(compilerThread_)
        || hclass->GetObjectSize() > g_maxRegularHeapObjectSize) {
        return false;
    }
    CreateObjectWithBufferInfo snapshot;
    snapshot.objectSize = hclass->GetObjectSize();
    snapshot.fieldCount = hclass->NumberOfProps();
    // Cached ordinary literals are private templates. Clone writes target the new instance;
    // representation transitions copy the layout and detach the new transition ancestry.
    auto layout = JSHandle<LayoutInfo>::Cast(env_->NewJSHandle(hclass->GetLayout(compilerThread_)));
    auto* fieldValues = snapshot.fieldCount == 0 ? nullptr : chunk->NewArray<uint64_t>(snapshot.fieldCount);
    for (uint32_t i = 0; i < snapshot.fieldCount; ++i) {
        PropertyAttributes attr = layout->GetAttr(compilerThread_, i);
        JSTaggedValue field = object->GetPropertyInlinedProps(compilerThread_, i);
        // Raw INT/DOUBLE slots already contain converted bits, not tagged values. Functions, accessors and
        // other heap-valued tagged slots need the general clone/barrier path and cannot be embedded here.
        if (attr.IsTaggedRep() && field.IsHeapObject()) {
            return false;
        }
        fieldValues[i] = field.GetRawData();
    }
    snapshot.fieldValues = Span<const uint64_t>(fieldValues, snapshot.fieldCount);
    *literal = MakeObjectRef(object.GetTaggedValue());
    *info = snapshot;
    return true;
}

bool ArkSteedHeapBroker::TryGetBuiltinIndexFromConstantPool(uint16_t cpIdx, size_t *index) const
{
    ASSERT(index != nullptr);
    NoGCScope scope(this);
    ArkSteedNameRef name;
    if (!TryGetNameFromConstantPool(cpIdx, &name)) {
        return false;
    }
    JSTaggedValue nameValue;
    if (!TryResolveRef(name, &nameValue)) {
        return false;
    }
    size_t builtinIndex = BuiltinIndex::GetInstance().GetBuiltinIndex(compilerThread_, nameValue);
    if (builtinIndex == BuiltinIndex::NOT_FOUND) {
        return false;
    }
    *index = builtinIndex;
    return true;
}

bool ArkSteedHeapBroker::GetFeedbackForNamedAccess(const ArkSteedFeedbackReader &reader, int slotIndex,
                                                   NamedAccessFeedback *feedback) const
{
    *feedback = {};
    uint32_t slotId = 0;
    if (!reader.TryGetFeedbackSlotId(slotIndex, false, &slotId)) {
        return false;
    }
    if (TryGetCachedNamedAccessFeedback({slotId, false}, feedback) ||
        TryGetCachedNamedAccessFeedback({slotId, true}, feedback)) {
        return true;
    }

    NoGCScope scope(this);
    bool success = reader.ReadNamedAccessFeedback(slotIndex, feedback);
    if (!success) {
        *feedback = {};
        return false;
    }
    CacheNamedAccessFeedback(*feedback);
    return true;
}

bool ArkSteedHeapBroker::GetFeedbackForOperation(const ArkSteedFeedbackReader &reader,
                                                 OperationFeedback *feedback) const
{
    *feedback = {};
    NoGCScope scope(this);
    return reader.ReadOperationFeedback(feedback);
}

bool ArkSteedHeapBroker::GetFeedbackForValueAccess(const ArkSteedFeedbackReader &reader,
                                                   ValueAccessFeedback *feedback) const
{
    *feedback = {};
    uint32_t slotId = 0;
    if (!reader.TryGetFeedbackSlotId(&slotId)) {
        return false;
    }
    if (TryGetCachedValueAccessFeedback(slotId, feedback)) {
        return true;
    }

    NoGCScope scope(this);
    if (!reader.ReadValueAccessFeedback(feedback)) {
        *feedback = {};
        return false;
    }
    CacheValueAccessFeedback(*feedback);
    return true;
}

bool ArkSteedHeapBroker::GetFeedbackForElementAccess(const ArkSteedFeedbackReader &reader, int slotIndex,
                                                     ElementAccessFeedback *feedback) const
{
    *feedback = {};
    NoGCScope scope(this);
    return reader.ReadElementAccessFeedback(slotIndex, feedback);
}

bool ArkSteedHeapBroker::GetFeedbackForGlobalAccess(const ArkSteedFeedbackReader &reader,
                                                    GlobalAccessFeedback *feedback) const
{
    *feedback = {};
    NoGCScope scope(this);
    return reader.ReadGlobalAccessFeedback(feedback);
}

bool ArkSteedHeapBroker::GetFeedbackForCall(const ArkSteedFeedbackReader &reader, CallFeedback *feedback) const
{
    *feedback = {};
    NoGCScope scope(this);
    return reader.ReadCallFeedback(feedback);
}

bool ArkSteedHeapBroker::GetFeedbackForConstructor(const ArkSteedFeedbackReader &reader,
                                                   ConstructorFeedback *feedback) const
{
    *feedback = {};
    NoGCScope scope(this);
    return reader.ReadConstructorFeedback(feedback);
}

}  // namespace panda::ecmascript::arksteed
