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
#include "ecmascript/mem/gc_key_stats.h"
#include "ecmascript/mem/gc_stats.h"
#include "ecmascript/js_runtime_options.h"
#include "ecmascript/tests/ecma_test_common.h"

using namespace panda;
using namespace panda::ecmascript;

namespace panda::test {

class GCLongThresholdTest : public BaseTestWithScope<false> {
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

HWTEST_F_L0(GCLongThresholdTest, IsLongGCSensitiveTest001)
{
    auto heap = const_cast<Heap *>(thread->GetEcmaVM()->GetHeap());
    GCStats *gcStats = new GCStats(heap);
    gcStats->SetGCReason(GCReason::OTHER);
    EXPECT_TRUE(gcStats->IsLongGC(GCReason::OTHER, true, false, 34.0f));
    delete gcStats;
}

HWTEST_F_L0(GCLongThresholdTest, IsLongGCSensitiveTest002)
{
    auto heap = const_cast<Heap *>(thread->GetEcmaVM()->GetHeap());
    GCStats *gcStats = new GCStats(heap);
    EXPECT_FALSE(gcStats->IsLongGC(GCReason::OTHER, true, false, 32.0f));
    delete gcStats;
}

HWTEST_F_L0(GCLongThresholdTest, IsLongGCNotSensitiveIdleFgTest001)
{
    auto heap = const_cast<Heap *>(thread->GetEcmaVM()->GetHeap());
    GCStats *gcStats = new GCStats(heap);
    EXPECT_TRUE(gcStats->IsLongGC(GCReason::IDLE, false, false, 201.0f));
    delete gcStats;
}

HWTEST_F_L0(GCLongThresholdTest, IsLongGCNotSensitiveIdleFgTest002)
{
    auto heap = const_cast<Heap *>(thread->GetEcmaVM()->GetHeap());
    GCStats *gcStats = new GCStats(heap);
    EXPECT_FALSE(gcStats->IsLongGC(GCReason::IDLE, false, false, 199.0f));
    delete gcStats;
}

HWTEST_F_L0(GCLongThresholdTest, IsLongGCNotSensitiveIdleBgTest001)
{
    auto heap = const_cast<Heap *>(thread->GetEcmaVM()->GetHeap());
    GCStats *gcStats = new GCStats(heap);
    EXPECT_TRUE(gcStats->IsLongGC(GCReason::IDLE_NATIVE, false, true, 501.0f));
    delete gcStats;
}

HWTEST_F_L0(GCLongThresholdTest, IsLongGCNotSensitiveIdleBgTest002)
{
    auto heap = const_cast<Heap *>(thread->GetEcmaVM()->GetHeap());
    GCStats *gcStats = new GCStats(heap);
    EXPECT_FALSE(gcStats->IsLongGC(GCReason::IDLE_NATIVE, false, true, 499.0f));
    delete gcStats;
}

HWTEST_F_L0(GCLongThresholdTest, IsLongGCNotSensitiveOtherFgTest001)
{
    auto heap = const_cast<Heap *>(thread->GetEcmaVM()->GetHeap());
    GCStats *gcStats = new GCStats(heap);
    EXPECT_TRUE(gcStats->IsLongGC(GCReason::OTHER, false, false, 34.0f));
    delete gcStats;
}

HWTEST_F_L0(GCLongThresholdTest, IsLongGCNotSensitiveOtherBgTest001)
{
    auto heap = const_cast<Heap *>(thread->GetEcmaVM()->GetHeap());
    GCStats *gcStats = new GCStats(heap);
    EXPECT_TRUE(gcStats->IsLongGC(GCReason::OTHER, false, true, 201.0f));
    delete gcStats;
}

HWTEST_F_L0(GCLongThresholdTest, IsLongGCReasonGetterTest001)
{
    auto heap = const_cast<Heap *>(thread->GetEcmaVM()->GetHeap());
    GCStats *gcStats = new GCStats(heap);
    gcStats->SetGCReason(GCReason::ALLOCATION_LIMIT);
    EXPECT_EQ(gcStats->GetGCReason(), GCReason::ALLOCATION_LIMIT);
    delete gcStats;
}

HWTEST_F_L0(GCLongThresholdTest, IsLongGCReasonGetterTest002)
{
    auto heap = const_cast<Heap *>(thread->GetEcmaVM()->GetHeap());
    GCStats *gcStats = new GCStats(heap);
    gcStats->SetMarkReason(MarkReason::HINT_GC);
    EXPECT_EQ(gcStats->GetMarkReason(), MarkReason::HINT_GC);
    delete gcStats;
}
}  // namespace panda::test
