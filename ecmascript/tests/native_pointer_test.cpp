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

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <unordered_set>

#include "common_components/taskpool/taskpool.h"
#include "ecmascript/ecma_string.h"
#include "ecmascript/ecma_vm.h"
#include "ecmascript/global_env.h"
#include "ecmascript/js_arraybuffer.h"
#include "ecmascript/js_function.h"
#include "ecmascript/js_handle.h"
#include "ecmascript/js_regexp.h"
#include "ecmascript/js_object-inl.h"
#include "ecmascript/mem/concurrent_marker.h"
#include "ecmascript/mem/cms_mem/slot_free_segment.h"
#include "ecmascript/mem/mem.h"
#include "ecmascript/mem/native_pointer_space.h"
#include "ecmascript/object_factory.h"
#include "ecmascript/tests/test_helper.h"

using namespace panda::ecmascript;

namespace panda::test {
namespace {
std::atomic<int> g_callbackCount {0};
std::mutex g_callbackMutex;
std::condition_variable g_callbackCV;

void CountNativePointerCallback([[maybe_unused]] void *env, [[maybe_unused]] void *externalPointer,
                                [[maybe_unused]] void *data)
{
    {
        std::lock_guard<std::mutex> lock(g_callbackMutex);
        g_callbackCount.fetch_add(1, std::memory_order_relaxed);
    }
    g_callbackCV.notify_all();
}

void ResetNativePointerCallbackCount()
{
    std::lock_guard<std::mutex> lock(g_callbackMutex);
    g_callbackCount.store(0, std::memory_order_relaxed);
}

bool WaitForNativePointerCallbackCount(int count)
{
    std::unique_lock<std::mutex> lock(g_callbackMutex);
    return g_callbackCV.wait_for(lock, std::chrono::seconds(1), [count] {
        return g_callbackCount.load(std::memory_order_relaxed) >= count;
    });
}

constexpr bool ShouldRunNativePointerSpaceTest()
{
    return !G_USE_CMS_GC;
}

JSHandle<JSRegExp> CreateRegExpForNativePointerTest(JSThread *thread)
{
    auto *factory = thread->GetEcmaVM()->GetFactory();
    JSHandle<GlobalEnv> env = thread->GetEcmaVM()->GetGlobalEnv();
    JSHandle<JSTaggedValue> proto = env->GetObjectFunctionPrototype();
    JSHandle<JSHClass> regexpClass = factory->NewEcmaHClass(JSRegExp::SIZE, JSType::JS_REG_EXP, proto);
    JSHandle<JSRegExp> regexp = JSHandle<JSRegExp>::Cast(factory->NewJSObject(regexpClass));
    regexp->SetByteCodeBuffer(thread, JSTaggedValue::Undefined());
    regexp->SetOriginalSource(thread, JSTaggedValue::Undefined());
    regexp->SetGroupName(thread, JSTaggedValue::Undefined());
    regexp->SetOriginalFlags(thread, JSTaggedValue(0));
    regexp->SetLength(0);
    return regexp;
}
}  // namespace

class NativePointerTest : public BaseTestWithScope<false> {
};

class NativePointerVerificationTest : public BaseTestWithOutScope {
};

HWTEST_F_L0(NativePointerTest, Print)
{
    char array[] = "Hello World!";
    ObjectFactory *factory = thread->GetEcmaVM()->GetFactory();
    JSHandle<GlobalEnv> env = thread->GetEcmaVM()->GetGlobalEnv();
    JSHandle<EcmaString> str = factory->NewFromASCII(array);
    EXPECT_TRUE(*str != nullptr);

    JSHandle<JSFunction> jsFunction = factory->NewJSFunction(env);
    EXPECT_TRUE(*jsFunction != nullptr);

    JSHandle<Method> target = factory->NewMethodForNativeFunction(nullptr);
    jsFunction->SetMethod(thread, target.GetTaggedValue());

    ASSERT_EQ(Method::Cast(target.GetTaggedValue().GetTaggedObject()), jsFunction->GetCallTarget(thread));
}

HWTEST_F_L0(NativePointerTest, ToString)
{
    ObjectFactory *factory = thread->GetEcmaVM()->GetFactory();
    size_t length = 4;
    uint8_t *data = static_cast<uint8_t *>(thread->GetEcmaVM()->GetNativeAreaAllocator()->AllocateBuffer(length));
    if (memset_s(data, length, 0, length) != EOK) {
        LOG_ECMA(FATAL) << "this branch is unreachable";
        UNREACHABLE();
    }
    std::stringstream expected;
    void *formBuffer = thread->GetEcmaVM()->GetNativeAreaAllocator()->AllocateBuffer(length);
    JSHandle<JSNativePointer> fromNativePointer =
        factory->NewJSNativePointer(formBuffer, nullptr, reinterpret_cast<void *>(data));
    expected << "[External: " << std::hex << formBuffer << "]";
    EcmaStringAccessor actual = EcmaStringAccessor(fromNativePointer->ToString(thread));
    EXPECT_EQ(actual.ToCString(thread).c_str(), expected.str());
}

HWTEST_F_L0(NativePointerTest, NewNativePointerUsesDedicatedSpace)
{
    if (!ShouldRunNativePointerSpaceTest()) {
        return;
    }

    [[maybe_unused]] EcmaHandleScope scope(thread);
    auto nativePointer = thread->GetEcmaVM()->GetFactory()->NewJSNativePointer(nullptr, nullptr, nullptr, true);
    EXPECT_TRUE(Region::ObjectAddressToRange(*nativePointer)->InNativePointerSpace());
    EXPECT_EQ(nativePointer->GetSize(), JSNativePointer::SIZE);
}

HWTEST_F_L0(NativePointerTest, NativePointerSpaceTypeMapping)
{
    auto *heap = const_cast<Heap *>(thread->GetEcmaVM()->GetHeap());
    if (!ShouldRunNativePointerSpaceTest()) {
        return;
    }

    EXPECT_EQ(heap->GetSweepableSpaceWithType(MemSpaceType::NATIVE_POINTER_SPACE),
              static_cast<SweepableSpace *>(heap->GetNativePointerSpace()));
    EXPECT_EQ(GetSweepableSpace(nullptr), nullptr);
}

HWTEST_F_L0(NativePointerTest, CollectDeadCallbacksWithoutFromRegion)
{
    auto *heap = const_cast<Heap *>(thread->GetEcmaVM()->GetHeap());
    if (!ShouldRunNativePointerSpaceTest()) {
        return;
    }

    size_t nativeBindingSizeBefore = heap->GetNativeBindingSize();
    heap->GetNativePointerSpace()->CollectDeadCallbacks();
    heap->GetNativePointerSpace()->ProcessDeadCallbacks();
    EXPECT_EQ(heap->GetNativeBindingSize(), nativeBindingSizeBefore);
    EXPECT_TRUE(thread->GetEcmaVM()->GetConcurrentNativePointerCallbacks().empty());
    EXPECT_EQ(thread->GetEcmaVM()->GetAsyncNativePointerCallbacksPack().GetNumCallBacks(), 0U);
}

HWTEST_F_L0(NativePointerTest, FullGCCompactsLiveNativePointer)
{
    auto *heap = const_cast<Heap *>(thread->GetEcmaVM()->GetHeap());
    if (!ShouldRunNativePointerSpaceTest()) {
        return;
    }

    ResetNativePointerCallbackCount();
    int externalPointer = 1;
    {
        [[maybe_unused]] EcmaHandleScope scope(thread);
        auto nativePointer = thread->GetEcmaVM()->GetFactory()->NewJSNativePointer(
            &externalPointer, CountNativePointerCallback, nullptr, false, 16);
        uintptr_t addressBefore = reinterpret_cast<uintptr_t>(*nativePointer);
        heap->CollectGarbage(TriggerGCType::FULL_GC, GCReason::OTHER);
        EXPECT_TRUE(Region::ObjectAddressToRange(*nativePointer)->InNativePointerSpace());
        EXPECT_NE(reinterpret_cast<uintptr_t>(*nativePointer), addressBefore);
    }
    EXPECT_EQ(g_callbackCount.load(std::memory_order_relaxed), 0);
}

HWTEST_F_L0(NativePointerTest, FullGCUpdatesDuplicateNonNativeReference)
{
    auto *heap = const_cast<Heap *>(thread->GetEcmaVM()->GetHeap());
    if (!ShouldRunNativePointerSpaceTest()) {
        return;
    }

    ObjectFactory *factory = thread->GetEcmaVM()->GetFactory();
    {
        [[maybe_unused]] EcmaHandleScope scope(thread);
        JSHandle<TaggedArray> target = factory->NewTaggedArray(1);
        JSHandle<TaggedArray> references = factory->NewTaggedArray(2);
        references->Set(thread, 0, target.GetTaggedValue());
        references->Set(thread, 1, target.GetTaggedValue());
        uintptr_t addressBefore = reinterpret_cast<uintptr_t>(*target);
        heap->CollectGarbage(TriggerGCType::FULL_GC, GCReason::OTHER);
        EXPECT_NE(reinterpret_cast<uintptr_t>(*target), addressBefore);
        EXPECT_EQ(references->Get(thread, 0), references->Get(thread, 1));
    }
}

HWTEST_F_L0(NativePointerTest, FullGCInvokesDeadNativePointerCallback)
{
    auto *heap = const_cast<Heap *>(thread->GetEcmaVM()->GetHeap());
    if (!ShouldRunNativePointerSpaceTest()) {
        return;
    }

    ResetNativePointerCallbackCount();
    int externalPointer = 1;
    {
        [[maybe_unused]] EcmaHandleScope scope(thread);
        thread->GetEcmaVM()->GetFactory()->NewJSNativePointer(
            &externalPointer, CountNativePointerCallback, nullptr, false, 16);
    }

    heap->CollectGarbage(TriggerGCType::FULL_GC, GCReason::OTHER);
    thread->GetEcmaVM()->GetAsyncNativePointerCallbacksPack().ProcessAll("NativePointerDeadCallbackTest");
    EXPECT_EQ(g_callbackCount.load(std::memory_order_relaxed), 1);
}

HWTEST_F_L0(NativePointerTest, OldGCCompactsLiveNativePointer)
{
    auto *heap = const_cast<Heap *>(thread->GetEcmaVM()->GetHeap());
    if (!ShouldRunNativePointerSpaceTest()) {
        return;
    }

    ResetNativePointerCallbackCount();
    int externalPointer = 1;
    {
        [[maybe_unused]] EcmaHandleScope scope(thread);
        auto nativePointer = thread->GetEcmaVM()->GetFactory()->NewJSNativePointer(
            &externalPointer, CountNativePointerCallback, nullptr, false, 16);
        uintptr_t addressBefore = reinterpret_cast<uintptr_t>(*nativePointer);
        heap->CollectGarbage(TriggerGCType::OLD_GC, GCReason::OTHER);
        EXPECT_TRUE(Region::ObjectAddressToRange(*nativePointer)->InNativePointerSpace());
        EXPECT_NE(reinterpret_cast<uintptr_t>(*nativePointer), addressBefore);
    }
    EXPECT_EQ(g_callbackCount.load(std::memory_order_relaxed), 0);
}

HWTEST_F_L0(NativePointerTest, OldGCInvokesDeadNativePointerCallback)
{
    auto *heap = const_cast<Heap *>(thread->GetEcmaVM()->GetHeap());
    if (!ShouldRunNativePointerSpaceTest()) {
        return;
    }

    ResetNativePointerCallbackCount();
    int externalPointer = 1;
    {
        [[maybe_unused]] EcmaHandleScope scope(thread);
        thread->GetEcmaVM()->GetFactory()->NewJSNativePointer(
            &externalPointer, CountNativePointerCallback, nullptr, false, 16);
    }

    heap->CollectGarbage(TriggerGCType::OLD_GC, GCReason::OTHER);
    EXPECT_EQ(g_callbackCount.load(std::memory_order_relaxed), 1);
}

HWTEST_F_L0(NativePointerTest, LocalCCCompactsLiveNativePointer)
{
    auto *heap = const_cast<Heap *>(thread->GetEcmaVM()->GetHeap());
    if (!ShouldRunNativePointerSpaceTest()) {
        return;
    }

    ResetNativePointerCallbackCount();
    int externalPointer = 1;
    {
        [[maybe_unused]] EcmaHandleScope scope(thread);
        auto nativePointer = thread->GetEcmaVM()->GetFactory()->NewJSNativePointer(
            &externalPointer, CountNativePointerCallback, nullptr, false, 16);
        uintptr_t addressBefore = reinterpret_cast<uintptr_t>(*nativePointer);
        if (heap->TryTriggerCCMarking(MarkReason::OTHER)) {
            heap->CollectGarbageFromCCMark(GCReason::OTHER);
            EXPECT_TRUE(thread->IsConcurrentCopying());
            heap->WaitAndHandleCCFinished();
            EXPECT_TRUE(Region::ObjectAddressToRange(*nativePointer)->InNativePointerSpace());
            EXPECT_NE(reinterpret_cast<uintptr_t>(*nativePointer), addressBefore);
        }
    }
    EXPECT_EQ(g_callbackCount.load(std::memory_order_relaxed), 0);
}

HWTEST_F_L0(NativePointerTest, LocalCCInvokesDeadNativePointerCallback)
{
    auto *heap = const_cast<Heap *>(thread->GetEcmaVM()->GetHeap());
    if (!ShouldRunNativePointerSpaceTest()) {
        return;
    }

    ResetNativePointerCallbackCount();
    int externalPointer = 1;
    {
        [[maybe_unused]] EcmaHandleScope scope(thread);
        thread->GetEcmaVM()->GetFactory()->NewJSNativePointer(
            &externalPointer, CountNativePointerCallback, nullptr, false, 16);
    }

    if (heap->TryTriggerCCMarking(MarkReason::OTHER)) {
        heap->CollectGarbageFromCCMark(GCReason::OTHER);
        EXPECT_TRUE(thread->IsConcurrentCopying());
        heap->WaitAndHandleCCFinished();
    }
    EXPECT_EQ(g_callbackCount.load(std::memory_order_relaxed), 1);
}

HWTEST_F_L0(NativePointerTest, LocalCCDefersCleanCallbackToSafepoint)
{
    auto* heap = const_cast<Heap*>(thread->GetEcmaVM()->GetHeap());
    if (!ShouldRunNativePointerSpaceTest()) {
        return;
    }

    ResetNativePointerCallbackCount();
    int externalPointer = 1;
    {
        [[maybe_unused]] EcmaHandleScope scope(thread);
        thread->GetEcmaVM()->GetFactory()->NewJSNativePointer(
            &externalPointer, CountNativePointerCallback, nullptr, false, 16);
    }

    if (heap->TryTriggerCCMarking(MarkReason::OTHER)) {
        heap->CollectGarbageFromCCMark(GCReason::OTHER);
        ASSERT_TRUE(thread->IsConcurrentCopying());
        heap->WaitAndHandleCCFinished(false);
        EXPECT_EQ(g_callbackCount.load(std::memory_order_relaxed), 0);
        thread->CheckSafepoint();
        ASSERT_TRUE(WaitForNativePointerCallbackCount(1));
    }
    EXPECT_EQ(g_callbackCount.load(std::memory_order_relaxed), 1);
}

HWTEST_F_L0(NativePointerTest, ClearNativePointerListHandlesDeferredCleanCallback)
{
    auto *heap = const_cast<Heap *>(thread->GetEcmaVM()->GetHeap());
    if (!ShouldRunNativePointerSpaceTest()) {
        return;
    }

    ResetNativePointerCallbackCount();
    int externalPointer = 1;
    {
        [[maybe_unused]] EcmaHandleScope scope(thread);
        thread->GetEcmaVM()->GetFactory()->NewJSNativePointer(
            &externalPointer, CountNativePointerCallback, nullptr, false, 16);
    }

    if (heap->TryTriggerCCMarking(MarkReason::OTHER)) {
        heap->CollectGarbageFromCCMark(GCReason::OTHER);
        ASSERT_TRUE(thread->IsConcurrentCopying());
        heap->WaitAndHandleCCFinished(false);
        EXPECT_EQ(g_callbackCount.load(std::memory_order_relaxed), 0);
        heap->WaitAllTasksFinished();
        heap->ClearNativePointerList();
        ASSERT_TRUE(WaitForNativePointerCallbackCount(1));
    }
    EXPECT_EQ(g_callbackCount.load(std::memory_order_relaxed), 1);
}

HWTEST_F_L0(NativePointerTest, FullGCKeepsLiveNativeBindingSize)
{
    auto *heap = const_cast<Heap *>(thread->GetEcmaVM()->GetHeap());
    if (!ShouldRunNativePointerSpaceTest()) {
        return;
    }

    constexpr uint32_t bindingSize = 16;
    size_t bindingSizeBefore = heap->GetNativeBindingSize();
    {
        [[maybe_unused]] EcmaHandleScope scope(thread);
        thread->GetEcmaVM()->GetFactory()->NewJSNativePointer(
            nullptr, nullptr, nullptr, false, bindingSize);
    }
    heap->CollectGarbage(TriggerGCType::FULL_GC, GCReason::OTHER);
    EXPECT_EQ(heap->GetNativeBindingSize(), bindingSizeBefore);
}

HWTEST_F_L0(NativePointerTest, FullGCInvokesAsyncAndConcurrentDeadCallbacks)
{
    auto* heap = const_cast<Heap*>(thread->GetEcmaVM()->GetHeap());
    if (!ShouldRunNativePointerSpaceTest()) {
        return;
    }

    ResetNativePointerCallbackCount();
    size_t bindingSizeBefore = heap->GetNativeBindingSize();
    int asyncExternalPointer = 1;
    int concurrentExternalPointer = 2;
    {
        [[maybe_unused]] EcmaHandleScope scope(thread);
        auto* factory = thread->GetEcmaVM()->GetFactory();
        factory->NewJSNativePointer(&asyncExternalPointer, CountNativePointerCallback, nullptr, false, 16);
        factory->NewJSNativePointer(
            &concurrentExternalPointer, CountNativePointerCallback, nullptr, false, 16, Concurrent::YES);
    }

    heap->CollectGarbage(TriggerGCType::FULL_GC, GCReason::OTHER);
    thread->GetEcmaVM()->GetAsyncNativePointerCallbacksPack().ProcessAll("NativePointerRouteCallbacksTest");
    ASSERT_TRUE(WaitForNativePointerCallbackCount(2));
    EXPECT_EQ(g_callbackCount.load(std::memory_order_relaxed), 2);
    EXPECT_EQ(heap->GetNativeBindingSize(), bindingSizeBefore);
}

HWTEST_F_L0(NativePointerTest, FullGCDeadPointerWithoutCallbackOrBinding)
{
    auto* heap = const_cast<Heap*>(thread->GetEcmaVM()->GetHeap());
    if (!ShouldRunNativePointerSpaceTest()) {
        return;
    }

    size_t bindingSizeBefore = heap->GetNativeBindingSize();
    {
        [[maybe_unused]] EcmaHandleScope scope(thread);
        thread->GetEcmaVM()->GetFactory()->NewJSNativePointer(nullptr, nullptr, nullptr, false, 0);
    }

    heap->CollectGarbage(TriggerGCType::FULL_GC, GCReason::OTHER);
    EXPECT_EQ(thread->GetEcmaVM()->GetAsyncNativePointerCallbacksPack().GetNumCallBacks(), 0U);
    EXPECT_TRUE(thread->GetEcmaVM()->GetConcurrentNativePointerCallbacks().empty());
    EXPECT_EQ(heap->GetNativeBindingSize(), bindingSizeBefore);
}

HWTEST_F_L0(NativePointerTest, ArrayBufferResizeUpdatesNativeBindingSize)
{
    auto *heap = const_cast<Heap *>(thread->GetEcmaVM()->GetHeap());
    if (!ShouldRunNativePointerSpaceTest()) {
        return;
    }

    ObjectFactory *factory = thread->GetEcmaVM()->GetFactory();
    size_t bindingSizeBefore = heap->GetNativeBindingSize();
    JSHandle<JSArrayBuffer> arrayBuffer = factory->NewJSArrayBuffer(8);
    factory->NewJSArrayBufferData(arrayBuffer, 16);
    EXPECT_EQ(heap->GetNativeBindingSize(), bindingSizeBefore + 16);

    factory->NewJSArrayBufferData(arrayBuffer, 4);
    EXPECT_EQ(heap->GetNativeBindingSize(), bindingSizeBefore + 4);
}

HWTEST_F_L0(NativePointerTest, RegExpByteCodeResizeUpdatesNativeBindingSize)
{
    auto *heap = const_cast<Heap *>(thread->GetEcmaVM()->GetHeap());
    if (!ShouldRunNativePointerSpaceTest()) {
        return;
    }

    ObjectFactory *factory = thread->GetEcmaVM()->GetFactory();
    size_t bindingSizeBefore = heap->GetNativeBindingSize();
    JSHandle<JSRegExp> regexp = CreateRegExpForNativePointerTest(thread);
    uint8_t byteCode[16] = {0};

    factory->NewJSRegExpByteCodeData(regexp, byteCode, sizeof(byteCode) / 2);
    EXPECT_EQ(heap->GetNativeBindingSize(), bindingSizeBefore + 8);

    factory->NewJSRegExpByteCodeData(regexp, byteCode, sizeof(byteCode));
    EXPECT_EQ(heap->GetNativeBindingSize(), bindingSizeBefore + 16);

    factory->NewJSRegExpByteCodeData(regexp, byteCode, sizeof(byteCode) / 4);
    EXPECT_EQ(heap->GetNativeBindingSize(), bindingSizeBefore + 4);
}

HWTEST_F_L0(NativePointerTest, FullGCCollectsCallbacksWhenParallelGCDisabled)
{
    auto *heap = const_cast<Heap *>(thread->GetEcmaVM()->GetHeap());
    if (!ShouldRunNativePointerSpaceTest()) {
        return;
    }

    ResetNativePointerCallbackCount();
    int externalPointer = 1;
    {
        [[maybe_unused]] EcmaHandleScope scope(thread);
        thread->GetEcmaVM()->GetFactory()->NewJSNativePointer(
            &externalPointer, CountNativePointerCallback, nullptr, false, 16);
    }

    heap->DisableParallelGC();
    heap->CollectGarbage(TriggerGCType::FULL_GC, GCReason::OTHER);
    common::Taskpool::GetCurrentTaskpool()->Initialize();
    heap->EnableParallelGC();
    thread->GetEcmaVM()->GetAsyncNativePointerCallbacksPack().ProcessAll("NativePointerSerialCollectTest");
    EXPECT_EQ(g_callbackCount.load(std::memory_order_relaxed), 1);
}

HWTEST_F_L0(NativePointerTest, FullGCUsesMultipleNativePointerCollectTasks)
{
    auto *heap = const_cast<Heap *>(thread->GetEcmaVM()->GetHeap());
    if (!ShouldRunNativePointerSpaceTest()) {
        return;
    }

    ResetNativePointerCallbackCount();
    ObjectFactory *factory = thread->GetEcmaVM()->GetFactory();
    std::unordered_set<Region *> regions;
    [[maybe_unused]] EcmaHandleScope scope(thread);
    JSHandle<JSNativePointer> livePointer = factory->NewJSNativePointer(
        nullptr, nullptr, nullptr, false, 0);
    regions.insert(Region::ObjectAddressToRange(*livePointer));

    auto *nativePointerSpace = heap->GetNativePointerSpace();
    while (regions.size() < 17) {
        uintptr_t address = nativePointerSpace->Allocate();
        ASSERT_NE(address, 0U);
        SlotFreeSegment::FillFreeSegment(address, JSNativePointer::SIZE);
        regions.insert(Region::ObjectAddressToRange(address));
    }
    ASSERT_GE(regions.size(), 17U);

    heap->CollectGarbage(TriggerGCType::FULL_GC, GCReason::OTHER);
    EXPECT_TRUE(Region::ObjectAddressToRange(*livePointer)->InNativePointerSpace());
    EXPECT_EQ(g_callbackCount.load(std::memory_order_relaxed), 0);
}

HWTEST_F_L0(NativePointerTest, ClearNativePointerListInvokesRemainingCallback)
{
    auto* heap = const_cast<Heap*>(thread->GetEcmaVM()->GetHeap());
    if (!ShouldRunNativePointerSpaceTest()) {
        return;
    }

    ResetNativePointerCallbackCount();
    size_t bindingSizeBefore = heap->GetNativeBindingSize();
    int externalPointer = 1;
    {
        [[maybe_unused]] EcmaHandleScope scope(thread);
        thread->GetEcmaVM()->GetFactory()->NewJSNativePointer(
            &externalPointer, CountNativePointerCallback, nullptr, false, 16);
    }
    heap->ClearNativePointerList();
    EXPECT_EQ(g_callbackCount.load(std::memory_order_relaxed), 1);
    EXPECT_EQ(heap->GetNativeBindingSize(), bindingSizeBefore);
}

HWTEST_F_L0(NativePointerTest, ClearNativePointerListSkipsNullCallback)
{
    auto *heap = const_cast<Heap *>(thread->GetEcmaVM()->GetHeap());
    if (!ShouldRunNativePointerSpaceTest()) {
        return;
    }

    ResetNativePointerCallbackCount();
    size_t bindingSizeBefore = heap->GetNativeBindingSize();
    int externalPointer = 1;
    {
        [[maybe_unused]] EcmaHandleScope scope(thread);
        thread->GetEcmaVM()->GetFactory()->NewJSNativePointer(
            &externalPointer, nullptr, nullptr, false, 16);
    }
    heap->ClearNativePointerList();
    EXPECT_EQ(g_callbackCount.load(std::memory_order_relaxed), 0);
    EXPECT_EQ(heap->GetNativeBindingSize(), bindingSizeBefore);
}

HWTEST_F_L0(NativePointerTest, GlobalNativeSizeDoesNotGrowAfterReset)
{
    auto *heap = const_cast<Heap *>(thread->GetEcmaVM()->GetHeap());
    if (!ShouldRunNativePointerSpaceTest()) {
        return;
    }

    heap->ResetNativeSizeAfterLastGC();
    EXPECT_FALSE(heap->GlobalNativeSizeLargerToTriggerGC());

    size_t nativeBindingSize = heap->GetNativeBindingSize();
    heap->ResetNativeBindingSize();
    EXPECT_FALSE(heap->GlobalNativeSizeLargerToTriggerGC());
    heap->IncreaseNativeBindingSize(nativeBindingSize);
}

HWTEST_F_L0(NativePointerVerificationTest, YoungGCVerifiesYoungObjectReferencingNativePointer)
{
    JSRuntimeOptions options;
    options.SetEnableForceGC(false);
    options.SetEnableHeapVerify(true);
    options.SetArkProperties(options.GetArkProperties() | ArkProperties::ENABLE_HEAP_VERIFY);
    auto *vm = JSNApi::CreateEcmaVM(options);
    ASSERT_TRUE(vm != nullptr);
    auto *thread = vm->GetJSThread();
    thread->ManagedCodeBegin();
    {
        EcmaHandleScope scope(thread);
        auto arrayBuffer = vm->GetFactory()->NewJSArrayBuffer(8);
        EXPECT_EQ(vm->GetHeap()->GetMarkType(), MarkType::MARK_YOUNG);
        EXPECT_TRUE(vm->GetHeap()->ShouldVerifyHeap());
        EXPECT_TRUE(Region::ObjectAddressToRange(*arrayBuffer)->InYoungSpace());
        vm->GetHeap()->CollectGarbage(TriggerGCType::YOUNG_GC, GCReason::OTHER);
        EXPECT_TRUE(Region::ObjectAddressToRange(
            JSNativePointer::Cast(arrayBuffer->GetArrayBufferData(thread).GetTaggedObject()))
            ->InNativePointerSpace());
    }
    thread->ManagedCodeEnd();
    JSNApi::DestroyJSVM(vm);
}

HWTEST_F_L0(NativePointerVerificationTest, ConcurrentFullMarkClearsNativePointerMark)
{
    JSRuntimeOptions options;
    options.SetEnableForceGC(true);
    options.SetEnableHeapVerify(true);
    options.SetArkProperties(options.GetArkProperties() | ArkProperties::ENABLE_HEAP_VERIFY |
        ArkProperties::PARALLEL_GC | ArkProperties::CONCURRENT_MARK);
    auto *vm = JSNApi::CreateEcmaVM(options);
    ASSERT_TRUE(vm != nullptr);
    auto *heap = vm->GetHeap();
    auto *thread = vm->GetJSThread();
    if (!heap->CheckCanTriggerConcurrentMarking()) {
        JSNApi::DestroyJSVM(vm);
        return;
    }
    thread->ManagedCodeBegin();
    {
        EcmaHandleScope scope(thread);
        auto arrayBuffer = vm->GetFactory()->NewJSArrayBuffer(8);
        auto *nativePointer = JSNativePointer::Cast(arrayBuffer->GetArrayBufferData(thread).GetTaggedObject());
        ASSERT_TRUE(Region::ObjectAddressToRange(nativePointer)->InNativePointerSpace());

        heap->SetMarkType(MarkType::MARK_FULL);
        heap->TriggerConcurrentMarking(MarkReason::OTHER);
        heap->WaitConcurrentMarkingFinished();
        heap->GetConcurrentMarker()->HandleMarkingFinished(GCReason::OTHER);
        heap->WaitClearTaskFinished();

        nativePointer = JSNativePointer::Cast(arrayBuffer->GetArrayBufferData(thread).GetTaggedObject());
        EXPECT_FALSE(Region::ObjectAddressToRange(nativePointer)->Test(nativePointer));

        heap->CollectGarbage(TriggerGCType::YOUNG_GC, GCReason::OTHER);
        EXPECT_TRUE(Region::ObjectAddressToRange(
            JSNativePointer::Cast(arrayBuffer->GetArrayBufferData(thread).GetTaggedObject()))
            ->InNativePointerSpace());
    }
    thread->ManagedCodeEnd();
    JSNApi::DestroyJSVM(vm);
}
}  // namespace panda::test
