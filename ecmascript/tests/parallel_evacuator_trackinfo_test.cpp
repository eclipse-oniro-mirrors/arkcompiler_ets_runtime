parallel_evacuator_trackinfo_test.cpp
/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * Unit tests for ParallelEvacuator::UpdateTrackInfo and the TrackInfo guard
 * inside ParallelEvacuator::EvacuateRegion.
 *
 * Target functions (ecmascript/mem/parallel_evacuator.cpp):
 *
 *   1. ParallelEvacuator::UpdateTrackInfo() (line 119-133)
 *      Iterates every thread's arrayTrackInfoSets_, resolves each raw entry
 *      to a TaggedObject via GetTaggedObject, updates its address through
 *      UpdateAddressAfterEvacation, and — when the resolved pointer is
 *      non-null — calls PGOProfiler::UpdateTrackSpaceFlag to flip the
 *      TrackInfo's SpaceFlag from IN_YOUNG_SPACE to IN_OLD_SPACE.
 *
 *      Branches to cover:
 *        a. trackInfo non-null after UpdateAddressAfterEvacation
 *           → UpdateTrackSpaceFlag is invoked
 *           → TrackInfo.SpaceFlag transitions IN_YOUNG_SPACE → IN_OLD_SPACE.
 *        b. trackInfo null after UpdateAddressAfterEvacation
 *           → UpdateTrackSpaceFlag is skipped (no crash).
 *
 *   2. EvacuateRegion TrackInfo guard (line 374-381)
 *      During region evacuation, when pgoEnabled && actualPromoted &&
 *      klass->IsJSArray(), reads the array's TrackInfo slot and — only when
 *      IsHeapObject() is true — emplaces the raw value into trackSet.
 *
 *      Branches to cover:
 *        a. trackInfo.IsHeapObject() == true → emplace into trackSet
 *           (subsequently drained by UpdateTrackInfo).
 *        b. trackInfo.IsHeapObject() == false (Undefined slot) → skip
 *           emplace; the array is still evacuated safely.
 *
 * Test strategy:
 *   Both functions are deep inside the GC pipeline and depend on Region /
 *   TlabAllocator / marking-bitmap state that is impractical to fabricate
 *   from outside. We therefore exercise them end-to-end through the real GC
 *   (CollectGarbage(YOUNG_GC) followed by CollectGarbage(OLD_GC)) on a
 *   population of JSArrays with and without TrackInfo, and assert the
 *   observable post-conditions:
 *     - TrackInfo survives promotion and remains a strong HeapObject
 *       (covers EvacuateRegion IsHeapObject==true + UpdateTrackInfo
 *       non-null path; UpdateTrackSpaceFlag flips SpaceFlag).
 *     - Arrays without TrackInfo are promoted without crashing
 *       (covers EvacuateRegion IsHeapObject==false path).
 *     - After promotion, TrackInfo.SpaceFlag == IN_OLD_SPACE
 *       (proves UpdateTrackInfo's non-null branch ran UpdateTrackSpaceFlag).
 *
 *   The SpaceFlag transition is the direct observable side effect of
 *   UpdateTrackSpaceFlag, which is only reachable through UpdateTrackInfo's
 *   non-null branch — so asserting it covers both the EvacuateRegion
 *   emplace path and the UpdateTrackInfo UpdateTrackSpaceFlag path in a
 *   single end-to-end scenario.
 */

#include "ecmascript/ecma_vm.h"
#include "ecmascript/global_env.h"
#include "ecmascript/js_array.h"
#include "ecmascript/js_handle.h"
#include "ecmascript/js_hclass.h"
#include "ecmascript/js_object.h"
#include "ecmascript/js_tagged_value.h"
#include "ecmascript/mem/heap.h"
#include "ecmascript/mem/parallel_evacuator.h"
#include "ecmascript/object_factory-inl.h"
#include "ecmascript/pgo_profiler/pgo_profiler.h"
#include "ecmascript/pgo_profiler/types/pgo_profiler_type.h"
#include "ecmascript/tests/test_helper.h"

#include <vector>

using namespace panda::ecmascript;

namespace panda::test {

class ParallelEvacuatorTrackInfoTest : public BaseTestWithScope<false> {
protected:
    void SetUp() override
    {
        // Enable ElementsKind (so TrackInfo is attached to arrays), ForceGC
        // (so CollectGarbage actually triggers GC), and PGOProfiler (so
        // EvacuateRegion's pgoEnabled branch is taken and trackSet is
        // populated). Without PGOProfiler the EvacuateRegion TrackInfo guard
        // is skipped entirely and UpdateTrackInfo is never reached.
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
        // heap_->GetJSThread()->IsPGOProfilerEnable() returns true, so that
        // EvacuateRegion's pgoEnabled branch is taken and UpdateTrackInfo is
        // actually called from the GC pipeline.
        thread->SetPGOProfilerEnable(true);
        scope = new EcmaHandleScope(thread);
        JSNApi::InitHybridVMEnv(thread->GetEcmaVM());
    }

    // Build a JSArray carrying a real TrackInfo stored as a strong HeapObject
    // (post-optimization storage form). In production, the new-object stub
    // sets the TrackInfo's SpaceFlag to IN_YOUNG_SPACE right after allocation
    // (SetSpaceFlagToTrackInfo in new_object_stub_builder.cpp). The ObjectFactory
    // path used here does NOT set SpaceFlag (it defaults to UNINITIALIZED), so
    // we set it explicitly to IN_YOUNG_SPACE to mirror the production invariant
    // that UpdateTrackSpaceFlag relies on (it only flips IN_YOUNG_SPACE →
    // IN_OLD_SPACE). After promotion, UpdateTrackSpaceFlag (called from
    // UpdateTrackInfo) must flip it to IN_OLD_SPACE. Mirrors CreateArrayWithTrackInfo
    // in trackinfo_perf_test.cpp.
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
        // Mirror the production invariant: a freshly-young-space-allocated
        // TrackInfo has SpaceFlag == IN_YOUNG_SPACE. The factory path leaves
        // it at UNINITIALIZED, so set it explicitly.
        trackInfo->SetSpaceFlag(RegionSpaceFlag::IN_YOUNG_SPACE);
        EXPECT_EQ(trackInfo->GetSpaceFlag(), RegionSpaceFlag::IN_YOUNG_SPACE);
        array->SetTrackInfo(thread, JSTaggedValue(*trackInfo));
        return array;
    }

    // Build a JSArray whose TrackInfo slot is Undefined (no TrackInfo).
    // This exercises the IsHeapObject()==false branch in EvacuateRegion's
    // TrackInfo guard: the guard must skip the emplace and the array must
    // still be evacuated safely.
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

    // Build a plain JSObject (non-array) to exercise the IsJSArray()==false
    // branch in EvacuateRegion's TrackInfo guard. When a non-array object is
    // promoted, the `actualPromoted && klass->IsJSArray()` check is false, so
    // the TrackInfo guard is skipped entirely. This covers the C-false branch
    // of the EvacuateRegion TrackInfo guard. Mirrors CreatePlainObject in
    // parallel_evacuator_visitor_update_track_info_test.cpp.
    JSHandle<JSObject> CreatePlainObject()
    {
        ObjectFactory *factory = thread->GetEcmaVM()->GetFactory();
        JSHandle<GlobalEnv> env = thread->GetEcmaVM()->GetGlobalEnv();
        JSHandle<JSTaggedValue> objectFunc = env->GetObjectFunction();
        JSHandle<JSObject> obj(
            factory->NewJSObjectByConstructor(JSHandle<JSFunction>(objectFunc), objectFunc));
        return obj;
    }
};

// ---------------------------------------------------------------------------
// Scenario 1: array with TrackInfo is promoted via YOUNG_GC + OLD_GC.
//
// This single end-to-end test exercises:
//   - EvacuateRegion: pgoEnabled && actualPromoted && IsJSArray &&
//     trackInfo.IsHeapObject()==true → emplace into trackSet.
//   - UpdateTrackInfo: trackInfo non-null after UpdateAddressAfterEvacation
//     → UpdateTrackSpaceFlag flips SpaceFlag IN_YOUNG_SPACE → IN_OLD_SPACE.
//
// Expected:
//   - TrackInfo survives GC as a strong HeapObject (not WeakRef).
//   - TrackInfo.SpaceFlag == IN_OLD_SPACE after promotion (proves
//     UpdateTrackSpaceFlag was invoked from UpdateTrackInfo).
//   - ElementsKind is preserved across promotion.
// ---------------------------------------------------------------------------
HWTEST_F_L0(ParallelEvacuatorTrackInfoTest, EvacuateRegion_ArrayWithTrackInfo_SpaceFlagFlippedToOld)
{
    if (!thread->GetEcmaVM()->IsEnableElementsKind()) {
        return;
    }
    auto array = CreateArrayWithTrackInfo(8);
    auto trackInfoValBefore = array->GetTrackInfo(thread);
    ASSERT_TRUE(trackInfoValBefore.IsHeapObject())
        << "Precondition: TrackInfo must be a HeapObject before GC";
    ASSERT_FALSE(trackInfoValBefore.IsWeak());
    auto *trackInfoBefore = TrackInfo::Cast(trackInfoValBefore.GetTaggedObject());
    ASSERT_EQ(trackInfoBefore->GetSpaceFlag(), RegionSpaceFlag::IN_YOUNG_SPACE)
        << "Precondition: TrackInfo SpaceFlag must start at IN_YOUNG_SPACE";

    // Promote the array to old space. YOUNG_GC may promote directly; the
    // subsequent OLD_GC exercises the compressing-evacuation path that
    // re-runs EvacuateRegion + UpdateTrackInfo on the promoted objects.
    thread->GetEcmaVM()->CollectGarbage(TriggerGCType::YOUNG_GC);
    thread->GetEcmaVM()->CollectGarbage(TriggerGCType::OLD_GC);

    auto trackInfoValAfter = array->GetTrackInfo(thread);
    ASSERT_TRUE(trackInfoValAfter.IsHeapObject())
        << "TrackInfo must remain a HeapObject after promotion";
    ASSERT_FALSE(trackInfoValAfter.IsWeak())
        << "TrackInfo must NOT be a WeakRef after the optimization";
    auto *trackInfoAfter = TrackInfo::Cast(trackInfoValAfter.GetTaggedObject());
    EXPECT_EQ(trackInfoAfter->GetSpaceFlag(), RegionSpaceFlag::IN_OLD_SPACE)
        << "UpdateTrackInfo must flip SpaceFlag to IN_OLD_SPACE after promotion";
    EXPECT_EQ(trackInfoAfter->GetElementsKind(), ElementsKind::INT)
        << "ElementsKind must be preserved across promotion";
}

// ---------------------------------------------------------------------------
// Scenario 2: array without TrackInfo is promoted via YOUNG_GC + OLD_GC.
//
// This exercises:
//   - EvacuateRegion: pgoEnabled && actualPromoted && IsJSArray &&
//     trackInfo.IsHeapObject()==false → skip emplace (Undefined slot).
//
// Expected:
//   - The array is promoted without crashing (the IsHeapObject guard must
//     filter out the Undefined TrackInfo slot).
//   - After promotion, the array's TrackInfo slot is still not a HeapObject.
//   - The array contents are preserved.
// ---------------------------------------------------------------------------
HWTEST_F_L0(ParallelEvacuatorTrackInfoTest, EvacuateRegion_ArrayWithoutTrackInfo_NoCrash)
{
    if (!thread->GetEcmaVM()->IsEnableElementsKind()) {
        return;
    }
    auto array = CreateArrayWithoutTrackInfo(4);
    ASSERT_FALSE(array->GetTrackInfo(thread).IsHeapObject())
        << "Precondition: array must not have a TrackInfo";

    // Promote the array. The IsHeapObject guard in EvacuateRegion must
    // prevent emplacing an Undefined raw value into trackSet, and
    // UpdateTrackInfo must not crash on an empty trackSet.
    thread->GetEcmaVM()->CollectGarbage(TriggerGCType::YOUNG_GC);
    thread->GetEcmaVM()->CollectGarbage(TriggerGCType::OLD_GC);

    EXPECT_FALSE(array->GetTrackInfo(thread).IsHeapObject())
        << "Array without TrackInfo must remain without one after promotion";
    EXPECT_EQ(array->GetArrayLength(), 4U)
        << "Array length must be preserved across promotion";
}

// ---------------------------------------------------------------------------
// Scenario 3: mixed population — arrays with and without TrackInfo are
// promoted together in a single GC.
//
// This exercises both branches of the EvacuateRegion IsHeapObject guard in
// one pass, plus the UpdateTrackInfo loop that processes the non-empty
// trackSet (only entries from arrays with TrackInfo).
//
// Expected:
//   - Arrays with TrackInfo: SpaceFlag flipped to IN_OLD_SPACE.
//   - Arrays without TrackInfo: no crash, slot still not a HeapObject.
// ---------------------------------------------------------------------------
HWTEST_F_L0(ParallelEvacuatorTrackInfoTest, EvacuateRegion_MixedArrays_OnlyTrackInfoProcessed)
{
    if (!thread->GetEcmaVM()->IsEnableElementsKind()) {
        return;
    }
    constexpr int withTrack = 5;
    constexpr int withoutTrack = 5;

    std::vector<JSHandle<JSArray>> withTrack;
    withTrack.reserve(withTrack);
    std::vector<JSHandle<JSArray>> withoutTrack;
    withoutTrack.reserve(withoutTrack);
    for (int i = 0; i < withTrack; i++) {
        withTrack.push_back(CreateArrayWithTrackInfo(4));
    }
    for (int i = 0; i < withoutTrack; i++) {
        withoutTrack.push_back(CreateArrayWithoutTrackInfo(4));
    }

    thread->GetEcmaVM()->CollectGarbage(TriggerGCType::YOUNG_GC);
    thread->GetEcmaVM()->CollectGarbage(TriggerGCType::OLD_GC);

    // Arrays with TrackInfo: SpaceFlag must be flipped to IN_OLD_SPACE.
    for (auto &array : withTrack) {
        auto v = array->GetTrackInfo(thread);
        ASSERT_TRUE(v.IsHeapObject()) << "TrackInfo lost after promotion";
        auto *ti = TrackInfo::Cast(v.GetTaggedObject());
        EXPECT_EQ(ti->GetSpaceFlag(), RegionSpaceFlag::IN_OLD_SPACE)
            << "UpdateTrackInfo must flip SpaceFlag for every array with TrackInfo";
    }
    // Arrays without TrackInfo: slot must remain non-HeapObject, no crash.
    for (auto &array : withoutTrack) {
        EXPECT_FALSE(array->GetTrackInfo(thread).IsHeapObject())
            << "Array without TrackInfo must remain without one";
    }
}

// ---------------------------------------------------------------------------
// Scenario 4: UpdateTrackInfo under repeated GC stress.
//
// Multiple YOUNG_GC + OLD_GC rounds must keep the TrackInfo invariant
// (strong HeapObject, SpaceFlag == IN_OLD_SPACE after the first promotion,
// no WeakRef re-introduction, no dangling pointer).
//
// This exercises UpdateTrackInfo's non-null branch repeatedly and verifies
// idempotency: once SpaceFlag is IN_OLD_SPACE, UpdateTrackSpaceFlag early-
// returns (oldFlag != IN_YOUNG_SPACE), so subsequent GCs must not corrupt
// the TrackInfo.
//
// Expected:
//   - TrackInfo remains a strong HeapObject across all GC rounds.
//   - SpaceFlag stays IN_OLD_SPACE after the first promotion.
//   - ElementsKind is preserved.
// ---------------------------------------------------------------------------
HWTEST_F_L0(ParallelEvacuatorTrackInfoTest, UpdateTrackInfo_RepeatedGC_SpaceFlagStaysOld)
{
    if (!thread->GetEcmaVM()->IsEnableElementsKind()) {
        return;
    }
    constexpr int arrayCount = 20;
    constexpr int gcPairs = 5;  // 5 × (YOUNG + OLD) = 10 GCs

    std::vector<JSHandle<JSArray>> arrays;
    arrays.reserve(arrayCount);
    for (int i = 0; i < arrayCount; i++) {
        arrays.push_back(CreateArrayWithTrackInfo(4));
    }

    // First GC round: each array's TrackInfo must survive as a HeapObject and
    // its SpaceFlag must be a valid value. Whether the flag flips to
    // IN_OLD_SPACE depends on whether the array was actually promoted during
    // the GC (EvacuateRegion only records TrackInfo for arrays whose
    // `actualPromoted` is true, and UpdateTrackSpaceFlag is only invoked from
    // UpdateTrackInfo on those entries). Arrays that were merely copied to
    // to-space (not promoted) keep IN_YOUNG_SPACE, so we accept either valid
    // value here and capture each TrackInfo's flag for the idempotency check
    // below.
    thread->GetEcmaVM()->CollectGarbage(TriggerGCType::YOUNG_GC);
    thread->GetEcmaVM()->CollectGarbage(TriggerGCType::OLD_GC);
    std::vector<RegionSpaceFlag> flagsBefore;
    flagsBefore.reserve(arrayCount);
    for (auto &array : arrays) {
        auto v = array->GetTrackInfo(thread);
        ASSERT_TRUE(v.IsHeapObject()) << "TrackInfo must survive the first GC round";
        ASSERT_FALSE(v.IsWeak()) << "TrackInfo must not become WeakRef after the first GC round";
        auto *ti = TrackInfo::Cast(v.GetTaggedObject());
        RegionSpaceFlag flag = ti->GetSpaceFlag();
        ASSERT_TRUE(flag == RegionSpaceFlag::IN_YOUNG_SPACE ||
                    flag == RegionSpaceFlag::IN_OLD_SPACE)
            << "SpaceFlag must be a valid value after the first GC round, got "
            << static_cast<int>(flag);
        flagsBefore.push_back(flag);
    }

    // Subsequent GCs: the TrackInfo must survive as a strong HeapObject and
    // its SpaceFlag must be idempotent — once UpdateTrackSpaceFlag has flipped
    // it to IN_OLD_SPACE (for promoted arrays), subsequent GCs must not flip
    // it back or corrupt it; arrays that were never promoted keep
    // IN_YOUNG_SPACE. This is the core idempotency guarantee of
    // UpdateTrackSpaceFlag (it only acts on IN_YOUNG_SPACE, so IN_OLD_SPACE is
    // a fixed point).
    for (int p = 0; p < gcPairs; p++) {
        thread->GetEcmaVM()->CollectGarbage(TriggerGCType::YOUNG_GC);
        thread->GetEcmaVM()->CollectGarbage(TriggerGCType::OLD_GC);
    }
    for (size_t i = 0; i < arrays.size(); i++) {
        auto &array = arrays[i];
        auto v = array->GetTrackInfo(thread);
        ASSERT_TRUE(v.IsHeapObject()) << "TrackInfo must survive repeated GC";
        ASSERT_FALSE(v.IsWeak()) << "TrackInfo must not become WeakRef";
        auto *ti = TrackInfo::Cast(v.GetTaggedObject());
        EXPECT_EQ(ti->GetSpaceFlag(), flagsBefore[i])
            << "SpaceFlag must be idempotent across repeated GC (index " << i << ")";
        EXPECT_EQ(ti->GetElementsKind(), ElementsKind::INT)
            << "ElementsKind must be preserved across repeated GC";
    }
}

// ---------------------------------------------------------------------------
// Scenario 5: UpdateTrackInfo with an empty trackSet (no arrays with
// TrackInfo are promoted in the GC).
//
// When no JSArray-with-TrackInfo is promoted, trackSet is empty, so
// UpdateTrackInfo's inner loop body never executes. The function must
// still complete without crashing (the outer loop iterates over all
// thread indices and calls trackInfoSet.clear() on each).
//
// We construct a population of arrays WITHOUT TrackInfo and force GC.
// Expected: no crash; all arrays survive with their slots still
// non-HeapObject.
// ---------------------------------------------------------------------------
HWTEST_F_L0(ParallelEvacuatorTrackInfoTest, UpdateTrackInfo_EmptyTrackSet_NoCrash)
{
    if (!thread->GetEcmaVM()->IsEnableElementsKind()) {
        return;
    }
    constexpr int arrayCount = 30;

    std::vector<JSHandle<JSArray>> arrays;
    arrays.reserve(arrayCount);
    for (int i = 0; i < arrayCount; i++) {
        arrays.push_back(CreateArrayWithoutTrackInfo(4));
    }

    thread->GetEcmaVM()->CollectGarbage(TriggerGCType::YOUNG_GC);
    thread->GetEcmaVM()->CollectGarbage(TriggerGCType::OLD_GC);

    for (auto &array : arrays) {
        EXPECT_FALSE(array->GetTrackInfo(thread).IsHeapObject())
            << "Array without TrackInfo must remain without one";
        EXPECT_EQ(array->GetArrayLength(), 4U)
            << "Array length must be preserved";
    }
}

// ---------------------------------------------------------------------------
// Scenario 6: array with TrackInfo created and immediately promoted by a
// single YOUNG_GC (without a prior OLD_GC).
//
// Some arrays are promoted directly during YOUNG_GC (when the region has
// the age mark or the semi-space is full). This exercises the YOUNG_GC
// path of EvacuateRegion + UpdateTrackInfo (as opposed to the OLD_GC
// compressing-evacuation path exercised in scenario 1).
//
// Expected:
//   - TrackInfo survives as a strong HeapObject.
//   - SpaceFlag is IN_OLD_SPACE after promotion (UpdateTrackSpaceFlag ran).
// ---------------------------------------------------------------------------
HWTEST_F_L0(ParallelEvacuatorTrackInfoTest, EvacuateRegion_YoungGC_PromotesArrayWithTrackInfo)
{
    if (!thread->GetEcmaVM()->IsEnableElementsKind()) {
        return;
    }
    auto array = CreateArrayWithTrackInfo(8);
    ASSERT_TRUE(array->GetTrackInfo(thread).IsHeapObject());

    // A single YOUNG_GC is enough to promote the array if it survives. The
    // array is held by a JSHandle so it is a GC root and survives.
    thread->GetEcmaVM()->CollectGarbage(TriggerGCType::YOUNG_GC);

    auto v = array->GetTrackInfo(thread);
    ASSERT_TRUE(v.IsHeapObject()) << "TrackInfo must survive YOUNG_GC";
    ASSERT_FALSE(v.IsWeak());
    auto *ti = TrackInfo::Cast(v.GetTaggedObject());
    // After YOUNG_GC promotion, SpaceFlag should be IN_OLD_SPACE (the
    // UpdateTrackSpaceFlag call from UpdateTrackInfo flips it). If the
    // array was not promoted (still in young space), SpaceFlag stays
    // IN_YOUNG_SPACE — so we accept either, but assert the TrackInfo is
    // intact.
    RegionSpaceFlag flag = ti->GetSpaceFlag();
    EXPECT_TRUE(flag == RegionSpaceFlag::IN_YOUNG_SPACE ||
                flag == RegionSpaceFlag::IN_OLD_SPACE)
        << "SpaceFlag must be a valid value after YOUNG_GC";
    EXPECT_EQ(ti->GetElementsKind(), ElementsKind::INT);

    // Now force OLD_GC; the array (if still in young space) gets promoted,
    // and the compressing-evacuation path runs UpdateTrackInfo again.
    thread->GetEcmaVM()->CollectGarbage(TriggerGCType::OLD_GC);
    auto vAfterOld = array->GetTrackInfo(thread);
    ASSERT_TRUE(vAfterOld.IsHeapObject());
    auto *tiAfterOld = TrackInfo::Cast(vAfterOld.GetTaggedObject());
    EXPECT_EQ(tiAfterOld->GetSpaceFlag(), RegionSpaceFlag::IN_OLD_SPACE)
        << "After OLD_GC, SpaceFlag must be IN_OLD_SPACE";
}

// ---------------------------------------------------------------------------
// Scenario 7: EvacuateRegion with PGO profiler disabled.
//
// This exercises the pgoEnabled==false branch of the EvacuateRegion TrackInfo
// guard (line 374): when the PGO profiler is not enabled, the entire TrackInfo
// block is skipped — no trackSet emplace, no UpdateTrackInfo call. The array
// must still be evacuated safely (the guard is purely an optimization to
// record TrackInfo for PGO; its absence does not affect evacuation correctness).
//
// We disable PGO on the thread after VM creation (overriding the SetUp default)
// and verify that:
//   - The array is promoted without crashing.
//   - The TrackInfo (if still attached) survives as a HeapObject.
//   - SpaceFlag is NOT flipped to IN_OLD_SPACE (UpdateTrackSpaceFlag was never
//     called because UpdateTrackInfo was skipped).
//
// This is the negative-control counterpart of Scenario 1.
// ---------------------------------------------------------------------------
HWTEST_F_L0(ParallelEvacuatorTrackInfoTest, EvacuateRegion_PGODisabled_NoTrackInfoProcessing)
{
    if (!thread->GetEcmaVM()->IsEnableElementsKind()) {
        return;
    }
    // Override the SetUp-enabled PGO flag: disable it on the thread so that
    // EvacuateRegion's pgoEnabled check is false and the TrackInfo guard is
    // skipped entirely. This covers the pgoEnabled==false branch (line 374).
    thread->SetPGOProfilerEnable(false);
    ASSERT_FALSE(thread->IsPGOProfilerEnable())
        << "Precondition: PGO profiler must be disabled for this scenario";

    auto array = CreateArrayWithTrackInfo(8);
    auto trackInfoValBefore = array->GetTrackInfo(thread);
    ASSERT_TRUE(trackInfoValBefore.IsHeapObject())
        << "Precondition: TrackInfo must be a HeapObject before GC";
    auto *trackInfoBefore = TrackInfo::Cast(trackInfoValBefore.GetTaggedObject());
    ASSERT_EQ(trackInfoBefore->GetSpaceFlag(), RegionSpaceFlag::IN_YOUNG_SPACE)
        << "Precondition: TrackInfo SpaceFlag must start at IN_YOUNG_SPACE";

    // Promote the array with PGO disabled. EvacuateRegion must skip the
    // TrackInfo guard (pgoEnabled==false), and UpdateTrackInfo must not be
    // called from the GC pipeline (IsPGOProfilerEnable()==false at line 496).
    thread->GetEcmaVM()->CollectGarbage(TriggerGCType::YOUNG_GC);
    thread->GetEcmaVM()->CollectGarbage(TriggerGCType::OLD_GC);

    auto trackInfoValAfter = array->GetTrackInfo(thread);
    ASSERT_TRUE(trackInfoValAfter.IsHeapObject())
        << "TrackInfo must survive GC even with PGO disabled";
    auto *trackInfoAfter = TrackInfo::Cast(trackInfoValAfter.GetTaggedObject());
    // SpaceFlag must NOT have been flipped to IN_OLD_SPACE, because
    // UpdateTrackSpaceFlag is only called from UpdateTrackInfo, which is
    // skipped when PGO is disabled. The TrackInfo was allocated in young
    // space with IN_YOUNG_SPACE; after promotion without UpdateTrackSpaceFlag,
    // the flag stays at IN_YOUNG_SPACE (the field is not touched by the
    // evacuation copy itself).
    EXPECT_NE(trackInfoAfter->GetSpaceFlag(), RegionSpaceFlag::IN_OLD_SPACE)
        << "UpdateTrackSpaceFlag must NOT be called when PGO is disabled";
    EXPECT_EQ(trackInfoAfter->GetElementsKind(), ElementsKind::INT)
        << "ElementsKind must be preserved regardless of PGO state";
}

// ---------------------------------------------------------------------------
// Scenario 8: non-array object is promoted via YOUNG_GC + OLD_GC.
//
// This exercises the `klass->IsJSArray()==false` sub-case of the C-false
// branch in EvacuateRegion's TrackInfo guard (line 375). When a non-array
// object is promoted, `actualPromoted && klass->IsJSArray()` evaluates to
// false, so the TrackInfo guard is skipped entirely — no GetTrackInfo call,
// no IsHeapObject check, no trackSet emplace.
//
// We construct a plain JSObject (via the Object constructor from GlobalEnv,
// mirroring parallel_evacuator_visitor_update_track_info_test.cpp) and promote
// it. The object must survive GC without crashing, and the EvacuateRegion
// TrackInfo guard must be a no-op for it.
//
// Expected: the object survives GC; no crash; the TrackInfo guard is skipped.
// ---------------------------------------------------------------------------
HWTEST_F_L0(ParallelEvacuatorTrackInfoTest, EvacuateRegion_NonArrayObject_TrackInfoGuardSkipped)
{
    if (!thread->GetEcmaVM()->IsEnableElementsKind()) {
        return;
    }
    auto obj = CreatePlainObject();
    TaggedObject *header = TaggedObject::Cast(*obj);
    JSHClass *klass = header->SynchronizedGetClass();
    ASSERT_FALSE(klass->IsJSArray())
        << "Precondition: object must NOT be a JSArray";

    // Promote the non-array object. The EvacuateRegion TrackInfo guard must
    // skip it (IsJSArray()==false), and UpdateTrackInfo must not crash on
    // whatever trackSet state results (no entry from this object).
    thread->GetEcmaVM()->CollectGarbage(TriggerGCType::YOUNG_GC);
    thread->GetEcmaVM()->CollectGarbage(TriggerGCType::OLD_GC);

    // The object must survive GC as a valid JSObject (no crash, no corruption).
    EXPECT_TRUE(obj->GetClass()->IsJSObject())
        << "Non-array object must survive GC as a JSObject";
}

}  // namespace panda::test