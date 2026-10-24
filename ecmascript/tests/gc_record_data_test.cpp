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
#include "ecmascript/js_runtime_options.h"
#include "ecmascript/tests/ecma_test_common.h"

using namespace panda;
using namespace panda::ecmascript;

namespace panda::test {

class GCRecordDataTest : public BaseTestWithScope<false> {
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

HWTEST_F_L0(GCRecordDataTest, RecordDataRoundTripTest001)
{
    auto heap = const_cast<Heap *>(thread->GetEcmaVM()->GetHeap());
    GCStats *gcStats = new GCStats(heap);
    gcStats->SetRecordData(RecordData::START_OBJ_SIZE, 1024);
    EXPECT_EQ(gcStats->GetRecordData(RecordData::START_OBJ_SIZE), 1024U);
    delete gcStats;
}

HWTEST_F_L0(GCRecordDataTest, RecordDataRoundTripTest002)
{
    auto heap = const_cast<Heap *>(thread->GetEcmaVM()->GetHeap());
    GCStats *gcStats = new GCStats(heap);
    gcStats->SetRecordData(RecordData::START_YOUNG_OBJ_SIZE, 512);
    EXPECT_EQ(gcStats->GetRecordData(RecordData::START_YOUNG_OBJ_SIZE), 512U);
    delete gcStats;
}

HWTEST_F_L0(GCRecordDataTest, RecordDataRoundTripTest003)
{
    auto heap = const_cast<Heap *>(thread->GetEcmaVM()->GetHeap());
    GCStats *gcStats = new GCStats(heap);
    gcStats->SetRecordData(RecordData::START_NATIVE_POINTER_NUM, 256);
    EXPECT_EQ(gcStats->GetRecordData(RecordData::START_NATIVE_POINTER_NUM), 256U);
    delete gcStats;
}

HWTEST_F_L0(GCRecordDataTest, RecordDataOverwriteTest001)
{
    auto heap = const_cast<Heap *>(thread->GetEcmaVM()->GetHeap());
    GCStats *gcStats = new GCStats(heap);
    gcStats->SetRecordData(RecordData::START_OBJ_SIZE, 100);
    gcStats->SetRecordData(RecordData::START_OBJ_SIZE, 200);
    EXPECT_EQ(gcStats->GetRecordData(RecordData::START_OBJ_SIZE), 200U);
    delete gcStats;
}

HWTEST_F_L0(GCRecordDataTest, GCReasonToStringTest001)
{
    auto heap = const_cast<Heap *>(thread->GetEcmaVM()->GetHeap());
    GCStats *gcStats = new GCStats(heap);
    gcStats->SetGCReason(GCReason::ALLOCATION_LIMIT);
    EXPECT_STREQ(gcStats->GCReasonToString(), "Memory reach limit");
    delete gcStats;
}

HWTEST_F_L0(GCRecordDataTest, GCReasonToStringTest002)
{
    auto heap = const_cast<Heap *>(thread->GetEcmaVM()->GetHeap());
    GCStats *gcStats = new GCStats(heap);
    gcStats->SetGCReason(GCReason::IDLE);
    EXPECT_STREQ(gcStats->GCReasonToString(), "Idle time task");
    delete gcStats;
}

HWTEST_F_L0(GCRecordDataTest, GCReasonToStringTest003)
{
    auto heap = const_cast<Heap *>(thread->GetEcmaVM()->GetHeap());
    GCStats *gcStats = new GCStats(heap);
    gcStats->SetGCReason(GCReason::TRIGGER_BY_JS);
    EXPECT_STREQ(gcStats->GCReasonToString(), "Trigger by JS");
    delete gcStats;
}

HWTEST_F_L0(GCRecordDataTest, GCReasonToStringTest004)
{
    auto heap = const_cast<Heap *>(thread->GetEcmaVM()->GetHeap());
    GCStats *gcStats = new GCStats(heap);
    EXPECT_STREQ(GCStats::GCReasonToString(GCReason::HINT_GC), "Trigger by hint");
    delete gcStats;
}

HWTEST_F_L0(GCRecordDataTest, GCSpeedDefaultTest001)
{
    auto heap = const_cast<Heap *>(thread->GetEcmaVM()->GetHeap());
    GCStats *gcStats = new GCStats(heap);
    EXPECT_EQ(gcStats->GetGCSpeed(SpeedData::MARK_SPEED), 0.0f);
    delete gcStats;
}
}  // namespace panda::test
