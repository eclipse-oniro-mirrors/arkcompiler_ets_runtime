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

#include "ecmascript/dfx/cpu_profiler/profiler_metadata_registry.h"
#include "ecmascript/tests/test_helper.h"

using namespace panda::ecmascript;

namespace panda::test {
class ProfilerMetadataRegistryTest : public testing::Test {
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

HWTEST_F_L0(ProfilerMetadataRegistryTest, InactiveHooksAreNoOps)
{
    auto &registry = ProfilerMetadataRegistry::Instance();
    JSPandaFile *dummy = reinterpret_cast<JSPandaFile *>(0x1000);
    // No Activate(): hooks must not append anything (single-branch fast path).
    registry.OnLoad(dummy);
    registry.OnUnload(dummy, nullptr);
    CVector<ProfilerMetadataRegistry::Event> events;
    uint64_t count = registry.GetEvents(0, events);
    EXPECT_EQ(count, 0U);
    EXPECT_TRUE(events.empty());
    EXPECT_EQ(registry.PublishedSequence()->load(), 0U);
}

HWTEST_F_L0(ProfilerMetadataRegistryTest, EventsAreSequencedAndCursored)
{
    auto &registry = ProfilerMetadataRegistry::Instance();
    JSPandaFile *fileA = reinterpret_cast<JSPandaFile *>(0x2000);
    JSPandaFile *fileB = reinterpret_cast<JSPandaFile *>(0x3000);
    registry.Activate();

    registry.OnLoad(fileA);
    registry.OnUnload(fileA, nullptr);
    registry.OnLoad(fileB);

    CVector<ProfilerMetadataRegistry::Event> events;
    uint64_t count = registry.GetEvents(0, events);
    ASSERT_EQ(count, 3U);
    EXPECT_EQ(events[0].sequence, 1U);
    EXPECT_FALSE(events[0].isDelete);
    EXPECT_EQ(events[0].file, fileA);
    EXPECT_EQ(events[1].sequence, 2U);
    EXPECT_TRUE(events[1].isDelete);
    EXPECT_EQ(events[2].sequence, 3U);
    EXPECT_EQ(registry.PublishedSequence()->load(), 3U);

    CVector<ProfilerMetadataRegistry::Event> tail;
    registry.GetEvents(2, tail); // cursor mid-log
    ASSERT_EQ(tail.size(), 1U);
    EXPECT_EQ(tail[0].sequence, 3U);

    EXPECT_TRUE(registry.Deactivate());
}

HWTEST_F_L0(ProfilerMetadataRegistryTest, DeactivateClearsAndReactivates)
{
    auto &registry = ProfilerMetadataRegistry::Instance();
    JSPandaFile *fileA = reinterpret_cast<JSPandaFile *>(0x2000);
    registry.Activate();
    registry.OnLoad(fileA);
    ASSERT_TRUE(registry.Deactivate());

    CVector<ProfilerMetadataRegistry::Event> events;
    EXPECT_EQ(registry.GetEvents(0, events), 0U);
    EXPECT_EQ(registry.PublishedSequence()->load(), 0U);

    registry.Activate();
    JSPandaFile *fileB = reinterpret_cast<JSPandaFile *>(0x3000);
    registry.OnLoad(fileB);
    registry.GetEvents(0, events);
    ASSERT_EQ(events.size(), 1U);
    EXPECT_EQ(events[0].file, fileB);
    EXPECT_TRUE(registry.Deactivate());
}

HWTEST_F_L0(ProfilerMetadataRegistryTest, NestedActivationRefusesEarlyClear)
{
    auto &registry = ProfilerMetadataRegistry::Instance();
    JSPandaFile *fileA = reinterpret_cast<JSPandaFile *>(0x2000);
    registry.Activate();
    registry.Activate();
    registry.OnLoad(fileA);

    EXPECT_FALSE(registry.Deactivate()); // one profiler still active
    CVector<ProfilerMetadataRegistry::Event> events;
    EXPECT_EQ(registry.GetEvents(0, events), 1U);
    EXPECT_EQ(events.size(), 1U);

    EXPECT_TRUE(registry.Deactivate()); // last one out clears
    events.clear();
    EXPECT_EQ(registry.GetEvents(0, events), 0U);
    EXPECT_TRUE(events.empty());
}
} // namespace panda::test
