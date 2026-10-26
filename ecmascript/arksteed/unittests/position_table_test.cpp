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
#include "ecmascript/tests/test_helper.h"

using namespace panda::ecmascript;
using namespace panda::ecmascript::arksteed;

namespace panda::test {

// ============================================================================
// Helper: build a position table binary from (pcOffset, bcOffset) pairs
// ============================================================================
static std::vector<uint8_t> BuildPositionTable(
    const std::vector<std::pair<uint32_t, uint32_t>> &entries)
{
    std::vector<uint8_t> buf;
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

// ============================================================================
// Test suite
// ============================================================================
class ArkSteedPositionTableTest : public BaseTestWithScope<false> {
};

// -----------------------------------------------------------------------
// Single entry roundtrip: build → lookup
// -----------------------------------------------------------------------
HWTEST_F_L0(ArkSteedPositionTableTest, SingleEntry)
{
    auto data = BuildPositionTable({{100, 42}});
    ASSERT_GT(data.size(), 0U);

    auto result = LookupBcOffsetFromPositionTable(data.data(), data.size(), 100);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value(), 42U);
}

// -----------------------------------------------------------------------
// Multiple entries: verify binary search works
// -----------------------------------------------------------------------
HWTEST_F_L0(ArkSteedPositionTableTest, MultipleEntries)
{
    auto data = BuildPositionTable({
        {10, 1}, {50, 5}, {100, 10}, {200, 20},
    });

    // Exact hit
    auto r1 = LookupBcOffsetFromPositionTable(data.data(), data.size(), 10);
    ASSERT_TRUE(r1.has_value());
    ASSERT_EQ(r1.value(), 1U);

    auto r2 = LookupBcOffsetFromPositionTable(data.data(), data.size(), 100);
    ASSERT_TRUE(r2.has_value());
    ASSERT_EQ(r2.value(), 10U);

    auto r3 = LookupBcOffsetFromPositionTable(data.data(), data.size(), 200);
    ASSERT_TRUE(r3.has_value());
    ASSERT_EQ(r3.value(), 20U);
}

// -----------------------------------------------------------------------
// Binary search: ≤ pcOffset, not exact match
// Lookup 150 should return the entry at 100 (bcOffset=10)
// -----------------------------------------------------------------------
HWTEST_F_L0(ArkSteedPositionTableTest, BinarySearchFloor)
{
    auto data = BuildPositionTable({
        {10, 1}, {50, 5}, {100, 10}, {200, 20},
    });

    // 150 is between 100 and 200, should return entry at 100
    auto result = LookupBcOffsetFromPositionTable(data.data(), data.size(), 150);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value(), 10U);
}

// -----------------------------------------------------------------------
// Before first entry: should return nullopt
// -----------------------------------------------------------------------
HWTEST_F_L0(ArkSteedPositionTableTest, BeforeFirstEntry)
{
    auto data = BuildPositionTable({
        {100, 42}, {200, 84},
    });

    // Query at pcOffset=50, which is before the first entry at 100
    auto result = LookupBcOffsetFromPositionTable(data.data(), data.size(), 50);
    ASSERT_FALSE(result.has_value());
}

// -----------------------------------------------------------------------
// Empty table: should return nullopt
// -----------------------------------------------------------------------
HWTEST_F_L0(ArkSteedPositionTableTest, EmptyTable)
{
    auto data = BuildPositionTable({});
    ASSERT_GT(data.size(), 0U); // at least numEntries=0

    auto result = LookupBcOffsetFromPositionTable(data.data(), data.size(), 100);
    ASSERT_FALSE(result.has_value());
}

// -----------------------------------------------------------------------
// Exact match at first entry
// -----------------------------------------------------------------------
HWTEST_F_L0(ArkSteedPositionTableTest, ExactMatchFirstEntry)
{
    auto data = BuildPositionTable({{0, 0}, {100, 42}});
    auto result = LookupBcOffsetFromPositionTable(data.data(), data.size(), 0);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value(), 0U);
}

// -----------------------------------------------------------------------
// Large offsets
// -----------------------------------------------------------------------
HWTEST_F_L0(ArkSteedPositionTableTest, LargeOffsets)
{
    auto data = BuildPositionTable({
        {0, 0}, {1000, 50}, {100000, 500},
    });

    auto r1 = LookupBcOffsetFromPositionTable(data.data(), data.size(), 0);
    ASSERT_TRUE(r1.has_value());
    ASSERT_EQ(r1.value(), 0U);

    auto r2 = LookupBcOffsetFromPositionTable(data.data(), data.size(), 1000);
    ASSERT_TRUE(r2.has_value());
    ASSERT_EQ(r2.value(), 50U);

    auto r3 = LookupBcOffsetFromPositionTable(data.data(), data.size(), 100000);
    ASSERT_TRUE(r3.has_value());
    ASSERT_EQ(r3.value(), 500U);

    // Floor lookup
    auto r4 = LookupBcOffsetFromPositionTable(data.data(), data.size(), 50000);
    ASSERT_TRUE(r4.has_value());
    ASSERT_EQ(r4.value(), 50U);
}

// -----------------------------------------------------------------------
// Same bytecode offset (same source line for multiple pc offsets)
// -----------------------------------------------------------------------
HWTEST_F_L0(ArkSteedPositionTableTest, SameBytecodeOffsets)
{
    auto data = BuildPositionTable({
        {0, 0}, {4, 0}, {8, 0}, {12, 1},
    });

    auto r1 = LookupBcOffsetFromPositionTable(data.data(), data.size(), 4);
    ASSERT_TRUE(r1.has_value());
    ASSERT_EQ(r1.value(), 0U);

    auto r2 = LookupBcOffsetFromPositionTable(data.data(), data.size(), 8);
    ASSERT_TRUE(r2.has_value());
    ASSERT_EQ(r2.value(), 0U);

    auto r3 = LookupBcOffsetFromPositionTable(data.data(), data.size(), 12);
    ASSERT_TRUE(r3.has_value());
    ASSERT_EQ(r3.value(), 1U);
}

// -----------------------------------------------------------------------
// Vertex-level anchor density (GraphBuilder bytecodeOffset fallback):
// straight-line region between two deopt points (calls at pc=100/bc=20 and
// pc=600/bc=80). Without the fallback only call sites produce anchors; with
// the fallback every code-emitting vertex does, so any pc inside a vertex's
// own code range resolves to that vertex's bytecode offset instead of the
// previous deopt point's.
// -----------------------------------------------------------------------
HWTEST_F_L0(ArkSteedPositionTableTest, VertexLevelAnchorDensity)
{
    // Deopt-point-only anchors (mixin-only filling, previous behavior)
    auto sparse = BuildPositionTable({{100, 20}, {600, 80}});

    // Vertex-level anchors (with fallback: arithmetic/load/store vertices
    // between the two calls also carry bytecode offsets)
    auto dense = BuildPositionTable({
        {100, 20}, {200, 40}, {300, 48}, {400, 56}, {500, 64}, {600, 80},
    });

    // pc=450 lies in the straight-line region between the two deopt points:
    // - sparse: floor anchor is pc=100 → bc=20 (error spans ~10 bytecodes)
    // - dense: floor anchor is pc=400 → bc=56 (owning vertex, error is 0)
    auto sparseResult = LookupBcOffsetFromPositionTable(sparse.data(), sparse.size(), 450);
    ASSERT_TRUE(sparseResult.has_value());
    ASSERT_EQ(sparseResult.value(), 20U);

    auto denseResult = LookupBcOffsetFromPositionTable(dense.data(), dense.size(), 450);
    ASSERT_TRUE(denseResult.has_value());
    ASSERT_EQ(denseResult.value(), 56U);

    // Every pc inside a vertex's own code range resolves to that vertex's bytecode offset
    for (uint32_t pc = 400; pc < 500; pc++) {
        auto r = LookupBcOffsetFromPositionTable(dense.data(), dense.size(), pc);
        ASSERT_TRUE(r.has_value());
        ASSERT_EQ(r.value(), 56U);
    }

    // Anchors within one bytecode share the same bcOffset (multiple vertices
    // from one bytecode), lookup still resolves exactly
    auto sameBc = BuildPositionTable({
        {100, 20}, {104, 20}, {108, 20}, {112, 24},
    });
    for (uint32_t pc = 100; pc < 112; pc++) {
        auto r = LookupBcOffsetFromPositionTable(sameBc.data(), sameBc.size(), pc);
        ASSERT_TRUE(r.has_value());
        ASSERT_EQ(r.value(), 20U);
    }
}

// -----------------------------------------------------------------------
// Truncated data (too small): should return nullopt
// -----------------------------------------------------------------------
HWTEST_F_L0(ArkSteedPositionTableTest, TruncatedData)
{
    std::vector<uint8_t> tiny = {0x01, 0x00, 0x00, 0x00}; // numEntries=1, but no entry data
    auto result = LookupBcOffsetFromPositionTable(tiny.data(), tiny.size(), 100);
    ASSERT_FALSE(result.has_value());
}

// -----------------------------------------------------------------------
// Null data
// -----------------------------------------------------------------------
HWTEST_F_L0(ArkSteedPositionTableTest, NullData)
{
    auto result = LookupBcOffsetFromPositionTable(nullptr, 0, 100);
    ASSERT_FALSE(result.has_value());
}

} // namespace panda::test