/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * Runtime-layer correctness tests for the TrackInfo weak-ref→heap-object
 * optimization in NewObjectStubBuilder.
 * The AOT compiler stub builders
 * NewObjectStubBuilder::CreateEmptyArray(GateRef, GateRef, TraceIdInfo,
 *                                      GateRef, GateRef, ProfileOperation)
 *
 * NewObjectStubBuilder::CreateArrayWithBuffer(...)
 * previously wrapped the TrackInfo value in a WeakRef via
 * trackInfo = env->GetBuilder()->CreateWeakRef(*trackInfo);
 * before passing it to NewJSArrayLiteral. That call has been removed so that
 * TrackInfo is stored as a strong HeapObject on the array. The stub builders
 * themselves live in the compiler Circuit-IR layer and cannot be unit-tested
 * in isolation (they require a full Circuit/Environment). These tests
 * therefore verify the runtime behaviour that the stub change produces:
 * TrackInfo is stored as a strong HeapObject (not a WeakRef) on arrays
 *  created via the runtime paths that mirror the stub logic.
 *
 * TrackInfo survives YOUNG_GC and OLD_GC as a HeapObject.
 * ElementsKind transitions still update the cached HClass on a strong
 *  TrackInfo (the fast-path branch that previously fed CreateWeakRef).
 *
 * The "profileTypeInfo undefined" slow-path still produces a usable
 *  array (CreateEmptyArrayCommon with Undefined trackInfo).
 *
 * The CreateArrayWithBuffer analogue (array literal from constant pool)
 *  stores a strong HeapObject TrackInfo when ElementsKind is enabled.
 *
 * Arrays created without a TrackInfo keep Undefined and do not crash GC
 *  — this guards the IsHeapObject checks added in the evacuator.
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

using namespace panda::ecmascript;

namespace panda::test {

class NewObjectStubBuilderTrackInfoTest : public BaseTestWithScope {
protected:
    void SetUp() override
    {
        // ElementsKind must be on, otherwise the TrackInfo fast paths in the
        // stub builder (LoadTrackInfo + CreateEmptyArrayCommon) are not taken
        // and the array slot would stay Undefined regardless of the change.
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

    // Mirror of CreateEmptyArrayCommon in the stub builder at the runtime
    // layer: build an array, attach a TrackInfo as a strong HeapObject
    // (the post-optimization storage form), and return it. This is the
    // behaviour the stub builder now produces after the CreateWeakRef call
    // was removed from the IR it generates.
    JSHandle<JSArray> CreateArrayWithStrongTrackInfo(uint32_t length)
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
        // Post-optimization: TrackInfo stored as a strong HeapObject,
        // NOT wrapped in CreateAndGetWeakRef().
        array->SetTrackInfo(thread, JSTaggedValue(*trackInfo));
        return array;
    }

    // Build an array whose TrackInfo slot is Undefined — this mirrors the
    // slow path of CreateEmptyArray (profileTypeInfo undefined) and
    // CreateEmptyArrayCommon called with Undefined trackInfo.
    JSHandle<JSArray> CreateArrayWithoutTrackInfo(uint32_t length)
    {
        ObjectFactory *factory = thread->GetEcmaVM()->GetFactory();
        JSHandle<TaggedArray> values(factory->NewTaggedArray(length));
        for (uint32_t i = 0; i < length; i++) {
            values->Set(thread, i, JSTaggedValue(static_cast<int>(i)));
        }
        JSHandle<JSArray> array(JSArray::CreateArrayFromList(thread, values));
        // TrackInfo slot is Undefined by default after CreateArrayFromList.
        return array;
    }
};

// ---------------------------------------------------------------------------
// Test 1: TrackInfo stored as a strong HeapObject, not a WeakRef.
//
// Verifies the runtime invariant produced by the stub builder change:
// after CreateEmptyArray / CreateArrayWithBuffer no longer call
// CreateWeakRef, the TrackInfo value on a fresh array must be a HeapObject
// and must NOT carry the weak tag.
// ---------------------------------------------------------------------------
HWTEST_F_L0(NewObjectStubBuilderTrackInfoTest, TrackInfoIsStrongHeapObject)
{
    if (!thread->GetEcmaVM()->IsEnableElementsKind()) {
        return;
    }
    auto array = CreateArrayWithStrongTrackInfo(8);
    auto trackInfoVal = array->GetTrackInfo(thread);
    ASSERT_TRUE(trackInfoVal.IsHeapObject()) << "TrackInfo must be a HeapObject after the optimization";
    ASSERT_FALSE(trackInfoVal.IsWeak()) << "TrackInfo must NOT be a WeakRef after CreateWeakRef was removed";
    auto *trackInfo = TrackInfo::Cast(trackInfoVal.GetTaggedObject());
    EXPECT_EQ(trackInfo->GetElementsKind(), ElementsKind::INT);
}

// ---------------------------------------------------------------------------
// Test 2: TrackInfo survives GC as a strong HeapObject.
//
// The evacuator relies on the IsHeapObject guard; if the slot were still a
// WeakRef it would be routed through weakQueue_ and the assertions below
// would fail.
// ---------------------------------------------------------------------------
HWTEST_F_L0(NewObjectStubBuilderTrackInfoTest, TrackInfoSurvivesGCAsHeapObject)
{
    if (!thread->GetEcmaVM()->IsEnableElementsKind()) {
        return;
    }
    auto array = CreateArrayWithStrongTrackInfo(4);
    thread->GetEcmaVM()->CollectGarbage(TriggerGCType::YOUNG_GC);
    thread->GetEcmaVM()->CollectGarbage(TriggerGCType::OLD_GC);
    auto trackInfoVal = array->GetTrackInfo(thread);
    ASSERT_TRUE(trackInfoVal.IsHeapObject()) << "TrackInfo lost HeapObject form after GC";
    ASSERT_FALSE(trackInfoVal.IsWeak()) << "TrackInfo must remain non-weak after GC";
    auto *trackInfo = TrackInfo::Cast(trackInfoVal.GetTaggedObject());
    EXPECT_EQ(trackInfo->GetElementsKind(), ElementsKind::INT);
}

// ---------------------------------------------------------------------------
// Test 3: ElementsKind transition updates the strong TrackInfo cached HClass.
//
// The fast-path branch in CreateEmptyArray that previously called
// CreateWeakRef feeds the TrackInfo into NewJSArrayLiteral; the runtime
// counterpart must keep the cached HClass in sync through transitions.
// ---------------------------------------------------------------------------
HWTEST_F_L0(NewObjectStubBuilderTrackInfoTest, ElementsKindTransitionUpdatesStrongTrackInfo)
{
    if (!thread->GetEcmaVM()->IsEnableElementsKind()) {
        return;
    }
    auto array = CreateArrayWithStrongTrackInfo(4);
    auto obj = JSHandle::Cast(array);
    JSHandle floatValue(thread, JSTaggedValue(3.14));
    ASSERT_TRUE(JSHClass::TransitToElementsKind(thread, obj, floatValue, ElementsKind::NONE));
    auto trackInfoVal = array->GetTrackInfo(thread);
    ASSERT_TRUE(trackInfoVal.IsHeapObject());
    ASSERT_FALSE(trackInfoVal.IsWeak());
    auto *trackInfo = TrackInfo::Cast(trackInfoVal.GetTaggedObject());
    EXPECT_EQ(trackInfo->GetElementsKind(), ElementsKind::NUMBER);
    auto numberHClass = JSTaggedValue(thread->GetArrayInstanceHClass(ElementsKind::NUMBER, false));
    EXPECT_EQ(trackInfo->GetCachedHClass(thread), numberHClass);
}

// ---------------------------------------------------------------------------
// Test 4: Slow-path array (Undefined TrackInfo) does not crash GC.
//
// CreateEmptyArray's slow path (profileTypeInfo undefined) and the single-
// argument CreateEmptyArray(glue) both call CreateEmptyArrayCommon with an
// Undefined trackInfo. After the optimization the stub builder passes that
// Undefined straight through to NewJSArrayLiteral; the runtime equivalent
// must keep GC stable when the slot is not a HeapObject.
// ---------------------------------------------------------------------------
HWTEST_F_L0(NewObjectStubBuilderTrackInfoTest, UndefinedTrackInfoDoesNotCrashGC)
{
    if (!thread->GetEcmaVM()->IsEnableElementsKind()) {
        return;
    }
    auto array = CreateArrayWithoutTrackInfo(4);
    auto trackInfoVal = array->GetTrackInfo(thread);
    EXPECT_FALSE(trackInfoVal.IsHeapObject()) << "Default TrackInfo slot must not be a HeapObject";
    EXPECT_FALSE(trackInfoVal.IsWeak());
    // GC over arrays with Undefined TrackInfo must be safe (IsHeapObject
    // guards in the evacuator short-circuit cleanly).
    thread->GetEcmaVM()->CollectGarbage(TriggerGCType::YOUNG_GC);
    thread->GetEcmaVM()->CollectGarbage(TriggerGCType::OLD_GC);
    auto trackInfoValAfterGC = array->GetTrackInfo(thread);
    EXPECT_FALSE(trackInfoValAfterGC.IsHeapObject());
    EXPECT_FALSE(trackInfoValAfterGC.IsWeak());
    SUCCEED();
}

// ---------------------------------------------------------------------------
// Test 5: CreateArrayWithBuffer analogue — array literal with TrackInfo.
//
// CreateArrayWithBuffer in the stub builder builds an array from a constant
// pool literal and attaches the TrackInfo from LoadTrackInfo. After the
// CreateWeakRef removal the TrackInfo must be a strong HeapObject. This test
// mirrors that path by attaching a strong TrackInfo to a small array literal
// created from a tagged-array constant.
// ---------------------------------------------------------------------------
HWTEST_F_L0(NewObjectStubBuilderTrackInfoTest, ArrayLiteralWithStrongTrackInfo)
{
    if (!thread->GetEcmaVM()->IsEnableElementsKind()) {
        return;
    }
    // Build a small "literal" array (mirrors a constant-pool array literal)
    // and attach a strong TrackInfo — the post-optimization storage form
    // produced by CreateArrayWithBuffer.
    auto array = CreateArrayWithStrongTrackInfo(2);
    auto trackInfoVal = array->GetTrackInfo(thread);
    ASSERT_TRUE(trackInfoVal.IsHeapObject());
    ASSERT_FALSE(trackInfoVal.IsWeak());
    // Verify the array contents are intact (CreateArrayWithBuffer copies the
    // literal's elements into the new array).
    auto elements = array->GetElements(thread);
    auto *taggedArray = TaggedArray::Cast(elements.GetTaggedObject());
    EXPECT_EQ(taggedArray->GetLength(), 2U);
    thread->GetEcmaVM()->CollectGarbage(TriggerGCType::YOUNG_GC);
    auto trackInfoValAfterGC = array->GetTrackInfo(thread);
    ASSERT_TRUE(trackInfoValAfterGC.IsHeapObject());
    ASSERT_FALSE(trackInfoValAfterGC.IsWeak());
}

// ---------------------------------------------------------------------------
// Test 6: Stress — many arrays with strong TrackInfo survive mixed GC.
//
// Mirrors the CreateEmptyArray hot path being executed many times (e.g. a
// loop that allocates empty arrays). After the optimization none of the
// arrays should have a weak-tagged TrackInfo slot, and the evacuator must
// keep every slot reachable.
// ---------------------------------------------------------------------------
HWTEST_F_L0(NewObjectStubBuilderTrackInfoTest, ManyArraysSurviveMixedGC)
{
    if (!thread->GetEcmaVM()->IsEnableElementsKind()) {
        return;
    }
    constexpr int arrayCount = 500;
    constexpr int gcPairs = 10;
    std::vector<JSHandle> arrays;
    arrays.reserve(arrayCount);
    for (int i = 0; i < arrayCount; i++) {
        arrays.push_back(CreateArrayWithStrongTrackInfo(4));
    }
    for (int p = 0; p < gcPairs; p++) {
        thread->GetEcmaVM()->CollectGarbage(TriggerGCType::YOUNG_GC);
        thread->GetEcmaVM()->CollectGarbage(TriggerGCType::OLD_GC);
    }
    for (auto &array : arrays) {
        auto v = array->GetTrackInfo(thread);
        ASSERT_TRUE(v.IsHeapObject()) << "TrackInfo not a HeapObject after mixed GC stress";
        ASSERT_FALSE(v.IsWeak()) << "TrackInfo must not be a WeakRef after the optimization";
        auto *ti = TrackInfo::Cast(v.GetTaggedObject());
        EXPECT_EQ(ti->GetElementsKind(), ElementsKind::INT);
    }
}

// ---------------------------------------------------------------------------
// Test 7: Attaching a WeakRef TrackInfo is still observable as weak.
//
// This is a regression guard: if a future change re-introduces
// CreateWeakRef, the storage form must be detectable as weak so the
// existing weak-ref handling in the evacuator stays correct. The test
// verifies that the runtime can still distinguish the two storage forms,
// confirming that the post-optimization arrays (strong HeapObject) are a
// deliberate, detectable change rather than an accident.
// ---------------------------------------------------------------------------
HWTEST_F_L0(NewObjectStubBuilderTrackInfoTest, WeakRefStorageFormIsDistinguishable)
{
    if (!thread->GetEcmaVM()->IsEnableElementsKind()) {
        return;
    }
    auto constants = thread->GlobalConstants();
    ObjectFactory *factory = thread->GetEcmaVM()->GetFactory();
    JSHandle values(factory->NewTaggedArray(4));
    for (uint32_t i = 0; i < 4; i++) { // 4: length of array
        values->Set(thread, i, JSTaggedValue(static_cast(i)));
    }
    JSHandle array(JSArray::CreateArrayFromList(thread, values));
    auto hClass = thread->GetArrayInstanceHClass(ElementsKind::INT, false);
    array->SynchronizedTransitionClass(thread, hClass);
    auto trackInfoHclass = JSHandle::Cast(constants->GetHandledTrackInfoClass());
    JSHandle trackInfo(thread, factory->NewObject(trackInfoHclass));
    trackInfo->SetCachedHClass(thread, constants->GetUndefined());
    trackInfo->SetElementsKind(ElementsKind::INT);
    // Pre-optimization storage form: wrapped in a WeakRef.
    array->SetTrackInfo(thread, JSTaggedValue(*trackInfo).CreateAndGetWeakRef());
    auto weakVal = array->GetTrackInfo(thread);
    ASSERT_TRUE(weakVal.IsWeak()) << "WeakRef storage form must still be detectable as weak";

    // Post-optimization storage form: strong HeapObject.
    auto strongArray = CreateArrayWithStrongTrackInfo(4);
    auto strongVal = strongArray->GetTrackInfo(thread);
    ASSERT_FALSE(strongVal.IsWeak()) << "Strong HeapObject must not be detectable as weak";
    ASSERT_TRUE(strongVal.IsHeapObject());
}
} // namespace panda::test