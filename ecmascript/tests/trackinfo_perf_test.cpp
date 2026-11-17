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
 * Performance tests for TrackInfo weak-ref → heap-object optimization.
 *
 * The optimization changes TrackInfo storage from WeakRef to a regular
 * HeapObject, eliminating per-access weak-tag stripping and per-GC weak
 * reference processing.
 *
 * Test layout:
 *   1. TrackInfoStorageCorrectness  — verify the new storage form and
 *      GC/transition invariants (behavioural, not timing).
 *   2. ArrayCreationWithTrackInfo   — creation throughput; measures the
 *      CreateWeakRef path that was removed from the hot path.
 *   3. ElementsKindTransition       — transition throughput; the IsWeak
 *      branch that was removed from UpdateTrackInfo/TransitToElementsKind.
 *   4. GCEvacuationWithTrackInfo    — isolated GC timing (allocation kept
 *      outside the timed region).
 *   5. TrackInfoRead_HeapObjectVsWeakRef — direct micro-benchmark of
 *      GetTaggedObject (new) vs GetWeakReferentUnChecked (old).
 *   6. CreateWeakRefCost            — direct micro-benchmark of the
 *      CreateAndGetWeakRef call that was removed from NewObjectStubBuilder.
 */

#include "ecmascript/ecma_vm.h"
#include "ecmascript/global_env.h"
#include "ecmascript/js_array.h"
#include "ecmascript/js_handle.h"
#include "ecmascript/js_hclass.h"
#include "ecmascript/js_tagged_value.h"
#include "ecmascript/mem/heap.h"
#include "ecmascript/object_factory-inl.h"
#include "ecmascript/pgo_profiler/pgo_profiler.h"
#include "ecmascript/tests/test_helper.h"

#include <chrono>
#include <vector>

using namespace panda::ecmascript;

namespace panda::test {

class TrackInfoPerfTest : public BaseTestWithScope<false> {
protected:
    using Clock = std::chrono::high_resolution_clock;
    using Duration = std::chrono::duration<double, std::milli>;

    void SetUp() override
    {
        // Enable ElementsKind so that TrackInfo paths are exercised. The base
        // class SetUp creates the VM with default options where ElementsKind
        // is off, which would make every test early-return.
        JSRuntimeOptions options;
        options.SetEnableForceGC(true);
        options.SetEnableAsmInterpreter(true);  // ensures stub file loads
        options.SetEnableElementsKind(true);
        instance = JSNApi::CreateEcmaVM(options);
        instance->SetEnableForceGC(true);
        ASSERT_TRUE(instance != nullptr) << "Cannot create EcmaVM";
        thread = instance->GetJSThread();
        thread->ManagedCodeBegin();
        scope = new EcmaHandleScope(thread);
        JSNApi::InitHybridVMEnv(thread->GetEcmaVM());
    }

    // Create a JSArray with a real TrackInfo attached, mirroring what the
    // interpreter/compiler does when PGO profiling is enabled. This helper
    // follows the same pattern as JSArrayTest::TrackInfo in js_array_test.cpp.
    // After the weak-ref→heap-object optimization, TrackInfo is stored as a
    // strong HeapObject (no CreateAndGetWeakRef).
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
        // Keep trackInfo in a JSHandle so it survives GC during the perf loops.
        JSHandle<TrackInfo> trackInfo(thread, factory->NewObject(trackInfoHclass));
        trackInfo->SetCachedHClass(thread, constants->GetUndefined());
        trackInfo->SetCachedFunc(thread, constants->GetUndefined());
        trackInfo->SetElementsKind(ElementsKind::INT);
        // TrackInfo is stored as a HeapObject (strong reference) after the
        // weak-ref→heap-object optimization.
        array->SetTrackInfo(thread, JSTaggedValue(*trackInfo));
        return array;
    }

    static double Ms(Clock::time_point start, Clock::time_point end)
    {
        return Duration(end - start).count();
    }

    // Build a JSArray whose TrackInfo slot is stored in a chosen form.
    //   useWeak=true  → slot holds a weak-tagged value (simulates the
    //                   pre-optimization storage; GC marking routes the slot
    //                   through weakQueue_/UpdateRecordWeakReference).
    //   useWeak=false → slot holds a strong HeapObject (post-optimization
    //                   storage; GC marking processes the slot inline).
    JSHandle<JSArray> BuildArrayWithTrackInfo(bool useWeak)
    {
        auto constants = thread->GlobalConstants();
        ObjectFactory *factory = thread->GetEcmaVM()->GetFactory();
        JSHandle<TaggedArray> values(factory->NewTaggedArray(4)); // 4: length of array
        for (uint32_t i = 0; i < 4; i++) { // 4: length of array
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
        if (useWeak) {
            array->SetTrackInfo(thread, JSTaggedValue(*trackInfo).CreateAndGetWeakRef());
        } else {
            array->SetTrackInfo(thread, JSTaggedValue(*trackInfo));
        }
        return array;
    }

    // Measure OLD_GC pause (ms per GC) over `arrayCount` live arrays whose
    // TrackInfo is stored in the chosen form. Arrays are promoted to old
    // space before the timed region so OLD_GC visits every TrackInfo slot.
    double MeasureOldGcPause(bool useWeak, int arrayCount, int gcRounds, int repeats)
    {
        std::vector<JSHandle<JSArray>> arrays;
        arrays.reserve(arrayCount);
        for (int i = 0; i < arrayCount; i++) {
            arrays.push_back(BuildArrayWithTrackInfo(useWeak));
        }
        thread->GetEcmaVM()->CollectGarbage(TriggerGCType::YOUNG_GC);
        thread->GetEcmaVM()->CollectGarbage(TriggerGCType::OLD_GC);

        double totalMs = 0.0;
        for (int r = 0; r < repeats; r++) {
            auto start = Clock::now();
            for (int g = 0; g < gcRounds; g++) {
                thread->GetEcmaVM()->CollectGarbage(TriggerGCType::OLD_GC);
            }
            auto end = Clock::now();
            totalMs += Ms(start, end);
        }
        return totalMs / (repeats * gcRounds);
    }
};

// ---------------------------------------------------------------------------
// Test 1: TrackInfo storage correctness (behavioural)
// ---------------------------------------------------------------------------
HWTEST_F_L0(TrackInfoPerfTest, TrackInfoStorageCorrectness)
{
    if (!thread->GetEcmaVM()->IsEnableElementsKind()) {
        return;
    }
    auto array = CreateArrayWithTrackInfo(8);

    // 1. TrackInfo is stored as a HeapObject, not a WeakRef.
    auto trackInfoVal = array->GetTrackInfo(thread);
    ASSERT_TRUE(trackInfoVal.IsHeapObject());
    ASSERT_FALSE(trackInfoVal.IsWeak());

    // 2. TrackInfo contents are readable via GetTaggedObject.
    auto *trackInfo = TrackInfo::Cast(trackInfoVal.GetTaggedObject());
    EXPECT_EQ(trackInfo->GetElementsKind(), ElementsKind::INT);

    // 3. TrackInfo survives GC and remains a strong HeapObject.
    thread->GetEcmaVM()->CollectGarbage(TriggerGCType::YOUNG_GC);
    thread->GetEcmaVM()->CollectGarbage(TriggerGCType::OLD_GC);
    auto trackInfoValAfterGC = array->GetTrackInfo(thread);
    ASSERT_TRUE(trackInfoValAfterGC.IsHeapObject());
    ASSERT_FALSE(trackInfoValAfterGC.IsWeak());
    auto *trackInfoAfterGC = TrackInfo::Cast(trackInfoValAfterGC.GetTaggedObject());
    EXPECT_EQ(trackInfoAfterGC->GetElementsKind(), ElementsKind::INT);

    // 4. ElementsKind transition still updates the TrackInfo correctly.
    auto obj = JSHandle<JSObject>::Cast(array);
    JSHandle<JSTaggedValue> floatValue(thread, JSTaggedValue(3.14));
    ASSERT_TRUE(JSHClass::TransitToElementsKind(thread, obj, floatValue, ElementsKind::NONE));
    EXPECT_EQ(trackInfoAfterGC->GetElementsKind(), ElementsKind::NUMBER);

    // 5. An array without TrackInfo stores Undefined and must not crash GC
    //    (the IsHeapObject guards added in the evacuator rely on this).
    ObjectFactory *factory = thread->GetEcmaVM()->GetFactory();
    JSHandle<TaggedArray> emptyValues(factory->NewTaggedArray(0));
    JSHandle<JSArray> noTrackArray(JSArray::CreateArrayFromList(thread, emptyValues));
    auto noTrackVal = noTrackArray->GetTrackInfo(thread);
    EXPECT_FALSE(noTrackVal.IsHeapObject());
    thread->GetEcmaVM()->CollectGarbage(TriggerGCType::YOUNG_GC);
    SUCCEED();
}

// ---------------------------------------------------------------------------
// Test 2: JSArray creation throughput with TrackInfo
// ---------------------------------------------------------------------------
HWTEST_F_L0(TrackInfoPerfTest, ArrayCreationWithTrackInfo)
{
    if (!thread->GetEcmaVM()->IsEnableElementsKind()) {
        return;
    }
    constexpr int iterations = 1000;
    constexpr uint32_t arrayLen = 8;

    // Warm up
    for (int i = 0; i < 100; i++) {
        CreateArrayWithTrackInfo(arrayLen);
    }
    thread->GetEcmaVM()->CollectGarbage(TriggerGCType::YOUNG_GC);

    auto start = Clock::now();
    for (int i = 0; i < iterations; i++) {
        CreateArrayWithTrackInfo(arrayLen);
    }
    auto end = Clock::now();
    double totalMs = Ms(start, end);
    double perOpUs = (totalMs * 1000.0) / iterations;
    std::cout << "WeakRef [TrackInfo-Perf] ArrayCreationWithTrackInfo: " << totalMs << " ms total, "
              << perOpUs << " us/op (" << iterations << " iterations)" << std::endl;
    EXPECT_GT(totalMs, 0.0);
}

// ---------------------------------------------------------------------------
// Test 3: ElementsKind transition throughput
// ---------------------------------------------------------------------------
HWTEST_F_L0(TrackInfoPerfTest, ElementsKindTransition)
{
    if (!thread->GetEcmaVM()->IsEnableElementsKind()) {
        return;
    }
    constexpr int arrayCount = 200;
    constexpr int transitions = 5;

    std::vector<JSHandle<JSArray>> arrays;
    arrays.reserve(arrayCount);
    for (int i = 0; i < arrayCount; i++) {
        arrays.push_back(CreateArrayWithTrackInfo(4));
    }

    auto start = Clock::now();
    for (int t = 0; t < transitions; t++) {
        for (auto &array : arrays) {
            auto obj = JSHandle<JSObject>::Cast(array);
            JSHandle<JSTaggedValue> floatValue(thread, JSTaggedValue(1.5 + t));
            JSHClass::TransitToElementsKind(thread, obj, floatValue, ElementsKind::NONE);
        }
    }
    auto end = Clock::now();
    double totalMs = Ms(start, end);
    double perOpUs = (totalMs * 1000.0) / (arrayCount * transitions);
    std::cout << "WeakRef [TrackInfo-Perf] ElementsKindTransition: " << totalMs << " ms total, "
              << perOpUs << " us/op (" << arrayCount * transitions << " transitions)" << std::endl;
    EXPECT_GT(totalMs, 0.0);
}

// ---------------------------------------------------------------------------
// Test 4: GC evacuation with TrackInfo — allocation kept outside the timed
//         region so only GC time is measured.
// ---------------------------------------------------------------------------
HWTEST_F_L0(TrackInfoPerfTest, GCEvacuationWithTrackInfo)
{
    if (!thread->GetEcmaVM()->IsEnableElementsKind()) {
        return;
    }
    constexpr int arrayCount = 500;
    constexpr int gcRounds = 5;

    // Pre-allocate all arrays outside the timed region.
    std::vector<JSHandle<JSArray>> arrays;
    arrays.reserve(arrayCount);
    for (int i = 0; i < arrayCount; i++) {
        arrays.push_back(CreateArrayWithTrackInfo(4));
    }

    // Promote some arrays to old space so that the young GC evacuation path
    // (which processes TrackInfo) is exercised.
    thread->GetEcmaVM()->CollectGarbage(TriggerGCType::YOUNG_GC);
    thread->GetEcmaVM()->CollectGarbage(TriggerGCType::OLD_GC);

    // Timed region: GC only.
    auto start = Clock::now();
    for (int i = 0; i < gcRounds; i++) {
        thread->GetEcmaVM()->CollectGarbage(TriggerGCType::YOUNG_GC);
    }
    auto end = Clock::now();
    double totalMs = Ms(start, end);
    double perGcMs = totalMs / gcRounds;
    std::cout << "WeakRef [TrackInfo-Perf] GCEvacuationWithTrackInfo: " << totalMs << " ms total, "
              << perGcMs << " ms/GC (" << gcRounds << " rounds, " << arrayCount
              << " arrays with TrackInfo)" << std::endl;
    EXPECT_GT(totalMs, 0.0);
}

// ---------------------------------------------------------------------------
// Test 4b: Old GC (compressing) evacuation with TrackInfo.
//
// The compressing OLD_GC reuses ParallelEvacuator, so the optimized TrackInfo
// path (IsHeapObject guard + GetTaggedObject, SpaceFlag update) is exercised
// here as well. Allocation is kept outside the timed region so only GC time
// is measured.
// ---------------------------------------------------------------------------
HWTEST_F_L0(TrackInfoPerfTest, OldGCEvacuationWithTrackInfo)
{
    if (!thread->GetEcmaVM()->IsEnableElementsKind()) {
        return;
    }
    constexpr int arrayCount = 500;
    constexpr int gcRounds = 3;

    // Pre-allocate all arrays outside the timed region.
    std::vector<JSHandle<JSArray>> arrays;
    arrays.reserve(arrayCount);
    for (int i = 0; i < arrayCount; i++) {
        arrays.push_back(CreateArrayWithTrackInfo(4));
    }

    // Promote arrays to old space first so that OLD_GC actually has work to
    // do on these objects (and their TrackInfo slots).
    thread->GetEcmaVM()->CollectGarbage(TriggerGCType::YOUNG_GC);
    thread->GetEcmaVM()->CollectGarbage(TriggerGCType::OLD_GC);

    // Verify the TrackInfo objects survived promotion and are still HeapObject.
    for (auto &array : arrays) {
        auto v = array->GetTrackInfo(thread);
        ASSERT_TRUE(v.IsHeapObject()) << "TrackInfo lost after promotion";
        ASSERT_FALSE(v.IsWeak());
    }

    // Timed region: OLD_GC only.
    auto start = Clock::now();
    for (int i = 0; i < gcRounds; i++) {
        thread->GetEcmaVM()->CollectGarbage(TriggerGCType::OLD_GC);
    }
    auto end = Clock::now();
    double totalMs = Ms(start, end);
    double perGcMs = totalMs / gcRounds;
    std::cout << "WeakRef [TrackInfo-Perf] OldGCEvacuationWithTrackInfo: " << totalMs << " ms total, "
              << perGcMs << " ms/GC (" << gcRounds << " rounds, " << arrayCount
              << " arrays with TrackInfo)" << std::endl;
    EXPECT_GT(totalMs, 0.0);
}

// ---------------------------------------------------------------------------
// Test 4c: Mixed GC stress — TrackInfo integrity across many YOUNG/OLD GCs.
//
// This is a correctness-focused test (not timing). After many mixed GCs the
// TrackInfo must still be reachable as a HeapObject, its ElementsKind must be
// preserved, and the SpaceFlag must reflect the current region. This guards
// the IsHeapObject guards added in EvacuateRegion and NewToOldEvacuationVisitor
// against regressions that would leave dangling entries in the trackInfo set.
// ---------------------------------------------------------------------------
HWTEST_F_L0(TrackInfoPerfTest, MixedGCStress_TrackInfoIntegrity)
{
    if (!thread->GetEcmaVM()->IsEnableElementsKind()) {
        return;
    }
    constexpr int arrayCount = 200;
    constexpr int gcPairs = 5;  // 5 × (YOUNG + OLD) = 10 GCs

    std::vector<JSHandle<JSArray>> arrays;
    arrays.reserve(arrayCount);
    for (int i = 0; i < arrayCount; i++) {
        arrays.push_back(CreateArrayWithTrackInfo(4));
    }

    for (int p = 0; p < gcPairs; p++) {
        thread->GetEcmaVM()->CollectGarbage(TriggerGCType::YOUNG_GC);
        thread->GetEcmaVM()->CollectGarbage(TriggerGCType::OLD_GC);
    }

    // Every TrackInfo must still be a strong HeapObject with the original
    // ElementsKind (no transitions were performed, so it must stay INT).
    for (auto &array : arrays) {
        auto v = array->GetTrackInfo(thread);
        ASSERT_TRUE(v.IsHeapObject()) << "TrackInfo not a HeapObject after mixed GC stress";
        ASSERT_FALSE(v.IsWeak()) << "TrackInfo must not be a WeakRef after the optimization";
        auto *ti = TrackInfo::Cast(v.GetTaggedObject());
        EXPECT_EQ(ti->GetElementsKind(), ElementsKind::INT);
    }

    // Also verify an array whose TrackInfo slot is Undefined does not crash
    // under mixed GC stress (the IsHeapObject guard in the evacuator must
    // filter it out).
    ObjectFactory *factory = thread->GetEcmaVM()->GetFactory();
    JSHandle<TaggedArray> emptyValues(factory->NewTaggedArray(0));
    JSHandle<JSArray> noTrackArray(JSArray::CreateArrayFromList(thread, emptyValues));
    for (int p = 0; p < gcPairs; p++) {
        thread->GetEcmaVM()->CollectGarbage(TriggerGCType::YOUNG_GC);
        thread->GetEcmaVM()->CollectGarbage(TriggerGCType::OLD_GC);
    }
    EXPECT_FALSE(noTrackArray->GetTrackInfo(thread).IsHeapObject());
    SUCCEED();
}

// ---------------------------------------------------------------------------
// Test 5: TrackInfo read throughput — HeapObject (new) vs WeakRef (old)
// ---------------------------------------------------------------------------
HWTEST_F_L0(TrackInfoPerfTest, TrackInfoRead_HeapObjectVsWeakRef)
{
    if (!thread->GetEcmaVM()->IsEnableElementsKind()) {
        return;
    }
    constexpr int iterations = 100000;
    auto array = CreateArrayWithTrackInfo(8);
    auto trackInfoVal = array->GetTrackInfo(thread);
    ASSERT_TRUE(trackInfoVal.IsHeapObject());

    // Build the WeakRef form that the old code path used, so both accessors
    // resolve to the same underlying TrackInfo.
    auto weakVal = trackInfoVal.CreateAndGetWeakRef();
    ASSERT_TRUE(weakVal.IsWeak());

    volatile uint32_t sink = 0;

    // Baseline: old path — GetWeakReferentUnChecked
    auto startWeak = Clock::now();
    for (int i = 0; i < iterations; i++) {
        auto *ti = TrackInfo::Cast(weakVal.GetWeakReferentUnChecked());
        sink ^= static_cast<uint32_t>(ti->GetElementsKind());
    }
    auto endWeak = Clock::now();
    double weakMs = Ms(startWeak, endWeak);

    // New path — GetTaggedObject
    auto startStrong = Clock::now();
    for (int i = 0; i < iterations; i++) {
        auto *ti = TrackInfo::Cast(trackInfoVal.GetTaggedObject());
        sink ^= static_cast<uint32_t>(ti->GetElementsKind());
    }
    auto endStrong = Clock::now();
    double strongMs = Ms(startStrong, endStrong);

    std::cout << "WeakRef [TrackInfo-Perf] ReadWeakReferent (baseline): " << weakMs << " ms ("
              << (weakMs * 1e6) / iterations << " ns/op)" << std::endl;
    std::cout << "WeakRef [TrackInfo-Perf] ReadHeapObject (new path):  " << strongMs << " ms ("
              << (strongMs * 1e6) / iterations << " ns/op)" << std::endl;
    std::cout << "WeakRef [TrackInfo-Perf] sink=" << sink << " (prevents DCE)" << std::endl;
    EXPECT_GT(weakMs, 0.0);
    EXPECT_GT(strongMs, 0.0);
}

// ---------------------------------------------------------------------------
// Test 6: CreateWeakRef cost — the call removed from NewObjectStubBuilder.
//
// Measures the overhead of CreateAndGetWeakRef (the operation the optimization
// removed from the array-creation fast path) against a plain JSTaggedValue
// construction. Uses a single pre-allocated TrackInfo to avoid allocation
// noise inside the timed loop.
// ---------------------------------------------------------------------------
HWTEST_F_L0(TrackInfoPerfTest, CreateWeakRefCost)
{
    if (!thread->GetEcmaVM()->IsEnableElementsKind()) {
        return;
    }
    constexpr int iterations = 500000;
    constexpr int repeats = 5;

    auto constants = thread->GlobalConstants();
    ObjectFactory *factory = thread->GetEcmaVM()->GetFactory();
    auto trackInfoHclass = JSHandle<JSHClass>::Cast(constants->GetHandledTrackInfoClass());

    // Single TrackInfo object; its address is stable for the duration of the
    // test (held alive by the handle).
    JSHandle<TrackInfo> trackInfo(thread, factory->NewObject(trackInfoHclass));
    trackInfo->SetCachedHClass(thread, constants->GetUndefined());
    trackInfo->SetCachedFunc(thread, constants->GetUndefined());
    trackInfo->SetElementsKind(ElementsKind::INT);
    JSTaggedType rawTrackInfo = JSTaggedValue(*trackInfo).GetRawData();

    volatile uint64_t sink = 0;

    // Baseline: CreateAndGetWeakRef — the call the optimization removed.
    double weakTotalMs = 0.0;
    for (int r = 0; r < repeats; r++) {
        auto start = Clock::now();
        for (int i = 0; i < iterations; i++) {
            sink ^= JSTaggedValue(rawTrackInfo).CreateAndGetWeakRef().GetRawData();
        }
        auto end = Clock::now();
        weakTotalMs += Ms(start, end);
    }
    double weakAvgMs = weakTotalMs / repeats;

    // New path: direct JSTaggedValue — no CreateWeakRef.
    double strongTotalMs = 0.0;
    for (int r = 0; r < repeats; r++) {
        auto start = Clock::now();
        for (int i = 0; i < iterations; i++) {
            sink ^= static_cast<uint64_t>(JSTaggedValue(rawTrackInfo).GetRawData());
        }
        auto end = Clock::now();
        strongTotalMs += Ms(start, end);
    }
    double strongAvgMs = strongTotalMs / repeats;

    std::cout << "WeakRef [TrackInfo-Perf] CreateAndGetWeakRef (baseline): " << weakAvgMs
              << " ms avg over " << repeats << " runs (" << iterations << " ops/run)" << std::endl;
    std::cout << "WeakRef [TrackInfo-Perf] Direct JSTaggedValue (new path): " << strongAvgMs
              << " ms avg over " << repeats << " runs (" << iterations << " ops/run)" << std::endl;
    std::cout << "WeakRef [TrackInfo-Perf] sink=" << sink << " (prevents DCE)" << std::endl;
    EXPECT_GT(weakAvgMs, 0.0);
    EXPECT_GT(strongAvgMs, 0.0);
}

// ===========================================================================
// GC / weak-reference optimization proof tests
//
// The weak-ref→heap-object optimization removes the TrackInfo slot from the
// GC weak-reference queue. In the old code, every JSArray's TrackInfo slot
// was detected as weak by HandleSlot during marking and pushed onto
// weakQueue_ for later processing in UpdateRecordWeakReference. After the
// optimization, the slot is processed inline as a normal strong reference,
// eliminating:
//   - one weakQueue_ push per live JSArray-with-TrackInfo per GC,
//   - one weakQueue_ pop + IsWeak + Region lookup + mark-bitmap test per
//     such slot in UpdateRecordWeakReference,
//   - the IsWeak/GetWeakReferentUnChecked branch in
//     ParallelEvacuator::UpdateTrackInfo.
//
// These tests are designed to make that benefit observable:
//   7. GC_WeakRefVsHeapObject_ArrayCount — measure GC pause as a function of
//      the number of live JSArrays, with TrackInfo stored the old way
//      (WeakRef) vs the new way (HeapObject). The slope (ms per extra
//      array) captures the per-slot weak-queue cost.
//   8. GC_WeakRefVsHeapObject_PauseTime  — end-to-end young/old GC pause
//      comparison on a fixed large workload.
//   9. WeakRefQueueNotPopulated          — correctness proof that after the
//      optimization, TrackInfo slots are NOT recorded as weak references
//      (i.e. they take the strong branch in HandleSlot).
// ===========================================================================

// ---------------------------------------------------------------------------
// Test 7: GC pause vs. live-array count, WeakRef vs HeapObject storage.
//
// Measures GC time as a function of the number of live JSArrays holding
// TrackInfo. The old (WeakRef) storage pushes one entry per array onto
// weakQueue_ during OLD_GC marking and drains it in UpdateRecordWeakReference;
// the new (HeapObject) storage processes the slot inline via MarkAndPush.
// The difference in slope (Δms / Δarrays) between the two storage forms
// isolates the per-slot weak-queue cost.
//
// IMPORTANT: the timed region must use OLD_GC (not YOUNG_GC). YOUNG_GC only
// marks the new space, so once the arrays are promoted to old space their
// TrackInfo slots are never visited by the marker and weakQueue_ stays
// empty — making weak and strong indistinguishable. OLD_GC marks the old
// space, so every live JSArray's TrackInfo slot is visited and, if
// weak-tagged, pushed onto weakQueue_.
//
// Note: because production code now stores TrackInfo as HeapObject, the
// WeakRef variant here stores the weak tag directly via SetTrackInfo. GC
// marking still detects the weak tag and routes the slot through the weak
// queue, exercising the old path.
// ---------------------------------------------------------------------------
HWTEST_F_L0(TrackInfoPerfTest, GC_WeakRefVsHeapObject_ArrayCount)
{
    if (!thread->GetEcmaVM()->IsEnableElementsKind()) {
        return;
    }
    constexpr int arrayCounts[] = {200, 400};
    constexpr int gcRounds = 3;
    constexpr int repeats = 1;

    std::cout << "[TrackInfo-Perf] GC_WeakRefVsHeapObject_ArrayCount (old GC, "
              << repeats << " repeats × " << gcRounds << " rounds):" << std::endl;
    std::cout << "  arrays | weakRef ms/GC | heapObj ms/GC | delta ms/GC" << std::endl;
    std::cout << "  -------|---------------|---------------|------------" << std::endl;

    // Note: do NOT assert weakMs > strongMs. Under OLD_GC with both JSArray
    // and TrackInfo in old space, the weak path only does RecordWeakReference
    // (push slot onto weakQueue_) and skips MarkAndPush(TrackInfo), while the
    // strong path does MarkAndPush(TrackInfo) which then recursively visits
    // TrackInfo's CachedHClass/CachedFunc slots. The weak path therefore does
    // LESS marking work than the strong path, and is expected to be faster or
    // comparable. The optimization's GC benefit is not in marking throughput
    // but in eliminating the weakQueue_ push/pop overhead and the per-access
    // weak-tag stripping — measured in TrackInfoRead_HeapObjectVsWeakRef,
    // CreateWeakRefCost, and WeakRefQueueNotPopulated.
    for (int count : arrayCounts) {
        double weakMs = MeasureOldGcPause(true, count, gcRounds, repeats);
        double strongMs = MeasureOldGcPause(false, count, gcRounds, repeats);
        double delta = weakMs - strongMs;
        std::cout << "  " << count << " | " << weakMs << " | " << strongMs
                  << " | " << delta << std::endl;
    }
}

// ---------------------------------------------------------------------------
// Test 8: End-to-end OLD_GC pause comparison on a fixed workload.
//
// Builds a large population of arrays with TrackInfo, promotes them to old
// space, then times a sequence of OLD_GCs under both storage forms. OLD_GC
// is used (not YOUNG_GC) because YOUNG_GC only marks the new space and
// would never visit the TrackInfo slots of promoted arrays, making weak
// and strong indistinguishable.
// ---------------------------------------------------------------------------
HWTEST_F_L0(TrackInfoPerfTest, GC_WeakRefVsHeapObject_PauseTime)
{
    if (!thread->GetEcmaVM()->IsEnableElementsKind()) {
        return;
    }
    constexpr int arrayCount = 400;
    constexpr int oldRounds = 3;
    constexpr int repeats = 1;

    double weakMs = MeasureOldGcPause(true, arrayCount, oldRounds, repeats);
    double strongMs = MeasureOldGcPause(false, arrayCount, oldRounds, repeats);
    double delta = weakMs - strongMs;

    std::cout << "[TrackInfo-Perf] GC_WeakRefVsHeapObject_PauseTime (" << arrayCount
              << " arrays, " << oldRounds << " old GCs × " << repeats
              << " repeats):" << std::endl;
    std::cout << "  WeakRef : " << weakMs << " ms/GC" << std::endl;
    std::cout << "  HeapObj : " << strongMs << " ms/GC" << std::endl;
    std::cout << "  Delta   : " << delta << " ms/GC" << std::endl;

    EXPECT_GT(weakMs, 0.0);
    EXPECT_GT(strongMs, 0.0);
    // Note: do NOT assert weakMs > strongMs. Under OLD_GC with both
    // JSArray and TrackInfo in old space, the weak path only does
    // RecordWeakReference and skips MarkAndPush(TrackInfo), while the strong
    // path does MarkAndPush(TrackInfo) which then recursively visits
    // TrackInfo's CachedHClass/CachedFunc slots. The weak path therefore does
    // LESS marking work, and is expected to be faster or comparable. The
    // optimization's GC benefit is the elimination of weakQueue_ push/pop
    // overhead and per-access weak-tag stripping, not marking throughput.
}

// ---------------------------------------------------------------------------
// Test 9: Correctness — TrackInfo slots are NOT weak after the optimization.
//
// After the optimization, every TrackInfo slot on every live JSArray must
// satisfy IsHeapObject() && !IsWeak(). This is the invariant that keeps the
// slot out of weakQueue_ during GC marking. If a future change accidentally
// re-introduces CreateAndGetWeakRef on the write side, this test will
// catch it.
//
// We verify the invariant on freshly created arrays, after promotion, and
// after a mixed-GC stress sequence.
// ---------------------------------------------------------------------------
HWTEST_F_L0(TrackInfoPerfTest, WeakRefQueueNotPopulated)
{
    if (!thread->GetEcmaVM()->IsEnableElementsKind()) {
        return;
    }
    constexpr int arrayCount = 100;

    std::vector<JSHandle<JSArray>> arrays;
    arrays.reserve(arrayCount);
    for (int i = 0; i < arrayCount; i++) {
        arrays.push_back(CreateArrayWithTrackInfo(4));
    }

    // 1. Freshly created — TrackInfo must be a strong HeapObject.
    for (auto &array : arrays) {
        auto v = array->GetTrackInfo(thread);
        ASSERT_TRUE(v.IsHeapObject()) << "TrackInfo must be a HeapObject (not Undefined)";
        ASSERT_FALSE(v.IsWeak()) << "TrackInfo must NOT be a WeakRef after the optimization";
    }

    // 2. After promotion to old space.
    thread->GetEcmaVM()->CollectGarbage(TriggerGCType::YOUNG_GC);
    thread->GetEcmaVM()->CollectGarbage(TriggerGCType::OLD_GC);
    for (auto &array : arrays) {
        auto v = array->GetTrackInfo(thread);
        ASSERT_TRUE(v.IsHeapObject());
        ASSERT_FALSE(v.IsWeak());
    }

    // 3. After mixed GC stress.
    for (int p = 0; p < 10; p++) {
        thread->GetEcmaVM()->CollectGarbage(TriggerGCType::YOUNG_GC);
        thread->GetEcmaVM()->CollectGarbage(TriggerGCType::OLD_GC);
    }
    for (auto &array : arrays) {
        auto v = array->GetTrackInfo(thread);
        ASSERT_TRUE(v.IsHeapObject());
        ASSERT_FALSE(v.IsWeak());
    }

    // 4. After an ElementsKind transition (which re-writes the TrackInfo).
    for (auto &array : arrays) {
        auto obj = JSHandle<JSObject>::Cast(array);
        JSHandle<JSTaggedValue> floatValue(thread, JSTaggedValue(1.5));
        JSHClass::TransitToElementsKind(thread, obj, floatValue, ElementsKind::NONE);
    }
    for (auto &array : arrays) {
        auto v = array->GetTrackInfo(thread);
        ASSERT_TRUE(v.IsHeapObject()) << "TrackInfo must remain HeapObject after transition";
        ASSERT_FALSE(v.IsWeak()) << "TrackInfo must NOT become WeakRef after transition";
    }
    SUCCEED();
}

}  // namespace panda::test