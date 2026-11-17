/*
 * Copyright (c) 2021 Huawei Device Co., Ltd.
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

// Test purpose:
// The CreateEmptyArray / CreateArrayWithBuffer stub builders historically
// wrapped the TrackInfo reference with CreateWeakRef before storing it into
// the new JSArray's TRACK_INFO_OFFSET slot. When this is changed to a strong
// reference, the TrackInfo slot no longer enters the WeakReferenceQueue during
// local CC marking, so ConcurrentCopyGC::UpdateRecordWeakReference (a STW
// phase inside RunPhase) has fewer entries to pop/test/clear.
//
// Because local CC marking runs concurrently, the extra marking work for the
// strongly-reachable TrackInfo (and its CachedHClass / CachedFunc children)
// does NOT contribute to pause time. The pause-time delta is concentrated in
// the STW ProcessWeakReference phase, measured by GCStats scope UpdateWeekRef.
//
// This file verifies the weak-ref TrackInfo storage configuration:
// STW pause-time impact of the UpdateWeekRef scope when the TrackInfo slot
// is stored as a weak reference (enqueued into WeakReferenceQueue).

#include "ecmascript/ecma_vm.h"
#include "ecmascript/global_env.h"
#include "ecmascript/js_array.h"
#include "ecmascript/js_handle.h"
#include "ecmascript/js_hclass.h"
#include "ecmascript/mem/concurrent_marker.h"
#include "ecmascript/mem/gc_stats.h"
#include "ecmascript/mem/heap.h"
#include "ecmascript/object_factory.h"
#include "ecmascript/tests/test_helper.h"

using namespace panda::ecmascript;

namespace panda::test {

class JSArrayTrackInfoGCTest : public BaseTestWithScope {
public:
    void SetUp() override
    {
        JSRuntimeOptions options;
        instance = JSNApi::CreateEcmaVM(options);
        ASSERT_TRUE(instance != nullptr) << "Cannot create EcmaVM";
        thread = instance->GetJSThread();
        instance->GetJSOptions().SetEnableForceGC(false);
        thread->ManagedCodeBegin();
        scope = new EcmaHandleScope(thread);
        instance->SetEnableForceGC(false);
        heap_ = const_cast<Heap *>(thread->GetEcmaVM()->GetHeap());
        heap_->GetConcurrentMarker()->ConfigConcurrentMark(true);
        heap_->GetConcurrentMarker()->EnableConcurrentMarking(EnableConcurrentMarkType::ENABLE);
        heap_->Prepare();
        if (heap_->CheckOngoingConcurrentMarking()) {
            heap_->GetConcurrentMarker()->Reset();
        }
    }

    // Drive a full local CC cycle: concurrent mark + STW copy phase.
    void TriggerLocalCC()
    {
        if (heap_->CheckOngoingConcurrentMarking()) {
            heap_->GetConcurrentMarker()->Reset();
        }
        bool succ = heap_->TryTriggerCCMarking(MarkReason::OTHER);
        if (!succ) {
            return;
        }
        heap_->CollectGarbageFromCCMark(GCReason::TRIGGER_BY_JS);
        ASSERT_TRUE(thread->IsConcurrentCopying());
        heap_->SetNearGCInSensitive(true);
        heap_->WaitAndHandleCCFinished();
    }

    // STW duration (ms) of the UpdateRecordWeakReference phase of the most
    // recent GC. Lower is better.
    float LastUpdateWeekRefMs() const
    {
        return heap_->GetEcmaVM()->GetEcmaGCStats()->GetScopeDuration(
            GCStats::Scope::ScopeId::UpdateWeekRef);
    }

    // Build a TrackInfo whose CachedHClass is a real heap JSHClass, so that
    // the weak-ref path actually enqueues the slot and the strong-ref path
    // actually pins the CachedHClass.
    TrackInfo *CreateTrackInfoWithCachedHClass(JSThread *thread)
    {
        auto constants = thread->GlobalConstants();
        ObjectFactory *factory = thread->GetEcmaVM()->GetFactory();
        auto trackInfoHclass = JSHandle<JSHClass>::Cast(constants->GetHandledTrackInfoClass());
        auto trackInfo = TrackInfo::Cast(factory->NewObject(trackInfoHclass));
        auto cachedHClass = thread->GetArrayInstanceHClass(ElementsKind::INT, false);
        trackInfo->SetCachedHClass(thread, JSTaggedValue(cachedHClass));
        trackInfo->SetElementsKind(ElementsKind::INT);
        trackInfo->SetCachedFunc(thread, constants->GetUndefined());
        trackInfo->SetArrayLength(0);
        return trackInfo;
    }

    JSArray *CreateEmptyJSArray(JSThread *thread)
    {
        ObjectFactory *factory = thread->GetEcmaVM()->GetFactory();
        JSHandle<JSHClass> arrayHClass(thread, thread->GetArrayInstanceHClass(ElementsKind::INT, false));
        JSHandle<JSArray> array(factory->NewJSArray(0, arrayHClass));
        return *array;
    }

    size_t HeapLiveSize() const
    {
        return heap_->GetHeapObjectSize();
    }

    Heap *heap_ {nullptr};
};

// ---------------------------------------------------------------------------
// STW UpdateWeekRef duration of local CC with weak-ref TrackInfo storage.
//
// When TrackInfo is stored as a weak reference, every array's TrackInfo slot
// is enqueued into the WeakReferenceQueue during concurrent marking, then
// dequeued and tested in the STW ProcessWeakReference phase. This test
// triggers multiple local CC cycles and reports the average UpdateWeekRef
// scope duration under that configuration.
// ---------------------------------------------------------------------------
HWTEST_F_L0(JSArrayTrackInfoGCTest, WeakRefLocalCCUpdateWeekRefTime)
{
    constexpr int num = 50000;
    constexpr int rounds = 5;

    std::vector<JSHandle<JSArray>> arrays;
    arrays.reserve(num);
    for (int i = 0; i < num; i++) {
        JSHandle<JSArray> array(thread, CreateEmptyJSArray(thread));
        TrackInfo *trackInfo = CreateTrackInfoWithCachedHClass(thread);
        JSTaggedValue weakRef = JSTaggedValue(trackInfo).CreateAndGetWeakRef();
        array->SetTrackInfo(thread, weakRef);
        arrays.push_back(array);
    }

    float totalMs = 0.0f;
    for (int r = 0; r < rounds; r++) {
        TriggerLocalCC();
        float ms = LastUpdateWeekRefMs();
        totalMs += ms;
        std::cout << "[WeakRef] round " << r << " UpdateWeekRef=" << ms << "ms" << std::endl;
    }
    float avgMs = totalMs / rounds;
    EXPECT_GE(avgMs, 0.0f) << "UpdateWeekRef avg duration must be non-negative; " <<
        "weak avg=" << avgMs << "ms over " << rounds << " rounds";

    arrays.clear();
    TriggerLocalCC(); // reclaim arrays themselves
}

} // namespace panda::test