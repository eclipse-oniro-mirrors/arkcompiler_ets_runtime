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
 * Concurrent stress for the JSSharedArray element IC hit paths (AC-3.6):
 * writer/reader worker VM hammer ICRuntimeStub::StoreElement/LoadElement on the
 * same shared array (exactly the IC hit functions the interpreter dispatches to),
 * while the main thread interleaves length grow/shrink cycles that replace the
 * elements TaggedArray. Expected (weak consistency, mirrors SharedArrayBuffer
 * non-Atomics semantics): no crash, every read value is a legal snapshot
 * (an int in the written range, or Hole from a shrunk region / capacity miss).
 */

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

#include "ecmascript/checkpoint/thread_state_transition.h"
#include "ecmascript/global_env.h"
#include "ecmascript/ic/ic_handler.h"
#include "ecmascript/ic/ic_runtime_stub-inl.h"
#include "ecmascript/js_handle.h"
#include "ecmascript/js_thread.h"
#include "ecmascript/object_factory.h"
#include "ecmascript/shared_objects/js_shared_array.h"
#include "ecmascript/tests/test_helper.h"

using namespace panda;
using namespace panda::ecmascript;

namespace {
constexpr uint32_t SHARED_ARRAY_LEN = 64;
constexpr uint32_t GROWN_ARRAY_LEN = 128;
constexpr uint32_t WORKER_LOOPS = 100000; // 1e5 per plan
constexpr int VALUE_RANGE_MAX = 100;
constexpr uint32_t MAIN_MUTATION_CYCLES = 200;

struct ICSharedConcurrentCtx {
    std::mutex mtx;
    std::condition_variable cv;
    bool start {false};
    std::atomic<uint32_t> workersReady {0};
    JSTaggedType sharedArrayRaw {0};
    std::atomic<uint32_t> readCount {0};
    std::atomic<uint32_t> badValueCount {0};
    std::atomic<uint32_t> deoptCount {0};
};

JSTaggedValue MakeSharedElementHandler()
{
    // Same encoding StoreHandler::StoreElement distills: tagged int carrying the
    // S_ELEMENT kind (no IsJSArrayBit / SSharedBit for shared receivers).
    uint32_t handler = 0;
    HandlerBase::SKindBit::Set<uint32_t>(HandlerBase::StoreHandlerKind::S_ELEMENT, &handler);
    return JSTaggedValue(static_cast<int>(handler));
}

void ICSharedWriterRun(ICSharedConcurrentCtx *ctx)
{
    JSRuntimeOptions options;
    options.SetEnableForceGC(false);
    EcmaVM *workerVm = JSNApi::CreateEcmaVM(options);
    ASSERT_TRUE(workerVm != nullptr) << "Cannot create writer EcmaVM";
    JSThread *workerThread = workerVm->GetJSThread();
    workerThread->ManagedCodeBegin();
    {
        EcmaHandleScope workerScope(workerThread);
        JSHandle<JSTaggedValue> arr(workerThread, JSTaggedValue(ctx->sharedArrayRaw));
        JSHClass *hclass = arr->GetTaggedObject()->GetClass();
        ASSERT_TRUE(hclass->IsJSShared());
        JSTaggedValue handler = MakeSharedElementHandler();
        // A local (non-shared) value used to exercise the deopt branch of the shared
        // store hit path: the hit path must return Hole without mutating the array.
        JSHandle<JSTaggedValue> objFun = workerThread->GetEcmaVM()->GetGlobalEnv()->GetObjectFunction();
        JSHandle<JSObject> localValue = workerThread->GetEcmaVM()->GetFactory()->NewJSObjectByConstructor(
            JSHandle<JSFunction>::Cast(objFun), objFun);
        {
            // Suspend the thread while blocking on the condvar so a concurrent
            // SuspendAll from another VM's GC never waits on a blocked thread.
            ThreadSuspensionScope suspensionScope(workerThread);
            std::unique_lock<std::mutex> lock(ctx->mtx);
            ctx->workersReady.fetch_add(1, std::memory_order_relaxed);
            ctx->cv.notify_all();
            ctx->cv.wait(lock, [ctx] { return ctx->start; });
        }
        for (uint32_t i = 0; i < WORKER_LOOPS; i++) {
            uint32_t idx = i % SHARED_ARRAY_LEN;
            JSTaggedValue value;
            if (UNLIKELY((i & 0x3FF) == 0x3FF)) { // every 1024th: non-shareable local object
                value = localValue.GetTaggedValue();
            } else {
                value = JSTaggedValue(static_cast<int>(i % VALUE_RANGE_MAX) + 1);
            }
            JSTaggedValue res = ICRuntimeStub::SharedStoreElement(
                workerThread, arr.GetTaggedValue(), hclass,
                JSTaggedValue(static_cast<int>(idx)), value, handler);
            if (res.IsException() || workerThread->HasPendingException()) {
                // The ConcurrentApiScope WRITE registration saw a concurrent reader or
                // writer and raised the concurrent-modification error directly, same
                // contract as the slow path's JSSharedArray::SetProperty: observe it,
                // clear it, the store simply does not happen.
                ctx->deoptCount.fetch_add(1, std::memory_order_relaxed);
                workerThread->ClearException();
            } else if (res.IsHole()) {
                // Plain deopt (no pending exception): either the non-shareable value
                // (value-check failure) or a capacity/Hole-slot miss caused by the
                // main-thread mutations. Only the non-shareable case is asserted and
                // counted; a dropped numeric store is one legal weak-consistency
                // interleaving.
                if (value.IsHeapObject()) {
                    ctx->deoptCount.fetch_add(1, std::memory_order_relaxed);
                    bool success = JSSharedArray::SetProperty(
                        workerThread, JSHandle<JSTaggedValue>::Cast(arr),
                        JSHandle<JSTaggedValue>(workerThread, JSTaggedValue(static_cast<int>(idx))),
                        JSHandle<JSTaggedValue>(workerThread, value), true, SCheckMode::CHECK);
                    ASSERT_FALSE(success);
                    ASSERT_TRUE(workerThread->HasPendingException());
                    workerThread->ClearException();
                }
            }
            if (UNLIKELY((i & 0xFF) == 0)) {
                workerThread->CheckSafepoint(); // mirror interpreter safepoint polling
            }
        }
    }
    workerThread->ManagedCodeEnd();
    JSNApi::DestroyJSVM(workerVm);
}

void ICSharedReaderRun(ICSharedConcurrentCtx *ctx)
{
    JSRuntimeOptions options;
    options.SetEnableForceGC(false);
    EcmaVM *workerVm = JSNApi::CreateEcmaVM(options);
    ASSERT_TRUE(workerVm != nullptr) << "Cannot create reader EcmaVM";
    JSThread *workerThread = workerVm->GetJSThread();
    workerThread->ManagedCodeBegin();
    {
        EcmaHandleScope workerScope(workerThread);
        JSHandle<JSObject> arr(workerThread, JSTaggedValue(ctx->sharedArrayRaw));
        {
            // Suspend the thread while blocking on the condvar so a concurrent
            // SuspendAll from another VM's GC never waits on a blocked thread.
            ThreadSuspensionScope suspensionScope(workerThread);
            std::unique_lock<std::mutex> lock(ctx->mtx);
            ctx->workersReady.fetch_add(1, std::memory_order_relaxed);
            ctx->cv.notify_all();
            ctx->cv.wait(lock, [ctx] { return ctx->start; });
        }
        for (uint32_t i = 0; i < WORKER_LOOPS; i++) {
            uint32_t idx = i % SHARED_ARRAY_LEN;
            JSTaggedValue res = ICRuntimeStub::LoadElement(
                workerThread, JSObject::Cast(arr.GetTaggedValue().GetTaggedObject()),
                JSTaggedValue(static_cast<int>(idx)));
            if (workerThread->HasPendingException()) {
                // The ConcurrentApiScope READ registration saw a writer registration
                // and raised the concurrent-modification error directly: observe and
                // clear, this read simply does not happen.
                workerThread->ClearException();
            } else {
                bool legal = res.IsHole() || res.IsUndefined() ||
                    (res.IsInt() && res.GetInt() >= 1 && res.GetInt() <= VALUE_RANGE_MAX);
                if (!legal) {
                    ctx->badValueCount.fetch_add(1, std::memory_order_relaxed);
                }
            }
            ctx->readCount.fetch_add(1, std::memory_order_relaxed);
            if (UNLIKELY((i & 0xFF) == 0)) {
                workerThread->CheckSafepoint(); // mirror interpreter safepoint polling
            }
        }
    }
    workerThread->ManagedCodeEnd();
    JSNApi::DestroyJSVM(workerVm);
}
} // namespace

namespace panda::test {
class ICSharedConcurrentTest : public BaseTestWithScope<true> {
};

/*
 * @tc.name: SharedArrayElementICConcurrentStress
 * @tc.desc: concurrent IC hit read/write + length mutations on one JSSharedArray
 * @tc.type: FUNC
 */
HWTEST_F_L0(ICSharedConcurrentTest, SharedArrayElementICConcurrentStress)
{
    // Create via ArrayCreate and fill elements DIRECTLY, mirroring the SendableArray
    // constructor (builtins_shared_array.cpp): SetCapacity grows with Holes and a Hole
    // slot is "not found" for SetProperty (OOB RangeError), so values must be written
    // straight into the elements array first.
    JSHandle<JSTaggedValue> arrValue = JSSharedArray::ArrayCreate(
        thread, JSTaggedNumber(static_cast<double>(SHARED_ARRAY_LEN)));
    ASSERT_TRUE(!thread->HasPendingException());
    JSHandle<JSObject> obj = JSHandle<JSObject>::Cast(arrValue);
    {
        JSHandle<TaggedArray> elements(thread, obj->GetElements(thread));
        ASSERT_GE(elements->GetLength(), SHARED_ARRAY_LEN); // capacity grows with slack
        for (uint32_t i = 0; i < SHARED_ARRAY_LEN; i++) {
            elements->Set(thread, i, JSTaggedValue(static_cast<int>(i % VALUE_RANGE_MAX) + 1));
        }
    }

    ICSharedConcurrentCtx ctx;
    ctx.sharedArrayRaw = obj.GetTaggedValue().GetRawData();

    std::thread writer(ICSharedWriterRun, &ctx);
    std::thread reader(ICSharedReaderRun, &ctx);
    {
        // Wait until both workers finished VM creation (native scope so the test
        // thread never blocks a SuspendAll barrier), then release them together.
        ThreadNativeScope nativeScope(thread);
        std::unique_lock<std::mutex> lock(ctx.mtx);
        ctx.cv.wait(lock, [&ctx] { return ctx.workersReady.load() >= 2; });
    }
    {
        std::unique_lock<std::mutex> lock(ctx.mtx);
        ctx.start = true;
        lock.unlock();
        ctx.cv.notify_all();
    }
    // Main thread interleaves grow/shrink length cycles; each grow replaces the
    // elements TaggedArray (pointer replacement race), shrink fills Holes.
    for (uint32_t i = 0; i < MAIN_MUTATION_CYCLES; i++) {
        bool grow = (i % 2) == 0;
        JSTaggedValue newLen(static_cast<int>(grow ? GROWN_ARRAY_LEN : SHARED_ARRAY_LEN));
        JSSharedArray::LengthSetter(thread, obj, JSHandle<JSTaggedValue>(thread, newLen), false);
    }
    writer.join();
    reader.join();

    EXPECT_EQ(ctx.badValueCount.load(), 0u);
    EXPECT_GT(ctx.readCount.load(), 0u);
    EXPECT_GT(ctx.deoptCount.load(), 0u); // deopt branch exercised and observed
}
} // namespace panda::test
