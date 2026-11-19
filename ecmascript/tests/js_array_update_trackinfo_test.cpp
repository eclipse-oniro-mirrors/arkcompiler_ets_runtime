/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * http://www.apache.org/licenses-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * Unit tests for JSArray::UpdateTrackInfo.
 * UpdateTrackInfo reads the TrackInfo slot on a JSArray and, when the slot
 * holds a HeapObject (post weak-ref→heap-object optimization), synchronises
 * the TrackInfo's ElementsKind and cached HClass with the array's current
 * JSHClass. The four branches that must be covered are:
 * TrackInfo slot is not a HeapObject (e.g. Undefined) → skip the block.
 * TrackInfo oldKind is Generic → early return, no update.
 * TrackInfo oldKind is non-Generic → update ElementsKind + cachedHClass.
 * Specific non-Generic kind (INT) propagation from hclass to TrackInfo.
 */
#include "ecmascript/ecma_vm.h"
#include "ecmascript/elements.h"
#include "ecmascript/global_env.h"
#include "ecmascript/js_array.h"
#include "ecmascript/js_handle.h"
#include "ecmascript/js_hclass.h"
#include "ecmascript/js_object.h"
#include "ecmascript/js_object-inl.h"
#include "ecmascript/js_tagged_value.h"
#include "ecmascript/object_factory-inl.h"
#include "ecmascript/pgo_profiler/pgo_profiler.h"
#include "ecmascript/tests/test_helper.h"

using namespace panda::ecmascript;

namespace panda::test {

class JSArrayUpdateTrackInfoTest : public BaseTestWithScope {
protected:
    void SetUp() override
    {
        // ElementsKind must be enabled, otherwise TrackInfo is never attached
        // to arrays and UpdateTrackInfo would always hit the non-HeapObject
        // branch regardless of the optimization under test.
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
        // PGOProfilerManager::InitializeData() may fail in a unit-test
        // environment (no valid ap output directory), which would leave the
        // profiler disabled even though SetEnablePGOProfiler(true) was set on
        // the options. Setting the flag directly on the thread bypasses
        // InitializeData and ensures thread->IsPGOProfilerEnable() returns
        // true, so that JSHClass::TransitToElementsKind reaches the changed
        // lines 868-871 (the IsHeapObject guard around PGOProfiler::
        // UpdateTrackInfo).
        thread->SetPGOProfilerEnable(true);
        scope = new EcmaHandleScope(thread);
        JSNApi::InitHybridVMEnv(thread->GetEcmaVM());
    }

    // Build a JSArray whose TrackInfo slot is Undefined — the default state
    // for arrays created without PGO profiling. UpdateTrackInfo must take the
    // non-HeapObject branch and leave the slot untouched. The array is
    // transitioned to a non-Generic ElementsKind (INT) so that
    // JSHClass::TransitToElementsKind (bool overload) does not short-circuit
    // at the IsGeneric(current) early-return and actually reaches the
    // IsHeapObject guard at lines 868-871.
    JSHandle<JSArray> CreateArrayWithoutTrackInfo(uint32_t length)
    {
        ObjectFactory *factory = thread->GetEcmaVM()->GetFactory();
        JSHandle<TaggedArray> values(factory->NewTaggedArray(length));
        for (uint32_t i = 0; i < length; i++) {
            values->Set(thread, i, JSTaggedValue(static_cast<int>(i)));
        }
        JSHandle<JSArray> array(JSArray::CreateArrayFromList(thread, values));
        // Transition to INT (non-Generic) so the bool overload of
        // TransitToElementsKind proceeds past the IsGeneric guard. The
        // TrackInfo slot stays Undefined because we never attach one.
        auto hClass = thread->GetArrayInstanceHClass(ElementsKind::INT, false);
        array->SynchronizedTransitionClass(thread, hClass);
        return array;
    }

    // Build a JSArray with a strong-HeapObject TrackInfo whose ElementsKind
    // is set to `initialKind`. The array's JSHClass is transitioned to
    // `arrayKind` so that UpdateTrackInfo has a non-Generic newKind to
    // propagate. The TrackInfo is stored as a HeapObject (post-optimization
    // storage form), not wrapped in CreateAndGetWeakRef.
    JSHandle<JSArray> CreateArrayWithTrackInfo(ElementsKind initialKind, ElementsKind arrayKind)
    {
        auto constants = thread->GlobalConstants();
        ObjectFactory *factory = thread->GetEcmaVM()->GetFactory();
        JSHandle<TaggedArray> values(factory->NewTaggedArray(4)); // 4: length of array
        for (uint32_t i = 0; i < 4; i++) { // 4: length of array
            values->Set(thread, i, JSTaggedValue(static_cast<int>(i)));
        }
        JSHandle<JSArray> array(JSArray::CreateArrayFromList(thread, values));
        auto hClass = thread->GetArrayInstanceHClass(arrayKind, false);
        array->SynchronizedTransitionClass(thread, hClass);
        auto trackInfoHclass = JSHandle<JSHClass>::Cast(constants->GetHandledTrackInfoClass());
        JSHandle<TrackInfo> trackInfo(thread, factory->NewObject(trackInfoHclass));
        trackInfo->SetCachedHClass(thread, constants->GetUndefined());
        trackInfo->SetCachedFunc(thread, constants->GetUndefined());
        trackInfo->SetElementsKind(initialKind);
        // Post-optimization storage: strong HeapObject, no CreateAndGetWeakRef.
        array->SetTrackInfo(thread, JSTaggedValue(*trackInfo));
        return array;
    }
};

// ---------------------------------------------------------------------------
// Branch 1: TrackInfo slot is not a HeapObject (Undefined) → skip the block.
//
// Scenario: an array created without PGO profiling has its TrackInfo slot set
// to Undefined. UpdateTrackInfo must take the !IsHeapObject branch and
// return without touching the slot or crashing.
// Expected: TrackInfo slot remains Undefined after the call; no side effects.
// ---------------------------------------------------------------------------
HWTEST_F_L0(JSArrayUpdateTrackInfoTest, UpdateTrackInfo_NoTrackInfo_SkipsUpdate)
{
    if (!thread->GetEcmaVM()->IsEnableElementsKind()) {
        return;
    }
    auto array = CreateArrayWithoutTrackInfo(4);
    // Precondition: slot is Undefined, which is not a HeapObject.
    ASSERT_FALSE(array->GetTrackInfo(thread).IsHeapObject());

    array->UpdateTrackInfo(thread);

    // Postcondition: slot is still Undefined — the function must be a no-op.
    auto slotAfter = array->GetTrackInfo(thread);
    EXPECT_FALSE(slotAfter.IsHeapObject());
    EXPECT_TRUE(slotAfter.IsUndefined());
}

// ---------------------------------------------------------------------------
// Branch 2: TrackInfo oldKind is Generic → early return, no update.
//
// Scenario: the TrackInfo is a HeapObject but its ElementsKind is already
// GENERIC. UpdateTrackInfo must hit the IsGeneric(oldKind) early-return
// and leave both the ElementsKind and the cached HClass untouched.
// Expected: TrackInfo ElementsKind stays GENERIC; cachedHClass is unchanged.
// ---------------------------------------------------------------------------
HWTEST_F_L0(JSArrayUpdateTrackInfoTest, UpdateTrackInfo_GenericKind_EarlyReturn)
{
    if (!thread->GetEcmaVM()->IsEnableElementsKind()) {
        return;
    }
    // Build an array whose TrackInfo oldKind is GENERIC and whose JSHClass
    // kind is INT (a non-Generic newKind). If the early return were missing,
    // the TrackInfo would be overwritten with INT.
    auto array = CreateArrayWithTrackInfo(ElementsKind::GENERIC, ElementsKind::INT);
    auto trackInfoVal = array->GetTrackInfo(thread);
    ASSERT_TRUE(trackInfoVal.IsHeapObject());
    auto *trackInfo = TrackInfo::Cast(trackInfoVal.GetTaggedObject());
    ASSERT_EQ(trackInfo->GetElementsKind(), ElementsKind::GENERIC);
    // Record the cached HClass before the call so we can prove it is unchanged.
    auto cachedHClassBefore = trackInfo->GetCachedHClass(thread);

    array->UpdateTrackInfo(thread);

    // Re-read the TrackInfo — the early return must leave it pristine.
    auto trackInfoValAfter = array->GetTrackInfo(thread);
    ASSERT_TRUE(trackInfoValAfter.IsHeapObject());
    auto *trackInfoAfter = TrackInfo::Cast(trackInfoValAfter.GetTaggedObject());
    EXPECT_EQ(trackInfoAfter->GetElementsKind(), ElementsKind::GENERIC)
        << "Generic oldKind must short-circuit before SetElementsKind";
    EXPECT_EQ(trackInfoAfter->GetCachedHClass(thread), cachedHClassBefore)
        << "Generic oldKind must short-circuit before SetCachedHClass";
}

// ---------------------------------------------------------------------------
// Branch 3: TrackInfo oldKind is non-Generic → update ElementsKind + cachedHClass.
//
// Scenario: the TrackInfo is a HeapObject with a non-Generic oldKind (NONE),
// and the array's JSHClass kind is NUMBER. UpdateTrackInfo must propagate the
// newKind (NUMBER) into the TrackInfo and refresh cachedHClass with the
// array instance HClass for NUMBER.
// Expected: TrackInfo ElementsKind becomes NUMBER; cachedHClass matches the
// NUMBER array instance HClass (with IsPrototype == false).
// ---------------------------------------------------------------------------
HWTEST_F_L0(JSArrayUpdateTrackInfoTest, UpdateTrackInfo_NonGenericKind_UpdatesTrackInfo)
{
    if (!thread->GetEcmaVM()->IsEnableElementsKind()) {
        return;
    }
    // oldKind = NONE (non-Generic), array hclass kind = NUMBER.
    auto array = CreateArrayWithTrackInfo(ElementsKind::NONE, ElementsKind::NUMBER);
    auto trackInfoVal = array->GetTrackInfo(thread);
    ASSERT_TRUE(trackInfoVal.IsHeapObject());
    auto *trackInfo = TrackInfo::Cast(trackInfoVal.GetTaggedObject());
    ASSERT_NE(trackInfo->GetElementsKind(), ElementsKind::GENERIC);

    array->UpdateTrackInfo(thread);

    auto trackInfoValAfter = array->GetTrackInfo(thread);
    ASSERT_TRUE(trackInfoValAfter.IsHeapObject());
    auto *trackInfoAfter = TrackInfo::Cast(trackInfoValAfter.GetTaggedObject());
    EXPECT_EQ(trackInfoAfter->GetElementsKind(), ElementsKind::NUMBER)
        << "UpdateTrackInfo must propagate hclass ElementsKind into TrackInfo";
    auto expectedCachedHClass = JSTaggedValue(thread->GetArrayInstanceHClass(ElementsKind::NUMBER, false));
    EXPECT_EQ(trackInfoAfter->GetCachedHClass(thread), expectedCachedHClass)
        << "UpdateTrackInfo must refresh cachedHClass with the array instance HClass for the new kind";
}

// ---------------------------------------------------------------------------
// Branch 4: Specific non-Generic kind (INT) propagation.
//
// Scenario: the TrackInfo oldKind is NUMBER (non-Generic) and the array's
// JSHClass kind is INT. UpdateTrackInfo must propagate INT into the TrackInfo
// and set cachedHClass to the INT array instance HClass. This guards the
// INT-specific path of the propagation (distinct from Branch 3's NUMBER
// propagation).
// Expected: TrackInfo ElementsKind becomes INT; cachedHClass matches the INT
// array instance HClass (with IsPrototype == false).
// ---------------------------------------------------------------------------
HWTEST_F_L0(JSArrayUpdateTrackInfoTest, UpdateTrackInfo_IntKind_UpdatesToIntKind)
{
    if (!thread->GetEcmaVM()->IsEnableElementsKind()) {
        return;
    }
    // oldKind = NUMBER (non-Generic), array hclass kind = INT.
    auto array = CreateArrayWithTrackInfo(ElementsKind::NUMBER, ElementsKind::INT);
    auto trackInfoVal = array->GetTrackInfo(thread);
    ASSERT_TRUE(trackInfoVal.IsHeapObject());
    auto *trackInfo = TrackInfo::Cast(trackInfoVal.GetTaggedObject());
    ASSERT_EQ(trackInfo->GetElementsKind(), ElementsKind::NUMBER);

    array->UpdateTrackInfo(thread);

    auto trackInfoValAfter = array->GetTrackInfo(thread);
    ASSERT_TRUE(trackInfoValAfter.IsHeapObject());
    auto *trackInfoAfter = TrackInfo::Cast(trackInfoValAfter.GetTaggedObject());
    EXPECT_EQ(trackInfoAfter->GetElementsKind(), ElementsKind::INT)
        << "UpdateTrackInfo must propagate the INT kind from hclass into TrackInfo";
    auto expectedCachedHClass = JSTaggedValue(thread->GetArrayInstanceHClass(ElementsKind::INT, false));
    EXPECT_EQ(trackInfoAfter->GetCachedHClass(thread), expectedCachedHClass)
        << "cachedHClass must be the INT array instance HClass after UpdateTrackInfo";
}

// ---------------------------------------------------------------------------
// JSHClass::TransitToElementsKind (bool overload, js_hclass.cpp:868-871).
//
// After calling JSArray::UpdateTrackInfo, TransitToElementsKind re-reads the
// TrackInfo slot and guards the PGOProfiler::UpdateTrackInfo call with
// IsHeapObject(). The two branches that must be covered are:
// 5. TrackInfo slot is not a HeapObject (Undefined) → skip PGO update.
// 6. TrackInfo slot is a HeapObject → invoke PGOProfiler::UpdateTrackInfo.
// ---------------------------------------------------------------------------

/**
 * Scene: array has no TrackInfo (Undefined) and PGO profiler is enabled.
 * Expect: IsHeapObject() == false branch taken at the PGOProfiler guard;
 *     PGOProfiler::UpdateTrackInfo is skipped; function returns true.
 */
HWTEST_F_L0(JSArrayUpdateTrackInfoTest, TransitToElementsKind_NoTrackInfo_SkipsProfilerUpdate)
{
    if (!thread->GetEcmaVM()->IsEnableElementsKind()) {
        return;
    }
    // SetUp() called thread->SetPGOProfilerEnable(true), so the
    // !thread->IsPGOProfilerEnable() early-return at js_hclass.cpp:865 is
    // NOT taken and execution reaches the IsHeapObject guard at line 869.
    ASSERT_TRUE(thread->IsPGOProfilerEnable());

    auto array = CreateArrayWithoutTrackInfo(4);
    auto obj = JSHandle<JSObject>::Cast(array);
    ASSERT_FALSE(array->GetTrackInfo(thread).IsHeapObject());

    JSHandle<JSTaggedValue> floatValue(thread, JSTaggedValue(3.14));
    bool ok = JSHClass::TransitToElementsKind(thread, obj, floatValue, ElementsKind::NONE);

    EXPECT_TRUE(ok) << "TransitToElementsKind must return true after UpdateTrackInfo";
    // TrackInfo still Undefined after transition — the IsHeapObject guard
    // short-circuited the PGOProfiler::UpdateTrackInfo call.
    auto slotAfter = array->GetTrackInfo(thread);
    EXPECT_FALSE(slotAfter.IsHeapObject());
    EXPECT_TRUE(slotAfter.IsUndefined());
}

/**
 * Scene: array has a TrackInfo (HeapObject) and PGO profiler is enabled.
 * Expect: IsHeapObject() == true branch taken at the PGOProfiler guard;
 *     PGOProfiler::UpdateTrackInfo is invoked; function returns true and
 *
 *     the TrackInfo is updated to the new kind.
 */
HWTEST_F_L0(JSArrayUpdateTrackInfoTest, TransitToElementsKind_WithTrackInfo_InvokesProfilerUpdate)
{
    if (!thread->GetEcmaVM()->IsEnableElementsKind()) {
        return;
    }
    ASSERT_TRUE(thread->IsPGOProfilerEnable());

    auto array = CreateArrayWithTrackInfo(ElementsKind::INT, ElementsKind::INT);
    auto obj = JSHandle<JSObject>::Cast(array);
    ASSERT_TRUE(array->GetTrackInfo(thread).IsHeapObject());

    JSHandle<JSTaggedValue> floatValue(thread, JSTaggedValue(3.14));
    bool ok = JSHClass::TransitToElementsKind(thread, obj, floatValue, ElementsKind::NONE);

    EXPECT_TRUE(ok) << "TransitToElementsKind must return true after successful transition";
    // TrackInfo survives as a HeapObject and was updated to NUMBER.
    auto slotAfter = array->GetTrackInfo(thread);
    ASSERT_TRUE(slotAfter.IsHeapObject());
    auto *trackInfoAfter = TrackInfo::Cast(slotAfter.GetTaggedObject());
    EXPECT_EQ(trackInfoAfter->GetElementsKind(), ElementsKind::NUMBER)
        << "TransitToElementsKind must propagate NUMBER kind into TrackInfo";
}

// ---------------------------------------------------------------------------
// Additional branch coverage for JSHClass::TransitToElementsKind (bool overload)
// to reach the 80% function/line/branch coverage threshold. The early-return
// branches below are exercised in isolation.
// ---------------------------------------------------------------------------

/**
 * Scene: object is not a JSArray (a plain JSObject).
 * Expect: the !object->IsJSArray() early-return at line 842-843 fires and
 *     the function returns false without touching TrackInfo or PGO state.
 */
HWTEST_F_L0(JSArrayUpdateTrackInfoTest, TransitToElementsKind_NotJSArray_ReturnsFalse)
{
    if (!thread->GetEcmaVM()->IsEnableElementsKind()) {
        return;
    }
    // Build a plain JSObject (not a JSArray) via the Object constructor.
    auto *factory = thread->GetEcmaVM()->GetFactory();
    JSHandle env = thread->GetEcmaVM()->GetGlobalEnv();
    JSHandle objFunc = env->GetObjectFunction();
    JSHandle obj(factory->NewJSObjectByConstructor(JSHandle(objFunc), objFunc));
    ASSERT_FALSE(obj->IsJSArray());

    JSHandle<JSTaggedValue> intValue(thread, JSTaggedValue(1));
    bool ok = JSHClass::TransitToElementsKind(thread, obj, intValue, ElementsKind::NONE);
    EXPECT_FALSE(ok) << "TransitToElementsKind must return false for non-JSArray objects";
}

/**
 * Scene: array has a Generic ElementsKind (the default after CreateArrayFromList).
 * Expect: the IsGeneric(current) early-return at line 846-847 fires and the
 *     function returns false without proceeding to the TrackInfo/PGO code.
 */
HWTEST_F_L0(JSArrayUpdateTrackInfoTest, TransitToElementsKind_GenericCurrent_ReturnsFalse)
{
    if (!thread->GetEcmaVM()->IsEnableElementsKind()) {
        return;
    }
    // CreateArrayFromList transitions the array to GENERIC by default; we do
    // NOT call SynchronizedTransitionClass here so the kind stays GENERIC.
    ObjectFactory *factory = thread->GetEcmaVM()->GetFactory();
    JSHandle values(factory->NewTaggedArray(2));
    values->Set(thread, 0, JSTaggedValue(1));
    values->Set(thread, 1, JSTaggedValue(2));
    JSHandle array(JSArray::CreateArrayFromList(thread, values));
    auto obj = JSHandle::Cast(array);
    ASSERT_TRUE(Elements::IsGeneric(array->GetJSHClass()->GetElementsKind()));

    JSHandle<JSTaggedValue> floatValue(thread, JSTaggedValue(3.14));
    bool ok = JSHClass::TransitToElementsKind(thread, obj, floatValue, ElementsKind::NONE);
    EXPECT_FALSE(ok) << "TransitToElementsKind must return false when current kind is Generic";
}

/**
 * Scene: the merged newKind equals the current kind.
 * Expect: the newKind == current early-return at line 852-853 fires and the
 *     function returns false without calling UpdateTrackInfo or the PGO
 *
 *     profiler. We build an INT array and pass an INT value with kind=NONE;
 *
 *     MergeElementsKindNoFix(INT_value, INT, NONE) yields INT, equal to
 *
 *     current, triggering the early return.
 */
HWTEST_F_L0(JSArrayUpdateTrackInfoTest, TransitToElementsKind_NewKindEqualsCurrent_ReturnsFalse)
{
    if (!thread->GetEcmaVM()->IsEnableElementsKind()) {
        return;
    }
    // Build an INT array (non-Generic) with a TrackInfo.
    auto array = CreateArrayWithTrackInfo(ElementsKind::INT, ElementsKind::INT);
    auto obj = JSHandle::Cast(array);
    ASSERT_EQ(array->GetJSHClass()->GetElementsKind(), ElementsKind::INT);

    // Pass an INT value; merging INT value with current INT and kind=NONE
    // yields INT, equal to current → early return at line 852-853.
    JSHandle<JSTaggedValue> intValue(thread, JSTaggedValue(1));
    bool ok = JSHClass::TransitToElementsKind(thread, obj, intValue, ElementsKind::NONE);
    EXPECT_FALSE(ok) << "TransitToElementsKind must return false when merged kind equals current";
}

/**
 * Scene: PGO profiler is disabled on the thread.
 * Expect: the !thread->IsPGOProfilerEnable() early-return at line 865-866
 *     fires and the function returns true without re-reading TrackInfo
 *
 *     or calling PGOProfiler::UpdateTrackInfo. We toggle the flag off,
 *
 *     call the function, then restore it.
 */
HWTEST_F_L0(JSArrayUpdateTrackInfoTest, TransitToElementsKind_PGODisabled_EarlyReturn)
{
    if (!thread->GetEcmaVM()->IsEnableElementsKind()) {
        return;
    }
    auto array = CreateArrayWithTrackInfo(ElementsKind::INT, ElementsKind::INT);
    auto obj = JSHandle::Cast(array);
    ASSERT_TRUE(array->GetTrackInfo(thread).IsHeapObject());

    // Disable PGO on the thread so the early-return at line 865-866 fires.
    ASSERT_TRUE(thread->IsPGOProfilerEnable());
    thread->SetPGOProfilerEnable(false);
    ASSERT_FALSE(thread->IsPGOProfilerEnable());

    JSHandle<JSTaggedValue> floatValue(thread, JSTaggedValue(3.14));
    bool ok = JSHClass::TransitToElementsKind(thread, obj, floatValue, ElementsKind::NONE);

    EXPECT_TRUE(ok) << "TransitToElementsKind must return true after UpdateTrackInfo when PGO is disabled";

    // Restore PGO enabled for subsequent tests in the same suite.
    thread->SetPGOProfilerEnable(true);

    // TrackInfo survives as a HeapObject and was updated to NUMBER by
    // JSArray::UpdateTrackInfo (line 863) even though the PGO branch was
    // skipped.
    auto slotAfter = array->GetTrackInfo(thread);
    ASSERT_TRUE(slotAfter.IsHeapObject());
    auto *trackInfoAfter = TrackInfo::Cast(slotAfter.GetTaggedObject());
    EXPECT_EQ(trackInfoAfter->GetElementsKind(), ElementsKind::NUMBER)
        << "UpdateTrackInfo must still propagate NUMBER kind when PGO is disabled";
}
} // namespace panda::test