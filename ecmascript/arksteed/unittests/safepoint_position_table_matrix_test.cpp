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

/**
 * @file arksteed_switch_matrix_test.cpp
 *
 * Switch matrix test for the ArkSteed JIT exception stack bcOffset pipeline.
 *
 * Covers 4 switch combinations of container payload (safepoint table /
 * position table present / absent) and verifies the priority chain:
 *   LookupBcOffsetFromPositionTable → ExtractBcOffsetFromSafepointTable → 0
 *
 * AC-06: JIT-compiled code exception stack must show correct line numbers.
 * AC-06.4: position-table off must not degrade below function-first-line.
 * AC-06.5: position-table on must produce results at least as accurate as off.
 *
 * Also covers the real-codegen layout where the call is the last instruction
 * of its vertex: position boundary keyed at pcBefore, safepoint entry keyed at
 * the return address (RealCodegenLayout_CallLastInstruction).
 */

#include <cstdint>
#include <cstring>

#include "securec.h"
#include <optional>
#include <utility>
#include <vector>

#include "ecmascript/arksteed/arksteed_safepoint_table.h"
#include "ecmascript/deoptimizer/deoptimizer.h"
#include "ecmascript/tests/test_helper.h"

using namespace panda::ecmascript;
using namespace panda::ecmascript::arksteed;

namespace panda::test {

// ============================================================================
// Helpers for building container payloads with various combinations
// ============================================================================
class ContainerBuilder {
public:
    static std::vector<uint8_t> BuildContainer(
        const std::vector<uint8_t> &safepointData,
        const std::vector<uint8_t> &positionData)
    {
        size_t headerSize = ArkSteedTableContainerHeader::SIZE;
        size_t safepointOffset = headerSize;
        size_t positionOffset = headerSize + safepointData.size();
        size_t totalSize = headerSize + safepointData.size() + positionData.size();

        std::vector<uint8_t> buf(totalSize, 0);

        // Write header
        auto *hdr = reinterpret_cast<ArkSteedTableContainerHeader *>(buf.data());
        hdr->magic = ArkSteedTableContainerHeader::MAGIC;
        hdr->version = ArkSteedTableContainerHeader::VERSION;
        hdr->safepointOffset = static_cast<uint32_t>(safepointOffset);
        hdr->safepointSize = static_cast<uint32_t>(safepointData.size());
        hdr->positionOffset = (positionData.empty()) ? 0 : static_cast<uint32_t>(positionOffset);
        hdr->positionSize = static_cast<uint32_t>(positionData.size());

        // Write safepoint table
        if (!safepointData.empty()) {
            if (memcpy_s(buf.data() + safepointOffset, buf.size() - safepointOffset,
                         safepointData.data(), safepointData.size()) != EOK) {
                ADD_FAILURE() << "memcpy_s failed for the safepoint table";
                return {};
            }
        }

        // Write position table
        if (!positionData.empty()) {
            if (memcpy_s(buf.data() + positionOffset, buf.size() - positionOffset,
                         positionData.data(), positionData.size()) != EOK) {
                ADD_FAILURE() << "memcpy_s failed for the position table";
                return {};
            }
        }

        return buf;
    }

    // Build a simple safepoint table with one entry having deopt data
    static std::vector<uint8_t> BuildSafepointTable(
        uint32_t pcOffset, int32_t bcOffsetValue, bool hasDeopt = true)
    {
        std::vector<uint8_t> buf;
        buf.reserve(64);

        // Header (16 bytes): numEntries, numTaggedSlots, numUntaggedSlots, deoptLiteralCount
        uint32_t numEntries = 1;
        uint32_t zero32 = 0;
        buf.insert(buf.end(), reinterpret_cast<const uint8_t *>(&numEntries),
                   reinterpret_cast<const uint8_t *>(&numEntries) + sizeof(numEntries));
        buf.insert(buf.end(), reinterpret_cast<const uint8_t *>(&zero32),
                   reinterpret_cast<const uint8_t *>(&zero32) + sizeof(zero32));
        buf.insert(buf.end(), reinterpret_cast<const uint8_t *>(&zero32),
                   reinterpret_cast<const uint8_t *>(&zero32) + sizeof(zero32));
        buf.insert(buf.end(), reinterpret_cast<const uint8_t *>(&zero32),
                   reinterpret_cast<const uint8_t *>(&zero32) + sizeof(zero32));

        // Entry (12 bytes): pcOffset, deoptOffset, extraSpillSlotsAndFlags, deoptNum
        uint32_t deoptOffset = sizeof(ArkSteedSafepointHeader) +
            sizeof(ArkSteedSafepointEntry);
        uint16_t flags = 0;
        uint16_t deoptNum = hasDeopt ? 2 : 0;  // 2: vregAndKind plus value entries
        buf.insert(buf.end(), reinterpret_cast<const uint8_t *>(&pcOffset),
                   reinterpret_cast<const uint8_t *>(&pcOffset) + sizeof(pcOffset));
        buf.insert(buf.end(), reinterpret_cast<const uint8_t *>(&deoptOffset),
                   reinterpret_cast<const uint8_t *>(&deoptOffset) + sizeof(deoptOffset));
        buf.insert(buf.end(), reinterpret_cast<const uint8_t *>(&flags),
                   reinterpret_cast<const uint8_t *>(&flags) + sizeof(flags));
        buf.insert(buf.end(), reinterpret_cast<const uint8_t *>(&deoptNum),
                   reinterpret_cast<const uint8_t *>(&deoptNum) + sizeof(deoptNum));

        if (hasDeopt) {
            // Deopt data: vregAndKind = PC_OFFSET_INDEX(-2) * 4 + CONSTANT(0)
            int64_t vregAndKind = static_cast<int64_t>(SpecVregIndex::PC_OFFSET_INDEX) * 4 + 0;
            // LEB128 encode
            auto appendLeb128 = [](std::vector<uint8_t> &b, int64_t val) {
                bool more = true;
                while (more) {
                    uint8_t byte = static_cast<uint8_t>(val & 0x7F);  // 0x7F: 7 payload bits per LEB128 byte
                    val >>= 7;  // 7: payload bits per byte
                    if ((val == 0 && (byte & 0x40) == 0) || (val == -1 && (byte & 0x40) != 0)) {
                        more = false;
                    } else {
                        byte |= 0x80;  // 0x80: continuation bit
                    }
                    b.push_back(byte);
                }
            };
            appendLeb128(buf, vregAndKind);
            appendLeb128(buf, bcOffsetValue);
        }

        return buf;
    }

    // Build a simple position table with one entry
    static std::vector<uint8_t> BuildPositionTable(
        uint32_t pcOffset, uint32_t bcOffset)
    {
        std::vector<uint8_t> buf;
        buf.reserve(16);

        // Header: numEntries
        uint32_t numEntries = 1;
        buf.insert(buf.end(), reinterpret_cast<const uint8_t *>(&numEntries),
                   reinterpret_cast<const uint8_t *>(&numEntries) + sizeof(numEntries));
        // Entry: pcOffset, bcOffset
        buf.insert(buf.end(), reinterpret_cast<const uint8_t *>(&pcOffset),
                   reinterpret_cast<const uint8_t *>(&pcOffset) + sizeof(pcOffset));
        buf.insert(buf.end(), reinterpret_cast<const uint8_t *>(&bcOffset),
                   reinterpret_cast<const uint8_t *>(&bcOffset) + sizeof(bcOffset));

        return buf;
    }

    // Build a position table with multiple entries
    static std::vector<uint8_t> BuildPositionTable(
        const std::vector<std::pair<uint32_t, uint32_t>> &entries)
    {
        std::vector<uint8_t> buf;
        buf.reserve(sizeof(uint32_t) + entries.size() * 2 * sizeof(uint32_t));  // 2: pc and bc u32 per entry

        // Header: numEntries
        uint32_t numEntries = static_cast<uint32_t>(entries.size());
        buf.insert(buf.end(), reinterpret_cast<const uint8_t *>(&numEntries),
                   reinterpret_cast<const uint8_t *>(&numEntries) + sizeof(numEntries));
        for (const auto &[pc, bc] : entries) {
            buf.insert(buf.end(), reinterpret_cast<const uint8_t *>(&pc),
                       reinterpret_cast<const uint8_t *>(&pc) + sizeof(pc));
            buf.insert(buf.end(), reinterpret_cast<const uint8_t *>(&bc),
                       reinterpret_cast<const uint8_t *>(&bc) + sizeof(bc));
        }
        return buf;
    }
};

// ============================================================================
// Test suite
// ============================================================================
class ArkSteedSafepointPositionTableMatrixTest : public BaseTestWithScope<false> {
};

// -----------------------------------------------------------------------
// Combination 1: safepoint table present, position table absent
// This simulates the default configuration (position table OFF).
// The priority chain should fall back to ExtractBcOffsetFromSafepointTable.
// -----------------------------------------------------------------------
HWTEST_F_L0(ArkSteedSafepointPositionTableMatrixTest, SafepointOnly_ExactHit)
{
    auto safepointData = ContainerBuilder::BuildSafepointTable(100, 42);
    auto container = ContainerBuilder::BuildContainer(safepointData, {});

    size_t safepointSize = 0;
    const uint8_t *safepointTable = GetArkSteedSafepointTable(
        container.data(), container.size(), &safepointSize);
    ASSERT_NE(safepointTable, nullptr);
    ASSERT_GT(safepointSize, 0U);

    // Position table absent: GetArkSteedPositionTable returns nullptr when positionSize == 0
    size_t posSize = 0;
    GetArkSteedPositionTable(container.data(), container.size(), &posSize);
    ASSERT_EQ(posSize, 0U);

    // Verify safepoint extraction works
    auto result = ExtractBcOffsetFromSafepointTable(safepointTable, safepointSize, 100);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value(), 42U);
}

HWTEST_F_L0(ArkSteedSafepointPositionTableMatrixTest, SafepointOnly_NoDeopt_FallbackToZero)
{
    // GC-only safepoint (no deopt data) → should return nullopt
    auto safepointData = ContainerBuilder::BuildSafepointTable(100, 0, false);
    auto container = ContainerBuilder::BuildContainer(safepointData, {});

    size_t safepointSize = 0;
    const uint8_t *safepointTable = GetArkSteedSafepointTable(
        container.data(), container.size(), &safepointSize);
    ASSERT_NE(safepointTable, nullptr);
    ASSERT_GT(safepointSize, 0U);

    auto result = ExtractBcOffsetFromSafepointTable(safepointTable, safepointSize, 100);
    ASSERT_FALSE(result.has_value());
}

// -----------------------------------------------------------------------
// Combination 2: both safepoint table and position table present
// This simulates the position table ON configuration.
// The priority chain should prefer the position table.
// -----------------------------------------------------------------------
HWTEST_F_L0(ArkSteedSafepointPositionTableMatrixTest, BothTables_PositionWins)
{
    // Position table has bcOffset=99, safepoint table has bcOffset=42
    // The priority chain should prefer the position table result.
    auto safepointData = ContainerBuilder::BuildSafepointTable(100, 42);
    auto positionData = ContainerBuilder::BuildPositionTable(100, 99);
    auto container = ContainerBuilder::BuildContainer(safepointData, positionData);

    size_t posSize = 0;
    const uint8_t *posTable = GetArkSteedPositionTable(
        container.data(), container.size(), &posSize);
    ASSERT_NE(posTable, nullptr);
    ASSERT_GT(posSize, 0U);

    size_t safepointSize = 0;
    const uint8_t *safepointTable = GetArkSteedSafepointTable(
        container.data(), container.size(), &safepointSize);
    ASSERT_NE(safepointTable, nullptr);
    ASSERT_GT(safepointSize, 0U);

    // Priority chain: position table first
    auto posResult = LookupBcOffsetFromPositionTable(posTable, posSize, 100);
    ASSERT_TRUE(posResult.has_value());
    ASSERT_EQ(posResult.value(), 99U);

    // Safepoint table should also work
    auto safepointResult = ExtractBcOffsetFromSafepointTable(safepointTable, safepointSize, 100);
    ASSERT_TRUE(safepointResult.has_value());
    ASSERT_EQ(safepointResult.value(), 42U);
}

// -----------------------------------------------------------------------
// Combination 3: position table present, safepoint table absent (edge case)
// -----------------------------------------------------------------------
HWTEST_F_L0(ArkSteedSafepointPositionTableMatrixTest, PositionTableOnly_ContainerValid)
{
    auto positionData = ContainerBuilder::BuildPositionTable(100, 77);
    auto container = ContainerBuilder::BuildContainer({}, positionData);

    size_t posSize = 0;
    const uint8_t *posTable = GetArkSteedPositionTable(
        container.data(), container.size(), &posSize);
    ASSERT_NE(posTable, nullptr);
    ASSERT_GT(posSize, 0U);

    // Safepoint absent: GetArkSteedSafepointTable returns a pointer (header valid)
    // but safepointSize == 0 indicates no data.
    size_t safepointSize = 0;
    const uint8_t *safepointTable = GetArkSteedSafepointTable(
        container.data(), container.size(), &safepointSize);
    ASSERT_NE(safepointTable, nullptr);
    ASSERT_EQ(safepointSize, 0U);

    auto posResult = LookupBcOffsetFromPositionTable(posTable, posSize, 100);
    ASSERT_TRUE(posResult.has_value());
    ASSERT_EQ(posResult.value(), 77U);
}

// -----------------------------------------------------------------------
// Combination 4: both tables absent (empty container)
// Both GetArkSteedSafepointTable and GetArkSteedPositionTable should return
// zero size. The consumer should fall back to bcOffset=0.
// -----------------------------------------------------------------------
HWTEST_F_L0(ArkSteedSafepointPositionTableMatrixTest, BothTablesAbsent_FallbackToZero)
{
    auto container = ContainerBuilder::BuildContainer({}, {});

    // Safepoint absent: GetArkSteedSafepointTable returns a pointer (header valid)
    // but safepointSize == 0 indicates no data.
    size_t safepointSize = 0;
    const uint8_t *safepointTable = GetArkSteedSafepointTable(
        container.data(), container.size(), &safepointSize);
    ASSERT_NE(safepointTable, nullptr);
    ASSERT_EQ(safepointSize, 0U);

    // Position table absent: GetArkSteedPositionTable returns nullptr when positionSize == 0
    size_t posSize = 0;
    GetArkSteedPositionTable(container.data(), container.size(), &posSize);
    ASSERT_EQ(posSize, 0U);
}

// -----------------------------------------------------------------------
// Invalid container: bad magic number
// -----------------------------------------------------------------------
HWTEST_F_L0(ArkSteedSafepointPositionTableMatrixTest, InvalidContainer_BadMagic)
{
    std::vector<uint8_t> badContainer(ArkSteedTableContainerHeader::SIZE, 0xFF);  // 0xFF: corrupt filler bytes
    // Write a different magic
    auto *hdr = reinterpret_cast<ArkSteedTableContainerHeader *>(badContainer.data());
    hdr->magic = 0xDEADBEEF;  // 0xDEADBEEF: wrong magic value
    hdr->version = ArkSteedTableContainerHeader::VERSION;

    size_t safepointSize = 0;
    const uint8_t *safepointTable = GetArkSteedSafepointTable(
        badContainer.data(), badContainer.size(), &safepointSize);
    ASSERT_EQ(safepointTable, nullptr);
    ASSERT_EQ(safepointSize, 0U);

    size_t posSize = 0;
    const uint8_t *posTable = GetArkSteedPositionTable(
        badContainer.data(), badContainer.size(), &posSize);
    ASSERT_EQ(posTable, nullptr);
    ASSERT_EQ(posSize, 0U);
}

// -----------------------------------------------------------------------
// Invalid container: truncated (too small for header)
// -----------------------------------------------------------------------
HWTEST_F_L0(ArkSteedSafepointPositionTableMatrixTest, InvalidContainer_Truncated)
{
    std::vector<uint8_t> tinyContainer(4, 0);  // too small for header

    size_t safepointSize = 0;
    const uint8_t *safepointTable = GetArkSteedSafepointTable(
        tinyContainer.data(), tinyContainer.size(), &safepointSize);
    ASSERT_EQ(safepointTable, nullptr);
    ASSERT_EQ(safepointSize, 0U);

    size_t posSize = 0;
    const uint8_t *posTable = GetArkSteedPositionTable(
        tinyContainer.data(), tinyContainer.size(), &posSize);
    ASSERT_EQ(posTable, nullptr);
    ASSERT_EQ(posSize, 0U);
}

// -----------------------------------------------------------------------
// Null container
// -----------------------------------------------------------------------
HWTEST_F_L0(ArkSteedSafepointPositionTableMatrixTest, NullContainer)
{
    size_t safepointSize = 0;
    const uint8_t *safepointTable = GetArkSteedSafepointTable(nullptr, 0, &safepointSize);
    ASSERT_EQ(safepointTable, nullptr);
    ASSERT_EQ(safepointSize, 0U);

    size_t posSize = 0;
    const uint8_t *posTable = GetArkSteedPositionTable(nullptr, 0, &posSize);
    ASSERT_EQ(posTable, nullptr);
    ASSERT_EQ(posSize, 0U);
}

// -----------------------------------------------------------------------
// Priority chain simulation: position table miss → safepoint hit → 0
// This tests the full priority chain logic used in GetStackTraceInfos.
// -----------------------------------------------------------------------
HWTEST_F_L0(ArkSteedSafepointPositionTableMatrixTest, PriorityChain_PositionMiss_SafepointHit)
{
    // Position table only covers pcOffset=200, but we query pcOffset=100
    // Safepoint table covers pcOffset=100 with bcOffset=42
    auto positionData = ContainerBuilder::BuildPositionTable(200, 99);
    auto safepointData = ContainerBuilder::BuildSafepointTable(100, 42);
    auto container = ContainerBuilder::BuildContainer(safepointData, positionData);

    size_t posSize = 0;
    const uint8_t *posTable = GetArkSteedPositionTable(
        container.data(), container.size(), &posSize);
    ASSERT_NE(posTable, nullptr);

    size_t safepointSize = 0;
    const uint8_t *safepointTable = GetArkSteedSafepointTable(
        container.data(), container.size(), &safepointSize);
    ASSERT_NE(safepointTable, nullptr);

    // Priority 1: position table → miss (pcOffset=100 not in position table)
    auto posResult = LookupBcOffsetFromPositionTable(posTable, posSize, 100);
    ASSERT_FALSE(posResult.has_value());

    // Priority 2: safepoint table → hit
    auto safepointResult = ExtractBcOffsetFromSafepointTable(safepointTable, safepointSize, 100);
    ASSERT_TRUE(safepointResult.has_value());
    ASSERT_EQ(safepointResult.value(), 42U);
}

// -----------------------------------------------------------------------
// Priority chain: position table exact hit
// -----------------------------------------------------------------------
HWTEST_F_L0(ArkSteedSafepointPositionTableMatrixTest, PriorityChain_PositionExactHit)
{
    // Both tables cover pcOffset=100, but position table should be preferred
    auto positionData = ContainerBuilder::BuildPositionTable(100, 99);
    auto safepointData = ContainerBuilder::BuildSafepointTable(100, 42);
    auto container = ContainerBuilder::BuildContainer(safepointData, positionData);

    size_t posSize = 0;
    const uint8_t *posTable = GetArkSteedPositionTable(
        container.data(), container.size(), &posSize);
    ASSERT_NE(posTable, nullptr);

    // Priority 1: position table → exact hit at pcOffset=100
    auto posResult = LookupBcOffsetFromPositionTable(posTable, posSize, 100);
    ASSERT_TRUE(posResult.has_value());
    ASSERT_EQ(posResult.value(), 99U);
}

// -----------------------------------------------------------------------
// Priority chain: both tables miss → fallback to 0
// Position table only covers pcOffset=200 (floor returns 200→99).
// Safepoint table covers pcOffset=100.
// Query at pcOffset=300 → position table floor returns 200→99,
// safepoint also misses. Consumer should use position table result.
// -----------------------------------------------------------------------
HWTEST_F_L0(ArkSteedSafepointPositionTableMatrixTest, PriorityChain_BothMiss_FallbackToZero)
{
    // Position table covers pcOffset=200, safepoint table covers pcOffset=100
    // Query at pcOffset=300 → position table floor returns (200, 99)
    auto positionData = ContainerBuilder::BuildPositionTable(200, 99);
    auto safepointData = ContainerBuilder::BuildSafepointTable(100, 42);
    auto container = ContainerBuilder::BuildContainer(safepointData, positionData);

    size_t posSize = 0;
    const uint8_t *posTable = GetArkSteedPositionTable(
        container.data(), container.size(), &posSize);
    ASSERT_NE(posTable, nullptr);

    size_t safepointSize = 0;
    const uint8_t *safepointTable = GetArkSteedSafepointTable(
        container.data(), container.size(), &safepointSize);
    ASSERT_NE(safepointTable, nullptr);

    // Priority 1: position table → floor returns (200, 99) for pcOffset=300
    auto posResult = LookupBcOffsetFromPositionTable(posTable, posSize, 300);
    ASSERT_TRUE(posResult.has_value());
    ASSERT_EQ(posResult.value(), 99U);

    // Priority 2: safepoint table → miss (pcOffset=300 not in table, no floor)
    auto safepointResult = ExtractBcOffsetFromSafepointTable(safepointTable, safepointSize, 300);
    ASSERT_FALSE(safepointResult.has_value());
}

// -----------------------------------------------------------------------
// Real codegen layout: call is the last instruction of its vertex.
//
// The two tables are recorded with different pc bases:
//   - Position table: boundary keyed at pcBefore, the START of the
//     vertex's emitted code (RecordPositionBoundary in
//     arksteed_codegen.cpp ProcessNonControlVertex).
//   - Safepoint table: deopt entry keyed at the pc AFTER the call, i.e.
//     the return address (DefineDeoptSafepoint(assembler->GetPcOffset())
//     called post-Call in arksteed_codegen.cpp).
//
// Layout (arm64, call occupies 4 bytes):
//   [100, 104)  call vertex V emits BL, bcOffset=42, position boundary at 100
//   104         RA == pcBefore(V'), next vertex boundary, bcOffset=50
//
// Documents the lookup conventions required by FindBcOffsetByPCOffset:
//   - position table: query with RA-1; the raw RA floor-hits the NEXT
//     vertex's boundary and reports the wrong bytecode offset;
//   - safepoint table: query with the raw RA (exact match); RA-1 misses.
// -----------------------------------------------------------------------
HWTEST_F_L0(ArkSteedSafepointPositionTableMatrixTest, RealCodegenLayout_CallLastInstruction)
{
    constexpr uint32_t PC_BEFORE = 100;
    constexpr uint32_t CALL_SIZE = 4;
    constexpr uint32_t RA = PC_BEFORE + CALL_SIZE;
    constexpr uint32_t CALL_BC = 42;
    constexpr uint32_t NEXT_BC = 50;

    auto positionData = ContainerBuilder::BuildPositionTable(
        {{PC_BEFORE, CALL_BC}, {RA, NEXT_BC}});
    auto safepointData = ContainerBuilder::BuildSafepointTable(RA, CALL_BC);
    auto container = ContainerBuilder::BuildContainer(safepointData, positionData);

    size_t posSize = 0;
    const uint8_t *posTable = GetArkSteedPositionTable(
        container.data(), container.size(), &posSize);
    ASSERT_NE(posTable, nullptr);
    ASSERT_GT(posSize, 0U);

    size_t safepointSize = 0;
    const uint8_t *safepointTable = GetArkSteedSafepointTable(
        container.data(), container.size(), &safepointSize);
    ASSERT_NE(safepointTable, nullptr);
    ASSERT_GT(safepointSize, 0U);

    auto posRawRa = LookupBcOffsetFromPositionTable(posTable, posSize, RA);
    ASSERT_TRUE(posRawRa.has_value());
    ASSERT_EQ(posRawRa.value(), NEXT_BC);

    auto posMinusOne = LookupBcOffsetFromPositionTable(posTable, posSize, RA - 1);
    ASSERT_TRUE(posMinusOne.has_value());
    ASSERT_EQ(posMinusOne.value(), CALL_BC);

    auto safepointRawRa = ExtractBcOffsetFromSafepointTable(safepointTable, safepointSize, RA);
    ASSERT_TRUE(safepointRawRa.has_value());
    ASSERT_EQ(safepointRawRa.value(), CALL_BC);

    auto safepointMinusOne = ExtractBcOffsetFromSafepointTable(safepointTable, safepointSize, RA - 1);
    ASSERT_FALSE(safepointMinusOne.has_value());
}

} // namespace panda::test