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

#include <cstdint>
#include <cstring>
#include <optional>
#include <vector>

#include "ecmascript/arksteed/arksteed_safepoint_table.h"
#include "ecmascript/deoptimizer/deoptimizer.h"
#include "ecmascript/tests/test_helper.h"

using namespace panda::ecmascript;
using namespace panda::ecmascript::arksteed;

namespace panda::test {

namespace {

// Documented binary layout of `arksteed_safepoint_table.h`. The field set, the
// header/entry sizes and the mask split are stated by that header's "Binary
// layout" comment and its `static_assert`s; the in-entry field order, and
// therefore the byte offsets used below, is the one of `struct
// ArkSteedSafepointEntry` in the same header, because `Emit()` copies that
// struct into the buffer while the reader reinterprets the buffer as it.
//
//   Header (16 bytes):
//     uint32_t numEntries
//     uint32_t numTaggedSlots
//     uint32_t numUntaggedSlots
//     uint32_t deoptLiteralCount
//   Entry[] (12 bytes each, sorted by pcOffset ascending):
//     uint32_t pcOffset
//     uint32_t deoptOffset
//     uint16_t extraSpillSlotsAndFlags
//     uint16_t deoptNum
//
// Every expected byte count and byte offset in this file is derived from this
// declared layout by hand; the table reader is then required to recover the
// exact same fields from the emitted bytes (independent oracle, no value is
// copied out of the production expression under test).
constexpr size_t HEADER_BYTES = 16;
constexpr size_t ENTRY_BYTES = 12;
constexpr size_t HEADER_OFFSET_TAGGED_SLOTS = 4;
constexpr size_t HEADER_OFFSET_UNTAGGED_SLOTS = 8;
constexpr size_t HEADER_OFFSET_DEOPT_LITERALS = 12;
constexpr size_t ENTRY_OFFSET_PC = 0;
constexpr size_t ENTRY_OFFSET_DEOPT_OFFSET = 4;
constexpr size_t ENTRY_OFFSET_FLAGS = 8;
constexpr size_t ENTRY_OFFSET_DEOPT_NUM = 10;

constexpr uint16_t SPILL_SLOTS_MAX = 0x1FFF;
constexpr uint16_t HANDLER_KIND_MASK = 0x6000;
constexpr uint16_t HANDLER_KIND_SHIFT = 13;

// Fresh allocator-backed chunk; the builder allocates from it. No VM is created.
struct ChunkFixture {
    NativeAreaAllocator allocator;
    Chunk chunk {&allocator};
};

uint16_t ReadU16LE(const uint8_t *data)
{
    return static_cast<uint16_t>(static_cast<uint16_t>(data[0]) |
                                 (static_cast<uint16_t>(data[1]) << 8));  // 8: second byte shift
}

uint32_t ReadU32LE(const uint8_t *data)
{
    return static_cast<uint32_t>(data[0]) | (static_cast<uint32_t>(data[1]) << 8) |   // 8, 16: byte shifts
           (static_cast<uint32_t>(data[2]) << 16) | (static_cast<uint32_t>(data[3]) << 24);  // 16, 24: shifts
}

void WriteU16LE(uint8_t *data, uint16_t value)
{
    data[0] = static_cast<uint8_t>(value & 0xFFU);          // 0xFF: low byte mask
    data[1] = static_cast<uint8_t>((value >> 8) & 0xFFU);   // 8, 0xFF: high byte
}

void WriteU32LE(uint8_t *data, uint32_t value)
{
    data[0] = static_cast<uint8_t>(value & 0xFFU);
    data[1] = static_cast<uint8_t>((value >> 8) & 0xFFU);    // 8: byte shift
    data[2] = static_cast<uint8_t>((value >> 16) & 0xFFU);   // 2: u32 byte index; 16: byte shift
    data[3] = static_cast<uint8_t>((value >> 24) & 0xFFU);   // 3: u32 byte index; 24: byte shift
}

uint8_t *EntryAt(uint8_t *buffer, size_t index)
{
    return buffer + HEADER_BYTES + index * ENTRY_BYTES;
}

// Deopt value fixtures. All magnitudes stay below 64 so that every encoded
// field occupies exactly one SLEB128 byte: that makes the hand-counted byte
// sizes below a true independent oracle for GetTableSize().
ArkSteedDeoptValue ConstantDeopt(int16_t id, int64_t value)
{
    return ArkSteedDeoptValue {id, ArkSteedDeoptValueKind::CONSTANT, value,
                               kungfu::LLVMStackMapType::INVALID_DWARF_REG};
}

ArkSteedDeoptValue StackSlotDeopt(int16_t id, int reg, int64_t frameOffset)
{
    return ArkSteedDeoptValue {id, ArkSteedDeoptValueKind::STACK_SLOT, frameOffset,
                               static_cast<kungfu::LLVMStackMapType::DwarfRegType>(reg)};
}

ArkSteedDeoptValue HeapLiteralDeopt(int16_t id, int64_t literalIndex)
{
    return ArkSteedDeoptValue {id, ArkSteedDeoptValueKind::HEAP_LITERAL, literalIndex,
                               kungfu::LLVMStackMapType::INVALID_DWARF_REG};
}

int HandlerKindCode(ExceptionHandlerKind kind)
{
    return static_cast<int>(static_cast<uint16_t>(kind));
}

// Hand-built table bytes per the documented layout, used as an oracle that does
// not go through the builder at all.
std::vector<uint8_t> HandBuiltSingleEntryTable(uint32_t pcOffset, uint16_t extraSpillSlotsAndFlags,
                                               uint16_t deoptNum, uint32_t deoptOffset)
{
    std::vector<uint8_t> buffer(HEADER_BYTES + ENTRY_BYTES, 0);
    WriteU32LE(buffer.data(), 1U);                                          // numEntries
    WriteU32LE(buffer.data() + HEADER_OFFSET_TAGGED_SLOTS, 6U);             // numTaggedSlots
    WriteU32LE(buffer.data() + HEADER_OFFSET_UNTAGGED_SLOTS, 1U);           // numUntaggedSlots
    WriteU32LE(buffer.data() + HEADER_OFFSET_DEOPT_LITERALS, 0U);           // deoptLiteralCount
    uint8_t *entry = EntryAt(buffer.data(), 0);
    WriteU32LE(entry + ENTRY_OFFSET_PC, pcOffset);
    WriteU32LE(entry + ENTRY_OFFSET_DEOPT_OFFSET, deoptOffset);
    WriteU16LE(entry + ENTRY_OFFSET_FLAGS, extraSpillSlotsAndFlags);
    WriteU16LE(entry + ENTRY_OFFSET_DEOPT_NUM, deoptNum);
    return buffer;
}

}  // namespace

// ============================================================================
// Helper: construct a safepoint table with one entry that has deopt data
// containing a CONSTANT value for a given vreg.
// ============================================================================
class SafepointTableBuilder {
public:
    static void AppendLeb128(std::vector<uint8_t> &buf, int64_t value)
    {
        bool more = true;
        while (more) {
            uint8_t byte = static_cast<uint8_t>(value & 0x7F);  // 0x7F: 7 payload bits per LEB128 byte
            value >>= 7;  // 7: payload bits per byte
            if ((value == 0 && (byte & 0x40) == 0) || (value == -1 && (byte & 0x40) != 0)) {
                more = false;
            } else {
                byte |= 0x80;  // 0x80: continuation bit
            }
            buf.push_back(byte);
        }
    }

    static std::vector<uint8_t> BuildOneEntry(uint32_t pcOffset, int32_t vreg,
                                               int64_t constantValue, uint16_t deoptNum = 2)
    {
        std::vector<uint8_t> buf;
        buf.reserve(64);

        // Header (16 bytes): numEntries, numTaggedSlots, numUntaggedSlots, deoptLiteralCount
        uint32_t numEntries = 1;
        uint32_t zero32 = 0;
        buf.insert(buf.end(), reinterpret_cast<const uint8_t*>(&numEntries),
                   reinterpret_cast<const uint8_t*>(&numEntries) + sizeof(numEntries));
        buf.insert(buf.end(), reinterpret_cast<const uint8_t*>(&zero32),
                   reinterpret_cast<const uint8_t*>(&zero32) + sizeof(zero32));
        buf.insert(buf.end(), reinterpret_cast<const uint8_t*>(&zero32),
                   reinterpret_cast<const uint8_t*>(&zero32) + sizeof(zero32));
        buf.insert(buf.end(), reinterpret_cast<const uint8_t*>(&zero32),
                   reinterpret_cast<const uint8_t*>(&zero32) + sizeof(zero32));

        // Entry (12 bytes): pcOffset, deoptOffset, extraSpillSlotsAndFlags, deoptNum
        uint32_t deoptOffset = sizeof(ArkSteedSafepointHeader) +
            sizeof(ArkSteedSafepointEntry);
        uint16_t extraSpillSlotsAndFlags = 0;
        buf.insert(buf.end(), reinterpret_cast<const uint8_t*>(&pcOffset),
                   reinterpret_cast<const uint8_t*>(&pcOffset) + sizeof(pcOffset));
        buf.insert(buf.end(), reinterpret_cast<const uint8_t*>(&deoptOffset),
                   reinterpret_cast<const uint8_t*>(&deoptOffset) + sizeof(deoptOffset));
        buf.insert(buf.end(), reinterpret_cast<const uint8_t*>(&extraSpillSlotsAndFlags),
                   reinterpret_cast<const uint8_t*>(&extraSpillSlotsAndFlags) + sizeof(extraSpillSlotsAndFlags));
        buf.insert(buf.end(), reinterpret_cast<const uint8_t*>(&deoptNum),
                   reinterpret_cast<const uint8_t*>(&deoptNum) + sizeof(deoptNum));

        // Deopt data: vregAndKind, value (both leb128)
        if (deoptNum > 0) {
            // 4: kind count, packs the DeoptValueKind into the low bits
            int64_t vregAndKind = static_cast<int64_t>(vreg) * 4 +
                static_cast<int64_t>(ArkSteedDeoptValueKind::CONSTANT);
            AppendLeb128(buf, vregAndKind);
            AppendLeb128(buf, constantValue);
        }

        return buf;
    }

    static std::vector<uint8_t> BuildMultiEntry()
    {
        std::vector<uint8_t> buf;
        buf.reserve(128);

        uint32_t numEntries = 3;
        uint32_t zero32 = 0;
        buf.insert(buf.end(), reinterpret_cast<const uint8_t*>(&numEntries),
                   reinterpret_cast<const uint8_t*>(&numEntries) + sizeof(numEntries));
        buf.insert(buf.end(), reinterpret_cast<const uint8_t*>(&zero32),
                   reinterpret_cast<const uint8_t*>(&zero32) + sizeof(zero32));
        buf.insert(buf.end(), reinterpret_cast<const uint8_t*>(&zero32),
                   reinterpret_cast<const uint8_t*>(&zero32) + sizeof(zero32));
        buf.insert(buf.end(), reinterpret_cast<const uint8_t*>(&zero32),
                   reinterpret_cast<const uint8_t*>(&zero32) + sizeof(zero32));

        // Entry 0: pcOffset=50, no deopt
        uint32_t pc0 = 50;
        uint32_t deoptOffset0 = 0;
        uint16_t deoptNum0 = 0;
        uint16_t flags0 = 0;
        buf.insert(buf.end(), reinterpret_cast<const uint8_t*>(&pc0),
                   reinterpret_cast<const uint8_t*>(&pc0) + sizeof(pc0));
        buf.insert(buf.end(), reinterpret_cast<const uint8_t*>(&deoptOffset0),
                   reinterpret_cast<const uint8_t*>(&deoptOffset0) + sizeof(deoptOffset0));
        buf.insert(buf.end(), reinterpret_cast<const uint8_t*>(&flags0),
                   reinterpret_cast<const uint8_t*>(&flags0) + sizeof(flags0));
        buf.insert(buf.end(), reinterpret_cast<const uint8_t*>(&deoptNum0),
                   reinterpret_cast<const uint8_t*>(&deoptNum0) + sizeof(deoptNum0));

        // Entry 1: pcOffset=100, has deopt with PC_OFFSET_INDEX = 42
        uint32_t pc1 = 100;
        // 3: entries preceding this one's deopt data
        uint32_t deoptOffset1 = sizeof(ArkSteedSafepointHeader) + 3 * sizeof(ArkSteedSafepointEntry);
        uint16_t deoptNum1 = 2;
        uint16_t flags1 = 0;
        buf.insert(buf.end(), reinterpret_cast<const uint8_t*>(&pc1),
                   reinterpret_cast<const uint8_t*>(&pc1) + sizeof(pc1));
        buf.insert(buf.end(), reinterpret_cast<const uint8_t*>(&deoptOffset1),
                   reinterpret_cast<const uint8_t*>(&deoptOffset1) + sizeof(deoptOffset1));
        buf.insert(buf.end(), reinterpret_cast<const uint8_t*>(&flags1),
                   reinterpret_cast<const uint8_t*>(&flags1) + sizeof(flags1));
        buf.insert(buf.end(), reinterpret_cast<const uint8_t*>(&deoptNum1),
                   reinterpret_cast<const uint8_t*>(&deoptNum1) + sizeof(deoptNum1));

        // Entry 2: pcOffset=200, no deopt
        uint32_t pc2 = 200;
        uint32_t deoptOffset2 = 0;
        uint16_t deoptNum2 = 0;
        uint16_t flags2 = 0;
        buf.insert(buf.end(), reinterpret_cast<const uint8_t*>(&pc2),
                   reinterpret_cast<const uint8_t*>(&pc2) + sizeof(pc2));
        buf.insert(buf.end(), reinterpret_cast<const uint8_t*>(&deoptOffset2),
                   reinterpret_cast<const uint8_t*>(&deoptOffset2) + sizeof(deoptOffset2));
        buf.insert(buf.end(), reinterpret_cast<const uint8_t*>(&flags2),
                   reinterpret_cast<const uint8_t*>(&flags2) + sizeof(flags2));
        buf.insert(buf.end(), reinterpret_cast<const uint8_t*>(&deoptNum2),
                   reinterpret_cast<const uint8_t*>(&deoptNum2) + sizeof(deoptNum2));

        // Deopt data for entry 1: vreg = PC_OFFSET_INDEX(-2), kind = CONSTANT, value = 42
        int64_t vregAndKind = static_cast<int64_t>(SpecVregIndex::PC_OFFSET_INDEX) * 4 +
            static_cast<int64_t>(ArkSteedDeoptValueKind::CONSTANT);
        AppendLeb128(buf, vregAndKind);
        AppendLeb128(buf, 42);

        return buf;
    }

    static std::vector<uint8_t> BuildGcOnlySafepoint(uint32_t pcOffset)
    {
        return BuildOneEntry(pcOffset, 0, 0, 0); // deoptNum=0
    }
};

// Position table builder for test
class PositionTableBuilder {
public:
    static std::vector<uint8_t> Build(const std::vector<std::pair<uint32_t, uint32_t>> &entries)
    {
        std::vector<uint8_t> buf;
        uint32_t numEntries = static_cast<uint32_t>(entries.size());
        buf.insert(buf.end(), reinterpret_cast<const uint8_t*>(&numEntries),
                   reinterpret_cast<const uint8_t*>(&numEntries) + sizeof(numEntries));
        for (const auto &[pc, bc] : entries) {
            buf.insert(buf.end(), reinterpret_cast<const uint8_t*>(&pc),
                       reinterpret_cast<const uint8_t*>(&pc) + sizeof(pc));
            buf.insert(buf.end(), reinterpret_cast<const uint8_t*>(&bc),
                       reinterpret_cast<const uint8_t*>(&bc) + sizeof(bc));
        }
        return buf;
    }
};

// ============================================================================
// Test suite
// ============================================================================
class ArkSteedSafepointTableTest : public BaseTestWithScope<false> {
};

// --- ExtractBcOffsetFromSafepointTable ---

/**
 * @tc.name: ExactMatchWithDeopt
 * @tc.desc: Exact match on a safepoint entry with PC_OFFSET_INDEX CONSTANT deopt
 * @tc.type: FUNC
 */
HWTEST_F_L0(ArkSteedSafepointTableTest, ExactMatchWithDeopt)
{
    auto table = SafepointTableBuilder::BuildOneEntry(
        100,
        static_cast<int32_t>(SpecVregIndex::PC_OFFSET_INDEX),
        42);

    auto result = ExtractBcOffsetFromSafepointTable(table.data(), table.size(), 100);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), 42U);
}

/**
 * @tc.name: MultiEntryExactMatch
 * @tc.desc: Multiple entries, find the one with deopt at exact pcOffset
 * @tc.type: FUNC
 */
HWTEST_F_L0(ArkSteedSafepointTableTest, MultiEntryExactMatch)
{
    auto table = SafepointTableBuilder::BuildMultiEntry();
    auto result = ExtractBcOffsetFromSafepointTable(table.data(), table.size(), 100);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), 42U);
}

/**
 * @tc.name: GcOnlySafepointReturnsNullopt
 * @tc.desc: GC-only safepoint (deoptNum=0) should return nullopt
 * @tc.type: FUNC
 */
HWTEST_F_L0(ArkSteedSafepointTableTest, GcOnlySafepointReturnsNullopt)
{
    auto table = SafepointTableBuilder::BuildGcOnlySafepoint(50);
    auto result = ExtractBcOffsetFromSafepointTable(table.data(), table.size(), 50);
    EXPECT_FALSE(result.has_value());
}

/**
 * @tc.name: PcOffsetNotFound
 * @tc.desc: pcOffset not in table should return nullopt
 * @tc.type: FUNC
 */
HWTEST_F_L0(ArkSteedSafepointTableTest, PcOffsetNotFound)
{
    auto table = SafepointTableBuilder::BuildOneEntry(
        100, static_cast<int32_t>(SpecVregIndex::PC_OFFSET_INDEX), 42);
    auto result = ExtractBcOffsetFromSafepointTable(table.data(), table.size(), 999);
    EXPECT_FALSE(result.has_value());
}

/**
 * @tc.name: EmptyTable
 * @tc.desc: Empty table (numEntries=0) should return nullopt
 * @tc.type: FUNC
 */
HWTEST_F_L0(ArkSteedSafepointTableTest, EmptyTable)
{
    std::vector<uint8_t> empty(16, 0);  // 16: header bytes only, no entries
    auto result = ExtractBcOffsetFromSafepointTable(empty.data(), empty.size(), 0);
    EXPECT_FALSE(result.has_value());
}

/**
 * @tc.name: NullData
 * @tc.desc: Null data should return nullopt
 * @tc.type: FUNC
 */
HWTEST_F_L0(ArkSteedSafepointTableTest, NullData)
{
    auto result = ExtractBcOffsetFromSafepointTable(nullptr, 0, 100);
    EXPECT_FALSE(result.has_value());
}

/**
 * @tc.name: TruncatedData
 * @tc.desc: Truncated data (header only) should return nullopt
 * @tc.type: FUNC
 */
HWTEST_F_L0(ArkSteedSafepointTableTest, TruncatedData)
{
    std::vector<uint8_t> truncated(8, 0);  // 8: half of the 16-byte header
    auto result = ExtractBcOffsetFromSafepointTable(truncated.data(), truncated.size(), 100);
    EXPECT_FALSE(result.has_value());
}

/**
 * @tc.name: NoDeoptAtExactPc
 * @tc.desc: Exact pc but entry has no deopt should return nullopt
 * @tc.type: FUNC
 */
HWTEST_F_L0(ArkSteedSafepointTableTest, NoDeoptAtExactPc)
{
    auto table = SafepointTableBuilder::BuildMultiEntry();
    auto result = ExtractBcOffsetFromSafepointTable(table.data(), table.size(), 50);
    EXPECT_FALSE(result.has_value());
}

/**
 * @tc.name: DeoptWithoutPcOffsetIndex
 * @tc.desc: Deopt with FUNC_INDEX (not PC_OFFSET_INDEX) should return nullopt
 * @tc.type: FUNC
 */
HWTEST_F_L0(ArkSteedSafepointTableTest, DeoptWithoutPcOffsetIndex)
{
    auto table = SafepointTableBuilder::BuildOneEntry(
        100, static_cast<int32_t>(SpecVregIndex::FUNC_INDEX), 12345);
    auto result = ExtractBcOffsetFromSafepointTable(table.data(), table.size(), 100);
    EXPECT_FALSE(result.has_value());
}

// --- LookupBcOffsetFromPositionTable ---

/**
 * @tc.name: PositionTableExactMatch
 * @tc.desc: Exact pcOffset match in position table
 * @tc.type: FUNC
 */
HWTEST_F_L0(ArkSteedSafepointTableTest, PositionTableExactMatch)
{
    auto table = PositionTableBuilder::Build({{10, 1}, {50, 5}, {100, 10}});
    auto result = LookupBcOffsetFromPositionTable(table.data(), table.size(), 50);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), 5U);
}

/**
 * @tc.name: PositionTableBetweenBoundaries
 * @tc.desc: pcOffset between two boundaries returns the lower one
 * @tc.type: FUNC
 */
HWTEST_F_L0(ArkSteedSafepointTableTest, PositionTableBetweenBoundaries)
{
    auto table = PositionTableBuilder::Build({{10, 1}, {50, 5}, {100, 10}});
    auto result = LookupBcOffsetFromPositionTable(table.data(), table.size(), 75);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), 5U);
}

/**
 * @tc.name: PositionTableBeforeFirst
 * @tc.desc: pcOffset before first boundary should return nullopt
 * @tc.type: FUNC
 */
HWTEST_F_L0(ArkSteedSafepointTableTest, PositionTableBeforeFirst)
{
    auto table = PositionTableBuilder::Build({{50, 5}, {100, 10}});
    auto result = LookupBcOffsetFromPositionTable(table.data(), table.size(), 10);
    EXPECT_FALSE(result.has_value());
}

/**
 * @tc.name: PositionTableBeyondLast
 * @tc.desc: pcOffset beyond last boundary returns last boundary
 * @tc.type: FUNC
 */
HWTEST_F_L0(ArkSteedSafepointTableTest, PositionTableBeyondLast)
{
    auto table = PositionTableBuilder::Build({{10, 1}, {50, 5}});
    auto result = LookupBcOffsetFromPositionTable(table.data(), table.size(), 999);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), 5U);
}

/**
 * @tc.name: PositionTableEmpty
 * @tc.desc: Empty position table should return nullopt
 * @tc.type: FUNC
 */
HWTEST_F_L0(ArkSteedSafepointTableTest, PositionTableEmpty)
{
    std::vector<uint8_t> empty(4, 0);  // 4: below the position-table header size
    auto result = LookupBcOffsetFromPositionTable(empty.data(), empty.size(), 100);
    EXPECT_FALSE(result.has_value());
}

/**
 * @tc.name: PositionTableNullData
 * @tc.desc: Null position table data should return nullopt
 * @tc.type: FUNC
 */
HWTEST_F_L0(ArkSteedSafepointTableTest, PositionTableNullData)
{
    auto result = LookupBcOffsetFromPositionTable(nullptr, 0, 100);
    EXPECT_FALSE(result.has_value());
}

/**
 * @tc.name: PositionTableSingleEntry
 * @tc.desc: Single entry position table
 * @tc.type: FUNC
 */
HWTEST_F_L0(ArkSteedSafepointTableTest, PositionTableSingleEntry)
{
    auto table = PositionTableBuilder::Build({{100, 42}});
    auto result = LookupBcOffsetFromPositionTable(table.data(), table.size(), 100);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), 42U);

    result = LookupBcOffsetFromPositionTable(table.data(), table.size(), 50);
    EXPECT_FALSE(result.has_value());
}

/**
 * @tc.name: SafepointTable_EmptyTable_HeaderOnlyRoundTrip
 * @tc.desc: an empty table is exactly the 16-byte documented header; counts survive Emit/EmitToNewBuffer
 *           and the reader reports zero entries with no findable safepoint
 * @tc.type: FUNC
 */
HWTEST_F_L0(ArkSteedSafepointTableTest, SafepointTable_EmptyTable_HeaderOnlyRoundTrip)
{
    ChunkFixture fixture;
    ArkSteedSafepointTableBuilder builder(&fixture.chunk);
    builder.SetFrameSlots(3, 5);
    builder.SetDeoptLiteralCount(0);

    EXPECT_EQ(builder.GetNumEntries(), 0U);
    // Documented layout: header only for zero entries.
    EXPECT_EQ(builder.GetTableSize(), HEADER_BYTES);
    // Cross-check of the documented layout with the production struct.
    EXPECT_EQ(sizeof(ArkSteedSafepointHeader), HEADER_BYTES);
    EXPECT_EQ(sizeof(ArkSteedSafepointEntry), ENTRY_BYTES);

    std::vector<uint8_t> buffer(builder.GetTableSize() + HEADER_BYTES, 0xAA);
    builder.Emit(buffer.data());
    // Emit writes exactly GetTableSize() bytes: the sentinel tail is untouched.
    for (size_t i = builder.GetTableSize(); i < buffer.size(); i++) {
        ASSERT_EQ(buffer[i], 0xAA) << "Emit wrote past GetTableSize() at byte " << i;
    }
    EXPECT_EQ(ReadU32LE(buffer.data()), 0U);                                  // numEntries
    EXPECT_EQ(ReadU32LE(buffer.data() + HEADER_OFFSET_TAGGED_SLOTS), 3U);     // numTaggedSlots
    EXPECT_EQ(ReadU32LE(buffer.data() + HEADER_OFFSET_UNTAGGED_SLOTS), 5U);   // numUntaggedSlots
    EXPECT_EQ(ReadU32LE(buffer.data() + HEADER_OFFSET_DEOPT_LITERALS), 0U);   // deoptLiteralCount

    uint8_t *fresh = builder.EmitToNewBuffer();
    ASSERT_NE(fresh, nullptr);
    EXPECT_EQ(std::memcmp(fresh, buffer.data(), builder.GetTableSize()), 0);

    ArkSteedSafepointTable table(fresh, builder.GetTableSize());
    EXPECT_TRUE(table.IsValid());
    EXPECT_EQ(table.GetNumEntries(), 0U);
    EXPECT_EQ(table.GetNumTaggedSlots(), 3U);
    EXPECT_EQ(table.GetNumUntaggedSlots(), 5U);
    EXPECT_EQ(table.GetDeoptLiteralCount(), 0U);
    EXPECT_EQ(table.FindEntry(0U), nullptr);
    EXPECT_EQ(table.FindEntry(0xFFFFU), nullptr);
    delete[] fresh;
}

/**
 * @tc.name: SafepointTable_TableSize_MatchesHandCountedDocumentedBytes
 * @tc.desc: GetTableSize() equals the hand-counted documented byte count (header + entries + variable
 *           deopt payloads) and Emit writes exactly that many bytes
 * @tc.type: FUNC
 */
HWTEST_F_L0(ArkSteedSafepointTableTest, SafepointTable_TableSize_MatchesHandCountedDocumentedBytes)
{
    ChunkFixture fixture;
    ArkSteedSafepointTableBuilder builder(&fixture.chunk);
    builder.SetFrameSlots(4, 1);
    builder.SetDeoptLiteralCount(2);
    builder.DefineSafepoint(0x00U).SetNumExtraSpillSlots(2);  // 2: plain safepoint, inline spill slots
    builder.DefineDeoptSafepoint(0x10U, {ConstantDeopt(1, 42), StackSlotDeopt(2, GCStackMapRegisters::FP, -16)});
    builder.DefineDeoptSafepoint(0x20U, {HeapLiteralDeopt(3, 0)});

    // Hand-counted payloads (all fields are single-byte SLEB128 by construction):
    //   entry 0x10: CONSTANT (id,value) = 2 bytes + STACK_SLOT (id,reg,offset) = 3 bytes
    //   entry 0x20: HEAP_LITERAL (id,literal index) = 2 bytes
    constexpr size_t DEOPT_BYTES_ENTRY_0X10 = 2 + 3;
    constexpr size_t DEOPT_BYTES_ENTRY_0X20 = 2;
    constexpr size_t EXPECTED_ENTRY_COUNT = 3;
    constexpr size_t DEOPT_PAYLOAD_START = HEADER_BYTES + EXPECTED_ENTRY_COUNT * ENTRY_BYTES;
    constexpr size_t EXPECTED_SIZE = DEOPT_PAYLOAD_START + DEOPT_BYTES_ENTRY_0X10 + DEOPT_BYTES_ENTRY_0X20;

    EXPECT_EQ(builder.GetNumEntries(), EXPECTED_ENTRY_COUNT);
    EXPECT_EQ(builder.GetTableSize(), EXPECTED_SIZE);

    std::vector<uint8_t> buffer(EXPECTED_SIZE + 8, 0xAA);  // 8: sentinel tail
    builder.Emit(buffer.data());
    for (size_t i = EXPECTED_SIZE; i < buffer.size(); i++) {
        ASSERT_EQ(buffer[i], 0xAA) << "Emit wrote past GetTableSize() at byte " << i;
    }

    EXPECT_EQ(ReadU32LE(buffer.data()), EXPECTED_ENTRY_COUNT);
    EXPECT_EQ(ReadU32LE(buffer.data() + HEADER_OFFSET_TAGGED_SLOTS), 4U);
    EXPECT_EQ(ReadU32LE(buffer.data() + HEADER_OFFSET_UNTAGGED_SLOTS), 1U);
    EXPECT_EQ(ReadU32LE(buffer.data() + HEADER_OFFSET_DEOPT_LITERALS), 2U);

    EXPECT_EQ(ReadU32LE(EntryAt(buffer.data(), 0) + ENTRY_OFFSET_PC), 0x00U);
    EXPECT_EQ(ReadU16LE(EntryAt(buffer.data(), 0) + ENTRY_OFFSET_FLAGS), 2U);
    EXPECT_EQ(ReadU16LE(EntryAt(buffer.data(), 0) + ENTRY_OFFSET_DEOPT_NUM), 0U);
    // An entry without deopt data keeps deoptOffset at zero.
    EXPECT_EQ(ReadU32LE(EntryAt(buffer.data(), 0) + ENTRY_OFFSET_DEOPT_OFFSET), 0U);

    EXPECT_EQ(ReadU32LE(EntryAt(buffer.data(), 1) + ENTRY_OFFSET_PC), 0x10U);
    // deoptNum is the legacy logical count: two units per vreg entry.
    EXPECT_EQ(ReadU16LE(EntryAt(buffer.data(), 1) + ENTRY_OFFSET_DEOPT_NUM), 4U);
    EXPECT_EQ(ReadU32LE(EntryAt(buffer.data(), 1) + ENTRY_OFFSET_DEOPT_OFFSET), DEOPT_PAYLOAD_START);

    EXPECT_EQ(ReadU32LE(EntryAt(buffer.data(), 2) + ENTRY_OFFSET_PC), 0x20U);
    EXPECT_EQ(ReadU16LE(EntryAt(buffer.data(), 2) + ENTRY_OFFSET_DEOPT_NUM), 2U);
    EXPECT_EQ(ReadU32LE(EntryAt(buffer.data(), 2) + ENTRY_OFFSET_DEOPT_OFFSET),
              DEOPT_PAYLOAD_START + DEOPT_BYTES_ENTRY_0X10);
    // The last payload ends exactly at the end of the table.
    EXPECT_EQ(DEOPT_PAYLOAD_START + DEOPT_BYTES_ENTRY_0X10 + DEOPT_BYTES_ENTRY_0X20, builder.GetTableSize());

    uint8_t *fresh = builder.EmitToNewBuffer();
    ASSERT_NE(fresh, nullptr);
    EXPECT_EQ(std::memcmp(fresh, buffer.data(), EXPECTED_SIZE), 0);
    ArkSteedSafepointTable table(fresh, EXPECTED_SIZE);
    EXPECT_TRUE(table.IsValid());
    EXPECT_EQ(table.GetNumEntries(), EXPECTED_ENTRY_COUNT);
    EXPECT_EQ(table.GetNumTaggedSlots(), 4U);
    EXPECT_EQ(table.GetNumUntaggedSlots(), 1U);
    EXPECT_EQ(table.GetDeoptLiteralCount(), 2U);
    delete[] fresh;
}

/**
 * @tc.name: SafepointTable_EntriesSortedByPcOffset_PayloadStaysWithItsEntry
 * @tc.desc: entries defined out of pcOffset order are emitted sorted ascending, and each payload
 *           offset still belongs to the entry that owns it
 * @tc.type: FUNC
 */
HWTEST_F_L0(ArkSteedSafepointTableTest, SafepointTable_EntriesSortedByPcOffset_PayloadStaysWithItsEntry)
{
    ChunkFixture fixture;
    ArkSteedSafepointTableBuilder builder(&fixture.chunk);
    builder.SetFrameSlots(3, 2);
    builder.SetDeoptLiteralCount(1);
    builder.DefineSafepoint(0x60U).SetNumExtraSpillSlots(1);
    builder.DefineSafepoint(0x10U).SetNumExtraSpillSlots(7);
    builder.DefineDeoptSafepoint(0x20U, {ConstantDeopt(1, 5)});
    builder.DefineSafepoint(0x40U).SetNumExtraSpillSlots(0);

    constexpr size_t ENTRY_COUNT = 4;
    constexpr uint32_t SORTED_PC_OFFSETS[ENTRY_COUNT] = {0x10U, 0x20U, 0x40U, 0x60U};
    constexpr uint16_t SORTED_SPILL_SLOTS[ENTRY_COUNT] = {7, 0, 0, 1};
    constexpr size_t DEOPT_PAYLOAD_START = HEADER_BYTES + ENTRY_COUNT * ENTRY_BYTES;
    // One CONSTANT deopt: id/kind field plus value = 2 bytes.
    constexpr size_t EXPECTED_SIZE = DEOPT_PAYLOAD_START + 2;

    EXPECT_EQ(builder.GetTableSize(), EXPECTED_SIZE);
    std::vector<uint8_t> buffer(EXPECTED_SIZE, 0);
    builder.Emit(buffer.data());

    for (size_t i = 0; i < ENTRY_COUNT; i++) {
        EXPECT_EQ(ReadU32LE(EntryAt(buffer.data(), i) + ENTRY_OFFSET_PC), SORTED_PC_OFFSETS[i]);
        EXPECT_EQ(ReadU16LE(EntryAt(buffer.data(), i) + ENTRY_OFFSET_FLAGS), SORTED_SPILL_SLOTS[i]);
        if (ReadU16LE(EntryAt(buffer.data(), i) + ENTRY_OFFSET_DEOPT_NUM) == 0) {
            EXPECT_EQ(ReadU32LE(EntryAt(buffer.data(), i) + ENTRY_OFFSET_DEOPT_OFFSET), 0U);
        }
    }
    // Only the 0x20 entry carries deopt data, and it owns the payload after the entries.
    EXPECT_EQ(ReadU16LE(EntryAt(buffer.data(), 1) + ENTRY_OFFSET_DEOPT_NUM), 2U);
    EXPECT_EQ(ReadU32LE(EntryAt(buffer.data(), 1) + ENTRY_OFFSET_DEOPT_OFFSET), DEOPT_PAYLOAD_START);

    uint8_t *fresh = builder.EmitToNewBuffer();
    ASSERT_NE(fresh, nullptr);
    ArkSteedSafepointTable table(fresh, EXPECTED_SIZE);
    ASSERT_TRUE(table.IsValid());
    for (size_t i = 0; i < ENTRY_COUNT; i++) {
        const ArkSteedSafepointEntry *entry = table.FindEntry(SORTED_PC_OFFSETS[i]);
        ASSERT_NE(entry, nullptr);
        EXPECT_EQ(entry->pcOffset, SORTED_PC_OFFSETS[i]);
        EXPECT_EQ(entry->GetNumExtraSpillSlots(), SORTED_SPILL_SLOTS[i]);
        EXPECT_EQ(HandlerKindCode(entry->GetExceptionHandlerKind()), HandlerKindCode(ExceptionHandlerKind::NONE));
    }
    delete[] fresh;
}

/**
 * @tc.name: SafepointTable_SpillSlotsAndHandlerKind_RoundTripAllValues
 * @tc.desc: every ExceptionHandlerKind and spill-slot counts up to the 0x1FFF mask boundary survive the
 *           builder/reader round trip through the documented flag field
 * @tc.type: FUNC
 */
HWTEST_F_L0(ArkSteedSafepointTableTest, SafepointTable_SpillSlotsAndHandlerKind_RoundTripAllValues)
{
    ChunkFixture fixture;
    ArkSteedSafepointTableBuilder builder(&fixture.chunk);
    builder.SetFrameSlots(9, 4);
    builder.SetDeoptLiteralCount(0);
    builder.DefineSafepoint(0x08U).SetNumExtraSpillSlots(0);
    builder.DefineDeoptSafepoint(0x18U, {}, ExceptionHandlerKind::COMPILED_CATCH);
    builder.DefineDeoptSafepoint(0x28U, {}, ExceptionHandlerKind::LAZY_DEOPT);
    builder.DefineSafepoint(0x38U).SetNumExtraSpillSlots(0x1FFEU);  // just inside the 13-bit mask
    builder.DefineSafepoint(0x48U).SetNumExtraSpillSlots(SPILL_SLOTS_MAX);  // mask boundary

    constexpr size_t ENTRY_COUNT = 5;
    constexpr uint32_t PC_OFFSETS[ENTRY_COUNT] = {0x08U, 0x18U, 0x28U, 0x38U, 0x48U};
    constexpr uint16_t SPILL_SLOTS[ENTRY_COUNT] = {0, 0, 0, 0x1FFEU, SPILL_SLOTS_MAX};
    constexpr uint16_t FLAGS[ENTRY_COUNT] = {
        0U,
        static_cast<uint16_t>(1U << HANDLER_KIND_SHIFT),   // COMPILED_CATCH
        static_cast<uint16_t>(2U << HANDLER_KIND_SHIFT),   // LAZY_DEOPT
        0x1FFEU,
        SPILL_SLOTS_MAX,
    };
    constexpr ExceptionHandlerKind KINDS[ENTRY_COUNT] = {
        ExceptionHandlerKind::NONE, ExceptionHandlerKind::COMPILED_CATCH, ExceptionHandlerKind::LAZY_DEOPT,
        ExceptionHandlerKind::NONE, ExceptionHandlerKind::NONE,
    };

    std::vector<uint8_t> buffer(builder.GetTableSize(), 0);
    builder.Emit(buffer.data());
    EXPECT_EQ(builder.GetTableSize(), HEADER_BYTES + ENTRY_COUNT * ENTRY_BYTES);
    for (size_t i = 0; i < ENTRY_COUNT; i++) {
        EXPECT_EQ(ReadU32LE(EntryAt(buffer.data(), i) + ENTRY_OFFSET_PC), PC_OFFSETS[i]);
        EXPECT_EQ(ReadU16LE(EntryAt(buffer.data(), i) + ENTRY_OFFSET_FLAGS), FLAGS[i]);
        EXPECT_EQ(ReadU16LE(EntryAt(buffer.data(), i) + ENTRY_OFFSET_DEOPT_NUM), 0U);
    }

    uint8_t *fresh = builder.EmitToNewBuffer();
    ASSERT_NE(fresh, nullptr);
    ArkSteedSafepointTable table(fresh, builder.GetTableSize());
    ASSERT_TRUE(table.IsValid());
    EXPECT_EQ(table.GetNumEntries(), ENTRY_COUNT);
    EXPECT_EQ(table.GetNumTaggedSlots(), 9U);
    EXPECT_EQ(table.GetNumUntaggedSlots(), 4U);
    for (size_t i = 0; i < ENTRY_COUNT; i++) {
        const ArkSteedSafepointEntry *entry = table.FindEntry(PC_OFFSETS[i]);
        ASSERT_NE(entry, nullptr);
        EXPECT_EQ(entry->pcOffset, PC_OFFSETS[i]);
        EXPECT_EQ(entry->GetNumExtraSpillSlots(), SPILL_SLOTS[i]);
        EXPECT_EQ(HandlerKindCode(entry->GetExceptionHandlerKind()), HandlerKindCode(KINDS[i]));
        EXPECT_EQ(HandlerKindCode(table.GetExceptionHandlerKind(PC_OFFSETS[i])), HandlerKindCode(KINDS[i]));
    }
    // Kinds are recovered only for an exact pcOffset match.
    EXPECT_EQ(HandlerKindCode(table.GetExceptionHandlerKind(0x1CU)), HandlerKindCode(ExceptionHandlerKind::NONE));
    EXPECT_EQ(HandlerKindCode(table.GetExceptionHandlerKind(0x0CU)), HandlerKindCode(ExceptionHandlerKind::NONE));
    delete[] fresh;
}

/**
 * @tc.name: SafepointTable_SpillSlotsAndHandlerKind_BitFieldsDoNotOverlap
 * @tc.desc: the 13-bit spill-slot field and the bits 13-14 handler-kind field are disjoint, verified on
 *           hand-built bytes that carry both fields at their boundary values
 * @tc.type: FUNC
 */
HWTEST_F_L0(ArkSteedSafepointTableTest, SafepointTable_SpillSlotsAndHandlerKind_BitFieldsDoNotOverlap)
{
    // Documented flags split; cross-checked below against hand-built bytes.
    EXPECT_EQ(static_cast<uint16_t>(ArkSteedSafepointEntry::EXTRA_SPILL_SLOTS_MASK & HANDLER_KIND_MASK), 0U);
    EXPECT_EQ(ArkSteedSafepointEntry::EXCEPTION_HANDLER_KIND_MASK, HANDLER_KIND_MASK);
    EXPECT_EQ(ArkSteedSafepointEntry::EXCEPTION_HANDLER_KIND_SHIFT, HANDLER_KIND_SHIFT);

    // Both fields at their boundary: 0x1FFF | (2 << 13) = 0x5FFF.
    constexpr uint16_t BOTH_FIELDS_MAX = static_cast<uint16_t>(SPILL_SLOTS_MAX | (2U << HANDLER_KIND_SHIFT));
    std::vector<uint8_t> both = HandBuiltSingleEntryTable(0x40U, BOTH_FIELDS_MAX, 0U, 0U);
    EXPECT_EQ(ReadU16LE(EntryAt(both.data(), 0) + ENTRY_OFFSET_FLAGS), 0x5FFFU);
    ArkSteedSafepointTable bothTable(both.data(), both.size());
    ASSERT_TRUE(bothTable.IsValid());
    const ArkSteedSafepointEntry *bothEntry = bothTable.FindEntry(0x40U);
    ASSERT_NE(bothEntry, nullptr);
    EXPECT_EQ(bothEntry->GetNumExtraSpillSlots(), SPILL_SLOTS_MAX);
    EXPECT_EQ(HandlerKindCode(bothEntry->GetExceptionHandlerKind()),
              HandlerKindCode(ExceptionHandlerKind::LAZY_DEOPT));

    // A lone kind bit must not leak into the spill-slot field.
    constexpr uint16_t KIND_ONLY = static_cast<uint16_t>(1U << HANDLER_KIND_SHIFT);
    std::vector<uint8_t> kindOnly = HandBuiltSingleEntryTable(0x80U, KIND_ONLY, 0U, 0U);
    ArkSteedSafepointTable kindOnlyTable(kindOnly.data(), kindOnly.size());
    ASSERT_TRUE(kindOnlyTable.IsValid());
    const ArkSteedSafepointEntry *kindOnlyEntry = kindOnlyTable.FindEntry(0x80U);
    ASSERT_NE(kindOnlyEntry, nullptr);
    EXPECT_EQ(kindOnlyEntry->GetNumExtraSpillSlots(), 0U);
    EXPECT_EQ(HandlerKindCode(kindOnlyEntry->GetExceptionHandlerKind()),
              HandlerKindCode(ExceptionHandlerKind::COMPILED_CATCH));
}

/**
 * @tc.name: SafepointTable_FindEntry_LastEntryAtOrBeforeOffset
 * @tc.desc: FindEntry returns the last entry with pcOffset <= target and nullptr before the first entry
 * @tc.type: FUNC
 */
HWTEST_F_L0(ArkSteedSafepointTableTest, SafepointTable_FindEntry_LastEntryAtOrBeforeOffset)
{
    ChunkFixture fixture;
    ArkSteedSafepointTableBuilder builder(&fixture.chunk);
    builder.SetFrameSlots(2, 2);
    builder.DefineSafepoint(0x10U).SetNumExtraSpillSlots(1);
    builder.DefineSafepoint(0x20U).SetNumExtraSpillSlots(2);
    builder.DefineSafepoint(0x40U).SetNumExtraSpillSlots(3);

    uint8_t *fresh = builder.EmitToNewBuffer();
    ASSERT_NE(fresh, nullptr);
    ArkSteedSafepointTable table(fresh, builder.GetTableSize());
    ASSERT_TRUE(table.IsValid());

    EXPECT_EQ(table.FindEntry(0x00U), nullptr);
    EXPECT_EQ(table.FindEntry(0x0FU), nullptr);

    const ArkSteedSafepointEntry *entry = table.FindEntry(0x10U);
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->pcOffset, 0x10U);
    EXPECT_EQ(entry->GetNumExtraSpillSlots(), 1U);

    entry = table.FindEntry(0x1FU);
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->pcOffset, 0x10U);

    entry = table.FindEntry(0x20U);
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->pcOffset, 0x20U);
    EXPECT_EQ(entry->GetNumExtraSpillSlots(), 2U);

    entry = table.FindEntry(0x21U);
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->pcOffset, 0x20U);

    entry = table.FindEntry(0x3FU);
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->pcOffset, 0x20U);

    entry = table.FindEntry(0x40U);
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->pcOffset, 0x40U);
    EXPECT_EQ(entry->GetNumExtraSpillSlots(), 3U);

    entry = table.FindEntry(0xFFFFU);
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->pcOffset, 0x40U);
    delete[] fresh;
}

/**
 * @tc.name: SafepointTable_GetDeoptInfo_RoundTripsAllValueKinds
 * @tc.desc: CONSTANT, STACK_SLOT and HEAP_LITERAL deopt values are recovered with their exact ids,
 *           kinds and values, including a literal above INT32_MAX
 * @tc.type: FUNC
 */
HWTEST_F_L0(ArkSteedSafepointTableTest, SafepointTable_GetDeoptInfo_RoundTripsAllValueKinds)
{
    ChunkFixture fixture;
    ArkSteedSafepointTableBuilder builder(&fixture.chunk);
    builder.SetFrameSlots(6, 0);
    builder.SetDeoptLiteralCount(2);
    builder.DefineDeoptSafepoint(0x20U,
                                 {ConstantDeopt(1, 42),
                                  StackSlotDeopt(2, GCStackMapRegisters::FP, -16),
                                  HeapLiteralDeopt(3, 0),
                                  HeapLiteralDeopt(4, 1)},
                                 ExceptionHandlerKind::COMPILED_CATCH);
    // Hand-counted payload: 2 + 3 + 2 + 2 bytes.
    constexpr size_t EXPECTED_SIZE = HEADER_BYTES + ENTRY_BYTES + (2 + 3 + 2 + 2);
    EXPECT_EQ(builder.GetTableSize(), EXPECTED_SIZE);

    uint8_t *fresh = builder.EmitToNewBuffer();
    ASSERT_NE(fresh, nullptr);
    const uint64_t deoptLiterals[2] = {0x1234ULL, 0x100000000ULL};  // second literal exceeds INT32_MAX
    ArkSteedSafepointTable table(fresh, EXPECTED_SIZE);
    ASSERT_TRUE(table.IsValid());

    std::vector<kungfu::ARKDeopt> deopts;
    ASSERT_TRUE(table.GetDeoptInfo(0x20U, deoptLiterals, 2, deopts));
    ASSERT_EQ(deopts.size(), 4U);

    EXPECT_EQ(deopts[0].id, 1);
    EXPECT_EQ(static_cast<int>(deopts[0].kind), static_cast<int>(kungfu::LocationTy::Kind::CONSTANT));
    EXPECT_EQ(std::get<kungfu::LLVMStackMapType::IntType>(deopts[0].value), 42);

    EXPECT_EQ(deopts[1].id, 2);
    EXPECT_EQ(static_cast<int>(deopts[1].kind), static_cast<int>(kungfu::LocationTy::Kind::INDIRECT));
    auto regAndOffset = std::get<kungfu::LLVMStackMapType::DwarfRegAndOffsetType>(deopts[1].value);
    EXPECT_EQ(regAndOffset.first, static_cast<kungfu::LLVMStackMapType::DwarfRegType>(GCStackMapRegisters::FP));
    EXPECT_EQ(regAndOffset.second, -16);

    EXPECT_EQ(deopts[2].id, 3);
    EXPECT_EQ(static_cast<int>(deopts[2].kind), static_cast<int>(kungfu::LocationTy::Kind::CONSTANT));
    EXPECT_EQ(std::get<kungfu::LLVMStackMapType::IntType>(deopts[2].value), 0x1234);

    EXPECT_EQ(deopts[3].id, 4);
    EXPECT_EQ(static_cast<int>(deopts[3].kind), static_cast<int>(kungfu::LocationTy::Kind::CONSTANTNDEX));
    EXPECT_EQ(std::get<kungfu::LLVMStackMapType::LargeInt>(deopts[3].value), 0x100000000LL);
    delete[] fresh;
}

/**
 * @tc.name: SafepointTable_GetDeoptInfo_NoDeoptEntryReturnsTrueEmpty
 * @tc.desc: an entry with deoptNum == 0, and a pcOffset without an exact entry, yield true with an
 *           empty result (no deopt information)
 * @tc.type: FUNC
 */
HWTEST_F_L0(ArkSteedSafepointTableTest, SafepointTable_GetDeoptInfo_NoDeoptEntryReturnsTrueEmpty)
{
    ChunkFixture fixture;
    ArkSteedSafepointTableBuilder builder(&fixture.chunk);
    builder.SetFrameSlots(1, 1);
    builder.SetDeoptLiteralCount(0);
    builder.DefineDeoptSafepoint(0x20U, {ConstantDeopt(1, 7)});
    builder.DefineSafepoint(0x40U).SetNumExtraSpillSlots(4);

    uint8_t *fresh = builder.EmitToNewBuffer();
    ASSERT_NE(fresh, nullptr);
    ArkSteedSafepointTable table(fresh, builder.GetTableSize());
    ASSERT_TRUE(table.IsValid());

    std::vector<kungfu::ARKDeopt> deopts;
    EXPECT_TRUE(table.GetDeoptInfo(0x40U, nullptr, 0, deopts));
    EXPECT_TRUE(deopts.empty());

    // 0x30 resolves to the 0x20 entry, but no entry matches the exact pcOffset.
    EXPECT_TRUE(table.GetDeoptInfo(0x30U, nullptr, 0, deopts));
    EXPECT_TRUE(deopts.empty());

    // The 0x20 entry still carries its own deopt data.
    ASSERT_TRUE(table.GetDeoptInfo(0x20U, nullptr, 0, deopts));
    ASSERT_EQ(deopts.size(), 1U);
    EXPECT_EQ(deopts[0].id, 1);
    EXPECT_EQ(std::get<kungfu::LLVMStackMapType::IntType>(deopts[0].value), 7);
    delete[] fresh;
}

/**
 * @tc.name: SafepointTable_GetDeoptInfo_RejectsLiteralTableMismatch
 * @tc.desc: GetDeoptInfo returns false without touching the output when deoptLiteralCount disagrees with
 *           the header, when literals are null with a nonzero count, and when a heap literal has no
 *           literal table at all
 * @tc.type: FUNC
 */
HWTEST_F_L0(ArkSteedSafepointTableTest, SafepointTable_GetDeoptInfo_RejectsLiteralTableMismatch)
{
    const uint64_t deoptLiterals[2] = {0ULL, 1ULL};

    {
        ChunkFixture fixture;
        ArkSteedSafepointTableBuilder builder(&fixture.chunk);
        builder.SetDeoptLiteralCount(2);
        builder.DefineDeoptSafepoint(0x20U, {ConstantDeopt(1, 7), ConstantDeopt(2, 8)});
        uint8_t *fresh = builder.EmitToNewBuffer();
        ASSERT_NE(fresh, nullptr);
        ArkSteedSafepointTable table(fresh, builder.GetTableSize());
        ASSERT_TRUE(table.IsValid());

        std::vector<kungfu::ARKDeopt> deopts;
        // 3 != header deoptLiteralCount (2): rejected before any decoding.
        EXPECT_FALSE(table.GetDeoptInfo(0x20U, deoptLiterals, 3, deopts));
        EXPECT_TRUE(deopts.empty());
        // Nonzero count with a null literal table.
        EXPECT_FALSE(table.GetDeoptInfo(0x20U, nullptr, 2, deopts));
        EXPECT_TRUE(deopts.empty());
        // Control: the same entry accepts the matching pair.
        ASSERT_TRUE(table.GetDeoptInfo(0x20U, deoptLiterals, 2, deopts));
        ASSERT_EQ(deopts.size(), 2U);
        EXPECT_EQ(std::get<kungfu::LLVMStackMapType::IntType>(deopts[0].value), 7);
        EXPECT_EQ(std::get<kungfu::LLVMStackMapType::IntType>(deopts[1].value), 8);
        delete[] fresh;
    }

    {
        // A heap literal with header deoptLiteralCount == 0 has no literal table to resolve.
        ChunkFixture fixture;
        ArkSteedSafepointTableBuilder builder(&fixture.chunk);
        builder.SetDeoptLiteralCount(0);
        builder.DefineDeoptSafepoint(0x20U, {HeapLiteralDeopt(1, 0)});
        uint8_t *fresh = builder.EmitToNewBuffer();
        ASSERT_NE(fresh, nullptr);
        ArkSteedSafepointTable table(fresh, builder.GetTableSize());
        ASSERT_TRUE(table.IsValid());

        std::vector<kungfu::ARKDeopt> deopts;
        EXPECT_FALSE(table.GetDeoptInfo(0x20U, nullptr, 0, deopts));
        EXPECT_TRUE(deopts.empty());
        delete[] fresh;
    }
}

/**
 * @tc.name: SafepointTable_InvalidBuffers_RejectedWithoutReachingAssert
 * @tc.desc: null, too-short and over-declared buffers are rejected by construction (IsValid false,
 *           FindEntry null) on every path that returns before the production ASSERT checks
 * @tc.type: FUNC
 */
HWTEST_F_L0(ArkSteedSafepointTableTest, SafepointTable_InvalidBuffers_RejectedWithoutReachingAssert)
{
    ArkSteedSafepointTable nullTable(nullptr, 64);
    EXPECT_FALSE(nullTable.IsValid());
    EXPECT_EQ(nullTable.FindEntry(0U), nullptr);

    std::vector<uint8_t> tooShort(HEADER_BYTES - 1, 0);
    ArkSteedSafepointTable shortTable(tooShort.data(), tooShort.size());
    EXPECT_FALSE(shortTable.IsValid());
    EXPECT_EQ(shortTable.FindEntry(0U), nullptr);

    // A full header declaring one entry with no room for it (size == header only).
    std::vector<uint8_t> declared = HandBuiltSingleEntryTable(0x00U, 0U, 0U, 0U);
    ArkSteedSafepointTable missingEntry(declared.data(), HEADER_BYTES);
    EXPECT_FALSE(missingEntry.IsValid());
    EXPECT_EQ(missingEntry.FindEntry(0U), nullptr);
    // One byte short of the required entry array is still rejected.
    ArkSteedSafepointTable oneShort(declared.data(), HEADER_BYTES + ENTRY_BYTES - 1);
    EXPECT_FALSE(oneShort.IsValid());
    EXPECT_EQ(oneShort.FindEntry(0U), nullptr);
    // Boundary control: the exact required size is accepted.
    ArkSteedSafepointTable exact(declared.data(), HEADER_BYTES + ENTRY_BYTES);
    EXPECT_TRUE(exact.IsValid());
    EXPECT_EQ(exact.GetNumEntries(), 1U);
    ASSERT_NE(exact.FindEntry(0U), nullptr);

    // numEntries larger than the entry capacity is rejected.
    std::vector<uint8_t> overflow = HandBuiltSingleEntryTable(0x00U, 0U, 0U, 0U);
    WriteU32LE(overflow.data(), 0xFFFFFFFFU);
    ArkSteedSafepointTable overflowTable(overflow.data(), overflow.size());
    EXPECT_FALSE(overflowTable.IsValid());
    EXPECT_EQ(overflowTable.FindEntry(0U), nullptr);
}

/**
 * @tc.name: SafepointTable_Mutation_ExpectationsFollowEmittedBytes
 * @tc.desc: P0 negative control: every expected header/entry field is bound to the emitted bytes, so a
 *           controlled single-field violation changes the observed value and restoring the byte
 *           restores the exact expected value
 * @tc.type: FUNC
 */
HWTEST_F_L0(ArkSteedSafepointTableTest, SafepointTable_Mutation_ExpectationsFollowEmittedBytes)
{
    ChunkFixture fixture;
    ArkSteedSafepointTableBuilder builder(&fixture.chunk);
    builder.SetFrameSlots(7, 2);
    builder.SetDeoptLiteralCount(1);
    builder.DefineDeoptSafepoint(0x20U, {ConstantDeopt(1, 42)});
    constexpr size_t TABLE_SIZE = HEADER_BYTES + ENTRY_BYTES + 2;  // one CONSTANT deopt (2 bytes)
    EXPECT_EQ(builder.GetTableSize(), TABLE_SIZE);

    std::vector<uint8_t> buffer(TABLE_SIZE, 0);
    builder.Emit(buffer.data());

    {
        ArkSteedSafepointTable baseline(buffer.data(), TABLE_SIZE);
        ASSERT_TRUE(baseline.IsValid());
        EXPECT_EQ(baseline.GetNumEntries(), 1U);
        EXPECT_EQ(baseline.GetNumTaggedSlots(), 7U);
        EXPECT_EQ(baseline.GetNumUntaggedSlots(), 2U);
        EXPECT_EQ(baseline.GetDeoptLiteralCount(), 1U);
        const ArkSteedSafepointEntry *entry = baseline.FindEntry(0x20U);
        ASSERT_NE(entry, nullptr);
        EXPECT_EQ(entry->GetNumExtraSpillSlots(), 0U);
    }

    uint8_t *entry = EntryAt(buffer.data(), 0);

    WriteU32LE(buffer.data() + HEADER_OFFSET_TAGGED_SLOTS, 9U);
    ArkSteedSafepointTable taggedSlots(buffer.data(), TABLE_SIZE);
    EXPECT_EQ(taggedSlots.GetNumTaggedSlots(), 9U);
    WriteU32LE(buffer.data() + HEADER_OFFSET_TAGGED_SLOTS, 7U);

    WriteU32LE(buffer.data() + HEADER_OFFSET_DEOPT_LITERALS, 5U);
    ArkSteedSafepointTable literalCount(buffer.data(), TABLE_SIZE);
    EXPECT_EQ(literalCount.GetDeoptLiteralCount(), 5U);
    WriteU32LE(buffer.data() + HEADER_OFFSET_DEOPT_LITERALS, 1U);

    WriteU16LE(entry + ENTRY_OFFSET_FLAGS, 5U);
    ArkSteedSafepointTable spillSlots(buffer.data(), TABLE_SIZE);
    ASSERT_NE(spillSlots.FindEntry(0x20U), nullptr);
    EXPECT_EQ(spillSlots.FindEntry(0x20U)->GetNumExtraSpillSlots(), 5U);
    WriteU16LE(entry + ENTRY_OFFSET_FLAGS, 0U);

    WriteU16LE(entry + ENTRY_OFFSET_FLAGS, static_cast<uint16_t>(2U << HANDLER_KIND_SHIFT));
    ArkSteedSafepointTable handlerKind(buffer.data(), TABLE_SIZE);
    EXPECT_EQ(HandlerKindCode(handlerKind.GetExceptionHandlerKind(0x20U)),
              HandlerKindCode(ExceptionHandlerKind::LAZY_DEOPT));
    WriteU16LE(entry + ENTRY_OFFSET_FLAGS, 0U);

    WriteU32LE(entry + ENTRY_OFFSET_PC, 0x30U);
    ArkSteedSafepointTable pcOffset(buffer.data(), TABLE_SIZE);
    EXPECT_EQ(pcOffset.FindEntry(0x20U), nullptr);
    ASSERT_NE(pcOffset.FindEntry(0x30U), nullptr);
    WriteU32LE(entry + ENTRY_OFFSET_PC, 0x20U);

    WriteU16LE(entry + ENTRY_OFFSET_DEOPT_NUM, 0U);
    ArkSteedSafepointTable noDeopt(buffer.data(), TABLE_SIZE);
    std::vector<kungfu::ARKDeopt> deopts;
    const uint64_t literal = 0ULL;
    EXPECT_TRUE(noDeopt.GetDeoptInfo(0x20U, &literal, 1, deopts));
    EXPECT_TRUE(deopts.empty());
    WriteU16LE(entry + ENTRY_OFFSET_DEOPT_NUM, 2U);

    // Restoring the mutated byte restores the exact expected value.
    ArkSteedSafepointTable restored(buffer.data(), TABLE_SIZE);
    ASSERT_TRUE(restored.GetDeoptInfo(0x20U, &literal, 1, deopts));
    ASSERT_EQ(deopts.size(), 1U);
    EXPECT_EQ(deopts[0].id, 1);
    EXPECT_EQ(std::get<kungfu::LLVMStackMapType::IntType>(deopts[0].value), 42);
}

}  // namespace panda::ecmascript::arksteed
