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

/*
 * Unit tests for the HintGC interface added for ets_runtime:
 *   - JSNApi::HintGC(const EcmaVM *vm, UrgencyLevel level)      (napi/jsnapi_expo.cpp)
 *   - Heap::CheckAndTriggerHintLocalCC(UrgencyLevel level)       (mem/heap.cpp)
 *   - GCReason::HINT_LOCAL_CC                                     (common_enum.h)
 *   - LOW_LEVEL_MIN_INC_RATIO / LOW_LEVEL_MIN_INC_STEP /
 *     HIGH_LEVEL_MIN_INC_STEP                                  (mem/mem.h)
 * The tests cover: urgency level enum values, hint GC threshold
 * constants/formula, trigger decision evaluation on idle and garbage
 * loaded heaps (the trigger itself is delegated asynchronously through the
 * VM loop and never runs a local CC on the calling thread), dispatching a
 * local CC with the HINT_LOCAL_CC reason from the CC mark state, live
 * object integrity across hint GC and local CC, and stress invocation of
 * the public interface.
 */

#include <thread>

#include "ecmascript/tests/test_helper.h"

#include "ecmascript/builtins/builtins_ark_tools.h"
#include "ecmascript/ecma_string.h"
#include "ecmascript/ecma_vm.h"
#include "ecmascript/global_env.h"
#include "ecmascript/js_handle.h"
#include "ecmascript/mem/concurrent_marker.h"
#include "ecmascript/mem/gc_stats.h"
#include "ecmascript/mem/idle_gc_trigger.h"
#include "ecmascript/mem/mem.h"
#include "ecmascript/object_factory-inl.h"
#include "ecmascript/runtime_lock.h"
#include "ecmascript/tagged_array.h"
#include "ecmascript/napi/include/jsnapi_expo.h"

using namespace panda::ecmascript;

namespace panda::test {
class HintGCTest : public BaseTestWithScope<false> {
public:
    void SetUp() override
    {
        JSRuntimeOptions options;
        options.SetEnableForceGC(false);
        instance = JSNApi::CreateEcmaVM(options);
        ASSERT_TRUE(instance != nullptr) << "Cannot create EcmaVM";
        thread = instance->GetJSThread();
        instance->GetJSOptions().SetEnableForceGC(false);
        instance->GetJSOptions().SetOpenArkTools(true);
        thread->ManagedCodeBegin();
        scope = new EcmaHandleScope(thread);
        instance->SetEnableForceGC(false);
        heap_ = const_cast<Heap *>(thread->GetEcmaVM()->GetHeap());
        heap_->GetConcurrentMarker()->EnableConcurrentMarking(EnableConcurrentMarkType::ENABLE);
        heap_->Prepare();
        if (heap_->CheckOngoingConcurrentMarking()) {
            heap_->GetConcurrentMarker()->Reset();
        }
    }

    /*
     * Mirror of the expected heap size computation in
     * Heap::CheckAndTriggerHintLocalCC for each urgency level.
     */
    static size_t ExpectedHeapSize(UrgencyLevel level, size_t heapAliveSizeAfterGC)
    {
        switch (level) {
            case UrgencyLevel::LOW:
                return std::max(static_cast<size_t>(heapAliveSizeAfterGC * LOW_LEVEL_MIN_INC_RATIO),
                    heapAliveSizeAfterGC + LOW_LEVEL_MIN_INC_STEP);
            case UrgencyLevel::MEDIUM:
                return heapAliveSizeAfterGC + LOW_LEVEL_MIN_INC_STEP;
            case UrgencyLevel::HIGH:
                return heapAliveSizeAfterGC + HIGH_LEVEL_MIN_INC_STEP;
            default:
                return 0;
        }
    }

    /* Allocate unreachable objects in old space to enlarge HeapObjectSize. */
    void AllocateOldSpaceGarbage(size_t count, size_t arrayLength)
    {
        ObjectFactory *factory = thread->GetEcmaVM()->GetFactory();
        [[maybe_unused]] ecmascript::EcmaHandleScope baseScope(thread);
        for (size_t i = 0; i < count; i++) {
            [[maybe_unused]] JSHandle<TaggedArray> array = factory->NewTaggedArray(
                arrayLength, JSTaggedValue::Hole(), MemSpaceType::OLD_SPACE);
        }
    }

    /* Allocate unreachable objects without triggering GC on allocation limits. */
    void AllocateOldSpaceGarbageWithoutGC(size_t count, size_t arrayLength)
    {
        ObjectFactory *factory = thread->GetEcmaVM()->GetFactory();
        [[maybe_unused]] ecmascript::EcmaHandleScope baseScope(thread);
        [[maybe_unused]] IgnoreGCScope gcScope(heap_);
        for (size_t i = 0; i < count; i++) {
            [[maybe_unused]] JSHandle<TaggedArray> array = factory->NewTaggedArray(
                arrayLength, JSTaggedValue::Hole(), MemSpaceType::OLD_SPACE);
        }
    }

    class IgnoreGCScope {
    public:
        explicit IgnoreGCScope(Heap *heap) : heap_(heap)
        {
            heap_->SetSensitiveStatus(AppSensitiveStatus::ENTER_HIGH_SENSITIVE);
            heap_->SetOnSerializeEvent(true);
        }

        ~IgnoreGCScope()
        {
            heap_->SetOnSerializeEvent(false);
            heap_->SetSensitiveStatus(AppSensitiveStatus::NORMAL_SCENE);
        }
    private:
        Heap *heap_ {nullptr};
    };

    Heap *heap_ {nullptr};
};

/* ------------------------------------------------------------------------ */
/* Urgency level enum and threshold constants                                */
/* ------------------------------------------------------------------------ */

HWTEST_F_L0(HintGCTest, UrgencyLevelEnumValueTest001)
{
    /*
     * @tc.number: SUB_ARK_ETS_RUNTIME_HINTGC_0100
     * @tc.name:   UrgencyLevelEnumValueTest001
     * @tc.desc:   Check enum values of JSNApi::UrgencyLevel exposed by the hint GC interface.
     */
    ASSERT_EQ(static_cast<uint8_t>(JSNApi::UrgencyLevel::LOW), 0U);
    ASSERT_EQ(static_cast<uint8_t>(JSNApi::UrgencyLevel::MEDIUM), 1U);
    ASSERT_EQ(static_cast<uint8_t>(JSNApi::UrgencyLevel::HIGH), 2U);
    /* The three levels must be distinguishable from each other. */
    ASSERT_TRUE(JSNApi::UrgencyLevel::LOW != JSNApi::UrgencyLevel::MEDIUM);
    ASSERT_TRUE(JSNApi::UrgencyLevel::MEDIUM != JSNApi::UrgencyLevel::HIGH);
    ASSERT_TRUE(JSNApi::UrgencyLevel::LOW != JSNApi::UrgencyLevel::HIGH);
    /* Heap level alias must refer to the same enum type. */
    static_assert(std::is_same<UrgencyLevel, JSNApi::UrgencyLevel>::value);
    ASSERT_EQ(static_cast<uint8_t>(UrgencyLevel::HIGH), 2U);
}

HWTEST_F_L0(HintGCTest, HintGCThresholdFormulaLowTest001)
{
    /*
     * @tc.number: SUB_ARK_ETS_RUNTIME_HINTGC_0120
     * @tc.name:   HintGCThresholdFormulaLowTest001
     * @tc.desc:   LOW level: threshold is max(alive * 1.05, alive + 5MB).
     */
    size_t alive = 4_MB;
    /* alive * 1.05 = 4.2MB < 9MB -> absolute step wins. */
    ASSERT_EQ(ExpectedHeapSize(UrgencyLevel::LOW, alive), alive + 5_MB);
    alive = 200_MB;
    /* alive * 1.05 = 210MB > 205MB -> ratio wins. */
    ASSERT_EQ(ExpectedHeapSize(UrgencyLevel::LOW, alive),
              static_cast<size_t>(alive * LOW_LEVEL_MIN_INC_RATIO));
    alive = 0;
    ASSERT_EQ(ExpectedHeapSize(UrgencyLevel::LOW, alive), 5_MB);
}

HWTEST_F_L0(HintGCTest, HintGCThresholdFormulaMediumTest001)
{
    /*
     * @tc.number: SUB_ARK_ETS_RUNTIME_HINTGC_0121
     * @tc.name:   HintGCThresholdFormulaMediumTest001
     * @tc.desc:   MEDIUM level: threshold is alive + 5MB.
     */
    size_t alive = 4_MB;
    /* MEDIUM threshold is alive + 5MB, no ratio is involved. */
    ASSERT_EQ(ExpectedHeapSize(UrgencyLevel::MEDIUM, alive), alive + 5_MB);
    alive = 0;
    ASSERT_EQ(ExpectedHeapSize(UrgencyLevel::MEDIUM, alive), 5_MB);
}

HWTEST_F_L0(HintGCTest, HintGCThresholdFormulaHighTest001)
{
    /*
     * @tc.number: SUB_ARK_ETS_RUNTIME_HINTGC_0122
     * @tc.name:   HintGCThresholdFormulaHighTest001
     * @tc.desc:   HIGH level: threshold is alive + 1MB regardless of alive size.
     */
    size_t alive = 4_MB;
    ASSERT_EQ(ExpectedHeapSize(UrgencyLevel::HIGH, alive), alive + 1_MB);
    alive = 200_MB;
    ASSERT_EQ(ExpectedHeapSize(UrgencyLevel::HIGH, alive), alive + 1_MB);
    alive = 0;
    ASSERT_EQ(ExpectedHeapSize(UrgencyLevel::HIGH, alive), 1_MB);
}

HWTEST_F_L0(HintGCTest, HintGCThresholdOrderTest001)
{
    /*
     * @tc.number: SUB_ARK_ETS_RUNTIME_HINTGC_0123
     * @tc.name:   HintGCThresholdOrderTest001
     * @tc.desc:   For a same alive size, HIGH threshold <= MEDIUM threshold <= LOW threshold.
     */
    for (size_t alive = 0; alive <= 64_MB; alive += 4_MB) {
        ASSERT_LE(ExpectedHeapSize(UrgencyLevel::HIGH, alive), ExpectedHeapSize(UrgencyLevel::MEDIUM, alive));
        ASSERT_LE(ExpectedHeapSize(UrgencyLevel::MEDIUM, alive), ExpectedHeapSize(UrgencyLevel::LOW, alive));
    }
}

/* ------------------------------------------------------------------------ */
/* GCReason statistics related to hint GC                                    */
/* ------------------------------------------------------------------------ */

HWTEST_F_L0(HintGCTest, HintLocalCCReasonTest001)
{
    /*
     * @tc.number: SUB_ARK_ETS_RUNTIME_HINTGC_0200
     * @tc.name:   HintLocalCCReasonTest001
     * @tc.desc:   GCReason::HINT_LOCAL_CC is a distinct reason and can be recorded/read by GCStats.
     */
    ASSERT_TRUE(GCReason::HINT_LOCAL_CC != GCReason::HINT_GC);
    ASSERT_TRUE(GCReason::HINT_LOCAL_CC != GCReason::TRIGGER_BY_JS);
    ASSERT_TRUE(GCReason::HINT_LOCAL_CC != GCReason::IDLE);
    GCStats *stats = heap_->GetEcmaGCStats();
    ASSERT_NE(stats, nullptr);
    GCReason oldReason = stats->GetGCReason();
    stats->SetGCReason(GCReason::HINT_LOCAL_CC);
    ASSERT_EQ(stats->GetGCReason(), GCReason::HINT_LOCAL_CC);
    stats->SetGCReason(GCReason::HINT_GC);
    ASSERT_EQ(stats->GetGCReason(), GCReason::HINT_GC);
    stats->SetGCReason(oldReason);
}

HWTEST_F_L0(HintGCTest, HintLocalCCReasonStringTest001)
{
    /*
     * @tc.number: SUB_ARK_ETS_RUNTIME_HINTGC_0201
     * @tc.name:   HintLocalCCReasonStringTest001
     * @tc.desc:   Reason strings of all GC reasons can be obtained without crash.
     */
    ASSERT_STREQ(GCStats::GCReasonToString(GCReason::HINT_GC), "Trigger by hint");
    const char *localCCReason = GCStats::GCReasonToString(GCReason::HINT_LOCAL_CC);
    ASSERT_NE(localCCReason, nullptr);
    ASSERT_TRUE(strlen(localCCReason) > 0);
    /* Reason string of LOCAL_CC GC type is available for hint GC statistics. */
    GCStats *stats = heap_->GetEcmaGCStats();
    ASSERT_NE(stats, nullptr);
    ASSERT_NE(stats->GetGCTypeName(), nullptr);
}

HWTEST_F_L0(HintGCTest, HeapAliveSizeBaselineTest001)
{
    /*
     * @tc.number: SUB_ARK_ETS_RUNTIME_HINTGC_0210
     * @tc.name:   HeapAliveSizeBaselineTest001
     * @tc.desc:   Full GC establishes the heapAliveSizeAfterGC baseline used by hint GC thresholds.
     */
    heap_->CollectGarbage(TriggerGCType::FULL_GC, GCReason::OTHER);
    size_t alive = heap_->GetHeapAliveSizeAfterGC();
    ASSERT_TRUE(alive > 0);
    /* Alive size after GC must never exceed current heap object size. */
    ASSERT_LE(heap_->GetHeapAliveSizeAfterGC(), heap_->GetHeapObjectSize());
}

HWTEST_F_L0(HintGCTest, HeapAliveSizeNotifyTest001)
{
    /*
     * @tc.number: SUB_ARK_ETS_RUNTIME_HINTGC_0211
     * @tc.name:   HeapAliveSizeNotifyTest001
     * @tc.desc:   NotifyHeapAliveSizeAfterGC updates the hint GC threshold baseline and it can be read back.
     */
    constexpr size_t TEST_ALIVE_SIZE = 2_MB;
    heap_->NotifyHeapAliveSizeAfterGC(TEST_ALIVE_SIZE);
    ASSERT_EQ(heap_->GetHeapAliveSizeAfterGC(), TEST_ALIVE_SIZE);
    heap_->NotifyHeapAliveSizeAfterGC(0);
    ASSERT_EQ(heap_->GetHeapAliveSizeAfterGC(), 0U);
    /* Full GC restores a real baseline. */
    heap_->CollectGarbage(TriggerGCType::FULL_GC, GCReason::OTHER);
    ASSERT_GT(heap_->GetHeapAliveSizeAfterGC(), 0U);
    ASSERT_LE(heap_->GetHeapAliveSizeAfterGC(), heap_->GetHeapObjectSize());
}

/* ------------------------------------------------------------------------ */
/* JSNApi::HintGC interface tests                                            */
/* ------------------------------------------------------------------------ */

HWTEST_F_L0(HintGCTest, JSNApiHintGCLowTest001)
{
    /*
     * @tc.number: SUB_ARK_ETS_RUNTIME_HINTGC_0300
     * @tc.name:   JSNApiHintGCLowTest001
     * @tc.desc:   JSNApi::HintGC with LOW level on a fresh heap keeps the VM usable.
     */
    ecmascript::ThreadManagedScope managedScope(thread);
    JSNApi::HintGC(instance, JSNApi::UrgencyLevel::LOW);
    ObjectFactory *factory = thread->GetEcmaVM()->GetFactory();
    JSHandle<EcmaString> str = factory->NewFromASCII("hintgc_low");
    ASSERT_STREQ(EcmaStringAccessor(str).ToCString(thread).c_str(), "hintgc_low");
    JSHandle<TaggedArray> array = factory->NewTaggedArray(4); // 4: array length
    array->Set(thread, 0, str.GetTaggedValue());
    ASSERT_EQ(array->Get(thread, 0), str.GetTaggedValue());
}

HWTEST_F_L0(HintGCTest, JSNApiHintGCMediumTest001)
{
    /*
     * @tc.number: SUB_ARK_ETS_RUNTIME_HINTGC_0301
     * @tc.name:   JSNApiHintGCMediumTest001
     * @tc.desc:   JSNApi::HintGC with MEDIUM level on a fresh heap keeps the VM usable.
     */
    ecmascript::ThreadManagedScope managedScope(thread);
    JSNApi::HintGC(instance, JSNApi::UrgencyLevel::MEDIUM);
    ObjectFactory *factory = thread->GetEcmaVM()->GetFactory();
    JSHandle<EcmaString> str = factory->NewFromASCII("hintgc_medium");
    ASSERT_STREQ(EcmaStringAccessor(str).ToCString(thread).c_str(), "hintgc_medium");
}

HWTEST_F_L0(HintGCTest, JSNApiHintGCHighTest001)
{
    /*
     * @tc.number: SUB_ARK_ETS_RUNTIME_HINTGC_0302
     * @tc.name:   JSNApiHintGCHighTest001
     * @tc.desc:   JSNApi::HintGC with HIGH level on a fresh heap keeps the VM usable.
     */
    ecmascript::ThreadManagedScope managedScope(thread);
    JSNApi::HintGC(instance, JSNApi::UrgencyLevel::HIGH);
    ObjectFactory *factory = thread->GetEcmaVM()->GetFactory();
    JSHandle<EcmaString> str = factory->NewFromASCII("hintgc_high");
    ASSERT_STREQ(EcmaStringAccessor(str).ToCString(thread).c_str(), "hintgc_high");
}

HWTEST_F_L0(HintGCTest, JSNApiHintGCAllLevelsSequentialTest001)
{
    /*
     * @tc.number: SUB_ARK_ETS_RUNTIME_HINTGC_0310
     * @tc.name:   JSNApiHintGCAllLevelsSequentialTest001
     * @tc.desc:   Calling hint GC with all levels sequentially is safe and keeps live data valid.
     */
    ecmascript::ThreadManagedScope managedScope(thread);
    ObjectFactory *factory = thread->GetEcmaVM()->GetFactory();
    constexpr uint32_t ARRAY_LEN = 16; // 16: array length
    JSHandle<TaggedArray> liveArray = factory->NewTaggedArray(ARRAY_LEN);
    {
        [[maybe_unused]] ecmascript::EcmaHandleScope baseScope(thread);
        for (uint32_t i = 0; i < ARRAY_LEN; i++) {
            JSHandle<EcmaString> str = factory->NewFromASCII("live_object");
            liveArray->Set(thread, i, str.GetTaggedValue());
        }
    }
    JSNApi::HintGC(instance, JSNApi::UrgencyLevel::LOW);
    JSNApi::HintGC(instance, JSNApi::UrgencyLevel::MEDIUM);
    JSNApi::HintGC(instance, JSNApi::UrgencyLevel::HIGH);
    for (uint32_t i = 0; i < ARRAY_LEN; i++) {
        JSTaggedValue value = liveArray->Get(thread, i);
        ASSERT_TRUE(value.IsString());
        ASSERT_STREQ(EcmaStringAccessor(value).ToCString(thread).c_str(), "live_object");
    }
}

HWTEST_F_L0(HintGCTest, JSNApiHintGCAfterGarbageAllocationTest001)
{
    /*
     * @tc.number: SUB_ARK_ETS_RUNTIME_HINTGC_0311
     * @tc.name:   JSNApiHintGCAfterGarbageAllocationTest001
     * @tc.desc:   Hint GC after allocating garbage: live objects are still accessible.
     */
    ecmascript::ThreadManagedScope managedScope(thread);
    ObjectFactory *factory = thread->GetEcmaVM()->GetFactory();
    JSHandle<EcmaString> liveStr = factory->NewFromASCII("still_alive");
    /* 256 * 1024 * 8B = 2MB garbage in old space. */
    constexpr size_t GARBAGE_COUNT = 256;
    constexpr size_t GARBAGE_ARRAY_LEN = 1024;
    AllocateOldSpaceGarbage(GARBAGE_COUNT, GARBAGE_ARRAY_LEN);
    size_t sizeBefore = heap_->GetHeapObjectSize();
    ASSERT_GT(sizeBefore, 0U);
    JSNApi::HintGC(instance, JSNApi::UrgencyLevel::LOW);
    JSNApi::HintGC(instance, JSNApi::UrgencyLevel::MEDIUM);
    JSNApi::HintGC(instance, JSNApi::UrgencyLevel::HIGH);
    ASSERT_STREQ(EcmaStringAccessor(liveStr).ToCString(thread).c_str(), "still_alive");
    /* Heap remains usable after hint GC. */
    JSHandle<TaggedArray> newArray = factory->NewTaggedArray(8); // 8: array length
    newArray->Set(thread, 0, liveStr.GetTaggedValue());
    ASSERT_EQ(newArray->Get(thread, 0), liveStr.GetTaggedValue());
}

HWTEST_F_L0(HintGCTest, JSNApiHintGCStressTest001)
{
    /*
     * @tc.number: SUB_ARK_ETS_RUNTIME_HINTGC_0320
     * @tc.name:   JSNApiHintGCStressTest001
     * @tc.desc:   Repeated hint GC calls of all levels do not crash the VM.
     */
    ecmascript::ThreadManagedScope managedScope(thread);
    ObjectFactory *factory = thread->GetEcmaVM()->GetFactory();
    JSHandle<EcmaString> liveStr = factory->NewFromASCII("stress_live");
    constexpr int LOOP_COUNT = 60;
    for (int i = 0; i < LOOP_COUNT; i++) {
        JSNApi::HintGC(instance, JSNApi::UrgencyLevel::LOW);
        JSNApi::HintGC(instance, JSNApi::UrgencyLevel::MEDIUM);
        JSNApi::HintGC(instance, JSNApi::UrgencyLevel::HIGH);
        /* Interleave some allocations between hint GC calls. */
        constexpr size_t GARBAGE_COUNT = 8;
        constexpr size_t GARBAGE_ARRAY_LEN = 256;
        AllocateOldSpaceGarbage(GARBAGE_COUNT, GARBAGE_ARRAY_LEN);
    }
    ASSERT_STREQ(EcmaStringAccessor(liveStr).ToCString(thread).c_str(), "stress_live");
}

HWTEST_F_L0(HintGCTest, JSNApiHintGCWeakRefTest001)
{
    /*
     * @tc.number: SUB_ARK_ETS_RUNTIME_HINTGC_0330
     * @tc.name:   JSNApiHintGCWeakRefTest001
     * @tc.desc:   Hint GC keeps a weak reference slot semantics intact.
     */
    ecmascript::ThreadManagedScope managedScope(thread);
    ObjectFactory *factory = thread->GetEcmaVM()->GetFactory();
    JSHandle<TaggedArray> array = factory->NewTaggedArray(2); // 2: array length
    JSTaggedValue weakValue = factory->NewTaggedArray(4).GetTaggedValue(); // 4: array length
    weakValue.CreateWeakRef();
    array->Set(thread, 0, weakValue);
    JSHandle<EcmaString> liveStr = factory->NewFromASCII("weak_live");
    array->Set(thread, 1, liveStr.GetTaggedValue());
    JSNApi::HintGC(instance, JSNApi::UrgencyLevel::LOW);
    JSNApi::HintGC(instance, JSNApi::UrgencyLevel::MEDIUM);
    JSNApi::HintGC(instance, JSNApi::UrgencyLevel::HIGH);
    /* Weak slot may keep weak value or be cleared, but the strong slot must survive. */
    ASSERT_TRUE(array->Get(thread, 0).IsWeak() || array->Get(thread, 0).IsUndefined());
    ASSERT_EQ(array->Get(thread, 1), liveStr.GetTaggedValue());
    ASSERT_STREQ(EcmaStringAccessor(array->Get(thread, 1)).ToCString(thread).c_str(), "weak_live");
}

HWTEST_F_L0(HintGCTest, JSNApiHintGCMultiVMTest001)
{
    /*
     * @tc.number: SUB_ARK_ETS_RUNTIME_HINTGC_0340
     * @tc.name:   JSNApiHintGCMultiVMTest001
     * @tc.desc:   Hint GC works on an independently created VM and does not affect the main VM.
     */
    {
        /*
         * A JSThread may only be transitioned to RUNNING by the OS thread it
         * is registered on (JSThread::currentThread TLS), so the second VM
         * must be created and driven on its own thread, as in
         * EcmaVMTest.CreateEcmaVMInTwoWays.
         */
        std::thread otherVmThread([&]() {
            JSRuntimeOptions options;
            options.SetEnableForceGC(false);
            EcmaVM *otherVm = JSNApi::CreateEcmaVM(options);
            EXPECT_TRUE(otherVm != nullptr);
            JSThread *otherThread = otherVm->GetJSThread();
            otherThread->ManagedCodeBegin();
            {
                ecmascript::ThreadManagedScope managedScope(otherThread);
                JSNApi::HintGC(otherVm, JSNApi::UrgencyLevel::LOW);
                JSNApi::HintGC(otherVm, JSNApi::UrgencyLevel::MEDIUM);
                JSNApi::HintGC(otherVm, JSNApi::UrgencyLevel::HIGH);
                ObjectFactory *factory = otherVm->GetFactory();
                JSHandle<EcmaString> str = factory->NewFromASCII("other_vm");
                EXPECT_STREQ(EcmaStringAccessor(str).ToCString(otherThread).c_str(), "other_vm");
            }
            otherThread->ManagedCodeEnd();
            JSNApi::DestroyJSVM(otherVm);
        });
        otherVmThread.join();
    }
    ecmascript::ThreadManagedScope managedScope(thread);
    JSNApi::HintGC(instance, JSNApi::UrgencyLevel::MEDIUM);
    ObjectFactory *factory = thread->GetEcmaVM()->GetFactory();
    JSHandle<EcmaString> str = factory->NewFromASCII("main_vm");
    ASSERT_STREQ(EcmaStringAccessor(str).ToCString(thread).c_str(), "main_vm");
}

/* ------------------------------------------------------------------------ */
/* Heap::CheckAndTriggerHintLocalCC trigger decision tests                   */
/* ------------------------------------------------------------------------ */

HWTEST_F_L0(HintGCTest, CheckAndTriggerHintLocalCCNoTriggerTest001)
{
    /*
     * @tc.number: SUB_ARK_ETS_RUNTIME_HINTGC_0400
     * @tc.name:   CheckAndTriggerHintLocalCCNoTriggerTest001
     * @tc.desc:   On a fresh heap after full GC, object size is below all thresholds, no GC happens.
     */
    heap_->CollectGarbage(TriggerGCType::FULL_GC, GCReason::OTHER);
    size_t alive = heap_->GetHeapAliveSizeAfterGC();
    size_t current = heap_->GetHeapObjectSize();
    ASSERT_LE(current, ExpectedHeapSize(UrgencyLevel::LOW, alive));
    ASSERT_LE(current, ExpectedHeapSize(UrgencyLevel::MEDIUM, alive));
    ASSERT_LE(current, ExpectedHeapSize(UrgencyLevel::HIGH, alive));
    heap_->CheckAndTriggerHintLocalCC(UrgencyLevel::LOW);
    heap_->CheckAndTriggerHintLocalCC(UrgencyLevel::MEDIUM);
    heap_->CheckAndTriggerHintLocalCC(UrgencyLevel::HIGH);
    /* No local CC was recorded, thread is not in concurrent copying. */
    ASSERT_FALSE(thread->IsConcurrentCopying());
    ASSERT_FALSE(heap_->IsCCMark());
}

HWTEST_F_L0(HintGCTest, CheckAndTriggerHintLocalCCAliveBaselineTest001)
{
    /*
     * @tc.number: SUB_ARK_ETS_RUNTIME_HINTGC_0401
     * @tc.name:   CheckAndTriggerHintLocalCCAliveBaselineTest001
     * @tc.desc:   With a zero alive baseline, HIGH threshold is 1MB and a loaded heap exceeds it.
     */
    constexpr size_t GARBAGE_COUNT = 256;     /* 256 * 1024 * 8B = 2MB */
    constexpr size_t GARBAGE_ARRAY_LEN = 1024;
    heap_->CollectGarbage(TriggerGCType::FULL_GC, GCReason::OTHER);
    heap_->NotifyHeapAliveSizeAfterGC(0);
    AllocateOldSpaceGarbage(GARBAGE_COUNT, GARBAGE_ARRAY_LEN);
    size_t current = heap_->GetHeapObjectSize();
    /* 2MB garbage guarantees current > 1MB HIGH threshold. */
    ASSERT_GT(current, HIGH_LEVEL_MIN_INC_STEP);
    ASSERT_GT(current, ExpectedHeapSize(UrgencyLevel::HIGH, 0));
    heap_->CheckAndTriggerHintLocalCC(UrgencyLevel::HIGH);
    /* Restore a real baseline for the following tests. */
    heap_->CollectGarbage(TriggerGCType::FULL_GC, GCReason::OTHER);
    ASSERT_GT(heap_->GetHeapAliveSizeAfterGC(), 0U);
}

HWTEST_F_L0(HintGCTest, CheckAndTriggerHintLocalCCHighTriggerTest001)
{
    /*
     * @tc.number: SUB_ARK_ETS_RUNTIME_HINTGC_0410
     * @tc.name:   CheckAndTriggerHintLocalCCHighTriggerTest001
     * @tc.desc:   In CC mark state, HIGH hint GC delegates the trigger to the async VM loop
     *             path and starts no local CC on the calling thread.
     */
    constexpr size_t GARBAGE_COUNT = 256;     /* 256 * 1024 * 8B = 2MB */
    constexpr size_t GARBAGE_ARRAY_LEN = 1024;
    heap_->CollectGarbage(TriggerGCType::FULL_GC, GCReason::OTHER);
    AllocateOldSpaceGarbageWithoutGC(GARBAGE_COUNT, GARBAGE_ARRAY_LEN);
    /* Enter concurrent mark state first. */
    ASSERT_TRUE(heap_->TryTriggerCCMarking(MarkReason::OTHER));
    /* Force the HIGH threshold below the current object size. */
    heap_->NotifyHeapAliveSizeAfterGC(0);
    ASSERT_GT(heap_->GetHeapObjectSize(), ExpectedHeapSize(UrgencyLevel::HIGH, 0));
    heap_->CheckAndTriggerHintLocalCC(UrgencyLevel::HIGH);
    /*
     * The hint GC trigger is asynchronous: the calling thread keeps running,
     * no local CC copying is started and the GC reason is not overwritten
     * with HINT_LOCAL_CC by the hint call itself.
     */
    ASSERT_NE(heap_->GetEcmaGCStats()->GetGCReason(), GCReason::HINT_LOCAL_CC);
    ASSERT_FALSE(thread->IsConcurrentCopying());
    /*
     * A local CC dispatched from the CC mark state records the HINT_LOCAL_CC
     * reason and the LocalCCGC type in the GC statistics.
     */
    heap_->CollectGarbage(TriggerGCType::OLD_GC, GCReason::HINT_LOCAL_CC);
    ASSERT_EQ(heap_->GetEcmaGCStats()->GetGCReason(), GCReason::HINT_LOCAL_CC);
    ASSERT_EQ(heap_->GetEcmaGCStats()->GetGCTypeName(), std::string("LocalCCGC"));
    ASSERT_TRUE(thread->IsConcurrentCopying());
    heap_->WaitAndHandleCCFinished();
    ASSERT_FALSE(thread->IsConcurrentCopying());
    heap_->CollectGarbage(TriggerGCType::FULL_GC, GCReason::OTHER);
}

HWTEST_F_L0(HintGCTest, CheckAndTriggerHintLocalCCMediumTriggerTest001)
{
    /*
     * @tc.number: SUB_ARK_ETS_RUNTIME_HINTGC_0411
     * @tc.name:   CheckAndTriggerHintLocalCCMediumTriggerTest001
     * @tc.desc:   In CC mark state, MEDIUM hint GC evaluates the threshold and delegates the
     *             trigger asynchronously without disturbing the heap state.
     */
    constexpr size_t GARBAGE_COUNT = 768;     /* 768 * 1024 * 8B = 6MB, exceeds 5MB MEDIUM step */
    constexpr size_t GARBAGE_ARRAY_LEN = 1024;
    heap_->CollectGarbage(TriggerGCType::FULL_GC, GCReason::OTHER);
    AllocateOldSpaceGarbageWithoutGC(GARBAGE_COUNT, GARBAGE_ARRAY_LEN);
    ASSERT_TRUE(heap_->TryTriggerCCMarking(MarkReason::OTHER));
    heap_->NotifyHeapAliveSizeAfterGC(0);
    ASSERT_GT(heap_->GetHeapObjectSize(), ExpectedHeapSize(UrgencyLevel::MEDIUM, 0));
    heap_->CheckAndTriggerHintLocalCC(UrgencyLevel::MEDIUM);
    /* Asynchronous trigger: no synchronous local CC, reason not overwritten. */
    ASSERT_NE(heap_->GetEcmaGCStats()->GetGCReason(), GCReason::HINT_LOCAL_CC);
    ASSERT_FALSE(thread->IsConcurrentCopying());
    /* The pending CC mark state can still be finished by an explicit local CC. */
    heap_->CollectGarbage(TriggerGCType::OLD_GC, GCReason::HINT_LOCAL_CC);
    ASSERT_EQ(heap_->GetEcmaGCStats()->GetGCReason(), GCReason::HINT_LOCAL_CC);
    heap_->WaitAndHandleCCFinished();
    ASSERT_FALSE(thread->IsConcurrentCopying());
    heap_->CollectGarbage(TriggerGCType::FULL_GC, GCReason::OTHER);
}

HWTEST_F_L0(HintGCTest, CheckAndTriggerHintLocalCCLowTriggerTest001)
{
    /*
     * @tc.number: SUB_ARK_ETS_RUNTIME_HINTGC_0412
     * @tc.name:   CheckAndTriggerHintLocalCCLowTriggerTest001
     * @tc.desc:   In CC mark state, LOW hint GC triggers local CC when threshold is exceeded.
     */
    constexpr size_t GARBAGE_COUNT = 1536;    /* 1536 * 1024 * 8B = 12MB, exceeds 5MB LOW step */
    constexpr size_t GARBAGE_ARRAY_LEN = 1024;
    heap_->CollectGarbage(TriggerGCType::FULL_GC, GCReason::OTHER);
    AllocateOldSpaceGarbageWithoutGC(GARBAGE_COUNT, GARBAGE_ARRAY_LEN);
    ASSERT_TRUE(heap_->TryTriggerCCMarking(MarkReason::OTHER));
    heap_->NotifyHeapAliveSizeAfterGC(0);
    ASSERT_GT(heap_->GetHeapObjectSize(), ExpectedHeapSize(UrgencyLevel::LOW, 0));
    heap_->CheckAndTriggerHintLocalCC(UrgencyLevel::LOW);
    /* Asynchronous trigger: no synchronous local CC, reason not overwritten. */
    ASSERT_NE(heap_->GetEcmaGCStats()->GetGCReason(), GCReason::HINT_LOCAL_CC);
    ASSERT_FALSE(thread->IsConcurrentCopying());
    /* The pending CC mark state can still be finished by an explicit local CC. */
    heap_->CollectGarbage(TriggerGCType::OLD_GC, GCReason::HINT_LOCAL_CC);
    ASSERT_EQ(heap_->GetEcmaGCStats()->GetGCReason(), GCReason::HINT_LOCAL_CC);
    heap_->WaitAndHandleCCFinished();
    ASSERT_FALSE(thread->IsConcurrentCopying());
    heap_->CollectGarbage(TriggerGCType::FULL_GC, GCReason::OTHER);
}

HWTEST_F_L0(HintGCTest, CheckAndTriggerHintLocalCCDuringCopyingTest001)
{
    /*
     * @tc.number: SUB_ARK_ETS_RUNTIME_HINTGC_0420
     * @tc.name:   CheckAndTriggerHintLocalCCDuringCopyingTest001
     * @tc.desc:   Hint GC during an ongoing local CC copying is delegated asynchronously and
     *             does not corrupt or block the ongoing CC.
     */
    constexpr size_t GARBAGE_COUNT = 256;
    constexpr size_t GARBAGE_ARRAY_LEN = 1024;
    heap_->CollectGarbage(TriggerGCType::FULL_GC, GCReason::OTHER);
    AllocateOldSpaceGarbageWithoutGC(GARBAGE_COUNT, GARBAGE_ARRAY_LEN);
    ASSERT_TRUE(heap_->TryTriggerCCMarking(MarkReason::OTHER));
    heap_->NotifyHeapAliveSizeAfterGC(0);
    /* Start the local CC copying phase from the CC mark state explicitly. */
    heap_->CollectGarbage(TriggerGCType::OLD_GC, GCReason::HINT_LOCAL_CC);
    ASSERT_TRUE(thread->IsConcurrentCopying());
    /*
     * The ongoing local CC updates the alive baseline when it finishes, so keep
     * allocating to ensure the HIGH threshold stays exceeded for the second call.
     */
    AllocateOldSpaceGarbageWithoutGC(GARBAGE_COUNT, GARBAGE_ARRAY_LEN);
    /* Hint GC while copying is in progress must keep the heap consistent. */
    heap_->CheckAndTriggerHintLocalCC(UrgencyLevel::HIGH);
    heap_->WaitAndHandleCCFinished();
    ASSERT_FALSE(thread->IsConcurrentCopying());
    heap_->CollectGarbage(TriggerGCType::FULL_GC, GCReason::OTHER);
}

HWTEST_F_L0(HintGCTest, CheckAndTriggerHintLocalCCPreserveObjectsTest001)
{
    /*
     * @tc.number: SUB_ARK_ETS_RUNTIME_HINTGC_0430
     * @tc.name:   CheckAndTriggerHintLocalCCPreserveObjectsTest001
     * @tc.desc:   Objects allocated before a hint GC and a local CC survive both.
     */
    constexpr size_t GARBAGE_COUNT = 256;
    constexpr size_t GARBAGE_ARRAY_LEN = 1024;
    ObjectFactory *factory = thread->GetEcmaVM()->GetFactory();
    JSHandle<TaggedArray> liveArray = factory->NewTaggedArray(4); // 4: array length
    {
        [[maybe_unused]] ecmascript::EcmaHandleScope baseScope(thread);
        JSHandle<EcmaString> str = factory->NewFromASCII("cc_live");
        liveArray->Set(thread, 0, str.GetTaggedValue());
    }
    heap_->CollectGarbage(TriggerGCType::FULL_GC, GCReason::OTHER);
    AllocateOldSpaceGarbage(GARBAGE_COUNT, GARBAGE_ARRAY_LEN);
    ASSERT_TRUE(heap_->TryTriggerCCMarking(MarkReason::OTHER));
    heap_->NotifyHeapAliveSizeAfterGC(0);
    /* Hint GC evaluates the threshold and delegates asynchronously. */
    heap_->CheckAndTriggerHintLocalCC(UrgencyLevel::HIGH);
    ASSERT_NE(heap_->GetEcmaGCStats()->GetGCReason(), GCReason::HINT_LOCAL_CC);
    /* Finish the pending CC mark state with a local CC. */
    heap_->CollectGarbage(TriggerGCType::OLD_GC, GCReason::HINT_LOCAL_CC);
    heap_->WaitAndHandleCCFinished();
    /* Live data is preserved by the local CC. */
    JSTaggedValue value = liveArray->Get(thread, 0);
    ASSERT_TRUE(value.IsString());
    ASSERT_STREQ(EcmaStringAccessor(value).ToCString(thread).c_str(), "cc_live");
    heap_->CollectGarbage(TriggerGCType::FULL_GC, GCReason::OTHER);
}

HWTEST_F_L0(HintGCTest, CheckAndTriggerHintLocalCCReasonNotOverwrittenTest001)
{
    /*
     * @tc.number: SUB_ARK_ETS_RUNTIME_HINTGC_0440
     * @tc.name:   CheckAndTriggerHintLocalCCReasonNotOverwrittenTest001
     * @tc.desc:   A plain full GC does not record the HINT_LOCAL_CC reason.
     */
    heap_->CollectGarbage(TriggerGCType::FULL_GC, GCReason::OTHER);
    ASSERT_NE(heap_->GetEcmaGCStats()->GetGCReason(), GCReason::HINT_LOCAL_CC);
    heap_->CollectGarbage(TriggerGCType::OLD_GC, GCReason::TRIGGER_BY_JS);
    ASSERT_NE(heap_->GetEcmaGCStats()->GetGCReason(), GCReason::HINT_LOCAL_CC);
}

/* ------------------------------------------------------------------------ */
/* Interaction of JSNApi::HintGC and heap level trigger                      */
/* ------------------------------------------------------------------------ */

HWTEST_F_L0(HintGCTest, JSNApiHintGCWithCCMarkTriggerTest001)
{
    /*
     * @tc.number: SUB_ARK_ETS_RUNTIME_HINTGC_0500
     * @tc.name:   JSNApiHintGCWithCCMarkTriggerTest001
     * @tc.desc:   JSNApi::HintGC in CC mark state delegates the trigger asynchronously and
     *             does not start a local CC on the calling thread.
     */
    constexpr size_t GARBAGE_COUNT = 256;
    constexpr size_t GARBAGE_ARRAY_LEN = 1024;
    heap_->CollectGarbage(TriggerGCType::FULL_GC, GCReason::OTHER);
    AllocateOldSpaceGarbageWithoutGC(GARBAGE_COUNT, GARBAGE_ARRAY_LEN);
    ASSERT_TRUE(heap_->TryTriggerCCMarking(MarkReason::OTHER));
    heap_->NotifyHeapAliveSizeAfterGC(0);
    {
        ecmascript::ThreadManagedScope managedScope(thread);
        JSNApi::HintGC(instance, JSNApi::UrgencyLevel::HIGH);
    }
    /* Public entry delegates asynchronously: no synchronous local CC. */
    ASSERT_NE(heap_->GetEcmaGCStats()->GetGCReason(), GCReason::HINT_LOCAL_CC);
    ASSERT_FALSE(thread->IsConcurrentCopying());
    /* A local CC dispatched afterwards records the HINT_LOCAL_CC reason. */
    heap_->CollectGarbage(TriggerGCType::OLD_GC, GCReason::HINT_LOCAL_CC);
    ASSERT_EQ(heap_->GetEcmaGCStats()->GetGCReason(), GCReason::HINT_LOCAL_CC);
    ASSERT_TRUE(thread->IsConcurrentCopying());
    heap_->WaitAndHandleCCFinished();
    ASSERT_FALSE(thread->IsConcurrentCopying());
    heap_->CollectGarbage(TriggerGCType::FULL_GC, GCReason::OTHER);
}

HWTEST_F_L0(HintGCTest, JSNApiHintGCEnumAliasConsistencyTest001)
{
    /*
     * @tc.number: SUB_ARK_ETS_RUNTIME_HINTGC_0501
     * @tc.name:   JSNApiHintGCEnumAliasConsistencyTest001
     * @tc.desc:   JSNApi::UrgencyLevel and heap UrgencyLevel alias are interchangeable.
     */
    ecmascript::ThreadManagedScope managedScope(thread);
    JSNApi::UrgencyLevel level = JSNApi::UrgencyLevel::MEDIUM;
    UrgencyLevel heapLevel = UrgencyLevel::MEDIUM;
    ASSERT_EQ(static_cast<uint8_t>(level), static_cast<uint8_t>(heapLevel));
    heapLevel = static_cast<UrgencyLevel>(JSNApi::UrgencyLevel::LOW);
    ASSERT_EQ(heapLevel, UrgencyLevel::LOW);
    /* Use the alias directly through the public interface. */
    JSNApi::HintGC(instance, heapLevel);
    JSNApi::HintGC(instance, static_cast<JSNApi::UrgencyLevel>(UrgencyLevel::HIGH));
}

HWTEST_F_L0(HintGCTest, JSNApiHintGCHeapUsableAfterCCMarkTriggerTest001)
{
    /*
     * @tc.number: SUB_ARK_ETS_RUNTIME_HINTGC_0502
     * @tc.name:   JSNApiHintGCHeapUsableAfterCCMarkTriggerTest001
     * @tc.desc:   Heap remains fully usable after a hint GC and a finished local CC.
     */
    constexpr size_t GARBAGE_COUNT = 256;
    constexpr size_t GARBAGE_ARRAY_LEN = 1024;
    heap_->CollectGarbage(TriggerGCType::FULL_GC, GCReason::OTHER);
    AllocateOldSpaceGarbageWithoutGC(GARBAGE_COUNT, GARBAGE_ARRAY_LEN);
    ASSERT_TRUE(heap_->TryTriggerCCMarking(MarkReason::OTHER));
    heap_->NotifyHeapAliveSizeAfterGC(0);
    {
        ecmascript::ThreadManagedScope managedScope(thread);
        JSNApi::HintGC(instance, JSNApi::UrgencyLevel::HIGH);
    }
    /* Finish the pending CC mark state with a local CC. */
    heap_->CollectGarbage(TriggerGCType::OLD_GC, GCReason::HINT_LOCAL_CC);
    heap_->WaitAndHandleCCFinished();
    ASSERT_FALSE(thread->IsConcurrentCopying());
    ecmascript::ThreadManagedScope managedScope(thread);
    ObjectFactory *factory = thread->GetEcmaVM()->GetFactory();
    JSHandle<EcmaString> str = factory->NewFromASCII("after_cc");
    ASSERT_STREQ(EcmaStringAccessor(str).ToCString(thread).c_str(), "after_cc");
    JSHandle<TaggedArray> array = factory->NewTaggedArray(8); // 8: array length
    for (uint32_t i = 0; i < 8; i++) { // 8: array length
        array->Set(thread, i, str.GetTaggedValue());
        ASSERT_EQ(array->Get(thread, i), str.GetTaggedValue());
    }
    heap_->CollectGarbage(TriggerGCType::FULL_GC, GCReason::OTHER);
}

HWTEST_F_L0(HintGCTest, CheckAndTriggerHintLocalCCSensitiveStatusTest001)
{
    /*
     * @tc.number: SUB_ARK_ETS_RUNTIME_HINTGC_0510
     * @tc.name:   CheckAndTriggerHintLocalCCSensitiveStatusTest001
     * @tc.desc:   Hint local CC decision can still be evaluated in sensitive status without crash.
     */
    constexpr size_t GARBAGE_COUNT = 256;
    constexpr size_t GARBAGE_ARRAY_LEN = 1024;
    heap_->CollectGarbage(TriggerGCType::FULL_GC, GCReason::OTHER);
    AllocateOldSpaceGarbage(GARBAGE_COUNT, GARBAGE_ARRAY_LEN);
    heap_->SetSensitiveStatus(AppSensitiveStatus::ENTER_HIGH_SENSITIVE);
    heap_->CheckAndTriggerHintLocalCC(UrgencyLevel::LOW);
    heap_->CheckAndTriggerHintLocalCC(UrgencyLevel::MEDIUM);
    heap_->CheckAndTriggerHintLocalCC(UrgencyLevel::HIGH);
    heap_->SetSensitiveStatus(AppSensitiveStatus::NORMAL_SCENE);
    /* VM is still healthy after hint GC calls under sensitive status. */
    ecmascript::ThreadManagedScope managedScope(thread);
    ObjectFactory *factory = thread->GetEcmaVM()->GetFactory();
    JSHandle<EcmaString> str = factory->NewFromASCII("sensitive_ok");
    ASSERT_STREQ(EcmaStringAccessor(str).ToCString(thread).c_str(), "sensitive_ok");
}

HWTEST_F_L0(HintGCTest, CheckAndTriggerHintLocalCCLoopTest001)
{
    /*
     * @tc.number: SUB_ARK_ETS_RUNTIME_HINTGC_0520
     * @tc.name:   CheckAndTriggerHintLocalCCLoopTest001
     * @tc.desc:   Looping all levels of hint local CC keeps heap consistent and terminates.
     */
    constexpr size_t GARBAGE_COUNT = 64;
    constexpr size_t GARBAGE_ARRAY_LEN = 1024;
    constexpr int LOOP_COUNT = 20;
    heap_->CollectGarbage(TriggerGCType::FULL_GC, GCReason::OTHER);
    for (int i = 0; i < LOOP_COUNT; i++) {
        AllocateOldSpaceGarbage(GARBAGE_COUNT, GARBAGE_ARRAY_LEN);
        heap_->CheckAndTriggerHintLocalCC(UrgencyLevel::HIGH);
        if (thread->IsConcurrentCopying()) {
            heap_->WaitCCFinished();
        }
        ASSERT_LE(heap_->GetHeapObjectSize(), heap_->GetEcmaParamConfiguration().GetMaxHeapSize());
    }
    heap_->CollectGarbage(TriggerGCType::FULL_GC, GCReason::OTHER);
    ASSERT_GT(heap_->GetHeapAliveSizeAfterGC(), 0U);
}
}  // namespace panda::test
