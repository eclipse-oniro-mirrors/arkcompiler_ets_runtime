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

#ifndef ECMASCRIPT_ARKSTEED_ASSEMBLER_INL_H
#define ECMASCRIPT_ARKSTEED_ASSEMBLER_INL_H

#include <limits>

#include "ecmascript/arksteed/arksteed_assembler.h"
#include "ecmascript/js_array.h"
#include "ecmascript/js_hclass.h"
#include "ecmascript/js_tagged_value.h"
#include "ecmascript/js_thread.h"
#include "ecmascript/mem/mem.h"

// Include platform-specific implementations
#if defined(PANDA_TARGET_AMD64)
#include "ecmascript/arksteed/arch/x64/arksteed_assembler_x64-inl.h"
#elif defined(PANDA_TARGET_ARM64)
#include "ecmascript/arksteed/arch/arm64/arksteed_assembler_arm64-inl.h"
#else
#error "ArkSteed does not support this architecture."
#endif

namespace panda::ecmascript::arksteed {

// =============================================================================
// Common Inline Implementations (platform-agnostic)
// =============================================================================

// Add any common inline implementations here that work across all platforms.
// Platform-specific inline implementations are in arch/xxx/arksteed_assembler_xxx_inl.h

inline void ArkSteedAssembler::Branch(Condition condition, Label *ifTrue, bool fallthroughWhenTrue, Label *ifFalse,
                                      bool fallthroughWhenFalse)
{
    if (fallthroughWhenFalse) {
        if (fallthroughWhenTrue) {
            ASSERT(ifTrue == ifFalse);
            return;
        }
        // Jump over the false block if true, otherwise fall through into it.
        JumpIf(condition, ifTrue);
    } else {
        // Jump to the false block if true.
        JumpIf(NegateCondition(condition), ifFalse);
        // Jump to the true block if it's not the next block.
        if (!fallthroughWhenTrue) {
            Jump(ifTrue);
        }
    }
}

inline void ArkSteedAssembler::InitializeObjectHeader(ArkSteedRegister glue,
                                                      ArkSteedRegister object,
                                                      ArkSteedRegister temp)
{
    Move(temp, static_cast<uint64_t>(JSTaggedValue::VALUE_ZERO));
    StoreField(temp, object, static_cast<int32_t>(JSObject::HASH_OFFSET));
    constexpr size_t emptyArrayOffset
        = static_cast<size_t>(ConstantIndex::EMPTY_ARRAY_OBJECT_INDEX) * JSTaggedValue::TaggedTypeSize();
    static_assert(emptyArrayOffset <= static_cast<size_t>(std::numeric_limits<int32_t>::max()));
    LoadField(temp, glue, static_cast<int32_t>(JSThread::GlueData::GetGlobalConstOffset(false)));
    LoadField(temp, temp, static_cast<int32_t>(emptyArrayOffset));
    StoreField(temp, object, static_cast<int32_t>(JSObject::PROPERTIES_OFFSET));
    StoreField(temp, object, static_cast<int32_t>(JSObject::ELEMENTS_OFFSET));
}

inline void ArkSteedAssembler::InitializeArrayFields(ArkSteedRegister glue,
                                                     ArkSteedRegister object,
                                                     uint32_t objectSize,
                                                     ArkSteedRegister value)
{
    Move(value, static_cast<uint64_t>(JSTaggedValue::VALUE_UNDEFINED));
    for (uint32_t offset = JSArray::SIZE; offset < objectSize; offset += JSTaggedValue::TaggedTypeSize()) {
        StoreField(value, object, static_cast<int32_t>(offset));
    }
    StoreField(value, object, static_cast<int32_t>(JSArray::TRACK_INFO_OFFSET));
    Move(value, 0);
    StoreInt32Field(value, object, static_cast<int32_t>(JSArray::LENGTH_OFFSET));
    StoreInt32Field(value, object, static_cast<int32_t>(JSArray::TRACE_INDEX_OFFSET));
    InitializeObjectHeader(glue, object, value);
    constexpr size_t lengthAccessorOffset
        = static_cast<size_t>(ConstantIndex::ARRAY_LENGTH_ACCESSOR) * JSTaggedValue::TaggedTypeSize();
    static_assert(lengthAccessorOffset <= static_cast<size_t>(std::numeric_limits<int32_t>::max()));
    static_assert(JSArray::LENGTH_INLINE_PROPERTY_INDEX == 0);
    LoadField(value, glue, static_cast<int32_t>(JSThread::GlueData::GetGlobalConstOffset(false)));
    LoadField(value, value, static_cast<int32_t>(lengthAccessorOffset));
    // The broker accepts only ordinary array layouts whose inline slots start at JSArray::SIZE.
    StoreField(value, object, static_cast<int32_t>(JSArray::SIZE));
}

inline void ArkSteedAssembler::JumpIfCMSGCEnabled(Label* target)
{
    if constexpr (G_USE_CMS_GC) {
        Jump(target);
    }
}

inline void ArkSteedAssembler::JumpIfCMCGCEnabled(ArkSteedRegister glue, ArkSteedRegister temp, Label* target)
{
    ASSERT(glue != temp);
    LoadInt32Field(temp, glue, static_cast<int32_t>(JSThread::GlueData::GetIsEnableCMCGCOffset(false)));
    Compare(temp, 0);
    JumpIf(Condition::NOT_EQUAL, target);
}

inline void ArkSteedAssembler::JumpIfHeapSamplingActive(ArkSteedRegister glue, ArkSteedRegister temp, Label* target)
{
    ASSERT(glue != temp);
    LoadField(temp, glue, static_cast<int32_t>(JSThread::GlueData::GetIsStartHeapSamplingOffset(false)));
    Compare(temp, static_cast<int32_t>(JSTaggedValue::VALUE_TRUE));
    JumpIf(Condition::EQUAL, target);
}

inline void ArkSteedAssembler::LoadStubEntry(ArkSteedRegister dst, kungfu::RuntimeStubCSigns::ID stubId)
{
    constexpr uint32_t SLOT_SIZE = sizeof(uintptr_t);
    uint32_t offset = static_cast<uint32_t>(JSThread::GlueData::GetRTStubEntriesOffset(false)) +
                      static_cast<uint32_t>(stubId) * SLOT_SIZE;
    LoadField(dst, GLUE_ROOT_REGISTER, static_cast<int32_t>(offset));
}

inline void ArkSteedAssembler::LoadStubEntry(ArkSteedRegister dst, kungfu::CommonStubCSigns::ID stubId)
{
    constexpr uint32_t SLOT_SIZE = sizeof(uintptr_t);
    uint32_t offset = static_cast<uint32_t>(JSThread::GlueData::GetCOStubEntriesOffset(false)) +
                      static_cast<uint32_t>(stubId) * SLOT_SIZE;
    LoadField(dst, GLUE_ROOT_REGISTER, static_cast<int32_t>(offset));
}

void ArkSteedAssembler::CallRuntime(kungfu::RuntimeStubCSigns::ID runtimeId)
{
    TemporaryRegisterScope scope(this);
    auto scratch = scope.AcquireScratch();
#if defined(PANDA_TARGET_AMD64)
    Move(x64::rax, GLUE_ROOT_REGISTER);
#elif defined(PANDA_TARGET_ARM64)
    Move(aarch64::x0, GLUE_ROOT_REGISTER);
#endif
    LoadStubEntry(scratch, kungfu::RuntimeStubCSigns::ID_CallRuntime);
    Call(scratch);
}

void ArkSteedAssembler::LoadGlue(ArkSteedRegister dst)
{
    Move(dst, GLUE_ROOT_REGISTER);
}

void ArkSteedAssembler::CallTrampoline(kungfu::RuntimeStubCSigns::ID stubId)
{
    TemporaryRegisterScope scope(this);
    auto scratch = scope.AcquireScratch();
#if defined(PANDA_TARGET_AMD64)
    Move(x64::rax, GLUE_ROOT_REGISTER);
#elif defined(PANDA_TARGET_ARM64)
    Move(aarch64::x0, GLUE_ROOT_REGISTER);
#endif
    LoadStubEntry(scratch, stubId);
    Call(scratch);
}

void ArkSteedAssembler::CallNativeBuiltinTrampoline(ArkSteedRegister nativeCode)
{
    TemporaryRegisterScope scope(this);
    ArkSteedRegister scratch = scope.AcquireScratch();
#if defined(PANDA_TARGET_AMD64)
    Move(x64::rax, nativeCode);
#elif defined(PANDA_TARGET_ARM64)
    Move(aarch64::x0, nativeCode);
#endif
    LoadStubEntry(scratch, RTSTUB_ID(PushCallArgsAndDispatchNative));
    Call(scratch);
}

void ArkSteedAssembler::CallNGCRuntime(kungfu::RuntimeStubCSigns::ID runtimeId)
{
    TemporaryRegisterScope scope(this);
    auto scratch = scope.AcquireScratch();
    LoadStubEntry(scratch, runtimeId);
    Call(scratch);
}

inline void ArkSteedAssembler::CallCommonStub(kungfu::CommonStubCSigns::ID stubId)
{
    // Common-stub C ABI: parameter 0 is always glue. The install is
    // centralized here so no vertex carries glue as an IR input; the code
    // generator pins vertex inputs to parameters 1..N.
    Move(GetParameterRegister(0), GLUE_ROOT_REGISTER);
    TemporaryRegisterScope scope(this);
    auto scratch = scope.AcquireScratch();
    LoadStubEntry(scratch, stubId);
    Call(scratch);
}

inline void ArkSteedAssembler::LoadBuiltinStubEntry(ArkSteedRegister dst, ArkSteedRegister glue, uint32_t builtinId)
{
    constexpr uint32_t SLOT_SIZE = sizeof(uintptr_t);
    uint32_t offset = static_cast<uint32_t>(JSThread::GlueData::GetBuiltinsStubEntriesOffset(false)) +
                      builtinId * SLOT_SIZE;
    LoadField(dst, glue, static_cast<int32_t>(offset));
}

inline void ArkSteedAssembler::CallDeoptHandler(kungfu::DeoptType deoptType)
{
    TemporaryRegisterScope scope(this);
    auto scratch = scope.AcquireScratch();
#if defined(PANDA_TARGET_AMD64)
    Move(x64::rdi, GLUE_ROOT_REGISTER);
    Move(x64::rsi, static_cast<uint64_t>(deoptType));
    Move(x64::rdx, JSTaggedValue::Undefined().GetRawData());
#elif defined(PANDA_TARGET_ARM64)
    Move(aarch64::x0, GLUE_ROOT_REGISTER);
    Move(aarch64::x1, static_cast<uint64_t>(deoptType));
    Move(aarch64::x2, JSTaggedValue::Undefined().GetRawData());
#endif
    LoadStubEntry(scratch, RTSTUB_ID(DeoptHandlerAsm));
    Call(scratch);
}
}  // namespace panda::ecmascript::arksteed

#endif  // ECMASCRIPT_ARKSTEED_ASSEMBLER_INL_H
