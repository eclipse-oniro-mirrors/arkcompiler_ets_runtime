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
 * Unit tests for PGOProfiler::UpdateTrackArrayLength.
 *
 * UpdateTrackArrayLength reads a JSTaggedValue that points at a TrackInfo
 * object and, when the value is a HeapObject, ensures the TrackInfo's
 * ArrayLength field is at least `newSize`. The three branches that must
 * be covered are:
 *   1. trackInfoVal is not a HeapObject (e.g. Undefined) → skip the block.
 *   2. trackInfoVal is a HeapObject and oldSize >= newSize → early return.
 *   3. trackInfoVal is a HeapObject and oldSize < newSize → SetArrayLength
 *      + UpdateTrackInfo propagation.
 */

#include "ecmascript/ecma_vm.h"
#include "ecmascript/global_env.h"
#include "ecmascript/js_array.h"
#include "ecmascript/js_handle.h"
#include "ecmascript/js_hclass.h"
#include "ecmascript/js_tagged_value.h"
#include "ecmascript/object_factory-inl.h"
#include "ecmascript/pgo_profiler/pgo_profiler.h"
#include "ecmascript/tests/test_helper.h"

using namespace panda::ecmascript;

namespace panda::test {

class PGOProfilerUpdateTrackArrayLengthTest : public BaseTestWithScope<false> {
protected:
    void SetUp() override
    {
        // ElementsKind must be enabled, otherwise TrackInfo is never attached
        // to arrays and UpdateTrackArrayLength would always take the
        // non-HeapObject branch regardless of the optimization under test.
        JSRuntimeOptions options;
        options.SetEnableForceGC(true);
        options.SetEnableAsmInterpreter(true);
        options.SetEnableElementsKind(true);
        instance = JSNApi::CreateEcmaVM(options);
        instance->SetEnableForceGC(true);
        ASSERT_TRUE(instance != nullptr) << "Cannot create EcmaVM";
        thread = instance->GetJSThread();
        thread->ManagedCodeBegin();
        scope = new EcmaHandleScope(thread);
        JSNApi::InitHybridVMEnv(thread->GetEcmaVM());
    }

    // Build a JSArray whose TrackInfo slot is Undefined — the default state
    // for arrays created without PGO profiling. UpdateTrackArrayLength must
    // take the non-HeapObject branch and return without touching anything.
    JSHandle<JSArray> CreateArrayWithoutTrackInfo(uint32_t length)
    {
        ObjectFactory *factory = thread->GetEcmaVM()->GetFactory();
        JSHandle<TaggedArray> values(factory->NewTaggedArray(length));
        for (uint32_t i = 0; i < length; i++) {
            values->Set(thread, i, JSTaggedValue(static_cast<int>(i)));
        }
        JSHandle<JSArray> array(JSArray::CreateArrayFromList(thread, values));
        return array;
    }

    // Build a JSArray with a strong-HeapObject TrackInfo whose initial
    // ArrayLength is `initialLength`. The TrackInfo is stored as a
    // HeapObject (post-optimization storage form), not wrapped in
    // CreateAndGetWeakRef, so IsHeapObject() returns true.
    JSHandle<JSArray> CreateArrayWithTrackInfo(uint32_t initialLength)
    {
        auto constants = thread->GlobalConstants();
        ObjectFactory *factory = thread->GetEcmaVM()->GetFactory();
        JSHandle<TaggedArray> values(factory->NewTaggedArray(initialLength));
        for (uint32_t i = 0; i < initialLength; i++) {
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
        trackInfo->SetArrayLength(initialLength);
        array->SetTrackInfo(thread, JSTaggedValue(*trackInfo));
        return array;
    }

    // Read the TrackInfo JSTaggedValue from the array's slot. Caller is
    // responsible for keeping the array handle alive.
    JSTaggedValue GetTrackInfoValue(JSHandle<JSArray> array)
    {
        return array->GetTrackInfo(thread);
    }
};

// ---------------------------------------------------------------------------
// Branch 1: trackInfoVal is not a HeapObject (Undefined) → skip the block.
// Scenario: an array without TrackInfo has its slot set to Undefined. Passing
// Undefined to UpdateTrackArrayLength must early-return at the IsHeapObject
// guard and must not crash.
// Expected: function returns without modification; no crash; TrackInfo slot
// remains Undefined.
// ---------------------------------------------------------------------------
HWTEST_F_L0(PGOProfilerUpdateTrackArrayLengthTest, UpdateTrackArrayLength_NoHeapObject_Skips)
{
    if (!thread->GetEcmaVM()->IsEnableElementsKind()) {
        return;
    }
    auto array = CreateArrayWithoutTrackInfo(4);
    auto trackInfoVal = GetTrackInfoValue(array);
    ASSERT_FALSE(trackInfoVal.IsHeapObject())
        << "TrackInfo slot must be Undefined for an array without profiling";

    auto profiler = thread->GetEcmaVM()->GetPGOProfiler();
    ASSERT_TRUE(profiler != nullptr);
    // newSize is irrelevant for this branch; the IsHeapObject guard must
    // short-circuit before any TrackInfo field is accessed.
    profiler->UpdateTrackArrayLength(trackInfoVal, 16);

    // Slot must remain non-HeapObject (Undefined) after the call.
    auto trackInfoValAfter = GetTrackInfoValue(array);
    EXPECT_FALSE(trackInfoValAfter.IsHeapObject())
        << "UpdateTrackArrayLength must not modify a non-HeapObject slot";
}

// ---------------------------------------------------------------------------
// Branch 2: trackInfoVal is a HeapObject, oldSize >= newSize → early return.
// Scenario: a TrackInfo with ArrayLength=10 is passed with newSize=5. The
// `oldSize >= newSize` guard must trigger an early return and the stored
// ArrayLength must remain 10.
// Expected: ArrayLength unchanged at 10 after the call.
// ---------------------------------------------------------------------------
HWTEST_F_L0(PGOProfilerUpdateTrackArrayLengthTest, UpdateTrackArrayLength_OldSizeGE_NewSize_EarlyReturn)
{
    if (!thread->GetEcmaVM()->IsEnableElementsKind()) {
        return;
    }
    constexpr uint32_t initialLength = 10;
    constexpr uint32_t newSize = 5;
    auto array = CreateArrayWithTrackInfo(initialLength);
    auto trackInfoVal = GetTrackInfoValue(array);
    ASSERT_TRUE(trackInfoVal.IsHeapObject())
        << "TrackInfo slot must be a HeapObject for a profiled array";
    auto *trackInfo = TrackInfo::Cast(trackInfoVal.GetTaggedObject());
    ASSERT_EQ(trackInfo->GetArrayLength(), initialLength)
        << "TrackInfo ArrayLength must match the initial value before the call";

    auto profiler = thread->GetEcmaVM()->GetPGOProfiler();
    ASSERT_TRUE(profiler != nullptr);
    profiler->UpdateTrackArrayLength(trackInfoVal, newSize);

    auto trackInfoValAfter = GetTrackInfoValue(array);
    ASSERT_TRUE(trackInfoValAfter.IsHeapObject());
    auto *trackInfoAfter = TrackInfo::Cast(trackInfoValAfter.GetTaggedObject());
    EXPECT_EQ(trackInfoAfter->GetArrayLength(), initialLength)
        << "ArrayLength must remain unchanged when oldSize >= newSize";
}

// ---------------------------------------------------------------------------
// Branch 2 (boundary): trackInfoVal is a HeapObject, oldSize == newSize.
// Scenario: a TrackInfo with ArrayLength=8 is passed with newSize=8. The
// `oldSize >= newSize` guard must trigger an early return (equal is covered
// by `>=`) and the stored ArrayLength must remain 8.
// Expected: ArrayLength unchanged at 8 after the call.
// ---------------------------------------------------------------------------
HWTEST_F_L0(PGOProfilerUpdateTrackArrayLengthTest, UpdateTrackArrayLength_EqualSize_EarlyReturn)
{
    if (!thread->GetEcmaVM()->IsEnableElementsKind()) {
        return;
    }
    constexpr uint32_t initialLength = 8;
    constexpr uint32_t newSize = 8;
    auto array = CreateArrayWithTrackInfo(initialLength);
    auto trackInfoVal = GetTrackInfoValue(array);
    ASSERT_TRUE(trackInfoVal.IsHeapObject());
    auto *trackInfo = TrackInfo::Cast(trackInfoVal.GetTaggedObject());
    ASSERT_EQ(trackInfo->GetArrayLength(), initialLength);

    auto profiler = thread->GetEcmaVM()->GetPGOProfiler();
    ASSERT_TRUE(profiler != nullptr);
    profiler->UpdateTrackArrayLength(trackInfoVal, newSize);

    auto trackInfoValAfter = GetTrackInfoValue(array);
    ASSERT_TRUE(trackInfoValAfter.IsHeapObject());
    auto *trackInfoAfter = TrackInfo::Cast(trackInfoValAfter.GetTaggedObject());
    EXPECT_EQ(trackInfoAfter->GetArrayLength(), initialLength)
        << "ArrayLength must remain unchanged when oldSize == newSize (>= branch)";
}

// ---------------------------------------------------------------------------
// Branch 3: trackInfoVal is a HeapObject, oldSize < newSize → SetArrayLength
// + UpdateTrackInfo propagation.
// Scenario: a TrackInfo with ArrayLength=4 is passed with newSize=16. The
// function must update the stored ArrayLength to 16 and invoke
// UpdateTrackInfo (which performs further propagation but must not crash).
// Expected: ArrayLength updated from 4 to 16 after the call.
// ---------------------------------------------------------------------------
HWTEST_F_L0(PGOProfilerUpdateTrackArrayLengthTest, UpdateTrackArrayLength_OldSizeLT_NewSize_Updates)
{
    if (!thread->GetEcmaVM()->IsEnableElementsKind()) {
        return;
    }
    constexpr uint32_t initialLength = 4;
    constexpr uint32_t newSize = 16;
    auto array = CreateArrayWithTrackInfo(initialLength);
    auto trackInfoVal = GetTrackInfoValue(array);
    ASSERT_TRUE(trackInfoVal.IsHeapObject());
    auto *trackInfo = TrackInfo::Cast(trackInfoVal.GetTaggedObject());
    ASSERT_EQ(trackInfo->GetArrayLength(), initialLength)
        << "TrackInfo ArrayLength must match the initial value before the call";

    auto profiler = thread->GetEcmaVM()->GetPGOProfiler();
    ASSERT_TRUE(profiler != nullptr);
    profiler->UpdateTrackArrayLength(trackInfoVal, newSize);

    auto trackInfoValAfter = GetTrackInfoValue(array);
    ASSERT_TRUE(trackInfoValAfter.IsHeapObject())
        << "TrackInfo slot must remain a HeapObject after the update";
    auto *trackInfoAfter = TrackInfo::Cast(trackInfoValAfter.GetTaggedObject());
    EXPECT_EQ(trackInfoAfter->GetArrayLength(), newSize)
        << "ArrayLength must be updated to newSize when oldSize < newSize";
}

}  // namespace panda::test