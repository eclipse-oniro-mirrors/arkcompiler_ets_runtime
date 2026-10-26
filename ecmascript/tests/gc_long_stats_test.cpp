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

#include "ecmascript/ecma_vm.h"
#include "ecmascript/mem/full_gc.h"
#include "ecmascript/object_factory-inl.h"
#include "ecmascript/mem/gc_stats.h"
#include "ecmascript/mem/long_gc_stats.h"
#include "ecmascript/js_runtime_options.h"
#include "ecmascript/tests/ecma_test_common.h"

using namespace panda;
using namespace panda::ecmascript;

namespace panda::test {

class GCLongStatsTest : public BaseTestWithScope<false> {
public:
    void SetUp() override
    {
        JSRuntimeOptions options;
        instance = JSNApi::CreateEcmaVM(options);
        ASSERT_TRUE(instance != nullptr) << "Cannot create EcmaVM";
        thread = instance->GetJSThread();
        thread->ManagedCodeBegin();
        scope = new EcmaHandleScope(thread);
    }
};

HWTEST_F_L0(GCLongStatsTest, LongGCStatsSensitiveRoundTripTest)
{
    auto heap = const_cast<Heap *>(thread->GetEcmaVM()->GetHeap());
    GCStats *gcStats = new GCStats(heap);
    LongGCStats *longGCStats = gcStats->GetLongGCStats();
    longGCStats->SetGCIsSensitive(true);
    EXPECT_TRUE(longGCStats->GetGCIsSensitive());
    longGCStats->SetGCIsSensitive(false);
    EXPECT_FALSE(longGCStats->GetGCIsSensitive());
    delete gcStats;
}

HWTEST_F_L0(GCLongStatsTest, LongGCStatsBackgroundRoundTripTest)
{
    auto heap = const_cast<Heap *>(thread->GetEcmaVM()->GetHeap());
    GCStats *gcStats = new GCStats(heap);
    LongGCStats *longGCStats = gcStats->GetLongGCStats();
    longGCStats->SetGCIsInBackground(true);
    EXPECT_TRUE(longGCStats->GetGCIsInBackground());
    longGCStats->SetGCIsInBackground(false);
    EXPECT_FALSE(longGCStats->GetGCIsInBackground());
    delete gcStats;
}

HWTEST_F_L0(GCLongStatsTest, LongGCStatsTotalTimeRoundTripTest)
{
    auto heap = const_cast<Heap *>(thread->GetEcmaVM()->GetHeap());
    GCStats *gcStats = new GCStats(heap);
    LongGCStats *longGCStats = gcStats->GetLongGCStats();
    longGCStats->SetGCTotalTime(123.5f);
    EXPECT_FLOAT_EQ(longGCStats->GetGCTotalTime(), 123.5f);
    delete gcStats;
}

HWTEST_F_L0(GCLongStatsTest, LongGCStatsMarkTimeRoundTripTest)
{
    auto heap = const_cast<Heap *>(thread->GetEcmaVM()->GetHeap());
    GCStats *gcStats = new GCStats(heap);
    LongGCStats *longGCStats = gcStats->GetLongGCStats();
    longGCStats->SetGCMarkTime(45.0f);
    EXPECT_FLOAT_EQ(longGCStats->GetGCMarkTime(), 45.0f);
    delete gcStats;
}

HWTEST_F_L0(GCLongStatsTest, LongGCStatsEvacuateTimeRoundTripTest)
{
    auto heap = const_cast<Heap *>(thread->GetEcmaVM()->GetHeap());
    GCStats *gcStats = new GCStats(heap);
    LongGCStats *longGCStats = gcStats->GetLongGCStats();
    longGCStats->SetGCEvacuateTime(20.0f);
    EXPECT_FLOAT_EQ(longGCStats->GetGCEvacuateTime(), 20.0f);
    delete gcStats;
}

HWTEST_F_L0(GCLongStatsTest, LongGCStatsGCTypeRoundTripTest)
{
    auto heap = const_cast<Heap *>(thread->GetEcmaVM()->GetHeap());
    GCStats *gcStats = new GCStats(heap);
    LongGCStats *longGCStats = gcStats->GetLongGCStats();
    longGCStats->SetGCType(static_cast<size_t>(GCType::PARTIAL_YOUNG_GC));
    EXPECT_EQ(longGCStats->GetGCType(), static_cast<size_t>(GCType::PARTIAL_YOUNG_GC));
    delete gcStats;
}

HWTEST_F_L0(GCLongStatsTest, LongGCStatsResetTest)
{
    auto heap = const_cast<Heap *>(thread->GetEcmaVM()->GetHeap());
    GCStats *gcStats = new GCStats(heap);
    LongGCStats *longGCStats = gcStats->GetLongGCStats();
    longGCStats->SetGCIsSensitive(true);
    longGCStats->SetGCIsInBackground(true);
    longGCStats->SetGCTotalTime(100.0f);
    longGCStats->SetGCMarkTime(50.0f);
    longGCStats->Reset();
    EXPECT_FALSE(longGCStats->GetGCIsSensitive());
    EXPECT_FALSE(longGCStats->GetGCIsInBackground());
    EXPECT_FLOAT_EQ(longGCStats->GetGCTotalTime(), 0.0f);
    EXPECT_FLOAT_EQ(longGCStats->GetGCMarkTime(), 0.0f);
    delete gcStats;
}

HWTEST_F_L0(GCLongStatsTest, LongGCStatsSizeToIntKBTest)
{
    EXPECT_EQ(LongGCStats::SizeToIntKB(2048), 2);
    EXPECT_EQ(LongGCStats::SizeToIntKB(0), 0);
    EXPECT_EQ(LongGCStats::SizeToIntKB(4096), 4);
}
}  // namespace panda::test
