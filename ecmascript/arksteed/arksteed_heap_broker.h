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

#ifndef ECMASCRIPT_ARKSTEED_HEAP_BROKER_H
#define ECMASCRIPT_ARKSTEED_HEAP_BROKER_H

#include <array>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

#include "ecmascript/global_env_constants.h"
#include "ecmascript/arksteed/arksteed_heap_ref.h"
#include "ecmascript/arksteed/arksteed_processed_feedback.h"
#include "ecmascript/compiler/jit_compilation_env.h"
#include "ecmascript/ic/profile_type_info.h"
#include "ecmascript/jit/jit.h"
#include "ecmascript/js_array.h"
#include "ecmascript/js_thread.h"
#include "ecmascript/js_tagged_value_wrapper.h"

namespace panda::ecmascript::arksteed {

class ArkSteedFeedbackReader;
struct CreateObjectWithBufferInfo;

enum class ArkSteedBrokerMode : uint8_t {
    DISABLED,
    SERIALIZING,
    SERIALIZED,
    RETIRED,
};

class ArkSteedHeapBroker {
  private:
    friend class ArkSteedHeapBrokerTestAccess;

    class NoGCScope
    {
      public:
        explicit NoGCScope(const ArkSteedHeapBroker *broker) : broker_(broker)
        {
            ASSERT(broker_->compilerThread_ != nullptr && broker_->compilerThread_->IsJitThread());
            ASSERT(broker_->compilerThread_->IsInRunningState());
            if (!broker_->SerializingAllowed()) {
                broker_->StartSerializing();
                ownsSerializing_ = true;
            }
        }

        ~NoGCScope()
        {
            if (ownsSerializing_) {
                broker_->StopSerializing();
            }
        }

        NO_COPY_SEMANTIC(NoGCScope);
        NO_MOVE_SEMANTIC(NoGCScope);

      private:
        const ArkSteedHeapBroker *broker_ {nullptr};
        ALLOW_HANDLE_ALLOC;
        ALLOW_DEREF_HANDLE;
        DISALLOW_HEAP_ALLOC;
        DISALLOW_GARBAGE_COLLECTION;
        bool ownsSerializing_ {false};
    };

  public:
    struct KnownConstructInfo {
        uint32_t formalArgc{0};
        ArkSteedHClassRef initialHClass{};
        uint32_t objectSize{0};
        bool canAllocateDirectly{false};
    };

    struct StoreHandlerData {
        uint64_t handlerInfo{0};
        ArkSteedHClassRef transitionHClass{};
        ArkSteedHClassRef holderHClass{};
        ArkSteedObjectRef holder{};
        ArkSteedProtoCellRef protoCell{};
        bool holderIsReceiver{true};
        bool hasTransitionHClass{false};
        bool hasProtoCell{false};
    };

    struct LoadHandlerData {
        uint64_t handlerInfo{0};
        ArkSteedHClassRef holderHClass{};
        ArkSteedObjectRef holder{};
        ArkSteedProtoCellRef protoCell{};
        uint32_t holderDepth{0};
        bool holderIsReceiver{true};
        bool hasProtoCell{false};
    };

    ArkSteedHeapBroker(JSThread *compilerThread, JitCompilationEnv *env) : compilerThread_(compilerThread), env_(env) {}

    void StartSerializing() const
    {
        mode_ = ArkSteedBrokerMode::SERIALIZING;
    }

    void StopSerializing() const
    {
        mode_ = ArkSteedBrokerMode::SERIALIZED;
    }

    void Retire() const
    {
        mode_ = ArkSteedBrokerMode::RETIRED;
    }

    ArkSteedBrokerMode Mode() const
    {
        return mode_;
    }

    bool SerializingAllowed() const
    {
        return mode_ == ArkSteedBrokerMode::SERIALIZING;
    }

    bool TryGetProfileTypeInfo(ProfileTypeInfo **profileTypeArray) const
    {
        if (!SerializingAllowed()) {
            return false;
        }
        auto profileTypeInfo = env_->GetProfileTypeInfo();
        JSTaggedValue profileTypeInfoValue = GetRawHandleValue(profileTypeInfo);
        if (!profileTypeInfoValue.IsProfileTypeInfo()) {
            return false;
        }

        *profileTypeArray = ProfileTypeInfo::Cast(profileTypeInfoValue.GetTaggedObject());
        return true;
    }

    bool TryGetArrayCreationHClass(uint32_t slotId, ArkSteedHClassRef *hclassRef) const
    {
        if (hclassRef == nullptr) {
            return false;
        }
        NoGCScope scope(this);
        ProfileTypeInfo *profileTypeInfo = nullptr;
        if (!TryGetProfileTypeInfo(&profileTypeInfo) || slotId >= profileTypeInfo->GetICSlotLength()) {
            return false;
        }
        JSTaggedValue slotValue = profileTypeInfo->GetICSlot(compilerThread_, slotId);
        if (!slotValue.IsTrackInfoObject()) {
            return false;
        }
        TrackInfo *trackInfo = TrackInfo::Cast(slotValue.GetTaggedObject());
        ElementsKind kind = trackInfo->GetElementsKind();
        if (kind == ElementsKind::NONE || Elements::IsGeneric(kind)) {
            return false;
        }
        JSHClass *hclass = env_->GetHostThread()->GetArrayInstanceHClass(
            env_->GetGlobalEnv(), kind, false, JSThread::ThreadKind::JitThread);
        if (hclass == nullptr) {
            return false;
        }
        *hclassRef = MakeHClassRef(JSTaggedValue(hclass));
        return hclassRef->IsSafeForCompile();
    }

    // Raw values require an active NoGCScope. The handle overload opens a
    // no-safepoint read operation; callers must already be in JitHeapAccessScope.
    ArkSteedObjectRef MakeObjectRef(JSTaggedValue value) const
    {
        return MakeStableHeapRef(value, false);
    }

    template <typename T> ArkSteedObjectRef MakeObjectRef(const JSHandle<T> &handle) const
    {
        if (handle.GetAddress() == 0U) {
            return {};
        }
        NoGCScope scope(this);
        return MakeObjectRef(handle.GetTaggedValue());
    }

    ArkSteedHClassRef MakeHClassRef(JSTaggedValue value) const
    {
        ASSERT(SerializingAllowed());
        if (value.IsWeak() || !value.IsJSHClass()) {
            return {};
        }
        return MakeObjectRef(value);
    }

    ArkSteedHClassRef MakeWeakHClassRef(JSTaggedValue weakValue) const
    {
        ASSERT(SerializingAllowed());
        if (!weakValue.IsWeak()) {
            return {};
        }
        return MakeHClassRef(JSTaggedValue(weakValue.GetWeakReferent()));
    }

    ArkSteedProtoCellRef MakeProtoCellRef(JSTaggedValue value) const
    {
        ASSERT(SerializingAllowed());
        if (value.IsWeak() || !value.IsProtoChangeMarker()) {
            return {};
        }
        return MakeObjectRef(value);
    }

    ArkSteedHandlerRef MakeHandlerRef(JSTaggedValue value) const
    {
        return MakeStableHeapRef(value, true);
    }

    ArkSteedNameRef MakeNameRef(JSTaggedValue value) const
    {
        ASSERT(SerializingAllowed());
        if (value.IsWeak() || (!value.IsString() && !value.IsSymbol())) {
            return {};
        }
        return MakeObjectRef(value);
    }

    bool TryGetGlobalConstantRef(ConstantIndex index, ArkSteedNameRef *name) const
    {
        if (name == nullptr) {
            return false;
        }
        NoGCScope scope(this);
        JSThread *hostThread = env_->GetHostThread();
        JSTaggedValue value = hostThread->GlobalConstants()->GetGlobalConstantObject(static_cast<size_t>(index));
        if (!value.IsString()) {
            return false;
        }
        *name = MakeNameRef(value);
        return name->IsSafeForCompile();
    }

    ArkSteedFunctionRef MakeFunctionRef(JSTaggedValue value) const
    {
        ASSERT(SerializingAllowed());
        if (value.IsWeak() || !value.IsJSFunction()) {
            return ArkSteedFunctionRef();
        }
        return MakeStableHeapRef(value, false);
    }

    bool TryGetNameFromConstantPool(uint16_t cpIdx, ArkSteedNameRef *name) const
    {
        if (name == nullptr || env_->GetMethodLiteral() == nullptr) {
            return false;
        }
        NoGCScope scope(this);
        uint32_t methodOffset = env_->GetMethodLiteral()->GetMethodId().GetOffset();
        JSTaggedValue nameValue = env_->GetStringFromConstantPool(methodOffset, cpIdx, false);
        if (nameValue.IsUndefined()) {
            return false;
        }
        *name = MakeNameRef(nameValue);
        return name->IsSafeForCompile();
    }

    bool TryGetBuiltinIndexFromConstantPool(uint16_t cpIdx, size_t *index) const;

    bool TryGetMethodFromConstantPool(uint16_t cpIdx, ArkSteedObjectRef *method) const
    {
        ASSERT(method != nullptr);
        if (env_->GetMethodLiteral() == nullptr) {
            return false;
        }
        NoGCScope scope(this);
        uint32_t methodOffset = env_->GetMethodLiteral()->GetMethodId().GetOffset();
        JSTaggedValue constpool = env_->GetConstantPoolByMethodOffset(methodOffset);
        if (!constpool.IsConstantPool()) {
            return false;
        }
        JSTaggedValue methodValue = env_->GetMethodFromCache(constpool, cpIdx);
        if (!methodValue.IsMethod()) {
            return false;
        }
        *method = MakeObjectRef(methodValue);
        return method->IsSafeForCompile();
    }

    bool TryGetEmptyArrayLayout(ArkSteedObjectRef* globalEnv, uint32_t* objectSize) const;
    bool TryGetEmptyObjectLayout(ArkSteedHClassRef* hclass, uint32_t* objectSize) const;
    bool TryGetArrayLiteralFromConstantPool(uint16_t cpIdx, ArkSteedObjectRef* literal, uint32_t* objectSize) const;

    bool TryGetObjectLiteralFromConstantPool(uint16_t cpIdx,
                                             Chunk* chunk,
                                             ArkSteedObjectRef* literal,
                                             CreateObjectWithBufferInfo* info) const;

    // Ref creation has already registered the constant. No heap access is needed.
    bool TryGetHeapConstantIndex(const ArkSteedHeapRef &ref, uint32_t *handleIndex) const
    {
        if (handleIndex == nullptr || !OwnsRef(ref) || !ref.IsHeapObject()) {
            return false;
        }
        *handleIndex = ref.index_;
        return true;
    }

    // Low-level helper for feedback/access-info readers with an active scope.
    // General callers should use the concrete read/compare interfaces below.
    bool TryResolveRef(const ArkSteedHeapRef &ref, JSTaggedValue *value) const
    {
        if (!OwnsRef(ref) || value == nullptr) {
            return false;
        }
        ASSERT((SerializingAllowed() || ref.IsImmediate()) && "Resolving a heap object requires an active NoGCScope");
        *value = ref.IsImmediate() ? ref.value_ : env_->GetHeapConstantHandle(ref.index_).GetTaggedValue();
        return true;
    }

    // These reads require an outer JitHeapAccessScope. They never park, change
    // thread state, or lock. NoGCScope checks that raw temporaries cannot cross
    // a safepoint; callers receive copied data or rooted refs.
    bool CanCallFunction(const ArkSteedFunctionRef &ref, uint32_t actualArgc) const;
    bool TryGetKnownConstructInfo(const ArkSteedFunctionRef &ref, KnownConstructInfo *info) const;
    uint32_t GetArgumentsObjectSize() const;
    bool CanLoadExternalModuleVar(int32_t index, bool installDependency) const;
    ArkSteedObjectRef GetBuiltinMethod(uint16_t builtinId, uint32_t globalEnvIndex) const;
    ArkSteedHClassRef GetEmptyObjectHClass(uint32_t *objectSize) const;
    bool TryGetObjectType(const ArkSteedHeapRef &ref, JSType *type, bool *callable = nullptr) const;
    bool TryGetStringLength(const ArkSteedHeapRef &ref, uint32_t *length) const;
    bool TryGetStringChar(const ArkSteedHeapRef &ref, uint32_t index, uint16_t *character) const;

    // Non-CMC compiler facts already retain HClass pointers. Only non-moving,
    // rooted HClasses may escape a scope; layout/prototype reads stay inside it.
    JSHClass *TryGetHClass(const ArkSteedHClassRef &ref) const;
    JSHClass *TryGetObjectHClass(const ArkSteedHeapRef &ref) const;
    ArkSteedHClassRef MakeHClassRef(JSHClass *hclass) const;
    std::optional<PropertyLookupResult> TryLookupProperty(JSHClass *hclass, const ArkSteedNameRef &name) const;
    bool TryGetPrototypeHClasses(const ArkSteedHClassRef &receiver, const ArkSteedHClassRef &holder,
                                 std::vector<ArkSteedHClassRef> *chain) const;
    JSHClass *TryGetGenericInitialArrayHClass(JSHClass *receiver, ArkSteedHClassRef *ref) const;
    bool TryInstallStableHClassDependency(JSHClass *hclass, bool *installed) const;
    bool TryInstallNotPrototypeDependency(JSHClass *hclass) const;

    // Each operation forbids safepoints and returns only data or rooted refs.
    bool TryReadStoreHandler(const ArkSteedHandlerRef &handler, StoreHandlerData *result) const;
    bool TryReadLoadHandler(const ArkSteedHandlerRef &handler, LoadHandlerData *result) const;
    bool IsJSArrayLengthAccessor(const ArkSteedHClassRef &receiver, const ArkSteedNameRef &name) const;
    bool IsTypedArrayLengthAccessor(const ArkSteedHClassRef &receiver, const LoadHandlerData &parsed,
                                    const ArkSteedNameRef &name) const;
    bool CanTransitionElementsKind(JSHClass *source, JSHClass *target) const;

    bool CheckStableHClass(const ArkSteedHClassRef &ref) const;
    bool DependOnStableHClass(const ArkSteedHClassRef &ref) const;
    bool DependOnStableProtoChain(const ArkSteedHClassRef &receiver, const ArkSteedHClassRef *holder = nullptr) const;
    bool DependOnNotPrototype(const ArkSteedHClassRef &receiver) const;
    bool DependOnArrayDetector() const;

    // The callback must not retain raw heap values/pointers, or allocate on the host heap.
    // Only copied scalar facts or broker-created refs may outlive it. Returning bool
    // prevents accidentally using a raw pointer as the result of this interface.
    template <typename Reader> bool TryReadRef(const ArkSteedHeapRef &ref, Reader &&reader) const
    {
        static_assert(std::is_same_v<std::invoke_result_t<Reader, JSTaggedValue>, bool>);
        if (!OwnsRef(ref)) {
            return false;
        }
        if (ref.IsImmediate()) {
            return std::forward<Reader>(reader)(ref.value_);
        }
        NoGCScope scope(this);
        JSTaggedValue value;
        return TryResolveRef(ref, &value) && std::forward<Reader>(reader)(value);
    }

    // For a live host object versus feedback. A raw JSTaggedValue overload would
    // obscure the lifetime of the input, so accept a rooted handle only.
    template <typename T> bool TryIsSameObject(const ArkSteedHeapRef &ref, const JSHandle<T> &object, bool *same) const
    {
        if (same == nullptr || object.GetAddress() == 0U || !OwnsRef(ref) || !ref.IsHeapObject()) {
            return false;
        }
        return TryReadRef(ref, [&](JSTaggedValue value) {
            *same = value == object.GetTaggedValue();
            return true;
        });
    }

    bool IsSameFunction(const ArkSteedFunctionRef &ref) const
    {
        bool same = false;
        return ref.IsJSFunction() && TryIsSameObject(ref, env_->GetJsFunction(), &same) && same;
    }

    JSThread *GetCompilerThread() const
    {
        return compilerThread_;
    }

    bool GetFeedbackForNamedAccess(const ArkSteedFeedbackReader &reader, int slotIndex,
                                   NamedAccessFeedback *feedback) const;

    bool GetFeedbackForValueAccess(const ArkSteedFeedbackReader &reader, ValueAccessFeedback *feedback) const;
    bool GetFeedbackForElementAccess(const ArkSteedFeedbackReader &reader, int slotIndex,
                                     ElementAccessFeedback *feedback) const;

    bool GetFeedbackForGlobalAccess(const ArkSteedFeedbackReader &reader, GlobalAccessFeedback *feedback) const;

    bool GetFeedbackForOperation(const ArkSteedFeedbackReader &reader, OperationFeedback *feedback) const;

    bool GetFeedbackForCall(const ArkSteedFeedbackReader &reader, CallFeedback *feedback) const;

    bool GetFeedbackForConstructor(const ArkSteedFeedbackReader &reader, ConstructorFeedback *feedback) const;

    bool TryGetCachedNamedAccessFeedback(AccessFeedbackSource source, NamedAccessFeedback *feedback) const
    {
        for (const auto &entry : namedAccessFeedbackCache_) {
            if (entry.valid && IsSameFeedbackSource(entry.source, source)) {
                *feedback = entry.feedback;
                return true;
            }
        }
        return false;
    }

    void CacheNamedAccessFeedback(const NamedAccessFeedback &feedback) const
    {
        uint32_t index = feedback.base.source.slotId % FEEDBACK_CACHE_SIZE;
        namedAccessFeedbackCache_[index] = {true, feedback.base.source, feedback};
    }

    bool TryGetCachedValueAccessFeedback(uint32_t slotId, ValueAccessFeedback *feedback) const
    {
        const auto &entry = valueAccessFeedbackCache_[slotId % FEEDBACK_CACHE_SIZE];
        if (!entry.valid || entry.source.slotId != slotId) {
            return false;
        }
        *feedback = entry.feedback;
        return true;
    }

    void CacheValueAccessFeedback(const ValueAccessFeedback &feedback) const
    {
        uint32_t index = feedback.base.source.slotId % FEEDBACK_CACHE_SIZE;
        valueAccessFeedbackCache_[index] = {true, feedback.base.source, feedback};
    }

private:
    static constexpr uint32_t FEEDBACK_CACHE_SIZE = 16;

    struct NamedAccessFeedbackCacheEntry {
        bool valid {false};
        AccessFeedbackSource source {};
        NamedAccessFeedback feedback {};
    };

    struct ValueAccessFeedbackCacheEntry {
        bool valid {false};
        AccessFeedbackSource source {};
        ValueAccessFeedback feedback {};
    };

    static bool IsSameFeedbackSource(AccessFeedbackSource left, AccessFeedbackSource right)
    {
        return left.slotId == right.slotId && left.isPoly == right.isPoly && left.slotKind == right.slotKind;
    }

    template <typename T>
    static JSTaggedValue GetRawHandleValue(const JSHandle<T> &handle)
    {
        uintptr_t address = handle.GetAddress();
        if (address == 0U) {
            return JSTaggedValue::Undefined();
        }
        return JSTaggedValue(*reinterpret_cast<JSTaggedType *>(address));
    }

    ArkSteedHeapRef MakeStableHeapRef(JSTaggedValue value, bool allowPrimitive) const
    {
        ASSERT(SerializingAllowed());
        if (!value.IsHeapObject()) {
            return allowPrimitive ? ArkSteedHeapRef::FromImmediate(value) : ArkSteedHeapRef();
        }
        if (value.IsWeak()) {
            return {};
        }
        using Kind = ArkSteedHeapRef::Kind;
        Kind kind = Kind::HEAP_OBJECT;
        if (value.IsJSHClass()) {
            kind = Kind::HCLASS;
        } else if (value.IsJSFunction()) {
            kind = Kind::FUNCTION;
        } else if (value.IsString()) {
            kind = Kind::STRING;
        } else if (value.IsSymbol()) {
            kind = Kind::SYMBOL;
        } else if (value.IsProtoChangeMarker()) {
            kind = Kind::PROTO_CELL;
        }
        return ArkSteedHeapRef(env_, env_->RecordHeapConstant(value), kind);
    }

    bool OwnsRef(const ArkSteedHeapRef &ref) const
    {
        return ref.IsSafeForCompile() && (ref.IsImmediate() || ref.owner_ == env_);
    }

    JSThread *compilerThread_ {nullptr};
    JitCompilationEnv *env_ {nullptr};
    mutable ArkSteedBrokerMode mode_ {ArkSteedBrokerMode::SERIALIZED};
    mutable std::array<NamedAccessFeedbackCacheEntry, FEEDBACK_CACHE_SIZE> namedAccessFeedbackCache_ {};
    mutable std::array<ValueAccessFeedbackCacheEntry, FEEDBACK_CACHE_SIZE> valueAccessFeedbackCache_ {};
};

}  // namespace panda::ecmascript::arksteed

#endif  // ECMASCRIPT_ARKSTEED_HEAP_BROKER_H
