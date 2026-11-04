/**
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
#include <cstring>

#include "ecmascript/dfx/cpu_profiler/profiler_signal_context.h"
#include "ecmascript/dfx/cpu_profiler/raw_sample.h"
#include "ecmascript/tests/test_helper.h"

using namespace panda::ecmascript;

namespace panda::test {
class RawSampleTest : public testing::Test {
public:
    static void SetUpTestCase()
    {
        GTEST_LOG_(INFO) << "SetUpTestCase";
    }

    static void TearDownTestCase()
    {
        GTEST_LOG_(INFO) << "TearDownCase";
    }

    void SetUp() override {}
    void TearDown() override {}
};

static RawSample MakeFilledSample(uint64_t timestamp, uint16_t frameCount)
{
    RawSample sample;
    sample.timestamp = timestamp;
    sample.frameCount = frameCount;
    for (uint16_t i = 0; i < frameCount; i++) {
        sample.frames[i].value = 0x2000 + i;
        sample.frames[i].kind = RawFrameKind::INTERPRETER;
    }
    return sample;
}

HWTEST_F_L0(RawSampleTest, DefaultInitialization)
{
    RawSample sample;
    EXPECT_EQ(sample.timestamp, 0U);
    EXPECT_EQ(sample.metadataSequence, 0U);
    EXPECT_EQ(sample.threadOrTaskId, 0U);
    EXPECT_EQ(sample.profilerGeneration, 0U);
    EXPECT_EQ(sample.frameCount, 0U);
    EXPECT_TRUE(sample.timestampIsApproximate);
    EXPECT_EQ(sample.status, SampleStatus::OK);

    RawFrame frame;
    EXPECT_EQ(frame.value, 0U);
    EXPECT_EQ(frame.jsPandaFile, 0U);
    EXPECT_EQ(frame.bytecodeOffset, 0U);
    EXPECT_EQ(frame.kind, RawFrameKind::UNKNOWN);
    EXPECT_EQ(frame.state, RawRunningState::OTHER);
    EXPECT_EQ(frame.deoptType, 0U);
    EXPECT_EQ(frame.reserved, 0U);
}

HWTEST_F_L0(RawSampleTest, MemcpyRoundTrip)
{
    RawSample src = MakeFilledSample(42, 8);
    alignas(alignof(RawSample)) uint8_t buffer[sizeof(RawSample)];
    const uint8_t *srcBytes = reinterpret_cast<const uint8_t *>(&src);
    std::copy(srcBytes, srcBytes + sizeof(RawSample), buffer);
    RawSample dst;
    uint8_t *dstBytes = reinterpret_cast<uint8_t *>(&dst);
    std::copy(buffer, buffer + sizeof(RawSample), dstBytes);
    EXPECT_EQ(dst.timestamp, 42U);
    EXPECT_EQ(dst.frameCount, 8U);
    EXPECT_EQ(dst.frames[7].value, 0x2007U);
}

HWTEST_F_L0(RawSampleTest, FrameCapacityMatchesProfileLimit)
{
    EXPECT_EQ(RAW_MAX_STACK_SIZE, static_cast<uint16_t>(128));
    RawSample sample;
    sample.frameCount = RAW_MAX_STACK_SIZE;
    for (uint16_t i = 0; i < RAW_MAX_STACK_SIZE; i++) {
        sample.frames[i].kind = RawFrameKind::BUILTIN;
    }
    EXPECT_EQ(sample.frames[RAW_MAX_STACK_SIZE - 1].kind, RawFrameKind::BUILTIN);
}

HWTEST_F_L0(RawSampleTest, SignalContextDefaults)
{
    RawSamplesQueue queue;
    CpuProfilerSignalContext ctx;
    ctx.queue = &queue;
    EXPECT_FALSE(ctx.IsRunning());
    EXPECT_FALSE(ctx.shouldRecordSample.load());
    EXPECT_EQ(ctx.activeHandlerCount.load(), 0U);
    EXPECT_EQ(ctx.queue, &queue);
}

HWTEST_F_L0(RawSampleTest, SignalContextTablePublishLookupUnpublish)
{
    auto &table = g_profilerSignalContextTable;
    RawSamplesQueue queue;
    CpuProfilerSignalContext ctx;
    ctx.queue = &queue;
    const uint64_t tid = 12345;

    EXPECT_EQ(table.Lookup(tid), nullptr);
    table.Publish(tid, &ctx);
    EXPECT_EQ(table.Lookup(tid), &ctx);

    CpuProfilerSignalContext other;
    table.Publish(tid, &other);
    EXPECT_EQ(table.Lookup(tid), &other);

    table.Unpublish(tid, &ctx);
    EXPECT_EQ(table.Lookup(tid), &other);
    table.Unpublish(tid, &other);
    EXPECT_EQ(table.Lookup(tid), nullptr);

    EXPECT_EQ(table.Lookup(tid + 1), nullptr);
}

HWTEST_F_L0(RawSampleTest, SignalContextTableHasFreeSlot)
{
    auto &table = g_profilerSignalContextTable;
    CpuProfilerSignalContext ctx;
    const uint64_t tid = 0xABCDEF;
    EXPECT_TRUE(table.HasFreeSlot(tid));
    table.Publish(tid, &ctx);
    EXPECT_TRUE(table.HasFreeSlot(tid));
    table.Unpublish(tid, &ctx);
    EXPECT_TRUE(table.HasFreeSlot(tid));
}

HWTEST_F_L0(RawSampleTest, SignalContextTableMissNotCountedForUnboundTid)
{
    constexpr uint64_t UNBOUND_TID = 98765; // distinct from tids published by sibling tests
    const uint32_t before = GetProfilerSignalTableMisses();
    EXPECT_EQ(g_profilerSignalContextTable.Lookup(UNBOUND_TID), nullptr);
    EXPECT_EQ(GetProfilerSignalTableMisses(), before);
}
} // namespace panda::test
