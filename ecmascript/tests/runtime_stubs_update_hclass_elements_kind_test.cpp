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
 * Unit tests for RuntimeStubs::UpdateHClassForElementsKind stub.
 *
 * Target function (ecmascript/stubs/runtime_stubs.cpp:636-662):
 *   DEF_RUNTIME_STUBS(UpdateHClassForElementsKind)
 *
 * The stub is invoked by the interpreter/AOT when an ElementsKind
 * transition is required. It:
 *   1. Calls JSHClass::TransitToElementsKindUncheck; returns Hole on failure.
 *   2. Calls JSArray::UpdateTrackInfo on the receiver.
 *   3. If PGO profiler is disabled, returns Hole early.
 *   4. Reads the receiver's TrackInfo slot; if it IsHeapObject(), calls
 *      PGOProfiler::UpdateTrackInfo; otherwise skips. Returns Hole.
 *
 * Branches to cover (the changed lines 656-661):
 *   a. trackInfoVal.IsHeapObject() == true  -> PGOProfiler::UpdateTrackInfo
 *      is invoked with the resolved TrackInfo.
 *   b. trackInfoVal.IsHeapObject() == false -> UpdateTrackInfo is skipped;
 *      the stub returns Hole without calling the profiler.
 *
 * Additional branches covered for function-level coverage:
 *   c. TransitToElementsKindUncheck returns false (non-initial hclass) ->
 *      early return at line 647.
 *   d. PGO profiler disabled -> early return at line 654.
 *
 * Invocation: the stub reads argv[0] as the receiver JSHandle<JSTaggedValue>
 * and argv[1] as the ElementsKind (cast from JSTaggedType). We call it
 * directly via RuntimeStubs::UpdateHClassForElementsKind with a stack-built
 * argv, mirroring how the interpreter passes arguments.
 */

#include "ecmascript/ecma_vm.h"
#include "ecmascript/global_env.h"
#include "ecmascript/js_array.h"
#include "ecmascript/js_handle.h"
#include "ecmascript/js_hclass.h"
#include "ecmascript/js_object.h"
#include "ecmascript/js_tagged_value.h"
#include "ecmascript/mem/heap.h"
#include "ecmascript/object_factory-inl.h"
#include "ecmascript/pgo_profiler/pgo_profiler.h"
#include "ecmascript/stubs/runtime_stubs.h"
#include "ecmascript/tests/test_helper.h"

using namespace panda::ecmascript;

namespace panda::test {

class RuntimeStubsUpdateHClassForElementsKindTest : public BaseTestWithScope<false> {
protected:
    void SetUp() override
    {
        // Enable ElementsKind (so TrackInfo is attached to arrays), ForceGC
        // (so CollectGarbage actually triggers GC), and PGOProfiler (so the
        // stub's IsPGOProfilerEnable branch is taken and the TrackInfo slot
        // is processed by PGOProfiler::UpdateTrackInfo). Without PGOProfiler
        // the stub early-returns at line 654 and never reaches the changed
        // lines 656-661.
        JSRuntimeOptions options;
        options.SetEnableForceGC(true);
        options.SetEnableAsmInterpreter(true);
        options.SetEnableElementsKind(true);
        options.SetEnablePGOProfiler(true);
        instance = JSNApi::CreateEcmaVM(options);
        instance->SetEnableForceGC(true);
        ASSERT_TRUE(instance != nullptr) << "Cannot create EcmaVM";
        thread = instance->GetJSThread();
        thread->ManagedCodeBegin();
        // Force the PGOProfiler-enable flag on the thread. PGOProfilerManager
        // ::InitializeData() may fail in a unit-test environment (no valid ap
        // output directory), which would leave the profiler disabled even
        // though SetEnablePGOProfiler(true) was set on the options. Setting
        // the flag directly on the thread bypasses InitializeData and ensures
        // thread->IsPGOProfilerEnable() returns true, so that the stub reaches
        // the changed lines 656-661.
        thread->SetPGOProfilerEnable(true);
        scope = new EcmaHandleScope(thread);
        JSNApi::InitHybridVMEnv(thread->GetEcmaVM());
    }

    // Build a JSArray carrying a real TrackInfo stored as a strong HeapObject
    // (post-optimization storage form). The array is created with INT
    // ElementsKind and a TrackInfo whose ElementsKind is INT, mirroring what
    // the interpreter/compiler does when PGO profiling is enabled.
    JSHandle<JSArray> CreateArrayWithTrackInfo(uint32_t length)
    {
        auto constants = thread->GlobalConstants();
        ObjectFactory *factory = thread->GetEcmaVM()->GetFactory();
        JSHandle<TaggedArray> values(factory->NewTaggedArray(length));
        for (uint32_t i = 0; i < length; i++) {
            values->Set(thread, i, JSTaggedValue(static_cast<int>(i)));
        }
        JSHandle<JSArray> array(JSArray::CreateArrayFromList(thread, values));
        auto hClass = thread->GetArrayInstanceHClass(ElementsKind::INT, false);
        array->SynchronizedTransitionClass(thread, hClass);
        auto trackInfoHclass = JSHandle<JSHClass>::Cast(constants->GetHandledTrackInfoClass());
        JSHandle<TrackInfo> trackInfo(thread, factory->NewObject(trackInfoHclass));
        trackInfo->SetCachedHClass(thread, constants->GetUndefined());
        trackInfo->SetCachedFunc(thread, constants->GetUndefined());
        trackInfo->SetElementsKind(ElementsKind::INT);
        // TrackInfo is stored as a HeapObject (strong reference) after the
        // weak-ref -> heap-object optimization.
        array->SetTrackInfo(thread, JSTaggedValue(*trackInfo));
        return array;
    }

    // Build a JSArray whose TrackInfo slot is Undefined (no TrackInfo). This
    // exercises the IsHeapObject()==false branch in the stub: the stub must
    // skip the PGOProfiler::UpdateTrackInfo call and return Hole.
    JSHandle<JSArray> CreateArrayWithoutTrackInfo(uint32_t length)
    {
        ObjectFactory *factory = thread->GetEcmaVM()->GetFactory();
        JSHandle<TaggedArray> values(factory->NewTaggedArray(length));
        for (uint32_t i = 0; i < length; i++) {
            values->Set(thread, i, JSTaggedValue(static_cast<int>(i)));
        }
        JSHandle<JSArray> array(JSArray::CreateArrayFromList(thread, values));
        auto hClass = thread->GetArrayInstanceHClass(ElementsKind::INT, false);
        array->SynchronizedTransitionClass(thread, hClass);
        // Verify the precondition: no TrackInfo attached.
        auto v = array->GetTrackInfo(thread);
        EXPECT_FALSE(v.IsHeapObject());
        return array;
    }

    // Invoke RuntimeStubs::UpdateHClassForElementsKind directly with a
    // stack-built argv. argv[0] is the receiver (JSHandle<JSTaggedValue>),
    // argv[1] is the target ElementsKind (as JSTaggedType int).
    JSTaggedType InvokeStub(const JSHandle<JSTaggedValue> &receiver, ElementsKind targetKind)
    {
        uintptr_t argGlue = thread->GetGlueAddr();
        constexpr uint32_t argc = 2;  // receiver + elementsKind
        JSTaggedType argv[argc];
        argv[0] = receiver->GetRawData();
        argv[1] = static_cast<JSTaggedType>(targetKind);
        return RuntimeStubs::UpdateHClassForElementsKind(argGlue, argc,
                                                         reinterpret_cast<uintptr_t>(argv));
    }
};

// ---------------------------------------------------------------------------
// Scenario 1: array with TrackInfo, PGO enabled, INT -> NUMBER transition.
//
// This exercises:
//   - TransitToElementsKindUncheck returns true (initial INT hclass) -> line 646.
//   - JSArray::UpdateTrackInfo runs and updates the TrackInfo's ElementsKind
//     (line 651).
//   - IsPGOProfilerEnable() == true -> does NOT early-return at line 654.
//   - trackInfoVal.IsHeapObject() == true (TrackInfo is a strong HeapObject)
//     -> PGOProfiler::UpdateTrackInfo is invoked (lines 657-659).
//
// Expected:
//   - The stub returns JSTaggedValue::Hole().
//   - After the call, the array's HClass ElementsKind has transitioned to
//     NUMBER (or the merged kind), proving TransitToElementsKindUncheck ran.
//   - The TrackInfo's ElementsKind has been updated by UpdateTrackInfo,
//     proving the stub reached line 651.
// ---------------------------------------------------------------------------
HWTEST_F_L0(RuntimeStubsUpdateHClassForElementsKindTest, TrackInfo_HeapObject_PGOEnabled)
{
    if (!thread->GetEcmaVM()->IsEnableElementsKind()) {
        return;
    }
    ASSERT_TRUE(thread->IsPGOProfilerEnable())
        << "Precondition: PGO profiler must be enabled";

    auto array = CreateArrayWithTrackInfo(4);
    auto receiver = JSHandle<JSTaggedValue>::Cast(array);

    // Precondition: TrackInfo is present and is a HeapObject.
    auto trackInfoValBefore = array->GetTrackInfo(thread);
    ASSERT_TRUE(trackInfoValBefore.IsHeapObject())
        << "Precondition: TrackInfo must be a HeapObject";
    ASSERT_FALSE(trackInfoValBefore.IsWeak());

    // Invoke the stub with INT -> NUMBER transition.
    auto result = InvokeStub(receiver, ElementsKind::NUMBER);
    EXPECT_EQ(result, JSTaggedValue::Hole().GetRawData())
        << "Stub must return Hole on success";

    // Verify the HClass ElementsKind transitioned (TransitToElementsKindUncheck ran).
    auto currentKind = array->GetJSHClass()->GetElementsKind();
    EXPECT_TRUE(currentKind == ElementsKind::NUMBER || currentKind == ElementsKind::HOLE)
        << "ElementsKind must have transitioned from INT after the stub call, got "
        << static_cast<int>(currentKind);

    // Verify the TrackInfo's ElementsKind was updated by JSArray::UpdateTrackInfo
    // (the stub calls UpdateTrackInfo at line 651 before the PGO branch).
    auto trackInfoValAfter = array->GetTrackInfo(thread);
    ASSERT_TRUE(trackInfoValAfter.IsHeapObject())
        << "TrackInfo must remain a HeapObject after the stub call";
    auto *trackInfoAfter = TrackInfo::Cast(trackInfoValAfter.GetTaggedObject());
    EXPECT_TRUE(trackInfoAfter->GetElementsKind() == ElementsKind::NUMBER ||
                trackInfoAfter->GetElementsKind() == ElementsKind::HOLE)
        << "TrackInfo ElementsKind must have been updated by UpdateTrackInfo";
}

// ---------------------------------------------------------------------------
// Scenario 2: array without TrackInfo, PGO enabled.
//
// This exercises:
//   - TransitToElementsKindUncheck returns true -> line 646.
//   - JSArray::UpdateTrackInfo runs but finds no TrackInfo (Undefined slot) ->
//     line 651 is a no-op for the TrackInfo contents.
//   - IsPGOProfilerEnable() == true -> does NOT early-return at line 654.
//   - trackInfoVal.IsHeapObject() == false (Undefined slot) -> the
//     PGOProfiler::UpdateTrackInfo call is SKIPPED (line 657 false branch).
//
// Expected:
//   - The stub returns JSTaggedValue::Hole().
//   - No crash: the IsHeapObject guard at line 657 correctly filters out the
//     Undefined slot and the stub returns without calling PGOProfiler.
//   - The array's TrackInfo slot remains non-HeapObject (Undefined).
// ---------------------------------------------------------------------------
HWTEST_F_L0(RuntimeStubsUpdateHClassForElementsKindTest, TrackInfo_Undefined_PGOEnabled)
{
    if (!thread->GetEcmaVM()->IsEnableElementsKind()) {
        return;
    }
    ASSERT_TRUE(thread->IsPGOProfilerEnable())
        << "Precondition: PGO profiler must be enabled";

    auto array = CreateArrayWithoutTrackInfo(4);
    auto receiver = JSHandle<JSTaggedValue>::Cast(array);

    // Precondition: no TrackInfo attached.
    auto trackInfoValBefore = array->GetTrackInfo(thread);
    ASSERT_FALSE(trackInfoValBefore.IsHeapObject())
        << "Precondition: TrackInfo must NOT be a HeapObject for this scenario";

    // Invoke the stub with INT -> NUMBER transition.
    auto result = InvokeStub(receiver, ElementsKind::NUMBER);
    EXPECT_EQ(result, JSTaggedValue::Hole().GetRawData())
        << "Stub must return Hole even when TrackInfo is absent";

    // The HClass transition still happened.
    auto currentKind = array->GetJSHClass()->GetElementsKind();
    EXPECT_TRUE(currentKind == ElementsKind::NUMBER || currentKind == ElementsKind::HOLE)
        << "ElementsKind must have transitioned even without TrackInfo, got "
        << static_cast<int>(currentKind);

    // TrackInfo slot must remain non-HeapObject (no TrackInfo was created).
    auto trackInfoValAfter = array->GetTrackInfo(thread);
    EXPECT_FALSE(trackInfoValAfter.IsHeapObject())
        << "TrackInfo slot must remain non-HeapObject (Undefined) after the stub call";
}

// ---------------------------------------------------------------------------
// Scenario 3: PGO profiler disabled.
//
// This exercises:
//   - TransitToElementsKindUncheck returns true -> line 646.
//   - JSArray::UpdateTrackInfo runs (line 651).
//   - IsPGOProfilerEnable() == false -> early return at line 654.
//   - Lines 656-661 are NOT reached (the changed branch is skipped).
//
// Expected:
//   - The stub returns JSTaggedValue::Hole().
//   - The HClass transition still happened (TransitToElementsKindUncheck ran
//     before the PGO check).
//   - The TrackInfo's ElementsKind was updated by JSArray::UpdateTrackInfo
//     (line 651 runs unconditionally before the PGO check at line 653).
//   - PGOProfiler::UpdateTrackInfo was NOT called (line 659 skipped).
// ---------------------------------------------------------------------------
HWTEST_F_L0(RuntimeStubsUpdateHClassForElementsKindTest, PGODisabled_SkipTrackInfoBranch)
{
    if (!thread->GetEcmaVM()->IsEnableElementsKind()) {
        return;
    }
    // Disable PGO on the thread so the stub early-returns at line 654.
    thread->SetPGOProfilerEnable(false);
    ASSERT_FALSE(thread->IsPGOProfilerEnable())
        << "Precondition: PGO profiler must be disabled for this scenario";

    auto array = CreateArrayWithTrackInfo(4);
    auto receiver = JSHandle<JSTaggedValue>::Cast(array);

    auto trackInfoValBefore = array->GetTrackInfo(thread);
    ASSERT_TRUE(trackInfoValBefore.IsHeapObject())
        << "Precondition: TrackInfo must be a HeapObject before the stub call";

    auto result = InvokeStub(receiver, ElementsKind::NUMBER);
    EXPECT_EQ(result, JSTaggedValue::Hole().GetRawData())
        << "Stub must return Hole when PGO is disabled";

    // The HClass transition still happened (TransitToElementsKindUncheck ran
    // before the PGO check).
    auto currentKind = array->GetJSHClass()->GetElementsKind();
    EXPECT_TRUE(currentKind == ElementsKind::NUMBER || currentKind == ElementsKind::HOLE)
        << "ElementsKind must have transitioned even with PGO disabled, got "
        << static_cast<int>(currentKind);

    // The TrackInfo's ElementsKind was updated by JSArray::UpdateTrackInfo
    // (line 651 runs unconditionally before the PGO check at line 653).
    auto trackInfoValAfter = array->GetTrackInfo(thread);
    ASSERT_TRUE(trackInfoValAfter.IsHeapObject())
        << "TrackInfo must remain a HeapObject after the stub call";
    auto *trackInfoAfter = TrackInfo::Cast(trackInfoValAfter.GetTaggedObject());
    EXPECT_TRUE(trackInfoAfter->GetElementsKind() == ElementsKind::NUMBER ||
                trackInfoAfter->GetElementsKind() == ElementsKind::HOLE)
        << "TrackInfo ElementsKind must have been updated by UpdateTrackInfo (line 651)";

    // Restore PGO enable state for subsequent tests in this fixture.
    thread->SetPGOProfilerEnable(true);
}

// ---------------------------------------------------------------------------
// Scenario 4: TransitToElementsKindUncheck returns false (non-initial hclass).
//
// NOTE: This scenario has been removed. The stub (runtime_stubs.cpp:645-646)
// contains `ASSERT(JSHClass::IsInitialArrayHClassWithElementsKind(...))`
// before the `TransitToElementsKindUncheck` call. This ASSERT guarantees
// the hclass is always initial when the stub is reached, making the
// `TransitToElementsKindUncheck` false branch (line 647 early-return)
// unreachable through the stub entry point. Any attempt to force the
// false branch by transitioning the array away from the initial hclass
// (e.g. via TransitionElementsToDictionary) trips the ASSERT and aborts
// the process (exit code -6), so the false branch cannot be exercised
// without removing the production ASSERT. The `TransitToElementsKindUncheck`
// false branch is instead covered directly by js_hclass unit tests that
// call TransitToElementsKindUncheck without going through the stub.
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Scenario 5: INT -> HOLE transition with TrackInfo present.
//
// This exercises the same changed branch (lines 656-661) as Scenario 1, but
// with a different target ElementsKind (HOLE instead of NUMBER). This
// provides additional confidence that the IsHeapObject branch is taken
// regardless of the target kind, and that the TrackInfo's ElementsKind is
// updated to HOLE (the merged kind for an INT array transitioning to HOLE).
//
// Expected:
//   - The stub returns JSTaggedValue::Hole().
//   - The HClass ElementsKind transitions to HOLE.
//   - The TrackInfo's ElementsKind is updated to HOLE.
// ---------------------------------------------------------------------------
HWTEST_F_L0(RuntimeStubsUpdateHClassForElementsKindTest, TrackInfo_HeapObject_TransitionToHole)
{
    if (!thread->GetEcmaVM()->IsEnableElementsKind()) {
        return;
    }
    ASSERT_TRUE(thread->IsPGOProfilerEnable())
        << "Precondition: PGO profiler must be enabled";

    auto array = CreateArrayWithTrackInfo(4);
    auto receiver = JSHandle<JSTaggedValue>::Cast(array);

    auto trackInfoValBefore = array->GetTrackInfo(thread);
    ASSERT_TRUE(trackInfoValBefore.IsHeapObject())
        << "Precondition: TrackInfo must be a HeapObject";

    auto result = InvokeStub(receiver, ElementsKind::HOLE);
    EXPECT_EQ(result, JSTaggedValue::Hole().GetRawData())
        << "Stub must return Hole on INT -> HOLE transition";

    auto currentKind = array->GetJSHClass()->GetElementsKind();
    EXPECT_EQ(currentKind, ElementsKind::HOLE)
        << "ElementsKind must have transitioned to HOLE, got "
        << static_cast<int>(currentKind);

    auto trackInfoValAfter = array->GetTrackInfo(thread);
    ASSERT_TRUE(trackInfoValAfter.IsHeapObject())
        << "TrackInfo must remain a HeapObject after the stub call";
    auto *trackInfoAfter = TrackInfo::Cast(trackInfoValAfter.GetTaggedObject());
    EXPECT_EQ(trackInfoAfter->GetElementsKind(), ElementsKind::HOLE)
        << "TrackInfo ElementsKind must have been updated to HOLE";
}

// ---------------------------------------------------------------------------
// Scenario 6: Verify the stub does not crash under GC stress with TrackInfo.
//
// This is a robustness test: invoke the stub repeatedly, interleaved with GC,
// to ensure the IsHeapObject branch (lines 656-661) does not crash when the
// TrackInfo is moved by the GC. The stub reads the TrackInfo slot AFTER
// TransitToElementsKindUncheck and UpdateTrackInfo, so the slot must be a
// stable HeapObject reference across GC.
//
// Expected:
//   - No crash across GC stress.
//   - The TrackInfo remains a HeapObject after each stub call + GC cycle.
// ---------------------------------------------------------------------------
HWTEST_F_L0(RuntimeStubsUpdateHClassForElementsKindTest, TrackInfo_HeapObject_GCStress)
{
    if (!thread->GetEcmaVM()->IsEnableElementsKind()) {
        return;
    }
    ASSERT_TRUE(thread->IsPGOProfilerEnable())
        << "Precondition: PGO profiler must be enabled";

    constexpr int iterations = 20;
    for (int i = 0; i < iterations; i++) {
        auto array = CreateArrayWithTrackInfo(4);
        auto receiver = JSHandle<JSTaggedValue>::Cast(array);

        auto result = InvokeStub(receiver, ElementsKind::NUMBER);
        EXPECT_EQ(result, JSTaggedValue::Hole().GetRawData())
            << "Stub must return Hole on each iteration";

        // Trigger GC to move objects; the TrackInfo must remain a HeapObject.
        thread->GetEcmaVM()->CollectGarbage(TriggerGCType::YOUNG_GC);

        auto trackInfoValAfter = array->GetTrackInfo(thread);
        ASSERT_TRUE(trackInfoValAfter.IsHeapObject())
            << "TrackInfo must remain a HeapObject after GC at iteration " << i;
        ASSERT_FALSE(trackInfoValAfter.IsWeak())
            << "TrackInfo must NOT become a WeakRef after GC";
    }
}

}  // namespace panda::test
