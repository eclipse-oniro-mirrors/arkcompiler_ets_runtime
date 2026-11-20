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

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <type_traits>
#include <utility>
#include <vector>

#include "ecmascript/daemon/daemon_thread.h"
#include "ecmascript/js_thread.h"
#include "ecmascript/linked_hash_table.h"
#include "ecmascript/mem/concurrent_sweeper.h"
#include "ecmascript/mem/gc_stats.h"
#include "ecmascript/mem/global_cc/global_cc.h"
#include "ecmascript/mem/heap-inl.h"
#include "ecmascript/mem/idle_gc_trigger.h"
#include "ecmascript/mem/region-inl.h"
#include "ecmascript/module/js_module_manager.h"
#include "ecmascript/module/js_module_source_text.h"
#include "ecmascript/napi/include/jsnapi_expo.h"
#include "ecmascript/object_factory-inl.h"
#include "ecmascript/runtime.h"
#include "ecmascript/serializer/base_deserializer.h"
#include "ecmascript/serializer/value_serializer.h"
#include "ecmascript/snapshot/mem/snapshot_env.h"

using namespace panda::ecmascript;

namespace {
struct GCCallbackTestState {
    static void DeleteNative([[maybe_unused]] void *env, [[maybe_unused]] void *pointer, void *data)
    {
        auto *state = static_cast<GCCallbackTestState *>(data);
        EXPECT_EQ(JSThread::GetCurrentThreadId(), state->thread->GetThreadId());
        state->nativeCount++;
        if (state->onDelete) {
            auto callback = std::move(state->onDelete);
            callback();
        }
    }

    static void FreeWeak(void *data)
    {
        auto *state = static_cast<GCCallbackTestState *>(data);
        state->freeWeakCount++;
        state->thread->DisposeGlobalHandle(state->weakSlot);
    }

    static void FinalizeWeak(void *data)
    {
        auto *state = static_cast<GCCallbackTestState *>(data);
        EXPECT_EQ(JSThread::GetCurrentThreadId(), state->thread->GetThreadId());
        state->weakCount++;
        if (state->onFinalize) {
            auto callback = std::move(state->onFinalize);
            callback();
        }
    }

    JSThread *thread {nullptr};
    uintptr_t weakSlot {0};
    std::function<void()> onDelete;
    std::function<void()> onFinalize;
    int nativeCount {0};
    int freeWeakCount {0};
    int weakCount {0};
    int expectedNativeCount {0};
    int expectedWeakCount {0};
    int expectedFinalizeCount {0};
};

struct ConcurrentGCCallbackTestState {
    static void DeleteNative([[maybe_unused]] void *env, [[maybe_unused]] void *pointer, void *data)
    {
        auto *state = static_cast<ConcurrentGCCallbackTestState *>(data);
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            state->count.fetch_add(1, std::memory_order_relaxed);
            state->cv.notify_all();
        }
    }

    std::mutex mutex;
    std::condition_variable cv;
    std::atomic_uint count {0};
};
}  // namespace

namespace panda::test {

class GlobalCCTest : public BaseTestWithScope<false> {
public:
    void SetUp() override
    {
        JSRuntimeOptions options;
        options.SetEnableForceGC(false);
        instance = JSNApi::CreateEcmaVM(options);
        ASSERT_TRUE(instance != nullptr) << "Cannot create EcmaVM";
        thread = instance->GetJSThread();
        thread->ManagedCodeBegin();
        scope = new EcmaHandleScope(thread);
        thread->SetCrossThreadExecution(true);
    }

    void TearDown() override
    {
        BaseTestWithScope<false>::TearDown();
        for (const auto &state : callbackStates_) {
            EXPECT_EQ(state->nativeCount, state->expectedNativeCount);
            EXPECT_EQ(state->freeWeakCount, state->expectedWeakCount);
            EXPECT_EQ(state->weakCount, state->expectedFinalizeCount);
        }
        for (const auto &state : concurrentCallbackStates_) {
            EXPECT_EQ(state->count.load(std::memory_order_relaxed), 1U);
        }
        EXPECT_EQ(destructionAsyncPostCount_, 0);
    }

    GCCallbackTestState *NewCallbackState()
    {
        auto state = std::make_unique<GCCallbackTestState>();
        state->thread = thread;
        auto *result = state.get();
        callbackStates_.emplace_back(std::move(state));
        return result;
    }

    void AllocateWeakObject(GCCallbackTestState *state)
    {
        EcmaHandleScope localScope(thread);
        auto object = instance->GetFactory()->NewEmptyJSObject();
        state->weakSlot = thread->NewGlobalHandle(object.GetTaggedValue().GetRawData());
        state->weakSlot = thread->SetWeak(state->weakSlot, state,
            GCCallbackTestState::FreeWeak, GCCallbackTestState::FinalizeWeak);
        state->expectedWeakCount = 1;
        state->expectedFinalizeCount = 1;
    }

    void TriggerGlobalCC()
    {
        auto *sHeap = SharedHeap::GetInstance();
        sHeap->CollectGarbage<TriggerGCType::GLOBAL_CC, GCReason::OTHER>(thread);
        instance->GetHeap()->WaitAllTasksFinished();
        sHeap->WaitAllTasksFinished(thread);
    }

    void RunFullGC()
    {
        auto *heap = instance->GetHeap();
        bool crossThreadExecution = thread->IsCrossThreadExecutionEnable();
        thread->SetCrossThreadExecution(false);
        heap->CollectGarbage(TriggerGCType::FULL_GC, GCReason::OTHER);
        heap->WaitAllTasksFinished();
        thread->SetCrossThreadExecution(crossThreadExecution);
    }

    std::vector<std::unique_ptr<GCCallbackTestState>> callbackStates_;
    std::vector<std::unique_ptr<ConcurrentGCCallbackTestState>> concurrentCallbackStates_;
    int destructionAsyncPostCount_ {0};

    JSHandle<TaggedArray> AllocateSharedArray(size_t arrayLen)
    {
        ObjectFactory *factory = instance->GetFactory();
        return factory->NewSOldSpaceTaggedArray(arrayLen, JSTaggedValue::Undefined());
    }
};

HWTEST_F_L0(GlobalCCTest, GlobalCCWeakAggregateClosureTest)
{
    JSMutableHandle<WeakLinkedHashMap> table(thread, WeakLinkedHashMap::Create(thread));
    JSHandle<JSObject> liveKey = instance->GetFactory()->NewEmptyJSObject();
    {
        EcmaHandleScope temporaryScope(thread);
        JSHandle<JSObject> dependentKey = instance->GetFactory()->NewEmptyJSObject();
        JSHandle<TaggedArray> target = instance->GetFactory()->NewTaggedArray(1);
        target->Set(thread, 0, JSTaggedValue(42));

        // The target is retained only after the live key retains dependentKey
        // and weak-aggregate closure is reached.
        table.Update(WeakLinkedHashMap::SetWeakRef(thread, table,
            JSHandle<JSTaggedValue>::Cast(dependentKey), JSHandle<JSTaggedValue>::Cast(target)));
        table.Update(WeakLinkedHashMap::SetWeakRef(thread, table,
            JSHandle<JSTaggedValue>::Cast(liveKey), JSHandle<JSTaggedValue>::Cast(dependentKey)));
    }

    TriggerGlobalCC();

    int firstEntry = table->FindElement(thread, liveKey.GetTaggedValue());
    ASSERT_NE(firstEntry, -1);
    JSTaggedValue dependentKey = table->GetValue(thread, firstEntry);
    ASSERT_TRUE(dependentKey.IsECMAObject());
    int secondEntry = table->FindElement(thread, dependentKey);
    ASSERT_NE(secondEntry, -1);
    JSTaggedValue target = table->GetValue(thread, secondEntry);
    ASSERT_TRUE(target.IsTaggedArray());
    EXPECT_EQ(TaggedArray::Cast(target.GetTaggedObject())->Get(thread, 0), JSTaggedValue(42));
}

HWTEST_F_L0(GlobalCCTest, GlobalCCWeakClosureTest)
{
    constexpr size_t arrayLen = 10;
    JSHandle<TaggedArray> holder = instance->GetFactory()->NewTaggedArray(arrayLen);
    JSHandle<TaggedArray> target = instance->GetFactory()->NewTaggedArray(arrayLen);

    JSTaggedValue weak = target.GetTaggedValue();
    weak.CreateWeakRef();
    holder->Set(thread, 0, weak);
    {
        EcmaHandleScope temporaryScope(thread);
        auto deadTarget = instance->GetFactory()->NewTaggedArray(arrayLen);
        JSTaggedValue deadWeak = deadTarget.GetTaggedValue();
        deadWeak.CreateWeakRef();
        holder->Set(thread, 1, deadWeak);
    }
    ASSERT_TRUE(holder->Get(thread, 0).IsWeak());
    ASSERT_TRUE(holder->Get(thread, 1).IsWeak());

    TriggerGlobalCC();

    // Live target: weak ref must be retained.
    JSTaggedValue afterGC = holder->Get(thread, 0);
    ASSERT_TRUE(afterGC.IsWeak());
    EXPECT_EQ(afterGC.GetWeakReferent(), target.GetTaggedValue().GetRawHeapObject());
    EXPECT_TRUE(holder->Get(thread, 1).IsUndefined());
}

HWTEST_F_L0(GlobalCCTest, GlobalCCSharedWeakClosureTest)
{
    constexpr size_t arrayLen = 10;
    JSHandle<TaggedArray> holder = AllocateSharedArray(arrayLen);
    JSHandle<TaggedArray> target = AllocateSharedArray(arrayLen);

    JSTaggedValue weak = target.GetTaggedValue();
    weak.CreateWeakRef();
    holder->Set(thread, 0, weak);
    {
        EcmaHandleScope temporaryScope(thread);
        auto deadTarget = AllocateSharedArray(arrayLen);
        JSTaggedValue deadWeak = deadTarget.GetTaggedValue();
        deadWeak.CreateWeakRef();
        holder->Set(thread, 1, deadWeak);
    }

    TriggerGlobalCC();

    JSTaggedValue afterGC = holder->Get(thread, 0);
    ASSERT_TRUE(afterGC.IsWeak());
    EXPECT_EQ(afterGC.GetWeakReferent(), target.GetTaggedValue().GetRawHeapObject());
    EXPECT_TRUE(holder->Get(thread, 1).IsUndefined());
}

#ifdef PANDA_TARGET_64
#if ENABLE_MODULE_SPACE && !USE_CMS_GC
HWTEST_F_L0(GlobalCCTest, ModuleSpaceEvacuated)
{
    CString recordName = "global_cc_module";
    JSHandle<SourceTextModule> module = instance->GetFactory()->NewSourceTextModule();
    ModuleManager *moduleManager = thread->GetModuleManager();
    moduleManager->AddResolveImportedModule(recordName, module.GetTaggedValue());
    ModuleSpace *moduleSpace = instance->GetHeap()->GetModuleSpace();
    uintptr_t before = reinterpret_cast<uintptr_t>(module.GetTaggedValue().GetTaggedObject());
    ASSERT_TRUE(Region::ObjectAddressToRange(before)->InModuleSpace());

    TriggerGlobalCC();

    TaggedObject *after = module.GetTaggedValue().GetTaggedObject();
    EXPECT_NE(reinterpret_cast<uintptr_t>(after), before);
    EXPECT_TRUE(Region::ObjectAddressToRange(after)->InModuleSpace());
    EXPECT_EQ(moduleManager->HostGetImportedModule(recordName).GetTaggedValue(), module.GetTaggedValue());
    EXPECT_EQ(module->GetCycleRoot(thread), module.GetTaggedValue());
    EXPECT_GE(moduleSpace->GetHeapObjectSize(), static_cast<size_t>(SourceTextModule::SIZE));
    EXPECT_FALSE(thread->HasPendingException());
}
#endif  // ENABLE_MODULE_SPACE && !USE_CMS_GC
#endif  // PANDA_TARGET_64

HWTEST_F_L0(GlobalCCTest, LocalPostGCState)
{
    if constexpr (G_USE_CMS_GC) {
        return;
    }
    RunFullGC();
    Heap *heap = instance->GetHeap();
    constexpr uint32_t arrayLen = DEFAULT_REGION_SIZE / JSTaggedValue::TaggedTypeSize();
    auto holder = instance->GetFactory()->NewTaggedArray(arrayLen);
    auto native = instance->GetFactory()->NewJSNativePointer(nullptr, nullptr, nullptr, false, 32);
    holder->Set(thread, 0, native.GetTaggedValue());
    ASSERT_TRUE(Region::ObjectAddressToRange(*holder)->InHugeObjectSpace());
    size_t allocLimit = heap->GetGlobalSpaceAllocLimit();
    size_t nativeLimit = heap->GetGlobalSpaceNativeLimit();
    size_t oldCapacity = heap->GetOldSpace()->GetInitialCapacity();

    for (int round = 0; round < 2; round++) {
        TriggerGlobalCC();
        EXPECT_EQ(heap->GetHeapAliveSizeAfterGC(), heap->GetHeapObjectSize());
        EXPECT_EQ(heap->GetHeapAliveSizeExcludesYoungAfterGC(),
                  heap->GetHeapAliveSizeAfterGC() - heap->GetNewSpace()->GetHeapObjectSize());
        EXPECT_EQ(heap->GetHeapBasicLoss(), heap->GetFragmentSizeAfterGC());
        EXPECT_EQ(heap->GetGlobalSpaceAllocLimit(), allocLimit);
        EXPECT_EQ(heap->GetGlobalSpaceNativeLimit(), nativeLimit);
        EXPECT_EQ(heap->GetOldSpace()->GetInitialCapacity(), oldCapacity);
        EXPECT_EQ(heap->GetMarkType(), MarkType::MARK_YOUNG);
        EXPECT_FALSE(heap->InGC());
        EXPECT_FALSE(heap->GetIdleGCTrigger()->CheckIdleOrHintFullGC<Heap>(heap));
        EXPECT_EQ(holder->GetLength(), arrayLen);
    }
}

HWTEST_F_L0(GlobalCCTest, RBStateMachineRoundTrip)
{
    TriggerGlobalCC();
    Runtime::GetInstance()->GCIterateThreadList([](JSThread *thread) {
        EXPECT_EQ(thread->GetSharedCCStatus(), SharedCCStatus::IDLE);
        EXPECT_FALSE(thread->HasCCSuspend());
    });
    EXPECT_FALSE(thread->HasPendingException());
}

HWTEST_F_L0(GlobalCCTest, TriggerGCApi)
{
    JSNApi::TriggerGC(instance, JSNApi::TRIGGER_GC_TYPE::GLOBAL_CC);
    auto *sHeap = SharedHeap::GetInstance();
    instance->GetHeap()->WaitAllTasksFinished();
    sHeap->WaitAllTasksFinished(thread);
    EXPECT_FALSE(sHeap->GetGlobalCC()->IsRunning());
    EXPECT_FALSE(thread->HasPendingException());
}

#if USE_CMS_GC
HWTEST_F_L0(GlobalCCTest, CMSFallsBackToSharedFullGC)
{
    TriggerGlobalCC();
    EXPECT_EQ(SharedHeap::GetInstance()->GetGCType(), TriggerGCType::SHARED_FULL_GC);
    EXPECT_FALSE(thread->IsGlobalConcurrentMarking());
    EXPECT_FALSE(thread->IsGlobalConcurrentCopying());
    EXPECT_FALSE(thread->HasPendingException());
}
#endif

HWTEST_F_L0(GlobalCCTest, IdleTriggerChecksLocalHeap)
{
    if constexpr (G_USE_CMS_GC) {
        return;
    }
    TriggerGlobalCC();
    Heap *heap = instance->GetHeap();
    auto *sHeap = SharedHeap::GetInstance();
    auto *trigger = heap->GetIdleGCTrigger();
    constexpr uint32_t arrayLen = 1_MB / JSTaggedValue::TaggedTypeSize();
    auto holder = instance->GetFactory()->NewTaggedArray(arrayLen);
    ASSERT_TRUE(trigger->CheckIdleOrHintFullGC<Heap>(heap));
    ASSERT_FALSE(trigger->CheckIdleOrHintFullGC<SharedHeap>(sHeap));
    ASSERT_FALSE(sHeap->NeedStopCollection());
    ASSERT_FALSE(heap->NeedStopCollection());
    size_t gcCount = sHeap->GetEcmaGCStats()->GetGCCount();

    heap->SetOnSerializeEvent(true);
    JSNApi::TriggerIdleGC(instance, JSNApi::TRIGGER_IDLE_GC_TYPE::GLOBAL_CC);
    heap->WaitAllTasksFinished();
    sHeap->WaitAllTasksFinished(thread);
    EXPECT_EQ(sHeap->GetEcmaGCStats()->GetGCCount(), gcCount);
    heap->SetOnSerializeEvent(false);

    JSNApi::TriggerIdleGC(instance, JSNApi::TRIGGER_IDLE_GC_TYPE::GLOBAL_CC);
    heap->WaitAllTasksFinished();
    sHeap->WaitAllTasksFinished(thread);
    EXPECT_GT(sHeap->GetEcmaGCStats()->GetGCCount(), gcCount);
    EXPECT_EQ(holder->GetLength(), arrayLen);
    EXPECT_FALSE(thread->HasPendingException());
}

HWTEST_F_L0(GlobalCCTest, IdleTriggerRejectsOccupiedGCGroup)
{
    if constexpr (G_USE_CMS_GC) {
        return;
    }
    TriggerGlobalCC();
    Heap *heap = instance->GetHeap();
    auto *sHeap = SharedHeap::GetInstance();
    constexpr uint32_t arrayLen = 1_MB / JSTaggedValue::TaggedTypeSize();
    auto holder = instance->GetFactory()->NewTaggedArray(arrayLen);
    ASSERT_TRUE(heap->GetIdleGCTrigger()->CheckIdleOrHintFullGC<Heap>(heap));
    ASSERT_FALSE(sHeap->NeedStopCollection());
    ASSERT_FALSE(heap->NeedStopCollection());

    bool posted = sHeap->CompressCollectGarbageNotWaiting<TriggerGCType::SHARED_GC, GCReason::OTHER>(thread, false);
    // RUNNING prevents the shared GC's STW from releasing its task group.
    bool globalCCTriggered = JSNApi::TryTriggerIdleGlobalCC(instance);
    sHeap->WaitAllTasksFinished(thread);
    EXPECT_TRUE(posted);
    EXPECT_FALSE(globalCCTriggered);
    EXPECT_EQ(holder->GetLength(), arrayLen);
    EXPECT_FALSE(thread->HasPendingException());
}

HWTEST_F_L0(GlobalCCTest, EvacuatorSlotsRestored)
{
    TriggerGlobalCC();
    Runtime::GetInstance()->GCIterateThreadList([](JSThread *thread) {
        EXPECT_EQ(thread->GetSharedCCEvacuator(), nullptr);
        EXPECT_TRUE(thread->GetLocalCCEvacuator() != nullptr);
    });
    EXPECT_FALSE(thread->HasPendingException());
}

HWTEST_F_L0(GlobalCCTest, ReadOnlyWeakKeyKeepsValueAlive)
{
    auto key = thread->GlobalConstants()->GetHandledJSNativePointerClass();
    auto *keyRegion = Region::ObjectAddressToRange(key.GetTaggedValue().GetTaggedObject());
    ASSERT_TRUE(keyRegion->InReadOnlySpace());
    auto table = WeakLinkedHashMap::Create(thread);
    {
        EcmaHandleScope temporaryScope(thread);
        auto value = instance->GetFactory()->NewTaggedArray(1);
        value->Set(thread, 0, JSTaggedValue(42));
        table = WeakLinkedHashMap::SetWeakRef(thread, table, key, JSHandle<JSTaggedValue>::Cast(value));
    }
    // The value has no strong handle: it survives only through the ReadOnly key.
    for (int round = 0; round < 2; round++) {
        TriggerGlobalCC();
        int entry = table->FindElement(thread, key.GetTaggedValue());
        ASSERT_NE(entry, -1);
        auto value = table->GetValue(thread, entry);
        ASSERT_TRUE(value.IsTaggedArray());
        EXPECT_EQ(TaggedArray::Cast(value.GetTaggedObject())->Get(thread, 0), JSTaggedValue(42));
    }
}

HWTEST_F_L0(GlobalCCTest, RoundEndStateRestoration)
{
    for (int round = 0; round < 2; round++) {
        TriggerGlobalCC();
        // A second round also validates that the previous round released its
        // internal mark work without reading queues cleared by Finish().
        EXPECT_FALSE(thread->IsGlobalConcurrentMarking());
        EXPECT_TRUE(thread->IsReadyToConcurrentMark());
        EXPECT_TRUE(DaemonThread::GetInstance()->IsReadyToConcurrentMark());
        Runtime::GetInstance()->GCIterateThreadList([](JSThread *iterated) {
            EXPECT_FALSE(iterated->IsGlobalConcurrentMarking());
            EXPECT_TRUE(iterated->IsReadyToConcurrentMark());
        });
    }
    EXPECT_FALSE(thread->HasPendingException());
}

HWTEST_F_L0(GlobalCCTest, WeakCallbacksRunAtSafepoint)
{
    auto *state = NewCallbackState();
    AllocateWeakObject(state);
    TriggerGlobalCC();
    EXPECT_EQ(state->weakCount, 0);
    EXPECT_TRUE(thread->CheckSafepoint());
    EXPECT_EQ(state->freeWeakCount, 1);
    EXPECT_EQ(state->weakCount, 1);
    thread->CheckSafepoint();
    EXPECT_EQ(state->freeWeakCount, 1);
    EXPECT_EQ(state->weakCount, 1);
    EXPECT_FALSE(thread->HasPendingException());
}

HWTEST_F_L0(GlobalCCTest, LocalGCConsumesCallbacksBeforeSafepoint)
{
    auto *state = NewCallbackState();
    AllocateWeakObject(state);
    TriggerGlobalCC();
    EXPECT_EQ(state->weakCount, 0);

    RunFullGC();
    EXPECT_EQ(state->freeWeakCount, 1);
    EXPECT_EQ(state->weakCount, 1);
    thread->CheckSafepoint();
    EXPECT_EQ(state->freeWeakCount, 1);
    EXPECT_EQ(state->weakCount, 1);
    EXPECT_FALSE(thread->HasPendingException());
}

HWTEST_F_L0(GlobalCCTest, VMDestructionConsumesPendingCallbacks)
{
    auto *state = NewCallbackState();
    state->expectedNativeCount = 1;
    AllocateWeakObject(state);
    {
        EcmaHandleScope localScope(thread);
        instance->GetFactory()->NewJSNativePointer(state, GCCallbackTestState::DeleteNative, state);
    }
    TriggerGlobalCC();
    EXPECT_EQ(state->nativeCount, 0);
    EXPECT_EQ(state->weakCount, 0);
    // TearDown checks both callbacks after the actual VM destructor runs.
}

HWTEST_F_L0(GlobalCCTest, IgnoreFinalizeCallbackDiscardsPendingWeakFinalizer)
{
    auto *state = NewCallbackState();
    AllocateWeakObject(state);
    TriggerGlobalCC();
    EXPECT_EQ(state->weakCount, 0);
    JSNApi::IgnoreFinalizeCallback(instance);
    state->expectedFinalizeCount = 0;
    thread->CheckSafepoint();
    EXPECT_EQ(state->freeWeakCount, 1);
    EXPECT_EQ(state->weakCount, 0);
}

HWTEST_F_L0(GlobalCCTest, DestructionLeavesLiveResourcesForNormalCleanup)
{
    auto *first = NewCallbackState();
    first->expectedNativeCount = 1;
    auto *second = NewCallbackState();
    second->expectedNativeCount = 1;
    auto *liveNative = NewCallbackState();
    liveNative->expectedNativeCount = 1;
    auto *liveWeak = NewCallbackState();
    {
        EcmaHandleScope localScope(thread);
        auto pointer = instance->GetFactory()->NewJSNativePointer(
            liveNative, GCCallbackTestState::DeleteNative, liveNative);
        thread->NewGlobalHandle(pointer.GetTaggedValue().GetRawData());
        AllocateWeakObject(liveWeak);
        thread->NewGlobalHandle(*reinterpret_cast<JSTaggedType *>(liveWeak->weakSlot));
        instance->GetFactory()->NewJSNativePointer(first, GCCallbackTestState::DeleteNative, first);
    }
    TriggerGlobalCC();
    first->onDelete = [this, second, liveNative, liveWeak]() {
        ThreadManagedScope managedScope(thread);
        EXPECT_TRUE(thread->ReadyForGCIterating());
        EXPECT_EQ(liveNative->nativeCount, 0);
        EXPECT_EQ(liveWeak->freeWeakCount, 0);
        EXPECT_EQ(liveWeak->weakCount, 0);
        EXPECT_TRUE(JSTaggedValue(*reinterpret_cast<JSTaggedType *>(liveWeak->weakSlot)).IsHeapObject());
        EcmaHandleScope localScope(thread);
        instance->GetFactory()->NewJSNativePointer(second, GCCallbackTestState::DeleteNative, second,
                                                  false, 0, Concurrent::YES);
        EXPECT_EQ(second->nativeCount, 0);
    };
}

HWTEST_F_L0(GlobalCCTest, DestructionDrainsDeadWeakCallbacksAndLocalGCReentry)
{
    auto *first = NewCallbackState();
    AllocateWeakObject(first);
    auto *second = NewCallbackState();
    auto *native = NewCallbackState();
    native->expectedNativeCount = 1;
    TriggerGlobalCC();
    first->onFinalize = [this, second, native]() {
        EXPECT_TRUE(thread->ReadyForGCIterating());
        AllocateWeakObject(second);
        {
            EcmaHandleScope localScope(thread);
            instance->GetFactory()->NewJSNativePointer(native, GCCallbackTestState::DeleteNative, native);
        }
        RunFullGC();
    };
}

HWTEST_F_L0(GlobalCCTest, DestructionWaitsForReentrantGlobalCCCopy)
{
    auto *first = NewCallbackState();
    first->expectedNativeCount = 1;
    auto *second = NewCallbackState();
    second->expectedNativeCount = 1;
    {
        EcmaHandleScope localScope(thread);
        instance->GetFactory()->NewJSNativePointer(first, GCCallbackTestState::DeleteNative, first);
    }
    TriggerGlobalCC();
    first->onDelete = [this, second]() {
        ThreadManagedScope managedScope(thread);
        {
            EcmaHandleScope localScope(thread);
            instance->GetFactory()->NewJSNativePointer(second, GCCallbackTestState::DeleteNative, second);
        }
        // Return after ReMark, allowing destruction to wait for the outstanding copy phase.
        SharedHeap::GetInstance()->CollectGarbage<TriggerGCType::GLOBAL_CC, GCReason::OTHER>(thread);
        EXPECT_EQ(second->nativeCount, 0);
    };
}

HWTEST_F_L0(GlobalCCTest, DestructionConsumesPendingWeakFinalizerOnce)
{
    auto *state = NewCallbackState();
    {
        EcmaHandleScope localScope(thread);
        auto object = instance->GetFactory()->NewEmptyJSObject();
        state->weakSlot = thread->NewGlobalHandle(object.GetTaggedValue().GetRawData());
        state->weakSlot = thread->SetWeak(state->weakSlot, state, nullptr, GCCallbackTestState::FinalizeWeak);
    }
    state->expectedFinalizeCount = 1;
    TriggerGlobalCC();
    EXPECT_EQ(state->weakCount, 0);
}

HWTEST_F_L0(GlobalCCTest, DestructionConsumesNativeCallbacksWithoutLoopPosting)
{
    JSNApi::SetAsyncCleanTaskCallback(instance, [this](panda::AsyncNativeCallbacksPack *callbacks) {
        destructionAsyncPostCount_++;
        callbacks->ProcessAll("GlobalCCTest");
        delete callbacks;
    });
    {
        EcmaHandleScope localScope(thread);
        auto *state = NewCallbackState();
        state->expectedNativeCount = 1;
        instance->GetFactory()->NewJSNativePointer(state, GCCallbackTestState::DeleteNative, state);
    }
    TriggerGlobalCC();
}

HWTEST_F_L0(GlobalCCTest, NativePointerSpaceCopyPreservesIdentityAndSize)
{
    if constexpr (G_USE_CMS_GC) {
        return;
    }
    TriggerGlobalCC();
    Heap *heap = instance->GetHeap();
    size_t spaceSizeBefore = heap->GetNativePointerSpace()->GetHeapObjectSize();
    size_t bindingSizeBefore = heap->GetNativeBindingSize();
    constexpr size_t bindingSize = 16;
    auto *state = NewCallbackState();
    state->expectedNativeCount = 1;
    auto native = instance->GetFactory()->NewJSNativePointer(state,
        GCCallbackTestState::DeleteNative, state, false, bindingSize);
    auto holder = instance->GetFactory()->NewTaggedArray(64);
    for (uint32_t i = 0; i < holder->GetLength(); i++) {
        holder->Set(thread, i, native.GetTaggedValue());
    }

    for (int round = 0; round < 3; round++) {
        uintptr_t oldAddress = reinterpret_cast<uintptr_t>(*native);
        TriggerGlobalCC();
        EXPECT_NE(reinterpret_cast<uintptr_t>(*native), oldAddress);
        EXPECT_TRUE(Region::ObjectAddressToRange(*native)->InNativePointerSpace());
        EXPECT_EQ(native->GetExternalPointer(), state);
        EXPECT_EQ(native->GetData(), state);
        EXPECT_EQ(native->GetBindingSize(), bindingSize);
        EXPECT_EQ(state->nativeCount, 0);
        EXPECT_EQ(heap->GetNativePointerSpace()->GetHeapObjectSize(), spaceSizeBefore + JSNativePointer::SIZE);
        EXPECT_EQ(heap->GetNativeBindingSize(), bindingSizeBefore + bindingSize);
        EXPECT_EQ(heap->GetNativeBindingSizeAfterLastGC(), heap->GetNativeBindingSize());
        for (uint32_t i = 0; i < holder->GetLength(); i++) {
            EXPECT_EQ(holder->Get(thread, i), native.GetTaggedValue());
        }
    }
}

HWTEST_F_L0(GlobalCCTest, NativePointerSpaceDeadCallbackDrainsOnOwner)
{
    TriggerGlobalCC();
    Heap *heap = instance->GetHeap();
    size_t bindingSizeBefore = heap->GetNativeBindingSize();
    auto *state = NewCallbackState();
    state->expectedNativeCount = 1;
    {
        EcmaHandleScope localScope(thread);
        instance->GetFactory()->NewJSNativePointer(state,
            GCCallbackTestState::DeleteNative, state, false, 16);
    }
    TriggerGlobalCC();
    EXPECT_EQ(state->nativeCount, 0);
    EXPECT_EQ(heap->GetNativeBindingSize(), bindingSizeBefore);
    EXPECT_EQ(heap->GetNativeBindingSizeAfterLastGC(), bindingSizeBefore);
    thread->CheckSafepoint();
    EXPECT_EQ(state->nativeCount, 1);
    TriggerGlobalCC();
    RunFullGC();
    EXPECT_EQ(state->nativeCount, 1);
}

HWTEST_F_L0(GlobalCCTest, NativePointerSpaceReclaimsBindingWithoutDeleter)
{
    if constexpr (G_USE_CMS_GC) {
        return;
    }
    TriggerGlobalCC();
    Heap *heap = instance->GetHeap();
    size_t spaceSizeBefore = heap->GetNativePointerSpace()->GetHeapObjectSize();
    auto liveNative = instance->GetFactory()->NewJSNativePointer(nullptr, nullptr, nullptr, false, 16);
    size_t bindingSizeBefore = heap->GetNativeBindingSize();
    {
        EcmaHandleScope localScope(thread);
        instance->GetFactory()->NewJSNativePointer(nullptr, nullptr, nullptr, false, 32);
    }
    EXPECT_EQ(heap->GetNativeBindingSize(), bindingSizeBefore + 32);
    TriggerGlobalCC();
    EXPECT_EQ(heap->GetNativeBindingSize(), bindingSizeBefore);
    EXPECT_EQ(heap->GetNativeBindingSizeAfterLastGC(), bindingSizeBefore);
    EXPECT_EQ(heap->GetNativePointerSpace()->GetHeapObjectSize(), spaceSizeBefore + JSNativePointer::SIZE);
    EXPECT_EQ(liveNative->GetBindingSize(), 16U);
}

HWTEST_F_L0(GlobalCCTest, NativePointerSpaceConcurrentCallbackRunsOnce)
{
    TriggerGlobalCC();
    Heap *heap = instance->GetHeap();
    size_t bindingSizeBefore = heap->GetNativeBindingSize();
    auto callbackState = std::make_unique<ConcurrentGCCallbackTestState>();
    auto *state = callbackState.get();
    concurrentCallbackStates_.emplace_back(std::move(callbackState));
    {
        EcmaHandleScope localScope(thread);
        instance->GetFactory()->NewJSNativePointer(state, ConcurrentGCCallbackTestState::DeleteNative,
                                                  state, false, 16, Concurrent::YES);
    }
    TriggerGlobalCC();
    EXPECT_EQ(heap->GetNativeBindingSize(), bindingSizeBefore);
    thread->CheckSafepoint();
    {
        std::unique_lock<std::mutex> lock(state->mutex);
        ASSERT_TRUE(state->cv.wait_for(lock, std::chrono::seconds(5), [state]() {
            return state->count.load(std::memory_order_relaxed) != 0;
        }));
    }
    EXPECT_EQ(state->count.load(std::memory_order_relaxed), 1U);
    TriggerGlobalCC();
    RunFullGC();
    EXPECT_EQ(state->count.load(std::memory_order_relaxed), 1U);
}

HWTEST_F_L0(GlobalCCTest, SafepointCallbackAllowsGlobalAndLocalGCReentry)
{
    auto *oldState = NewCallbackState();
    oldState->expectedNativeCount = 1;
    auto *reentrantState = NewCallbackState();
    {
        EcmaHandleScope localScope(thread);
        instance->GetFactory()->NewJSNativePointer(oldState, GCCallbackTestState::DeleteNative, oldState);
    }
    TriggerGlobalCC();
    EXPECT_EQ(oldState->nativeCount, 0);
    oldState->onDelete = [this, reentrantState]() {
        ThreadManagedScope managedScope(thread);
        reentrantState->expectedNativeCount = 1;
        {
            EcmaHandleScope localScope(thread);
            instance->GetFactory()->NewJSNativePointer(reentrantState,
                GCCallbackTestState::DeleteNative, reentrantState);
        }
        TriggerGlobalCC();
        EXPECT_EQ(reentrantState->nativeCount, 0);
        // Local GC still consumes its own native-pointer callbacks directly.
        RunFullGC();
        EXPECT_EQ(reentrantState->nativeCount, 1);
    };
    thread->CheckSafepoint();
    EXPECT_EQ(oldState->nativeCount, 1);
    EXPECT_EQ(reentrantState->nativeCount, 1);
    thread->CheckSafepoint();
    EXPECT_EQ(oldState->nativeCount, 1);
    EXPECT_EQ(reentrantState->nativeCount, 1);
}

HWTEST_F_L0(GlobalCCTest, ReentrantGlobalCCPublishesNextSafepoint)
{
    auto *state = NewCallbackState();
    state->expectedNativeCount = 1;
    auto *reentrantState = NewCallbackState();
    {
        EcmaHandleScope localScope(thread);
        instance->GetFactory()->NewJSNativePointer(state, GCCallbackTestState::DeleteNative, state);
    }
    TriggerGlobalCC();
    state->onDelete = [this, reentrantState]() {
        ThreadManagedScope managedScope(thread);
        AllocateWeakObject(reentrantState);
        TriggerGlobalCC();
        EXPECT_EQ(reentrantState->weakCount, 0);
    };
    thread->CheckSafepoint();
    EXPECT_EQ(state->nativeCount, 1);
    EXPECT_EQ(reentrantState->weakCount, 0);
    thread->CheckSafepoint();
    EXPECT_EQ(reentrantState->freeWeakCount, 1);
    EXPECT_EQ(reentrantState->weakCount, 1);
    thread->CheckSafepoint();
    EXPECT_EQ(reentrantState->weakCount, 1);
}

HWTEST_F_L0(GlobalCCTest, ReadOnlySnapshotMappingSurvivesGlobalCC)
{
    auto *nativeClass = thread->GlobalConstants()->GetJSNativePointerClass().GetTaggedObject();
    auto *region = Region::ObjectAddressToRange(nativeClass);
    ASSERT_TRUE(region->InReadOnlySpace());
    auto index = instance->GetSnapshotEnv()->FindEnvObjectIndex(reinterpret_cast<uintptr_t>(nativeClass));
    ASSERT_NE(index, SnapshotEnv::MAX_UINT_32);
    for (int round = 0; round < 2; round++) {
        TriggerGlobalCC();
        EXPECT_EQ(instance->GetSnapshotEnv()->FindEnvObjectIndex(reinterpret_cast<uintptr_t>(nativeClass)), index);
        EXPECT_EQ(thread->GlobalConstants()->GetJSNativePointerClass().GetTaggedObject(), nativeClass);
    }
}

HWTEST_F_L0(GlobalCCTest, ReadOnlySnapshotMappingSurvivesFullAndGlobalGC)
{
    auto *nativeClass = thread->GlobalConstants()->GetJSNativePointerClass().GetTaggedObject();
    auto index = instance->GetSnapshotEnv()->FindEnvObjectIndex(reinterpret_cast<uintptr_t>(nativeClass));
    ASSERT_NE(index, SnapshotEnv::MAX_UINT_32);
    RunFullGC();
    TriggerGlobalCC();
    EXPECT_EQ(instance->GetSnapshotEnv()->FindEnvObjectIndex(reinterpret_cast<uintptr_t>(nativeClass)), index);
}

HWTEST_F_L0(GlobalCCTest, NativeHClassRoundTripSurvivesGlobalAndFullGC)
{
    auto *expectedClass = reinterpret_cast<JSHClass *>(
        thread->GlobalConstants()->GetJSNativePointerClass().GetTaggedObject());
    auto index = instance->GetSnapshotEnv()->FindEnvObjectIndex(reinterpret_cast<uintptr_t>(expectedClass));
    ASSERT_NE(index, SnapshotEnv::MAX_UINT_32);
    TriggerGlobalCC();
    ASSERT_EQ(instance->GetSnapshotEnv()->FindEnvObjectIndex(reinterpret_cast<uintptr_t>(expectedClass)), index);

    auto source = instance->GetFactory()->NewJSArrayBuffer(32);
    JSHandle<JSTaggedValue> undefined(thread, JSTaggedValue::Undefined());
    ValueSerializer serializer(thread);
    ASSERT_TRUE(serializer.WriteValue(thread, JSHandle<JSTaggedValue>::Cast(source), undefined, undefined));
    auto data = serializer.Release();
    JSHandle<JSTaggedValue> result;
    {
        // Drop the deserializer's temporary object vector before FullGC.
        BaseDeserializer deserializer(thread, data.get());
        result = deserializer.ReadValue();
    }
    ASSERT_FALSE(result.IsEmpty());
    ASSERT_TRUE(result->IsArrayBuffer());
    auto copy = JSHandle<JSArrayBuffer>::Cast(result);
    auto *native = copy->GetArrayBufferData(thread).GetTaggedObject();
    ASSERT_EQ(native->GetClass(), expectedClass);
    RunFullGC();
    native = copy->GetArrayBufferData(thread).GetTaggedObject();
    EXPECT_EQ(native->GetClass(), expectedClass);
}

HWTEST_F_L0(GlobalCCTest, SweptNonEvacuatedRegionKeepsL2SRebuildSet)
{
    auto checkRebuild = [this](auto referenceType) {
        constexpr ReferenceType refType = decltype(referenceType)::value;
        Heap *heap = instance->GetHeap();
        NonMovableSpace space(heap, DEFAULT_REGION_SIZE, DEFAULT_REGION_SIZE);
        space.Initialize();
        Region *region = space.GetCurrentRegion();
        uintptr_t rebuiltSlot = region->GetBegin();
        uintptr_t writtenSlot = rebuiltSlot + JSTaggedValue::TaggedTypeSize();
        region->InsertLocalToShareRSet<refType>(rebuiltSlot);
        region->ClearLocalToShareRSetInRange(rebuiltSlot, writtenSlot);
        region->SwapLocalToShareRSetForCC();
        region->SetSwept();

        // The owner writes after sweep, before GlobalCC finishes rebuilding the region.
        region->InsertLocalToShareRSet<refType>(writtenSlot);
        region->InsertSweepingLocalToShareRSetForCC<refType>(rebuiltSlot);
        region->ResetSwept();
        region->MergeLocalToShareRSetForCC();

        EXPECT_TRUE(region->TestLocalToShare<refType>(rebuiltSlot));
        EXPECT_TRUE(region->TestLocalToShare<refType>(writtenSlot));
        space.Destroy();
    };
    checkRebuild(std::integral_constant<ReferenceType, ReferenceType::NORMAL> {});
#ifdef USE_COMPRESSED_POINTER
    checkRebuild(std::integral_constant<ReferenceType, ReferenceType::COMPRESSED> {});
#endif
}

HWTEST_F_L0(GlobalCCTest, ReTrimPreservesL2SReferencesInReusedSweptRanges)
{
    auto checkReTrim = [this](auto referenceType) {
        constexpr ReferenceType refType = decltype(referenceType)::value;
        for (bool isToRegion : {false, true}) {
            Heap *heap = instance->GetHeap();
            NonMovableSpace space(heap, DEFAULT_REGION_SIZE, DEFAULT_REGION_SIZE);
            space.Initialize();
            Region *region = space.GetCurrentRegion();
            if (isToRegion) {
                region->SetRegionTypeFlag(RegionTypeFlag::TO);
            }
            uintptr_t retainedSlot = region->GetBegin();
            uintptr_t trimmedSlot = retainedSlot + JSTaggedValue::TaggedTypeSize();
            uintptr_t reusedSlot = trimmedSlot + JSTaggedValue::TaggedTypeSize();
            uintptr_t trimEnd = reusedSlot + JSTaggedValue::TaggedTypeSize();
            region->InsertLocalToShareRSet<refType>(retainedSlot);
            region->ClearLocalToShareRSetInRange(retainedSlot, trimEnd);
            region->SwapLocalToShareRSetForCC();
            region->InsertSweepingLocalToShareRSetForCC<refType>(retainedSlot);
            heap->ClearSlotsRange(region, trimmedSlot, trimEnd);

            // An update that read the old fields can insert after trim cleared them.
            region->InsertSweepingLocalToShareRSetForCC<refType>(trimmedSlot);
            region->InsertSweepingLocalToShareRSetForCC<refType>(reusedSlot);
            region->SetSwept();
            if (!isToRegion) {
                region->InsertLocalToShareRSet<refType>(reusedSlot);
            }
            region->ResetSwept();
            region->MergeLocalToShareRSetForCC();

            EXPECT_TRUE(region->TestLocalToShare<refType>(retainedSlot));
            EXPECT_FALSE(region->TestLocalToShare<refType>(trimmedSlot));
            EXPECT_EQ(region->TestLocalToShare<refType>(reusedSlot), !isToRegion);
            space.Destroy();
        }
    };
    checkReTrim(std::integral_constant<ReferenceType, ReferenceType::NORMAL> {});
#ifdef USE_COMPRESSED_POINTER
    checkReTrim(std::integral_constant<ReferenceType, ReferenceType::COMPRESSED> {});
#endif
}

HWTEST_F_L0(GlobalCCTest, SweptAndCopiedL2SReferencesSurvive)
{
    ObjectFactory *factory = instance->GetFactory();
    auto nonMovable = factory->NewTaggedArray(8, JSTaggedValue::Undefined(), MemSpaceType::NON_MOVABLE);
    auto movable = factory->NewTaggedArray(8, JSTaggedValue::Undefined(), MemSpaceType::OLD_SPACE);
    constexpr uint32_t hugeLength = DEFAULT_REGION_SIZE / JSTaggedValue::TaggedTypeSize();
    auto huge = factory->NewTaggedArray(hugeLength);
    auto shared = AllocateSharedArray(8);
    ASSERT_TRUE(Region::ObjectAddressToRange(*huge)->InHugeObjectSpace());
    for (auto holder : {nonMovable, movable, huge}) {
        holder->Set(thread, 0, shared.GetTaggedValue());
    }
    for (int round = 0; round < 3; round++) {
        for (auto holder : {nonMovable, movable, huge}) {
            holder->Set(thread, 1, shared.GetTaggedValue());
            holder->Set(thread, 1, JSTaggedValue::Undefined());
        }
        TriggerGlobalCC();
        for (auto holder : {nonMovable, movable, huge}) {
            EXPECT_EQ(holder->Get(thread, 0), shared.GetTaggedValue());
            Region *region = Region::ObjectAddressToRange(*holder);
            EXPECT_TRUE(region->TestLocalToShare<ReferenceType::NORMAL>(
                reinterpret_cast<uintptr_t>(holder->GetData())));
            EXPECT_FALSE(region->TestLocalToShare<ReferenceType::NORMAL>(
                reinterpret_cast<uintptr_t>(holder->GetData()) + JSTaggedValue::TaggedTypeSize()));
        }
    }
    EXPECT_FALSE(thread->HasPendingException());
}

HWTEST_F_L0(GlobalCCTest, DeadHugeRegionReclaimedWithL2S)
{
    uintptr_t deadRegion = 0;
    auto shared = AllocateSharedArray(8);
    {
        EcmaHandleScope temporaryScope(thread);
        constexpr uint32_t length = DEFAULT_REGION_SIZE / JSTaggedValue::TaggedTypeSize();
        auto dead = instance->GetFactory()->NewTaggedArray(length);
        Region *region = Region::ObjectAddressToRange(*dead);
        ASSERT_TRUE(region->InHugeObjectSpace());
        deadRegion = reinterpret_cast<uintptr_t>(region);
        dead->Set(thread, 0, shared.GetTaggedValue());
    }
    TriggerGlobalCC();
    Heap *heap = instance->GetHeap();
    heap->GetHugeObjectSpace()->EnumerateRegions([deadRegion](Region *region) {
        EXPECT_NE(reinterpret_cast<uintptr_t>(region), deadRegion);
    });
    EXPECT_FALSE(thread->HasPendingException());
}

}  // namespace panda::test
