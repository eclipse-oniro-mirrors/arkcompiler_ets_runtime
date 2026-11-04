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

#include "ecmascript/tests/test_helper.h"
#include "ecmascript/arksteed/arksteed_pipeline_statistics.h"
#include "ecmascript/js_runtime_options.h"
#include "ecmascript/mem/native_area_allocator.h"

#include <unistd.h>  // optind

using namespace panda::ecmascript;
namespace panda::test {
using namespace panda::ecmascript::arksteed;

class ArkSteedPipelineStatisticsTest : public BaseTestWithOutScope {
};

namespace {
// Fresh allocator-backed chunk for Chunk counting / collector memory tests.
struct ChunkFixture {
    NativeAreaAllocator allocator;
    Chunk chunk;

    ChunkFixture() : chunk(&allocator, true) {}
};
}  // namespace

/**
 * @tc.name: PipelineStatistics_Option_DefaultFalse
 * @tc.desc: --compiler-arksteed-pass-trace defaults to false
 * @tc.type: FUNC
 */
HWTEST_F_L0(ArkSteedPipelineStatisticsTest, PipelineStatistics_Option_DefaultFalse)
{
    JSRuntimeOptions opts;
    EXPECT_FALSE(opts.GetCompilerArkSteedPassTrace());
}

/**
 * @tc.name: PipelineStatistics_Option_SetGet
 * @tc.desc: SetCompilerArkSteedPassTrace/GetCompilerArkSteedPassTrace roundtrip
 * @tc.type: FUNC
 */
HWTEST_F_L0(ArkSteedPipelineStatisticsTest, PipelineStatistics_Option_SetGet)
{
    JSRuntimeOptions opts;
    opts.SetCompilerArkSteedPassTrace(true);
    EXPECT_TRUE(opts.GetCompilerArkSteedPassTrace());
    opts.SetCompilerArkSteedPassTrace(false);
    EXPECT_FALSE(opts.GetCompilerArkSteedPassTrace());
}

/**
 * @tc.name: PipelineStatistics_Option_ParseTrue
 * @tc.desc: ParseCommand parses --compiler-arksteed-pass-trace=true
 * @tc.type: FUNC
 */
HWTEST_F_L0(ArkSteedPipelineStatisticsTest, PipelineStatistics_Option_ParseTrue)
{
    JSRuntimeOptions opts;
    optind = 0;
    const char *argv[] = {"test", "--compiler-arksteed-pass-trace=true"};
    EXPECT_TRUE(opts.ParseCommand(2, argv));  // 2: argc (program plus one option)
    EXPECT_TRUE(opts.GetCompilerArkSteedPassTrace());
}

/**
 * @tc.name: PipelineStatistics_Option_ParseFalse
 * @tc.desc: ParseCommand parses --compiler-arksteed-pass-trace=false
 * @tc.type: FUNC
 */
HWTEST_F_L0(ArkSteedPipelineStatisticsTest, PipelineStatistics_Option_ParseFalse)
{
    JSRuntimeOptions opts;
    optind = 0;
    const char *argv[] = {"test", "--compiler-arksteed-pass-trace=false"};
    EXPECT_TRUE(opts.ParseCommand(2, argv));  // 2: argc (program plus one option)
    EXPECT_FALSE(opts.GetCompilerArkSteedPassTrace());
}

/**
 * @tc.name: PipelineStatistics_Option_ParseNonBoolValueFollowsRuntimeConvention
 * @tc.desc: ParseCommand follows runtime-wide lenient bool parsing: any value
 *           other than "false"/"0" is treated as true (same as other bool options)
 * @tc.type: FUNC
 */
HWTEST_F_L0(ArkSteedPipelineStatisticsTest, PipelineStatistics_Option_ParseNonBoolValueFollowsRuntimeConvention)
{
    JSRuntimeOptions opts;
    optind = 0;
    const char *argv[] = {"test", "--compiler-arksteed-pass-trace=notabool"};
    EXPECT_TRUE(opts.ParseCommand(2, argv));  // 2: argc (program plus one option)
    EXPECT_TRUE(opts.GetCompilerArkSteedPassTrace());
}

/**
 * @tc.name: PipelineStatistics_Option_MethodsDefaultEmpty
 * @tc.desc: --compiler-arksteed-pass-trace-methods defaults to empty string
 * @tc.type: FUNC
 */
HWTEST_F_L0(ArkSteedPipelineStatisticsTest, PipelineStatistics_Option_MethodsDefaultEmpty)
{
    JSRuntimeOptions opts;
    EXPECT_TRUE(opts.GetCompilerArkSteedPassTraceMethods().empty());
}

/**
 * @tc.name: PipelineStatistics_Option_MethodsSetGet
 * @tc.desc: SetCompilerArkSteedPassTraceMethods/GetCompilerArkSteedPassTraceMethods roundtrip
 * @tc.type: FUNC
 */
HWTEST_F_L0(ArkSteedPipelineStatisticsTest, PipelineStatistics_Option_MethodsSetGet)
{
    JSRuntimeOptions opts;
    opts.SetCompilerArkSteedPassTraceMethods("foo,bar");
    EXPECT_EQ(opts.GetCompilerArkSteedPassTraceMethods(), std::string("foo,bar"));
    opts.SetCompilerArkSteedPassTraceMethods("");
    EXPECT_TRUE(opts.GetCompilerArkSteedPassTraceMethods().empty());
}

/**
 * @tc.name: PipelineStatistics_Option_MethodsParse
 * @tc.desc: ParseCommand parses --compiler-arksteed-pass-trace-methods=foo,bar
 * @tc.type: FUNC
 */
HWTEST_F_L0(ArkSteedPipelineStatisticsTest, PipelineStatistics_Option_MethodsParse)
{
    JSRuntimeOptions opts;
    optind = 0;
    const char *argv[] = {"test", "--compiler-arksteed-pass-trace-methods=foo,bar"};
    EXPECT_TRUE(opts.ParseCommand(2, argv));  // 2: argc (program plus one option)
    EXPECT_EQ(opts.GetCompilerArkSteedPassTraceMethods(), std::string("foo,bar"));
}

/**
 * @tc.name: PipelineStatistics_Chunk_AllocateAccumulates
 * @tc.desc: GetAllocatedSize accumulates aligned request sizes (AC-5)
 * @tc.type: FUNC
 */
HWTEST_F_L0(ArkSteedPipelineStatisticsTest, PipelineStatistics_Chunk_AllocateAccumulates)
{
    ChunkFixture fixture;
    EXPECT_EQ(fixture.chunk.GetAllocatedSize(), 0U);
    void *first = fixture.chunk.Allocate(100);  // aligned up to 104
    void *second = fixture.chunk.Allocate(8);
    (void)first;
    (void)second;
    EXPECT_EQ(fixture.chunk.GetAllocatedSize(), 112U);  // 112U: 104 (first allocation, aligned) + 8 (second)
}

/**
 * @tc.name: PipelineStatistics_Chunk_ExpandPathCounted
 * @tc.desc: both Expand paths (fresh chunk and grown chunk) are counted exactly
 * @tc.type: FUNC
 */
HWTEST_F_L0(ArkSteedPipelineStatisticsTest, PipelineStatistics_Chunk_ExpandPathCounted)
{
    ChunkFixture fixture;
    // First allocation on a fresh chunk always goes through Expand (no area yet).
    void *first = fixture.chunk.Allocate(4096);
    (void)first;
    EXPECT_EQ(fixture.chunk.GetAllocatedSize(), 4096U);
    // A large follow-up allocation grows the chunk again (Expand with existing head).
    void *second = fixture.chunk.Allocate(65536);
    (void)second;
    EXPECT_EQ(fixture.chunk.GetAllocatedSize(), 4096U + 65536U);
}

/**
 * @tc.name: PipelineStatistics_Chunk_InstancesIndependent
 * @tc.desc: counters of different Chunk instances do not interfere
 * @tc.type: FUNC
 */
HWTEST_F_L0(ArkSteedPipelineStatisticsTest, PipelineStatistics_Chunk_InstancesIndependent)
{
    ChunkFixture fixtureOne;
    ChunkFixture fixtureTwo;
    void *first = fixtureOne.chunk.Allocate(64);
    void *second = fixtureTwo.chunk.Allocate(256);
    (void)first;
    (void)second;
    EXPECT_EQ(fixtureOne.chunk.GetAllocatedSize(), 64U);
    EXPECT_EQ(fixtureTwo.chunk.GetAllocatedSize(), 256U);
}

/**
 * @tc.name: PipelineStatistics_Chunk_FreeNotDeducted
 * @tc.desc: Chunk::Free does not deduct from the counter (bump semantics)
 * @tc.type: FUNC
 */
HWTEST_F_L0(ArkSteedPipelineStatisticsTest, PipelineStatistics_Chunk_FreeNotDeducted)
{
    ChunkFixture fixture;
    void *mem = fixture.chunk.Allocate(64);
    fixture.chunk.Free(mem);
    EXPECT_EQ(fixture.chunk.GetAllocatedSize(), 64U);
}

/**
 * @tc.name: PipelineStatistics_Collector_DisabledNoCollection
 * @tc.desc: option off -> no sampling, no entries (AC-7 zero-overhead gate)
 * @tc.type: FUNC
 */
HWTEST_F_L0(ArkSteedPipelineStatisticsTest, PipelineStatistics_Collector_DisabledNoCollection)
{
    ChunkFixture fixture;
    PipelineStatisticsCollector collector(false);
    collector.AttachChunk(&fixture.chunk);
    {
        ArkSteedPipelineStatistics stats(collector, "BuildGraph");
        (void)fixture.chunk.Allocate(64);
    }
    EXPECT_FALSE(collector.Enabled());
    EXPECT_TRUE(collector.Entries().empty());
}

/**
 * @tc.name: PipelineStatistics_Collector_NestedDepthsAndMemory
 * @tc.desc: nested scopes produce depth-ordered entries with per-pass chunk deltas
 * @tc.type: FUNC
 */
HWTEST_F_L0(ArkSteedPipelineStatisticsTest, PipelineStatistics_Collector_NestedDepthsAndMemory)
{
    ChunkFixture fixture;
    PipelineStatisticsCollector collector(true);
    collector.AttachChunk(&fixture.chunk);
    {
        ArkSteedPipelineStatistics parent(collector, "BuildGraph");
        (void)fixture.chunk.Allocate(64);
        {
            ArkSteedPipelineStatistics child(collector, "GraphBuilder");
            (void)fixture.chunk.Allocate(128);
        }
    }
    const auto &entries = collector.Entries();
    ASSERT_EQ(entries.size(), 2U);
    EXPECT_STREQ(entries[0].name, "BuildGraph");
    EXPECT_EQ(entries[0].depth, 0U);
    EXPECT_STREQ(entries[1].name, "GraphBuilder");
    EXPECT_EQ(entries[1].depth, 1U);
    // Parent covers both allocations (64 + 128), child only its own (128).
    EXPECT_EQ(entries[0].chunkDeltaBytes, 192U);
    EXPECT_EQ(entries[1].chunkDeltaBytes, 128U);
    EXPECT_EQ(entries[1].chunkBytesAtStart, 64U);
    // Parent duration encloses child duration (both back-filled on Finish).
    EXPECT_GE(entries[0].durationUs, entries[1].durationUs);
}

/**
 * @tc.name: PipelineStatistics_Collector_EarlyReturnFinishRecorded
 * @tc.desc: early-return destructor back-fills the entry (duration and memory)
 * @tc.type: FUNC
 */
HWTEST_F_L0(ArkSteedPipelineStatisticsTest, PipelineStatistics_Collector_EarlyReturnFinishRecorded)
{
    ChunkFixture fixture;
    PipelineStatisticsCollector collector(true);
    collector.AttachChunk(&fixture.chunk);
    auto earlyReturnScope = [&collector, &fixture]() {
        ArkSteedPipelineStatistics stats(collector, "EarlyReturn");
        (void)fixture.chunk.Allocate(32);
        if (true) {
            return;  // dtor back-fills entry
        }
    };
    earlyReturnScope();
    const auto &entries = collector.Entries();
    ASSERT_EQ(entries.size(), 1U);
    EXPECT_STREQ(entries[0].name, "EarlyReturn");
    EXPECT_EQ(entries[0].chunkDeltaBytes, 32U);
}

/**
 * @tc.name: PipelineStatistics_Summary_MarkSuccessFlag
 * @tc.desc: MarkSuccess toggles the success flag consumed by the summary output
 * @tc.type: FUNC
 */
HWTEST_F_L0(ArkSteedPipelineStatisticsTest, PipelineStatistics_Summary_MarkSuccessFlag)
{
    ChunkFixture fixture;
    PipelineStatisticsCollector collector(true);
    collector.AttachChunk(&fixture.chunk);
    {
        ArkSteedPipelineStatisticsSummary summary(collector, "TestFunc");
        EXPECT_FALSE(summary.IsSuccess());
        summary.MarkSuccess();
        EXPECT_TRUE(summary.IsSuccess());
    }
}

/**
 * @tc.name: PipelineStatistics_Summary_DisabledNoFormatNoCrash
 * @tc.desc: option off -> summary destructor outputs nothing and does not crash
 * @tc.type: FUNC
 */
HWTEST_F_L0(ArkSteedPipelineStatisticsTest, PipelineStatistics_Summary_DisabledNoFormatNoCrash)
{
    ChunkFixture fixture;
    PipelineStatisticsCollector collector(false);
    collector.AttachChunk(&fixture.chunk);
    {
        ArkSteedPipelineStatisticsSummary summary(collector, "TestFunc");
        summary.MarkSuccess();
    }
    SUCCEED();
}

/**
 * @tc.name: PipelineStatistics_Format_ContainsHeaderIndentMemoryAborted
 * @tc.desc: formatted summary contains header (method/total/chunk/aborted),
 *           2/4-space indent and per-pass time/memory fields
 * @tc.type: FUNC
 */
HWTEST_F_L0(ArkSteedPipelineStatisticsTest, PipelineStatistics_Format_ContainsHeaderIndentMemoryAborted)
{
    std::vector<PipelineStatisticsEntry> entries;
    PipelineStatisticsEntry parent {"BuildGraph", 0, 1000, 3200, 0, 640 * 1024};       // 3.20ms, +640KB
    PipelineStatisticsEntry child {"GraphBuilder", 1, 1200, 1900, 300 * 1024, 340 * 1024};  // 1.90ms, +340KB
    entries.push_back(parent);
    entries.push_back(child);

    auto lines = FormatPipelineStatisticsSummary("TestFunc", 12.5f, 1200 * 1024, true, entries);
    ASSERT_EQ(lines.size(), 3U);
    EXPECT_EQ(lines[0],
        CString("[ArkSteed] Pipeline statistics for 'TestFunc' (total: 12.50ms, chunk: 1.17MB, aborted):"));
    EXPECT_EQ(lines[1], CString("  BuildGraph: 3.20ms, +640KB (cum 640KB)"));
    EXPECT_EQ(lines[2], CString("    GraphBuilder: 1.90ms, +340KB (cum 640KB)"));

    auto successLines = FormatPipelineStatisticsSummary("TestFunc", 1.0f, 512, false, entries);
    EXPECT_EQ(successLines[0].find("aborted"), CString::npos);
    EXPECT_EQ(successLines[0],
        CString("[ArkSteed] Pipeline statistics for 'TestFunc' (total: 1.00ms, chunk: 512B):"));
}

/**
 * @tc.name: PipelineStatistics_MatchMethodName_EmptyFilterOrEntries
 * @tc.desc: empty filter and empty-only entries never match (empty means "collect all" at caller)
 * @tc.type: FUNC
 */
HWTEST_F_L0(ArkSteedPipelineStatisticsTest, PipelineStatistics_MatchMethodName_EmptyFilterOrEntries)
{
    EXPECT_FALSE(MatchMethodName("", "foo"));
    EXPECT_FALSE(MatchMethodName(",", "foo"));
    EXPECT_FALSE(MatchMethodName(", ,", "foo"));
    EXPECT_FALSE(MatchMethodName("foo", ""));
}

/**
 * @tc.name: PipelineStatistics_MatchMethodName_ExactMatchOnly
 * @tc.desc: matching is exact and case-sensitive: no prefix/suffix/case tolerance
 * @tc.type: FUNC
 */
HWTEST_F_L0(ArkSteedPipelineStatisticsTest, PipelineStatistics_MatchMethodName_ExactMatchOnly)
{
    EXPECT_TRUE(MatchMethodName("foo", "foo"));
    EXPECT_FALSE(MatchMethodName("foo", "foobar"));
    EXPECT_FALSE(MatchMethodName("foobar", "foo"));
    EXPECT_FALSE(MatchMethodName("foo,bar", "Foo"));
    EXPECT_FALSE(MatchMethodName("foo,bar", "foo,"));
    EXPECT_FALSE(MatchMethodName("foo,bar", "foo,bar"));
}

/**
 * @tc.name: PipelineStatistics_MatchMethodName_MultiEntryTrimAndEmptyIgnored
 * @tc.desc: entries are comma-separated, trimmed, empty entries ignored
 * @tc.type: FUNC
 */
HWTEST_F_L0(ArkSteedPipelineStatisticsTest, PipelineStatistics_MatchMethodName_MultiEntryTrimAndEmptyIgnored)
{
    EXPECT_TRUE(MatchMethodName("foo,bar", "bar"));
    EXPECT_TRUE(MatchMethodName("foo,bar,baz", "foo"));
    EXPECT_TRUE(MatchMethodName("foo,bar,baz", "baz"));
    EXPECT_FALSE(MatchMethodName("foo,bar", "qux"));
    EXPECT_TRUE(MatchMethodName(" foo , bar ", "foo"));
    EXPECT_TRUE(MatchMethodName(" foo , bar ", "bar"));
    EXPECT_TRUE(MatchMethodName("foo,,bar", "bar"));
    EXPECT_TRUE(MatchMethodName("foo,", "foo"));
}
}  // namespace panda::test
