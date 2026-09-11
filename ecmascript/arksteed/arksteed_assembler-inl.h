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

#include "ecmascript/arksteed/arksteed_assembler.h"
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

inline void ArkSteedAssembler::LoadObjectSizeFromHClass(ArkSteedRegister hclass, ArkSteedRegister objectSize)
{
    ASSERT(hclass != objectSize);
    static_assert(JSTaggedValue::TaggedTypeSize() == (1U << TAGGED_TYPE_SIZE_LOG));
    constexpr int32_t objectSizeMask = static_cast<int32_t>((1U << JSHClass::ObjectSizeInWordsBits::SIZE) - 1U);

    LoadInt32Field(objectSize, hclass, static_cast<int32_t>(JSHClass::BIT_FIELD1_OFFSET));
    Lsr(objectSize, JSHClass::ObjectSizeInWordsBits::START_BIT);
    And(objectSize, objectSizeMask);
    ShiftLeft(objectSize, TAGGED_TYPE_SIZE_LOG);
}

inline void ArkSteedAssembler::LoadInlinedPropsStartFromHClass(ArkSteedRegister hclass, ArkSteedRegister offset)
{
    ASSERT(hclass != offset);
    static_assert(JSTaggedValue::TaggedTypeSize() == (1U << TAGGED_TYPE_SIZE_LOG));
    constexpr int32_t inlinedPropsStartMask
        = static_cast<int32_t>((1U << JSHClass::InlinedPropsStartBits::SIZE) - 1U);

    LoadInt32Field(offset, hclass, static_cast<int32_t>(JSHClass::BIT_FIELD1_OFFSET));
    Lsr(offset, JSHClass::InlinedPropsStartBits::START_BIT);
    And(offset, inlinedPropsStartMask);
    ShiftLeft(offset, TAGGED_TYPE_SIZE_LOG);
}

inline void ArkSteedAssembler::TryAllocateInYoungSpace(
    ArkSteedRegister glue, ArkSteedRegister size, ArkSteedRegister result, ArkSteedRegister temp, Label* failed)
{
    ASSERT(glue != size);
    ASSERT(glue != result);
    ASSERT(glue != temp);
    ASSERT(size != result);
    ASSERT(size != temp);
    ASSERT(result != temp);

    LoadField(result, glue, static_cast<int32_t>(JSThread::GlueData::GetNewSpaceAllocationTopAddressOffset(false)));
    LoadField(result, result, 0);
    LoadField(temp, glue, static_cast<int32_t>(JSThread::GlueData::GetNewSpaceAllocationEndAddressOffset(false)));
    LoadField(temp, temp, 0);
    // Match the interpreter allocator: the trusted regular-object size cannot overflow a valid heap address.
    Add(result, size);
    Compare(result, temp);
    JumpIf(Condition::ABOVE, failed);

    // Publishing top commits the allocation. No failure path is allowed after this store.
    LoadField(temp, glue, static_cast<int32_t>(JSThread::GlueData::GetNewSpaceAllocationTopAddressOffset(false)));
    StoreField(result, temp, 0);
    Sub(result, size);
}

void ArkSteedAssembler::CallRuntime(kungfu::RuntimeStubCSigns::ID runtimeId)
{
    TemporaryRegisterScope scope(this);
    ASSERT(entryThread_ != nullptr);
    Address address = entryThread_->GetRTInterface(static_cast<size_t>(kungfu::RuntimeStubCSigns::ID_CallRuntime));
    auto scratch = scope.AcquireScratch();
#if defined(PANDA_TARGET_AMD64)
    Move(x64::rax, static_cast<uint64_t>(entryThread_->GetGlueAddr()));
#elif defined(PANDA_TARGET_ARM64)
    Move(aarch64::x0, static_cast<uint64_t>(entryThread_->GetGlueAddr()));
#endif
    Move(scratch, static_cast<uint64_t>(address));
    Call(scratch);
}

void ArkSteedAssembler::LoadGlue(ArkSteedRegister dst)
{
    ASSERT(entryThread_ != nullptr);
    Move(dst, static_cast<uint64_t>(entryThread_->GetGlueAddr()));
}

void ArkSteedAssembler::CallTrampoline(kungfu::RuntimeStubCSigns::ID stubId)
{
    TemporaryRegisterScope scope(this);
    ASSERT(entryThread_ != nullptr);
    Address address = entryThread_->GetRTInterface(static_cast<size_t>(stubId));
    auto scratch = scope.AcquireScratch();
#if defined(PANDA_TARGET_AMD64)
    Move(x64::rax, static_cast<uint64_t>(entryThread_->GetGlueAddr()));
#elif defined(PANDA_TARGET_ARM64)
    Move(aarch64::x0, static_cast<uint64_t>(entryThread_->GetGlueAddr()));
#endif
    Move(scratch, static_cast<uint64_t>(address));
    Call(scratch);
}

void ArkSteedAssembler::CallNativeBuiltinTrampoline(ArkSteedRegister nativeCode)
{
    TemporaryRegisterScope scope(this);
    ASSERT(entryThread_ != nullptr);
    Address address = entryThread_->GetRTInterface(RTSTUB_ID(PushCallArgsAndDispatchNative));
    ArkSteedRegister scratch = scope.AcquireScratch();
#if defined(PANDA_TARGET_AMD64)
    Move(x64::rax, nativeCode);
#elif defined(PANDA_TARGET_ARM64)
    Move(aarch64::x0, nativeCode);
#endif
    Move(scratch, static_cast<uint64_t>(address));
    Call(scratch);
}

void ArkSteedAssembler::CallNGCRuntime(kungfu::RuntimeStubCSigns::ID runtimeId)
{
    TemporaryRegisterScope scope(this);
    ASSERT(entryThread_ != nullptr);
    Address address = entryThread_->GetRTInterface(static_cast<size_t>(runtimeId));
    auto scratch = scope.AcquireScratch();
    Move(scratch, static_cast<uint64_t>(address));
    Call(scratch);
}

inline void ArkSteedAssembler::CallCommonStub(uint32_t stubId)
{
    TemporaryRegisterScope scope(this);
    ASSERT(entryThread_ != nullptr);
    Address address = entryThread_->GetFastStubEntry(stubId);
    auto scratch = scope.AcquireScratch();
    Move(scratch, static_cast<uint64_t>(address));
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
    ASSERT(entryThread_ != nullptr);
    Address address = entryThread_->GetRTInterface(RTSTUB_ID(DeoptHandlerAsm));
    auto scratch = scope.AcquireScratch();
#if defined(PANDA_TARGET_AMD64)
    Move(x64::rdi, static_cast<uint64_t>(entryThread_->GetGlueAddr()));
    Move(x64::rsi, static_cast<uint64_t>(deoptType));
    Move(x64::rdx, JSTaggedValue::Undefined().GetRawData());
#elif defined(PANDA_TARGET_ARM64)
    Move(aarch64::x0, static_cast<uint64_t>(entryThread_->GetGlueAddr()));
    Move(aarch64::x1, static_cast<uint64_t>(deoptType));
    Move(aarch64::x2, JSTaggedValue::Undefined().GetRawData());
#endif
    Move(scratch, static_cast<uint64_t>(address));
    Call(scratch);
}
}  // namespace panda::ecmascript::arksteed

#endif  // ECMASCRIPT_ARKSTEED_ASSEMBLER_INL_H
