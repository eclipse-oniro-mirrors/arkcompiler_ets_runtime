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

#include "ecmascript/builtins/builtins_ark_tools.h"
#include "ecmascript/builtins/builtins_gc.h"
#include "ecmascript/ecma_vm.h"
#include "ecmascript/global_env.h"
#include "ecmascript/js_handle.h"
#include "ecmascript/linked_hash_table.h"
#include "ecmascript/mem/cms_mem/slot_space_config.h"
#include "ecmascript/mem/clock_scope.h"
#include "ecmascript/mem/concurrent_marker.h"
#include "ecmascript/mem/concurrent_sweeper.h"
#include "ecmascript/mem/idle_gc_trigger.h"
#include "ecmascript/mem/local_cmc/cc_evacuator-inl.h"
#include "ecmascript/mem/specialized_slot_space-inl.h"
#include "ecmascript/mem/module_space.h"
#include "ecmascript/mem/tlab_allocator-inl.h"
#include "ecmascript/module/module_data_extractor.h"
#include "ecmascript/mem/verification.h"
#include "ecmascript/stubs/runtime_stubs.h"

using namespace panda::ecmascript;

namespace panda::test {
class LocalCCTest : public BaseTestWithScope<false> {
public:
    void SetUp() override
    {
        JSRuntimeOptions options;
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

    JSHandle<TaggedArray> AllocateWithoutGC(size_t size)
    {
        ObjectFactory *factory = thread->GetEcmaVM()->GetFactory();
        // Disallow garbage collection
        IgnoreGCScope scope(heap_);
        size_t arrayLength = size / sizeof(JSTaggedType);
        auto ret = factory->NewTaggedArray(arrayLength, JSTaggedValue::Hole());
        return ret;
    }

    Heap *heap_ {nullptr};
};

class TestSpecializedSlotSpace : public SpecializedSlotSpace {
public:
    TestSpecializedSlotSpace(Heap* heap, size_t slotSize, size_t initialCapacity, size_t maximumCapacity)
        : SpecializedSlotSpace(heap, MemSpaceType::SLOT_SPACE, slotSize, initialCapacity, maximumCapacity)
    {
    }

    void SetAllowExpand(bool allowExpand)
    {
        allowExpand_ = allowExpand;
    }

    void PrepareSweeping() override {}
    void Sweep() override {}
    void AsyncSweep([[maybe_unused]] bool isMain,
                    [[maybe_unused]] bool releaseMemory = false) override {}
    bool TryFillSweptRegion() override
    {
        return true;
    }
    bool FinishFillSweptRegion() override
    {
        return true;
    }

protected:
    bool CheckExpandBudget([[maybe_unused]] size_t expandSize) override
    {
        return allowExpand_;
    }

private:
    bool allowExpand_ {true};
};

class ModuleSpaceTestHelper {
public:
    static JSHandle<SourceTextModule> CreateModule(JSThread *thread, const CString &name)
    {
        JSHandle<JSTaggedValue> result = ModuleDataExtractor::ParseNativeModule(thread, name, name,
            ModuleTypes::NATIVE_MODULE);
        return JSHandle<SourceTextModule>(thread, SourceTextModule::Cast(result->GetTaggedObject()));
    }

    static bool InModuleSpace(TaggedObject *obj)
    {
        return Region::ObjectAddressToRange(obj)->InModuleSpace();
    }
};

#ifdef PANDA_TARGET_64
void SimulateWorker()
{
    JSRuntimeOptions options;
    options.SetEnableForceGC(false);
    EcmaVM *ecmaVm = JSNApi::CreateEcmaVM(options);
    EXPECT_TRUE(ecmaVm != nullptr) << "Cannot create Runtime";
    JSThread *thread = ecmaVm->GetJSThread();
    thread->ManagedCodeBegin();
    Heap *heap = const_cast<Heap*>(ecmaVm->GetHeap());
    IdleGCTrigger *idleGCTrigger = heap->GetIdleGCTrigger();
    idleGCTrigger->TryTriggerLocalCC();
    EXPECT_FALSE(heap->IsCCMark());
    thread->ManagedCodeEnd();
    JSNApi::DestroyJSVM(ecmaVm);
}

HWTEST_F_L0(LocalCCTest, IdleTriggerTest)
{
    IdleGCTrigger *idleGCTrigger = heap_->GetIdleGCTrigger();
    idleGCTrigger->TryTriggerLocalCC();
    EXPECT_TRUE(!heap_->IsCCMark());
    constexpr size_t OBJ_SIZE = IDLE_LOCAL_CC_LIMIT;
    AllocateWithoutGC(OBJ_SIZE);
    EXPECT_TRUE(heap_->GetHeapObjectSize() >= IDLE_LOCAL_CC_LIMIT);

    idleGCTrigger->TryTriggerLocalCC();
    EXPECT_TRUE(heap_->IsCCMark());
    idleGCTrigger->TryTriggerLocalCC();
    EXPECT_TRUE(thread->IsConcurrentCopying());
    idleGCTrigger->TryTriggerLocalCC();
    EXPECT_TRUE(!thread->IsConcurrentCopying());
    // Test when triggered cc at leave frame not null.
    JSHandle<JSTaggedValue> undefined = thread->GlobalConstants()->GetHandledUndefined();
    EcmaInterpreter::NewRuntimeCallInfo(thread, undefined, undefined, undefined, 0);
    JSTaggedType *testLeaveFrame = const_cast<JSTaggedType *>(thread->GetCurrentSPFrame());
    thread->SetLastLeaveFrame(testLeaveFrame);
    idleGCTrigger->TryTriggerLocalCC();
    EXPECT_FALSE(heap_->IsCCMark());
    thread->SetLastLeaveFrame(nullptr);
    heap_->SetFullMarkRequestedState(true);
    if (heap_->TryTriggerConcurrentMarking()) {
        // TryTriggerLocalCC when other concurrent mark is triggered.
        idleGCTrigger->TryTriggerLocalCC();
        EXPECT_FALSE(heap_->IsCCMark());
    }
}

HWTEST_F_L0(LocalCCTest, SimulateTriggerCCInWorkerTest)
{
    std::thread t1(SimulateWorker);
    t1.join();
}

HWTEST_F_L0(LocalCCTest, GCTest)
{
    constexpr size_t OBJ_SIZE = 3_KB;
    uint32_t slotId = 0;
    JSHandle<TaggedArray> array1 = AllocateWithoutGC(OBJ_SIZE);
    JSHandle<TaggedArray> value = AllocateWithoutGC(OBJ_SIZE);
    JSTaggedValue weakValue = AllocateWithoutGC(OBJ_SIZE).GetTaggedValue();
    weakValue.CreateWeakRef();
    array1->Set(thread, slotId++, value.GetTaggedValue()); // slotid 0
    array1->Set(thread, slotId++, weakValue); // slotid 1
    uint8_t strList[6] = {1, 23, 45, 97, 127}; // 6 : char str size.
    uint32_t strLen = sizeof(strList) - 1;
    EcmaString *string =
        EcmaStringAccessor::CreateFromUtf8(instance, &strList[0], strLen, true);
    array1->Set(thread, slotId++, JSTaggedValue(string));  // slotid 2
    {
        EcmaHandleScope scope(thread);
        JSTaggedValue deadValue = AllocateWithoutGC(OBJ_SIZE).GetTaggedValue();
        deadValue.CreateWeakRef();
        array1->Set(thread, slotId++, deadValue);  // slotid 3
    }
    bool succ = heap_->TryTriggerCCMarking(MarkReason::OTHER);
    EXPECT_TRUE(succ);
    heap_->CollectGarbageFromCCMark(GCReason::OTHER);
    EXPECT_TRUE(thread->IsConcurrentCopying());
    heap_->SetNearGCInSensitive(true);
    heap_->WaitAndHandleCCFinished();
    EXPECT_NE(array1->Get(thread, 0).GetRawData(), JSTaggedValue::Undefined().GetRawData());
    EXPECT_TRUE(array1->Get(thread, 1).IsWeak());
    EXPECT_TRUE(array1->Get(thread, 2).IsInSharedHeap()); // 2 : third object in array
    EXPECT_TRUE(array1->Get(thread, 3).IsUndefined());  // 3 : forth object in array
}

HWTEST_F_L0(LocalCCTest, ReadBarrierTest)
{
    constexpr size_t OBJ_SIZE = 3_KB;
    uint32_t slotId = 0;
    JSHandle<TaggedArray> array1 = AllocateWithoutGC(OBJ_SIZE);
    JSHandle<TaggedArray> value = AllocateWithoutGC(OBJ_SIZE);
    JSTaggedValue weakValue = AllocateWithoutGC(OBJ_SIZE).GetTaggedValue();
    weakValue.CreateWeakRef();
    array1->Set(thread, slotId++, value.GetTaggedValue()); // slotid 0
    array1->Set(thread, slotId++, weakValue); // slotid 1
    uint8_t strList[6] = {1, 23, 45, 97, 127}; // 6 : char str size.
    uint32_t strLen = sizeof(strList) - 1;
    EcmaString *string =
        EcmaStringAccessor::CreateFromUtf8(instance, &strList[0], strLen, true);
    array1->Set(thread, slotId++, JSTaggedValue(string));  // slotid 2
    bool succ = heap_->TryTriggerCCMarking(MarkReason::OTHER);
    EXPECT_TRUE(succ);
    heap_->CollectGarbageFromCCMark(GCReason::OTHER);
    EXPECT_TRUE(thread->IsConcurrentCopying());
    JSTaggedType *data = array1->GetData();
    constexpr uintptr_t TEST_OFFSET0 = 0;
    constexpr uintptr_t TEST_OFFSET1 = sizeof(JSTaggedType);
    constexpr uintptr_t TEST_OFFSET2 = sizeof(JSTaggedType) * 2; // 2 : the third data offset
    uintptr_t slotAddress = ToUintPtr(data) + TEST_OFFSET0;
    JSTaggedValue slotValue0 = ObjectSlot(slotAddress).GetTaggedValue();
    JSTaggedType slotObj = Barriers::ReadBarrierForObject(thread, slotAddress, slotValue0);
    EXPECT_NE(slotObj, JSTaggedValue::Undefined().GetRawData());
    uintptr_t slotAddress1 = ToUintPtr(data) + TEST_OFFSET1;
    JSTaggedValue slotValue1 = ObjectSlot(slotAddress1).GetTaggedValue();
    JSTaggedType slotObj1 = ReadBarrier(thread, slotAddress1, slotValue1);
    EXPECT_TRUE(JSTaggedValue(slotObj1).IsWeak());
    uintptr_t slotAddress2 = ToUintPtr(data) + TEST_OFFSET2;
    JSTaggedValue slotValue2 = ObjectSlot(slotAddress2).GetTaggedValue();
    JSTaggedType slotObj2 = ReadBarrier(thread, slotAddress2, slotValue2);
    EXPECT_TRUE(JSTaggedValue(slotObj2).IsInSharedHeap());
    ObjectFactory *factory = thread->GetEcmaVM()->GetFactory();
    JSHandle<JSArrayBuffer> arraybuffer = factory->NewJSArrayBuffer(8);
    JSTaggedType hclassSlot = arraybuffer.GetTaggedValue().GetRawData();
    JSTaggedValue slotValue3 = ObjectSlot(hclassSlot).GetTaggedValue();
    JSTaggedType hclass = ReadBarrier(thread, hclassSlot, slotValue3);
    EXPECT_TRUE(JSTaggedValue(hclass & TaggedObject::GC_STATE_MASK).IsJSHClass());
    JSTaggedType nullValue(0);
    uintptr_t nullSlot = ToUintPtr(&nullValue);
    JSTaggedValue nullSlotValue = ObjectSlot(nullSlot).GetTaggedValue();
    JSTaggedType nullObject = ReadBarrierImpl(thread, nullSlot, nullSlotValue);
    EXPECT_EQ(nullObject, static_cast<JSTaggedType>(0));
}

HWTEST_F_L0(LocalCCTest, BCDebugStubSwitchScopeTest)
{
    heap_->WaitCCFinished();
    instance->GetJsDebuggerManager()->SetDebugMode(true);
    thread->CheckSwitchDebuggerBCStub();
    thread->SwitchAllStub(false);
    EXPECT_EQ(thread->GetBCStubStatus(), BCStubStatus::NORMAL_BC_STUB);
    EXPECT_EQ(thread->GetCommonStubStatus(), CommonStubStatus::NORMAL_COMMON_STUB);
    EXPECT_EQ(thread->GetBuiltinsStubStatus(), BuiltinsStubStatus::NORMAL_BUILTINS_STUB);
}

HWTEST_F_L0(LocalCCTest, CheckSafepointTest)
{
    constexpr size_t OBJ_SIZE = 3_KB;
    AllocateWithoutGC(OBJ_SIZE);
    heap_->TryTriggerCCMarking(MarkReason::OTHER);
    heap_->CheckOngoingConcurrentMarking();
    EXPECT_TRUE(thread->IsMarkFinished());
    thread->CheckSafepoint();
    EXPECT_TRUE(thread->IsConcurrentCopying());
    heap_->WaitCCFinished();
    thread->CheckSafepoint();
    EXPECT_FALSE(thread->IsConcurrentCopying());
}

HWTEST_F_L0(LocalCCTest, GCConflictTest)
{
    constexpr size_t OBJ_SIZE = 3_KB;
    AllocateWithoutGC(OBJ_SIZE);
    constexpr int enableGCLog = 0x805c;
    instance->GetJSOptions().SetArkProperties(enableGCLog);
    if (heap_->TryTriggerCCMarking(MarkReason::OTHER)) {
        heap_->CollectGarbage(TriggerGCType::OLD_GC, GCReason::OTHER);
        EXPECT_TRUE(thread->IsConcurrentCopying());
        heap_->CollectGarbage(TriggerGCType::OLD_GC, GCReason::OTHER);
        EXPECT_FALSE(thread->IsConcurrentCopying());
    }
}

HWTEST_F_L0(LocalCCTest, GCConflictTest2)
{
    constexpr size_t OBJ_SIZE = 3_KB;
    AllocateWithoutGC(OBJ_SIZE);
    if (heap_->TryTriggerCCMarking(MarkReason::OTHER)) {
        heap_->CollectGarbage(TriggerGCType::FULL_GC, GCReason::OTHER);
        EXPECT_FALSE(thread->IsConcurrentCopying());
        heap_->CollectGarbage(TriggerGCType::OLD_GC, GCReason::OTHER);
        EXPECT_FALSE(thread->IsConcurrentCopying());
    }
}

HWTEST_F_L0(LocalCCTest, GCConflictTest3)
{
    constexpr size_t OBJ_SIZE = 3_KB;
    AllocateWithoutGC(OBJ_SIZE);
    if (heap_->TryTriggerCCMarking(MarkReason::OTHER)) {
        heap_->CollectGarbage(TriggerGCType::OLD_GC, GCReason::OTHER);
        EXPECT_TRUE(thread->IsConcurrentCopying());
        SharedHeap::GetInstance()->CollectGarbage<TriggerGCType::SHARED_FULL_GC, GCReason::OTHER>(thread);
        EXPECT_FALSE(thread->IsConcurrentCopying());
    }
}

#if !USE_CMS_GC
HWTEST_F_L0(LocalCCTest, GCConflictOOMTest)
{
    constexpr size_t OBJ_SIZE = 3_KB;
    AllocateWithoutGC(OBJ_SIZE);
    if (heap_->TryTriggerCCMarking(MarkReason::OTHER)) {
        heap_->ShouldThrowOOMError(true);
        heap_->CollectGarbage(TriggerGCType::OLD_GC, GCReason::OTHER);
        EXPECT_FALSE(thread->IsConcurrentCopying());
        heap_->CollectGarbage(TriggerGCType::OLD_GC, GCReason::OTHER);
        EXPECT_FALSE(thread->IsConcurrentCopying());
    }
}
#endif

HWTEST_F_L0(LocalCCTest, SkipGCTest)
{
    constexpr size_t OBJ_SIZE = 3_KB;
    AllocateWithoutGC(OBJ_SIZE);
    if (heap_->TryTriggerCCMarking(MarkReason::OTHER)) {
        IgnoreGCScope scope(heap_);
        heap_->CollectGarbage(TriggerGCType::OLD_GC, GCReason::OTHER);
        EXPECT_TRUE(thread->IsConcurrentMarkingOrFinished());
        EXPECT_FALSE(thread->IsConcurrentCopying());
    }
}

HWTEST_F_L0(LocalCCTest, VerifyTest)
{
    constexpr size_t OBJ_SIZE = 3_KB;
    uint32_t slotId = 0;
    ObjectFactory *factory = thread->GetEcmaVM()->GetFactory();
    JSHandle<TaggedArray> array1 = AllocateWithoutGC(OBJ_SIZE);
    JSHandle<TaggedArray> value = AllocateWithoutGC(OBJ_SIZE);
    JSTaggedValue weakValue = AllocateWithoutGC(OBJ_SIZE).GetTaggedValue();
    weakValue.CreateWeakRef();
    array1->Set(thread, slotId++, value.GetTaggedValue());
    array1->Set(thread, slotId++, weakValue);
    JSHandle<GlobalEnv> env = instance->GetGlobalEnv();
    JSHandle<JSTaggedValue> constructor = env->GetBuiltinsWeakMapFunction();
    JSHandle<JSWeakMap>::Cast(factory->NewJSObjectByConstructor(JSHandle<JSFunction>(constructor),
                                                                constructor));
    heap_->EnableHeapVerication(true);
    if (heap_->TryTriggerCCMarking(MarkReason::OTHER)) {
        heap_->CollectGarbage(TriggerGCType::OLD_GC, GCReason::OTHER);
        EXPECT_TRUE(thread->IsConcurrentCopying());
    }
    EXPECT_TRUE(array1->Get(thread, 1).IsWeak());
}

HWTEST_F_L0(LocalCCTest, EvacuatorCopyTest)
{
    constexpr size_t OBJ_SIZE = 3_KB;
    JSHandle<TaggedArray> testArray = AllocateWithoutGC(OBJ_SIZE);
    TaggedObject *testObject = testArray.GetTaggedValue().GetTaggedObject();
    MarkWord markWord(testObject, RELAXED_LOAD);
    CCEvacuator *mainEvacuator = thread->GetLocalCCEvacuator();
    thread->SetReadBarrierState(true);
    thread->SetCCStatus(CCStatus::COPYING);
    TaggedObject *dst1 = mainEvacuator->Copy(testObject, markWord);
    TaggedObject *dst2 = mainEvacuator->Copy(testObject, markWord);
    EXPECT_EQ(dst1, dst2);
    thread->SetReadBarrierState(false);
    thread->SetCCStatus(CCStatus::READY);
}

HWTEST_F_L0(LocalCCTest, UpdateRecordJSWeakMapTest)
{
    constexpr size_t OBJ_SIZE = 3_KB;
    ObjectFactory *factory = thread->GetEcmaVM()->GetFactory();
    JSHandle<JSTaggedValue> value = JSHandle<JSTaggedValue>::Cast(AllocateWithoutGC(OBJ_SIZE));
    JSHandle<GlobalEnv> env = instance->GetGlobalEnv();
    JSHandle<JSTaggedValue> constructor = env->GetBuiltinsWeakMapFunction();
    JSHandle<JSWeakMap> weakMap =
        JSHandle<JSWeakMap>::Cast(factory->NewJSObjectByConstructor(JSHandle<JSFunction>(constructor),
                                                                    constructor));
    JSHandle<WeakLinkedHashMap> hashMap = WeakLinkedHashMap::Create(thread);
    weakMap->SetWeakLinkedMap(thread, hashMap);
    {
        EcmaHandleScope scope(thread);
        JSHandle<JSTaggedValue> key = JSHandle<JSTaggedValue>::Cast(AllocateWithoutGC(OBJ_SIZE));
        JSWeakMap::Set(thread, weakMap, key, value);
    }
    if (heap_->TryTriggerCCMarking(MarkReason::OTHER)) {
        heap_->CollectGarbageFromCCMark(GCReason::OTHER);
        EXPECT_TRUE(thread->IsConcurrentCopying());
    }
}

HWTEST_F_L0(LocalCCTest, UpdateRecordWeakRefTest)
{
    constexpr size_t OBJ_SIZE = 3_KB;
    uint32_t slotId = 0;
    ObjectFactory *factory = thread->GetEcmaVM()->GetFactory();
    JSHandle<TaggedArray> array1 = AllocateWithoutGC(OBJ_SIZE);
    JSHandle<TaggedArray> value = AllocateWithoutGC(OBJ_SIZE);
    JSTaggedValue weakValue = AllocateWithoutGC(OBJ_SIZE).GetTaggedValue();
    weakValue.CreateWeakRef();
    JSTaggedValue weakString = factory->NewFromUtf8("local_cc").GetTaggedValue();
    weakString.CreateWeakRef();
    array1->Set(thread, slotId++, value.GetTaggedValue());
    array1->Set(thread, slotId++, weakValue);
    array1->Set(thread, slotId++, weakString);
    array1->Set(thread, slotId, weakValue);
    if (heap_->TryTriggerCCMarking(MarkReason::OTHER)) {
        heap_->CheckOngoingConcurrentMarking();
        // change weakslot to strong reference.
        array1->Set(thread, slotId, value);
        heap_->CollectGarbageFromCCMark(GCReason::OTHER);
        EXPECT_TRUE(thread->IsConcurrentCopying());
    }
}

HWTEST_F_L0(LocalCCTest, UpdateWeakGlobalTest)
{
    constexpr size_t OBJ_SIZE = 3_KB;
    JSHandle<TaggedArray> value = AllocateWithoutGC(OBJ_SIZE);
    uintptr_t globalDeadValue = 0;
    {
        [[maybe_unused]] EcmaHandleScope scope(thread);
        JSHandle<TaggedArray> deadValue = AllocateWithoutGC(OBJ_SIZE);
        globalDeadValue = thread->NewGlobalHandle(deadValue.GetTaggedType());
        thread->SetWeak(globalDeadValue);
    }
    uintptr_t globalValue1 = thread->NewGlobalHandle(value.GetTaggedType());
    uintptr_t globalValue2 = thread->NewGlobalHandle(value.GetTaggedType());
    thread->SetWeak(globalValue1);
    thread->SetWeak(globalValue2);
    if (heap_->TryTriggerCCMarking(MarkReason::OTHER)) {
        heap_->CollectGarbageFromCCMark(GCReason::OTHER);
        EXPECT_TRUE(thread->IsConcurrentCopying());
        EXPECT_EQ(*reinterpret_cast<JSTaggedType*>(globalValue1), *reinterpret_cast<JSTaggedType*>(globalValue2));
        EXPECT_EQ(*reinterpret_cast<JSTaggedType*>(globalDeadValue), JSTaggedValue::Undefined().GetRawData());
        heap_->WaitCCFinished();
        EXPECT_TRUE(thread->IsConcurrentCopying());
        heap_->WaitAndHandleCCFinished();
        EXPECT_FALSE(thread->IsConcurrentCopying());
    }
}

HWTEST_F_L0(LocalCCTest, StringToGcTypeTest)
{
    ObjectFactory *factory = thread->GetEcmaVM()->GetFactory();
    JSHandle<EcmaString> gcTypeString = factory->NewFromUtf8("local_cc");
    auto cause = builtins::BuiltinsGc::StringToGcType(thread, gcTypeString.GetTaggedValue());
    EXPECT_EQ(cause, TriggerGCType::LOCAL_CC);
}

HWTEST_F_L0(LocalCCTest, ObjectCopyWithRbTest)
{
    constexpr size_t OBJ_SIZE = 3_KB;
    constexpr size_t COPY_COUNT = 10;
    uint32_t slotId = 0;
    JSHandle<TaggedArray> srcArray = AllocateWithoutGC(OBJ_SIZE);
    JSHandle<TaggedArray> value = AllocateWithoutGC(OBJ_SIZE);
    JSTaggedValue weakValue = AllocateWithoutGC(OBJ_SIZE).GetTaggedValue();
    weakValue.CreateWeakRef();
    srcArray->Set(thread, slotId++, value.GetTaggedValue()); // slotid 0
    srcArray->Set(thread, slotId++, weakValue); // slotid 1
    uint8_t strList[6] = {1, 23, 45, 97, 127}; // 6 : char str size.
    uint32_t strLen = sizeof(strList) - 1;
    EcmaString *string =
        EcmaStringAccessor::CreateFromUtf8(instance, &strList[0], strLen, true);
    srcArray->Set(thread, slotId++, JSTaggedValue(string));  // slotid 2

    JSHandle<TaggedArray> dstArray = AllocateWithoutGC(OBJ_SIZE);
    if (heap_->TryTriggerCCMarking(MarkReason::OTHER)) {
        heap_->CollectGarbageFromCCMark(GCReason::OTHER);
        EXPECT_TRUE(thread->IsConcurrentCopying());
        JSTaggedType *dstObject = reinterpret_cast<JSTaggedType *>(dstArray.GetTaggedValue().GetHeapObject());
        RuntimeStubs::ObjectCopy(reinterpret_cast<uintptr_t>(thread), dstObject, dstArray->GetData(),
            srcArray->GetData(), COPY_COUNT);
        EXPECT_NE(dstArray->Get(thread, 0).GetRawData(), JSTaggedValue::Undefined().GetRawData());
        EXPECT_TRUE(dstArray->Get(thread, 1).IsWeak());
        EXPECT_TRUE(dstArray->Get(thread, 2).IsInSharedHeap()); // 2 : third object in array
    }
}

HWTEST_F_L0(LocalCCTest, ObjectCopyWithoutRbTest)
{
    constexpr size_t OBJ_SIZE = 3_KB;
    constexpr size_t COPY_COUNT = 10;
    uint32_t slotId = 0;
    JSHandle<TaggedArray> srcArray = AllocateWithoutGC(OBJ_SIZE);
    JSHandle<TaggedArray> value = AllocateWithoutGC(OBJ_SIZE);
    JSTaggedValue weakValue = AllocateWithoutGC(OBJ_SIZE).GetTaggedValue();
    weakValue.CreateWeakRef();
    srcArray->Set(thread, slotId++, value.GetTaggedValue()); // slotid 0
    srcArray->Set(thread, slotId++, weakValue); // slotid 1
    uint8_t strList[6] = {1, 23, 45, 97, 127}; // 6 : char str size.
    uint32_t strLen = sizeof(strList) - 1;
    EcmaString *string =
        EcmaStringAccessor::CreateFromUtf8(instance, &strList[0], strLen, true);
    srcArray->Set(thread, slotId++, JSTaggedValue(string));  // slotid 2
    JSHandle<TaggedArray> dstArray = AllocateWithoutGC(OBJ_SIZE);

    heap_->CollectGarbage(TriggerGCType::FULL_GC, GCReason::OTHER);
    heap_->Prepare();
        
    JSTaggedType *dstObject = reinterpret_cast<JSTaggedType *>(dstArray.GetTaggedValue().GetHeapObject());
    RuntimeStubs::ObjectCopy(reinterpret_cast<uintptr_t>(thread), dstObject, dstArray->GetData(),
        srcArray->GetData(), COPY_COUNT);
    EXPECT_NE(dstArray->Get(thread, 0).GetRawData(), JSTaggedValue::Undefined().GetRawData());
    EXPECT_TRUE(dstArray->Get(thread, 1).IsWeak());
    EXPECT_TRUE(dstArray->Get(thread, 2).IsInSharedHeap()); // 2 : third object in array
}

HWTEST_F_L0(LocalCCTest, ReverseArrayWithRbTest)
{
    constexpr size_t OBJ_SIZE = 3_KB;
    constexpr size_t REVERSE_LENGTH = 10;
    uint32_t slotId = 0;
    JSHandle<TaggedArray> array = AllocateWithoutGC(OBJ_SIZE);
    JSHandle<TaggedArray> value = AllocateWithoutGC(OBJ_SIZE);
    JSTaggedValue weakValue = AllocateWithoutGC(OBJ_SIZE).GetTaggedValue();
    weakValue.CreateWeakRef();
    array->Set(thread, slotId++, value.GetTaggedValue()); // slotid 0
    array->Set(thread, slotId++, weakValue); // slotid 1
    uint8_t strList[6] = {1, 23, 45, 97, 127}; // 6 : char str size.
    uint32_t strLen = sizeof(strList) - 1;
    EcmaString *string =
        EcmaStringAccessor::CreateFromUtf8(instance, &strList[0], strLen, true);
    array->Set(thread, slotId++, JSTaggedValue(string));  // slotid 2

    if (heap_->TryTriggerCCMarking(MarkReason::OTHER)) {
        heap_->CollectGarbageFromCCMark(GCReason::OTHER);
        EXPECT_TRUE(thread->IsConcurrentCopying());
        RuntimeStubs::ReverseArray(reinterpret_cast<uintptr_t>(thread), array->GetData(), REVERSE_LENGTH);
        // reverse object 1.
        EXPECT_NE(array->Get(thread, REVERSE_LENGTH - 1).GetRawData(), JSTaggedValue::Undefined().GetRawData());
        // reverse object 2.
        EXPECT_TRUE(array->Get(thread, REVERSE_LENGTH - 2).IsWeak());
        // reverse object 3.
        EXPECT_TRUE(array->Get(thread, REVERSE_LENGTH - 3).IsInSharedHeap());
    }
}

HWTEST_F_L0(LocalCCTest, ReverseArrayWithoutRbTest)
{
    constexpr size_t OBJ_SIZE = 3_KB;
    constexpr size_t REVERSE_LENGTH = 10;
    uint32_t slotId = 0;
    JSHandle<TaggedArray> array = AllocateWithoutGC(OBJ_SIZE);
    JSHandle<TaggedArray> value = AllocateWithoutGC(OBJ_SIZE);
    JSTaggedValue weakValue = AllocateWithoutGC(OBJ_SIZE).GetTaggedValue();
    weakValue.CreateWeakRef();
    array->Set(thread, slotId++, value.GetTaggedValue()); // slotid 0
    array->Set(thread, slotId++, weakValue); // slotid 1
    uint8_t strList[6] = {1, 23, 45, 97, 127}; // 6 : char str size.
    uint32_t strLen = sizeof(strList) - 1;
    EcmaString *string =
        EcmaStringAccessor::CreateFromUtf8(instance, &strList[0], strLen, true);
    array->Set(thread, slotId++, JSTaggedValue(string));  // slotid 2

    heap_->CollectGarbage(TriggerGCType::FULL_GC, GCReason::OTHER);
    heap_->Prepare();
    RuntimeStubs::ReverseArray(reinterpret_cast<uintptr_t>(thread), array->GetData(), REVERSE_LENGTH);
    // reverse object 1.
    EXPECT_NE(array->Get(thread, REVERSE_LENGTH - 1).GetRawData(), JSTaggedValue::Undefined().GetRawData());
    // reverse object 2.
    EXPECT_TRUE(array->Get(thread, REVERSE_LENGTH - 2).IsWeak());
    // reverse object 3.
    EXPECT_TRUE(array->Get(thread, REVERSE_LENGTH - 3).IsInSharedHeap());
}

HWTEST_F_L0(LocalCCTest, ArkToolsTriggerTest)
{
    JSHandle<JSTaggedValue> undefined = thread->GlobalConstants()->GetHandledUndefined();
    EcmaRuntimeCallInfo *runtimeInfo =
        EcmaInterpreter::NewRuntimeCallInfo(thread, undefined, undefined, undefined, 0);
    builtins::BuiltinsArkTools::TriggerLocalCCGC(runtimeInfo);
    if (heap_->TryTriggerCCMarking(MarkReason::OTHER)) {
        EXPECT_TRUE(heap_->IsCCMark());
    }
}

HWTEST_F_L0(LocalCCTest, ArkToolsTriggerImplTest)
{
    constexpr size_t OBJ_SIZE = 3_KB;
    uint32_t slotId = 0;
    JSHandle<TaggedArray> array1 = AllocateWithoutGC(OBJ_SIZE);
    JSHandle<TaggedArray> value = AllocateWithoutGC(OBJ_SIZE);
    JSTaggedValue weakValue = AllocateWithoutGC(OBJ_SIZE).GetTaggedValue();
    weakValue.CreateWeakRef();
    array1->Set(thread, slotId++, value.GetTaggedValue());
    array1->Set(thread, slotId++, weakValue);
    heap_->SetFullMarkRequestedState(true);
    if (heap_->TryTriggerConcurrentMarking()) {
        JSHandle<JSTaggedValue> undefined = thread->GlobalConstants()->GetHandledUndefined();
        EcmaRuntimeCallInfo *runtimeInfo =
            EcmaInterpreter::NewRuntimeCallInfo(thread, undefined, undefined, undefined, 0);
        builtins::BuiltinsArkTools::TriggerLocalCCGC(runtimeInfo);
    }
    if (heap_->TryTriggerCCMarking(MarkReason::OTHER)) {
        JSHandle<JSTaggedValue> undefined = thread->GlobalConstants()->GetHandledUndefined();
        EcmaRuntimeCallInfo *runtimeInfo =
            EcmaInterpreter::NewRuntimeCallInfo(thread, undefined, undefined, undefined, 0);
        builtins::BuiltinsArkTools::TriggerLocalCCGC(runtimeInfo);
        EXPECT_TRUE(thread->IsConcurrentCopying());
        builtins::BuiltinsArkTools::TriggerLocalCCGC(runtimeInfo);
        EXPECT_FALSE(thread->IsConcurrentCopying());
    }
}
#endif

HWTEST_F_L0(LocalCCTest, DisableTest)
{
    heap_->DisableLocalCC();
    EXPECT_FALSE(heap_->LocalCCEnabled());
    bool succ = heap_->TryTriggerCCMarking(MarkReason::OTHER);
    EXPECT_FALSE(succ);
}

HWTEST_F_L0(LocalCCTest, SpecializedSlotSpaceAllocationTest)
{
    constexpr size_t SLOT_SIZE = SlotSpaceConfig::SLOT_STEP_SIZE * 2;
    TestSpecializedSlotSpace space(heap_, SLOT_SIZE, DEFAULT_REGION_SIZE, DEFAULT_REGION_SIZE);
    space.SetAllowExpand(false);
    EXPECT_EQ(space.GetSlotSize(), SLOT_SIZE);
    EXPECT_EQ(space.Allocate(), 0U);
    EXPECT_EQ(space.GetRegionCount(), 0U);

    space.SetAllowExpand(true);
    uintptr_t address = space.Allocate();
    EXPECT_NE(address, 0U);
    EXPECT_EQ(space.GetRegionCount(), 1U);
}

HWTEST_F_L0(LocalCCTest, SpecializedSlotSpacePrepareEvacuateWithPendingRegionTest)
{
    constexpr size_t SLOT_SIZE = SlotSpaceConfig::SLOT_STEP_SIZE * 2;
    TestSpecializedSlotSpace space(heap_, SLOT_SIZE, DEFAULT_REGION_SIZE, DEFAULT_REGION_SIZE);
    uintptr_t address = space.Allocate();
    EXPECT_NE(address, 0U);
    EXPECT_EQ(space.GetRegionCount(), 1U);

    space.PrepareEvacuation();
    space.MergeToGCRegions();
    space.ReclaimFromRegions();
    EXPECT_EQ(space.GetRegionCount(), 0U);
}

HWTEST_F_L0(LocalCCTest, SpecializedSlotSpaceGCObjectTest)
{
    constexpr size_t SLOT_SIZE = SlotSpaceConfig::SLOT_STEP_SIZE * 2;
    TestSpecializedSlotSpace space(heap_, SLOT_SIZE, DEFAULT_REGION_SIZE, DEFAULT_REGION_SIZE);
    space.PrepareEvacuation();
    auto [address, bufferSize] = space.AllocateBufferSync();
    EXPECT_NE(address, 0U);
    EXPECT_NE(bufferSize, 0U);
    EXPECT_EQ(bufferSize % SLOT_SIZE, 0U);

    space.MergeToGCRegions();
    space.ReclaimFromRegions();
    EXPECT_EQ(space.GetRegionCount(), 1U);
}

HWTEST_F_L0(LocalCCTest, SpecializedSlotSpaceGCBufferAllocationTest)
{
    constexpr size_t SLOT_SIZE = SlotSpaceConfig::SLOT_STEP_SIZE * 2;
    TestSpecializedSlotSpace space(heap_, SLOT_SIZE, DEFAULT_REGION_SIZE, DEFAULT_REGION_SIZE);
    space.PrepareEvacuation();

    auto [first, firstBufferSize] = space.AllocateBufferSync();
    auto [second, secondBufferSize] = space.AllocateBufferSync();
    EXPECT_NE(first, 0U);
    EXPECT_NE(second, 0U);
    EXPECT_NE(firstBufferSize, 0U);
    EXPECT_NE(secondBufferSize, 0U);
    EXPECT_EQ(firstBufferSize % SLOT_SIZE, 0U);
    EXPECT_EQ(secondBufferSize % SLOT_SIZE, 0U);

    space.MergeToGCRegions();
    space.ReclaimFromRegions();
    EXPECT_EQ(space.GetRegionCount(), 1U);
}

HWTEST_F_L0(LocalCCTest, SpecializedSlotTlabAllocatorTest)
{
    constexpr size_t SLOT_SIZE = SlotSpaceConfig::SLOT_STEP_SIZE * 2;
    constexpr size_t OTHER_SLOT_SIZE = SlotSpaceConfig::SLOT_STEP_SIZE * 4;
    TestSpecializedSlotSpace space(heap_, SLOT_SIZE, DEFAULT_REGION_SIZE, DEFAULT_REGION_SIZE);
    TestSpecializedSlotSpace otherSpace(heap_, OTHER_SLOT_SIZE, DEFAULT_REGION_SIZE, DEFAULT_REGION_SIZE);
    space.PrepareEvacuation();
    otherSpace.PrepareEvacuation();
    SpecializedSlotTlabAllocator allocator(&space);

    uintptr_t firstAddress = allocator.Allocate();
    allocator.Reset(&otherSpace);
    uintptr_t secondAddress = allocator.Allocate();
    uintptr_t thirdAddress = allocator.Allocate();
    EXPECT_NE(firstAddress, 0U);
    EXPECT_NE(secondAddress, 0U);
    EXPECT_NE(thirdAddress, 0U);
    EXPECT_NE(firstAddress, secondAddress);
    EXPECT_EQ(thirdAddress - secondAddress, OTHER_SLOT_SIZE);

    allocator.Finalize();
    space.MergeToGCRegions();
    space.ReclaimFromRegions();
    otherSpace.MergeToGCRegions();
    otherSpace.ReclaimFromRegions();
    EXPECT_EQ(space.GetRegionCount(), 1U);
    EXPECT_EQ(otherSpace.GetRegionCount(), 1U);
}

// The module TLAB is bound to module space once per work node, filled during evacuation and
// finalized when that work node finishes; production never re-binds it to another space.
HWTEST_F_L0(LocalCCTest, SpecializedSlotTlabAllocatorAccountingTest)
{
    constexpr size_t SLOT_SIZE = SlotSpaceConfig::SLOT_STEP_SIZE * 2;
    constexpr size_t WORK_NODE_COUNT = 3;
    constexpr size_t SLOT_PER_NODE = 4;
    TestSpecializedSlotSpace space(heap_, SLOT_SIZE, DEFAULT_REGION_SIZE, DEFAULT_REGION_SIZE);
    space.PrepareEvacuation();
    EXPECT_EQ(space.GetHeapObjectSize(), 0U);

    for (size_t node = 0; node < WORK_NODE_COUNT; node++) {
        // One TLAB per work node, like WorkNodeHolder owns one per GC thread.
        SpecializedSlotTlabAllocator allocator(&space);
        uintptr_t previous = allocator.Allocate();
        ASSERT_NE(previous, 0U);
        for (size_t i = 1; i < SLOT_PER_NODE; i++) {
            uintptr_t current = allocator.Allocate();
            EXPECT_EQ(current - previous, SLOT_SIZE);
            previous = current;
        }
        // Slots handed out by a work node are published only when that node finalizes.
        EXPECT_EQ(space.GetHeapObjectSize(), node * SLOT_PER_NODE * SLOT_SIZE);
        allocator.Finalize();
        EXPECT_EQ(space.GetHeapObjectSize(), (node + 1) * SLOT_PER_NODE * SLOT_SIZE);
    }

    space.MergeToGCRegions();
    space.ReclaimFromRegions();
    EXPECT_GE(space.GetRegionCount(), 1U);
}
// ModuleSpace tests

#ifdef PANDA_TARGET_64
#if ENABLE_MODULE_SPACE
// A1: allocation goes to moduleSpace; slot size matches; module not in young/old.
HWTEST_F_L0(LocalCCTest, ModuleSpaceAllocTest)
{
    constexpr int MODULE_COUNT = 10;
    std::vector<JSHandle<SourceTextModule>> modules;
    modules.reserve(MODULE_COUNT);
    for (int i = 0; i < MODULE_COUNT; i++) {
        CString name = CString(std::to_string(i).c_str()).insert(0, "test_module_");
        modules.emplace_back(ModuleSpaceTestHelper::CreateModule(thread, name));
        ASSERT_TRUE(!modules[i].IsEmpty());
    }
    ModuleSpace *moduleSpace = heap_->GetModuleSpace();
    EXPECT_EQ(moduleSpace->GetRegionCount(), 1U); // all fit in one region
    EXPECT_EQ(moduleSpace->GetSlotSize(), static_cast<size_t>(SourceTextModule::SIZE));
    for (int i = 0; i < MODULE_COUNT; i++) {
        EXPECT_TRUE(ModuleSpaceTestHelper::InModuleSpace(modules[i].GetTaggedValue().GetTaggedObject()));
    }
    EXPECT_GT(moduleSpace->GetCommittedSize(), 0U);
}

// A4: module root address stays put across a LocalCC cycle (never FROM).
HWTEST_F_L0(LocalCCTest, ModuleAddressStableAcrossLocalCC)
{
    constexpr int MODULE_COUNT = 5;
    std::vector<JSTaggedType> addresses;
    addresses.reserve(MODULE_COUNT);
    {
        std::vector<JSHandle<SourceTextModule>> handles;
        for (int i = 0; i < MODULE_COUNT; i++) {
            CString name = CString(std::to_string(i).c_str()).insert(0, "stable_module_");
            handles.emplace_back(ModuleSpaceTestHelper::CreateModule(thread, name));
            addresses.push_back(handles[i].GetTaggedValue().GetRawData());
        }
    }
    // Full LocalCC cycle: mark → copy → finish.
    bool succ = heap_->TryTriggerCCMarking(MarkReason::OTHER);
    ASSERT_TRUE(succ);
    heap_->CollectGarbageFromCCMark(GCReason::OTHER);
    ASSERT_TRUE(thread->IsConcurrentCopying());
    heap_->SetNearGCInSensitive(true);
    heap_->WaitAndHandleCCFinished();
    EXPECT_FALSE(thread->IsConcurrentCopying());

    // Re-fetch from module space: all addresses must be identical.
    ModuleSpace *moduleSpace = heap_->GetModuleSpace();
    std::set<JSTaggedType> currentAddresses;
    moduleSpace->IterateOverObjects([&](TaggedObject *obj) {
        currentAddresses.insert(reinterpret_cast<uintptr_t>(obj));
    });
    for (JSTaggedType addr : addresses) {
        EXPECT_NE(currentAddresses.find(addr), currentAddresses.end())
            << "module address changed after LocalCC";
    }
    EXPECT_EQ(currentAddresses.size(), static_cast<size_t>(MODULE_COUNT));
}

// A3: dead module survives LocalCC via the R2 region scan (floating garbage).
HWTEST_F_L0(LocalCCTest, DeadModuleReclaimedByLocalCC)
{
    {
        EcmaHandleScope deadScope(thread);
        CString name = "dead_module_lc";
        JSHandle<SourceTextModule> dead = ModuleSpaceTestHelper::CreateModule(thread, name);
        (void)dead;
    }
    ModuleSpace *moduleSpace = heap_->GetModuleSpace();
    size_t beforeCount = 0;
    moduleSpace->IterateOverObjects([&](TaggedObject *obj) {
        (void)obj;
        beforeCount++;
    });
    ASSERT_EQ(beforeCount, 1U);

    // Full LocalCC cycle: moduleSpace is swept in place like nonMovable, so a
    // dead module never floats past the cycle that frees its young targets.
    bool succ = heap_->TryTriggerCCMarking(MarkReason::OTHER);
    ASSERT_TRUE(succ);
    heap_->CollectGarbageFromCCMark(GCReason::OTHER);
    ASSERT_TRUE(thread->IsConcurrentCopying());
    heap_->SetNearGCInSensitive(true);
    heap_->WaitAndHandleCCFinished();
    heap_->Prepare();

    size_t afterCount = 0;
    moduleSpace->IterateOverObjects([&](TaggedObject *obj) {
        (void)obj;
        afterCount++;
    });
    EXPECT_EQ(afterCount, 0U) << "dead module must be reclaimed by the LocalCC itself";
}

// A5: full GC compaction — surviving modules get valid addresses, dead freed.
HWTEST_F_L0(LocalCCTest, ModuleSpaceFullGCCompaction)
{
    constexpr int TOTAL = 20;
    constexpr int LIVE = 5;
    // Dead modules: created in a nested scope, unreachable after it exits.
    {
        EcmaHandleScope deadScope(thread);
        for (int i = LIVE; i < TOTAL; i++) {
            CString name = CString(std::to_string(i).c_str()).insert(0, "compact_module_");
            JSHandle<SourceTextModule> dead = ModuleSpaceTestHelper::CreateModule(thread, name);
            (void)dead;
        }
    }
    // Live modules: handles outlive the GC in the test's own scope.
    std::vector<JSTaggedType> oldAddresses;
    std::vector<JSHandle<SourceTextModule>> keepAlive;
    for (int i = 0; i < LIVE; i++) {
        CString name = CString(std::to_string(i).c_str()).insert(0, "compact_module_");
        keepAlive.emplace_back(ModuleSpaceTestHelper::CreateModule(thread, name));
        oldAddresses.push_back(keepAlive[i].GetTaggedValue().GetRawData());
    }

    ModuleSpace *moduleSpace = heap_->GetModuleSpace();
    size_t beforeRegionCount = moduleSpace->GetRegionCount();
    EXPECT_GE(beforeRegionCount, 1U);

    heap_->CollectGarbage(TriggerGCType::FULL_GC, GCReason::OTHER);

    // After compaction: exactly the live modules remain, all in moduleSpace.
    size_t afterCount = 0;
    std::set<JSTaggedType> current;
    moduleSpace->IterateOverObjects([&](TaggedObject *obj) {
        current.insert(reinterpret_cast<uintptr_t>(obj));
        afterCount++;
    });
    EXPECT_EQ(afterCount, static_cast<size_t>(LIVE));
    EXPECT_EQ(current.size(), static_cast<size_t>(LIVE));
    for (int i = 0; i < LIVE; i++) {
        JSTaggedType moved = keepAlive[i].GetTaggedValue().GetRawData();
        EXPECT_NE(current.find(moved), current.end()) << "live module lost by compaction";
        EXPECT_TRUE(ModuleSpaceTestHelper::InModuleSpace(
            keepAlive[i].GetTaggedValue().GetTaggedObject()));
        // Source regions are reclaimed, so survivors must have moved.
        EXPECT_NE(moved, oldAddresses[i]);
    }
    // All survivors compacted into a single region.
    EXPECT_LE(moduleSpace->GetRegionCount(), 1U);
    // Accounting regression: the used size used to read 0 until the next sweep.
    EXPECT_EQ(moduleSpace->GetHeapObjectSize(), LIVE * SourceTextModule::SIZE);
}

// A6: K1 — module allocation can trigger GC but never independent OOM.
HWTEST_F_L0(LocalCCTest, ModuleSpaceNoIndependentOOM)
{
    ModuleSpace *moduleSpace = heap_->GetModuleSpace();
    constexpr int BATCH = 100;
    {
        std::vector<JSHandle<SourceTextModule>> modules;
        modules.reserve(BATCH);
        for (int i = 0; i < BATCH; i++) {
            CString name = CString(std::to_string(i).c_str()).insert(0, "oom_test_module_");
            modules.emplace_back(ModuleSpaceTestHelper::CreateModule(thread, name));
            ASSERT_TRUE(!modules[i].IsEmpty());
        }
    }
    EXPECT_GT(moduleSpace->GetCommittedSize(), 0U);
    // Committed size is bounded by the shared budget (oldSpace capacity), not a module cap.
    EXPECT_LT(moduleSpace->GetCommittedSize(), heap_->GetOldSpace()->GetMaximumCapacity());
}

// Regression: LocalCC then FullGC — a stale/null recordRegion_ dangles or
// never clears mark bits.
HWTEST_F_L0(LocalCCTest, ModuleMixedGCTest)
{
    constexpr int LIVE = 3;
    std::vector<JSHandle<SourceTextModule>> keepAlive;
    keepAlive.reserve(LIVE);
    for (int i = 0; i < LIVE; i++) {
        CString name = CString(std::to_string(i).c_str()).insert(0, "mixed_live_");
        keepAlive.emplace_back(ModuleSpaceTestHelper::CreateModule(thread, name));
    }
    {
        EcmaHandleScope deadScope(thread);
        CString name = "mixed_dead_";
        JSHandle<SourceTextModule> dead = ModuleSpaceTestHelper::CreateModule(thread, name);
        (void)dead;
    }
    bool succ = heap_->TryTriggerCCMarking(MarkReason::OTHER);
    ASSERT_TRUE(succ);
    heap_->CollectGarbageFromCCMark(GCReason::OTHER);
    ASSERT_TRUE(thread->IsConcurrentCopying());
    heap_->SetNearGCInSensitive(true);
    heap_->WaitAndHandleCCFinished();
    EXPECT_FALSE(thread->IsConcurrentCopying());
    heap_->CollectGarbage(TriggerGCType::FULL_GC, GCReason::OTHER);

    ModuleSpace *moduleSpace = heap_->GetModuleSpace();
    std::set<uintptr_t> current;
    moduleSpace->IterateOverObjects([&](TaggedObject *obj) {
        current.insert(reinterpret_cast<uintptr_t>(obj));
    });
    EXPECT_EQ(current.size(), static_cast<size_t>(LIVE));
    for (int i = 0; i < LIVE; i++) {
        JSTaggedType moved = keepAlive[i].GetTaggedValue().GetRawData();
        EXPECT_NE(current.find(moved), current.end()) << "live module lost across CC then FullGC";
        EXPECT_TRUE(ModuleSpaceTestHelper::InModuleSpace(keepAlive[i].GetTaggedValue().GetTaggedObject()));
    }
}

// Old GCs sweep moduleSpace in place, concurrently, exactly like oldSpace:
// a module that dies before the OLD_GC's full mark is reclaimed by that same
// GC (Prepare() lets the concurrent sweeper finish before the walk), live
// modules keep their addresses across consecutive OLD_GCs (moduleSpace is
// never evacuated there), and a full GC afterwards still compacts the
// survivors. The sweep setup itself surviving the promotion join is covered
// by ModuleOldGcWithPromotionsDoesNotOrphanSweepTest.
HWTEST_F_L0(LocalCCTest, ModuleConsecutiveOldGCTest)
{
    constexpr int LIVE = 2;
    std::vector<JSHandle<SourceTextModule>> keepAlive;
    keepAlive.reserve(LIVE);
    for (int i = 0; i < LIVE; i++) {
        CString name = CString(std::to_string(i).c_str()).insert(0, "oldchain_live_");
        keepAlive.emplace_back(ModuleSpaceTestHelper::CreateModule(thread, name));
    }
    std::set<uintptr_t> liveAddrsBefore;
    for (int i = 0; i < LIVE; i++) {
        liveAddrsBefore.insert(keepAlive[i].GetTaggedValue().GetRawData());
    }
    uintptr_t shortLivedAddr = 0;
    {
        EcmaHandleScope liveScope(thread);
        CString name = "oldchain_shortlived_";
        JSHandle<SourceTextModule> shortLived = ModuleSpaceTestHelper::CreateModule(thread, name);
        heap_->CollectGarbage(TriggerGCType::OLD_GC, GCReason::OTHER);
        shortLivedAddr = shortLived.GetTaggedValue().GetRawData();
        ASSERT_TRUE(ModuleSpaceTestHelper::InModuleSpace(reinterpret_cast<TaggedObject *>(shortLivedAddr)));
    }
    heap_->CollectGarbage(TriggerGCType::OLD_GC, GCReason::OTHER);
    // Let the concurrent sweeper settle before walking moduleSpace.
    heap_->Prepare();

    ModuleSpace *moduleSpace = heap_->GetModuleSpace();
    std::set<uintptr_t> current;
    auto snapshotModules = [&]() {
        current.clear();
        moduleSpace->IterateOverObjects([&](TaggedObject *obj) {
            current.insert(reinterpret_cast<uintptr_t>(obj));
        });
    };
    snapshotModules();
    EXPECT_EQ(current.find(shortLivedAddr), current.end())
        << "dead module must be reclaimed by the first OLD_GC after its death";
    EXPECT_EQ(current.size(), static_cast<size_t>(LIVE));
    for (int i = 0; i < LIVE; i++) {
        EXPECT_NE(current.find(keepAlive[i].GetTaggedValue().GetRawData()), current.end())
            << "live module lost across consecutive old GCs";
        // In-place sweep: live module addresses are stable across OLD_GCs.
        EXPECT_NE(liveAddrsBefore.find(keepAlive[i].GetTaggedValue().GetRawData()), liveAddrsBefore.end())
            << "live module moved during an old GC";
        EXPECT_TRUE(ModuleSpaceTestHelper::InModuleSpace(keepAlive[i].GetTaggedValue().GetTaggedObject()));
    }
    heap_->CollectGarbage(TriggerGCType::FULL_GC, GCReason::OTHER);
    snapshotModules();
    EXPECT_EQ(current.size(), static_cast<size_t>(LIVE));
    for (int i = 0; i < LIVE; i++) {
        EXPECT_NE(current.find(keepAlive[i].GetTaggedValue().GetRawData()), current.end());
        EXPECT_TRUE(ModuleSpaceTestHelper::InModuleSpace(keepAlive[i].GetTaggedValue().GetTaggedObject()));
    }
}

// Newborns created after the R2 region scan survive via handle roots + ReMark.
HWTEST_F_L0(LocalCCTest, ModuleNewbornDuringCCMarkingTest)
{
    constexpr int PRE = 150;
    constexpr int NEWBORN = 200;
    std::vector<JSTaggedType> expected;
    expected.reserve(PRE + NEWBORN);
    for (int i = 0; i < PRE; i++) {
        CString name = CString(std::to_string(i).c_str()).insert(0, "newborn_pre_");
        JSHandle<SourceTextModule> module = ModuleSpaceTestHelper::CreateModule(thread, name);
        expected.push_back(module.GetTaggedValue().GetRawData());
    }
    bool succ = heap_->TryTriggerCCMarking(MarkReason::OTHER);
    ASSERT_TRUE(succ);
    for (int i = 0; i < NEWBORN; i++) {
        CString name = CString(std::to_string(i).c_str()).insert(0, "newborn_during_");
        JSHandle<SourceTextModule> module = ModuleSpaceTestHelper::CreateModule(thread, name);
        expected.push_back(module.GetTaggedValue().GetRawData());
    }
    heap_->CollectGarbageFromCCMark(GCReason::OTHER);
    ASSERT_TRUE(thread->IsConcurrentCopying());
    heap_->SetNearGCInSensitive(true);
    heap_->WaitAndHandleCCFinished();
    EXPECT_FALSE(thread->IsConcurrentCopying());

    ModuleSpace *moduleSpace = heap_->GetModuleSpace();
    std::set<JSTaggedType> current;
    moduleSpace->IterateOverObjects([&](TaggedObject *obj) {
        current.insert(reinterpret_cast<uintptr_t>(obj));
    });
    EXPECT_EQ(current.size(), static_cast<size_t>(PRE + NEWBORN));
    for (JSTaggedType addr : expected) {
        // Modules are never FROM: addresses are stable across the whole cycle.
        EXPECT_NE(current.find(addr), current.end()) << "module lost across CC marking window";
        // The published body is walkable: the class reads back as a module.
        TaggedObject *obj = reinterpret_cast<TaggedObject *>(addr);
        EXPECT_TRUE(obj->GetClass()->IsSourceTextModule());
    }
}

// Modules born during a concurrent old-gen mark survive via handle roots + ReMark.
HWTEST_F_L0(LocalCCTest, ModuleNewbornDuringOldMarkingTest)
{
    constexpr int COUNT = 20;
    heap_->SetMarkType(MarkType::MARK_FULL);
    heap_->TriggerConcurrentMarking(MarkReason::OTHER);
    std::vector<JSHandle<SourceTextModule>> keepAlive;
    keepAlive.reserve(COUNT);
    for (int i = 0; i < COUNT; i++) {
        CString name = CString(std::to_string(i).c_str()).insert(0, "oldmark_newborn_");
        keepAlive.emplace_back(ModuleSpaceTestHelper::CreateModule(thread, name));
    }
    // Finishes the concurrent mark (ReMark) and sweeps moduleSpace in place:
    // handle-rooted modules are marked by ReMark (the publish write barrier
    // plus the remark root scan — the same mechanism that keeps newborn
    // old-space objects alive) and survive the sweep.
    heap_->CollectGarbage(TriggerGCType::OLD_GC, GCReason::OTHER);
    // Let the concurrent sweeper settle before walking moduleSpace.
    heap_->Prepare();

    ModuleSpace *moduleSpace = heap_->GetModuleSpace();
    std::set<JSTaggedType> current;
    moduleSpace->IterateOverObjects([&](TaggedObject *obj) {
        current.insert(reinterpret_cast<uintptr_t>(obj));
    });
    EXPECT_EQ(current.size(), static_cast<size_t>(COUNT));
    for (int i = 0; i < COUNT; i++) {
        EXPECT_NE(current.find(keepAlive[i].GetTaggedValue().GetRawData()), current.end())
            << "module born inside the old-mark window lost";
        EXPECT_TRUE(ModuleSpaceTestHelper::InModuleSpace(keepAlive[i].GetTaggedValue().GetTaggedObject()));
    }
}

// An OLD_GC during a young concurrent mark abandons it (CollectGarbage resets
// the marker) and re-marks from roots; handle-rooted modules born in that
// window must survive.
HWTEST_F_L0(LocalCCTest, ModuleYoungMarkThenOldGCTest)
{
    constexpr int COUNT = 20;
    heap_->TriggerConcurrentMarking(MarkReason::OTHER);  // markType_ defaults to MARK_YOUNG
    std::vector<JSHandle<SourceTextModule>> keepAlive;
    keepAlive.reserve(COUNT);
    for (int i = 0; i < COUNT; i++) {
        CString name = CString(std::to_string(i).c_str()).insert(0, "ym_then_old_");
        keepAlive.emplace_back(ModuleSpaceTestHelper::CreateModule(thread, name));
    }
    heap_->CollectGarbage(TriggerGCType::OLD_GC, GCReason::OTHER);
    // Let the concurrent sweeper settle before walking moduleSpace.
    heap_->Prepare();

    ModuleSpace *moduleSpace = heap_->GetModuleSpace();
    std::set<JSTaggedType> current;
    moduleSpace->IterateOverObjects([&](TaggedObject *obj) {
        current.insert(reinterpret_cast<uintptr_t>(obj));
    });
    EXPECT_EQ(current.size(), static_cast<size_t>(COUNT));
    for (int i = 0; i < COUNT; i++) {
        EXPECT_NE(current.find(keepAlive[i].GetTaggedValue().GetRawData()), current.end())
            << "module born inside the young-mark window destroyed by OLD_GC";
        EXPECT_TRUE(ModuleSpaceTestHelper::InModuleSpace(keepAlive[i].GetTaggedValue().GetTaggedObject()));
    }
}

// Born-and-abandoned module in a CC window: the concurrent marker captures it
// via the creation handle before the scope pops, so the update-phase
// moduleSpace walk (marked bits only) rewrites its young-referencing field in
// place; the next cycle's R2 scan then walks valid pointers.
HWTEST_F_L0(LocalCCTest, ModuleBornAbandonedInCCWindowTest)
{
    JSTaggedType youngAddr = 0;
    bool succ = heap_->TryTriggerCCMarking(MarkReason::OTHER);
    ASSERT_TRUE(succ);
    {
        EcmaHandleScope deadScope(thread);
        JSHandle<SourceTextModule> dead = ModuleSpaceTestHelper::CreateModule(thread, "cc_abandoned_");
        ObjectFactory *factory = thread->GetEcmaVM()->GetFactory();
        JSHandle<TaggedArray> youngArr = factory->NewTaggedArray(4);
        youngAddr = youngArr.GetTaggedValue().GetRawData();
        dead->SetRequestedModules(thread, youngArr.GetTaggedValue());
    }
    heap_->CollectGarbageFromCCMark(GCReason::OTHER);
    ASSERT_TRUE(thread->IsConcurrentCopying());
    heap_->SetNearGCInSensitive(true);
    heap_->WaitAndHandleCCFinished();
    EXPECT_FALSE(thread->IsConcurrentCopying());

    ModuleSpace *ms = heap_->GetModuleSpace();
    JSTaggedType fieldAfter = 0;
    ms->IterateOverObjects([&](TaggedObject *obj) {
        SourceTextModule *m = SourceTextModule::Cast(obj);
        if (m->GetEcmaModuleRecordNameString() == "cc_abandoned_") {
            fieldAfter = m->GetRequestedModules(thread).GetRawData();
        }
    });
    EXPECT_NE(fieldAfter, youngAddr) << "abandoned module field not updated — dangling into FROM";

    // Cycle 2 churns young and re-scans every module: must walk valid fields.
    ObjectFactory *factory = thread->GetEcmaVM()->GetFactory();
    for (int i = 0; i < 300; i++) {
        (void)factory->NewTaggedArray(8);
    }
    succ = heap_->TryTriggerCCMarking(MarkReason::OTHER);
    ASSERT_TRUE(succ);
    heap_->CollectGarbageFromCCMark(GCReason::OTHER);
    ASSERT_TRUE(thread->IsConcurrentCopying());
    heap_->SetNearGCInSensitive(true);
    heap_->WaitAndHandleCCFinished();
}

// Regression (orphaned sweep setup): with promotions, an old GC's Evacuate ran
// SweepNewToOldRegions between PartialGC::Sweep() and PostTask(OLD_GC),
// rewriting the sweeper range to exclude MODULE_SPACE — the module sweeper
// task was never posted, the parked sweepingRegionList_ was orphaned, and the
// clear task zeroed its regions' mark bits. The next module allocation then
// swept those regions inline with all-zero bits and freed live modules,
// handing their slots to new allocations (module cache corruption, GC crashes
// on illegal RSet references). The MemSpaceType reorder (MODULE_SPACE now
// lies inside [OLD_SPACE..MACHINE_CODE_SPACE]) makes the promotion-path
// range rewrite idempotent, so the module sweeper task is always posted; the
// promotion join must not corrupt the sweep, and fresh allocations must never
// alias live modules.
HWTEST_F_L0(LocalCCTest, ModuleOldGcWithPromotionsDoesNotOrphanSweepTest)
{
    constexpr int LIVE = 8;
    constexpr int NEWBORN = 32;
    std::vector<JSHandle<SourceTextModule>> keepAlive;
    keepAlive.reserve(LIVE);
    for (int i = 0; i < LIVE; i++) {
        CString name = CString(std::to_string(i).c_str()).insert(0, "orphan_live_");
        keepAlive.emplace_back(ModuleSpaceTestHelper::CreateModule(thread, name));
    }
    std::set<uintptr_t> liveAddresses;
    for (int i = 0; i < LIVE; i++) {
        liveAddresses.insert(keepAlive[i].GetTaggedValue().GetRawData());
    }

    // Surviving young arrays force new-to-old promotion (hasNewToOldRegions_)
    // in the old GC that finishes the concurrent full mark.
    constexpr size_t OBJ_SIZE = 3_KB;
    std::vector<JSHandle<TaggedArray>> youngSurvivors;
    for (int i = 0; i < 16; i++) {
        youngSurvivors.emplace_back(AllocateWithoutGC(OBJ_SIZE));
    }
    heap_->SetMarkType(MarkType::MARK_FULL);
    heap_->TriggerConcurrentMarking(MarkReason::OTHER);
    heap_->CollectGarbage(TriggerGCType::OLD_GC, GCReason::OTHER);
    heap_->CollectGarbage(TriggerGCType::OLD_GC, GCReason::OTHER);
    // Drain the async clear task: its mark-bit clearing must happen before the
    // allocations below (the buggy path swept lazily on first allocation).
    heap_->Prepare();

    for (int i = 0; i < LIVE; i++) {
        TaggedObject *obj = keepAlive[i].GetTaggedValue().GetTaggedObject();
        ASSERT_TRUE(ModuleSpaceTestHelper::InModuleSpace(obj));
        EXPECT_TRUE(obj->GetClass()->IsSourceTextModule()) << "live module corrupted by old GC";
    }
    for (int i = 0; i < NEWBORN; i++) {
        CString name = CString(std::to_string(i).c_str()).insert(0, "orphan_new_");
        JSHandle<SourceTextModule> fresh = ModuleSpaceTestHelper::CreateModule(thread, name);
        EXPECT_EQ(liveAddresses.find(fresh.GetTaggedValue().GetRawData()), liveAddresses.end())
            << "new module aliased a live module's slot";
    }
}

// C2: the store barrier records module->young fields in the old-to-new RSet
// (IN_MODULE_SPACE rides the GENERAL_OLD range); a young GC must keep the
// referent alive and rewrite the slot. The array handle dies before the GC,
// so the RSet is its only channel.
HWTEST_F_L0(LocalCCTest, ModuleToYoungReferenceSurvivesYoungGC)
{
    JSHandle<SourceTextModule> module = ModuleSpaceTestHelper::CreateModule(thread, "young_ref_");
    JSTaggedType youngAddrBefore = 0;
    {
        EcmaHandleScope scope(thread);
        ObjectFactory *factory = thread->GetEcmaVM()->GetFactory();
        JSHandle<TaggedArray> youngArr = factory->NewTaggedArray(4);
        youngArr->Set(thread, 0, JSTaggedValue(123));
        youngArr->Set(thread, 3, JSTaggedValue(456));
        youngAddrBefore = youngArr.GetTaggedValue().GetRawData();
        module->SetRequestedModules(thread, youngArr.GetTaggedValue());
    }
    JSTaggedType moduleAddrBefore = module.GetTaggedValue().GetRawData();
    ASSERT_TRUE(ModuleSpaceTestHelper::InModuleSpace(module.GetTaggedValue().GetTaggedObject()));
    ASSERT_TRUE(Region::ObjectAddressToRange(reinterpret_cast<TaggedObject *>(youngAddrBefore))->InYoungSpace());

    heap_->CollectGarbage(TriggerGCType::YOUNG_GC, GCReason::OTHER);

    // The module never moves in a young GC.
    EXPECT_EQ(module.GetTaggedValue().GetRawData(), moduleAddrBefore);
    // The RSet channel relocated the referent; ASSERT keeps a broken run from
    // reading through the stale slot below.
    JSTaggedValue field = module->GetRequestedModules(thread);
    ASSERT_TRUE(field.IsHeapObject());
    ASSERT_NE(field.GetRawData(), youngAddrBefore);
    JSHandle<TaggedArray> survivor(thread, field);
    EXPECT_EQ(survivor->Get(thread, 0).GetRawData(), JSTaggedValue(123).GetRawData());
    EXPECT_EQ(survivor->Get(thread, 3).GetRawData(), JSTaggedValue(456).GetRawData());
}
// ===== moduleSpace accounting invariants =====
// These cases do not depend on how a GC is triggered; they only assert that
// GetHeapObjectSize() equals live slots * slot size once a collection ended.

static size_t CountModuleSlots(ModuleSpace *moduleSpace)
{
    size_t count = 0;
    moduleSpace->IterateOverObjects([&count](TaggedObject *) { count++; });
    return count;
}

static JSHandle<SourceTextModule> CreateModuleForGC(JSThread *thread, int index, const char *tag)
{
    CString name = CString(std::to_string(index).c_str()).insert(0, tag);
    return ModuleSpaceTestHelper::CreateModule(thread, name);
}

static void CheckModuleAccounting(ModuleSpace *moduleSpace, const char *stage)
{
    size_t slots = CountModuleSlots(moduleSpace);
    EXPECT_EQ(moduleSpace->GetHeapObjectSize(), slots * moduleSpace->GetSlotSize())
        << "used size must equal live slots * slot size (" << stage << ")";
    EXPECT_LE(moduleSpace->GetHeapObjectSize(), moduleSpace->GetCommittedSize()) << stage;
}

// LOCAL_CC -> OLD_GC -> FULL_GC, checking the invariant after every kind of GC.
HWTEST_F_L0(LocalCCTest, ModuleSpaceAccountingAcrossGCs)
{
    ModuleSpace *moduleSpace = heap_->GetModuleSpace();
    ASSERT_TRUE(moduleSpace != nullptr);
    {
        EcmaHandleScope deadScope(thread);
        for (int i = 0; i < 6; i++) {
            JSHandle<SourceTextModule> dead = CreateModuleForGC(thread, i, "module_dead_");
            (void)dead;
        }
    }
    std::vector<JSHandle<SourceTextModule>> live;
    for (int i = 0; i < 4; i++) {
        live.emplace_back(CreateModuleForGC(thread, i, "module_live_"));
        ASSERT_FALSE(live.back().IsEmpty());
    }

    bool succ = heap_->TryTriggerCCMarking(MarkReason::OTHER);
    ASSERT_TRUE(succ);
    heap_->CollectGarbageFromCCMark(GCReason::OTHER);
    ASSERT_TRUE(thread->IsConcurrentCopying());
    heap_->SetNearGCInSensitive(true);
    heap_->WaitAndHandleCCFinished();
    heap_->Prepare();
    CheckModuleAccounting(moduleSpace, "after LOCAL_CC");

    heap_->CollectGarbage(TriggerGCType::OLD_GC, GCReason::OTHER);
    CheckModuleAccounting(moduleSpace, "after OLD_GC");

    heap_->CollectGarbage(TriggerGCType::FULL_GC, GCReason::OTHER);
    CheckModuleAccounting(moduleSpace, "after FULL_GC");
}

// Each FULL_GC resets the counter instead of adding to it: three consecutive
// full GCs must all report the same used size.
HWTEST_F_L0(LocalCCTest, ModuleSpaceFullGCAccountingReset)
{
    ModuleSpace *moduleSpace = heap_->GetModuleSpace();
    constexpr int LIVE = 5;
    std::vector<JSHandle<SourceTextModule>> live;
    for (int i = 0; i < LIVE; i++) {
        live.emplace_back(CreateModuleForGC(thread, i, "module_fullgc_"));
        ASSERT_FALSE(live.back().IsEmpty());
    }
    for (int round = 0; round < 3; round++) {
        heap_->CollectGarbage(TriggerGCType::FULL_GC, GCReason::OTHER);
        EXPECT_EQ(CountModuleSlots(moduleSpace), static_cast<size_t>(LIVE)) << "round " << round;
        EXPECT_EQ(moduleSpace->GetHeapObjectSize(), LIVE * moduleSpace->GetSlotSize())
            << "round " << round << ": counter must be a per-GC snapshot, not a running sum";
    }
}

// Nothing survives: the counter and the committed size must both return to 0.
HWTEST_F_L0(LocalCCTest, DeadModuleReclaimedByFullGC)
{
    ModuleSpace *moduleSpace = heap_->GetModuleSpace();
    {
        EcmaHandleScope deadScope(thread);
        for (int i = 0; i < 8; i++) {
            JSHandle<SourceTextModule> dead = CreateModuleForGC(thread, i, "module_all_dead_");
            (void)dead;
        }
    }
    ASSERT_GT(moduleSpace->GetRegionCount(), 0U);
    heap_->CollectGarbage(TriggerGCType::FULL_GC, GCReason::OTHER);
    EXPECT_EQ(CountModuleSlots(moduleSpace), 0U);
    EXPECT_EQ(moduleSpace->GetHeapObjectSize(), 0U);
    EXPECT_EQ(moduleSpace->GetRegionCount(), 0U);
    EXPECT_EQ(moduleSpace->GetCommittedSize(), 0U);
}

// Evacuation spanning more than one module region: the per-work-node counters
// must be summed exactly once, so used size equals the survivor count.
HWTEST_F_L0(LocalCCTest, ModuleSpaceMultiRegionEvacuation)
{
    ModuleSpace *moduleSpace = heap_->GetModuleSpace();
    constexpr size_t MAX_MODULES = 4000;
    std::vector<JSHandle<SourceTextModule>> live;
    while (moduleSpace->GetRegionCount() < 2 && live.size() < MAX_MODULES) {
        live.emplace_back(CreateModuleForGC(thread, static_cast<int>(live.size()), "module_multi_"));
        ASSERT_FALSE(live.back().IsEmpty());
    }
    // A 256KB region holds ~1560 slots, so the budget above reliably reaches two regions;
    // make that a precondition instead of silently skipping the multi-region evacuation path.
    ASSERT_GE(moduleSpace->GetRegionCount(), 2U);
    heap_->CollectGarbage(TriggerGCType::FULL_GC, GCReason::OTHER);
    EXPECT_EQ(CountModuleSlots(moduleSpace), live.size());
    EXPECT_EQ(moduleSpace->GetHeapObjectSize(), live.size() * moduleSpace->GetSlotSize());
}

// Bytes allocated after the last sweep must be added on top of the snapshot.
HWTEST_F_L0(LocalCCTest, ModuleSpaceOldGCSizeAccounting)
{
    ModuleSpace *moduleSpace = heap_->GetModuleSpace();
    constexpr int FIRST = 3;
    constexpr int SECOND = 4;
    std::vector<JSHandle<SourceTextModule>> live;
    for (int i = 0; i < FIRST; i++) {
        live.emplace_back(CreateModuleForGC(thread, i, "module_after_sweep_"));
        ASSERT_FALSE(live.back().IsEmpty());
    }
    heap_->CollectGarbage(TriggerGCType::OLD_GC, GCReason::OTHER);
    EXPECT_EQ(moduleSpace->GetHeapObjectSize(), FIRST * moduleSpace->GetSlotSize());

    for (int i = 0; i < SECOND; i++) {
        live.emplace_back(CreateModuleForGC(thread, FIRST + i, "module_after_sweep_"));
        ASSERT_FALSE(live.back().IsEmpty());
    }
    // Consumed bytes are settled in batches (when the current free list runs out), so between
    // two GCs the reported size may lag behind the real usage, but it never exceeds it.
    const size_t usedAfterSweep = moduleSpace->GetHeapObjectSize();
    EXPECT_GE(usedAfterSweep, FIRST * moduleSpace->GetSlotSize());
    EXPECT_LE(usedAfterSweep, (FIRST + SECOND) * moduleSpace->GetSlotSize());

    heap_->CollectGarbage(TriggerGCType::OLD_GC, GCReason::OTHER);
    EXPECT_EQ(moduleSpace->GetHeapObjectSize(), (FIRST + SECOND) * moduleSpace->GetSlotSize());
}

// The object walker used by the heap verifier must report exactly the live modules, step over the
// free slots left by the dead ones, and agree with the region count the space reports.
HWTEST_F_L0(LocalCCTest, ModuleSpaceIterateObjectsTest)
{
    ModuleSpace *moduleSpace = heap_->GetModuleSpace();
    constexpr int LIVE_COUNT = 4;
    std::vector<JSHandle<SourceTextModule>> live;
    live.reserve(LIVE_COUNT);
    for (int i = 0; i < LIVE_COUNT; i++) {
        live.emplace_back(CreateModuleForGC(thread, i, "module_iter_live_"));
        ASSERT_FALSE(live.back().IsEmpty());
    }
    {
        EcmaHandleScope deadScope(thread);
        for (int i = 0; i < 3; i++) {
            JSHandle<SourceTextModule> dead = CreateModuleForGC(thread, 10 + i, "module_iter_dead_");
            ASSERT_FALSE(dead.IsEmpty());
        }
    }
    heap_->CollectGarbage(TriggerGCType::OLD_GC, GCReason::OTHER);

    size_t directCount = 0;
    moduleSpace->IterateOverObjects([&directCount](TaggedObject *object) {
        EXPECT_TRUE(ModuleSpaceTestHelper::InModuleSpace(object));
        directCount++;
    });
    EXPECT_EQ(directCount, static_cast<size_t>(LIVE_COUNT));
    EXPECT_GT(moduleSpace->GetRegionCount(), 0U);
}

// Sweeping also runs inline on the mutator thread when concurrent sweeping is disabled. The dead
// modules must be reclaimed and the accounting must stay exact on that path too.
HWTEST_F_L0(LocalCCTest, ModuleSpaceSyncSweepAccountingTest)
{
    ModuleSpace *moduleSpace = heap_->GetModuleSpace();
    constexpr int LIVE_COUNT = 3;
    constexpr int DEAD_COUNT = 3;
    std::vector<JSHandle<SourceTextModule>> live;
    live.reserve(LIVE_COUNT);
    for (int i = 0; i < LIVE_COUNT; i++) {
        live.emplace_back(CreateModuleForGC(thread, i, "module_sync_live_"));
        ASSERT_FALSE(live.back().IsEmpty());
    }
    {
        EcmaHandleScope deadScope(thread);
        for (int i = 0; i < DEAD_COUNT; i++) {
            JSHandle<SourceTextModule> dead = CreateModuleForGC(thread, 10 + i, "module_sync_dead_");
            ASSERT_FALSE(dead.IsEmpty());
        }
    }
    ConcurrentSweeper *sweeper = heap_->GetSweeper();
    sweeper->ConfigConcurrentSweep(false);
    heap_->CollectGarbage(TriggerGCType::OLD_GC, GCReason::OTHER);
    EXPECT_EQ(moduleSpace->GetHeapObjectSize(), LIVE_COUNT * moduleSpace->GetSlotSize());
    EXPECT_EQ(CountModuleSlots(moduleSpace), static_cast<size_t>(LIVE_COUNT));
    sweeper->ConfigConcurrentSweep(true);
}
#endif  // ENABLE_MODULE_SPACE

#if !ENABLE_MODULE_SPACE
// Feature-disabled fallback: modules must go through the legacy young/old path
// and leave moduleSpace empty.
HWTEST_F_L0(LocalCCTest, ModuleSpaceDisabledFallbackTest)
{
    JSHandle<SourceTextModule> module = ModuleSpaceTestHelper::CreateModule(thread, "legacy_module_");
    ASSERT_FALSE(module.IsEmpty());
    EXPECT_FALSE(ModuleSpaceTestHelper::InModuleSpace(module.GetTaggedValue().GetTaggedObject()));
    EXPECT_EQ(heap_->GetModuleSpace()->GetRegionCount(), 0U);

    heap_->CollectGarbage(TriggerGCType::OLD_GC, GCReason::OTHER);
    EXPECT_FALSE(ModuleSpaceTestHelper::InModuleSpace(module.GetTaggedValue().GetTaggedObject()));
    EXPECT_EQ(heap_->GetModuleSpace()->GetRegionCount(), 0U);
}
#endif  // !ENABLE_MODULE_SPACE

#endif  // PANDA_TARGET_64

}  // namespace panda::test
