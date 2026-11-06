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

#include "ecmascript/arksteed/arch/x64/arksteed_assembler_x64-inl.h"

#include <limits>

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
#include "ecmascript/mem/tagged_object.h"
#include "ecmascript/method.h"
#include "ecmascript/string/line_string.h"
#include "ecmascript/tagged_array.h"

namespace panda::ecmascript::arksteed {
#if defined(PANDA_TARGET_AMD64)
// =============================================================================
// Register Move Operations
// =============================================================================

void ArkSteedAssembler::Move(ArkSteedRegister dst, ArkSteedRegister src)
{
    assembler_.Movq(src, dst);
}

void ArkSteedAssembler::Move(ArkSteedRegister dst, int32_t immediate)
{
    assembler_.Movq(x64::Immediate(immediate), dst);
}

void ArkSteedAssembler::Move(ArkSteedRegister dst, int64_t immediate)
{
    assembler_.Movabs(static_cast<uint64_t>(immediate), dst);
}

void ArkSteedAssembler::Move(ArkSteedRegister dst, uint64_t immediate)
{
    assembler_.Movabs(immediate, dst);
}

void ArkSteedAssembler::MoveEmbeddedTagged(ArkSteedRegister dst, uint32_t handleIndex)
{
    constexpr uint32_t MOVABS_IMMEDIATE_OFFSET = 2;
    constexpr uint32_t MOVABS_SIZE = 10;
    constexpr uint8_t RELOC_WIDTH = sizeof(JSTaggedType);

    uint32_t instructionOffset = GetPcOffset();
    assembler_.Movabs(JSTaggedValue::VALUE_HOLE, dst);
    ASSERT(GetPcOffset() - instructionOffset == MOVABS_SIZE);
    embeddedRefRelocations_.push_back({instructionOffset + MOVABS_IMMEDIATE_OFFSET,
                                      handleIndex,
                                      EmbeddedCodeRefRelocKind::X64_MOVABS_IMM64,
                                      RELOC_WIDTH});
}

void ArkSteedAssembler::FinalizeEmbeddedRefs()
{
}

void ArkSteedAssembler::Move(ArkSteedDoubleRegister dst, ArkSteedDoubleRegister src)
{
    assembler_.Movsd(dst, src);
}

void ArkSteedAssembler::Move(ArkSteedDoubleRegister dst, double immediate)
{
    uint64_t bits = base::bit_cast<uint64_t>(immediate);
    if (bits == 0) {
        assembler_.Xorpd(dst, dst);
        return;
    }
    TemporaryRegisterScope scope(this);
    ArkSteedRegister scratch = scope.AcquireScratch();
    assembler_.Movabs(bits, scratch);
    assembler_.Movq(dst, scratch);
}

void ArkSteedAssembler::Move(ArkSteedDoubleRegister dst, double immediate, ArkSteedRegister scratch)
{
    uint64_t bits = base::bit_cast<uint64_t>(immediate);
    if (bits == 0) {
        assembler_.Xorpd(dst, dst);
        return;
    }
    assembler_.Movabs(bits, scratch);
    assembler_.Movq(dst, scratch);
}

void ArkSteedAssembler::Move(ArkSteedDoubleRegister dst, ArkSteedRegister src)
{
    assembler_.Movq(dst, src);
}

void ArkSteedAssembler::Move(ArkSteedRegister dst, ArkSteedDoubleRegister src)
{
    assembler_.Movq(dst, src);
}

// =============================================================================
// Memory Operations
// =============================================================================

void ArkSteedAssembler::LoadField(ArkSteedRegister dst, ArkSteedRegister base, int32_t offset)
{
    x64::Operand operand(base, offset);
    assembler_.Movq(operand, dst);
}

void ArkSteedAssembler::LoadUint8Field(ArkSteedRegister dst, ArkSteedRegister base, int32_t offset)
{
    assembler_.Movzbl(x64::Operand(base, offset), dst);
}

void ArkSteedAssembler::LoadInt32Field(ArkSteedRegister dst, ArkSteedRegister base, int32_t offset)
{
    x64::Operand operand(base, offset);
    assembler_.Movl(operand, dst);
}

void ArkSteedAssembler::LoadTaggedElement(ArkSteedRegister dst, ArkSteedRegister elements, ArkSteedRegister index)
{
    x64::Operand operand(elements, index, x64::Scale::Times8, static_cast<int32_t>(TaggedArray::DATA_OFFSET));
    assembler_.Movq(operand, dst);
}

void ArkSteedAssembler::StoreTaggedElement(ArkSteedRegister elements, ArkSteedRegister index,
                                           ArkSteedRegister value, ArkSteedRegister scratch)
{
    (void)scratch;
    x64::Operand operand(elements, index, x64::Scale::Times8, static_cast<int32_t>(TaggedArray::DATA_OFFSET));
    assembler_.Movq(value, operand);
}

void ArkSteedAssembler::LoadLineStringCharCode(ArkSteedRegister dst, ArkSteedRegister string, ArkSteedRegister index,
                                               ArkSteedRegister lengthAndFlags)
{
    Label utf16;
    Label done;
    assembler_.Btl(x64::Immediate(BaseString::CompressedStatusBit::START_BIT), lengthAndFlags);
    JumpIf(Condition::BELOW, &utf16);
    assembler_.Movzbq(x64::Operand(string, index, x64::Scale::Times1, static_cast<int32_t>(LineString::DATA_OFFSET)),
                      dst);
    Jump(&done);
    Bind(&utf16);
    assembler_.Movzwq(x64::Operand(string, index, x64::Scale::Times2, static_cast<int32_t>(LineString::DATA_OFFSET)),
                      dst);
    Bind(&done);
}

void ArkSteedAssembler::LoadTypedArrayDataPointer(ArkSteedRegister dst, ArkSteedRegister receiver,
                                                  ArkSteedRegister storage, ArkSteedRegister scratch, bool isOnHeap)
{
    if (isOnHeap) {
        Move(dst, storage);
        Add(dst, static_cast<int32_t>(ByteArray::DATA_OFFSET));
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
            assembler_.Movzbl(x64::Operand(data, index, x64::Scale::Times1, 0), dst);
            Int32ShiftLeft(dst, 24U);
            Int32ShiftRightArithmetic(dst, 24U);
            break;
        case JSType::JS_UINT8_ARRAY:
        case JSType::JS_UINT8_CLAMPED_ARRAY:
            assembler_.Movzbl(x64::Operand(data, index, x64::Scale::Times1, 0), dst);
            break;
        case JSType::JS_INT16_ARRAY:
            assembler_.Movzwq(x64::Operand(data, index, x64::Scale::Times2, 0), dst);
            Int32ShiftLeft(dst, 16U);
            Int32ShiftRightArithmetic(dst, 16U);
            break;
        case JSType::JS_UINT16_ARRAY:
            assembler_.Movzwq(x64::Operand(data, index, x64::Scale::Times2, 0), dst);
            break;
        case JSType::JS_INT32_ARRAY:
            assembler_.Movl(x64::Operand(data, index, x64::Scale::Times4, 0), dst);
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
            assembler_.Movl(x64::Operand(data, index, x64::Scale::Times4, 0), scratch);
            assembler_.Cvtsi2sd(scratch, dst);
            break;
        case JSType::JS_FLOAT32_ARRAY:
            assembler_.Movss(dst, x64::Operand(data, index, x64::Scale::Times4, 0));
            assembler_.Cvtss2sd(dst, dst);
            break;
        case JSType::JS_FLOAT64_ARRAY:
            assembler_.Movsd(dst, x64::Operand(data, index, x64::Scale::Times8, 0));
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
            assembler_.Movb(value, x64::Operand(data, index, x64::Scale::Times1, 0));
            return;
        case JSType::JS_INT16_ARRAY:
        case JSType::JS_UINT16_ARRAY:
            assembler_.Movw(value, x64::Operand(data, index, x64::Scale::Times2, 0));
            return;
        case JSType::JS_INT32_ARRAY:
        case JSType::JS_UINT32_ARRAY:
            assembler_.Movl(value, x64::Operand(data, index, x64::Scale::Times4, 0));
            return;
        default:
            UNREACHABLE();
    }
}

void ArkSteedAssembler::StoreField(ArkSteedRegister src, ArkSteedRegister base, int32_t offset)
{
    x64::Operand operand(base, offset);
    assembler_.Movq(src, operand);
}

void ArkSteedAssembler::StoreField(ArkSteedRegister src, ArkSteedRegister base, ArkSteedRegister offset)
{
    assembler_.Movq(src, x64::Operand(base, offset, x64::Scale::Times1, 0));
}

void ArkSteedAssembler::StoreInt8Field(ArkSteedRegister src, ArkSteedRegister base, int32_t offset)
{
    assembler_.Movb(src, x64::Operand(base, offset));
}

void ArkSteedAssembler::StoreInt16Field(ArkSteedRegister src, ArkSteedRegister base, int32_t offset)
{
    assembler_.Movw(src, x64::Operand(base, offset));
}

void ArkSteedAssembler::StoreInt32Field(ArkSteedRegister src, ArkSteedRegister base, int32_t offset)
{
    x64::Operand operand(base, offset);
    assembler_.Movl(src, operand);
}

void ArkSteedAssembler::StoreInt32FieldRelease(ArkSteedRegister src, ArkSteedRegister base, int32_t offset)
{
    StoreInt32Field(src, base, offset);
}

void ArkSteedAssembler::StoreHeaderRelease(ArkSteedRegister src,
                                           ArkSteedRegister object)
{
    static_assert(TaggedObject::HCLASS_OFFSET == 0);
    // Aligned stores are atomic on x64 and TSO provides release ordering.
    StoreField(src, object, 0);
}

void ArkSteedAssembler::StoreFloat64Field(ArkSteedDoubleRegister src, ArkSteedRegister base, int32_t offset)
{
    StoreFloat64(x64::Operand(base, offset), src);
}

void ArkSteedAssembler::StoreFloat32Field(ArkSteedDoubleRegister src, ArkSteedDoubleRegister scratch,
                                          ArkSteedRegister base, int32_t offset)
{
    assembler_.Cvtsd2ss(src, scratch);
    assembler_.Movss(x64::Operand(base, offset), scratch);
}

void ArkSteedAssembler::LoadActualArgc(ArkSteedRegister dst)
{
    // 2: argc is stored at fp + 2 * FRAME_SLOT_SIZE
    x64::Operand operand(kFramePointerRegister, 2 * FRAME_SLOT_SIZE);
    assembler_.Movq(operand, dst);
}

void ArkSteedAssembler::LoadActualArgument(ArkSteedRegister dst, ArkSteedRegister index,
                                           [[maybe_unused]] ArkSteedRegister scratch)
{
    // 3: skip saved fp, return address, and argc; then skip mandatory JS-function arguments.
    constexpr int32_t FIRST_USER_ARG_SLOT = 3 + NUM_MANDATORY_JSFUNC_ARGS;
    x64::Operand operand(kFramePointerRegister, index, x64::Scale::Times8,
                         FIRST_USER_ARG_SLOT * FRAME_SLOT_SIZE);
    assembler_.Movq(operand, dst);
}

void ArkSteedAssembler::LoadFloat64(ArkSteedDoubleRegister dst, MemoryOperand srcOp)
{
    assembler_.Movsd(dst, srcOp);
}

void ArkSteedAssembler::StoreFloat64(MemoryOperand dstOp, ArkSteedDoubleRegister src)
{
    assembler_.Movsd(dstOp, src);
}

void ArkSteedAssembler::StoreFloat64Constant(MemoryOperand dstOp, double immediate, ArkSteedRegister scratchGPR,
                                             [[maybe_unused]] ArkSteedDoubleRegister scratchFPR)
{
    uint64_t bits = base::bit_cast<uint64_t>(immediate);
    int32_t immediate32 = static_cast<int32_t>(bits);
    if (static_cast<uint64_t>(static_cast<int64_t>(immediate32)) == bits) {
        assembler_.Movq(x64::Immediate(immediate32), dstOp);
        return;
    }
    assembler_.Movabs(bits, scratchGPR);
    assembler_.Movq(scratchGPR, dstOp);
}

void ArkSteedAssembler::NormalizeEagerDeoptOverflowLink()
{
    static_assert(ARKSTEED_EAGER_DEOPT_FIXED_EXIT_LINK_SIZE == FRAME_SLOT_SIZE);
    assembler_.Addq(x64::Immediate(static_cast<int32_t>(ARKSTEED_EAGER_DEOPT_FIXED_EXIT_LINK_SIZE)), x64::rsp);
}

void ArkSteedAssembler::CallArkSteedDeoptimizationEntry()
{
    ASSERT(temporaryRegisterScope_ == nullptr);
    ASSERT(entryThread_ != nullptr);

    ArkSteedRegister glue = ARKSTEED_EAGER_DEOPT_ENTRY_GLUE_REGISTER;
    ArkSteedRegister target = ARKSTEED_EAGER_DEOPT_ENTRY_TARGET_REGISTER;
    Move(glue, static_cast<uint64_t>(entryThread_->GetGlueAddr()));

    Address address = entryThread_->GetRTInterface(RTSTUB_ID(ArkSteedDeoptimizationEntry));
    Move(target, static_cast<uint64_t>(address));
    Call(target);
}

void ArkSteedAssembler::ConvertInt32ToDouble(ArkSteedDoubleRegister dst, ArkSteedRegister src)
{
    assembler_.Cvtsi2sd32(src, dst);
}

// =============================================================================
// Arithmetic Operations
// =============================================================================

void ArkSteedAssembler::Add(ArkSteedRegister dst, ArkSteedRegister src)
{
    assembler_.Addq(src, dst);
}

void ArkSteedAssembler::Add(ArkSteedRegister dst, int32_t immediate)
{
    assembler_.Addq(x64::Immediate(immediate), dst);
}

void ArkSteedAssembler::Add(ArkSteedRegister dst, int64_t immediate)
{
    TemporaryRegisterScope scope(this);
    auto scratch = scope.AcquireScratch();
    assembler_.Movabs(static_cast<uint64_t>(immediate), scratch);
    assembler_.Addq(scratch, dst);
}

void ArkSteedAssembler::Sub(ArkSteedRegister dst, ArkSteedRegister src)
{
    assembler_.Subq(src, dst);
}

void ArkSteedAssembler::Sub(ArkSteedRegister dst, int32_t immediate)
{
    assembler_.Subq(x64::Immediate(immediate), dst);
}

void ArkSteedAssembler::SignExtendInt32ToInt64(ArkSteedRegister dst, ArkSteedRegister src)
{
    assembler_.Movsxd(src, dst);
}

void ArkSteedAssembler::Int32Add(ArkSteedRegister dst, ArkSteedRegister left, ArkSteedRegister right)
{
    ASSERT(dst == left);
    assembler_.Addl(right, dst);
}

void ArkSteedAssembler::Int32Add(ArkSteedRegister dst, ArkSteedRegister left, int32_t immediate)
{
    ASSERT(dst == left);
    assembler_.Addl(x64::Immediate(immediate), dst);
}

void ArkSteedAssembler::Int32Sub(ArkSteedRegister dst, ArkSteedRegister left, ArkSteedRegister right)
{
    ASSERT(dst == left);
    assembler_.Subl(right, dst);
}

void ArkSteedAssembler::Int32Sub(ArkSteedRegister dst, ArkSteedRegister left, int32_t immediate)
{
    ASSERT(dst == left);
    assembler_.Subl(x64::Immediate(immediate), dst);
}

void ArkSteedAssembler::Int32Mul(ArkSteedRegister dst, ArkSteedRegister src)
{
    assembler_.Imull(src, dst);
}

void ArkSteedAssembler::Int32Mul(ArkSteedRegister dst, int32_t immediate)
{
    TemporaryRegisterScope scope(this);
    ArkSteedRegister scratch = scope.AcquireScratch();
    Move(scratch, immediate);
    Int32Mul(dst, scratch);
}

void ArkSteedAssembler::Int32MulWide([[maybe_unused]] ArkSteedRegister dst,
                                     [[maybe_unused]] ArkSteedRegister left,
                                     [[maybe_unused]] ArkSteedRegister right)
{
    UNREACHABLE();
}

void ArkSteedAssembler::Int32MulHigh(ArkSteedRegister dst, ArkSteedRegister left, ArkSteedRegister right)
{
    ASSERT(dst == x64::rdx);
    ASSERT(left == x64::rax);
    assembler_.Imull(right);
}

void ArkSteedAssembler::Int32Div(ArkSteedRegister dst, ArkSteedRegister dividend, ArkSteedRegister divisor)
{
    Int32DivAndRemainder(dst, x64::rdx, dividend, divisor);
}

void ArkSteedAssembler::Int32DivAndRemainder(ArkSteedRegister quotient, ArkSteedRegister remainder,
                                             ArkSteedRegister dividend, ArkSteedRegister divisor)
{
    ASSERT(quotient == x64::rax);
    ASSERT(remainder == x64::rdx);
    ASSERT(divisor != x64::rax && divisor != x64::rdx);
    if (dividend != x64::rax) {
        Move(x64::rax, dividend);
    }
    assembler_.Cdq();
    assembler_.Idivl(divisor);
}

void ArkSteedAssembler::PositiveInt32Mod(ArkSteedRegister dst, ArkSteedRegister dividend, ArkSteedRegister divisor)
{
    ASSERT(divisor != x64::rax && divisor != x64::rdx);
    if (dst != x64::rdx) {
        ASSERT(dst != x64::rax);
    }
    Int32DivAndRemainder(x64::rax, x64::rdx, dividend, divisor);
    Move(dst, x64::rdx);
}

void ArkSteedAssembler::Int32ToFloat64(ArkSteedDoubleRegister dst, ArkSteedRegister src)
{
    TemporaryRegisterScope scope(this);
    ArkSteedRegister scratch = scope.AcquireScratch();
    SignExtendInt32ToInt64(scratch, src);
    assembler_.Cvtsi2sd(scratch, dst);
}

void ArkSteedAssembler::Float64Add(ArkSteedDoubleRegister dst, ArkSteedDoubleRegister src)
{
    assembler_.Addsd(src, dst);
}

void ArkSteedAssembler::Uint32ToFloat64(ArkSteedDoubleRegister dst, ArkSteedRegister src)
{
    TemporaryRegisterScope scope(this);
    ArkSteedRegister scratch = scope.AcquireScratch();
    assembler_.Movl(src, scratch);
    assembler_.Cvtsi2sd(scratch, dst);
}

void ArkSteedAssembler::Float64Sub(ArkSteedDoubleRegister dst, ArkSteedDoubleRegister src)
{
    assembler_.Subsd(src, dst);
}

void ArkSteedAssembler::Float64Mul(ArkSteedDoubleRegister dst, ArkSteedDoubleRegister src)
{
    assembler_.Mulsd(src, dst);
}

void ArkSteedAssembler::Float64Div(ArkSteedDoubleRegister dst, ArkSteedDoubleRegister src)
{
    assembler_.Divsd(src, dst);
}

void ArkSteedAssembler::Float64Neg(ArkSteedDoubleRegister dst, ArkSteedDoubleRegister src)
{
    if (dst != src) {
        Move(dst, src);
    }
    TemporaryRegisterScope scope(this);
    ArkSteedRegister scratchGPR = scope.AcquireScratch();
    ArkSteedDoubleRegister scratchFPR = scope.AcquireDoubleScratch();
    Move(scratchGPR, static_cast<uint64_t>(1ULL << 63U));
    Move(scratchFPR, scratchGPR);
    assembler_.Xorpd(scratchFPR, dst);
}

void ArkSteedAssembler::CompareFloat64(ArkSteedDoubleRegister left, ArkSteedDoubleRegister right)
{
    assembler_.Ucomisd(right, left);
}

void ArkSteedAssembler::TruncateFloat64ToInt32(ArkSteedRegister dst, ArkSteedDoubleRegister src)
{
    assembler_.Cvttsd2si64(src, dst);
    assembler_.Andq(x64::Immediate(0xFFFFFFFF), dst);
}

void ArkSteedAssembler::Word64And(ArkSteedRegister dst, ArkSteedRegister src)
{
    assembler_.And(src, dst);
}

// =============================================================================
// Bitwise Operations
// =============================================================================

void ArkSteedAssembler::Int32Neg(ArkSteedRegister dst, ArkSteedRegister src)
{
    ASSERT(dst == src);
    assembler_.Negl(dst);
}

void ArkSteedAssembler::Int32Inc(ArkSteedRegister dst, ArkSteedRegister src)
{
    ASSERT(dst == src);
    assembler_.Incl(dst);
}

void ArkSteedAssembler::Int32Dec(ArkSteedRegister dst, ArkSteedRegister src)
{
    ASSERT(dst == src);
    assembler_.Decl(dst);
}

void ArkSteedAssembler::Int32BNot(ArkSteedRegister dst)
{
    assembler_.Notl(dst);
}

void ArkSteedAssembler::Or(ArkSteedRegister dst, int32_t immediate)
{
    assembler_.Or(x64::Immediate(immediate), dst);
}

void ArkSteedAssembler::Or(ArkSteedRegister dst, int64_t immediate)
{
    TemporaryRegisterScope scope(this);
    auto scratch = scope.AcquireScratch();
    assembler_.Movabs(static_cast<uint64_t>(immediate), scratch);
    assembler_.Orq(scratch, dst);
}

void ArkSteedAssembler::Or(ArkSteedRegister dst, ArkSteedRegister src)
{
    assembler_.Orq(src, dst);
}

void ArkSteedAssembler::And(ArkSteedRegister dst, int32_t immediate)
{
    assembler_.Andq(x64::Immediate(immediate), dst);
}

void ArkSteedAssembler::And(ArkSteedRegister dst, int64_t immediate)
{
    TemporaryRegisterScope scope(this);
    auto scratch = scope.AcquireScratch();
    assembler_.Movabs(static_cast<uint64_t>(immediate), scratch);
    assembler_.And(scratch, dst);
}

void ArkSteedAssembler::And(ArkSteedRegister dst, ArkSteedRegister src)
{
    assembler_.And(src, dst);
}

void ArkSteedAssembler::Lsr(ArkSteedRegister dst, uint32_t shift)
{
    assembler_.Shrq(x64::Immediate(static_cast<int32_t>(shift)), dst);
}

void ArkSteedAssembler::ShiftRightLogical(ArkSteedRegister dst, uint32_t shift)
{
    assembler_.Shrq(shift, dst);
}

void ArkSteedAssembler::ShiftLeft(ArkSteedRegister dst, uint32_t shift)
{
    assembler_.Shlq(x64::Immediate(static_cast<int32_t>(shift)), dst);
}

void ArkSteedAssembler::ShiftRightLogical32(ArkSteedRegister dst, uint32_t shift)
{
    assembler_.Shrl(shift, dst);
}

void ArkSteedAssembler::MoveBitMask32(ArkSteedRegister dst, ArkSteedRegister bitIndex)
{
    Move(dst, 0);
    assembler_.Btsl(bitIndex, dst);
}

void ArkSteedAssembler::Int32And(ArkSteedRegister dst, ArkSteedRegister src)
{
    assembler_.Andl(src, dst);
}

void ArkSteedAssembler::Int32And(ArkSteedRegister dst, int32_t immediate)
{
    assembler_.Andl(x64::Immediate(immediate), dst);
}

void ArkSteedAssembler::Int32Or(ArkSteedRegister dst, ArkSteedRegister src)
{
    assembler_.Orl(src, dst);
}

void ArkSteedAssembler::Int32Or(ArkSteedRegister dst, int32_t immediate)
{
    assembler_.Orl(x64::Immediate(immediate), dst);
}

void ArkSteedAssembler::Int32Xor(ArkSteedRegister dst, ArkSteedRegister src)
{
    assembler_.Xorl(src, dst);
}

void ArkSteedAssembler::Int32Xor(ArkSteedRegister dst, int32_t immediate)
{
    assembler_.Xorl(x64::Immediate(immediate), dst);
}

void ArkSteedAssembler::Int32ShiftLeft(ArkSteedRegister dst, uint32_t shift)
{
    assembler_.Shll(x64::Immediate(static_cast<int32_t>(shift)), dst);
}

void ArkSteedAssembler::Int32ShiftLeftByRegister(ArkSteedRegister dst, ArkSteedRegister shift)
{
    ASSERT(shift == x64::rcx);
    assembler_.ShllCl(dst);
}

void ArkSteedAssembler::Int32ShiftRightLogical(ArkSteedRegister dst, uint32_t shift)
{
    assembler_.Shrl(x64::Immediate(static_cast<int32_t>(shift)), dst);
}

void ArkSteedAssembler::Int32ShiftRightLogicalByRegister(ArkSteedRegister dst, ArkSteedRegister shift)
{
    ASSERT(shift == x64::rcx);
    assembler_.ShrlCl(dst);
}

void ArkSteedAssembler::Int32ShiftRightArithmetic(ArkSteedRegister dst, uint32_t shift)
{
    assembler_.Sarl(x64::Immediate(static_cast<int32_t>(shift)), dst);
}

void ArkSteedAssembler::Int32ShiftRightArithmeticByRegister(ArkSteedRegister dst, ArkSteedRegister shift)
{
    ASSERT(shift == x64::rcx);
    assembler_.SarlCl(dst);
}

// =============================================================================
// Comparison Operations
// =============================================================================

void ArkSteedAssembler::Compare(ArkSteedRegister lhs, ArkSteedRegister rhs)
{
    assembler_.Cmpq(rhs, lhs);
}

void ArkSteedAssembler::CompareInt32(ArkSteedRegister lhs, ArkSteedRegister rhs)
{
    assembler_.Cmpl(rhs, lhs);
}

void ArkSteedAssembler::Compare(ArkSteedRegister lhs, int32_t immediate)
{
    assembler_.Cmpq(x64::Immediate(immediate), lhs);
}

void ArkSteedAssembler::Compare(ArkSteedRegister lhs, int64_t immediate)
{
    TemporaryRegisterScope scope(this);
    auto scratch = scope.AcquireScratch();
    assembler_.Movabs(static_cast<uint64_t>(immediate), scratch);
    assembler_.Cmpq(scratch, lhs);
}

void ArkSteedAssembler::CompareInt32(ArkSteedRegister lhs, int32_t immediate)
{
    assembler_.Cmpl(x64::Immediate(immediate), lhs);
}

void ArkSteedAssembler::CompareField(ArkSteedRegister base, int32_t offset, ArkSteedRegister rhs)
{
    x64::Operand operand(base, offset);
    assembler_.Cmpq(rhs, operand);
}

// =============================================================================
// Control Flow
// =============================================================================

void ArkSteedAssembler::Jump(Label *target)
{
    assembler_.Jmp(target);
}

void ArkSteedAssembler::Jump(ArkSteedRegister target)
{
    assembler_.Jmp(target);
}

void ArkSteedAssembler::JumpIf(Condition condition, Label *target)
{
    switch (condition) {
        case Condition::EQUAL:
            assembler_.Je(target);
            break;
        case Condition::NOT_EQUAL:
            assembler_.Jne(target);
            break;
        case Condition::LESS_THAN:
            // COND_LESS_THAN is signed; unsigned comparisons must use a separate condition.
            assembler_.Jl(target);
            break;
        case Condition::LESS_THAN_OR_EQUAL:
            assembler_.Jle(target);
            break;
        case Condition::GREATER_THAN:
            assembler_.Jg(target);
            break;
        case Condition::GREATER_THAN_OR_EQUAL:
            assembler_.Jge(target);
            break;
        case Condition::ZERO:
            assembler_.Jz(target);
            break;
        case Condition::NOT_ZERO:
            assembler_.Jnz(target);
            break;
        case Condition::ABOVE:
            assembler_.Ja(target);
            break;
        case Condition::BELOW:
            assembler_.Jb(target);
            break;
        case Condition::ABOVE_OR_EQUAL:
            assembler_.Jae(target);
            break;
        case Condition::BELOW_OR_EQUAL:
            assembler_.Jbe(target);
            break;
        case Condition::OVERFLOWS:
            assembler_.Jo(target);
            break;
        case Condition::NOT_OVERFLOW:
            assembler_.Jno(target);
            break;
        case Condition::PARITY:
            assembler_.Jp(target);
            break;
        case Condition::NOT_PARITY:
            assembler_.Jnp(target);
            break;
        default:
            UNREACHABLE();
    }
}

void ArkSteedAssembler::JumpIfNotTaggedHeapObject(ArkSteedRegister value, Label *target)
{
    TemporaryRegisterScope scope(this);
    ArkSteedRegister scratch = scope.Acquire();
    Move(scratch, value);
    And(scratch, static_cast<int64_t>(JSTaggedValue::TAG_HEAPOBJECT_MASK));
    Compare(scratch, 0);
    JumpIf(Condition::NOT_EQUAL, target);
}

void ArkSteedAssembler::JumpIfNotJSFunction(ArkSteedRegister value, Label *target)
{
    TemporaryRegisterScope scope(this);
    ArkSteedRegister scratch = scope.Acquire();
    LoadHClassFromObject(scratch, value);
    LoadField(scratch, scratch, JSHClass::BIT_FIELD_OFFSET);
    And(scratch, static_cast<int32_t>((1U << JSHClass::TYPE_BITFIELD_NUM) - 1));
    Compare(scratch, static_cast<int32_t>(JSType::JS_FUNCTION_FIRST));
    JumpIf(Condition::LESS_THAN, target);
    Compare(scratch, static_cast<int32_t>(JSType::JS_FUNCTION_LAST));
    JumpIf(Condition::GREATER_THAN, target);
}

void ArkSteedAssembler::JumpIfNotEcmaObject(ArkSteedRegister value, Label *target)
{
    TemporaryRegisterScope scope(this);
    ArkSteedRegister scratch = scope.Acquire();
    LoadHClassFromObject(scratch, value);
    LoadField(scratch, scratch, JSHClass::BIT_FIELD_OFFSET);
    And(scratch, static_cast<int32_t>((1U << JSHClass::TYPE_BITFIELD_NUM) - 1));
    Compare(scratch, static_cast<int32_t>(JSType::ECMA_OBJECT_FIRST));
    JumpIf(Condition::LESS_THAN, target);
    Compare(scratch, static_cast<int32_t>(JSType::ECMA_OBJECT_LAST));
    JumpIf(Condition::GREATER_THAN, target);
}

void ArkSteedAssembler::JumpIfClassConstructor(ArkSteedRegister jsFunc, Label *target)
{
    TemporaryRegisterScope scope(this);
    ArkSteedRegister scratch = scope.Acquire();
    Label notClassConstructor;
    LoadHClassFromObject(scratch, jsFunc);
    LoadField(scratch, scratch, JSHClass::BIT_FIELD_OFFSET);
    assembler_.Btq(x64::Immediate(JSHClass::IsClassConstructorOrPrototypeBit::START_BIT), scratch);
    assembler_.Jnb(&notClassConstructor);
    assembler_.Btq(x64::Immediate(JSHClass::ConstructorBit::START_BIT), scratch);
    assembler_.Jb(target);
    Bind(&notClassConstructor);
}

void ArkSteedAssembler::JumpIfNotJSConstructorFunction(ArkSteedRegister value, Label *target)
{
    TemporaryRegisterScope scope(this);
    ArkSteedRegister scratch = scope.Acquire();
    LoadHClassFromObject(scratch, value);
    LoadField(scratch, scratch, JSHClass::BIT_FIELD_OFFSET);
    // Constructor bit first: the type mask below clears the rest of the bitfield.
    assembler_.Btq(x64::Immediate(JSHClass::ConstructorBit::START_BIT), scratch);
    assembler_.Jnb(target);
    And(scratch, static_cast<int32_t>((1U << JSHClass::TYPE_BITFIELD_NUM) - 1));
    Compare(scratch, static_cast<int32_t>(JSType::JS_FUNCTION_FIRST));
    JumpIf(Condition::LESS_THAN, target);
    Compare(scratch, static_cast<int32_t>(JSType::JS_FUNCTION_LAST));
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
    Move(scratch, protoOrHClass);
    And(scratch, static_cast<int64_t>(JSTaggedValue::TAG_HEAPOBJECT_MASK));
    Compare(scratch, 0);
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
    assembler_.Btq(x64::Immediate(JSHClass::IsStableElementsBit::START_BIT), temp1);
    assembler_.Jnb(target);
    // IsJSArrayPrototypeModified bit: rejects arrays created with a prototype
    // other than the current Array.prototype.
    assembler_.Btq(x64::Immediate(JSHClass::IsJSArrayPrototypeModifiedBit::START_BIT), temp1);
    assembler_.Jb(target);
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
    assembler_.Btq(x64::Immediate(GlobalEnv::ArrayIteratorDetectorBits::START_BIT), temp2);
    assembler_.Jb(target);
    assembler_.Btq(x64::Immediate(GlobalEnv::ArrayPrototypeChangedGuardiansBits::START_BIT), temp2);
    assembler_.Jnb(target);
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
    Lsr(kind, Method::FunctionKindBits::START_BIT);
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
    assembler_.Btq(x64::Immediate(JSFunctionBase::IsCompiledCodeBit::START_BIT), bitfield);
    assembler_.Jnb(target);
}

void ArkSteedAssembler::BranchIfNoPendingException(Label *target)
{
    TemporaryRegisterScope scope(this);
    ArkSteedRegister scratch = scope.AcquireScratch();
    Move(scratch, static_cast<uint64_t>(entryThread_->GetGlueAddr()));
    LoadField(scratch, scratch, static_cast<int32_t>(JSThread::GlueData::GetExceptionOffset(false)));
    Compare(scratch, static_cast<int32_t>(JSTaggedValue::VALUE_HOLE));
    JumpIf(Condition::EQUAL, target);
}

void ArkSteedAssembler::BranchIfPendingException(Label *target)
{
    TemporaryRegisterScope scope(this);
    ArkSteedRegister scratch = scope.AcquireScratch();
    Move(scratch, static_cast<uint64_t>(entryThread_->GetGlueAddr()));
    LoadField(scratch, scratch, static_cast<int32_t>(JSThread::GlueData::GetExceptionOffset(false)));
    Compare(scratch, static_cast<int32_t>(JSTaggedValue::VALUE_HOLE));
    JumpIf(Condition::NOT_EQUAL, target);
}

void ArkSteedAssembler::ReturnWithPendingException()
{
    assembler_.Movabs(JSTaggedValue::Exception().GetRawData(), x64::rax);
    Epilogue();
    Return();
}

void ArkSteedAssembler::LoadAndClearPendingException(ArkSteedRegister dst, ArkSteedRegister glue)
{
    TemporaryRegisterScope scope(this);
    ArkSteedRegister scratch = scope.AcquireScratch();
    size_t offset = JSThread::GlueData::GetExceptionOffset(false);  // false : isArch32 = false

    LoadField(dst, glue, static_cast<int32_t>(offset));
    assembler_.Movabs(JSTaggedValue::Hole().GetRawData(), scratch);
    StoreField(scratch, glue, static_cast<int32_t>(offset));
}

void ArkSteedAssembler::Bind(Label *label)
{
    assembler_.Bind(label);
}

// =============================================================================
// Call/Return
// =============================================================================

void ArkSteedAssembler::Call(ArkSteedRegister target)
{
    assembler_.Callq(target);
}

void ArkSteedAssembler::Call(Label *target)
{
    assembler_.Callq(target);
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
    assembler_.Pushq(reg);
}

void ArkSteedAssembler::Pop(ArkSteedRegister reg)
{
    assembler_.Popq(reg);
}

void ArkSteedAssembler::Push(ArkSteedDoubleRegister reg)
{
    assembler_.Subq(x64::Immediate(FRAME_SLOT_SIZE), x64::rsp);
    assembler_.Movsd(x64::Operand(x64::rsp, 0), reg);
}

void ArkSteedAssembler::Pop(ArkSteedDoubleRegister reg)
{
    assembler_.Movsd(reg, x64::Operand(x64::rsp, 0));
    assembler_.Addq(x64::Immediate(FRAME_SLOT_SIZE), x64::rsp);
}

void ArkSteedAssembler::PushAll(const ArkSteedRegList &registerList)
{
    for (ArkSteedRegister reg : registerList) {
        Push(reg);
    }
    if ((registerList.Count() & 1U) != 0) {
        ReserveCallArgSlots(1);
    }
}

void ArkSteedAssembler::PopAll(const ArkSteedRegList &registerList)
{
    if ((registerList.Count() & 1U) != 0) {
        FreeCallArgSlots(1);
    }
    ArkSteedRegList registers = registerList;
    while (!registers.IsEmpty()) {
        Pop(registers.PopLast());
    }
}

void ArkSteedAssembler::PushAll(const ArkDoubleRegList &registerList)
{
    for (ArkSteedDoubleRegister reg : registerList) {
        Push(reg);
    }
    if ((registerList.Count() & 1U) != 0) {
        ReserveCallArgSlots(1);
    }
}

void ArkSteedAssembler::PopAll(const ArkDoubleRegList &registerList)
{
    if ((registerList.Count() & 1U) != 0) {
        FreeCallArgSlots(1);
    }
    ArkDoubleRegList registers = registerList;
    while (!registers.IsEmpty()) {
        Pop(registers.PopLast());
    }
}

void ArkSteedAssembler::ReserveCallArgSlots(int32_t slotCount)
{
    if (slotCount > 0) {
        assembler_.Subq(x64::Immediate(slotCount * FRAME_SLOT_SIZE), x64::rsp);
    }
}

void ArkSteedAssembler::LoadCallArgv(ArkSteedRegister dst)
{
    assembler_.Movq(x64::rsp, dst);
}

void ArkSteedAssembler::FreeCallArgSlots(int32_t slotCount)
{
    if (slotCount > 0) {
        assembler_.Addq(x64::Immediate(slotCount * FRAME_SLOT_SIZE), x64::rsp);
    }
}

void ArkSteedAssembler::ReserveCallArgSlots(ArkSteedRegister slotCount)
{
    static constexpr int FRAME_SLOT_SIZE_LOG2 = 3;
    assembler_.Leaq(x64::Operand(slotCount, x64::Scale::Times8, 0), slotCount);
    assembler_.Subq(slotCount, x64::rsp);
    assembler_.Shrq(x64::Immediate(FRAME_SLOT_SIZE_LOG2), slotCount);
}

void ArkSteedAssembler::FreeCallArgSlots(ArkSteedRegister slotCount)
{
    assembler_.Leaq(x64::Operand(slotCount, x64::Scale::Times8, 0), slotCount);
    assembler_.Addq(slotCount, x64::rsp);
}

void ArkSteedAssembler::RestoreStackPointerToFrameBottom(Graph *graph)
{
    uint32_t taggedSlots = graph->GetTaggedStackSlots();
    uint32_t untaggedSlots = graph->GetUntaggedStackSlots();
    uint32_t frameSlots = 3 + taggedSlots + untaggedSlots;  // 3: frameType, jsFunc, lexicalEnv.
    if (((taggedSlots + untaggedSlots) & 1U) == 0) {
        frameSlots++;
    }
    assembler_.Leaq(x64::Operand(x64::rbp, -static_cast<int32_t>(frameSlots * FRAME_SLOT_SIZE)), x64::rsp);
}

void ArkSteedAssembler::PushUndefinedForSteedCall(ArkSteedRegister fillSlotCount, uint32_t userArgc)
{
    Label fillUndefined;
    Label fillDone;
    Compare(fillSlotCount, 0);
    JumpIf(Condition::LESS_THAN_OR_EQUAL, &fillDone);

    assembler_.Bind(&fillUndefined);
    constexpr int32_t SLOT_BEFORE_FIRST_OPTIONAL_ARG = NUM_MANDATORY_JSFUNC_ARGS;
    int32_t firstUndefinedArgBaseOffset =
        static_cast<int32_t>((SLOT_BEFORE_FIRST_OPTIONAL_ARG + userArgc) * FRAME_SLOT_SIZE);
    assembler_.Movq(x64::Immediate(static_cast<int32_t>(JSTaggedValue::VALUE_UNDEFINED)),
                    x64::Operand(x64::rsp, fillSlotCount, x64::Scale::Times8, firstUndefinedArgBaseOffset));
    Sub(fillSlotCount, 1);
    Compare(fillSlotCount, 0);
    JumpIf(Condition::GREATER_THAN, &fillUndefined);
    assembler_.Bind(&fillDone);
}

void ArkSteedAssembler::PushUndefinedForSteedSpreadCall(ArkSteedRegister fillSlotCount, ArkSteedRegister lastIndex)
{
    Label fillUndefined;
    Label fillDone;
    Compare(fillSlotCount, 0);
    JumpIf(Condition::LESS_THAN_OR_EQUAL, &fillDone);

    // Slot address = rsp + lastIndex*8 (single index register; the caller
    // pre-summed the mandatory prefix, the spread length and the fill offset).
    assembler_.Bind(&fillUndefined);
    assembler_.Movq(x64::Immediate(static_cast<int32_t>(JSTaggedValue::VALUE_UNDEFINED)),
                    x64::Operand(x64::rsp, lastIndex, x64::Scale::Times8, 0));
    Sub(lastIndex, 1);
    Sub(fillSlotCount, 1);
    Compare(fillSlotCount, 0);
    JumpIf(Condition::GREATER_THAN, &fillUndefined);
    assembler_.Bind(&fillDone);
}

void ArkSteedAssembler::CopySpreadElementsToCallArgSlots(ArkSteedRegister elements, ArkSteedRegister length,
                                                         int32_t firstUserArgSlot)
{
    TemporaryRegisterScope scope(this);
    ArkSteedRegister index = scope.Acquire();
    ArkSteedRegister elem = scope.Acquire();
    Label loop;
    Label notHole;
    Label done;
    Compare(length, 0);
    JumpIf(Condition::LESS_THAN_OR_EQUAL, &done);
    Move(index, 0);
    Bind(&loop);
    // elem = [elements + TaggedArray::DATA_OFFSET + index*8]
    assembler_.Movq(x64::Operand(elements, index, x64::Scale::Times8, static_cast<int32_t>(TaggedArray::DATA_OFFSET)),
                    elem);
    // Hole -> undefined (VALUE_HOLE = 0x05 fits an imm compare). Defensive:
    // the SUPERCALLSPREAD operand is the frontend-materialized spread result,
    // which the starrayspread admission (or its runtime arm) keeps hole-free.
    Compare(elem, static_cast<int64_t>(JSTaggedValue::VALUE_HOLE));
    JumpIf(Condition::NOT_EQUAL, &notHole);
    Move(elem, static_cast<uint64_t>(JSTaggedValue::VALUE_UNDEFINED));
    Bind(&notHole);
    // [rsp + index*8 + firstUserArgSlot*8] = elem
    assembler_.Movq(elem, x64::Operand(x64::rsp, index, x64::Scale::Times8, firstUserArgSlot * FRAME_SLOT_SIZE));
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
    // Same source slot as LoadActualArgument: skip saved fp, return address,
    // argc, and the mandatory JS-function arguments.
    constexpr int32_t FIRST_USER_ARG_SLOT = 3 + NUM_MANDATORY_JSFUNC_ARGS;
    Label loop;
    Label done;
    Compare(length, 0);
    JumpIf(Condition::LESS_THAN_OR_EQUAL, &done);
    Move(index, 0);
    Bind(&loop);
    // elem = [fp + index*8 + FIRST_USER_ARG_SLOT*8]
    assembler_.Movq(
        x64::Operand(kFramePointerRegister, index, x64::Scale::Times8, FIRST_USER_ARG_SLOT * FRAME_SLOT_SIZE), elem);
    // [rsp + index*8 + firstUserArgSlot*8] = elem
    assembler_.Movq(
        elem, x64::Operand(x64::rsp, index, x64::Scale::Times8, firstUserArgSlot * FRAME_SLOT_SIZE));
    Add(index, 1);
    Compare(index, length);
    JumpIf(Condition::LESS_THAN, &loop);
    Bind(&done);
}

void ArkSteedAssembler::PrepareSteedCalleeContext(ArkSteedRegister target, ArkSteedRegister codeEntry)
{
    Move(x64::r12, target);
    LoadField(x64::rbx, x64::r12, JSFunction::LEXICAL_ENV_OFFSET);
    LoadField(codeEntry, x64::r12, JSFunction::CODE_ENTRY_OFFSET);
}

// =============================================================================
// Function Prologue/Epilogue
// =============================================================================

void ArkSteedAssembler::Prologue(Graph *graph)
{
    RecordComment("Prologue");

    // 1. Set up fp.
    assembler_.Pushq(x64::rbp);
    assembler_.Movq(x64::rsp, x64::rbp);

    // 2. Build the SteedFunctionFrame fixed header:
    // [rbp - 8] = frameType, [rbp - 16] = jsFunc, [rbp - 24] = lexicalEnv.
    assembler_.Pushq(x64::Immediate(static_cast<int>(FrameType::STEED_FUNCTION_FRAME)));
    assembler_.Pushq(x64::r12);  // jsFunc
    assembler_.Pushq(x64::rbx);  // lexicalEnv

    // 3. Initialize tagged slots for GC safety.
    uint32_t taggedSlots = graph->GetTaggedStackSlots();
    taggedStackSlots_ = taggedSlots;
    if (taggedSlots > 0) {
        auto undefined = static_cast<int64_t>(JSTaggedValue::VALUE_UNDEFINED);
        assembler_.Movq(x64::Immediate(undefined), x64::rax);
        for (uint32_t i = 0; i < taggedSlots; ++i) {
            assembler_.Pushq(x64::rax);
        }
    }

    // 4. Allocate untagged slots.
    uint32_t untaggedSlots = graph->GetUntaggedStackSlots();
    if (untaggedSlots > 0) {
        assembler_.Subq(x64::Immediate(untaggedSlots * sizeof(uint64_t)), x64::rsp);
    }

    // SysV requires 16-byte alignment at call sites. The 3 fixed header slots
    // leave rsp misaligned when the number of local slots is even.
    if (((taggedSlots + untaggedSlots) & 1U) == 0) {
        assembler_.Subq(x64::Immediate(FRAME_SLOT_SIZE), x64::rsp);
    }

    SetHasFrame(true);
}

void ArkSteedAssembler::Epilogue()
{
    RecordComment("Epilogue");
    // LeaveFrame: mov rsp, rbp; pop rbp
    assembler_.Movq(x64::rbp, x64::rsp);
    assembler_.Popq(x64::rbp);
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
#endif
}  // namespace panda::ecmascript::arksteed
