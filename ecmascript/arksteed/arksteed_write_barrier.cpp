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

#include "ecmascript/arksteed/arksteed_write_barrier.h"

#include "ecmascript/arksteed/arksteed_assembler-inl.h"  // IWYU pragma: keep
#include "ecmascript/js_thread.h"
#include "ecmascript/js_tagged_value.h"
#include "ecmascript/mem/mem.h"
#include "ecmascript/mem/region.h"

namespace panda::ecmascript::arksteed {
namespace {

ArkSteedRegList GetBarrierCallArgumentRegisters()
{
    return ArkSteedRegList {ArkSteedAssembler::GetParameterRegister(0), ArkSteedAssembler::GetParameterRegister(1),
                            ArkSteedAssembler::GetParameterRegister(2), ArkSteedAssembler::GetParameterRegister(3)};
}

class DeferredBarrierCall final : public ArkSteedDeferredCode {
public:
    DeferredBarrierCall(kungfu::RuntimeStubCSigns::ID runtimeId, ArkSteedRegister object,
                        int32_t offset, ArkSteedRegister value, const DeferredRegisterSnapshot &registerSnapshot,
                        bool preserveInputs)
        : runtimeId_(runtimeId),
          object_(object),
          offset_(offset),
          value_(value),
          registerSnapshot_(registerSnapshot),
          preserveInputs_(preserveInputs)
    {}

    Label *GetReturnLabel()
    {
        return &returnLabel_;
    }

    void Generate(ArkSteedAssembler *assembler) override
    {
        ArkSteedRegList clobberedRegisters;
        ArkDoubleRegList clobberedDoubleRegisters;
        switch (runtimeId_) {
            case RTSTUB_ID(ASMFastWriteBarrier):
                clobberedRegisters = GetASMFastWriteBarrierClobberedGeneralRegisters();
                // The stub preserves its argument registers, but the deferred adapter overwrites them
                // while materializing arbitrary ArkSteed input locations into the barrier ABI.
                clobberedRegisters |= GetBarrierCallArgumentRegisters();
                clobberedDoubleRegisters = GetASMFastWriteBarrierClobberedDoubleRegisters();
                break;
            case RTSTUB_ID(InsertOldToNewRSet):
            case RTSTUB_ID(InsertLocalToShareRSet):
            case RTSTUB_ID(MarkingBarrier):
            case RTSTUB_ID(SharedGCMarkingBarrier):
                clobberedRegisters = GetNGCRuntimeCallerSavedGeneralRegisters();
                clobberedDoubleRegisters = GetNGCRuntimeCallerSavedDoubleRegisters();
                break;
            default:
                UNREACHABLE();
        }

        ArkSteedRegList liveRegisters = registerSnapshot_.liveRegisters;
        if (preserveInputs_) {
            liveRegisters.Set(object_);
            liveRegisters.Set(value_);
        }
        liveRegisters &= clobberedRegisters;
        ArkDoubleRegList liveDoubleRegisters = registerSnapshot_.liveDoubleRegisters;
        liveDoubleRegisters &= clobberedDoubleRegisters;

        assembler->PushAll(liveRegisters);
        assembler->PushAll(liveDoubleRegisters);

        constexpr int32_t GLUE_SLOT = 0;
        constexpr int32_t OBJECT_SLOT = 1;
        constexpr int32_t OFFSET_SLOT = 2;
        constexpr int32_t VALUE_SLOT = 3;
        constexpr int32_t ABI_ARG_SLOT_COUNT = 4;
        assembler->ReserveCallArgSlots(ABI_ARG_SLOT_COUNT);
        // Glue comes from the root register; it is callee-saved so it survives
        // across the runtime call without being part of the live set.
        assembler->MoveRepr(MachineRepresentation::Word64, assembler->GetCallArgSlot(GLUE_SLOT),
                            ArkSteedAssembler::GLUE_ROOT_REGISTER);
        assembler->MoveRepr(MachineRepresentation::Tagged, assembler->GetCallArgSlot(OBJECT_SLOT), object_);
        assembler->MoveRepr(MachineRepresentation::Tagged, assembler->GetCallArgSlot(VALUE_SLOT), value_);

        ArkSteedRegister glueArg = ArkSteedAssembler::GetParameterRegister(0);
        ArkSteedRegister objectArg = ArkSteedAssembler::GetParameterRegister(1);
        ArkSteedRegister offsetArg = ArkSteedAssembler::GetParameterRegister(2);
        ArkSteedRegister valueArg = ArkSteedAssembler::GetParameterRegister(3);
        assembler->Move(offsetArg, static_cast<int64_t>(offset_));
        assembler->MoveRepr(MachineRepresentation::Word64, assembler->GetCallArgSlot(OFFSET_SLOT), offsetArg);

        assembler->MoveRepr(MachineRepresentation::Word64, glueArg, assembler->GetCallArgSlot(GLUE_SLOT));
        assembler->MoveRepr(MachineRepresentation::Tagged, objectArg, assembler->GetCallArgSlot(OBJECT_SLOT));
        assembler->MoveRepr(MachineRepresentation::Word64, offsetArg, assembler->GetCallArgSlot(OFFSET_SLOT));
        assembler->MoveRepr(MachineRepresentation::Tagged, valueArg, assembler->GetCallArgSlot(VALUE_SLOT));
        assembler->CallNGCRuntime(runtimeId_);
        assembler->FreeCallArgSlots(ABI_ARG_SLOT_COUNT);

        assembler->PopAll(liveDoubleRegisters);
        assembler->PopAll(liveRegisters);
        assembler->Jump(&returnLabel_);
    }

private:
    kungfu::RuntimeStubCSigns::ID runtimeId_;
    ArkSteedRegister object_;
    int32_t offset_;
    ArkSteedRegister value_;
    DeferredRegisterSnapshot registerSnapshot_;
    bool preserveInputs_;
    Label returnLabel_;
};

}  // namespace

#define __ assembler_->

void ArkSteedWriteBarrierEmitter::CallBarrierRuntime(kungfu::RuntimeStubCSigns::ID runtimeId,
                                                     ArkSteedRegister object, int32_t offset,
                                                     ArkSteedRegister value, bool preserveInputs)
{
    ASSERT(chunk_ != nullptr);
    ASSERT(deferredCode_ != nullptr);
    auto *deferredCall =
        chunk_->New<DeferredBarrierCall>(runtimeId, object, offset, value, registerSnapshot_, preserveInputs);
    deferredCode_->push_back(deferredCall);
    __ Jump(deferredCall->GetEntryLabel());
    __ Bind(deferredCall->GetReturnLabel());
}

void ArkSteedWriteBarrierEmitter::EmitFastWriteBarrier(ArkSteedRegister object,
                                                       ArkSteedRegister value, int32_t offset, bool preserveInputs)
{
    CallBarrierRuntime(RTSTUB_ID(ASMFastWriteBarrier), object, offset, value, preserveInputs);
}

void ArkSteedWriteBarrierEmitter::EmitWriteBarrier(ArkSteedRegister object,
                                                   ArkSteedRegister value, int32_t offset,
                                                   ArkSteedWriteBarrierKind barrierKind,
                                                   ArkSteedRegister objectRegionScratch,
                                                   ArkSteedRegister valueRegionScratch, bool preserveInputs)
{
    if (barrierKind == ArkSteedWriteBarrierKind::NO_BARRIER) {
        return;
    }

#if USE_STICKY_CMS_GC
    if (barrierKind == ArkSteedWriteBarrierKind::GENERIC_BARRIER) {
        EmitFastWriteBarrier(object, value, offset, preserveInputs);
        return;
    }
#endif

    Label done;
    Label callBarrier;
    constexpr int64_t REGION_BASE_MASK = static_cast<int64_t>(~(JSTaggedValue::TAG_MARK | DEFAULT_REGION_MASK));
    static_assert(RegionSpaceFlag::VALID_SPACE_MASK == 0xff);
    __ Move(valueRegionScratch, value);
    __ And(valueRegionScratch, REGION_BASE_MASK);
    __ LoadUint8Field(valueRegionScratch, valueRegionScratch,
                      static_cast<int32_t>(Region::PackedData::GetFlagsOffset(false)));
    __ Compare(valueRegionScratch, static_cast<int32_t>(RegionSpaceFlag::IN_SHARED_READ_ONLY_SPACE));
    __ JumpIf(Condition::EQUAL, &done);

    // Only local non-young values can skip the barrier based on local marking.
    // Compare space boundaries directly so young and shared values go to the
    // common stub, which handles shared marking independently of the local GC state.
    constexpr int32_t LOCAL_NON_YOUNG_BEGIN = static_cast<int32_t>(RegionSpaceFlag::IN_YOUNG_SPACE) + 1;
    __ Compare(valueRegionScratch, LOCAL_NON_YOUNG_BEGIN);
    __ JumpIf(Condition::LESS_THAN, &callBarrier);
    __ Compare(valueRegionScratch, static_cast<int32_t>(RegionSpaceFlag::SHARED_SPACE_BEGIN));
    __ JumpIf(Condition::GREATER_THAN_OR_EQUAL, &callBarrier);
    __ LoadField(objectRegionScratch, ArkSteedAssembler::GLUE_ROOT_REGISTER,
                 static_cast<int32_t>(JSThread::GlueData::GetGCStateBitFieldOffset(false)));
    __ And(objectRegionScratch, static_cast<int64_t>(JSThread::CONCURRENT_MARKING_BITFIELD_MASK));
    __ Compare(objectRegionScratch, static_cast<int32_t>(MarkStatus::READY_TO_MARK));
    __ JumpIf(Condition::EQUAL, &done);

    __ Bind(&callBarrier);
    // Keep region-pair dispatch, remembered sets and marking in shared machine
    // code instead of emitting all their paths and call adapters at every store.
    EmitFastWriteBarrier(object, value, offset, preserveInputs);
    __ Bind(&done);
}

void ArkSteedWriteBarrierEmitter::StoreTaggedField(ArkSteedRegister object,
                                                   ArkSteedRegister value, int32_t offset,
                                                   ArkSteedWriteBarrierKind barrierKind,
                                                   ArkSteedRegister objectRegionScratch,
                                                   ArkSteedRegister valueRegionScratch,
                                                   ArkSteedWriteBarrierValueKind valueKind)
{
    if (barrierKind == ArkSteedWriteBarrierKind::NO_BARRIER || valueKind == ArkSteedWriteBarrierValueKind::NonHeap) {
        __ StoreField(value, object, offset);
        return;
    }

    if (valueKind == ArkSteedWriteBarrierValueKind::HeapObject) {
        __ StoreField(value, object, offset);
        EmitWriteBarrier(object, value, offset, barrierKind, objectRegionScratch, valueRegionScratch);
        return;
    }

    Label done;
    __ StoreField(value, object, offset);
    __ Move(objectRegionScratch, value);
    __ And(objectRegionScratch, static_cast<int64_t>(JSTaggedValue::TAG_HEAPOBJECT_MASK));
    __ Compare(objectRegionScratch, 0);
    __ JumpIf(Condition::NOT_EQUAL, &done);
    EmitWriteBarrier(object, value, offset, barrierKind, objectRegionScratch, valueRegionScratch);
    __ Bind(&done);
}

void ArkSteedWriteBarrierEmitter::TransitionHClass(ArkSteedRegister object,
                                                   ArkSteedRegister hclass, ArkSteedRegister objectRegionScratch,
                                                   ArkSteedRegister hclassRegionScratch)
{
    static_assert(TaggedObject::HCLASS_OFFSET == 0);
    constexpr int32_t offset = static_cast<int32_t>(TaggedObject::HCLASS_OFFSET);
#ifndef ARK_USE_SATB_BARRIER
    __ StoreInt32FieldRelease(hclass, object, offset);
    EmitWriteBarrier(object, hclass, offset, ArkSteedWriteBarrierKind::GENERIC_BARRIER, objectRegionScratch,
                     hclassRegionScratch);
#else
    CallBarrierRuntime(RTSTUB_ID(ASMFastWriteBarrier), object, offset, hclass, true);
    __ StoreInt32FieldRelease(hclass, object, offset);
#endif
}

void ArkSteedWriteBarrierEmitter::InitializeHClass(ArkSteedRegister object,
                                                   ArkSteedRegister hclass, ArkSteedRegister objectRegionScratch,
                                                   ArkSteedRegister hclassRegionScratch)
{
    static_assert(TaggedObject::HCLASS_OFFSET == 0);
    constexpr int32_t offset = static_cast<int32_t>(TaggedObject::HCLASS_OFFSET);
    // The header store after the deferred call still needs object and hclass.
    EmitWriteBarrier(object, hclass, offset, ArkSteedWriteBarrierKind::GENERIC_BARRIER, objectRegionScratch,
                     hclassRegionScratch, true);
    __ StoreHeaderRelease(hclass, object);
}

void ArkSteedWriteBarrierEmitter::StoreTaggedElement(ArkSteedRegister object,
                                                     ArkSteedRegister byteOffset, ArkSteedRegister value,
                                                     ArkSteedRegister scratch, ArkSteedWriteBarrierValueKind valueKind)
{
    ASSERT(object == ArkSteedAssembler::GetParameterRegister(1));
    ASSERT(byteOffset == ArkSteedAssembler::GetParameterRegister(2));
    ASSERT(value == ArkSteedAssembler::GetParameterRegister(3));

    if (valueKind == ArkSteedWriteBarrierValueKind::NonHeap) {
        __ StoreField(value, object, byteOffset);
        return;
    }
    if (valueKind == ArkSteedWriteBarrierValueKind::HeapObject) {
        __ StoreField(value, object, byteOffset);
        __ MoveRepr(MachineRepresentation::Word64, ArkSteedAssembler::GetParameterRegister(0),
                    ArkSteedAssembler::GLUE_ROOT_REGISTER);
        __ CallNGCRuntime(RTSTUB_ID(ASMFastWriteBarrier));
        return;
    }

    Label needsBarrier;
    Label done;
    __ Move(scratch, value);
    __ And(scratch, static_cast<int64_t>(JSTaggedValue::TAG_HEAPOBJECT_MASK));
    __ Compare(scratch, 0);
    __ JumpIf(Condition::EQUAL, &needsBarrier);
    __ StoreField(value, object, byteOffset);
    __ Jump(&done);

    __ Bind(&needsBarrier);
    __ StoreField(value, object, byteOffset);
    __ MoveRepr(MachineRepresentation::Word64, ArkSteedAssembler::GetParameterRegister(0),
                ArkSteedAssembler::GLUE_ROOT_REGISTER);
    __ CallNGCRuntime(RTSTUB_ID(ASMFastWriteBarrier));
    __ Bind(&done);
}

#undef __
}  // namespace panda::ecmascript::arksteed
