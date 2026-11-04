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

// Unit domain closing matrix row G7.1u (ISSUE-0024): x64 eager-deopt ABI and
// frame layout.
//
// The domain is a static ABI contract. No EcmaVM, no graph and no generated
// code is created; only the compile-time constants and `constexpr` helpers of
// `ecmascript/arksteed/arksteed_deopt_abi.h` and the SysV argument-register
// order of `ArkSteedAssembler::GetParameterRegister` are observed.
//
// Expectations come from independent sources: the System V AMD64 argument
// register order, the x86-64 `CALL rel32` encoding, the architectural x86-64
// register codes, snapshot/frame size arithmetic and the allocatable register
// lists in `ecmascript/arksteed/arksteed_regalloc_types.h`. The production
// `static_assert`s of `arksteed_deopt_abi.h` are complemented by full-range
// enumeration and cross-representation checks instead of being repeated.

#include "ecmascript/arksteed/arksteed_assembler-inl.h"
#include "ecmascript/arksteed/arksteed_deopt_abi.h"
#include "ecmascript/tests/test_helper.h"

#include <array>
#include <cstdint>
#include <set>

namespace panda::test {

using namespace panda::ecmascript;
using namespace panda::ecmascript::arksteed;

#if defined(PANDA_TARGET_AMD64)

class ArkSteedDeoptAbiTest : public BaseTestWithOutScope {
};

namespace {

constexpr uint32_t kSlotBytes = static_cast<uint32_t>(sizeof(uint64_t));
// Architectural code space is [0, GENERAL_REGISTER_CODE_COUNT); this value never
// matches a general register and therefore selects "no mutation".
constexpr uint32_t kNoShiftedCode = ARKSTEED_DEOPT_GENERAL_REGISTER_CODE_COUNT;

// Representation 1 (allocatable register list, `arksteed_regalloc_types.h`)
// turned into a mask independently of the deopt ABI code list.
uint64_t GeneralMaskFromAllocatableRegisters()
{
    uint64_t mask = 0;
    for (uint32_t code = 0; code < ARKSTEED_DEOPT_GENERAL_REGISTER_CODE_COUNT; ++code) {
        ArkSteedRegister reg = ArkSteedRegister::FromCode(static_cast<int8_t>(code));
        if (GetAllocatableGeneralRegisters().Has(reg)) {
            mask |= 1ULL << code;
        }
    }
    return mask;
}

uint64_t FloatingMaskFromAllocatableRegisters()
{
    uint64_t mask = 0;
    for (uint32_t code = 0; code < ARKSTEED_DEOPT_FLOATING_REGISTER_CODE_COUNT; ++code) {
        ArkSteedDoubleRegister reg =
            RegListRegisterTraits<ArkSteedDoubleRegister>::FromCode(static_cast<int>(code));
        if (GetAllocatableDoubleRegisters().Has(reg)) {
            mask |= 1ULL << code;
        }
    }
    return mask;
}

uint32_t CountMaskBits(uint64_t mask)
{
    uint32_t count = 0;
    while (mask != 0) {
        count += static_cast<uint32_t>(mask & 1ULL);
        mask >>= 1;
    }
    return count;
}

// Detector shared by the positive cases and the negative control. It walks the
// whole architectural general code space: every allocatable register must own
// exactly one slot inside the general half of the snapshot. `shiftedCode` and
// `slotShift` model a mutated layout (the negative control shifts one register).
bool GeneralSnapshotLayoutIsConsistent(uint32_t shiftedCode, int32_t slotShift)
{
    std::set<uint32_t> slots;
    uint32_t allocatable = 0;
    for (uint32_t code = 0; code < ARKSTEED_DEOPT_GENERAL_REGISTER_CODE_COUNT; ++code) {
        ArkSteedRegister reg = ArkSteedRegister::FromCode(static_cast<int8_t>(code));
        if (!GetAllocatableGeneralRegisters().Has(reg)) {
            continue;
        }
        ++allocatable;
        int32_t offset = GetArkSteedDeoptGeneralSnapshotOffset(code);
        if (code == shiftedCode) {
            offset += slotShift * static_cast<int32_t>(kSlotBytes);
        }
        if (offset < 0 || offset % static_cast<int32_t>(kSlotBytes) != 0 ||
            static_cast<uint32_t>(offset) >= ARKSTEED_DEOPT_SNAPSHOT_SIZE) {
            return false;
        }
        uint32_t slot = static_cast<uint32_t>(offset) / kSlotBytes;
        if (slot >= ArkSteedDeoptSnapshot::GENERAL_SLOT_COUNT || !slots.insert(slot).second) {
            return false;
        }
    }
    return allocatable == ArkSteedDeoptSnapshot::GENERAL_SLOT_COUNT &&
           slots.size() == ArkSteedDeoptSnapshot::GENERAL_SLOT_COUNT;
}

bool FloatingSnapshotLayoutIsConsistent(uint32_t shiftedCode, int32_t slotShift)
{
    constexpr uint32_t kFloatingSlots = ArkSteedDeoptSnapshot::FLOATING_SLOT_COUNT;
    std::set<uint32_t> slots;
    uint32_t allocatable = 0;
    for (uint32_t code = 0; code < ARKSTEED_DEOPT_FLOATING_REGISTER_CODE_COUNT; ++code) {
        ArkSteedDoubleRegister reg =
            RegListRegisterTraits<ArkSteedDoubleRegister>::FromCode(static_cast<int>(code));
        if (!GetAllocatableDoubleRegisters().Has(reg)) {
            continue;
        }
        ++allocatable;
        int32_t offset = GetArkSteedDeoptFloatingSnapshotOffset(code);
        if (code == shiftedCode) {
            offset += slotShift * static_cast<int32_t>(kSlotBytes);
        }
        if (offset < 0 || offset % static_cast<int32_t>(kSlotBytes) != 0 ||
            static_cast<uint32_t>(offset) >= ARKSTEED_DEOPT_SNAPSHOT_SIZE) {
            return false;
        }
        uint32_t slot = static_cast<uint32_t>(offset) / kSlotBytes;
        if (slot < ArkSteedDeoptSnapshot::GENERAL_SLOT_COUNT ||
            slot >= ArkSteedDeoptSnapshot::GENERAL_SLOT_COUNT + kFloatingSlots ||
            !slots.insert(slot).second) {
            return false;
        }
    }
    return allocatable == kFloatingSlots && slots.size() == kFloatingSlots;
}

}  // namespace

/**
 * @tc.name: DeoptAbi_GeneralRegisterMaskMatchesAllocatableList
 * @tc.desc: the general deopt mask equals the register list cross-checked with
 *           the allocatable register list and the snapshot slot count.
 * @tc.type: FUNC
 */
HWTEST_F_L0(ArkSteedDeoptAbiTest, DeoptAbi_GeneralRegisterMaskMatchesAllocatableList)
{
    const uint64_t mask = GetArkSteedDeoptGeneralRegisterMask();
    const uint64_t derivedMask = GeneralMaskFromAllocatableRegisters();

    // Mask (representation 2) vs allocatable list (representation 1).
    EXPECT_EQ(mask, derivedMask);
    EXPECT_EQ(mask, static_cast<uint64_t>(GetAllocatableGeneralRegisters().Bits()));
    // Bit count of the mask vs the snapshot slot count of the layout.
    EXPECT_EQ(CountMaskBits(mask), ArkSteedDeoptSnapshot::GENERAL_SLOT_COUNT);
    EXPECT_EQ(GetAllocatableGeneralRegisters().Count(), ArkSteedDeoptSnapshot::GENERAL_SLOT_COUNT);
    // Register list (representation 3) vs both other representations.
    EXPECT_EQ(ARKSTEED_DEOPT_GENERAL_REGISTER_CODES.size(),
              static_cast<size_t>(ArkSteedDeoptSnapshot::GENERAL_SLOT_COUNT));
    std::set<uint32_t> listedCodes;
    for (uint32_t code : ARKSTEED_DEOPT_GENERAL_REGISTER_CODES) {
        EXPECT_LT(code, ARKSTEED_DEOPT_GENERAL_REGISTER_CODE_COUNT);
        EXPECT_TRUE(GetAllocatableGeneralRegisters().Has(ArkSteedRegister::FromCode(static_cast<int8_t>(code))))
            << "deopt register code " << code << " must be allocatable";
        EXPECT_TRUE(listedCodes.insert(code).second) << "duplicated deopt register code " << code;
        EXPECT_NE((mask & (1ULL << code)), 0ULL) << "deopt register code " << code << " missing from the mask";
    }
    EXPECT_EQ(listedCodes.size(), ARKSTEED_DEOPT_GENERAL_REGISTER_CODES.size());
}

/**
 * @tc.name: DeoptAbi_FloatingRegisterMaskMatchesAllocatableList
 * @tc.desc: the floating deopt mask equals the allocatable XMM list and the
 *           snapshot floating slot count.
 * @tc.type: FUNC
 */
HWTEST_F_L0(ArkSteedDeoptAbiTest, DeoptAbi_FloatingRegisterMaskMatchesAllocatableList)
{
    const uint64_t mask = GetArkSteedDeoptFloatingRegisterMask();
    EXPECT_EQ(mask, FloatingMaskFromAllocatableRegisters());
    EXPECT_EQ(mask, static_cast<uint64_t>(GetAllocatableDoubleRegisters().Bits()));
    EXPECT_EQ(CountMaskBits(mask), ArkSteedDeoptSnapshot::FLOATING_SLOT_COUNT);
    EXPECT_EQ(GetAllocatableDoubleRegisters().Count(), ArkSteedDeoptSnapshot::FLOATING_SLOT_COUNT);
    EXPECT_LT(GetAllocatableDoubleRegisters().Count(), ARKSTEED_DEOPT_FLOATING_REGISTER_CODE_COUNT);
    // No allocatable floating register may sit above the snapshot code space.
    for (uint32_t code = ARKSTEED_DEOPT_FLOATING_REGISTER_COUNT;
         code < ARKSTEED_DEOPT_FLOATING_REGISTER_CODE_COUNT; ++code) {
        ArkSteedDoubleRegister reg =
            RegListRegisterTraits<ArkSteedDoubleRegister>::FromCode(static_cast<int>(code));
        EXPECT_FALSE(GetAllocatableDoubleRegisters().Has(reg)) << "reserved floating code " << code;
    }
}

/**
 * @tc.name: DeoptAbi_GeneralSnapshotOffsetsFormCompletePermutation
 * @tc.desc: enumeration over all architectural GPR codes: each allocatable
 *           register has exactly one aligned snapshot offset inside the
 *           snapshot, the offsets are unique and cover the whole general half.
 * @tc.type: FUNC
 */
HWTEST_F_L0(ArkSteedDeoptAbiTest, DeoptAbi_GeneralSnapshotOffsetsFormCompletePermutation)
{
    EXPECT_TRUE(GeneralSnapshotLayoutIsConsistent(kNoShiftedCode, 0));

    std::set<uint32_t> slots;
    for (uint32_t code = 0; code < ARKSTEED_DEOPT_GENERAL_REGISTER_CODE_COUNT; ++code) {
        ArkSteedRegister reg = ArkSteedRegister::FromCode(static_cast<int8_t>(code));
        const int32_t offset = GetArkSteedDeoptGeneralSnapshotOffset(code);
        if (!GetAllocatableGeneralRegisters().Has(reg)) {
            EXPECT_EQ(offset, -1) << "reserved GPR code " << code << " must have no snapshot slot";
            continue;
        }
        ASSERT_GE(offset, 0) << "allocatable GPR code " << code << " missing a snapshot slot";
        EXPECT_EQ(offset % static_cast<int32_t>(kSlotBytes), 0) << "unaligned offset for GPR code " << code;
        EXPECT_LT(static_cast<uint32_t>(offset), ARKSTEED_DEOPT_SNAPSHOT_SIZE)
            << "offset outside the snapshot for GPR code " << code;
        const uint32_t slot = static_cast<uint32_t>(offset) / kSlotBytes;
        EXPECT_LT(slot, ArkSteedDeoptSnapshot::GENERAL_SLOT_COUNT)
            << "GPR code " << code << " leaves the general half of the snapshot";
        EXPECT_TRUE(slots.insert(slot).second) << "snapshot slot " << slot << " used twice";
    }
    EXPECT_EQ(slots.size(), ArkSteedDeoptSnapshot::GENERAL_SLOT_COUNT);
    for (uint32_t slot = 0; slot < ArkSteedDeoptSnapshot::GENERAL_SLOT_COUNT; ++slot) {
        EXPECT_EQ(slots.count(slot), 1U) << "general snapshot slot " << slot << " is not covered";
    }
}

/**
 * @tc.name: DeoptAbi_FloatingSnapshotOffsetsFormCompletePermutation
 * @tc.desc: enumeration over all architectural FP codes: each allocatable XMM
 *           register has exactly one aligned offset in the floating half.
 * @tc.type: FUNC
 */
HWTEST_F_L0(ArkSteedDeoptAbiTest, DeoptAbi_FloatingSnapshotOffsetsFormCompletePermutation)
{
    EXPECT_TRUE(FloatingSnapshotLayoutIsConsistent(kNoShiftedCode, 0));

    std::set<uint32_t> slots;
    for (uint32_t code = 0; code < ARKSTEED_DEOPT_FLOATING_REGISTER_CODE_COUNT; ++code) {
        ArkSteedDoubleRegister reg =
            RegListRegisterTraits<ArkSteedDoubleRegister>::FromCode(static_cast<int>(code));
        const int32_t offset = GetArkSteedDeoptFloatingSnapshotOffset(code);
        if (!GetAllocatableDoubleRegisters().Has(reg)) {
            EXPECT_EQ(offset, -1) << "reserved FP code " << code << " must have no snapshot slot";
            continue;
        }
        ASSERT_GE(offset, 0) << "allocatable FP code " << code << " missing a snapshot slot";
        EXPECT_EQ(offset % static_cast<int32_t>(kSlotBytes), 0) << "unaligned offset for FP code " << code;
        EXPECT_LT(static_cast<uint32_t>(offset), ARKSTEED_DEOPT_SNAPSHOT_SIZE)
            << "offset outside the snapshot for FP code " << code;
        const uint32_t slot = static_cast<uint32_t>(offset) / kSlotBytes;
        EXPECT_GE(slot, ArkSteedDeoptSnapshot::GENERAL_SLOT_COUNT)
            << "FP code " << code << " overlaps the general half";
        EXPECT_LT(slot, ArkSteedDeoptSnapshot::GENERAL_SLOT_COUNT + ArkSteedDeoptSnapshot::FLOATING_SLOT_COUNT)
            << "FP code " << code << " leaves the snapshot";
        EXPECT_TRUE(slots.insert(slot).second) << "snapshot slot " << slot << " used twice";
    }
    EXPECT_EQ(slots.size(), ArkSteedDeoptSnapshot::FLOATING_SLOT_COUNT);
}

/**
 * @tc.name: DeoptAbi_EntryRegistersAreReservedAndDistinct
 * @tc.desc: the eager-deopt entry target/glue registers are reserved, distinct,
 *           outside the snapshot and never argument registers.
 * @tc.type: FUNC
 */
HWTEST_F_L0(ArkSteedDeoptAbiTest, DeoptAbi_EntryRegistersAreReservedAndDistinct)
{
    constexpr ArkSteedRegister kEntryTarget = ARKSTEED_EAGER_DEOPT_ENTRY_TARGET_REGISTER;
    constexpr ArkSteedRegister kEntryGlue = ARKSTEED_EAGER_DEOPT_ENTRY_GLUE_REGISTER;

    // Independent oracle: architectural x86-64 codes r10 = 10, r13 = 13.
    EXPECT_EQ(static_cast<int32_t>(kEntryTarget.Code()), 10) << "entry target must be r10";
    EXPECT_EQ(static_cast<int32_t>(kEntryGlue.Code()), 13) << "entry glue must be r13";
    EXPECT_NE(kEntryTarget.Code(), kEntryGlue.Code());
    EXPECT_EQ(kEntryTarget.Code(), x64::r10.Code());
    EXPECT_EQ(kEntryGlue.Code(), x64::r13.Code());
    EXPECT_FALSE(GetAllocatableGeneralRegisters().Has(kEntryTarget));
    EXPECT_FALSE(GetAllocatableGeneralRegisters().Has(kEntryGlue));
    // Reserved entry registers have no snapshot slot.
    EXPECT_EQ(GetArkSteedDeoptGeneralSnapshotOffset(static_cast<uint32_t>(kEntryTarget.Code())), -1);
    EXPECT_EQ(GetArkSteedDeoptGeneralSnapshotOffset(static_cast<uint32_t>(kEntryGlue.Code())), -1);
    // They are never argument registers.
    for (int i = 0; i < ArkSteedAssembler::NUM_ARG_REGISTERS; ++i) {
        EXPECT_NE(ArkSteedAssembler::GetParameterRegister(i).Code(), kEntryTarget.Code());
        EXPECT_NE(ArkSteedAssembler::GetParameterRegister(i).Code(), kEntryGlue.Code());
    }
}

/**
 * @tc.name: DeoptAbi_FrameSizesMatchComponentsAndAlignment
 * @tc.desc: snapshot, entry frame and veneer frame sizes equal the sum of their
 *           components; snapshot and entry frame keep the 16-byte alignment and
 *           the floating half starts slot-aligned after the general half.
 * @tc.type: FUNC
 */
HWTEST_F_L0(ArkSteedDeoptAbiTest, DeoptAbi_FrameSizesMatchComponentsAndAlignment)
{
    constexpr uint32_t kAlignment = static_cast<uint32_t>(ARKSTEED_EAGER_DEOPT_STACK_ALIGNMENT);
    constexpr uint32_t kSlot = static_cast<uint32_t>(ARKSTEED_EAGER_DEOPT_SLOT_SIZE);
    EXPECT_EQ(kAlignment, 16U);  // 16U: bytes of stack alignment
    EXPECT_EQ(kSlot, kSlotBytes);
    EXPECT_EQ(alignof(ArkSteedDeoptSnapshot), kAlignment);

    constexpr uint32_t kSnapshotSlots =
        ArkSteedDeoptSnapshot::GENERAL_SLOT_COUNT + ArkSteedDeoptSnapshot::FLOATING_SLOT_COUNT;
    constexpr uint32_t kRawSnapshotSize = kSnapshotSlots * kSlot;
    constexpr uint32_t kExpectedSnapshotSize = (kRawSnapshotSize + kAlignment - 1U) & ~(kAlignment - 1U);
    EXPECT_EQ(ARKSTEED_DEOPT_SNAPSHOT_SIZE, kExpectedSnapshotSize);
    EXPECT_EQ(ARKSTEED_DEOPT_SNAPSHOT_SIZE, x64_eager_deopt_abi::SNAPSHOT_SIZE);
    EXPECT_EQ(ARKSTEED_DEOPT_SNAPSHOT_SIZE % kAlignment, 0U);

    // The floating half starts right after the general half and is slot-aligned.
    EXPECT_EQ(ARKSTEED_DEOPT_FLOATING_SNAPSHOT_OFFSET,
              ArkSteedDeoptSnapshot::GENERAL_SLOT_COUNT * kSlot);
    EXPECT_EQ(ARKSTEED_DEOPT_FLOATING_SNAPSHOT_OFFSET % kSlot, 0U);

    // Global entry frame = snapshot + native metadata (glue plus SysV alignment).
    EXPECT_EQ(ARKSTEED_EAGER_DEOPT_ENTRY_METADATA_SIZE, 2U * kSlot);
    EXPECT_EQ(ARKSTEED_EAGER_DEOPT_ENTRY_FRAME_SIZE,
              ARKSTEED_DEOPT_SNAPSHOT_SIZE + ARKSTEED_EAGER_DEOPT_ENTRY_METADATA_SIZE);
    EXPECT_EQ(ARKSTEED_EAGER_DEOPT_ENTRY_FRAME_SIZE,
              ARKSTEED_DEOPT_SNAPSHOT_SIZE + 2U * kSlot);  // 2U: two extra slots past the snapshot
    EXPECT_EQ(ARKSTEED_EAGER_DEOPT_ENTRY_FRAME_SIZE, x64_eager_deopt_abi::ENTRY_FRAME_SIZE);
    EXPECT_EQ(ARKSTEED_EAGER_DEOPT_ENTRY_FRAME_SIZE % kAlignment, 0U);

    // The x64 veneer frame is the single CALL continuation slot; together with
    // the return address pushed by `CALL rel32` the veneer region keeps the
    // 16-byte stack alignment.
    EXPECT_EQ(ARKSTEED_EAGER_DEOPT_VENEER_FRAME_SIZE, ARKSTEED_EAGER_DEOPT_FIXED_EXIT_LINK_SIZE);
    EXPECT_EQ(ARKSTEED_EAGER_DEOPT_VENEER_FRAME_SIZE, x64_eager_deopt_abi::VENEER_FRAME_SIZE);
    EXPECT_EQ(ARKSTEED_EAGER_DEOPT_VENEER_FRAME_SIZE, kSlot);
    EXPECT_EQ((ARKSTEED_EAGER_DEOPT_VENEER_FRAME_SIZE + ARKSTEED_EAGER_DEOPT_FIXED_EXIT_LINK_SIZE) % kAlignment, 0U);
}

/**
 * @tc.name: DeoptAbi_SysVX64ArgumentRegisterOrder
 * @tc.desc: NUM_ARG_REGISTERS is 6 and GetParameterRegister reproduces the
 *           System V AMD64 order rdi, rsi, rdx, rcx, r8, r9.
 * @tc.type: FUNC
 */
HWTEST_F_L0(ArkSteedDeoptAbiTest, DeoptAbi_SysVX64ArgumentRegisterOrder)
{
    // Independent oracle: SysV AMD64 integer argument registers, by
    // architectural code (rdi=7, rsi=6, rdx=2, rcx=1, r8=8, r9=9).
    constexpr std::array<int32_t, 6> kExpectedCodes = {7, 6, 2, 1, 8, 9};
    constexpr std::array<ArkSteedRegister, 6> kExpectedRegisters = {
        x64::rdi, x64::rsi, x64::rdx, x64::rcx, x64::r8, x64::r9};

    EXPECT_EQ(ArkSteedAssembler::NUM_ARG_REGISTERS, 6);  // 6: argument registers
    EXPECT_EQ(kExpectedCodes.size(), static_cast<size_t>(ArkSteedAssembler::NUM_ARG_REGISTERS));
    for (int i = 0; i < ArkSteedAssembler::NUM_ARG_REGISTERS; ++i) {
        ArkSteedRegister reg = ArkSteedAssembler::GetParameterRegister(i);
        EXPECT_EQ(static_cast<int32_t>(reg.Code()), kExpectedCodes[static_cast<size_t>(i)])
            << "argument register " << i;
        EXPECT_EQ(reg.Code(), kExpectedRegisters[static_cast<size_t>(i)].Code()) << "argument register " << i;
        EXPECT_TRUE(GetAllocatableGeneralRegisters().Has(reg)) << "argument register " << i << " must be allocatable";
    }
    for (int i = 0; i < ArkSteedAssembler::NUM_ARG_REGISTERS; ++i) {
        for (int j = i + 1; j < ArkSteedAssembler::NUM_ARG_REGISTERS; ++j) {
            EXPECT_NE(ArkSteedAssembler::GetParameterRegister(i).Code(),
                      ArkSteedAssembler::GetParameterRegister(j).Code());
        }
    }
    for (int i = 0; i < ArkSteedAssembler::NUM_ARG_REGISTERS; ++i) {
        const int32_t slot = GetArkSteedDeoptGeneralSnapshotOffset(
            static_cast<uint32_t>(ArkSteedAssembler::GetParameterRegister(i).Code()));
        EXPECT_GE(slot, 0) << "argument register " << i << " must have a snapshot slot";
    }
}

/**
 * @tc.name: DeoptAbi_X64ExitEncoding
 * @tc.desc: the x64 fixed-exit encoding is a 5-byte CALL rel32 and the return
 *           PC lives on the stack (no link register).
 * @tc.type: FUNC
 */
HWTEST_F_L0(ArkSteedDeoptAbiTest, DeoptAbi_X64ExitEncoding)
{
    // Independent oracle: x86-64 `CALL rel32` is 5 bytes and pushes an 8-byte
    // return address on the stack.
    EXPECT_EQ(ARKSTEED_EAGER_DEOPT_EXIT_SIZE, 5U);
    EXPECT_EQ(ARKSTEED_EAGER_DEOPT_EXIT_SIZE, x64_eager_deopt_abi::EXIT_SIZE);
    EXPECT_EQ(ARKSTEED_EAGER_DEOPT_RETURN_PC_SOURCE, ArkSteedEagerDeoptReturnPcSource::STACK_LINK);
    EXPECT_NE(ARKSTEED_EAGER_DEOPT_RETURN_PC_SOURCE, ArkSteedEagerDeoptReturnPcSource::LINK_REGISTER);
    EXPECT_EQ(ARKSTEED_EAGER_DEOPT_FIXED_EXIT_LINK_SIZE, kSlotBytes);
}

/**
 * @tc.name: DeoptAbi_MutationOfExpectedLayoutIsDetected
 * @tc.desc: negative control (P0, H6): the detectors used by the positive cases
 *           reject a mutated expected mask and a shifted snapshot slot, so a
 *           broken ABI layout cannot pass silently.
 * @tc.type: FUNC
 */
HWTEST_F_L0(ArkSteedDeoptAbiTest, DeoptAbi_MutationOfExpectedLayoutIsDetected)
{
    const uint64_t generalMask = GetArkSteedDeoptGeneralRegisterMask();
    const uint64_t floatingMask = GetArkSteedDeoptFloatingRegisterMask();

    // Positive control: the production masks are accepted by the comparison.
    EXPECT_EQ(generalMask, GeneralMaskFromAllocatableRegisters());
    EXPECT_EQ(floatingMask, FloatingMaskFromAllocatableRegisters());
    // Flipping one expected bit (rax, code 0) must turn the comparison red.
    EXPECT_NE(generalMask, (GeneralMaskFromAllocatableRegisters() ^ (1ULL << 0)));
    // Substituting the other register class must also be rejected.
    EXPECT_NE(generalMask, FloatingMaskFromAllocatableRegisters());

    // Shift of one snapshot slot: rax (slot 0) collides with rbx (slot 1).
    EXPECT_TRUE(GeneralSnapshotLayoutIsConsistent(kNoShiftedCode, 0));
    EXPECT_FALSE(GeneralSnapshotLayoutIsConsistent(0U, 1));
    // Shift out of the general half: the slot leaves its region.
    EXPECT_FALSE(GeneralSnapshotLayoutIsConsistent(
        0U, static_cast<int32_t>(ArkSteedDeoptSnapshot::GENERAL_SLOT_COUNT)));

    EXPECT_TRUE(FloatingSnapshotLayoutIsConsistent(kNoShiftedCode, 0));
    EXPECT_FALSE(FloatingSnapshotLayoutIsConsistent(0U, 1));
}

#endif  // PANDA_TARGET_AMD64

}  // namespace panda::test
