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

#include <thread>

#include "ecmascript/dfx/cpu_profiler/raw_samples_queue.h"
#include "ecmascript/tests/test_helper.h"

using namespace panda::ecmascript;

namespace panda::test {
class RawSamplesQueueTest : public testing::Test {
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

constexpr uint32_t BYTECODE_OFFSET_STRIDE = 8; // 8: per-frame bytecode offset increment

static RawSample MakeSample(uint64_t timestamp, uint16_t frameCount)
{
    RawSample sample;
    sample.timestamp = timestamp;
    sample.frameCount = frameCount;
    for (uint16_t i = 0; i < frameCount; i++) {
        sample.frames[i].value = 0x1000 + i;
        sample.frames[i].bytecodeOffset = i * BYTECODE_OFFSET_STRIDE;
        sample.frames[i].kind = RawFrameKind::INTERPRETER;
    }
    return sample;
}

static bool Enqueue(RawSamplesQueue &queue, const RawSample &sample)
{
    RawSample *slot = queue.TryStartEnqueue();
    if (slot == nullptr) {
        return false;
    }
    *slot = sample;
    queue.FinishEnqueue();
    return true;
}

HWTEST_F_L0(RawSamplesQueueTest, PushThenPop)
{
    RawSamplesQueue queue;
    RawSample in = MakeSample(100, 3);
    ASSERT_TRUE(Enqueue(queue, in));

    RawSample out;
    ASSERT_TRUE(queue.TryPop(out));
    EXPECT_EQ(out.timestamp, 100U);
    EXPECT_EQ(out.frameCount, 3U);
    EXPECT_EQ(out.frames[0].value, 0x1000U);
    EXPECT_EQ(out.frames[2].bytecodeOffset, 16U);

    RawSample none;
    EXPECT_FALSE(queue.TryPop(none));
}

HWTEST_F_L0(RawSamplesQueueTest, FifoOrder)
{
    RawSamplesQueue queue;
    constexpr uint16_t count = 16;
    for (uint16_t i = 0; i < count; i++) {
        ASSERT_TRUE(Enqueue(queue, MakeSample(i, 1)));
    }

    RawSample out;
    for (uint16_t i = 0; i < count; i++) {
        ASSERT_TRUE(queue.TryPop(out));
        EXPECT_EQ(out.timestamp, static_cast<uint64_t>(i));
    }
    RawSample none;
    EXPECT_FALSE(queue.TryPop(none));
}

HWTEST_F_L0(RawSamplesQueueTest, FullQueueDrops)
{
    RawSamplesQueue queue;
    constexpr size_t capacity = RawSamplesQueue::QUEUE_CAPACITY;
    for (size_t i = 0; i < capacity; i++) {
        ASSERT_TRUE(Enqueue(queue, MakeSample(i, 0)));
    }
    EXPECT_EQ(queue.TryStartEnqueue(), nullptr);

    RawSample out;
    ASSERT_TRUE(queue.TryPop(out));
    EXPECT_EQ(out.timestamp, 0U);
    EXPECT_TRUE(Enqueue(queue, MakeSample(capacity, 0)));
}

HWTEST_F_L0(RawSamplesQueueTest, WrapAroundKeepsOrder)
{
    RawSamplesQueue queue;
    constexpr size_t capacity = RawSamplesQueue::QUEUE_CAPACITY;
    constexpr size_t total = capacity * 4;
    uint64_t produced = 0;
    uint64_t consumed = 0;
    RawSample out;
    // Rounds of capacity/2 push the cursor past half the ring, covering slot
    // indices where stride/index drift bugs only appear at higher positions.
    for (size_t round = 0; round < 8; round++) {
        for (size_t i = 0; i < capacity / 2; i++) {
            ASSERT_TRUE(Enqueue(queue, MakeSample(produced++, 0)));
        }
        for (size_t i = 0; i < capacity / 2; i++) {
            ASSERT_TRUE(queue.TryPop(out));
            EXPECT_EQ(out.timestamp, consumed);
            consumed++;
        }
    }
    EXPECT_EQ(produced, total);
    EXPECT_EQ(consumed, total);
}

HWTEST_F_L0(RawSamplesQueueTest, MultithreadSpscNoLoss)
{
    RawSamplesQueue queue;
    constexpr uint32_t total = 10000;
    std::atomic<uint32_t> consumedCount {0};
    std::atomic<bool> producerDone {false};

    std::thread consumer([&queue, &consumedCount, &producerDone]() {
        RawSample out;
        uint64_t expected = 0;
        while (consumedCount.load() < total) {
            if (queue.TryPop(out)) {
                EXPECT_EQ(out.timestamp, expected);
                expected++;
                consumedCount.fetch_add(1);
            } else if (producerDone.load() && consumedCount.load() < total) {
                // Producer finished but items are still pending: keep draining.
                continue;
            } else {
                std::this_thread::yield();
            }
        }
    });

    std::thread producer([&queue, &producerDone]() {
        for (uint32_t i = 0; i < total; i++) {
            while (!Enqueue(queue, MakeSample(i, 2))) {
                std::this_thread::yield();
            }
        }
        producerDone.store(true);
    });

    producer.join();
    consumer.join();
    EXPECT_EQ(consumedCount.load(), total);
}

HWTEST_F_L0(RawSamplesQueueTest, MemoryFootprintWithinBudget)
{
    EXPECT_LE(sizeof(RawSamplesQueue), static_cast<size_t>(1024 * 1024));
}
} // namespace panda::test
