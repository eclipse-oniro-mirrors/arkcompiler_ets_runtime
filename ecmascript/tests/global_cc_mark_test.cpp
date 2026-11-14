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

// GlobalCC mark acceptance tests (AC-1.1 .. AC-4.1, see spec.md).
// A GlobalCC round has no production trigger yet, so Mark()/ReMark() are driven from a
// daemon-posted task on the daemon thread.

#include "ecmascript/checkpoint/thread_state_transition.h"
#include "ecmascript/daemon/daemon_task.h"
#include "ecmascript/daemon/daemon_thread.h"
#include "ecmascript/ecma_vm.h"
#include "ecmascript/js_object-inl.h"
#include "ecmascript/mem/global_cc/global_cc.h"
#include "ecmascript/mem/global_cc/global_concurrent_marker.h"
#include "ecmascript/mem/global_cc/global_mark_visitor-inl.h"
#include "ecmascript/mem/global_cc/global_mark_work_manager.h"
#include "ecmascript/mem/heap.h"
#include "ecmascript/napi/include/jsnapi.h"
#include "ecmascript/object_factory.h"
#include "ecmascript/runtime_lock.h"
#include "ecmascript/tests/ecma_test_common.h"

using namespace panda;
using namespace panda::ecmascript;

namespace panda::test {

namespace {
bool g_taskIsRunning {false};
Mutex g_mutex;
ConditionVariable g_cv;

class GlobalCCMarkerTest : public GlobalCCMarker {
public:
    explicit GlobalCCMarkerTest(GlobalCC *cc) : GlobalCCMarker(cc) {}
    ~GlobalCCMarkerTest() override = default;

    void HandleMarkingFinished() override
    {
        DaemonThread *dThread = DaemonThread::GetInstance();
        {
            // ReMark requires a launched suspend-all.
            SuspendAllScope<DaemonThread> scope(dThread);
            ReMark();
            dThread->FinishRunningTask();
        }
    }
};

void GlobalCCRoundRunner()
{
    SharedHeap *sHeap = SharedHeap::GetInstance();
    GlobalCCMarkerTest marker(sHeap->GetGlobalCC());
    marker.Mark();
    LockHolder lock(g_mutex);
    g_taskIsRunning = false;
    g_cv.SignalAll();
}

class GlobalCCRoundTask : public DaemonTask {
public:
    explicit GlobalCCRoundTask(JSThread *thread)
        : DaemonTask(thread, DaemonTaskType::MOCK_FOR_TEST, DaemonTaskGroup::MOCK_FOR_TEST_GROUP,
                     &GlobalCCRoundRunner, nullptr) {}
    ~GlobalCCRoundTask() override = default;
};
}  // namespace

class GlobalCCMarkTest : public BaseTestWithScope<false> {
public:
    void SetUp() override
    {
        JSRuntimeOptions options;
        options.SetEnableForceGC(false);
        instance = JSNApi::CreateEcmaVM(options);
        EXPECT_TRUE(instance != nullptr) << "Cannot create EcmaVM";
        thread = instance->GetJSThread();
        thread->ManagedCodeBegin();
        scope = new EcmaHandleScope(thread);
    }

    void RunGlobalCCRound()
    {
        LockHolder lock(g_mutex);
        g_taskIsRunning = true;
        DaemonThread::PostTaskResult result =
            DaemonThread::GetInstance()->CheckAndPostTask(GlobalCCRoundTask(thread));
        EXPECT_TRUE(result == DaemonThread::PostTaskResult::SUCCESS);
    }

    void WaitGlobalCCRoundFinish()
    {
        ThreadNativeScope nativeScope(thread);
        LockHolder lock(g_mutex);
        while (g_taskIsRunning) {
            g_cv.Wait(&g_mutex);
        }
    }

    JSHandle<TaggedArray> AllocateLocalArray(size_t arrayLen)
    {
        return instance->GetFactory()->NewTaggedArray(arrayLen, JSTaggedValue::Undefined(), false);
    }

    JSHandle<TaggedArray> AllocateSharedArray(size_t arrayLen)
    {
        return instance->GetFactory()->NewSTaggedArray(arrayLen, JSTaggedValue::Undefined());
    }

    void AllocateGarbage()
    {
        constexpr size_t GARBAGE_LEN = 16;
        constexpr size_t GARBAGE_COUNT = 32;
        EcmaHandleScope temporaryScope(thread);
        for (size_t i = 0; i < GARBAGE_COUNT; i++) {
            AllocateLocalArray(GARBAGE_LEN);
            AllocateSharedArray(GARBAGE_LEN);
        }
    }
};

// ------------------------------------------------------------------
// AC-1.1: one round establishes a cross-heap strong closure.
// ------------------------------------------------------------------
HWTEST_F_L0(GlobalCCMarkTest, GlobalCCMarkCrossHeapReachableTest)
{
    constexpr size_t ARRAY_LEN = 10;
    JSHandle<TaggedArray> localArr = AllocateLocalArray(ARRAY_LEN);
    {
        EcmaHandleScope temporaryScope(thread);
        JSHandle<TaggedArray> sharedArr = AllocateSharedArray(ARRAY_LEN);
        localArr->Set(thread, 0, sharedArr);
    }
    AllocateGarbage();
    RunGlobalCCRound();
    while (!thread->HasSuspendRequest()) {
        continue;
    }
    JSTaggedValue ref = localArr->Get(thread, 0);
    EXPECT_TRUE(ref.IsHeapObject());
    EXPECT_TRUE(ref.IsInSharedHeap());
    EXPECT_EQ(JSHandle<TaggedArray>(thread, ref)->GetLength(), ARRAY_LEN);
    WaitGlobalCCRoundFinish();
    Region *region = Region::ObjectAddressToRange(ref.GetTaggedObject());
    EXPECT_TRUE(region->Test(ref.GetRawData()));
}

// ------------------------------------------------------------------
// AC-1.2 / AC-3.1: write barrier marks during concurrent mark (no miss) and is
// routed to GlobalCC only while the thread flag is set.
// ------------------------------------------------------------------
HWTEST_F_L0(GlobalCCMarkTest, GlobalCCWriteBarrierNoMissTest)
{
    constexpr size_t ARRAY_LEN = 8;
    JSHandle<TaggedArray> holder = AllocateLocalArray(ARRAY_LEN);
    JSHandle<TaggedArray> target = AllocateLocalArray(ARRAY_LEN);
    RunGlobalCCRound();
    while (!thread->HasSuspendRequest()) {
        continue;
    }
    thread->CheckSafepointIfSuspended();
    JSHandle<TaggedArray> newObj = AllocateLocalArray(ARRAY_LEN);
    JSHandle<TaggedArray> sharedObj = AllocateSharedArray(ARRAY_LEN);
    holder->Set(thread, 0, target);     // 0 : first slot
    holder->Set(thread, 1, newObj);     // 1 : second slot
    holder->Set(thread, 2, sharedObj);  // 2 : third slot
    JSTaggedValue ref = holder->Get(thread, 0);
    EXPECT_TRUE(ref.IsHeapObject());
    EXPECT_EQ(ref.GetRawHeapObject(), target.GetTaggedValue().GetRawHeapObject());
    WaitGlobalCCRoundFinish();
    Region *targetRegion = Region::ObjectAddressToRange(*target);
    EXPECT_TRUE(targetRegion->Test(*target));
    Region *newObjRegion = Region::ObjectAddressToRange(*newObj);
    EXPECT_TRUE(newObjRegion->Test(*newObj));
}

// ------------------------------------------------------------------
// AC-2.1: ReMark reaches a fixpoint (no residual work after the round).
// ------------------------------------------------------------------
HWTEST_F_L0(GlobalCCMarkTest, GlobalCCReMarkFixpointTest)
{
    JSHandle<TaggedArray> alive = AllocateLocalArray(32);
    AllocateGarbage();
    RunGlobalCCRound();
    GlobalCCWorkManager *wm = SharedHeap::GetInstance()->GetGlobalCCWorkManager();
    EXPECT_TRUE(wm->IsGlobalMarkEmpty());
    EXPECT_TRUE(wm->IsGlobalFreshWeakEmpty());
    EXPECT_TRUE(wm->IsGlobalPendingWeakEmpty());
    EXPECT_EQ(alive->GetLength(), 32);
    WaitGlobalCCRoundFinish();
}

// ------------------------------------------------------------------
// AC-2.2: weak closure — dead weak refs cleared, live weak refs kept.
// ------------------------------------------------------------------
HWTEST_F_L0(GlobalCCMarkTest, GlobalCCWeakClosureTest)
{
    constexpr size_t ARRAY_LEN = 10;
    JSHandle<TaggedArray> holder = AllocateLocalArray(ARRAY_LEN);
    JSHandle<TaggedArray> target = AllocateLocalArray(ARRAY_LEN);
    JSTaggedValue weak = target.GetTaggedValue();
    weak.CreateWeakRef();
    holder->Set(thread, 0, weak);
    EXPECT_TRUE(JSTaggedValue(holder->Get(thread, 0)).IsWeak());
    RunGlobalCCRound();
    // Live target: weak ref must be retained.
    JSTaggedValue afterGC = holder->Get(thread, 0);
    EXPECT_TRUE(afterGC.IsWeak());
    EXPECT_EQ(afterGC.GetWeakReferent(), target.GetTaggedValue().GetRawHeapObject());
    WaitGlobalCCRoundFinish();
}

// ------------------------------------------------------------------
// AC-4.1: GlobalCC mark verification runs when heap verify is enabled.
// ------------------------------------------------------------------
class GlobalCCVerificationTest : public GlobalCCMarkTest {
public:
    void SetUp() override
    {
        JSRuntimeOptions options;
        options.SetArkProperties(options.GetArkProperties() | ArkProperties::ENABLE_HEAP_VERIFY);
        options.SetEnableForceGC(false);
        instance = JSNApi::CreateEcmaVM(options);
        thread = instance->GetJSThread();
        thread->ManagedCodeBegin();
        scope = new EcmaHandleScope(thread);
    }
};

HWTEST_F_L0(GlobalCCVerificationTest, GlobalCCVerifyMarkTest)
{
    constexpr size_t ARRAY_LEN = 10;
    constexpr size_t FILLER_ENTRIES = 5;
    ObjectFactory *factory = instance->GetFactory();
    JSHandle<JSTaggedValue> objFun(thread->GetGlobalEnv()->GetObjectFunction());
    JSHandle<TaggedArray> localArr = AllocateLocalArray(ARRAY_LEN);
    {
        EcmaHandleScope temporaryScope(thread);
        JSHandle<TaggedArray> sharedArr = AllocateSharedArray(ARRAY_LEN);
        localArr->Set(thread, 0, sharedArr);
    }
    JSHandle<TaggedArray> alive = AllocateLocalArray(16);
    JSHandle<TaggedArray> slotArray = AllocateLocalArray(6); // [2..5] stay Undefined (RO targets)
    JSHandle<TaggedArray> strongTarget = AllocateLocalArray(ARRAY_LEN);
    JSHandle<TaggedArray> weakTarget = AllocateLocalArray(ARRAY_LEN);
    slotArray->Set(thread, 0, strongTarget.GetTaggedValue());
    slotArray->Set(thread, 1, JSTaggedValue(42)); // smi slot
    {
        JSTaggedValue weakVal(weakTarget.GetTaggedValue());
        weakVal.CreateWeakRef();
        slotArray->Set(thread, 2, weakVal); // live weak slot
    }
    JSHandle<JSObject> mixedObj = factory->NewJSObjectByConstructor(JSHandle<JSFunction>(objFun), objFun);
    JSHandle<TaggedArray> inObjTarget = AllocateLocalArray(ARRAY_LEN);
    JSHandle<JSTaggedValue> dKey(factory->NewFromASCII("dProp"));
    JSHandle<JSTaggedValue> tKey(factory->NewFromASCII("tProp"));
    PropertyAttributes doubleAttr = PropertyAttributes::Default();
    doubleAttr.SetOffset(0);
    doubleAttr.SetIsInlinedProps(true);
    doubleAttr.SetRepresentation(Representation::DOUBLE);
    JSHClass::AddProperty(thread, mixedObj, dKey, doubleAttr, Representation::DOUBLE);
    PropertyAttributes taggedAttr = PropertyAttributes::Default();
    taggedAttr.SetOffset(1);
    taggedAttr.SetIsInlinedProps(true);
    JSHClass::AddProperty(thread, mixedObj, tKey, taggedAttr, Representation::NONE);
    EXPECT_TRUE(JSObject::SetProperty(thread, mixedObj, tKey, JSHandle<JSTaggedValue>(inObjTarget)));
    mixedObj->GetJSHClass()->SetIsAllTaggedProp(false);
    EXPECT_FALSE(mixedObj->GetJSHClass()->IsAllTaggedProp());
    JSHandle<JSTaggedValue> liveKey(JSHandle<JSObject>(factory->NewJSObjectByConstructor(
        JSHandle<JSFunction>(objFun), objFun)));
    JSHandle<JSTaggedValue> liveValue(JSHandle<TaggedArray>(AllocateLocalArray(ARRAY_LEN)));
    JSHandle<JSTaggedValue> smiKey(JSHandle<JSObject>(factory->NewJSObjectByConstructor(
        JSHandle<JSFunction>(objFun), objFun)));
    JSHandle<JSTaggedValue> smiVal(thread, JSTaggedValue(7));
    std::vector<JSHandle<WeakLinkedHashMap>> tableSnapshots;
    JSHandle<WeakLinkedHashMap> map = WeakLinkedHashMap::Create(thread, 2);
    tableSnapshots.emplace_back(map);
    map = WeakLinkedHashMap::SetWeakRef(thread, map, liveKey, liveValue);
    tableSnapshots.emplace_back(map);
    map = WeakLinkedHashMap::SetWeakRef(thread, map, smiKey, smiVal);
    for (size_t i = 0; i < FILLER_ENTRIES; ++i) {
        JSHandle<JSTaggedValue> fillerKey(JSHandle<JSObject>(factory->NewJSObjectByConstructor(
            JSHandle<JSFunction>(objFun), objFun)));
        JSHandle<JSTaggedValue> fillerValue(JSHandle<TaggedArray>(AllocateLocalArray(ARRAY_LEN)));
        tableSnapshots.emplace_back(map);
        map = WeakLinkedHashMap::SetWeakRef(thread, map, fillerKey, fillerValue);
    }
    bool anyNextTable = false;
    for (const auto &snapshot : tableSnapshots) {
        anyNextTable |=
            JSTaggedValue(snapshot->GetData()[WeakLinkedHashMap::NEXT_TABLE_INDEX]).IsHeapObject();
    }
    EXPECT_TRUE(anyNextTable);
    {
        EcmaHandleScope temporaryScope(thread);
        JSHandle<JSTaggedValue> deadKey(JSHandle<JSObject>(factory->NewJSObjectByConstructor(
            JSHandle<JSFunction>(objFun), objFun)));
        JSHandle<JSTaggedValue> deadValue(JSHandle<TaggedArray>(AllocateLocalArray(ARRAY_LEN)));
        map = WeakLinkedHashMap::SetWeakRef(thread, map, deadKey, deadValue);
    }
    RunGlobalCCRound();
    while (!thread->HasSuspendRequest()) {
        continue;
    }
    thread->CheckSafepointIfSuspended();
    WaitGlobalCCRoundFinish();
    EXPECT_EQ(alive->GetLength(), 16);
    JSTaggedValue sharedRef = localArr->Get(thread, 0);
    EXPECT_TRUE(sharedRef.IsHeapObject());
    EXPECT_TRUE(sharedRef.IsInSharedHeap());
    EXPECT_EQ(slotArray->Get(thread, 0), strongTarget.GetTaggedValue());
    JSTaggedValue weakSlot = slotArray->Get(thread, 2);
    EXPECT_TRUE(weakSlot.IsWeak());
    EXPECT_EQ(weakSlot.GetWeakReferent(), weakTarget.GetTaggedValue().GetRawHeapObject());
    EXPECT_NE(map->FindElement(thread, liveKey.GetTaggedValue()), -1);
    int smiEntry = map->FindElement(thread, smiKey.GetTaggedValue());
    EXPECT_NE(smiEntry, -1);
    EXPECT_EQ(map->GetValue(thread, smiEntry), JSTaggedValue(7));
    EXPECT_EQ(JSObject::GetProperty(thread, JSHandle<JSTaggedValue>(mixedObj), tKey).GetValue().GetTaggedValue(),
              inObjTarget.GetTaggedValue());
}
}  // namespace panda::test
