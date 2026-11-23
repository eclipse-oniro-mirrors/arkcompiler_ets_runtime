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

#include <algorithm>
#include <limits>
#include <sstream>

#if defined(__aarch64__) && (defined(__linux__) || defined(PANDA_TARGET_OHOS))
#include <sys/auxv.h>
#endif

#include "ecmascript/arksteed/arch/arm64/arksteed_assembler_arm64-inl.h"

#include "ecmascript/arksteed/arksteed_assembler-inl.h"
#include "ecmascript/arksteed/arksteed_assembler.h"
#include "ecmascript/arksteed/arksteed_graph.h"
#include "ecmascript/base/bit_helper.h"
#include "ecmascript/byte_array.h"
#include "ecmascript/global_env.h"
#include "ecmascript/js_array.h"
#include "ecmascript/js_function.h"
#include "ecmascript/js_hclass.h"
#include "ecmascript/js_native_pointer.h"
#include "ecmascript/js_thread.h"
#include "ecmascript/js_tagged_value_wrapper.h"
#include "ecmascript/js_typed_array.h"
#include "ecmascript/mem/mem.h"
#include "ecmascript/mem/tagged_object.h"
#include "ecmascript/method.h"
#include "ecmascript/string/line_string.h"
#include "ecmascript/tagged_array.h"

namespace panda::ecmascript::arksteed {
#if defined(PANDA_TARGET_ARM64)
void ArkSteedAssembler::EmitAddSubImmediate(ArkSteedRegister dst, ArkSteedRegister src, uint64_t immediate,
                                            AddSubImmediateOp operation)
{
    ASSERT(dst.IsW() == src.IsW());
    ASSERT(FitsAddSubImmediate(immediate));

    constexpr uint32_t SF_BIT = 31U;
    constexpr uint32_t SET_FLAGS_BIT = 29U;
    constexpr uint32_t SHIFT_BIT = 22U;
    constexpr uint32_t IMMEDIATE_LOW_BIT = 10U;
    constexpr uint32_t SOURCE_LOW_BIT = 5U;
    constexpr uint32_t IMM12_MASK = (1U << 12U) - 1U;

    uint32_t opcode = 0U;
    switch (operation) {
        case AddSubImmediateOp::ADD:
            opcode = aarch64::ADD_Imm;
            break;
        case AddSubImmediateOp::SUB:
            opcode = aarch64::SUB_Imm;
            break;
        case AddSubImmediateOp::ADDS:
            opcode = aarch64::ADD_Imm | (1U << SET_FLAGS_BIT);
            break;
        case AddSubImmediateOp::SUBS:
            opcode = aarch64::SUB_Imm | (1U << SET_FLAGS_BIT);
            break;
        default:
            UNREACHABLE();
    }

    uint32_t shift = immediate > IMM12_MASK ? 1U : 0U;
    uint32_t imm12 = static_cast<uint32_t>(shift == 0U ? immediate : immediate >> 12U);
    uint32_t size = dst.IsW() ? 0U : 1U;
    uint32_t code = opcode | (size << SF_BIT) | (shift << SHIFT_BIT) | (imm12 << IMMEDIATE_LOW_BIT) |
                    (static_cast<uint32_t>(src.GetId()) << SOURCE_LOW_BIT) | static_cast<uint32_t>(dst.GetId());
    assembler_.EmitU32(code);
}

// =============================================================================
// Register Move Operations
// =============================================================================

void ArkSteedAssembler::Move(ArkSteedRegister dst, ArkSteedRegister src)
{
    assembler_.Mov(dst, src);
}

void ArkSteedAssembler::Move(ArkSteedRegister dst, int32_t immediate)
{
    assembler_.Mov(dst, aarch64::Immediate(immediate));
}

void ArkSteedAssembler::Move(ArkSteedRegister dst, int64_t immediate)
{
    assembler_.Mov(dst, aarch64::Immediate(immediate));
}

void ArkSteedAssembler::Move(ArkSteedRegister dst, uint64_t immediate)
{
    Move(dst, static_cast<int64_t>(immediate));
}

void ArkSteedAssembler::MoveEmbeddedTagged(ArkSteedRegister dst, uint32_t handleIndex)
{
    // Keep both the final literal displacement and the unresolved-label link chain within imm19 range.
    CheckCodePools(true);

    size_t literalIndex = 0;
    auto it = embeddedLiteralIndexByHandle_.find(handleIndex);
    if (it == embeddedLiteralIndexByHandle_.end()) {
        literalIndex = pendingEmbeddedLiterals_.size();
        auto literal = std::make_unique<PendingEmbeddedLiteral>();
        literal->handleIndex = handleIndex;
        pendingEmbeddedLiterals_.push_back(std::move(literal));
        embeddedLiteralIndexByHandle_.emplace(handleIndex, literalIndex);
    } else {
        literalIndex = it->second;
    }

    PendingEmbeddedLiteral *literal = pendingEmbeddedLiterals_[literalIndex].get();
    uint32_t loadOffset = GetPcOffset();
    literal->loadOffsets.push_back(loadOffset);
    UpdateEmbeddedLiteralPoolCheck(loadOffset, literalIndex);
    assembler_.Ldr(dst, &literal->label);
}

void ArkSteedAssembler::FinalizeEmbeddedRefs()
{
    EmitEmbeddedLiteralPool(true);
}

void ArkSteedAssembler::Move(ArkSteedDoubleRegister dst, ArkSteedDoubleRegister src)
{
    assembler_.Mov(dst, src);
}

void ArkSteedAssembler::Move(ArkSteedDoubleRegister dst, double immediate)
{
    uint64_t bits = base::bit_cast<uint64_t>(immediate);
    if (bits == 0) {
        assembler_.Fmov(dst, aarch64::xzr);
        return;
    }
    if (assembler_.TryFmov(dst, immediate)) {
        return;
    }
    TemporaryRegisterScope scope(this);
    ArkSteedRegister scratch = scope.AcquireScratch();
    Move(scratch, static_cast<int64_t>(bits));
    assembler_.Fmov(dst, scratch);
}

void ArkSteedAssembler::Move(ArkSteedDoubleRegister dst, double immediate, ArkSteedRegister scratch)
{
    uint64_t bits = base::bit_cast<uint64_t>(immediate);
    if (bits == 0) {
        assembler_.Fmov(dst, aarch64::xzr);
        return;
    }
    if (assembler_.TryFmov(dst, immediate)) {
        return;
    }
    Move(scratch, static_cast<int64_t>(bits));
    assembler_.Fmov(dst, scratch);
}

void ArkSteedAssembler::Move(ArkSteedDoubleRegister dst, ArkSteedRegister src)
{
    assembler_.Fmov(dst, src);
}

void ArkSteedAssembler::Move(ArkSteedRegister dst, ArkSteedDoubleRegister src)
{
    assembler_.Fmov(dst, src);
}

// =============================================================================
// Memory Operations
// =============================================================================

void ArkSteedAssembler::LoadField(ArkSteedRegister dst, ArkSteedRegister base, int32_t offset)
{
    aarch64::MemoryOperand operand(base, offset, aarch64::AddrMode::OFFSET);
    LoadRegisterWithOperand(dst, operand);
}

void ArkSteedAssembler::LoadUint8Field(ArkSteedRegister dst, ArkSteedRegister base, int32_t offset)
{
    assembler_.Ldrb(dst.W(), aarch64::MemoryOperand(base, offset));
}

void ArkSteedAssembler::LoadInt32Field(ArkSteedRegister dst, ArkSteedRegister base, int32_t offset)
{
    aarch64::MemoryOperand operand(base, offset, aarch64::AddrMode::OFFSET);
    assembler_.Ldr(dst.W(), operand);
}

std::optional<int32_t> ArkSteedAssembler::TryGetTaggedElementOffset(int32_t index)
{
    if (index < 0) {
        return std::nullopt;
    }
    int64_t offset = static_cast<int64_t>(TaggedArray::DATA_OFFSET) +
                     static_cast<int64_t>(index) * static_cast<int64_t>(sizeof(JSTaggedType));
    if (offset > 4095 * static_cast<int64_t>(sizeof(JSTaggedType))) {  // 4095: maximum unsigned imm12 value.
        return std::nullopt;
    }
    return static_cast<int32_t>(offset);
}

void ArkSteedAssembler::LoadObjectType(ArkSteedRegister dst, ArkSteedRegister object)
{
    LoadHClassFromObject(dst, object);
    static_assert(JSHClass::ObjectTypeBits::START_BIT == 0 && JSHClass::ObjectTypeBits::SIZE == 8);
    LoadUint8Field(dst, dst, static_cast<int32_t>(JSHClass::BIT_FIELD_OFFSET));
}

void ArkSteedAssembler::LoadTaggedElement(ArkSteedRegister dst, ArkSteedRegister elements, ArkSteedRegister index)
{
    constexpr uint8_t TAGGED_SIZE_SHIFT = static_cast<uint8_t>(TAGGED_TYPE_SIZE_LOG);
    assembler_.Add(dst, elements, aarch64::Operand(index, aarch64::UXTW, TAGGED_SIZE_SHIFT));
    LoadField(dst, dst, static_cast<int32_t>(TaggedArray::DATA_OFFSET));
}

void ArkSteedAssembler::StoreTaggedElement(ArkSteedRegister elements, ArkSteedRegister index, int32_t value,
                                           ArkSteedRegister scratch)
{
    ASSERT(value == 0);
    StoreTaggedElement(elements, index, aarch64::xzr, scratch);
}

std::optional<int32_t> ArkSteedAssembler::TryGetScaledElementOffset(std::optional<int32_t> index,
                                                                    uint32_t elementShift, int32_t headerOffset)
{
    ASSERT(elementShift <= TAGGED_TYPE_SIZE_LOG);
    if (!index || *index < 0) {
        return std::nullopt;
    }
    const int64_t scale = int64_t {1} << elementShift;
    const int64_t byteOffset = static_cast<int64_t>(*index) * scale + headerOffset;
    if (!FitsScaledImmediateOffset(byteOffset, scale)) {
        return std::nullopt;
    }
    return static_cast<int32_t>(byteOffset);
}

std::optional<int32_t> ArkSteedAssembler::TryGetTaggedElementBarrierOffset(std::optional<int32_t> index)
{
    if (!index || *index < 0) {
        return std::nullopt;
    }
    const int64_t byteOffset = (static_cast<int64_t>(*index) << TAGGED_TYPE_SIZE_LOG) + TaggedArray::DATA_OFFSET;
    // A non-negative int32 offset materializes in at most two instructions, matching or beating shift + add.
    if (byteOffset > std::numeric_limits<int32_t>::max()) {
        return std::nullopt;
    }
    return static_cast<int32_t>(byteOffset);
}

void ArkSteedAssembler::ComputeTaggedElementByteOffset(ArkSteedRegister dst, ArkSteedRegister index)
{
    ShiftLeft(dst, index, TAGGED_TYPE_SIZE_LOG);
    Add(dst, static_cast<int32_t>(TaggedArray::DATA_OFFSET));
}

void ArkSteedAssembler::StoreTaggedElement(ArkSteedRegister elements, ArkSteedRegister index,
                                           ArkSteedRegister value, ArkSteedRegister scratch)
{
    constexpr uint8_t TAGGED_SIZE_SHIFT = static_cast<uint8_t>(TAGGED_TYPE_SIZE_LOG);
    assembler_.Add(scratch, elements, aarch64::Operand(index, aarch64::UXTW, TAGGED_SIZE_SHIFT));
    StoreField(value, scratch, static_cast<int32_t>(TaggedArray::DATA_OFFSET));
}

void ArkSteedAssembler::LoadLineStringCharCode(ArkSteedRegister dst, ArkSteedRegister string, ArkSteedRegister index,
                                               ArkSteedRegister lengthAndFlags)
{
    TemporaryRegisterScope scope(this);
    ArkSteedRegister data = scope.AcquireScratch();
    Move(data, string);
    Add(data, static_cast<int32_t>(LineString::DATA_OFFSET));

    Label utf16;
    Label done;
    TestAndBranchIfNotZero(lengthAndFlags, BaseString::CompressedStatusBit::START_BIT, &utf16);
    assembler_.Ldrb(dst.W(), aarch64::MemoryOperand(data, index, aarch64::UXTW, 0));
    Jump(&done);
    Bind(&utf16);
    assembler_.Ldrh(dst.W(), aarch64::MemoryOperand(data, index, aarch64::UXTW, 1));
    Bind(&done);
}

void ArkSteedAssembler::LoadTypedArrayDataPointer(ArkSteedRegister dst, ArkSteedRegister receiver,
                                                  ArkSteedRegister storage, ArkSteedRegister scratch, bool isOnHeap)
{
    if (isOnHeap) {
        assembler_.Add(dst, storage, aarch64::Operand(static_cast<int64_t>(ByteArray::DATA_OFFSET)));
        return;
    }
    LoadInt32Field(scratch, receiver, static_cast<int32_t>(JSTypedArray::BYTE_OFFSET_OFFSET));
    LoadField(dst, storage, static_cast<int32_t>(JSNativePointer::POINTER_OFFSET));
    Add(dst, scratch);
}

void ArkSteedAssembler::LoadTypedArrayIntElement(ArkSteedRegister dst, ArkSteedRegister data, ArkSteedRegister index,
                                                 JSType elementType)
{
    switch (elementType) {
        case JSType::JS_INT8_ARRAY:
            assembler_.Ldrsb(dst.W(), aarch64::MemoryOperand(data, index, aarch64::UXTW, 0));
            break;
        case JSType::JS_UINT8_ARRAY:
        case JSType::JS_UINT8_CLAMPED_ARRAY:
            assembler_.Ldrb(dst.W(), aarch64::MemoryOperand(data, index, aarch64::UXTW, 0));
            break;
        case JSType::JS_INT16_ARRAY:
            assembler_.Ldrsh(dst.W(), aarch64::MemoryOperand(data, index, aarch64::UXTW, 1));
            break;
        case JSType::JS_UINT16_ARRAY:
            assembler_.Ldrh(dst.W(), aarch64::MemoryOperand(data, index, aarch64::UXTW, 1));
            break;
        case JSType::JS_INT32_ARRAY:
            assembler_.Ldr(dst.W(),
                           aarch64::MemoryOperand(data, index, aarch64::UXTW, 2));  // 2: scale index by 4 bytes.
            break;
        default:
            UNREACHABLE();
    }
}

void ArkSteedAssembler::LoadTypedArrayDoubleElement(ArkSteedDoubleRegister dst, ArkSteedRegister data,
                                                    ArkSteedRegister index, ArkSteedRegister scratch,
                                                    JSType elementType)
{
    switch (elementType) {
        case JSType::JS_UINT32_ARRAY:
            assembler_.Ldr(scratch.W(),
                           aarch64::MemoryOperand(data, index, aarch64::UXTW, 2));  // 2: scale index by 4 bytes.
            assembler_.Ucvtf(dst, scratch.W());
            break;
        case JSType::JS_FLOAT32_ARRAY: {
            auto single = aarch64::VRegister::Create(dst.Code(), aarch64::S_REG_SIZE);
            assembler_.Ldr(single,
                           aarch64::MemoryOperand(data, index, aarch64::UXTW, 2));  // 2: scale index by 4 bytes.
            assembler_.Fcvt(dst, single);
            break;
        }
        case JSType::JS_FLOAT64_ARRAY:
            assembler_.Ldr(dst, aarch64::MemoryOperand(data, index, aarch64::UXTW, 3));  // 3: scale index by 8 bytes.
            break;
        default:
            UNREACHABLE();
    }
}

void ArkSteedAssembler::StoreTypedArrayIntElement(ArkSteedRegister value, ArkSteedRegister data,
                                                  ArkSteedRegister index, JSType elementType)
{
    switch (elementType) {
        case JSType::JS_INT8_ARRAY:
        case JSType::JS_UINT8_ARRAY:
        case JSType::JS_UINT8_CLAMPED_ARRAY:
            assembler_.Strb(value.W(), aarch64::MemoryOperand(data, index, aarch64::UXTW, 0));
            return;
        case JSType::JS_INT16_ARRAY:
        case JSType::JS_UINT16_ARRAY:
            assembler_.Strh(value.W(), aarch64::MemoryOperand(data, index, aarch64::UXTW, 1));
            return;
        case JSType::JS_INT32_ARRAY:
        case JSType::JS_UINT32_ARRAY:
            assembler_.Str(value.W(), aarch64::MemoryOperand(data, index, aarch64::UXTW, 2));
            return;
        default:
            UNREACHABLE();
    }
}

bool ArkSteedAssembler::CanStoreConstant(uint64_t bits)
{
    return bits == 0;
}

void ArkSteedAssembler::StoreField(int32_t value, ArkSteedRegister base, int32_t offset)
{
    ASSERT(value == 0);
    StoreField(aarch64::xzr, base, offset);
}

void ArkSteedAssembler::StoreField(ArkSteedRegister src, ArkSteedRegister base, int32_t offset)
{
    aarch64::MemoryOperand operand(base, offset, aarch64::AddrMode::OFFSET);
    StoreRegisterWithOperand(src, operand);
}

void ArkSteedAssembler::StoreField(ArkSteedRegister src, ArkSteedRegister base, ArkSteedRegister offset)
{
    assembler_.Str(src, aarch64::MemoryOperand(base, offset, aarch64::UXTX));
}

void ArkSteedAssembler::StoreInt8Field(ArkSteedRegister src, ArkSteedRegister base, int32_t offset)
{
    assembler_.Strb(src.W(), aarch64::MemoryOperand(base, offset, aarch64::AddrMode::OFFSET));
}

void ArkSteedAssembler::StoreInt16Field(ArkSteedRegister src, ArkSteedRegister base, int32_t offset)
{
    assembler_.Strh(src.W(), aarch64::MemoryOperand(base, offset, aarch64::AddrMode::OFFSET));
}

bool ArkSteedAssembler::CanStoreConstant(int32_t value)
{
    return value == 0;
}

void ArkSteedAssembler::StoreInt32Field(int32_t value, ArkSteedRegister base, int32_t offset)
{
    ASSERT(CanStoreConstant(value));
    StoreInt32Field(aarch64::wzr, base, offset);
}

void ArkSteedAssembler::StoreInt32Field(ArkSteedRegister src, ArkSteedRegister base, int32_t offset)
{
    aarch64::MemoryOperand operand(base, offset, aarch64::AddrMode::OFFSET);
    StoreRegisterWithOperand(src.W(), operand);
}

void ArkSteedAssembler::StoreInt32FieldRelease(ArkSteedRegister src, ArkSteedRegister base, int32_t offset)
{
    aarch64::MemoryOperand operand(base, offset, aarch64::AddrMode::OFFSET);
    assembler_.Stlr(src.W(), operand);
}

void ArkSteedAssembler::StoreHeaderRelease(ArkSteedRegister src,
                                           ArkSteedRegister object)
{
    static_assert(TaggedObject::HCLASS_OFFSET == 0);
    // AssemblerAarch64::Stlr currently only encodes the W-register form.
    // HClass headers are full tagged words, so emit STLR Xt, [Xn] directly.
    ASSERT(src.IsX());
    ASSERT(object.IsX());
    constexpr uint32_t STLR_X = 0xc89ffc00;
    constexpr uint32_t RN_SHIFT = 5;
    assembler_.EmitU32(STLR_X | (object.GetId() << RN_SHIFT) | src.GetId());
}

void ArkSteedAssembler::StoreFloat64Field(ArkSteedDoubleRegister src, ArkSteedRegister base, int32_t offset)
{
    StoreFloat64(aarch64::MemoryOperand(base, offset, aarch64::AddrMode::OFFSET), src);
}

void ArkSteedAssembler::StoreFloat32Field(ArkSteedDoubleRegister src, ArkSteedDoubleRegister scratch,
                                          ArkSteedRegister base, int32_t offset)
{
    auto floatScratch = aarch64::VRegister::Create(scratch.Code(), 32);
    assembler_.FcvtFloat32(floatScratch, src);
    assembler_.StrFloat32(floatScratch, aarch64::MemoryOperand(base, offset, aarch64::AddrMode::OFFSET));
}

void ArkSteedAssembler::LoadActualArgc(ArkSteedRegister dst)
{
    // 2: argc is stored at fp + 2 * FRAME_SLOT_SIZE
    aarch64::MemoryOperand operand(kFramePointerRegister, 2 * FRAME_SLOT_SIZE, aarch64::AddrMode::OFFSET);
    assembler_.Ldr(dst, operand);
}

void ArkSteedAssembler::LoadActualArgument(ArkSteedRegister dst, ArkSteedRegister index,
                                           ArkSteedRegister scratch)
{
    // 3: skip saved fp, return address, and argc; then skip mandatory JS-function arguments.
    constexpr int32_t FIRST_USER_ARG_SLOT = 3 + NUM_MANDATORY_JSFUNC_ARGS;
    constexpr uint8_t TAGGED_SIZE_SHIFT = static_cast<uint8_t>(TAGGED_TYPE_SIZE_LOG);
    static_assert(JSTaggedValue::TaggedTypeSize() == (1U << TAGGED_SIZE_SHIFT));
    assembler_.Add(scratch, kFramePointerRegister,
                   aarch64::Operand(index, aarch64::UXTW, TAGGED_SIZE_SHIFT));
    LoadField(dst, scratch, FIRST_USER_ARG_SLOT * FRAME_SLOT_SIZE);
}

void ArkSteedAssembler::LoadFloat64(ArkSteedDoubleRegister dst, MemoryOperand srcOp)
{
    if (FitsScaledImmediateOffset(srcOp, true)) {
        assembler_.Ldr(dst, srcOp);
        return;
    }
    assembler_.Ldr(dst, MaterializeAddress(srcOp));
}

void ArkSteedAssembler::StoreFloat64(MemoryOperand dstOp, ArkSteedDoubleRegister src)
{
    if (FitsScaledImmediateOffset(dstOp, true)) {
        assembler_.Str(src, dstOp);
        return;
    }
    assembler_.Str(src, MaterializeAddress(dstOp));
}

void ArkSteedAssembler::StoreFloat64Constant(MemoryOperand dstOp, double immediate, ArkSteedRegister scratchGPR,
                                             ArkSteedDoubleRegister scratchFPR)
{
    uint64_t bits = base::bit_cast<uint64_t>(immediate);
    if (bits == 0) {
        assembler_.Fmov(scratchFPR, aarch64::xzr);
        StoreFloat64(dstOp, scratchFPR);
        return;
    }
    if (assembler_.TryFmov(scratchFPR, immediate)) {
        StoreFloat64(dstOp, scratchFPR);
        return;
    }
    Move(scratchGPR, bits);
    MoveRepr(MachineRepresentation::Word64, dstOp, scratchGPR);
}

void ArkSteedAssembler::NormalizeEagerDeoptOverflowLink()
{
    static_assert(ARKSTEED_EAGER_DEOPT_FIXED_EXIT_LINK_SIZE == 0U);
    static_assert(ARKSTEED_EAGER_DEOPT_VENEER_FRAME_SIZE == 2U * FRAME_SLOT_SIZE);
    assembler_.Add(aarch64::sp, aarch64::sp,
                   aarch64::Operand(aarch64::Immediate(ARKSTEED_EAGER_DEOPT_VENEER_FRAME_SIZE)));
}

void ArkSteedAssembler::CallArkSteedDeoptimizationEntry()
{
    ASSERT(temporaryRegisterScope_ == nullptr);

    // The entry ABI expects glue in ARKSTEED_EAGER_DEOPT_ENTRY_GLUE_REGISTER
    // (x17); forward it from the glue root register (x28).
    ArkSteedRegister glue = ARKSTEED_EAGER_DEOPT_ENTRY_GLUE_REGISTER;
    ArkSteedRegister target = ARKSTEED_EAGER_DEOPT_ENTRY_TARGET_REGISTER;
    Move(glue, GLUE_ROOT_REGISTER);

    LoadStubEntry(target, RTSTUB_ID(ArkSteedDeoptimizationEntry));
    Push(aarch64::lr);
    Call(target);
}

void ArkSteedAssembler::ConvertInt32ToDouble(ArkSteedDoubleRegister dst, ArkSteedRegister src)
{
    assembler_.Scvtf(dst, src.W());
}

// =============================================================================
// Arithmetic Operations
// =============================================================================

void ArkSteedAssembler::Add(ArkSteedRegister dst, ArkSteedRegister src)
{
    ArkSteedRegister arm64Dst = dst;
    assembler_.Add(arm64Dst, arm64Dst, aarch64::Operand(src));
}

void ArkSteedAssembler::Add(ArkSteedRegister dst, ArkSteedRegister left, ArkSteedRegister right)
{
    assembler_.Add(dst, left, aarch64::Operand(right));
}

void ArkSteedAssembler::EncodeTaggedDouble(ArkSteedRegister dst, ArkSteedDoubleRegister input)
{
    Label pureDouble;
    Label done;

    Move(dst, input);
    static_assert(JSTaggedValue::TAG_INT == 0ULL - JSTaggedValue::DOUBLE_ENCODE_OFFSET);
    TemporaryRegisterScope scope(this);
    auto offset = scope.AcquireScratch();
    Move(offset, static_cast<uint64_t>(JSTaggedValue::DOUBLE_ENCODE_OFFSET));
    TestDoubleEncodeOverflow(dst, offset);
    JumpIf(Condition::BELOW, &pureDouble);
    LoadTaggedValue(dst, JSTaggedValue(base::NAN_VALUE).GetRawData());
    Jump(&done);
    Bind(&pureDouble);
    Add(dst, offset);
    Bind(&done);
}

void ArkSteedAssembler::DecodeTaggedDouble(ArkSteedRegister dst, ArkSteedRegister value,
                                           ArkSteedRegister tagMarkScratch)
{
    static_assert(JSTaggedValue::TAG_MARK == 0ULL - JSTaggedValue::DOUBLE_ENCODE_OFFSET);
    Add(dst, value, tagMarkScratch);
}

void ArkSteedAssembler::TestDoubleEncodeOverflow(ArkSteedRegister raw, ArkSteedRegister encodeOffset)
{
    assembler_.Adds(aarch64::xzr, raw, aarch64::Operand(encodeOffset, aarch64::LSL, 1U));
}

void ArkSteedAssembler::Add(ArkSteedRegister dst, int32_t immediate)
{
    int64_t value = immediate;
    if (value >= 0 && FitsAddSubImmediate(static_cast<uint64_t>(value))) {
        EmitAddSubImmediate(dst, dst, static_cast<uint64_t>(value), AddSubImmediateOp::ADD);
        return;
    }
    if (value < 0 && FitsAddSubImmediate(static_cast<uint64_t>(-value))) {
        EmitAddSubImmediate(dst, dst, static_cast<uint64_t>(-value), AddSubImmediateOp::SUB);
        return;
    }
    TemporaryRegisterScope scope(this);
    ArkSteedRegister scratch = scope.AcquireScratch();
    Move(scratch, value);
    if (dst.IsSp()) {
        assembler_.Add(dst, dst, aarch64::Operand(scratch, aarch64::UXTX, 0));
    } else {
        assembler_.Add(dst, dst, aarch64::Operand(scratch));
    }
}

void ArkSteedAssembler::Add(ArkSteedRegister dst, int64_t immediate)
{
    TemporaryRegisterScope scope(this);
    ArkSteedRegister scratch = scope.AcquireScratch();
    Move(scratch, immediate);
    Add(dst, scratch);
}

void ArkSteedAssembler::Sub(ArkSteedRegister dst, ArkSteedRegister src)
{
    ArkSteedRegister arm64Dst = dst;
    assembler_.Sub(arm64Dst, arm64Dst, aarch64::Operand(src));
}

void ArkSteedAssembler::Sub(ArkSteedRegister dst, int32_t immediate)
{
    int64_t value = immediate;
    if (value >= 0 && FitsAddSubImmediate(static_cast<uint64_t>(value))) {
        EmitAddSubImmediate(dst, dst, static_cast<uint64_t>(value), AddSubImmediateOp::SUB);
        return;
    }
    if (value < 0 && FitsAddSubImmediate(static_cast<uint64_t>(-value))) {
        EmitAddSubImmediate(dst, dst, static_cast<uint64_t>(-value), AddSubImmediateOp::ADD);
        return;
    }
    TemporaryRegisterScope scope(this);
    ArkSteedRegister scratch = scope.AcquireScratch();
    Move(scratch, value);
    if (dst.IsSp()) {
        assembler_.Sub(dst, dst, aarch64::Operand(scratch, aarch64::UXTX, 0));
    } else {
        assembler_.Sub(dst, dst, aarch64::Operand(scratch));
    }
}

void ArkSteedAssembler::SignExtendInt32ToInt64(ArkSteedRegister dst, ArkSteedRegister src)
{
    // ADD (extended register) interprets register code 31 in Rn as SP, not
    // XZR. SBFM Xd, Xn, #0, #31 is the architectural SXTW alias and does not
    // accidentally add the current stack pointer to the Int32 payload.
    assembler_.Sbfm(dst, src, 0, 31);  // 0, 31: SXTW bit range.
}

bool ArkSteedAssembler::CanUseAddSubImmediate(int32_t value)
{
    return CanUseCompareImmediate(value);
}

void ArkSteedAssembler::Int32Add(ArkSteedRegister dst, ArkSteedRegister src, int32_t immediate, bool setFlags)
{
    ASSERT(CanUseAddSubImmediate(immediate));
    uint64_t bits = static_cast<uint64_t>(static_cast<int64_t>(immediate));
    uint64_t magnitude = immediate < 0 ? 0U - bits : bits;
    AddSubImmediateOp operation;
    if (immediate < 0) {
        operation = setFlags ? AddSubImmediateOp::SUBS : AddSubImmediateOp::SUB;
    } else {
        operation = setFlags ? AddSubImmediateOp::ADDS : AddSubImmediateOp::ADD;
    }
    EmitAddSubImmediate(dst.W(), src.W(), magnitude, operation);
}

void ArkSteedAssembler::Int32Sub(ArkSteedRegister dst, ArkSteedRegister src, int32_t immediate, bool setFlags)
{
    ASSERT(CanUseAddSubImmediate(immediate));
    uint64_t bits = static_cast<uint64_t>(static_cast<int64_t>(immediate));
    uint64_t magnitude = immediate < 0 ? 0U - bits : bits;
    AddSubImmediateOp operation;
    if (immediate < 0) {
        operation = setFlags ? AddSubImmediateOp::ADDS : AddSubImmediateOp::ADD;
    } else {
        operation = setFlags ? AddSubImmediateOp::SUBS : AddSubImmediateOp::SUB;
    }
    EmitAddSubImmediate(dst.W(), src.W(), magnitude, operation);
}

void ArkSteedAssembler::Int32Add(ArkSteedRegister dst, ArkSteedRegister left, ArkSteedRegister right)
{
    assembler_.Adds(dst.W(), left.W(), aarch64::Operand(right.W()));
}

void ArkSteedAssembler::Int32Add(ArkSteedRegister dst, ArkSteedRegister left, int32_t immediate)
{
    int64_t value = immediate;
    if (value >= 0 && FitsAddSubImmediate(static_cast<uint64_t>(value))) {
        EmitAddSubImmediate(dst.W(), left.W(), static_cast<uint64_t>(value), AddSubImmediateOp::ADD);
        return;
    }
    if (value < 0 && FitsAddSubImmediate(static_cast<uint64_t>(-value))) {
        EmitAddSubImmediate(dst.W(), left.W(), static_cast<uint64_t>(-value), AddSubImmediateOp::SUB);
        return;
    }
    TemporaryRegisterScope scope(this);
    ArkSteedRegister scratch = scope.AcquireScratch();
    Move(scratch, value);
    Int32Add(dst, left, scratch);
}

void ArkSteedAssembler::Int32Sub(ArkSteedRegister dst, ArkSteedRegister left, ArkSteedRegister right)
{
    assembler_.Subs(dst.W(), left.W(), aarch64::Operand(right.W()));
}

void ArkSteedAssembler::Int32Sub(ArkSteedRegister dst, ArkSteedRegister left, int32_t immediate)
{
    int64_t value = immediate;
    if (value >= 0 && FitsAddSubImmediate(static_cast<uint64_t>(value))) {
        EmitAddSubImmediate(dst.W(), left.W(), static_cast<uint64_t>(value), AddSubImmediateOp::SUB);
        return;
    }
    if (value < 0 && FitsAddSubImmediate(static_cast<uint64_t>(-value))) {
        EmitAddSubImmediate(dst.W(), left.W(), static_cast<uint64_t>(-value), AddSubImmediateOp::ADD);
        return;
    }
    TemporaryRegisterScope scope(this);
    ArkSteedRegister scratch = scope.AcquireScratch();
    Move(scratch, value);
    Int32Sub(dst, left, scratch);
}

void ArkSteedAssembler::Int32Mul(ArkSteedRegister dst, ArkSteedRegister src)
{
    Int32Mul(dst, dst, src);
}

void ArkSteedAssembler::Int32Mul(ArkSteedRegister dst, ArkSteedRegister left, ArkSteedRegister right)
{
    assembler_.Mul(dst.W(), left.W(), right.W());
}

void ArkSteedAssembler::Int32Mul(ArkSteedRegister dst, int32_t immediate)
{
    TemporaryRegisterScope scope(this);
    ArkSteedRegister scratch = scope.AcquireScratch();
    Move(scratch, immediate);
    Int32Mul(dst, scratch);
}

void ArkSteedAssembler::Int32Mul(ArkSteedRegister dst, ArkSteedRegister left, int32_t immediate)
{
    TemporaryRegisterScope scope(this);
    ArkSteedRegister scratch = scope.AcquireScratch();
    Move(scratch, immediate);
    Int32Mul(dst, left, scratch);
}

void ArkSteedAssembler::Int32MulWide(ArkSteedRegister dst, ArkSteedRegister left, ArkSteedRegister right)
{
    assembler_.Smull(dst, left.W(), right.W());
}

void ArkSteedAssembler::Int32MulHigh(ArkSteedRegister dst, ArkSteedRegister left, ArkSteedRegister right)
{
    Int32MulWide(dst, left, right);
    assembler_.Asr(dst, dst, 32U);  // 32: signed high half of the 64-bit product.
}

void ArkSteedAssembler::Int32MulWithOverflow(ArkSteedRegister dst, ArkSteedRegister left, ArkSteedRegister right,
                                             Label *overflowExit, Label *negativeZeroExit)
{
    ASSERT(dst != left && dst != right);
    Label success;
    Int32MulWide(dst, left, right);
    // Compare the full product against the sign extension of its low word.
    assembler_.Cmp(dst, aarch64::Operand(dst.W(), aarch64::SXTW));
    JumpIf(Condition::NOT_EQUAL, overflowExit);
    JumpIfNotZero(dst.W(), &success);
    TemporaryRegisterScope scope(this);
    ArkSteedRegister signs = scope.AcquireScratch();
    Int32Or(signs, left, right);
    CompareInt32(signs, 0);
    JumpIf(Condition::LESS_THAN, negativeZeroExit);
    Bind(&success);
}

std::optional<int32_t> ArkSteedAssembler::TryGetInt32PowerOfTwoMask(std::optional<int32_t> divisor)
{
    if (divisor && *divisor > 1 && (*divisor & (*divisor - 1)) == 0) {
        return *divisor - 1;
    }
    return std::nullopt;
}

void ArkSteedAssembler::Int32DivByConstWithCheck(ArkSteedRegister dst, ArkSteedRegister dividend, int32_t divisor,
                                                 int32_t magic, uint32_t shift, Label *negativeZeroExit,
                                                 Label *notIntExit)
{
    if (auto mask = TryGetInt32PowerOfTwoMask(divisor)) {
        TestInt32Mask(dividend, static_cast<uint32_t>(*mask));
        JumpIf(Condition::NOT_EQUAL, notIntExit);
        assembler_.Asr(dst.W(), dividend.W(),
                       static_cast<uint32_t>(__builtin_ctz(static_cast<uint32_t>(divisor))));
        return;
    }

    ASSERT(dst != dividend);
    TemporaryRegisterScope scope(this);
    ArkSteedRegister work = scope.Acquire();
    if (divisor < 0) {
        JumpIfZero(dividend.W(), negativeZeroExit);
    }

    Move(work, magic);
    Int32MulHigh(work, dividend, work);
    if (divisor > 0 && magic < 0) {
        Int32Add(work, work, dividend);
    } else if (divisor < 0 && magic > 0) {
        Int32Sub(work, work, dividend);
    }
    if (shift != 0) {
        Int32ShiftRightArithmetic(work, shift);
    }

    assembler_.Add(dst.W(), work.W(),
                   aarch64::Operand(work.W(), aarch64::LSR, std::numeric_limits<int32_t>::digits));

    Move(work, divisor);
    Int32Mul(work, dst);
    CompareInt32(work, dividend);
    JumpIf(Condition::NOT_EQUAL, notIntExit);
}

void ArkSteedAssembler::Int32Div(ArkSteedRegister dst, ArkSteedRegister dividend, ArkSteedRegister divisor)
{
    assembler_.Sdiv(dst.W(), dividend.W(), divisor.W());
}

void ArkSteedAssembler::CheckedInt32Div(ArkSteedRegister dst, ArkSteedRegister left, ArkSteedRegister right,
                                        Label *divideZeroExit, Label *overflowExit, Label *notIntExit,
                                        Label *negativeZeroExit)
{
    Label divisorReady;
    Label done;

    JumpIfZero(right.W(), divideZeroExit);
    CompareInt32(left, std::numeric_limits<int32_t>::min());
    JumpIf(Condition::NOT_EQUAL, &divisorReady);
    CompareInt32(right, -1);
    JumpIf(Condition::EQUAL, overflowExit);
    Bind(&divisorReady);
    TemporaryRegisterScope scope(this);
    ArkSteedRegister remainder = scope.AcquireScratch();
    Int32DivAndRemainder(dst, remainder, left, right);
    JumpIfNotZero(remainder.W(), notIntExit);
    JumpIfNotZero(dst.W(), &done);
    CompareInt32(right, 0);
    JumpIf(Condition::LESS_THAN, negativeZeroExit);
    Bind(&done);
}

void ArkSteedAssembler::Int32DivAndRemainder(ArkSteedRegister quotient, ArkSteedRegister remainder,
                                             ArkSteedRegister dividend, ArkSteedRegister divisor)
{
    Int32Div(quotient, dividend, divisor);
    assembler_.Msub(remainder.W(), quotient.W(), divisor.W(), dividend.W());
}

void ArkSteedAssembler::PositiveInt32Mod(ArkSteedRegister dst, ArkSteedRegister dividend, ArkSteedRegister divisor)
{
    TemporaryRegisterScope scope(this);
    ArkSteedRegister quotient = scope.AcquireScratch();
    assembler_.Sdiv(quotient.W(), dividend.W(), divisor.W());
    assembler_.Msub(dst.W(), quotient.W(), divisor.W(), dividend.W());
}

void ArkSteedAssembler::CheckedInt32Mod(ArkSteedRegister dst, ArkSteedRegister left,
                                        std::optional<ArkSteedRegister> right, std::optional<int32_t> mask,
                                        Label *divideZeroExit, Label *overflowExit, Label *negativeZeroExit)
{
    ASSERT(right.has_value() != mask.has_value());
    if (mask) {
        Label leftNeg;
        Label done;
        CompareInt32(left, 0);
        JumpIf(Condition::LESS_THAN, &leftNeg);
        Int32And(dst, left, *mask);
        Jump(&done);

        Bind(&leftNeg);
        Int32Neg(dst, left);
        Int32And(dst, *mask);
        Int32Neg(dst, dst);
        JumpIfZero(dst.W(), negativeZeroExit);
        Bind(&done);
        return;
    }

    Label divisorReady;
    Label done;
    JumpIfZero(right->W(), divideZeroExit);
    CompareInt32(left, std::numeric_limits<int32_t>::min());
    JumpIf(Condition::NOT_EQUAL, &divisorReady);
    CompareInt32(*right, -1);
    JumpIf(Condition::EQUAL, overflowExit);
    Bind(&divisorReady);

    // Test the dividend sign before writing dst (which can alias left); SDIV and MSUB preserve NZCV.
    CompareInt32(left, 0);
    PositiveInt32Mod(dst, left, *right);
    JumpIf(Condition::GREATER_THAN_OR_EQUAL, &done);
    JumpIfZero(dst.W(), negativeZeroExit);
    Bind(&done);
}

void ArkSteedAssembler::Int32UnaryWithOverflow(
    ArkSteedRegister dst, ArkSteedRegister input,
    void (ArkSteedAssembler::*operation)(ArkSteedRegister, ArkSteedRegister), bool checkZero, Label *deoptExit)
{
    if (checkZero) {
        JumpIfZero(input.W(), deoptExit);
    }
    (this->*operation)(dst, input);
    JumpIf(Condition::OVERFLOWS, deoptExit);
}

void ArkSteedAssembler::Int32ToFloat64(ArkSteedDoubleRegister dst, ArkSteedRegister src)
{
    assembler_.Scvtf(dst, src.W());
}

void ArkSteedAssembler::Float64Add(ArkSteedDoubleRegister dst, ArkSteedDoubleRegister src)
{
    Float64Add(dst, dst, src);
}

void ArkSteedAssembler::Float64Add(ArkSteedDoubleRegister dst, ArkSteedDoubleRegister left,
                                   ArkSteedDoubleRegister right)
{
    assembler_.Fadd(dst, left, right);
}

void ArkSteedAssembler::Uint32ToFloat64(ArkSteedDoubleRegister dst, ArkSteedRegister src)
{
    assembler_.Ucvtf(dst, src.W());
}

void ArkSteedAssembler::Float64Sub(ArkSteedDoubleRegister dst, ArkSteedDoubleRegister src)
{
    Float64Sub(dst, dst, src);
}

void ArkSteedAssembler::Float64Sub(ArkSteedDoubleRegister dst, ArkSteedDoubleRegister left,
                                   ArkSteedDoubleRegister right)
{
    assembler_.Fsub(dst, left, right);
}

void ArkSteedAssembler::Float64Mul(ArkSteedDoubleRegister dst, ArkSteedDoubleRegister src)
{
    Float64Mul(dst, dst, src);
}

void ArkSteedAssembler::Float64Mul(ArkSteedDoubleRegister dst, ArkSteedDoubleRegister left,
                                   ArkSteedDoubleRegister right)
{
    assembler_.Fmul(dst, left, right);
}

void ArkSteedAssembler::Float64Div(ArkSteedDoubleRegister dst, ArkSteedDoubleRegister src)
{
    Float64Div(dst, dst, src);
}

void ArkSteedAssembler::Float64Div(ArkSteedDoubleRegister dst, ArkSteedDoubleRegister left,
                                   ArkSteedDoubleRegister right)
{
    assembler_.Fdiv(dst, left, right);
}

void ArkSteedAssembler::Float64Neg(ArkSteedDoubleRegister dst, ArkSteedDoubleRegister src)
{
    assembler_.Fneg(dst, src);
}

void ArkSteedAssembler::CompareFloat64(ArkSteedDoubleRegister left, ArkSteedDoubleRegister right)
{
    assembler_.Fcmp(left, right);
}

void ArkSteedAssembler::TruncateFloat64ToInt32(ArkSteedRegister dst, ArkSteedDoubleRegister src)
{
    // Bounded conversion: the caller proves the input is in [-2^63, 2^63).
    assembler_.Fcvtzs(dst, src);
}

bool ArkSteedAssembler::TryConvertFloat64ToInt32([[maybe_unused]] ArkSteedRegister dst,
                                                 [[maybe_unused]] ArkSteedDoubleRegister src)
{
#if defined(__aarch64__) && (defined(__linux__) || defined(PANDA_TARGET_OHOS)) && defined(HWCAP_JSCVT)
    static const bool hasJscvt = (getauxval(AT_HWCAP) & HWCAP_JSCVT) != 0;
    if (hasJscvt) {
        assembler_.Fjcvtzs(dst.W(), src);
        return true;
    }
#endif
    return false;
}

void ArkSteedAssembler::TruncateFloat64ToInt32OrSlow(ArkSteedRegister dst, ArkSteedDoubleRegister src, Label *slow)
{
    assembler_.Fcvtzs(dst, src);
    Compare(dst, 1);
    assembler_.Ccmn(dst, 1, 1, aarch64::VC);
    JumpIf(Condition::OVERFLOWS, slow);
}

void ArkSteedAssembler::RoundFloat64ToNearestInt32(ArkSteedRegister dst, ArkSteedDoubleRegister src,
                                                   ArkSteedDoubleRegister scratch)
{
    assembler_.Frintn(scratch, src);
    assembler_.Fcvtzs(dst.W(), scratch);
}

// =============================================================================
// Bitwise Operations
// =============================================================================

void ArkSteedAssembler::Word64And(ArkSteedRegister dst, ArkSteedRegister src)
{
    assembler_.And(dst, dst, aarch64::Operand(src));
}

bool ArkSteedAssembler::CanUseLogicalImmediate(uint64_t bits)
{
    return aarch64::LogicalImmediate::Create(bits, aarch64::X_REG_SIZE).IsValid();
}

void ArkSteedAssembler::And(ArkSteedRegister dst, ArkSteedRegister left, ArkSteedRegister right)
{
    assembler_.And(dst, left, aarch64::Operand(right));
}

void ArkSteedAssembler::And(ArkSteedRegister dst, ArkSteedRegister src, int64_t immediate)
{
    ASSERT(CanUseLogicalImmediate(static_cast<uint64_t>(immediate)));
    auto logical = aarch64::LogicalImmediate::Create(static_cast<uint64_t>(immediate), aarch64::X_REG_SIZE);
    assembler_.And(dst, src, logical);
}

void ArkSteedAssembler::Or(ArkSteedRegister dst, ArkSteedRegister left, ArkSteedRegister right)
{
    assembler_.Orr(dst, left, aarch64::Operand(right));
}

void ArkSteedAssembler::Or(ArkSteedRegister dst, ArkSteedRegister src, int64_t immediate)
{
    ASSERT(CanUseLogicalImmediate(static_cast<uint64_t>(immediate)));
    auto logical = aarch64::LogicalImmediate::Create(static_cast<uint64_t>(immediate), aarch64::X_REG_SIZE);
    assembler_.Orr(dst, src, logical);
}

void ArkSteedAssembler::Or(ArkSteedRegister dst, int32_t immediate)
{
    Or(dst, static_cast<int64_t>(immediate));
}

void ArkSteedAssembler::Or(ArkSteedRegister dst, int64_t immediate)
{
    ArkSteedRegister arm64Dst = dst;
    auto imm = aarch64::LogicalImmediate::Create(static_cast<uint64_t>(immediate),
                                                 arm64Dst.IsW() ? aarch64::W_REG_SIZE : aarch64::X_REG_SIZE);
    if (imm.IsValid()) {
        assembler_.Orr(arm64Dst, arm64Dst, imm);
        return;
    }

    TemporaryRegisterScope scope(this);
    ArkSteedRegister scratch = scope.AcquireScratch();
    Move(scratch, immediate);
    assembler_.Orr(arm64Dst, arm64Dst, aarch64::Operand(scratch));
}

void ArkSteedAssembler::Or(ArkSteedRegister dst, ArkSteedRegister src)
{
    ArkSteedRegister arm64Dst = dst;
    assembler_.Orr(arm64Dst, arm64Dst, aarch64::Operand(src));
}

void ArkSteedAssembler::And(ArkSteedRegister dst, int32_t immediate)
{
    And(dst, static_cast<int64_t>(immediate));
}

void ArkSteedAssembler::And(ArkSteedRegister dst, int64_t immediate)
{
    ArkSteedRegister arm64Dst = dst;
    auto imm = aarch64::LogicalImmediate::Create(static_cast<uint64_t>(immediate),
                                                 arm64Dst.IsW() ? aarch64::W_REG_SIZE : aarch64::X_REG_SIZE);
    if (imm.IsValid()) {
        assembler_.And(arm64Dst, arm64Dst, imm);
        return;
    }

    TemporaryRegisterScope scope(this);
    ArkSteedRegister scratch = scope.AcquireScratch();
    Move(scratch, immediate);
    assembler_.And(arm64Dst, arm64Dst, aarch64::Operand(scratch));
}

void ArkSteedAssembler::And(ArkSteedRegister dst, ArkSteedRegister src)
{
    ArkSteedRegister arm64Dst = dst;
    assembler_.And(arm64Dst, arm64Dst, aarch64::Operand(src));
}

void ArkSteedAssembler::AndSetZeroCondition(ArkSteedRegister dst, ArkSteedRegister src)
{
    assembler_.Ands(dst, dst, aarch64::Operand(src));
}

void ArkSteedAssembler::AndSetZeroCondition(ArkSteedRegister dst, int32_t immediate)
{
    AndSetZeroCondition(dst, static_cast<int64_t>(immediate));
}

void ArkSteedAssembler::TestInt32Mask(ArkSteedRegister value, uint32_t mask)
{
    auto immediate = aarch64::LogicalImmediate::Create(mask, aarch64::W_REG_SIZE);
    ASSERT(immediate.IsValid());
    assembler_.Ands(aarch64::wzr, value.W(), immediate);
}

void ArkSteedAssembler::AndSetZeroCondition(ArkSteedRegister dst, int64_t immediate)
{
    auto encoded = aarch64::LogicalImmediate::Create(static_cast<uint64_t>(immediate),
                                                     dst.IsW() ? aarch64::W_REG_SIZE : aarch64::X_REG_SIZE);
    if (encoded.IsValid()) {
        assembler_.Ands(dst, dst, encoded);
        return;
    }
    TemporaryRegisterScope scope(this);
    auto scratch = scope.AcquireScratch();
    Move(scratch, immediate);
    AndSetZeroCondition(dst, scratch);
}

void ArkSteedAssembler::Lsr(ArkSteedRegister dst, uint32_t shift)
{
    ArkSteedRegister arm64Dst = dst;
    assembler_.Lsr(arm64Dst, arm64Dst, shift);
}

void ArkSteedAssembler::ShiftRightLogical(ArkSteedRegister dst, uint32_t shift)
{
    assembler_.Lsr(dst, dst, shift);
}

void ArkSteedAssembler::ShiftLeft(ArkSteedRegister dst, uint32_t shift)
{
    assembler_.Orr(dst, aarch64::xzr, aarch64::Operand(dst, aarch64::Shift::LSL, shift));
}

void ArkSteedAssembler::ShiftLeft(ArkSteedRegister dst, ArkSteedRegister src, uint32_t shift)
{
    assembler_.Orr(dst, aarch64::xzr, aarch64::Operand(src, aarch64::Shift::LSL, shift));
}

void ArkSteedAssembler::ShiftRightLogical32(ArkSteedRegister dst, uint32_t shift)
{
    assembler_.Lsr(dst.W(), dst.W(), shift);
}

void ArkSteedAssembler::MoveBitMask32(ArkSteedRegister dst, ArkSteedRegister bitIndex)
{
    Move(dst, 1);
    assembler_.Lsl(dst.W(), dst.W(), bitIndex.W());
}

void ArkSteedAssembler::Int32Neg(ArkSteedRegister dst, ArkSteedRegister src)
{
    assembler_.Subs(dst.W(), aarch64::wzr, aarch64::Operand(src.W()));
}

void ArkSteedAssembler::Int32Inc(ArkSteedRegister dst, ArkSteedRegister src)
{
    assembler_.Adds(dst.W(), src.W(), aarch64::Operand(aarch64::Immediate(1)));
}

void ArkSteedAssembler::Int32Dec(ArkSteedRegister dst, ArkSteedRegister src)
{
    assembler_.Subs(dst.W(), src.W(), aarch64::Operand(aarch64::Immediate(1)));
}

void ArkSteedAssembler::Int32BNot(ArkSteedRegister dst)
{
    assembler_.Mvn(dst.W(), dst.W());
}

void ArkSteedAssembler::Int32And(ArkSteedRegister dst, ArkSteedRegister src)
{
    assembler_.And(dst.W(), dst.W(), aarch64::Operand(src.W()));
}

void ArkSteedAssembler::Int32And(ArkSteedRegister dst, int32_t immediate)
{
    auto imm = aarch64::LogicalImmediate::Create(static_cast<uint32_t>(immediate), aarch64::W_REG_SIZE);
    if (imm.IsValid()) {
        assembler_.And(dst.W(), dst.W(), imm);
        return;
    }
    TemporaryRegisterScope scope(this);
    ArkSteedRegister scratch = scope.AcquireScratch();
    Move(scratch, immediate);
    assembler_.And(dst.W(), dst.W(), aarch64::Operand(scratch.W()));
}

void ArkSteedAssembler::Int32Or(ArkSteedRegister dst, ArkSteedRegister src)
{
    assembler_.Orr(dst.W(), dst.W(), aarch64::Operand(src.W()));
}

void ArkSteedAssembler::Int32Or(ArkSteedRegister dst, int32_t immediate)
{
    auto imm = aarch64::LogicalImmediate::Create(static_cast<uint32_t>(immediate), aarch64::W_REG_SIZE);
    if (imm.IsValid()) {
        assembler_.Orr(dst.W(), dst.W(), imm);
        return;
    }
    TemporaryRegisterScope scope(this);
    ArkSteedRegister scratch = scope.AcquireScratch();
    Move(scratch, immediate);
    assembler_.Orr(dst.W(), dst.W(), aarch64::Operand(scratch.W()));
}

void ArkSteedAssembler::Int32Xor(ArkSteedRegister dst, ArkSteedRegister src)
{
    assembler_.Eor(dst.W(), dst.W(), aarch64::Operand(src.W()));
}

void ArkSteedAssembler::Int32Xor(ArkSteedRegister dst, int32_t immediate)
{
    auto imm = aarch64::LogicalImmediate::Create(static_cast<uint32_t>(immediate), aarch64::W_REG_SIZE);
    if (imm.IsValid()) {
        assembler_.Eor(dst.W(), dst.W(), imm);
        return;
    }
    TemporaryRegisterScope scope(this);
    ArkSteedRegister scratch = scope.AcquireScratch();
    Move(scratch, immediate);
    assembler_.Eor(dst.W(), dst.W(), aarch64::Operand(scratch.W()));
}

void ArkSteedAssembler::Int32ShiftLeft(ArkSteedRegister dst, uint32_t shift)
{
    assembler_.Orr(dst.W(), aarch64::wzr, aarch64::Operand(dst.W(), aarch64::Shift::LSL, shift));
}

#define DEFINE_ARM64_I32_BITWISE(Name, Instruction)                                                     \
    void ArkSteedAssembler::Int32##Name(ArkSteedRegister dst, ArkSteedRegister left, ArkSteedRegister right) \
    {                                                                                                   \
        assembler_.Instruction(dst.W(), left.W(), aarch64::Operand(right.W()));                            \
    }                                                                                                   \
    void ArkSteedAssembler::Int32##Name(ArkSteedRegister dst, ArkSteedRegister left, int32_t immediate)      \
    {                                                                                                   \
        auto imm = aarch64::LogicalImmediate::Create(static_cast<uint32_t>(immediate), aarch64::W_REG_SIZE); \
        if (imm.IsValid()) {                                                                             \
            assembler_.Instruction(dst.W(), left.W(), imm);                                               \
            return;                                                                                      \
        }                                                                                                \
        TemporaryRegisterScope scope(this);                                                               \
        ArkSteedRegister scratch = scope.AcquireScratch();                                                 \
        Move(scratch, immediate);                                                                         \
        assembler_.Instruction(dst.W(), left.W(), aarch64::Operand(scratch.W()));                           \
    }

DEFINE_ARM64_I32_BITWISE(And, And)
DEFINE_ARM64_I32_BITWISE(Or, Orr)
DEFINE_ARM64_I32_BITWISE(Xor, Eor)
#undef DEFINE_ARM64_I32_BITWISE

void ArkSteedAssembler::Int32ShiftLeft(ArkSteedRegister dst, ArkSteedRegister left, uint32_t shift)
{
    if (shift == 0 && dst == left) {
        return;
    }
    assembler_.Orr(dst.W(), aarch64::wzr, aarch64::Operand(left.W(), aarch64::LSL, shift));
}

void ArkSteedAssembler::Int32ShiftLeftByRegister(ArkSteedRegister dst, ArkSteedRegister left, ArkSteedRegister shift)
{
    assembler_.Lsl(dst.W(), left.W(), shift.W());
}

void ArkSteedAssembler::Int32ShiftRightLogical(ArkSteedRegister dst, ArkSteedRegister left, uint32_t shift)
{
    if (shift == 0 && dst == left) {
        return;
    }
    assembler_.Lsr(dst.W(), left.W(), shift);
}

void ArkSteedAssembler::Int32ShiftRightLogicalByRegister(ArkSteedRegister dst, ArkSteedRegister left,
                                                         ArkSteedRegister shift)
{
    assembler_.Lsr(dst.W(), left.W(), shift.W());
}

void ArkSteedAssembler::Int32ShiftRightArithmetic(ArkSteedRegister dst, ArkSteedRegister left, uint32_t shift)
{
    if (shift == 0 && dst == left) {
        return;
    }
    assembler_.Asr(dst.W(), left.W(), shift);
}

void ArkSteedAssembler::Int32ShiftRightArithmeticByRegister(ArkSteedRegister dst, ArkSteedRegister left,
                                                            ArkSteedRegister shift)
{
    assembler_.Asr(dst.W(), left.W(), shift.W());
}

void ArkSteedAssembler::SetTaggedBoolean(ArkSteedRegister dst, Condition condition)
{
    static_assert(JSTaggedValue::VALUE_TRUE == JSTaggedValue::VALUE_FALSE + 1);
    assembler_.Cset(dst.W(), ToPhysicalCondition(condition));
    assembler_.Add(dst.W(), dst.W(), aarch64::Operand(aarch64::Immediate(JSTaggedValue::VALUE_FALSE)));
}

void ArkSteedAssembler::Int32ShiftLeftByRegister(ArkSteedRegister dst, ArkSteedRegister shift)
{
    assembler_.Lsl(dst.W(), dst.W(), shift.W());
}

void ArkSteedAssembler::Int32ShiftRightLogical(ArkSteedRegister dst, uint32_t shift)
{
    assembler_.Lsr(dst.W(), dst.W(), shift);
}

void ArkSteedAssembler::Int32ShiftRightLogicalByRegister(ArkSteedRegister dst, ArkSteedRegister shift)
{
    assembler_.Lsr(dst.W(), dst.W(), shift.W());
}

void ArkSteedAssembler::Int32ShiftRightArithmetic(ArkSteedRegister dst, uint32_t shift)
{
    assembler_.Asr(dst.W(), dst.W(), shift);
}

void ArkSteedAssembler::Int32ShiftRightArithmeticByRegister(ArkSteedRegister dst, ArkSteedRegister shift)
{
    assembler_.Asr(dst.W(), dst.W(), shift.W());
}

// =============================================================================
// Comparison Operations
// =============================================================================

bool ArkSteedAssembler::CanUseCompareImmediate(int32_t value)
{
    return CanUseCompareImmediate(static_cast<int64_t>(value));
}

bool ArkSteedAssembler::CanUseCompareImmediate(int64_t value)
{
    uint64_t bits = static_cast<uint64_t>(value);
    uint64_t magnitude = value < 0 ? 0U - bits : bits;
    return FitsAddSubImmediate(magnitude);
}

bool ArkSteedAssembler::CanUseCompareImmediate(uint64_t bits)
{
    return CanUseCompareImmediate(base::bit_cast<int64_t>(bits));
}

void ArkSteedAssembler::Compare(ArkSteedRegister lhs, ArkSteedRegister rhs)
{
    assembler_.Cmp(lhs, aarch64::Operand(rhs));
}

void ArkSteedAssembler::CompareInt32(ArkSteedRegister lhs, ArkSteedRegister rhs)
{
    assembler_.Cmp(lhs.W(), aarch64::Operand(rhs.W()));
}

void ArkSteedAssembler::Compare(ArkSteedRegister lhs, int32_t immediate)
{
    Compare(lhs, static_cast<int64_t>(immediate));
}

void ArkSteedAssembler::Compare(ArkSteedRegister lhs, int64_t immediate)
{
    if (CanUseCompareImmediate(immediate)) {
        uint64_t bits = static_cast<uint64_t>(static_cast<int64_t>(immediate));
        uint64_t magnitude = immediate < 0 ? 0U - bits : bits;
        EmitAddSubImmediate(aarch64::xzr, lhs, magnitude,
                            immediate < 0 ? AddSubImmediateOp::ADDS : AddSubImmediateOp::SUBS);
        return;
    }
    TemporaryRegisterScope scope(this);
    ArkSteedRegister scratch = scope.AcquireScratch();
    Move(scratch, static_cast<int64_t>(immediate));
    assembler_.Cmp(lhs, aarch64::Operand(scratch));
}

void ArkSteedAssembler::CompareInt32(ArkSteedRegister lhs, int32_t immediate)
{
    if (CanUseCompareImmediate(immediate)) {
        uint64_t bits = static_cast<uint64_t>(static_cast<int64_t>(immediate));
        uint64_t magnitude = immediate < 0 ? 0U - bits : bits;
        EmitAddSubImmediate(aarch64::wzr, lhs.W(), magnitude,
                            immediate < 0 ? AddSubImmediateOp::ADDS : AddSubImmediateOp::SUBS);
        return;
    }
    TemporaryRegisterScope scope(this);
    ArkSteedRegister scratch = scope.AcquireScratch();
    Move(scratch, static_cast<int64_t>(immediate));
    assembler_.Cmp(lhs.W(), aarch64::Operand(scratch.W()));
}

void ArkSteedAssembler::CompareTaggedTrue(ArkSteedRegister value)
{
    Compare(value, static_cast<int64_t>(JSTaggedValue::VALUE_TRUE));
}

void ArkSteedAssembler::CompareField(ArkSteedRegister base, int32_t offset, ArkSteedRegister rhs)
{
    TemporaryRegisterScope scope(this);
    ArkSteedRegister scratch = scope.AcquireScratch();
    LoadField(scratch, base, offset);
    Compare(scratch, rhs);
}

// =============================================================================
// Control Flow
// =============================================================================

bool ArkSteedAssembler::IsVeneerBranchOrCall(uint32_t instruction)
{
    return (instruction & aarch64::BranchOpCode::BranchFMask) == aarch64::BranchOpCode::Branch;
}

bool ArkSteedAssembler::IsVeneerConditionOrCompareBranch(uint32_t instruction)
{
    return (instruction & aarch64::BranchOpCode::BranchCondFMask) == aarch64::BranchOpCode::BranchCond ||
           (instruction & aarch64::BranchOpCode::BranchCompareFMask) == aarch64::BranchOpCode::CBZ;
}

bool ArkSteedAssembler::IsVeneerTestBranch(uint32_t instruction)
{
    return (instruction & aarch64::BranchOpCode::BranchTestFMask) == aarch64::BranchOpCode::TBZ;
}

bool ArkSteedAssembler::IsVeneerBranchInRange(uint32_t instruction, int64_t displacement) const
{
    if ((displacement & (VENEER_INSTRUCTION_SIZE - 1U)) != 0) {
        return false;
    }
    if (IsVeneerBranchOrCall(instruction)) {
        constexpr int64_t minDisplacement = -(1LL << 27U);  // imm26 scaled by 4: -128 MiB.
        constexpr int64_t maxDisplacement = 1LL << 27U;  // imm26 scaled by 4: +128 MiB, exclusive.
        return displacement >= minDisplacement && displacement < maxDisplacement;
    }
    if (IsVeneerConditionOrCompareBranch(instruction)) {
        constexpr int64_t minDisplacement = -(1LL << 20U);  // imm19 scaled by 4: -1 MiB.
        constexpr int64_t maxDisplacement = 1LL << 20U;  // imm19 scaled by 4: +1 MiB, exclusive.
        return displacement >= minDisplacement && displacement < maxDisplacement;
    }
    if (IsVeneerTestBranch(instruction)) {
        constexpr int64_t minDisplacement = -(1LL << 15U);  // imm14 scaled by 4: -32 KiB.
        constexpr int64_t maxDisplacement = 1LL << 15U;  // imm14 scaled by 4: +32 KiB, exclusive.
        return displacement >= minDisplacement && displacement < maxDisplacement;
    }
    LOG_COMPILER(FATAL) << "Unsupported ARM64 veneer branch instruction: " << std::hex << instruction;
    UNREACHABLE();
}

uint32_t ArkSteedAssembler::GetVeneerBranchDeadline(uint32_t branchPc, uint32_t instruction) const
{
    uint64_t maxForwardDisplacement;
    if (IsVeneerBranchOrCall(instruction)) {
        maxForwardDisplacement = 1ULL << 27U;  // B/BL byte reach from signed imm26.
    } else if (IsVeneerConditionOrCompareBranch(instruction)) {
        maxForwardDisplacement = 1ULL << 20U;  // B.cond/CBZ/CBNZ byte reach from signed imm19.
    } else if (IsVeneerTestBranch(instruction)) {
        maxForwardDisplacement = 1ULL << 15U;  // TBZ/TBNZ byte reach from signed imm14.
    } else {
        LOG_COMPILER(FATAL) << "Unsupported ARM64 veneer branch instruction: " << std::hex << instruction;
        UNREACHABLE();
    }

    uint64_t poolEntryCount = static_cast<uint64_t>(veneerBranches_.size()) + 1U;  // 1: optional guard branch.
    uint64_t poolReserve = poolEntryCount * VENEER_INSTRUCTION_SIZE;
    uint64_t reservedDisplacement = VENEER_DISTANCE_MARGIN + poolReserve;
    if (maxForwardDisplacement <= reservedDisplacement) {
        return branchPc;
    }
    uint64_t deadline = static_cast<uint64_t>(branchPc) + maxForwardDisplacement - reservedDisplacement;
    return static_cast<uint32_t>(std::min(deadline, static_cast<uint64_t>(UINT32_MAX)));
}

uint64_t ArkSteedAssembler::GetPotentialVeneerPoolSize() const
{
    if (veneerBranches_.empty()) {
        return 0;
    }
    uint64_t entryCount = static_cast<uint64_t>(veneerBranches_.size()) + 1U;  // 1: optional guard branch.
    return entryCount * VENEER_INSTRUCTION_SIZE;
}

void ArkSteedAssembler::UpdateEmbeddedLiteralPoolCheck(uint32_t loadOffset, size_t literalIndex)
{
    // A literal's position is the pool prefix followed by its index in the current pool.
    uint64_t literalOffsetInPool = static_cast<uint64_t>(literalIndex) * EMBEDDED_LITERAL_SIZE;
    uint64_t reserve = EMBEDDED_LITERAL_DISTANCE_MARGIN + EMBEDDED_LITERAL_POOL_PREFIX_RESERVE +
                       literalOffsetInPool;
    uint64_t maximumPoolPosition = static_cast<uint64_t>(loadOffset) +
                                   EMBEDDED_LITERAL_MAX_FORWARD_DISPLACEMENT;
    uint64_t deadline = maximumPoolPosition > reserve ? maximumPoolPosition - reserve : 0;
    nextEmbeddedLiteralPoolCheck_ = std::min(
        nextEmbeddedLiteralPoolCheck_,
        static_cast<uint32_t>(std::min(deadline, static_cast<uint64_t>(UINT32_MAX))));
}

uint64_t ArkSteedAssembler::GetEmbeddedLiteralPoolMaxSize() const
{
    return EMBEDDED_LITERAL_POOL_PREFIX_RESERVE +
           static_cast<uint64_t>(pendingEmbeddedLiterals_.size()) * EMBEDDED_LITERAL_SIZE;
}

void ArkSteedAssembler::EmitEmbeddedLiteralPool(bool precedingCodeCanFallThrough)
{
    ASSERT(!emittingEmbeddedLiteralPool_);
    if (pendingEmbeddedLiterals_.empty()) {
        nextEmbeddedLiteralPoolCheck_ = UINT32_MAX;
        return;
    }

    emittingEmbeddedLiteralPool_ = true;
    uint32_t guardPc = UINT32_MAX;
    if (precedingCodeCanFallThrough) {
        guardPc = GetPcOffset();
        assembler_.B(0);  // 0: placeholder branch displacement.
    }

    while ((GetPcOffset() % EMBEDDED_LITERAL_SIZE) != 0) {
        assembler_.EmitU32(aarch64::Nop);
    }

    constexpr uint8_t RELOC_WIDTH = sizeof(JSTaggedType);
    for (const auto &literal : pendingEmbeddedLiterals_) {
        uint32_t literalOffset = GetPcOffset();
        ASSERT(literalOffset % EMBEDDED_LITERAL_SIZE == 0);
#ifndef NDEBUG
        for (uint32_t loadOffset : literal->loadOffsets) {
            int64_t displacement = static_cast<int64_t>(literalOffset) - static_cast<int64_t>(loadOffset);
            ASSERT(displacement >= 0);
            ASSERT(displacement <= EMBEDDED_LITERAL_MAX_FORWARD_DISPLACEMENT);
            ASSERT(displacement % sizeof(uint32_t) == 0);
        }
#endif
        assembler_.Bind(&literal->label);
        embeddedRefRelocations_.push_back({literalOffset,
                                          literal->handleIndex,
                                          EmbeddedCodeRefRelocKind::ARM64_LITERAL64,
                                          RELOC_WIDTH});
        assembler_.EmitU64(JSTaggedValue::VALUE_HOLE);
    }

    if (precedingCodeCanFallThrough) {
        PatchVeneerBranchTarget(guardPc, GetPcOffset());
    }
    pendingEmbeddedLiterals_.clear();
    embeddedLiteralIndexByHandle_.clear();
    nextEmbeddedLiteralPoolCheck_ = UINT32_MAX;
    emittingEmbeddedLiteralPool_ = false;
}

void ArkSteedAssembler::CheckCodePools(bool precedingCodeCanFallThrough, size_t protectedCodeSize)
{
    ASSERT(!emittingVeneerPool_);
    ASSERT(!emittingEmbeddedLiteralPool_);

    uint64_t protectedCodeEnd = static_cast<uint64_t>(GetPcOffset()) + protectedCodeSize;
    uint64_t potentialVeneerPoolSize = GetPotentialVeneerPoolSize();
    bool emitLiteralPool = !pendingEmbeddedLiterals_.empty() &&
        protectedCodeEnd + potentialVeneerPoolSize >= nextEmbeddedLiteralPoolCheck_;
    if (emitLiteralPool) {
        // Veneers emitted before this pool also consume displacement from pending literal loads.
        uint64_t literalPoolSize = GetEmbeddedLiteralPoolMaxSize();
        ASSERT(literalPoolSize <= std::numeric_limits<size_t>::max() - protectedCodeSize);
        CheckVeneerPool(precedingCodeCanFallThrough,
                        protectedCodeSize + static_cast<size_t>(literalPoolSize));
        EmitEmbeddedLiteralPool(precedingCodeCanFallThrough);
    }
    CheckVeneerPool(precedingCodeCanFallThrough, protectedCodeSize);
}

void ArkSteedAssembler::UpdateVeneerPoolCheck()
{
    nextVeneerPoolCheck_ = UINT32_MAX;  // No deadline until an unresolved branch supplies one.
    for (const auto &[_, branches] : veneerBranches_) {
        for (uint32_t branchPc : branches) {
            uint32_t deadline = GetVeneerBranchDeadline(branchPc, assembler_.GetU32(branchPc));
            nextVeneerPoolCheck_ = std::min(nextVeneerPoolCheck_, deadline);
        }
    }
}

void ArkSteedAssembler::RecordVeneerBranch(uint32_t branchPc, Label *target)
{
    auto [iter, inserted] = veneerBranches_.try_emplace(target, chunk_);
    iter->second.push_back(branchPc);

    if (inserted && nextVeneerPoolCheck_ != UINT32_MAX) {
        uint32_t additionalReserve = VENEER_INSTRUCTION_SIZE;
        nextVeneerPoolCheck_ = nextVeneerPoolCheck_ > additionalReserve
            ? nextVeneerPoolCheck_ - additionalReserve
            : 0;  // 0: force a full check at the next safe codegen boundary.
    }
    uint32_t deadline = GetVeneerBranchDeadline(branchPc, assembler_.GetU32(branchPc));
    nextVeneerPoolCheck_ = std::min(nextVeneerPoolCheck_, deadline);
}

void ArkSteedAssembler::PatchVeneerBranchTarget(uint32_t branchPc, uint32_t targetPc)
{
    uint32_t instruction = assembler_.GetU32(branchPc);
    int64_t displacement = static_cast<int64_t>(targetPc) - static_cast<int64_t>(branchPc);
    if (!IsVeneerBranchInRange(instruction, displacement)) {
        LOG_COMPILER(FATAL) << "ARM64 veneer branch is out of range: branchPc=" << branchPc
                            << ", targetPc=" << targetPc;
    }

    uint32_t encodedDisplacement = static_cast<uint32_t>(displacement / VENEER_INSTRUCTION_SIZE);
    if (IsVeneerBranchOrCall(instruction)) {
        instruction &= ~aarch64::BRANCH_Imm26_MASK;
        instruction |= (encodedDisplacement << aarch64::BRANCH_Imm26_LOWBITS) & aarch64::BRANCH_Imm26_MASK;
    } else if (IsVeneerConditionOrCompareBranch(instruction)) {
        instruction &= ~aarch64::BRANCH_Imm19_MASK;
        instruction |= (encodedDisplacement << aarch64::BRANCH_Imm19_LOWBITS) & aarch64::BRANCH_Imm19_MASK;
    } else if (IsVeneerTestBranch(instruction)) {
        instruction &= ~aarch64::BRANCH_Imm14_MASK;
        instruction |= (encodedDisplacement << aarch64::BRANCH_Imm14_LOWBITS) & aarch64::BRANCH_Imm14_MASK;
    } else {
        UNREACHABLE();
    }
    assembler_.PutI32(branchPc, static_cast<int32_t>(instruction));
}

void ArkSteedAssembler::BindVeneerLabel(Label *label)
{
    ASSERT(!label->IsBound());
    ASSERT(!label->IsLinked());
    uint32_t targetPc = GetPcOffset();
    auto iter = veneerBranches_.find(label);
    if (iter != veneerBranches_.end()) {
        for (uint32_t branchPc : iter->second) {
            PatchVeneerBranchTarget(branchPc, targetPc);
        }
        veneerBranches_.erase(iter);
    }
    label->BindTo(static_cast<int32_t>(targetPc));
}

void ArkSteedAssembler::CheckVeneerPool(bool precedingCodeCanFallThrough, size_t protectedCodeSize)
{
    ASSERT(!emittingVeneerPool_);
    if (veneerBranches_.empty()) {
        nextVeneerPoolCheck_ = UINT32_MAX;  // No unresolved branch needs another pool check.
        return;
    }

    uint32_t currentPc = GetPcOffset();
    uint64_t protectedCodeEnd = static_cast<uint64_t>(currentPc) + protectedCodeSize;
    if (protectedCodeEnd < nextVeneerPoolCheck_) {
        return;
    }

    uint64_t poolEntryCount = static_cast<uint64_t>(veneerBranches_.size()) + 1U;  // 1: optional guard branch.
    uint64_t poolReserve = poolEntryCount * VENEER_INSTRUCTION_SIZE;
    // No pool check may occur inside protectedCodeSize. Test unresolved
    // branches against the far side of that range and emit their veneers now.
    uint64_t prospectivePoolEnd = protectedCodeEnd + VENEER_DISTANCE_MARGIN + poolReserve;
    ChunkVector<std::pair<uint32_t, Label *>> candidates(chunk_);
    for (const auto &[target, branches] : veneerBranches_) {
        uint32_t firstDeadline = UINT32_MAX;  // Sentinel until this target's first branch is examined.
        bool needsVeneer = false;
        for (uint32_t branchPc : branches) {
            uint32_t instruction = assembler_.GetU32(branchPc);
            firstDeadline = std::min(firstDeadline, GetVeneerBranchDeadline(branchPc, instruction));
            int64_t displacement = static_cast<int64_t>(prospectivePoolEnd) - static_cast<int64_t>(branchPc);
            needsVeneer = needsVeneer || !IsVeneerBranchInRange(instruction, displacement);
        }
        if (needsVeneer) {
            candidates.emplace_back(firstDeadline, target);
        }
    }

    if (candidates.empty()) {
        UpdateVeneerPoolCheck();
        return;
    }
    std::sort(candidates.begin(), candidates.end(), [](const auto &left, const auto &right) {
        return left.first < right.first;
    });

    emittingVeneerPool_ = true;
    uint32_t guardPc = UINT32_MAX;  // Sentinel used when fallthrough does not require a guard.
    if (precedingCodeCanFallThrough) {
        guardPc = GetPcOffset();
        assembler_.B(0);  // 0: placeholder branch displacement.
    }

    for (const auto &[_, target] : candidates) {
        auto iter = veneerBranches_.find(target);
        ASSERT(iter != veneerBranches_.end());
        uint32_t veneerPc = GetPcOffset();
        assembler_.B(0);  // 0: placeholder branch displacement.
        for (uint32_t branchPc : iter->second) {
            PatchVeneerBranchTarget(branchPc, veneerPc);
        }
        iter->second.clear();
        iter->second.push_back(veneerPc);
    }

    if (precedingCodeCanFallThrough) {
        PatchVeneerBranchTarget(guardPc, GetPcOffset());
    }
    emittingVeneerPool_ = false;
    UpdateVeneerPoolCheck();
}

void ArkSteedAssembler::FinalizeVeneers()
{
    if (!veneerBranches_.empty()) {
        LOG_COMPILER(FATAL) << "Unbound labels remain after ARM64 veneer finalization";
    }
}

void ArkSteedAssembler::Jump(Label *target)
{
    uint32_t branchPc = GetPcOffset();
    assembler_.B(0);  // 0: placeholder branch displacement.
    if (target->IsBound()) {
        PatchVeneerBranchTarget(branchPc, target->GetPos());
        return;
    }
    RecordVeneerBranch(branchPc, target);
}

void ArkSteedAssembler::JumpIf(Condition condition, Label *target)
{
    if (target->IsBound()) {
        int64_t displacement = static_cast<int64_t>(target->GetPos()) - static_cast<int64_t>(GetPcOffset());
        if (!IsVeneerBranchInRange(aarch64::BranchOpCode::BranchCond, displacement)) {
            assembler_.B(ToPhysicalCondition(NegateCondition(condition)), 2);  // 2: skip the following B.
            Jump(target);
            return;
        }
    }

    uint32_t branchPc = GetPcOffset();
    assembler_.B(ToPhysicalCondition(condition), 0);  // 0: placeholder branch displacement.
    if (target->IsBound()) {
        PatchVeneerBranchTarget(branchPc, target->GetPos());
        return;
    }
    RecordVeneerBranch(branchPc, target);
}

void ArkSteedAssembler::JumpIfZero(ArkSteedRegister value, Label *target)
{
    if (target->IsBound()) {
        int64_t displacement = static_cast<int64_t>(target->GetPos()) - static_cast<int64_t>(GetPcOffset());
        if (!IsVeneerBranchInRange(aarch64::BranchOpCode::CBZ, displacement)) {
            assembler_.Cbnz(value, 2);  // 2: branch by two instructions to skip the following B.
            Jump(target);
            return;
        }
    }
    uint32_t branchPc = GetPcOffset();
    assembler_.Cbz(value, 0);
    if (target->IsBound()) {
        PatchVeneerBranchTarget(branchPc, target->GetPos());
    } else {
        RecordVeneerBranch(branchPc, target);
    }
}

void ArkSteedAssembler::JumpIfNotZero(ArkSteedRegister value, Label *target)
{
    if (target->IsBound()) {
        int64_t displacement = static_cast<int64_t>(target->GetPos()) - static_cast<int64_t>(GetPcOffset());
        if (!IsVeneerBranchInRange(aarch64::BranchOpCode::CBNZ, displacement)) {
            assembler_.Cbz(value, 2);  // 2: branch by two instructions to skip the following B.
            Jump(target);
            return;
        }
    }
    uint32_t branchPc = GetPcOffset();
    assembler_.Cbnz(value, 0);
    if (target->IsBound()) {
        PatchVeneerBranchTarget(branchPc, target->GetPos());
    } else {
        RecordVeneerBranch(branchPc, target);
    }
}


bool ArkSteedAssembler::TryJumpOnBoundBit(ArkSteedRegister value, int32_t bit, bool whenSet, Label *target)
{
    if (!target->IsBound()) {
        return false;
    }
    const auto displacement = static_cast<int64_t>(target->GetPos()) - GetPcOffset();
    auto opcode = whenSet ? aarch64::BranchOpCode::TBNZ : aarch64::BranchOpCode::TBZ;
    if (!IsVeneerBranchInRange(opcode, displacement)) {
        return false;
    }
    const uint32_t branchPc = GetPcOffset();
    if (whenSet) {
        assembler_.Tbnz(value, bit, 0);
    } else {
        assembler_.Tbz(value, bit, 0);
    }
    PatchVeneerBranchTarget(branchPc, target->GetPos());
    return true;
}

void ArkSteedAssembler::JumpOnInt32Sign(ArkSteedRegister value, bool negative, Label *target)
{
    constexpr int32_t SIGN_BIT = 31;
    if (TryJumpOnBoundBit(value.W(), SIGN_BIT, negative, target)) {
        return;
    }
    // Preserve fallthrough and use the wider conditional branch range when
    // a short test branch cannot be proven to reach the target.
    CompareInt32(value, 0);
    JumpIf(negative ? Condition::LESS_THAN : Condition::GREATER_THAN_OR_EQUAL, target);
}

void ArkSteedAssembler::BranchOnBitSet(ArkSteedRegister value, uint32_t bit, Label *ifTrue, bool trueFallsThrough,
                                       Label *ifFalse, bool falseFallsThrough)
{
    ASSERT(bit < aarch64::X_REG_SIZE);
    if (trueFallsThrough && falseFallsThrough) {
        ASSERT(ifTrue == ifFalse);
        return;
    }
    // Keep unbound targets on the ordinary conditional branch and its existing veneer path.
    auto *target = falseFallsThrough ? ifTrue : ifFalse;
    if (TryJumpOnBoundBit(value, static_cast<int32_t>(bit), falseFallsThrough, target)) {
        if (!trueFallsThrough && !falseFallsThrough) {
            Jump(ifTrue);
        }
        return;
    }
    AndSetZeroCondition(value, static_cast<int64_t>(uint64_t {1} << bit));
    Branch(Condition::NOT_EQUAL, ifTrue, trueFallsThrough, ifFalse, falseFallsThrough);
}

void ArkSteedAssembler::TestAndBranchIfZero(ArkSteedRegister value, int32_t bit, Label *target)
{
    if (target->IsBound()) {
        int64_t displacement = static_cast<int64_t>(target->GetPos()) - static_cast<int64_t>(GetPcOffset());
        if (!IsVeneerBranchInRange(aarch64::BranchOpCode::TBZ, displacement)) {
            assembler_.Tbnz(value, bit, 2);  // 2: skip the following B.
            Jump(target);
            return;
        }
    }

    uint32_t branchPc = GetPcOffset();
    assembler_.Tbz(value, bit, 0);  // 0: placeholder branch displacement.
    if (target->IsBound()) {
        PatchVeneerBranchTarget(branchPc, target->GetPos());
        return;
    }
    RecordVeneerBranch(branchPc, target);
}

void ArkSteedAssembler::TestAndBranchIfNotZero(ArkSteedRegister value, int32_t bit, Label *target)
{
    if (target->IsBound()) {
        int64_t displacement = static_cast<int64_t>(target->GetPos()) - static_cast<int64_t>(GetPcOffset());
        if (!IsVeneerBranchInRange(aarch64::BranchOpCode::TBNZ, displacement)) {
            assembler_.Tbz(value, bit, 2);  // 2: skip the following B.
            Jump(target);
            return;
        }
    }

    uint32_t branchPc = GetPcOffset();
    assembler_.Tbnz(value, bit, 0);  // 0: placeholder branch displacement.
    if (target->IsBound()) {
        PatchVeneerBranchTarget(branchPc, target->GetPos());
        return;
    }
    RecordVeneerBranch(branchPc, target);
}

static_assert(JSTaggedValue::TAG_INT == JSTaggedValue::TAG_MARK);
static_assert(JSTaggedValue::TAG_MARK == (0xFFFFULL << 48U));

void ArkSteedAssembler::JumpIfNotTaggedInt(ArkSteedRegister value, ArkSteedRegister scratch, Label *target)
{
    ASSERT(value != scratch);
    // The upper 16 bits are all ones iff their signed value is -1.
    assembler_.Asr(scratch, value, 48U);
    assembler_.Adds(aarch64::xzr, scratch, aarch64::Operand(aarch64::Immediate(1)));
    JumpIf(Condition::NOT_EQUAL, target);
}

void ArkSteedAssembler::TestTaggedNumber(ArkSteedRegister value)
{
    static_assert(JSTaggedValue::TAG_OBJECT == 0);
    static_assert(JSTaggedValue::TAG_MARK == (0xFFFFULL << 48U));
    static_assert(JSTaggedValue::TAG_INT == JSTaggedValue::TAG_MARK);
    static_assert(JSTaggedValue::DOUBLE_ENCODE_OFFSET == (1ULL << 48U));
    auto mask = aarch64::LogicalImmediate::Create(JSTaggedValue::TAG_MARK, aarch64::X_REG_SIZE);
    ASSERT(mask.IsValid());
    assembler_.Tst(value, mask);
}

void ArkSteedAssembler::CheckTaggedNumber(ArkSteedRegister value, Label *deopt)
{
    TestTaggedNumber(value);
    JumpIf(Condition::EQUAL, deopt);
}

void ArkSteedAssembler::TestMask(ArkSteedRegister value, ArkSteedRegister mask)
{
    assembler_.Tst(value, aarch64::Operand(mask));
}

void ArkSteedAssembler::CheckHClass(ArkSteedRegister receiver, uint32_t expectedHandleIndex, Label *deopt)
{
    TemporaryRegisterScope scope(this);
    auto actual = scope.Acquire();
    auto expected = scope.Acquire();
    Move(expected, static_cast<uint64_t>(JSTaggedValue::TAG_HEAPOBJECT_MASK));
    TestMask(receiver, expected);
    JumpIf(Condition::NOT_EQUAL, deopt);
    LoadHClassFromObject(actual, receiver);
    MoveEmbeddedTagged(expected, expectedHandleIndex);
    Compare(actual, expected);
    JumpIf(Condition::NOT_EQUAL, deopt);
}

void ArkSteedAssembler::CheckStableElements(ArkSteedRegister receiver, Label *deopt)
{
    TemporaryRegisterScope scope(this);
    auto bitField = scope.Acquire();
    LoadHClassFromObject(bitField, receiver);
    LoadField(bitField, bitField, static_cast<int32_t>(JSHClass::BIT_FIELD_OFFSET));
    AndSetZeroCondition(bitField, static_cast<int64_t>(1U << JSHClass::IsStableElementsBit::START_BIT));
    JumpIf(Condition::EQUAL, deopt);
}

void ArkSteedAssembler::JumpIfNotTaggedHeapObject(ArkSteedRegister value, Label *target)
{
    TemporaryRegisterScope scope(this);
    ArkSteedRegister scratch = scope.Acquire();
    Move(scratch, static_cast<int64_t>(JSTaggedValue::TAG_HEAPOBJECT_MASK));
    TestMask(value, scratch);
    JumpIf(Condition::NOT_EQUAL, target);
}

void ArkSteedAssembler::JumpIfNotJSFunction(ArkSteedRegister value, Label *target)
{
    TemporaryRegisterScope scope(this);
    ArkSteedRegister hclass = scope.Acquire();
    ArkSteedRegister objectType = scope.Acquire();
    LoadHClassFromObject(hclass, value);
    LoadField(objectType, hclass, JSHClass::BIT_FIELD_OFFSET);
    And(objectType, static_cast<int32_t>((1U << JSHClass::TYPE_BITFIELD_NUM) - 1));
    Compare(objectType, static_cast<int32_t>(JSType::JS_FUNCTION_FIRST));
    JumpIf(Condition::LESS_THAN, target);
    Compare(objectType, static_cast<int32_t>(JSType::JS_FUNCTION_LAST));
    JumpIf(Condition::GREATER_THAN, target);
}

void ArkSteedAssembler::JumpIfNotEcmaObject(ArkSteedRegister value, Label *target)
{
    TemporaryRegisterScope scope(this);
    ArkSteedRegister hclass = scope.Acquire();
    ArkSteedRegister objectType = scope.Acquire();
    LoadHClassFromObject(hclass, value);
    LoadField(objectType, hclass, JSHClass::BIT_FIELD_OFFSET);
    And(objectType, static_cast<int32_t>((1U << JSHClass::TYPE_BITFIELD_NUM) - 1));
    Compare(objectType, static_cast<int32_t>(JSType::ECMA_OBJECT_FIRST));
    JumpIf(Condition::LESS_THAN, target);
    Compare(objectType, static_cast<int32_t>(JSType::ECMA_OBJECT_LAST));
    JumpIf(Condition::GREATER_THAN, target);
}

void ArkSteedAssembler::JumpIfClassConstructor(ArkSteedRegister jsFunc, Label *target)
{
    TemporaryRegisterScope scope(this);
    ArkSteedRegister hclass = scope.Acquire();
    ArkSteedRegister bitfield = scope.Acquire();
    Label notClassConstructor;
    LoadHClassFromObject(hclass, jsFunc);
    LoadField(bitfield, hclass, JSHClass::BIT_FIELD_OFFSET);
    TestAndBranchIfZero(bitfield, JSHClass::IsClassConstructorOrPrototypeBit::START_BIT, &notClassConstructor);
    TestAndBranchIfNotZero(bitfield, JSHClass::ConstructorBit::START_BIT, target);
    Bind(&notClassConstructor);
}

void ArkSteedAssembler::JumpIfNotJSConstructorFunction(ArkSteedRegister value, Label *target)
{
    TemporaryRegisterScope scope(this);
    ArkSteedRegister bitfield = scope.Acquire();
    LoadHClassFromObject(bitfield, value);
    LoadField(bitfield, bitfield, JSHClass::BIT_FIELD_OFFSET);
    // Constructor bit first: the type mask below clears the rest of the bitfield.
    TestAndBranchIfZero(bitfield, JSHClass::ConstructorBit::START_BIT, target);
    And(bitfield, static_cast<int32_t>((1U << JSHClass::TYPE_BITFIELD_NUM) - 1));
    Compare(bitfield, static_cast<int32_t>(JSType::JS_FUNCTION_FIRST));
    JumpIf(Condition::LESS_THAN, target);
    Compare(bitfield, static_cast<int32_t>(JSType::JS_FUNCTION_LAST));
    JumpIf(Condition::GREATER_THAN, target);
}

void ArkSteedAssembler::JumpIfNewTargetHClassNotReady(ArkSteedRegister value, Label *target)
{
    TemporaryRegisterScope scope(this);
    ArkSteedRegister protoOrHClass = scope.Acquire();
    ArkSteedRegister scratch = scope.Acquire();
    // Mirrors the FastSuperAllocateThis stub's inline conditions: JSFunction first (the
    // proto-or-hclass load dereferences newTarget), tag bits before the slot content is
    // dereferenced, then JSHClass, then a JS_OBJECT instance type.
    LoadHClassFromObject(scratch, value);
    LoadField(scratch, scratch, JSHClass::BIT_FIELD_OFFSET);
    And(scratch, static_cast<int32_t>((1U << JSHClass::TYPE_BITFIELD_NUM) - 1));
    Compare(scratch, static_cast<int32_t>(JSType::JS_FUNCTION_FIRST));
    JumpIf(Condition::LESS_THAN, target);
    Compare(scratch, static_cast<int32_t>(JSType::JS_FUNCTION_LAST));
    JumpIf(Condition::GREATER_THAN, target);
    LoadField(protoOrHClass, value, JSFunction::PROTO_OR_DYNCLASS_OFFSET);
    Move(scratch, static_cast<int64_t>(JSTaggedValue::TAG_HEAPOBJECT_MASK));
    TestMask(protoOrHClass, scratch);
    JumpIf(Condition::NOT_EQUAL, target);
    LoadHClassFromObject(scratch, protoOrHClass);
    LoadField(scratch, scratch, JSHClass::BIT_FIELD_OFFSET);
    And(scratch, static_cast<int32_t>((1U << JSHClass::TYPE_BITFIELD_NUM) - 1));
    Compare(scratch, static_cast<int32_t>(JSType::HCLASS));
    JumpIf(Condition::NOT_EQUAL, target);
    LoadField(scratch, protoOrHClass, JSHClass::BIT_FIELD_OFFSET);
    And(scratch, static_cast<int32_t>((1U << JSHClass::TYPE_BITFIELD_NUM) - 1));
    Compare(scratch, static_cast<int32_t>(JSType::JS_OBJECT));
    JumpIf(Condition::NOT_EQUAL, target);
}

void ArkSteedAssembler::JumpIfNotFastSpreadArray(ArkSteedRegister value, ArkSteedRegister globalEnv, Label *target,
                                                 bool checkHoles)
{
    TemporaryRegisterScope scope(this);
    // Three scratch registers recycled through the admission phases; each
    // phase's comment names the value a temp currently holds. temp1 keeps one
    // job the longest — the array's hclass bitfield through the type/stable/
    // kind checks, then the elements object — while temp0 and temp2 rotate
    // whatever the phase needs (hclass, kind, lengths, global env bits, and
    // at the end the scan's element value and index).
    ArkSteedRegister temp0 = scope.Acquire();
    ArkSteedRegister temp1 = scope.Acquire();
    ArkSteedRegister temp2 = scope.Acquire();
    Label kindAdmitted;
    // A heap object first: the hclass load dereferences the value.
    JumpIfNotTaggedHeapObject(value, target);
    // The whole admission set hangs off two loads: the array's hclass bitfield
    // (JSType, elements kind, stable bit, prototype-modified bit) and the global
    // env bitfield (iterator detector, elements guardians).
    LoadHClassFromObject(temp0, value);
    LoadField(temp1, temp0, JSHClass::BIT_FIELD_OFFSET);
    // JSType == JS_ARRAY (low TYPE_BITFIELD_NUM bits of the bitfield).
    And(temp1, static_cast<int32_t>((1U << JSHClass::TYPE_BITFIELD_NUM) - 1));
    Compare(temp1, static_cast<int32_t>(JSType::JS_ARRAY));
    JumpIf(Condition::NOT_EQUAL, target);
    LoadField(temp1, temp0, JSHClass::BIT_FIELD_OFFSET);
    // IsStableElements bit: rejects dictionary-mode elements.
    TestAndBranchIfZero(temp1, JSHClass::IsStableElementsBit::START_BIT, target);
    // IsJSArrayPrototypeModified bit: rejects arrays created with a prototype
    // other than the current Array.prototype.
    TestAndBranchIfNotZero(temp1, JSHClass::IsJSArrayPrototypeModifiedBit::START_BIT, target);
    // Elements kind (bits 13-17 of the bitfield), the interpreter ladder's
    // ElementsKindHclassCompare: non-GENERIC kinds pass (the backing
    // representation is decided by the elements object's JSType below),
    // GENERIC is admitted only with the canonical pristine hclass (any own
    // property, e.g. a shadowing @@iterator, transitions it away).
    Move(temp2, temp1);
    Lsr(temp2, JSHClass::ElementsKindBits::START_BIT);
    And(temp2, static_cast<int32_t>((1U << JSHClass::ELEMENTS_KIND_BITFIELD_NUM) - 1));
    Compare(temp2, static_cast<int32_t>(ElementsKind::GENERIC));
    JumpIf(Condition::NOT_EQUAL, &kindAdmitted);
    {
        constexpr size_t canonicalHClassOffset =
            GlobalEnv::HEADER_SIZE +
            static_cast<size_t>(GlobalEnvField::ELEMENT_HOLE_TAGGED_HCLASS_INDEX) * JSTaggedValue::TaggedTypeSize();
        LoadField(temp2, globalEnv, static_cast<int32_t>(canonicalHClassOffset));
        Compare(temp2, temp0);
        JumpIf(Condition::NOT_EQUAL, target);
    }
    Bind(&kindAdmitted);
    // Tagged-backing admission on the elements object's own JSType (the same
    // encoding CopyJSArrayToTaggedArrayArgs dispatches on): the mutant
    // families hold unboxed int/double slots and take the runtime slow path.
    LoadField(temp1, value, static_cast<int32_t>(JSObject::ELEMENTS_OFFSET));
    // Length cap, for stack safety: both spread arms size their call-slot
    // reservation from this backing's length with no sp check at the call
    // site (the cross-tier call convention), so longer arrays must take the
    // runtime slow path, which materializes argv on the heap under its own
    // checks. Placed first after the elements load to short-circuit the
    // backing-kind reads below.
    LoadInt32Field(temp2, temp1, static_cast<int32_t>(TaggedArray::LENGTH_OFFSET));
    Compare(temp2, MAX_FAST_SPREAD_ELEMENTS);
    JumpIf(Condition::GREATER_THAN, target);
    LoadHClassFromObject(temp2, temp1);
    LoadField(temp1, temp2, JSHClass::BIT_FIELD_OFFSET);
    And(temp1, static_cast<int32_t>((1U << JSHClass::TYPE_BITFIELD_NUM) - 1));
    Compare(temp1, static_cast<int32_t>(JSType::MUTANT_TAGGED_ARRAY));
    JumpIf(Condition::EQUAL, target);
    Compare(temp1, static_cast<int32_t>(JSType::COW_MUTANT_TAGGED_ARRAY));
    JumpIf(Condition::EQUAL, target);
    // Global Array.prototype context: the iterator detector (Array.prototype
    // [Symbol.iterator] unmodified — otherwise the custom iterator must run)
    // and the elements guardians (Array.prototype carries no elements).
    LoadField(temp2, globalEnv, static_cast<int32_t>(GlobalEnv::BIT_FIELD_OFFSET));
    TestAndBranchIfNotZero(temp2, GlobalEnv::ArrayIteratorDetectorBits::START_BIT, target);
    TestAndBranchIfZero(temp2, GlobalEnv::ArrayPrototypeChangedGuardiansBits::START_BIT, target);
    if (checkHoles) {
        // STARRAYSPREAD-only tail: a hole resolves through the prototype chain
        // in the runtime slow path, not through the copy's undefined mapping.
        // The scan walks exactly the copy range [0, array length), mirroring
        // the runtime arm: RuntimeStArraySpread's stable-array fast path
        // copies GetArrayLength() elements without consulting the backing's
        // own length — a stable admitted array's backing is at least the
        // array length, the invariant CopyJSArrayObject asserts — so the
        // grown tail between the two lengths is never copied and needs no
        // scan. A negative array length is a uint32 length above INT32_MAX;
        // an array that long can only take the runtime slow path. The scan
        // index lives in temp2 and the loaded element in temp0, with the
        // backing in temp1: the index must survive the element load, or the
        // loaded tagged word becomes the next index and the scan turns into
        // an out-of-bounds walk (a trailing raw 0 even ends it after a single
        // slot).
        LoadField(temp1, value, static_cast<int32_t>(JSObject::ELEMENTS_OFFSET));
        LoadInt32Field(temp0, value, static_cast<int32_t>(JSArray::LENGTH_OFFSET));
        CompareInt32(temp0, 0);
        JumpIf(Condition::LESS_THAN, target);
        Move(temp2, temp0);
        Label scanLoop;
        Label scanDone;
        Bind(&scanLoop);
        CompareInt32(temp2, 0);
        JumpIf(Condition::ZERO, &scanDone);
        Add(temp2, -1);
        LoadTaggedElement(temp0, temp1, temp2);
        Compare(temp0, static_cast<int64_t>(JSTaggedValue::Hole().GetRawData()));
        JumpIf(Condition::EQUAL, target);
        Jump(&scanLoop);
        Bind(&scanDone);
    }
}

void ArkSteedAssembler::JumpIfConstructorIsBase(ArkSteedRegister jsFunc, Label *target)
{
    TemporaryRegisterScope scope(this);
    ArkSteedRegister method = scope.Acquire();
    ArkSteedRegister kind = scope.Acquire();
    // Base super constructors carry FunctionKind <= CLASS_CONSTRUCTOR in the method's
    // extra literal info; derived and builtin constructors sort above it.
    LoadField(method, jsFunc, JSFunctionBase::METHOD_OFFSET);
    LoadField(kind, method, Method::EXTRA_LITERAL_INFO_OFFSET);
    if constexpr (Method::FunctionKindBits::START_BIT != 0) {
        Lsr(kind, Method::FunctionKindBits::START_BIT);
    }
    And(kind, static_cast<int64_t>(
        Method::FunctionKindBits::Mask() >> Method::FunctionKindBits::START_BIT));
    Compare(kind, static_cast<int32_t>(FunctionKind::CLASS_CONSTRUCTOR));
    JumpIf(Condition::LESS_THAN_OR_EQUAL, target);
}

void ArkSteedAssembler::JumpIfFunctionNotCompiled(ArkSteedRegister jsFunc, Label *target)
{
    TemporaryRegisterScope scope(this);
    ArkSteedRegister bitfield = scope.Acquire();
    LoadField(bitfield, jsFunc, JSFunctionBase::BIT_FIELD_OFFSET);
    TestAndBranchIfZero(bitfield, JSFunctionBase::IsCompiledCodeBit::START_BIT, target);
}

void ArkSteedAssembler::BranchIfNoPendingException(Label *target)
{
    TemporaryRegisterScope scope(this);
    ArkSteedRegister scratch = scope.AcquireScratch();
    LoadField(scratch, GLUE_ROOT_REGISTER,
              static_cast<int32_t>(JSThread::GlueData::GetExceptionOffset(false)));
    Compare(scratch, static_cast<int32_t>(JSTaggedValue::VALUE_HOLE));
    JumpIf(Condition::EQUAL, target);
}

void ArkSteedAssembler::BranchIfPendingException(Label *target)
{
    TemporaryRegisterScope scope(this);
    ArkSteedRegister scratch = scope.AcquireScratch();
    LoadField(scratch, GLUE_ROOT_REGISTER,
              static_cast<int32_t>(JSThread::GlueData::GetExceptionOffset(false)));
    Compare(scratch, static_cast<int32_t>(JSTaggedValue::VALUE_HOLE));
    JumpIf(Condition::NOT_EQUAL, target);
}

void ArkSteedAssembler::ReturnWithPendingException()
{
    Move(aarch64::x0, JSTaggedValue::Exception().GetRawData());
    Epilogue();
    Return();
}

void ArkSteedAssembler::LoadAndClearPendingException(ArkSteedRegister dst, ArkSteedRegister glue)
{
    TemporaryRegisterScope scope(this);
    ArkSteedRegister scratch = scope.AcquireScratch();
    size_t offset = JSThread::GlueData::GetExceptionOffset(false);  // false : isArch32 = false

    LoadField(dst, glue, static_cast<int32_t>(offset));
    Move(scratch, JSTaggedValue::Hole().GetRawData());
    StoreField(scratch, glue, static_cast<int32_t>(offset));
}

void ArkSteedAssembler::Bind(Label *label)
{
    BindVeneerLabel(label);
}

// =============================================================================
// Call/Return
// =============================================================================

void ArkSteedAssembler::Call(ArkSteedRegister target)
{
    assembler_.Blr(target);
}

void ArkSteedAssembler::Call(Label *target)
{
    uint32_t branchPc = GetPcOffset();
    assembler_.Bl(0);  // 0: placeholder branch displacement.
    if (target->IsBound()) {
        PatchVeneerBranchTarget(branchPc, target->GetPos());
        return;
    }
    RecordVeneerBranch(branchPc, target);
}

void ArkSteedAssembler::Return()
{
    assembler_.Ret();
}

// =============================================================================
// Stack Operations
// =============================================================================

void ArkSteedAssembler::Push(ArkSteedRegister reg)
{
    PushPair(reg, aarch64::xzr);
}

void ArkSteedAssembler::Pop(ArkSteedRegister reg)
{
    PopPair(reg, aarch64::xzr);
}

namespace {
constexpr int32_t MIN_PAIR_SLOT = -64;
constexpr int32_t MAX_PAIR_SLOT = 63;
constexpr int32_t PAIR_SLOT_COUNT = 2;
constexpr uint32_t PAIR_REGISTER_COUNT = 2;

bool FitsWord64PairOffset(int32_t byteOffset)
{
    constexpr int32_t SLOT_SIZE = ArkSteedAssembler::FRAME_SLOT_SIZE;
    return byteOffset % SLOT_SIZE == 0 &&
           byteOffset / SLOT_SIZE >= MIN_PAIR_SLOT && byteOffset / SLOT_SIZE <= MAX_PAIR_SLOT;
}
}  // namespace

bool ArkSteedAssembler::CanStoreCallArgumentPair(int32_t firstCallSlot)
{
    return firstCallSlot >= 0 && firstCallSlot <= MAX_PAIR_SLOT;
}

bool ArkSteedAssembler::TryStoreCallArgumentPair(ArkSteedRegister first, ArkSteedRegister second, int32_t firstCallSlot)
{
    if (!CanStoreCallArgumentPair(firstCallSlot)) {
        return false;
    }
    assembler_.Stp(first, second, GetCallArgSlot(firstCallSlot));
    return true;
}

bool ArkSteedAssembler::TryCopyCallArgumentPair(const InstructionOperand &first, const InstructionOperand &second,
                                                int32_t firstCallSlot)
{
    if (!CanStoreCallArgumentPair(firstCallSlot) || !first.IsStackSlot() || !second.IsStackSlot()) {
        return false;
    }
    auto firstState = AllocatedState::Cast(first);
    auto secondState = AllocatedState::Cast(second);
    if (firstState.GetRepresentation() != MachineRepresentation::Tagged ||
        secondState.GetRepresentation() != MachineRepresentation::Tagged) {
        return false;
    }
    int32_t firstOffset = GetFramePointerOffsetForStackSlot(firstState.GetIndex(), firstState.GetRepresentation());
    int32_t secondOffset = GetFramePointerOffsetForStackSlot(secondState.GetIndex(), secondState.GetRepresentation());
    const int64_t distance = static_cast<int64_t>(secondOffset) - firstOffset;
    const bool reversed = distance == -FRAME_SLOT_SIZE;
    if ((distance != FRAME_SLOT_SIZE && !reversed) ||
        !FitsWord64PairOffset(std::min(firstOffset, secondOffset))) {
        return false;
    }
    TemporaryRegisterScope scope(this);
    if (!scope.HasScratchRegisters(PAIR_REGISTER_COUNT)) {
        return false;
    }
    ArkSteedRegister firstScratch = scope.AcquireScratch();
    ArkSteedRegister secondScratch = scope.AcquireScratch();
    if (reversed) {
        assembler_.Ldp(secondScratch, firstScratch, GetStackSlot(secondState));
    } else {
        assembler_.Ldp(firstScratch, secondScratch, GetStackSlot(firstState));
    }
    assembler_.Stp(firstScratch, secondScratch, GetCallArgSlot(firstCallSlot));
    return true;
}

bool ArkSteedAssembler::TryFillCallArgumentPairWithUndefined(int32_t firstSlot)
{
    if (!CanStoreCallArgumentPair(firstSlot)) {
        return false;
    }
    TemporaryRegisterScope scope(this);
    if (!scope.HasScratchRegisters(1)) {
        return false;
    }
    auto scratch = scope.AcquireScratch();
    LoadTaggedValue(scratch, JSTaggedValue::VALUE_UNDEFINED);
    return TryStoreCallArgumentPair(scratch, scratch, firstSlot);
}

void ArkSteedAssembler::PushPair(ArkSteedRegister reg1, ArkSteedRegister reg2)
{
    // -16: space for two 64-bit registers (16 bytes)
    aarch64::MemoryOperand operand(aarch64::sp, -16, aarch64::AddrMode::PREINDEX);
    // Match two sequential pushes while keeping SP 16-byte aligned.
    assembler_.Stp(reg2, reg1, operand);
}

void ArkSteedAssembler::PopPair(ArkSteedRegister reg1, ArkSteedRegister reg2)
{
    // 16: space for two 64-bit registers (16 bytes)
    aarch64::MemoryOperand operand(aarch64::sp, 16, aarch64::AddrMode::POSTINDEX);
    assembler_.Ldp(reg2, reg1, operand);
}

void ArkSteedAssembler::PushPair(ArkSteedDoubleRegister reg1, ArkSteedDoubleRegister reg2)
{
    aarch64::MemoryOperand operand(aarch64::sp, -2 * FRAME_SLOT_SIZE, aarch64::AddrMode::PREINDEX);
    assembler_.Stp(reg2, reg1, operand);
}

void ArkSteedAssembler::PopPair(ArkSteedDoubleRegister reg1, ArkSteedDoubleRegister reg2)
{
    aarch64::MemoryOperand operand(aarch64::sp, 2 * FRAME_SLOT_SIZE, aarch64::AddrMode::POSTINDEX);
    assembler_.Ldp(reg2, reg1, operand);
}

void ArkSteedAssembler::Push(ArkSteedDoubleRegister reg)
{
    aarch64::MemoryOperand operand(aarch64::sp, -2 * FRAME_SLOT_SIZE, aarch64::AddrMode::PREINDEX);
    assembler_.Str(reg, operand);
}

void ArkSteedAssembler::Pop(ArkSteedDoubleRegister reg)
{
    aarch64::MemoryOperand operand(aarch64::sp, 2 * FRAME_SLOT_SIZE, aarch64::AddrMode::POSTINDEX);
    assembler_.Ldr(reg, operand);
}

void ArkSteedAssembler::PushAll(const ArkSteedRegList &registerList)
{
    ArkSteedRegList registers = registerList;
    while (registers.Count() > 1U) {
        ArkSteedRegister reg1 = registers.PopFirst();
        ArkSteedRegister reg2 = registers.PopFirst();
        PushPair(reg1, reg2);
    }
    if (!registers.IsEmpty()) {
        Push(registers.PopFirst());
    }
}

void ArkSteedAssembler::PopAll(const ArkSteedRegList &registerList)
{
    ArkSteedRegList registers = registerList;
    if ((registers.Count() & 1U) != 0) {
        Pop(registers.PopLast());
    }
    while (!registers.IsEmpty()) {
        ArkSteedRegister reg2 = registers.PopLast();
        ArkSteedRegister reg1 = registers.PopLast();
        PopPair(reg1, reg2);
    }
}

void ArkSteedAssembler::PushAll(const ArkDoubleRegList &registerList)
{
    ArkDoubleRegList registers = registerList;
    while (registers.Count() > 1U) {
        ArkSteedDoubleRegister reg1 = registers.PopFirst();
        ArkSteedDoubleRegister reg2 = registers.PopFirst();
        PushPair(reg1, reg2);
    }
    if (!registers.IsEmpty()) {
        Push(registers.PopFirst());
    }
}

void ArkSteedAssembler::PopAll(const ArkDoubleRegList &registerList)
{
    ArkDoubleRegList registers = registerList;
    if ((registers.Count() & 1U) != 0) {
        Pop(registers.PopLast());
    }
    while (!registers.IsEmpty()) {
        ArkSteedDoubleRegister reg2 = registers.PopLast();
        ArkSteedDoubleRegister reg1 = registers.PopLast();
        PopPair(reg1, reg2);
    }
}

bool ArkSteedAssembler::TryReserveCallArgSlotsWithHeader(int32_t slotCount, int32_t first, int32_t second)
{
    // Pair pre-index uses a signed 7-bit slot displacement; SP must remain 16-byte aligned.
    if (slotCount < PAIR_SLOT_COUNT || slotCount > -MIN_PAIR_SLOT || slotCount % PAIR_SLOT_COUNT != 0) {
        return false;
    }
    TemporaryRegisterScope scope(this);
    if (!scope.HasScratchRegisters(PAIR_REGISTER_COUNT)) {
        return false;
    }
    auto firstRegister = scope.AcquireScratch();
    auto secondRegister = scope.AcquireScratch();
    Move(firstRegister, first);
    Move(secondRegister, second);
    assembler_.Stp(firstRegister, secondRegister,
                   aarch64::MemoryOperand(aarch64::sp, -slotCount * FRAME_SLOT_SIZE, aarch64::AddrMode::PREINDEX));
    return true;
}

void ArkSteedAssembler::ReserveCallArgSlots(int32_t slotCount)
{
    if (slotCount > 0) {
        Sub(aarch64::sp, slotCount * FRAME_SLOT_SIZE);
    }
}

void ArkSteedAssembler::LoadCallArgv(ArkSteedRegister dst)
{
    assembler_.Mov(dst, aarch64::sp);
}

void ArkSteedAssembler::FreeCallArgSlots(int32_t slotCount)
{
    if (slotCount > 0) {
        Add(aarch64::sp, slotCount * FRAME_SLOT_SIZE);
    }
}

void ArkSteedAssembler::ReserveCallArgSlots(ArkSteedRegister slotCount)
{
    static constexpr int FRAME_SLOT_SIZE_LOG2 = 3;
    assembler_.Sub(aarch64::sp, aarch64::sp, aarch64::Operand(slotCount, aarch64::UXTW, FRAME_SLOT_SIZE_LOG2));
}

void ArkSteedAssembler::FreeCallArgSlots(ArkSteedRegister slotCount)
{
    static constexpr int FRAME_SLOT_SIZE_LOG2 = 3;
    assembler_.Add(aarch64::sp, aarch64::sp, aarch64::Operand(slotCount, aarch64::UXTW, FRAME_SLOT_SIZE_LOG2));
}

void ArkSteedAssembler::RestoreStackPointerToFrameBottom(Graph *graph)
{
    uint32_t taggedSlots = graph->GetTaggedStackSlots();
    uint32_t untaggedSlots = graph->GetUntaggedStackSlots();
    uint32_t frameSlots = 3 + taggedSlots + untaggedSlots;  // 3: frameType, jsFunc, lexicalEnv.
    if (((taggedSlots + untaggedSlots) & 1U) == 0) {
        frameSlots++;
    }
    int32_t offset = -static_cast<int32_t>(frameSlots * FRAME_SLOT_SIZE);
    Move(aarch64::sp, kFramePointerRegister);
    Add(aarch64::sp, offset);
}

void ArkSteedAssembler::PushUndefinedForSteedCall(ArkSteedRegister fillSlotCount, uint32_t userArgc)
{
    Label fillUndefined;
    Label fillDone;
    Compare(fillSlotCount, 0);
    JumpIf(Condition::LESS_THAN_OR_EQUAL, &fillDone);

    TemporaryRegisterScope scope(this);
    ArkSteedRegister scratch = scope.AcquireScratch();
    constexpr int32_t SLOT_INDEX_SHIFT = 3;
    const int32_t firstUndefinedArgBaseSlot = static_cast<int32_t>(NUM_MANDATORY_JSFUNC_ARGS + userArgc);
    Move(scratch, static_cast<int64_t>(JSTaggedValue::VALUE_UNDEFINED));
    Add(fillSlotCount, firstUndefinedArgBaseSlot);
    Bind(&fillUndefined);
    assembler_.Str(scratch, MemoryOperand(aarch64::sp, fillSlotCount, aarch64::UXTW, SLOT_INDEX_SHIFT));
    Sub(fillSlotCount, 1);
    Compare(fillSlotCount, firstUndefinedArgBaseSlot);
    JumpIf(Condition::GREATER_THAN, &fillUndefined);
    Bind(&fillDone);
}

void ArkSteedAssembler::PushUndefinedForSteedSpreadCall(ArkSteedRegister fillSlotCount, ArkSteedRegister lastIndex)
{
    Label fillUndefined;
    Label fillDone;
    Compare(fillSlotCount, 0);
    JumpIf(Condition::LESS_THAN_OR_EQUAL, &fillDone);

    TemporaryRegisterScope scope(this);
    ArkSteedRegister scratch = scope.AcquireScratch();
    constexpr int32_t SLOT_INDEX_SHIFT = 3;
    Move(scratch, static_cast<int64_t>(JSTaggedValue::VALUE_UNDEFINED));
    // lastIndex is the pre-summed slot index (mandatory prefix + spread length +
    // fill offset); the loop counts it down to the first optional slot - 1.
    Bind(&fillUndefined);
    assembler_.Str(scratch, MemoryOperand(aarch64::sp, lastIndex, aarch64::UXTW, SLOT_INDEX_SHIFT));
    Sub(lastIndex, 1);
    Sub(fillSlotCount, 1);
    Compare(fillSlotCount, 0);
    JumpIf(Condition::GREATER_THAN, &fillUndefined);
    Bind(&fillDone);
}

void ArkSteedAssembler::CopySpreadElementsToCallArgSlots(ArkSteedRegister elements, ArkSteedRegister length,
                                                         int32_t firstUserArgSlot)
{
    TemporaryRegisterScope scope(this);
    ArkSteedRegister index = scope.Acquire();
    ArkSteedRegister elem = scope.Acquire();
    constexpr int32_t SLOT_INDEX_SHIFT = 3;
    Label loop;
    Label notHole;
    Label done;
    Compare(length, 0);
    JumpIf(Condition::LESS_THAN_OR_EQUAL, &done);
    // arm64 register-offset addressing has no displacement slot: advance the base
    // to the TaggedArray data area first (elements is dead after the copy).
    Add(elements, static_cast<int32_t>(TaggedArray::DATA_OFFSET));
    Move(index, 0);
    Bind(&loop);
    // elem = [elements + index*8]
    assembler_.Ldr(elem, MemoryOperand(elements, index, aarch64::UXTW, SLOT_INDEX_SHIFT));
    // Hole -> undefined (VALUE_HOLE = 0x05 fits an imm compare). Defensive:
    // the SUPERCALLSPREAD operand is the frontend-materialized spread result,
    // which the starrayspread admission (or its runtime arm) keeps hole-free.
    Compare(elem, static_cast<int64_t>(JSTaggedValue::VALUE_HOLE));
    JumpIf(Condition::NOT_EQUAL, &notHole);
    Move(elem, static_cast<uint64_t>(JSTaggedValue::VALUE_UNDEFINED));
    Bind(&notHole);
    // [sp + index*8 + firstUserArgSlot*8] = elem — same addressing constraint:
    // fold the base slot offset into the index instead.
    Add(index, firstUserArgSlot);
    assembler_.Str(elem, MemoryOperand(aarch64::sp, index, aarch64::UXTW, SLOT_INDEX_SHIFT));
    Sub(index, firstUserArgSlot);
    Add(index, 1);
    Compare(index, length);
    JumpIf(Condition::LESS_THAN, &loop);
    Bind(&done);
}

void ArkSteedAssembler::CopyCallerArgsToCallArgSlots(ArkSteedRegister length, int32_t firstUserArgSlot)
{
    TemporaryRegisterScope scope(this);
    ArkSteedRegister index = scope.Acquire();
    ArkSteedRegister elem = scope.Acquire();
    ArkSteedRegister srcBase = scope.Acquire();
    // Same source slot as LoadActualArgument: skip saved fp, return address,
    // argc, and the mandatory JS-function arguments.
    constexpr int32_t FIRST_USER_ARG_SLOT = 3 + NUM_MANDATORY_JSFUNC_ARGS;
    constexpr uint8_t TAGGED_SIZE_SHIFT = static_cast<uint8_t>(TAGGED_TYPE_SIZE_LOG);
    Label loop;
    Label done;
    Compare(length, 0);
    JumpIf(Condition::LESS_THAN_OR_EQUAL, &done);
    Move(index, 0);
    Bind(&loop);
    // elem = [fp + index*8 + FIRST_USER_ARG_SLOT*8] — register-offset
    // addressing has no displacement slot, so the index-scaled fp goes through
    // srcBase first (the LoadActualArgument idiom).
    assembler_.Add(srcBase, kFramePointerRegister,
                   aarch64::Operand(index, aarch64::UXTW, TAGGED_SIZE_SHIFT));
    LoadField(elem, srcBase, FIRST_USER_ARG_SLOT * FRAME_SLOT_SIZE);
    // [sp + index*8 + firstUserArgSlot*8] = elem — same addressing constraint:
    // fold the base slot offset into the index instead.
    Add(index, firstUserArgSlot);
    assembler_.Str(elem, MemoryOperand(aarch64::sp, index, aarch64::UXTW, TAGGED_SIZE_SHIFT));
    Sub(index, firstUserArgSlot);
    Add(index, 1);
    Compare(index, length);
    JumpIf(Condition::LESS_THAN, &loop);
    Bind(&done);
}

void ArkSteedAssembler::PrepareSteedCalleeContext(ArkSteedRegister target, ArkSteedRegister codeEntry)
{
    Move(aarch64::x20, target);
    LoadField(aarch64::x19, aarch64::x20, JSFunction::LEXICAL_ENV_OFFSET);
    LoadField(codeEntry, aarch64::x20, JSFunction::CODE_ENTRY_OFFSET);
}

// =============================================================================
// Function Prologue/Epilogue
// =============================================================================

void ArkSteedAssembler::Prologue(Graph *graph)
{
    RecordComment("Prologue");

    // 1. Save prevFp and returnAddr.
    // -16: space for fp and lr (two 64-bit registers, 16 bytes)
    aarch64::MemoryOperand saveFpLr(aarch64::sp, -16, aarch64::AddrMode::PREINDEX);
    assembler_.Stp(aarch64::fp, aarch64::lr, saveFpLr);
    // Delay setting fp until all frame header fields are written, so that
    // x29 is valid at any signalable point (e.g. SIGPROF for CpuProfiler).
    assembler_.Mov(aarch64::x10, aarch64::sp);

    // 2. Build the SteedFunctionFrame fixed header in push order.
    // [fp - 8] = frameType, [fp - 16] = jsFunc, [fp - 24] = lexicalEnv.
    ArkSteedRegister x19 = aarch64::x19;
    ArkSteedRegister x20 = aarch64::x20;
    ArkSteedRegister tmp = aarch64::x9;
    taggedStackSlots_ = graph->GetTaggedStackSlots();
    uint32_t taggedSlots = taggedStackSlots_ - 1;
    uint32_t untaggedSlots = graph->GetUntaggedStackSlots();
    // 3. Build the SteedFunctionFrame fixed header in push order.
    assembler_.Mov(tmp, aarch64::Immediate(static_cast<int>(FrameType::STEED_FUNCTION_FRAME)));
    PushPair(tmp, x20);
    assembler_.Mov(tmp, aarch64::Immediate(JSTaggedValue::VALUE_UNDEFINED));
    PushPair(x19, tmp);
    for (size_t i = 0; i < taggedSlots / 2; i++) {  // 2: push two slots at a time
        PushPair(tmp, tmp);
    }

    if (untaggedSlots > 0) {
        size_t slotSize = untaggedSlots * sizeof(uint64_t);
        ASSERT(slotSize <= static_cast<size_t>(std::numeric_limits<int32_t>::max()));
        Sub(aarch64::sp, static_cast<int32_t>(slotSize));
    }
    // Now all frame header fields are written — safe to set fp.
    assembler_.Mov(aarch64::fp, aarch64::x10);
    SetHasFrame(true);
}

void ArkSteedAssembler::Epilogue()
{
    RecordComment("Epilogue");
    // LeaveFrame: restore sp, then restore fp (prevFp) and lr.
    assembler_.Mov(aarch64::sp, aarch64::fp);
    // 16: space for fp and lr (two 64-bit registers, 16 bytes)
    aarch64::MemoryOperand restoreFpLr(aarch64::sp, 16, aarch64::AddrMode::POSTINDEX);
    assembler_.Ldp(aarch64::fp, aarch64::lr, restoreFpLr);
}

// =============================================================================
// Code Access
// =============================================================================

uint8_t *ArkSteedAssembler::GetCodeBuffer()
{
    return assembler_.GetBegin();
}

size_t ArkSteedAssembler::GetCodeSize()
{
    return assembler_.GetCurrentPosition();
}

// =============================================================================
// Condition Code Mapping
// =============================================================================

aarch64::Condition ArkSteedAssembler::ToPhysicalCondition(Condition condition) const
{
    switch (condition) {
        case Condition::EQUAL:
            return aarch64::Condition::EQ;
        case Condition::NOT_EQUAL:
            return aarch64::Condition::NE;
        case Condition::LESS_THAN:
            return aarch64::Condition::LT;
        case Condition::LESS_THAN_OR_EQUAL:
            return aarch64::Condition::LE;
        case Condition::GREATER_THAN:
            return aarch64::Condition::GT;
        case Condition::GREATER_THAN_OR_EQUAL:
            return aarch64::Condition::GE;
        case Condition::ABOVE:
            return aarch64::Condition::HI;
        case Condition::BELOW:
            return aarch64::Condition::LO;
        case Condition::ABOVE_OR_EQUAL:
            return aarch64::Condition::HS;
        case Condition::BELOW_OR_EQUAL:
            return aarch64::Condition::LS;
        case Condition::ZERO:
            return aarch64::Condition::EQ;
        case Condition::NOT_ZERO:
            return aarch64::Condition::NE;
        case Condition::OVERFLOWS:
            return aarch64::Condition::VS;
        case Condition::NOT_OVERFLOW:
            return aarch64::Condition::VC;
        case Condition::PARITY:
            return aarch64::Condition::VS;
        case Condition::NOT_PARITY:
            return aarch64::Condition::VC;
        default:
            UNREACHABLE();
    }
}

bool ArkSteedAssembler::TryJumpOnZeroCompare(ArkSteedRegister value, Condition condition, Label *target, bool isInt32)
{
    constexpr int32_t INT64_SIGN_BIT_INDEX = 63;
    if (isInt32) {
        value = value.W();
    }
    switch (condition) {
        case Condition::EQUAL:
        case Condition::ZERO:
        case Condition::BELOW_OR_EQUAL:
            JumpIfZero(value, target);
            return true;
        case Condition::NOT_EQUAL:
        case Condition::NOT_ZERO:
        case Condition::ABOVE:
            JumpIfNotZero(value, target);
            return true;
        case Condition::LESS_THAN:
            if (isInt32) {
                JumpOnInt32Sign(value, true, target);
            } else {
                TestAndBranchIfNotZero(value, INT64_SIGN_BIT_INDEX, target);
            }
            return true;
        case Condition::GREATER_THAN_OR_EQUAL:
            if (isInt32) {
                JumpOnInt32Sign(value, false, target);
            } else {
                TestAndBranchIfZero(value, INT64_SIGN_BIT_INDEX, target);
            }
            return true;
        default:
            return false;
    }
}

void ArkSteedAssembler::Int32BitwiseBinary(ArkSteedRegister dst, ArkSteedRegister left, int32_t right,
                                           IntBitwiseKind kind)
{
    constexpr uint32_t INT32_SHIFT_MASK = 31U;
    constexpr uint32_t INT64_BITS = 64U;
    const uint32_t shift = static_cast<uint32_t>(right) & INT32_SHIFT_MASK;
    if (kind == IntBitwiseKind::SHIFT_RIGHT_ARITHMETIC) {
        assembler_.Sbfm(dst, left, shift, INT32_SHIFT_MASK);
        return;
    }
    if (kind == IntBitwiseKind::SHIFT_LEFT && shift != 0) {
        assembler_.Sbfm(dst, left, INT64_BITS - shift, INT32_SHIFT_MASK - shift);
        return;
    }
    if (kind == IntBitwiseKind::SHIFT_RIGHT_LOGICAL && shift != 0) {
        Int32ShiftRightLogical(dst, left, shift);
        return;
    }
    if (kind == IntBitwiseKind::BITWISE_AND && right >= 0) {
        Int32And(dst, left, right);
        return;
    }
    switch (kind) {
        case IntBitwiseKind::BITWISE_AND:
            Int32And(dst, left, right);
            break;
        case IntBitwiseKind::BITWISE_OR:
            Int32Or(dst, left, right);
            break;
        case IntBitwiseKind::BITWISE_XOR:
            Int32Xor(dst, left, right);
            break;
        case IntBitwiseKind::SHIFT_LEFT:
        case IntBitwiseKind::SHIFT_RIGHT_LOGICAL:
            SignExtendInt32ToInt64(dst, left);
            return;
        default:
            UNREACHABLE();
    }
    SignExtendInt32ToInt64(dst, dst);
}

#endif
}  // namespace panda::ecmascript::arksteed
