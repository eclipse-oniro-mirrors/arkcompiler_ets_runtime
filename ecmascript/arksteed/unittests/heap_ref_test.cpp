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

#include <chrono>
#include <condition_variable>
#include <future>
#include <mutex>
#include <thread>

#include "ecmascript/arksteed/arksteed_heap_broker.h"
#include "ecmascript/ic/ic_handler.h"
#include "ecmascript/jit/jit_task.h"
#include "ecmascript/tests/test_helper.h"

namespace panda::ecmascript::arksteed
{

// Use a real compiler thread and sustaining handles. The host can collect between
// requests while the compiler is native; no test-only ref constructors bypass the broker.
class ArkSteedHeapRefTest : public testing::Test
{
  public:
    void SetUp() override
    {
        panda::test::TestHelper::CreateEcmaVMWithScope(vm_, thread_, scope_);
        vm_->SetEnableForceGC(false);
        vm_->GetJSOptions().SetCompilerEnableLiteCG(true);
        Jit::GetInstance()->PreFork();
        auto *factory = vm_->GetFactory();
        function_ = factory->NewJSFunction(vm_->GetGlobalEnv());
        auto lexicalEnv = factory->NewLexicalEnv(0);
        lexicalEnv->SetGlobalEnv(thread_, vm_->GetGlobalEnv().GetTaggedValue());
        function_->SetLexicalEnv(thread_, lexicalEnv.GetTaggedValue());
        std::promise<void> ready;
        auto initialized = ready.get_future();
        worker_ = std::thread([this, &ready] {
            auto *jitVM = JitVM::Create();
            jitVM->SetHostVM(thread_);
            auto *jitThread = static_cast<JitThread *>(jitVM->GetJSThreadNoCheck());
            CString name("heap-ref-test");
            {
                JitTask task(thread_, jitThread, Jit::GetInstance(), function_, CompilerTier(CompilerTier::Tier::FAST),
                             name, 0, JitCompileMode(JitCompileMode::Mode::SYNC));
                jitThread->SetCurrentTask(&task);
                std::unique_ptr<JitCompilationEnv> env;
                {
                    Jit::JitLockHolder lock(jitThread);
                    env = std::make_unique<JitCompilationEnv>(jitVM, vm_, function_, task.GetDependencies());
                }
                ArkSteedHeapBroker broker(jitThread, env.get());
                broker_ = &broker;
                env_ = env.get();
                ready.set_value();
                for (;;) {
                    std::function<void()> request;
                    {
                        std::unique_lock<std::mutex> lock(mutex_);
                        pending_.wait(lock, [this] { return stop_ || request_ != nullptr; });
                        if (stop_) {
                            break;
                        }
                        request = std::move(request_);
                        request_ = nullptr;
                    }
                    JitHeapAccessScope heapAccess(jitThread);
                    request();
                }
                jitThread->SetCurrentTask(nullptr);
            }
            jitVM->ReSetHostVM();
            JitVM::Destroy(jitVM);
        });
        ThreadNativeScope native(thread_);
        initialized.get();
    }

    void TearDown() override
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stop_ = true;
        }
        pending_.notify_one();
        {
            ThreadNativeScope native(thread_);
            worker_.join();
        }
        panda::test::TestHelper::DestroyEcmaVMWithScope(vm_, scope_);
    }

  protected:
    std::future<void> ScheduleCompiler(std::function<void()> request)
    {
        auto task = std::make_shared<std::packaged_task<void()>>(std::move(request));
        auto complete = task->get_future();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            request_ = [task] { (*task)(); };
        }
        pending_.notify_one();
        return complete;
    }

    void OnCompiler(std::function<void()> request)
    {
        auto complete = ScheduleCompiler(std::move(request));
        ThreadNativeScope native(thread_);
        complete.get();
    }

    EcmaVM *vm_{nullptr};
    JSThread *thread_{nullptr};
    EcmaHandleScope *scope_{nullptr};
    JSHandle<JSFunction> function_;
    ArkSteedHeapBroker *broker_{nullptr};
    JitCompilationEnv *env_{nullptr};

  private:
    std::thread worker_;
    std::mutex mutex_;
    std::condition_variable pending_;
    std::function<void()> request_;
    bool stop_{false};
};

TEST(ArkSteedHeapRefImmediateTest, NoHeapAccess)
{
    ArkSteedHeapBroker broker(nullptr, nullptr);
    DISALLOW_DEREF_HANDLE;
    for (auto value : {JSTaggedValue(42), JSTaggedValue(1.5), JSTaggedValue::True(), JSTaggedValue::False(),
                       JSTaggedValue::Null(), JSTaggedValue::Undefined(), JSTaggedValue::Hole()}) {
        auto ref = ArkSteedHeapRef::FromImmediate(value);
        EXPECT_TRUE(ref.IsImmediate());
        EXPECT_FALSE(ref.HasHandle());
        EXPECT_TRUE(ref.Equals(ArkSteedHeapRef::FromImmediate(value)));
        JSTaggedValue resolved;
        EXPECT_TRUE(broker.TryResolveRef(ref, &resolved));
        EXPECT_EQ(resolved, value);
        EXPECT_TRUE(broker.TryReadRef(ref, [value](JSTaggedValue read) { return value == read; }));
    }
    EXPECT_FALSE(ArkSteedHeapRef().Equals(ArkSteedHeapRef()));
    EXPECT_FALSE(
        ArkSteedHeapRef::FromImmediate(JSTaggedValue(1)).Equals(ArkSteedHeapRef::FromImmediate(JSTaggedValue(2))));
    JSTaggedValue output = JSTaggedValue::Hole();
    EXPECT_FALSE(broker.TryResolveRef({}, &output));
    EXPECT_TRUE(output.IsHole());
    EXPECT_FALSE(broker.TryResolveRef(ArkSteedHeapRef::FromImmediate(JSTaggedValue(1)), nullptr));
    EXPECT_FALSE(broker.TryReadRef({}, [](JSTaggedValue) {
        ADD_FAILURE();
        return true;
    }));
    ArkSteedHeapBroker::StoreHandlerData store;
    ArkSteedHeapBroker::LoadHandlerData load;
    auto handler = ArkSteedHeapRef::FromImmediate(JSTaggedValue(42));
    EXPECT_TRUE(broker.TryReadStoreHandler(handler, &store));
    EXPECT_TRUE(broker.TryReadLoadHandler(handler, &load));
    EXPECT_EQ(store.handlerInfo, 42U);
    EXPECT_EQ(load.handlerInfo, 42U);
    EXPECT_FALSE(broker.TryReadStoreHandler({}, &store));
    EXPECT_FALSE(broker.TryReadLoadHandler(ArkSteedHeapRef::FromImmediate(JSTaggedValue::Null()), &load));
    EXPECT_EQ(broker.Mode(), ArkSteedBrokerMode::SERIALIZED);
}

HWTEST_F_L0(ArkSteedHeapRefTest, CanonicalIdentityAndAutomaticScope)
{
    auto object = vm_->GetFactory()->NewEmptyJSObject(vm_->GetGlobalEnv());
    JSHandle<JSTaggedValue> anotherHandle(thread_, object.GetTaggedValue());
    auto different = vm_->GetFactory()->NewEmptyJSObject(vm_->GetGlobalEnv());
    OnCompiler([&] {
        DISALLOW_DEREF_HANDLE;
        auto ref = broker_->MakeObjectRef(object);
        auto duplicate = broker_->MakeObjectRef(anotherHandle);
        ASSERT_TRUE(ref.IsHeapObject());
        EXPECT_TRUE(ref.Equals(duplicate));
        EXPECT_EQ(env_->GetHeapConstantTable().size(), 1U);
        uint32_t index = JitCompilationEnv::INVALID_HEAP_CONSTANT_INDEX;
        ASSERT_TRUE(broker_->TryGetHeapConstantIndex(ref, &index));
        EXPECT_EQ(index, 0U);
        EXPECT_EQ(broker_->Mode(), ArkSteedBrokerMode::SERIALIZED);
        bool same = false;
        EXPECT_TRUE(broker_->TryIsSameObject(ref, anotherHandle, &same));
        EXPECT_TRUE(same);
        EXPECT_TRUE(broker_->TryIsSameObject(ref, different, &same));
        EXPECT_FALSE(same);
        EXPECT_FALSE(broker_->TryIsSameObject(ref, object, nullptr));
        EXPECT_FALSE(broker_->TryIsSameObject(ref, JSHandle<JSTaggedValue>(), &same));
        EXPECT_TRUE(broker_->TryReadRef(ref, [&](JSTaggedValue value) {
            EXPECT_TRUE(broker_->SerializingAllowed());
            // Assert scopes enforce these restrictions only in Debug builds.
            EXPECT_EQ(AllowHeapAlloc::IsAllowed(), !IS_ALLOW_CHECK);
            EXPECT_EQ(AllowGarbageCollection::IsAllowed(), !IS_ALLOW_CHECK);
            EXPECT_EQ(value, object.GetTaggedValue());
            auto nested = broker_->MakeObjectRef(anotherHandle);
            EXPECT_TRUE(nested.Equals(ref));
            EXPECT_TRUE(broker_->SerializingAllowed());
            return true;
        }));
        EXPECT_EQ(broker_->Mode(), ArkSteedBrokerMode::SERIALIZED);
        EXPECT_FALSE(broker_->TryReadRef(ref, [](JSTaggedValue) { return false; }));
        EXPECT_EQ(broker_->Mode(), ArkSteedBrokerMode::SERIALIZED);
        // Reading the index/type did not secretly allow handle dereference outside the callback.
        EXPECT_EQ(AllowDeRefHandle::IsAllowed(), !IS_ALLOW_CHECK);
        EXPECT_TRUE(broker_->TryReadRef(ref, [&](JSTaggedValue) {
            EXPECT_EQ(env_->RecordHeapConstant(anotherHandle), index);
            JitCompilationEnv::ConstantPoolHeapConstant key{0, 1, JitCompilationEnv::IN_SHARED_CONSTANTPOOL};
            EXPECT_EQ(env_->RecordHeapConstant(key, anotherHandle), index);
            EXPECT_EQ(env_->GetHeapConstantTable().size(), 1U);
            return true;
        }));
    });
}

HWTEST_F_L0(ArkSteedHeapRefTest, MovingGCAndTableGrowthPreserveIdentity)
{
    ArkSteedHeapRef original;
    uint32_t originalIndex;
    JSTaggedType oldAddress;
    // After this scope closes the broker's sustaining slot is the ONLY root of this array.
    {
        EcmaHandleScope temporary(thread_);
        auto array = vm_->GetFactory()->NewTaggedArray(8);
        oldAddress = array.GetTaggedType();
        OnCompiler([&] {
            original = broker_->MakeObjectRef(array);
            ASSERT_TRUE(broker_->TryGetHeapConstantIndex(original, &originalIndex));
        });
    }
    vm_->GetHeap()->CollectGarbage(TriggerGCType::YOUNG_GC);
    std::vector<JSHandle<TaggedArray>> arrays;
    for (size_t i = 0; i < 128; ++i) {
        arrays.push_back(vm_->GetFactory()->NewTaggedArray(1));
    }
    OnCompiler([&] {
        for (const auto &array : arrays) {
            ASSERT_TRUE(broker_->MakeObjectRef(array).IsHeapObject());
        }
        EXPECT_TRUE(broker_->TryReadRef(original, [&](JSTaggedValue value) {
            EXPECT_NE(value.GetRawData(), oldAddress);
            EXPECT_EQ(TaggedArray::Cast(value.GetTaggedObject())->GetLength(), 8U);
            EXPECT_TRUE(broker_->MakeObjectRef(value).Equals(original));
            EXPECT_EQ(env_->GetHeapConstantTable().size(), 129U);
            return true;
        }));
        DISALLOW_DEREF_HANDLE;
        uint32_t index;
        EXPECT_TRUE(broker_->TryGetHeapConstantIndex(original, &index));
        EXPECT_EQ(index, originalIndex);
        bool same = false;
        EXPECT_TRUE(broker_->TryIsSameObject(original, env_->GetHeapConstantHandle(index), &same));
        EXPECT_TRUE(same);
        EXPECT_TRUE(broker_->TryIsSameObject(original, arrays.front(), &same));
        EXPECT_FALSE(same);
        JSType type;
        EXPECT_TRUE(broker_->TryGetObjectType(original, &type));
        EXPECT_EQ(type, JSType::TAGGED_ARRAY);
        EXPECT_NE(broker_->TryGetObjectHClass(original), nullptr);
        EXPECT_FALSE(broker_->SerializingAllowed());
        EXPECT_EQ(AllowDeRefHandle::IsAllowed(), !IS_ALLOW_CHECK);
    });
}

HWTEST_F_L0(ArkSteedHeapRefTest, TypesAndCompilationOwnership)
{
    auto string = vm_->GetFactory()->NewFromASCII("heap-ref");
    auto symbol = vm_->GetFactory()->NewEmptySymbol();
    auto cell = vm_->GetFactory()->NewProtoChangeMarker();
    OnCompiler([&] {
        auto stringRef = broker_->MakeObjectRef(string);
        auto symbolRef = broker_->MakeObjectRef(symbol);
        auto cellRef = broker_->MakeObjectRef(cell);
        auto functionRef = broker_->MakeObjectRef(function_);
        DISALLOW_DEREF_HANDLE;
        EXPECT_TRUE(stringRef.IsString());
        EXPECT_TRUE(symbolRef.IsSymbol());
        EXPECT_TRUE(cellRef.IsProtoCell());
        EXPECT_TRUE(functionRef.IsJSFunction());
        EXPECT_TRUE(broker_->IsSameFunction(functionRef));
        EXPECT_FALSE(stringRef.Equals(symbolRef));
        EXPECT_TRUE(broker_->TryReadRef(functionRef, [&](JSTaggedValue) {
            EXPECT_FALSE(broker_->MakeHClassRef(string.GetTaggedValue()).IsSafeForCompile());
            EXPECT_FALSE(broker_->MakeNameRef(function_.GetTaggedValue()).IsSafeForCompile());
            EXPECT_FALSE(broker_->MakeProtoCellRef(symbol.GetTaggedValue()).IsSafeForCompile());
            EXPECT_FALSE(ArkSteedHeapRef::FromImmediate(string.GetTaggedValue()).IsSafeForCompile());
            auto hclassRef = broker_->MakeHClassRef(JSTaggedValue(function_->GetClass()));
            EXPECT_TRUE(hclassRef.IsJSHClass());
            JitCompilationEnv otherEnv(env_->GetEcmaVM(), vm_, function_, nullptr);
            ArkSteedHeapBroker otherBroker(env_->GetJSThread(), &otherEnv);
            // Both tables give their first object index zero; they are still different identities.
            auto otherRef = otherBroker.MakeObjectRef(string);
            EXPECT_FALSE(otherRef.Equals(stringRef));
            uint32_t index;
            JSTaggedValue value;
            bool same;
            EXPECT_FALSE(broker_->TryGetHeapConstantIndex(otherRef, &index));
            EXPECT_FALSE(broker_->TryResolveRef(otherRef, &value));
            EXPECT_FALSE(broker_->TryIsSameObject(otherRef, string, &same));
            EXPECT_FALSE(broker_->TryReadRef(otherRef, [](JSTaggedValue) {
                ADD_FAILURE();
                return true;
            }));
            return true;
        }));
    });
}

HWTEST_F_L0(ArkSteedHeapRefTest, ConcreteReadsDoNotParkOrLock)
{
    auto string = vm_->GetFactory()->NewFromASCII("heap-ref");
    auto object = vm_->GetFactory()->NewEmptyJSObject(vm_->GetGlobalEnv());
    // A broker read must not acquire the old JIT mutex. This deliberately does
    // not request a cooperative stop: the compiler is allowed to keep reading.
    LockHolder legacyLock(*thread_->GetJitLock());
    OnCompiler([&] {
        DISALLOW_DEREF_HANDLE;
        auto ref = broker_->MakeObjectRef(string);
        uint32_t length = 0;
        uint16_t character = 0;
        JSType type;
        bool callable = true;
        ASSERT_TRUE(broker_->TryGetStringLength(ref, &length));
        EXPECT_EQ(length, 8U);
        ASSERT_TRUE(broker_->TryGetStringChar(ref, 5, &character));
        EXPECT_EQ(character, 'r');
        EXPECT_FALSE(broker_->TryGetStringChar(ref, length, &character));
        EXPECT_FALSE(broker_->TryGetStringLength(ref, nullptr));
        EXPECT_TRUE(broker_->TryGetObjectType(ref, &type, &callable));
        EXPECT_FALSE(callable);
        EXPECT_GE(type, JSType::STRING_FIRST);
        EXPECT_LE(type, JSType::STRING_LAST);

        auto objectRef = broker_->MakeObjectRef(object);
        EXPECT_FALSE(broker_->TryGetStringLength(objectRef, &length));
        auto *hclass = broker_->TryGetObjectHClass(objectRef);
        ASSERT_NE(hclass, nullptr);
        auto hclassRef = broker_->MakeHClassRef(hclass);
        EXPECT_TRUE(hclassRef.IsJSHClass());
        EXPECT_EQ(broker_->TryGetHClass(hclassRef), hclass);
        EXPECT_EQ(broker_->TryGetHClass(objectRef), nullptr);
        EXPECT_EQ(broker_->TryGetObjectHClass({}), nullptr);
        EXPECT_FALSE(broker_->TryGetObjectType({}, &type));
        EXPECT_FALSE(broker_->TryGetStringLength({}, &length));
        EXPECT_FALSE(broker_->CanCallFunction(objectRef, 0));

        uint32_t objectSize = 0;
        auto initial = broker_->GetEmptyObjectHClass(&objectSize);
        EXPECT_TRUE(initial.IsJSHClass());
        EXPECT_GT(objectSize, 0U);
        ArkSteedNameRef name;
        EXPECT_TRUE(broker_->TryGetGlobalConstantRef(ConstantIndex::UNDEFINED_STRING_INDEX, &name));
        EXPECT_TRUE(name.IsString());
        EXPECT_EQ(broker_->Mode(), ArkSteedBrokerMode::SERIALIZED);
        EXPECT_EQ(AllowDeRefHandle::IsAllowed(), !IS_ALLOW_CHECK);
    });
}

HWTEST_F_L0(ArkSteedHeapRefTest, HandlerReadsOwnScopesAcrossGC)
{
    auto *factory = vm_->GetFactory();
    auto holder = factory->NewEmptyJSObject(vm_->GetGlobalEnv());
    auto cell = factory->NewProtoChangeMarker();
    auto prototype = factory->NewPrototypeHandler();
    prototype->SetHandlerInfo(thread_, JSTaggedValue(42));
    prototype->SetHolder(thread_, holder.GetTaggedValue());
    prototype->SetProtoCell(thread_, cell.GetTaggedValue());
    auto transition = factory->NewTransitionHandler();
    transition->SetHandlerInfo(thread_, JSTaggedValue(43));
    transition->SetTransitionHClass(thread_, JSTaggedValue(holder->GetClass()));
    auto transWithProto = factory->NewTransWithProtoHandler();
    transWithProto->SetHandlerInfo(thread_, JSTaggedValue(44));
    transWithProto->SetTransitionHClass(thread_, JSTaggedValue(holder->GetClass()));
    transWithProto->SetProtoCell(thread_, cell.GetTaggedValue());
    ArkSteedHeapRef prototypeRef;
    ArkSteedHeapRef transitionRef;
    ArkSteedHeapRef transWithProtoRef;
    OnCompiler([&] {
        prototypeRef = broker_->MakeObjectRef(prototype);
        transitionRef = broker_->MakeObjectRef(transition);
        transWithProtoRef = broker_->MakeObjectRef(transWithProto);
    });
    vm_->GetHeap()->CollectGarbage(TriggerGCType::YOUNG_GC);
    OnCompiler([&] {
        DISALLOW_DEREF_HANDLE;
        ArkSteedHeapBroker::LoadHandlerData load;
        ArkSteedHeapBroker::StoreHandlerData store;
        ASSERT_TRUE(broker_->TryReadLoadHandler(prototypeRef, &load));
        EXPECT_FALSE(broker_->SerializingAllowed());
        EXPECT_EQ(load.handlerInfo, 42U);
        EXPECT_FALSE(load.holderIsReceiver);
        EXPECT_TRUE(load.holder.Equals(broker_->MakeObjectRef(holder)));
        EXPECT_TRUE(load.protoCell.Equals(broker_->MakeObjectRef(cell)));
        ASSERT_TRUE(broker_->TryReadStoreHandler(prototypeRef, &store));
        EXPECT_TRUE(store.holder.Equals(load.holder));
        EXPECT_TRUE(store.holderHClass.Equals(load.holderHClass));
        ASSERT_TRUE(broker_->TryReadStoreHandler(transitionRef, &store));
        EXPECT_EQ(store.handlerInfo, 43U);
        EXPECT_TRUE(store.hasTransitionHClass);
        EXPECT_FALSE(store.hasProtoCell);
        EXPECT_TRUE(store.transitionHClass.Equals(load.holderHClass));
        ASSERT_TRUE(broker_->TryReadStoreHandler(transWithProtoRef, &store));
        EXPECT_EQ(store.handlerInfo, 44U);
        EXPECT_TRUE(store.hasProtoCell);
        EXPECT_TRUE(store.protoCell.Equals(load.protoCell));
        EXPECT_FALSE(broker_->TryReadLoadHandler(transitionRef, &load));
        EXPECT_FALSE(broker_->TryReadStoreHandler(prototypeRef, nullptr));
        EXPECT_FALSE(broker_->SerializingAllowed());
        EXPECT_EQ(AllowDeRefHandle::IsAllowed(), !IS_ALLOW_CHECK);
    });
}

HWTEST_F_L0(ArkSteedHeapRefTest, LocalGCWaitsForSafepointThenUpdatesRef)
{
    auto array = vm_->GetFactory()->NewTaggedArray(8);
    ArkSteedHeapRef ref;
    OnCompiler([&] { ref = broker_->MakeObjectRef(array); });
    std::promise<void> rawAddressCopied;
    auto ready = rawAddressCopied.get_future();
    JSTaggedType before = 0;
    auto complete = ScheduleCompiler([&] {
        auto *jitThread = static_cast<JitThread *>(env_->GetJSThread());
        EXPECT_TRUE(broker_->TryReadRef(ref, [&](JSTaggedValue value) {
            before = value.GetRawData();
            rawAddressCopied.set_value();
            // GC requests a stop, but may not move this temporary until this
            // read returns and the compiler explicitly reaches its safepoint.
            while (!jitThread->HasSuspendRequest()) {
                std::this_thread::yield();
            }
            EXPECT_EQ(TaggedArray::Cast(value.GetTaggedObject())->GetLength(), 8U);
            EXPECT_EQ(array.GetTaggedValue(), value);
            return true;
        }));
        jitThread->HeapSafepoint();
        EXPECT_TRUE(broker_->TryReadRef(ref, [&](JSTaggedValue value) {
            EXPECT_NE(value.GetRawData(), before);
            EXPECT_EQ(value, array.GetTaggedValue());
            EXPECT_EQ(TaggedArray::Cast(value.GetTaggedObject())->GetLength(), 8U);
            EXPECT_TRUE(broker_->MakeObjectRef(value).Equals(ref));
            return true;
        }));
    });
    {
        ThreadNativeScope native(thread_);
        ready.get();
    }
    vm_->GetHeap()->CollectGarbage(TriggerGCType::YOUNG_GC);
    {
        ThreadNativeScope native(thread_);
        complete.get();
    }
}

HWTEST_F_L0(ArkSteedHeapRefTest, SharedGCWaitsForSafepoint)
{
    auto array = vm_->GetFactory()->NewSTaggedArray(8);
    ArkSteedHeapRef ref;
    OnCompiler([&] { ref = broker_->MakeObjectRef(array); });
    std::promise<void> rawAddressCopied;
    auto ready = rawAddressCopied.get_future();
    auto complete = ScheduleCompiler([&] {
        auto *jitThread = static_cast<JitThread *>(env_->GetJSThread());
        EXPECT_TRUE(broker_->TryReadRef(ref, [&](JSTaggedValue value) {
            rawAddressCopied.set_value();
            // Shared and local GC use the same thread-state request. Both
            // must wait until this raw read ends and the compiler parks.
            while (!jitThread->HasSuspendRequest()) {
                std::this_thread::yield();
            }
            EXPECT_EQ(TaggedArray::Cast(value.GetTaggedObject())->GetLength(), 8U);
            EXPECT_EQ(array.GetTaggedValue(), value);
            return true;
        }));
        jitThread->HeapSafepoint();
        EXPECT_TRUE(broker_->TryReadRef(ref, [&](JSTaggedValue value) {
            EXPECT_EQ(value, array.GetTaggedValue());
            EXPECT_EQ(TaggedArray::Cast(value.GetTaggedObject())->GetLength(), 8U);
            EXPECT_TRUE(broker_->MakeObjectRef(value).Equals(ref));
            return true;
        }));
    });
    {
        ThreadNativeScope native(thread_);
        ready.get();
    }
    SharedHeap::GetInstance()->CollectGarbage<TriggerGCType::SHARED_FULL_GC, GCReason::OTHER>(thread_);
    {
        ThreadNativeScope native(thread_);
        complete.get();
    }
}

HWTEST_F_L0(ArkSteedHeapRefTest, ParkedCompilerWaitsForBothCollectors)
{
    std::atomic<bool> ran{false};
    std::future<void> complete;
    auto *compiler = env_->GetJSThread();
    {
        // The compiler is idle/native. Requesting a local safepoint must also
        // prevent a NEW heap-access phase, not merely stop existing readers.
        Jit::JitGCLockHolder localStop(thread_);
        EXPECT_TRUE(compiler->HasSuspendRequest());
        EXPECT_FALSE(compiler->IsInRunningState());
        complete = ScheduleCompiler([&] {
            EXPECT_TRUE(broker_->MakeObjectRef(function_).IsJSFunction());
            ran.store(true);
        });
        {
            Jit::JitGCLockHolder nestedStop(thread_);
            SharedHeap::GetInstance()->CollectGarbage<TriggerGCType::SHARED_FULL_GC, GCReason::OTHER>(thread_);
        }
        // Neither shared GC's resume nor the inner local scope may erase the
        // outstanding outer local request on the same thread-state word.
        EXPECT_TRUE(compiler->HasSuspendRequest());
        EXPECT_FALSE(compiler->IsInRunningState());
        EXPECT_FALSE(ran.load());
    }
    {
        ThreadNativeScope native(thread_);
        complete.get();
    }
    EXPECT_TRUE(ran.load());
}

HWTEST_F_L0(ArkSteedHeapRefTest, RunningCompilerAcknowledgesBothCollectors)
{
    std::promise<JSThread *> helperReady;
    auto helperInitialized = helperReady.get_future();
    std::promise<void> startSharedGC;
    auto startShared = startSharedGC.get_future();
    // Register after the compiler: Runtime visits this thread after sending
    // the compiler its shared-GC request (the main host is handled last).
    std::thread helper([&] {
        EcmaVM *helperVM = nullptr;
        JSThread *helperThread = nullptr;
        EcmaHandleScope *helperScope = nullptr;
        panda::test::TestHelper::CreateEcmaVMWithScope(helperVM, helperThread, helperScope);
        helperVM->SetEnableForceGC(false);
        helperReady.set_value(helperThread);
        {
            ThreadNativeScope native(helperThread);
            startShared.get();
        }
        SharedHeap::GetInstance()->CollectGarbage<TriggerGCType::SHARED_FULL_GC, GCReason::OTHER>(helperThread);
        panda::test::TestHelper::DestroyEcmaVMWithScope(helperVM, helperScope);
    });
    JSThread *helperThread = nullptr;
    {
        ThreadNativeScope native(thread_);
        helperThread = helperInitialized.get();
    }
    auto array = vm_->GetFactory()->NewTaggedArray(8);
    ArkSteedHeapRef ref;
    OnCompiler([&] { ref = broker_->MakeObjectRef(array); });
    std::promise<void> rawAddressCopied;
    auto ready = rawAddressCopied.get_future();
    auto complete = ScheduleCompiler([&] {
        auto *compiler = static_cast<JitThread *>(env_->GetJSThread());
        JSTaggedType before = 0;
        EXPECT_TRUE(broker_->TryReadRef(ref, [&](JSTaggedValue value) {
            before = value.GetRawData();
            rawAddressCopied.set_value();
            while (!compiler->HasSuspendRequest()) {
                std::this_thread::yield();
            }
            startSharedGC.set_value();
            while (!helperThread->HasSuspendRequest()) {
                std::this_thread::yield();
            }
            EXPECT_EQ(TaggedArray::Cast(value.GetTaggedObject())->GetLength(), 8U);
            EXPECT_EQ(value, array.GetTaggedValue());
            return true;
        }));
        compiler->HeapSafepoint();
        EXPECT_TRUE(broker_->TryReadRef(ref, [&](JSTaggedValue value) {
            EXPECT_NE(value.GetRawData(), before);
            EXPECT_EQ(value, array.GetTaggedValue());
            EXPECT_TRUE(broker_->MakeObjectRef(value).Equals(ref));
            return true;
        }));
    });
    {
        ThreadNativeScope native(thread_);
        ready.get();
    }
    vm_->GetHeap()->CollectGarbage(TriggerGCType::YOUNG_GC);
    {
        ThreadNativeScope native(thread_);
        complete.get();
        helper.join();
    }
}

HWTEST_F_L0(ArkSteedHeapRefTest, LegacyMutexWaiterParksForSharedGC)
{
    std::promise<void> aboutToLock;
    auto ready = aboutToLock.get_future();
    std::atomic<bool> finished{false};
    std::future<void> complete;
    auto *compiler = env_->GetJSThread();
    {
        LockHolder lock(*thread_->GetJitLock());
        complete = ScheduleCompiler([&] {
            aboutToLock.set_value();
            Jit::JitLockHolder legacy(compiler);
            EXPECT_TRUE(broker_->MakeObjectRef(function_).IsJSFunction());
            finished.store(true);
        });
        {
            ThreadNativeScope native(thread_);
            ready.get();
            while (compiler->IsInRunningState()) {
                std::this_thread::yield();
            }
        }
        SharedHeap::GetInstance()->CollectGarbage<TriggerGCType::SHARED_FULL_GC, GCReason::OTHER>(thread_);
        EXPECT_FALSE(compiler->IsInRunningState());
        EXPECT_FALSE(finished.load());
    }
    {
        ThreadNativeScope native(thread_);
        complete.get();
    }
    EXPECT_TRUE(finished.load());
}

HWTEST_F_L0(ArkSteedHeapRefTest, NestedRunningScopeDoesNotPark)
{
    OnCompiler([&] {
        auto ref = broker_->MakeObjectRef(function_);
        EXPECT_TRUE(broker_->TryReadRef(ref, [&](JSTaggedValue value) {
            {
                JitHeapAccessScope nested(env_->GetJSThread());
                EXPECT_EQ(value, function_.GetTaggedValue());
                EXPECT_EQ(AllowGarbageCollection::IsAllowed(), !IS_ALLOW_CHECK);
                EXPECT_EQ(AllowHeapAlloc::IsAllowed(), !IS_ALLOW_CHECK);
            }
            EXPECT_TRUE(env_->GetJSThread()->IsInRunningState());
            return true;
        }));
    });
}

HWTEST_F_L0(ArkSteedHeapRefTest, SyncWaitParksHostForSharedGC)
{
    auto *task = static_cast<JitThread *>(env_->GetJSThread())->GetCurrentTask();
    ASSERT_NE(task, nullptr);
    std::promise<void> helperReady;
    auto initialized = helperReady.get_future();
    std::promise<void> startGC;
    auto start = startGC.get_future();
    std::thread helper([&] {
        EcmaVM *helperVM = nullptr;
        JSThread *helperThread = nullptr;
        EcmaHandleScope *helperScope = nullptr;
        panda::test::TestHelper::CreateEcmaVMWithScope(helperVM, helperThread, helperScope);
        helperVM->SetEnableForceGC(false);
        helperReady.set_value();
        {
            ThreadNativeScope native(helperThread);
            start.get();
        }
        // Bound the failure path so a missing native transition reports a
        // failure instead of deadlocking the test itself.
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (thread_->IsInRunningState() && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::yield();
        }
        bool parked = !thread_->IsInRunningState();
        EXPECT_TRUE(parked);
        if (parked) {
            SharedHeap::GetInstance()->CollectGarbage<TriggerGCType::SHARED_FULL_GC, GCReason::OTHER>(helperThread);
        }
        task->SetRunStateFinish();
        panda::test::TestHelper::DestroyEcmaVMWithScope(helperVM, helperScope);
    });
    {
        ThreadNativeScope native(thread_);
        initialized.get();
    }
    startGC.set_value();
    task->WaitFinish();
    EXPECT_TRUE(thread_->IsInRunningState());
    {
        ThreadNativeScope native(thread_);
        helper.join();
    }
}

#ifndef NDEBUG
TEST(ArkSteedHeapRefImmediateTest, RawCreationRequiresCallerScope)
{
    ArkSteedHeapBroker broker(nullptr, nullptr);
    EXPECT_DEATH(broker.MakeHandlerRef(JSTaggedValue(42)), "SerializingAllowed");
    EXPECT_DEATH(broker.MakeHClassRef(JSTaggedValue::Undefined()), "SerializingAllowed");
}

HWTEST_F_L0(ArkSteedHeapRefTest, HeapReadCannotBypassSafepointCheck)
{
    auto object = vm_->GetFactory()->NewEmptyJSObject(vm_->GetGlobalEnv());
    EXPECT_DEATH(
        {
            OnCompiler([&] {
                auto ref = broker_->MakeObjectRef(object);
                broker_->TryReadRef(ref, [&](JSTaggedValue) {
                    env_->GetJSThread()->ManagedCodeEnd();
                    return true;
                });
            });
        },
        "disallow execute garbage collection");
}

HWTEST_F_L0(ArkSteedHeapRefTest, GenericNoGCScopeForbidsSafepoint)
{
    EXPECT_DEATH(
        {
            OnCompiler([&] {
                DISALLOW_GARBAGE_COLLECTION;
                static_cast<JitThread *>(env_->GetJSThread())->HeapSafepoint();
            });
        },
        "disallow execute garbage collection");
}

HWTEST_F_L0(ArkSteedHeapRefTest, HeapReadCannotCrossSafepoint)
{
    auto object = vm_->GetFactory()->NewEmptyJSObject(vm_->GetGlobalEnv());
    EXPECT_DEATH(
        {
            OnCompiler([&] {
                auto ref = broker_->MakeObjectRef(object);
                broker_->TryReadRef(ref, [&](JSTaggedValue) {
                    static_cast<JitThread *>(env_->GetJSThread())->HeapSafepoint();
                    return true;
                });
            });
        },
        "disallow execute garbage collection");
}

HWTEST_F_L0(ArkSteedHeapRefTest, RawHeapReadRequiresCallerScope)
{
    auto object = vm_->GetFactory()->NewEmptyJSObject(vm_->GetGlobalEnv());
    ArkSteedHeapRef ref;
    OnCompiler([&] { ref = broker_->MakeObjectRef(object); });
    EXPECT_DEATH(
        {
            DISALLOW_DEREF_HANDLE;
            JSTaggedValue value;
            broker_->TryResolveRef(ref, &value);
        },
        "Resolving a heap object requires an active NoGCScope");
}
#endif

} // namespace panda::ecmascript::arksteed
