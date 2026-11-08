/*
 * Copyright (c) 2024 Huawei Device Co., Ltd.
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

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <getopt.h>
#include <mutex>
#include <set>
#include <thread>

#include "ecmascript/checkpoint/thread_state_transition.h"
#include "ecmascript/global_env.h"
#include "ecmascript/ic/profile_type_info.h"
#include "ecmascript/interpreter/slow_runtime_stub.h"
#include "ecmascript/napi/include/jsnapi.h"
#include "ecmascript/object_factory-inl.h"
#include "ecmascript/tests/test_helper.h"
#include "ecmascript/jit/jit_task.h"
#if ECMASCRIPT_ENABLE_ARK_STEED
#include "ecmascript/arksteed/arksteed_task.h"
#include "ecmascript/mem/jit_fort.h"
#include "ecmascript/mem/heap_region_allocator.h"
#include "ecmascript/platform/map.h"
#endif

using namespace panda::ecmascript;
using namespace testing::ext;

namespace panda::test {
namespace {
constexpr std::chrono::seconds RENDEZVOUS_TIMEOUT {10};
constexpr int FP_DELTA_PREV_FRAME_SP = static_cast<int>(sizeof(uint64_t) + sizeof(uint64_t));
}

class JitOptionsTest : public testing::Test {
protected:
    static bool ParseWorkerCount(JSRuntimeOptions &options, const char *value)
    {
        int savedOptind = optind;
        int savedOpterr = opterr;
        optind = 1;
#if defined(PANDA_TARGET_MACOS)
        optreset = 1;
#endif
        const char *argv[] = {"ark_js_vm", "--compiler-jit-worker-count", value};
        bool result = options.ParseCommand(3, argv);
        optind = savedOptind;
        opterr = savedOpterr;
        return result;
    }
};

HWTEST_F_L0(JitOptionsTest, WorkerCountDefaultsAndValidValues)
{
    JSRuntimeOptions options;
    EXPECT_EQ(options.GetCompilerJitWorkerCount(), JitTaskpool::DEFAULT_WORKER_COUNT);
    for (uint32_t count = 1; count <= common::MAX_TASKPOOL_THREAD_NUM; ++count) {
        std::string value = std::to_string(count);
        ASSERT_TRUE(ParseWorkerCount(options, value.c_str()));
        EXPECT_EQ(options.GetCompilerJitWorkerCount(), count);
    }
    options.SetCompilerJitWorkerCount(2);
    EXPECT_EQ(options.GetCompilerJitWorkerCount(), 2U);
}

HWTEST_F_L0(JitOptionsTest, WorkerCountRejectsInvalidValues)
{
    JSRuntimeOptions options;
    options.SetCompilerJitWorkerCount(2);
    const char *invalidValues[] = {
        "", "0", "6", "-1", "+1", "1.5", "2junk", " 2", "2 ", "0x2", "abc", "4294967297",
        "18446744073709551616",
    };
    for (const char *value : invalidValues) {
        EXPECT_FALSE(ParseWorkerCount(options, value)) << value;
        EXPECT_EQ(options.GetCompilerJitWorkerCount(), 2U);
    }
}

class JitTest : public testing::Test {
public:
    static void SetUpTestCase()
    {
        GTEST_LOG_(INFO) << "SetUpTestCase";
    }

    static void TearDownTestCase()
    {
        GTEST_LOG_(INFO) << "TearDownCase";
    }

    void SetUp() override
    {
        TestHelper::CreateEcmaVMWithScope(instance_, thread_, scope_, false, false, true, true);
        thread_ = instance_->GetJSThread();
        ThreadNativeScope scope(thread_);
        auto *pool = JitTaskpool::GetCurrentTaskpool();
        pool->Destroy();
        pool->Initialize(!Jit::GetInstance()->IsDisableCodeSign(), 1);
        pool->WaitForJitTaskPoolReady();
        jit_ = Jit::GetInstance();
    }

    void TearDown() override
    {
        TestHelper::DestroyEcmaVMWithScope(instance_, scope_);
    }

    EcmaVM *instance_ {nullptr};
    EcmaHandleScope *scope_ {nullptr};
    JSThread *thread_ {nullptr};
    Jit *jit_ {nullptr};
};

/**
 * @tc.name: IsEnableFastJit
 * @tc.desc: check jit is enable fast.
 * @tc.type: FUNC
 * @tc.require:
 */
HWTEST_F_L0(JitTest, IsEnableFastJit)
{
    EXPECT_TRUE(jit_->IsEnableFastJit());
}

/**
 * @tc.name: NewJitTask
 * @tc.desc: check new jit task.
 * @tc.type: FUNC
 * @tc.require:
 */
HWTEST_F_L0(JitTest, NewJitTask)
{
    JSHandle<JSFunction> emptyHandle;
    CString methodName("methodName");
    std::shared_ptr<JitTask> jitTask = std::make_shared<JitTask>(thread_, nullptr,
        jit_, emptyHandle, CompilerTier(CompilerTier::Tier::FAST), methodName, 0,
        JitCompileMode(JitCompileMode::Mode::SYNC));
    EXPECT_TRUE(jitTask->GetHostThread() == thread_);
    EXPECT_EQ(jitTask->GetCompilerThread(), nullptr);
    EXPECT_EQ(jitTask->GetCompilerTask(), nullptr);
    EXPECT_EQ(JitTaskpool::GetCurrentTaskpool()->GetCompilerVm(), nullptr);
}

HWTEST_F_L0(JitTest, LegacyTaskBindsCompilerOnWorker)
{
    JSHandle<JSFunction> empty;
    CString name("legacy-worker-binding");
    auto task = std::make_shared<JitTask>(thread_, nullptr, jit_, empty, CompilerTier::Tier::FAST, name,
                                         MachineCode::INVALID_OSR_OFFSET, JitCompileMode::Mode::SYNC);
    ASSERT_TRUE(JitTaskpool::GetCurrentTaskpool()->PostTask(
        std::make_unique<JitTask::AsyncTask>(task, thread_->GetThreadId())));
    task->WaitFinish();
    EXPECT_TRUE(task->IsCompileSuccess());
    ASSERT_NE(task->GetCompilerThread(), nullptr);
    EXPECT_TRUE(task->GetCompilerThread()->IsJitThread());
    EXPECT_NE(task->GetCompilerVM(), instance_);
}

HWTEST_F_L0(JitTest, CancelledLegacyTasksFinishAndClearFlags)
{
    auto *pool = JitTaskpool::GetCurrentTaskpool();
    for (auto tier : {CompilerTier::Tier::FAST, CompilerTier::Tier::BASELINE}) {
        for (auto mode : {JitCompileMode::Mode::SYNC, JitCompileMode::Mode::ASYNC}) {
            auto function = instance_->GetFactory()->NewJSFunction(instance_->GetGlobalEnv());
            bool fast = tier == CompilerTier::Tier::FAST;
            function->SetJitCompilingFlag(fast);
            function->SetBaselinejitCompilingFlag(!fast);
            CString name("cancelled-legacy");
            auto task = std::make_shared<JitTask>(thread_, nullptr, jit_, function, tier, name,
                                                 MachineCode::INVALID_OSR_OFFSET, mode);
            auto asyncTask = std::make_unique<JitTask::AsyncTask>(task, thread_->GetThreadId());
            asyncTask->Terminated();
            ASSERT_TRUE(pool->PostTask(std::move(asyncTask)));
            task->WaitFinish();
            {
                ThreadNativeScope native(thread_);
                pool->Destroy();
            }
            EXPECT_FALSE(task->IsCompileSuccess());
            EXPECT_EQ(task->GetCompilerThread(), nullptr);
            EXPECT_EQ(task->GetCompilerTask(), nullptr);
            if (mode == JitCompileMode::Mode::SYNC) {
                task->InstallCode();
            } else {
                jit_->InstallTasks(thread_);
            }
            EXPECT_FALSE(function->IsJitCompiling());
            EXPECT_FALSE(function->IsBaselinejitCompiling());
            task.reset();
            EXPECT_EQ(jit_->GetRunningTaskCnt(instance_), 0U);
            {
                ThreadNativeScope native(thread_);
                pool->Initialize(!jit_->IsDisableCodeSign(), 1);
                ASSERT_TRUE(pool->WaitForJitTaskPoolReady());
            }
        }
    }
}
/**
 * @tc.name: SetEnableOrDisable_BaselineStubBuildGate
 * @tc.desc: Build capability constrains Baseline requests without coupling the Fast JIT state.
 * @tc.type: FUNC
 * @tc.require: 20260803825096
 */
HWTEST_F(JitTest, SetEnableOrDisable_BaselineStubBuildGate, TestSize.Level1)
{
    ASSERT_NE(jit_, nullptr);
    const auto &options = instance_->GetJSOptions();
    for (bool fastRequested : {false, true}) {
        for (bool baselineRequested : {false, true}) {
            jit_->SetEnableOrDisable(options, fastRequested, baselineRequested);
            EXPECT_EQ(jit_->IsEnableFastJit(), fastRequested);
#if defined(ECMASCRIPT_ENABLE_BASELINE_STUB)
            EXPECT_EQ(jit_->IsEnableBaselineJit(), baselineRequested);
#else
            EXPECT_FALSE(jit_->IsEnableBaselineJit());
#endif
        }
    }
    // Restore the fixture's initial policy before its VM and task pool are destroyed.
    jit_->SetEnableOrDisable(options, true, false);
    EXPECT_TRUE(jit_->IsEnableFastJit());
    EXPECT_FALSE(jit_->IsEnableBaselineJit());
}

/**
 * @tc.name: WorkerTieringGate
 * @tc.desc: main-VM-only tiering affinity: worker runtimes skip the jit profile
 *           stub switch and stay on the baseline interpreter stubs, so no
 *           profiling or hotness instrumentation runs on them, while
 *           ProfileTypeInfo is still allocated to keep ic feedback working.
 * @tc.type: FUNC
 * @tc.require:
 */
HWTEST_F_L0(JitTest, WorkerTieringGate)
{
    EXPECT_TRUE(thread_->IsMainThreadFast());
    // The fixture main VM went through the normal profile stub switch.
    EXPECT_EQ(thread_->GetBCStubStatus(), BCStubStatus::PROFILE_BC_STUB);

    ObjectFactory *mainFactory = instance_->GetFactory();
    JSHandle<ProfileTypeInfo> mainProfileTypeInfo = mainFactory->NewProfileTypeInfo(4);  // 4: ic slot size
    EXPECT_EQ(mainProfileTypeInfo->GetPeriodIndex(), ProfileTypeInfo::INITIAL_PERIOD_INDEX);

    RuntimeOption workerOption;
    workerOption.SetIsWorker();
    std::thread workerThread([&workerOption]() {
        EcmaVM *workerVm = JSNApi::CreateJSVM(workerOption);
        ASSERT_TRUE(workerVm != nullptr);
        JSThread *workerThread = workerVm->GetJSThread();
        {
            ThreadManagedScope managedScope(workerThread);
            EcmaHandleScope handleScope(workerThread);
            EXPECT_FALSE(workerThread->IsMainThreadFast());
            // Worker runtimes skip the jit profile stub switch: they stay on the
            // non-profiling baseline stub set (NORMAL or its STW-copy running
            // set), so profiling data collection and hotness tier-up never
            // trigger on them.
            EXPECT_NE(workerThread->GetBCStubStatus(), BCStubStatus::PROFILE_BC_STUB);
            EXPECT_NE(workerThread->GetBCStubStatus(), BCStubStatus::JIT_PROFILE_BC_STUB);

            // ProfileTypeInfo is still allocated on worker runtimes for ic feedback;
            // hotness thresholds now live in Method::HotnessCounterField and are
            // managed by TieringManager, so worker compile is gated in
            // Jit::CompileArkSteed instead. PGO profiler is forced off on workers.
            EXPECT_FALSE(workerThread->IsPGOProfilerEnable());
            JSHandle<JSTaggedValue> objFun = workerVm->GetGlobalEnv()->GetObjectFunction();
            JSFunction *function = JSFunction::Cast(objFun->GetTaggedObject());
            JSTaggedValue profileTypeInfoValue = SlowRuntimeStub::NotifyInlineCache(workerThread, function);
            ASSERT_TRUE(profileTypeInfoValue.IsProfileTypeInfo());
            JSHandle<ProfileTypeInfo> workerProfileTypeInfo(workerThread, profileTypeInfoValue);
            EXPECT_EQ(workerProfileTypeInfo->GetPeriodIndex(), ProfileTypeInfo::INITIAL_PERIOD_INDEX);
        }
        JSNApi::DestroyJSVM(workerVm);
    });
    workerThread.join();
}

// This fixture explicitly requests the production default. The older JitTest
// fixture pins one worker because it deliberately borrows that worker's VM.
class ParallelJitTest : public testing::Test {
public:
    void SetUp() override
    {
        TestHelper::CreateEcmaVMWithScope(vm_, host_, handles_, false, false, true, true);
        ThreadNativeScope native(host_);
        ASSERT_TRUE(JitTaskpool::GetCurrentTaskpool()->WaitForJitTaskPoolReady());
    }

    void TearDown() override
    {
        if (vm_ != nullptr) {
            TestHelper::DestroyEcmaVMWithScope(vm_, handles_);
        }
    }

protected:
    EcmaVM *vm_ {nullptr};
    JSThread *host_ {nullptr};
    EcmaHandleScope *handles_ {nullptr};
};

struct WorkerRendezvous {
    std::mutex mutex;
    std::condition_variable cv;
    std::set<EcmaVM *> workerVms;
    std::set<uint32_t> indices;
    bool release {false};
};

class RendezvousTask final : public common::Task {
public:
    RendezvousTask(std::shared_ptr<JitTask> task, WorkerRendezvous &rendezvous)
        : common::Task(task->GetHostThread()->GetThreadId()), task_(task), rendezvous_(rendezvous)
    {
    }

    bool Run(uint32_t index) override
    {
        JitAsyncTaskRunScope binding(task_.get());
        EXPECT_TRUE(binding.IsBound());
        if (!binding.IsBound()) {
            task_->FinishWithoutCompile();
            return false;
        }
        auto *compiler = static_cast<JitThread *>(task_->GetCompilerThread());
        EXPECT_EQ(JSThread::GetCurrent(), compiler);
        EXPECT_EQ(JitTaskpool::GetCurrentTaskpool()->GetCompilerVm(), compiler->GetJitVM());
        EXPECT_EQ(compiler->GetHostThread(), task_->GetHostThread());
        EXPECT_EQ(compiler->GetCurrentTask(), task_.get());
        EXPECT_EQ(compiler->GetThreadId(), os::thread::GetCurrentThreadId());
        // A test latch must never block a RUNNING mutator needed by shared GC.
        EXPECT_FALSE(compiler->IsInRunningState());
        std::unique_lock<std::mutex> lock(rendezvous_.mutex);
        rendezvous_.workerVms.insert(compiler->GetJitVM());
        rendezvous_.indices.insert(index);
        rendezvous_.cv.notify_all();
        rendezvous_.cv.wait(lock, [this] { return rendezvous_.release; });
        return true;
    }

private:
    std::shared_ptr<JitTask> task_;
    WorkerRendezvous &rendezvous_;
};

HWTEST_F_L0(ParallelJitTest, DefaultWorkersHaveDistinctVMsAndOverlap)
{
    auto *pool = JitTaskpool::GetCurrentTaskpool();
    uint32_t count = pool->GetWorkerCount();
#if ECMASCRIPT_ENABLE_ARK_STEED
    EXPECT_EQ(count, g_isEnableCMCGC ? 1U : 3U);
#else
    EXPECT_EQ(count, 1U);
#endif
    WorkerRendezvous rendezvous;
    std::vector<std::shared_ptr<JitTask>> tasks;
    JSHandle<JSFunction> empty;
    CString name("worker-rendezvous");
    for (uint32_t i = 0; i < count; ++i) {
        auto task = std::make_shared<JitTask>(
            host_, nullptr, Jit::GetInstance(), empty, CompilerTier(CompilerTier::Tier::ARKSTEED), name,
            MachineCode::INVALID_OSR_OFFSET, JitCompileMode(JitCompileMode::Mode::SYNC));
        tasks.push_back(task);
        pool->PostTask(std::make_unique<RendezvousTask>(task, rendezvous));
    }
    bool allEntered;
    {
        ThreadNativeScope native(host_);
        std::unique_lock<std::mutex> lock(rendezvous.mutex);
        allEntered = rendezvous.cv.wait_for(
            lock, RENDEZVOUS_TIMEOUT, [&] { return rendezvous.workerVms.size() == count; });
        // Release even on timeout, so failures cannot strand the worker pool.
        rendezvous.release = true;
        rendezvous.cv.notify_all();
    }
    for (auto &task : tasks) {
        task->WaitFinish();
    }
    EXPECT_TRUE(allEntered);
    EXPECT_EQ(rendezvous.workerVms.size(), count);
    EXPECT_EQ(rendezvous.indices.size(), count);
}

HWTEST_F_L0(ParallelJitTest, DestroyHostDrainsPendingResults)
{
    auto *jit = Jit::GetInstance();
    JSHandle<JSFunction> empty;
    CString name("pending-install-result");
    auto task = std::make_shared<JitTask>(host_, nullptr, jit, empty, CompilerTier(CompilerTier::Tier::ARKSTEED), name,
                                          MachineCode::INVALID_OSR_OFFSET, JitCompileMode(JitCompileMode::Mode::ASYNC));
    task->FinishWithoutCompile();
    jit->RequestInstallCode(task);
    std::weak_ptr<JitTask> pending = task;
    task.reset();  // Only the Host installation queue owns the completed task.
    ASSERT_FALSE(pending.expired());
    EXPECT_EQ(jit->GetRunningTaskCnt(vm_), 1U);

    EXPECT_TRUE(jit->IsEnableFastJit());
    TestHelper::DestroyEcmaVMWithScope(vm_, handles_);
    vm_ = nullptr;
    host_ = nullptr;
    handles_ = nullptr;
    EXPECT_TRUE(pending.expired());
}

HWTEST_F_L0(ParallelJitTest, ClearHostWithoutTasksIsRepeatable)
{
    auto *jit = Jit::GetInstance();
    jit->ClearTaskWithVm(vm_);
    jit->ClearTaskWithVm(vm_);
    // A no-op cleanup must not leave skipInstallTask_ set for this Host.
    JSHandle<JSFunction> empty;
    CString name("after-empty-host-cleanup");
    auto task = std::make_shared<JitTask>(host_, nullptr, jit, empty, CompilerTier(CompilerTier::Tier::ARKSTEED), name,
                                          MachineCode::INVALID_OSR_OFFSET, JitCompileMode(JitCompileMode::Mode::SYNC));
    EXPECT_EQ(jit->GetRunningTaskCnt(vm_), 1U);
}

HWTEST_F_L0(ParallelJitTest, AcceptsTasksBeyondFormerAdmissionLimits)
{
    auto *jit = Jit::GetInstance();
    auto *pool = JitTaskpool::GetCurrentTaskpool();
    JSHandle<JSFunction> empty;
    CString name("unbounded-admission");
    // Keep all tasks alive to exercise both the former 16/Host and 64/pool limits.
    constexpr size_t taskCount = 65;
    std::vector<std::shared_ptr<JitTask>> tasks;
    for (size_t i = 0; i < taskCount; i++) {
        auto task =
            std::make_shared<JitTask>(host_, nullptr, jit, empty, CompilerTier(CompilerTier::Tier::ARKSTEED), name,
                                      MachineCode::INVALID_OSR_OFFSET, JitCompileMode(JitCompileMode::Mode::SYNC));
        bool posted = pool->PostTask(std::make_unique<JitTask::AsyncTask>(task, host_->GetThreadId()));
        EXPECT_TRUE(posted);
        if (!posted) {
            task->FinishWithoutCompile();
        }
        tasks.push_back(std::move(task));
    }
    EXPECT_EQ(jit->GetRunningTaskCnt(vm_), taskCount);
    for (const auto &task : tasks) {
        task->WaitFinish();
        EXPECT_TRUE(task->IsCompileSuccess());
    }
}

#if ECMASCRIPT_ENABLE_ARK_STEED
HWTEST_F_L0(ParallelJitTest, CancelBeforeRunNotifiesSynchronousWaiter)
{
    JSHandle<JSFunction> empty;
    CString name("cancel-before-run");
    auto task = std::make_shared<arksteed::ArkSteedTask>(
        host_, nullptr, Jit::GetInstance(), empty, CompilerTier(CompilerTier::Tier::ARKSTEED), name,
        MachineCode::INVALID_OSR_OFFSET, JitCompileMode(JitCompileMode::Mode::SYNC));
    auto asyncTask = std::make_unique<arksteed::ArkSteedTask::AsyncTask>(task, host_->GetThreadId());
    asyncTask->Terminated();
    ASSERT_TRUE(JitTaskpool::GetCurrentTaskpool()->PostTask(std::move(asyncTask)));
    task->WaitFinish();
    EXPECT_TRUE(task->IsFinish());
    EXPECT_FALSE(task->IsCompileSuccess());
    EXPECT_EQ(task->GetCompilerThread(), nullptr);
}

static void StopAndDrainPool(JitTaskpool *pool, WorkerRendezvous &rendezvous,
                             std::shared_ptr<arksteed::ArkSteedTask> &queued, JSThread *host)
{
    {
        ThreadNativeScope native(host);
        {
            std::unique_lock<std::mutex> lock(rendezvous.mutex);
            EXPECT_TRUE(rendezvous.cv.wait_for(lock, RENDEZVOUS_TIMEOUT,
                                               [&] { return rendezvous.workerVms.size() == pool->GetWorkerCount(); }));
        }
        EXPECT_TRUE(pool->PostTask(std::make_unique<arksteed::ArkSteedTask::AsyncTask>(queued, host->GetThreadId())));
        std::atomic<bool> destroyed {false};
        std::thread stopper([&] {
            pool->Destroy();
            destroyed.store(true);
        });
        auto deadline = std::chrono::steady_clock::now() + RENDEZVOUS_TIMEOUT;
        bool stopped = false;
        do {
            stopped = !pool->WaitForJitTaskPoolReady();
            if (stopped) {
                break;
            }
            std::this_thread::yield();
        } while (std::chrono::steady_clock::now() < deadline);
        EXPECT_TRUE(stopped);
        EXPECT_FALSE(destroyed.load());
        if (stopped) {
            EXPECT_FALSE(
                pool->PostTask(std::make_unique<arksteed::ArkSteedTask::AsyncTask>(queued, host->GetThreadId())));
            // Enumeration is not part of the shared contract while draining;
            // teardown-time cancellation is owned by Runner::TerminateThread.
        }
        {
            std::lock_guard<std::mutex> lock(rendezvous.mutex);
            rendezvous.release = true;
            rendezvous.cv.notify_all();
        }
        stopper.join();
        EXPECT_TRUE(destroyed.load());
        EXPECT_FALSE(pool->WaitForJitTaskPoolReady());
        pool->Destroy();
        EXPECT_EQ(pool->GetCompilerVm(), nullptr);
    }
}

HWTEST_F_L0(ParallelJitTest, StopClosesAdmissionAndDrainsWorkers)
{
    auto *pool = JitTaskpool::GetCurrentTaskpool();
    uint32_t count = pool->GetWorkerCount();
    WorkerRendezvous rendezvous;
    std::vector<std::shared_ptr<JitTask>> tasks;
    JSHandle<JSFunction> empty;
    CString name("stop-bound-worker");
    for (uint32_t i = 0; i < count; ++i) {
        auto task = std::make_shared<JitTask>(
            host_, nullptr, Jit::GetInstance(), empty, CompilerTier(CompilerTier::Tier::ARKSTEED), name,
            MachineCode::INVALID_OSR_OFFSET, JitCompileMode(JitCompileMode::Mode::SYNC));
        tasks.push_back(task);
        EXPECT_TRUE(pool->PostTask(std::make_unique<RendezvousTask>(task, rendezvous)));
    }
    auto queued = std::make_shared<arksteed::ArkSteedTask>(
        host_, nullptr, Jit::GetInstance(), empty, CompilerTier(CompilerTier::Tier::ARKSTEED), name,
        MachineCode::INVALID_OSR_OFFSET, JitCompileMode(JitCompileMode::Mode::SYNC));
    StopAndDrainPool(pool, rendezvous, queued, host_);
    for (auto &task : tasks) {
        task->WaitFinish();
        EXPECT_EQ(task->GetCompilerThread(), nullptr);
    }
    queued->WaitFinish();
    EXPECT_FALSE(queued->IsCompileSuccess());
    EXPECT_EQ(queued->GetCompilerThread(), nullptr);
    tasks.clear();
    queued.reset();
    EXPECT_EQ(Jit::GetInstance()->GetRunningTaskCnt(vm_), 0U);
    {
        ThreadNativeScope native(host_);
        pool->Initialize(true, count);
        ASSERT_TRUE(pool->WaitForJitTaskPoolReady());
        pool->Initialize(true, count == 1 ? 3 : 1);
        EXPECT_EQ(pool->GetWorkerCount(), count);
    }
    auto fresh = std::make_shared<arksteed::ArkSteedTask>(
        host_, nullptr, Jit::GetInstance(), empty, CompilerTier(CompilerTier::Tier::ARKSTEED), name,
        MachineCode::INVALID_OSR_OFFSET, JitCompileMode(JitCompileMode::Mode::SYNC));
    auto asyncTask = std::make_unique<arksteed::ArkSteedTask::AsyncTask>(fresh, host_->GetThreadId());
    asyncTask->Terminated();
    ASSERT_TRUE(pool->PostTask(std::move(asyncTask)));
    fresh->WaitFinish();
    EXPECT_FALSE(fresh->IsCompileSuccess());
}

static void InstallHostFortCode(EcmaVM *vm, JSThread *host, JSHandle<JSFunction> &function, JSHandle<Method> &method,
                                CString &name, bool prepared, size_t codeSize)
{
    arksteed::ArkSteedTask task(host, nullptr, Jit::GetInstance(), function,
                                CompilerTier(CompilerTier::Tier::ARKSTEED), name,
                                MachineCode::INVALID_OSR_OFFSET, JitCompileMode(JitCompileMode::Mode::ASYNC));
    std::vector<uint8_t> bytes(codeSize, 0x5A);
    FuncEntryDes entry {};
    entry.funcSize_ = codeSize;
    entry.fpDeltaPrevFrameSp_ = FP_DELTA_PREV_FRAME_SP;
    auto &desc = task.GetMachineCodeDesc();
    desc.codeType = MachineCodeType::ARKSTEED_CODE;
    desc.codeSize = bytes.size();
    size_t size = JitTask::ComputePayLoadSize(desc);
    ASSERT_EQ(desc.instructionsAddr, 0U);
    ASSERT_EQ(desc.hugeObjRegion, 0U);
    EXPECT_EQ(desc.isHugeObj, codeSize > g_maxRegularHeapObjectSize);
#if defined(PANDA_TARGET_MACOS)
    JitWriteScope writeScope;
#endif
    if (prepared) {
        if (desc.isHugeObj) {
            desc.hugeObjRegion = reinterpret_cast<uintptr_t>(
                vm->GetHeap()->GetHugeMachineCodeSpace()->AllocateFort(size + MachineCode::SIZE, host, &desc));
            ASSERT_NE(desc.hugeObjRegion, 0U);
        } else {
            desc.instructionsAddr = vm->GetHeap()->GetMachineCodeSpace()->JitFortAllocate(&desc);
        }
        ASSERT_NE(desc.instructionsAddr, 0U);
        std::copy_n(bytes.data(), bytes.size(), reinterpret_cast<uint8_t *>(desc.instructionsAddr));
        desc.isAsyncCompileMode = true;
    }
    task.SetCodeData(bytes.data(), bytes.size(), entry);
    if (prepared) {
        EXPECT_EQ(desc.codeAddr, 0U);
    }
    auto *factory = vm->GetFactory();
    TaggedObject *object = factory->NewMachineCodeObject(size, desc);
    ASSERT_NE(object, nullptr);
    desc.hugeObjRegion = 0;
    auto code = factory->SetMachineCodeObjectData(object, size, desc, method);
    ASSERT_FALSE(code.IsEmpty());
    EXPECT_EQ(std::memcmp(reinterpret_cast<void *>(code->GetText()), bytes.data(), bytes.size()), 0);
    vm->GetHeap()->GetOrCreateJitFort()->MarkJitFortMemInstalled(code.GetObject<MachineCode>(), desc.isHugeObj);
    desc.instructionsAddr = 0;
}

HWTEST_F_L0(ParallelJitTest, HostInstallsPreparedAndUnpreparedFortCode)
{
    if (g_isEnableCMCGC || !Jit::GetInstance()->IsEnableJitFort()) {
        GTEST_SKIP() << "Requires the supported non-CMC Fort path";
    }
    struct RestoreAsyncCopyOptions {
        JSRuntimeOptions &options;
        bool hostValue;
        bool jitValue;
        ~RestoreAsyncCopyOptions()
        {
            options.SetEnableAsyncCopyToFort(hostValue);
            Jit::GetInstance()->SetEnableAsyncCopyToFort(jitValue);
        }
    } restore {vm_->GetJSOptions(), vm_->GetJSOptions().GetEnableAsyncCopyToFort(),
               Jit::GetInstance()->IsEnableAsyncCopyToFort()};
    JSHandle<JSFunction> function(vm_->GetGlobalEnv()->GetArrayFunction());
    JSHandle<Method> method(host_, function->GetMethod(host_));
    CString name("host-fort-copy");
    for (bool prepared : {false, true}) {
        vm_->GetJSOptions().SetEnableAsyncCopyToFort(prepared);
        Jit::GetInstance()->SetEnableAsyncCopyToFort(prepared);
        for (size_t codeSize : {size_t(32), size_t(g_maxRegularHeapObjectSize) + 64}) {
            InstallHostFortCode(vm_, host_, function, method, name, prepared, codeSize);
        }
    }
}

HWTEST_F_L0(ParallelJitTest, DiscardedFortCodeReleasesReservations)
{
    if (g_isEnableCMCGC || !Jit::GetInstance()->IsEnableJitFort()) {
        GTEST_SKIP() << "Requires the supported non-CMC Fort path";
    }
    auto *heap = vm_->GetHeap();
    auto *fort = heap->GetOrCreateJitFort();
    auto *allocator = heap->GetHeapRegionAllocator();
    JSHandle<JSFunction> empty;
    CString name("discard-fort-code");
    for (size_t codeSize : {size_t(32), size_t(g_maxRegularHeapObjectSize) + 64}) {
        for (bool copied : {false, true}) {
            size_t memoryBefore = allocator->GetAnnoMemoryUsage();
            uintptr_t text = 0;
            auto isReserved = [&]() {
                auto *region = fort->ObjectAddressToRange(text);
                bool reserved = !region->AtomicMark(reinterpret_cast<void *>(text));
                if (!reserved) {
                    region->ClearMark(reinterpret_cast<void *>(text));
                }
                return reserved;
            };
            {
                arksteed::ArkSteedTask task(
                    host_, nullptr, Jit::GetInstance(), empty, CompilerTier(CompilerTier::Tier::ARKSTEED), name,
                    MachineCode::INVALID_OSR_OFFSET, JitCompileMode(JitCompileMode::Mode::ASYNC));
                auto &desc = task.GetMachineCodeDesc();
                desc.codeType = MachineCodeType::ARKSTEED_CODE;
                desc.codeSize = codeSize;
                size_t size = JitTask::ComputePayLoadSize(desc);
                if (desc.isHugeObj) {
                    desc.hugeObjRegion = reinterpret_cast<uintptr_t>(
                        heap->GetHugeMachineCodeSpace()->AllocateFort(size + MachineCode::SIZE, host_, &desc));
                    ASSERT_NE(desc.hugeObjRegion, 0U);
                } else {
                    desc.instructionsAddr = heap->GetMachineCodeSpace()->JitFortAllocate(&desc);
                }
                text = desc.instructionsAddr;
                ASSERT_NE(text, 0U);
                desc.isAsyncCompileMode = copied;
                EXPECT_TRUE(isReserved());
            }
            EXPECT_FALSE(isReserved());
            EXPECT_EQ(allocator->GetAnnoMemoryUsage(), memoryBefore);
        }
    }
}

HWTEST_F_L0(ParallelJitTest, CancelledAsyncTaskClearsCompilingFlag)
{
    auto *pool = JitTaskpool::GetCurrentTaskpool();
    uint32_t count = pool->GetWorkerCount();
    JSHandle<GlobalEnv> env = vm_->GetGlobalEnv();
    auto function = vm_->GetFactory()->NewJSFunction(env);
    function->SetJitCompilingFlag(true);
    CString name("cancelled-async");
    auto task = std::make_shared<arksteed::ArkSteedTask>(
        host_, nullptr, Jit::GetInstance(), function, CompilerTier(CompilerTier::Tier::ARKSTEED), name,
        MachineCode::INVALID_OSR_OFFSET, JitCompileMode(JitCompileMode::Mode::ASYNC));
    auto asyncTask = std::make_unique<arksteed::ArkSteedTask::AsyncTask>(task, host_->GetThreadId());
    asyncTask->Terminated();
    ASSERT_TRUE(pool->PostTask(std::move(asyncTask)));
    task->WaitFinish();
    // FINISH precedes completion-queue publication. Join completes publication
    // before the Host consumes the failed result; no task is resubmitted.
    {
        ThreadNativeScope native(host_);
        pool->Destroy();
    }
    EXPECT_FALSE(task->IsCompileSuccess());
    EXPECT_EQ(task->GetCompilerThread(), nullptr);
    EXPECT_EQ(task->GetCompilerTask(), nullptr);
    Jit::GetInstance()->InstallTasks(host_);
    EXPECT_FALSE(function->IsJitCompiling());
    task.reset();
    EXPECT_EQ(Jit::GetInstance()->GetRunningTaskCnt(vm_), 0U);
    {
        ThreadNativeScope native(host_);
        pool->Initialize(true, count);
        ASSERT_TRUE(pool->WaitForJitTaskPoolReady());
    }
}

HWTEST_F_L0(ParallelJitTest, CancelledArkSteedTaskDoesNotBindAWorker)
{
    JSHandle<JSFunction> empty;
    CString name("cancel-arksteed-before-run");
    auto task = std::make_shared<arksteed::ArkSteedTask>(
        host_, nullptr, Jit::GetInstance(), empty, CompilerTier(CompilerTier::Tier::ARKSTEED), name,
        MachineCode::INVALID_OSR_OFFSET, JitCompileMode(JitCompileMode::Mode::SYNC));
    auto asyncTask = std::make_unique<arksteed::ArkSteedTask::AsyncTask>(task, host_->GetThreadId());
    asyncTask->Terminated();
    ASSERT_TRUE(JitTaskpool::GetCurrentTaskpool()->PostTask(std::move(asyncTask)));
    task->WaitFinish();
    EXPECT_TRUE(task->IsFinish());
    EXPECT_FALSE(task->IsCompileSuccess());
    EXPECT_EQ(task->GetCompilerThread(), nullptr);
    EXPECT_EQ(task->GetCompilerTask(), nullptr);
}

HWTEST_F_L0(ParallelJitTest, DetachedOutputDoesNotBorrowCompilerBuffers)
{
    JSHandle<JSFunction> empty;
    CString name("detached-output");
    for (bool prepared : {false, true}) {
        arksteed::ArkSteedTask task(host_, nullptr, Jit::GetInstance(), empty,
                                    CompilerTier(CompilerTier::Tier::ARKSTEED), name,
                                    MachineCode::INVALID_OSR_OFFSET, JitCompileMode(JitCompileMode::Mode::SYNC));
        auto &desc = task.GetMachineCodeDesc();
        desc.isAsyncCompileMode = prepared;
        {
            std::vector<uint8_t> code {1, 2, 3, 4};
            FuncEntryDes entry {};
            entry.fpDeltaPrevFrameSp_ = FP_DELTA_PREV_FRAME_SP;
            entry.funcSize_ = code.size();
            task.SetCodeData(prepared ? nullptr : code.data(), code.size(), entry);
            code.assign(code.size(), 0);
            entry.fpDeltaPrevFrameSp_ = 0;
        }
        ASSERT_EQ(desc.codeSize, 4U);
        if (prepared) {
            EXPECT_EQ(desc.codeAddr, 0U);
        } else {
            EXPECT_EQ(reinterpret_cast<const uint8_t *>(desc.codeAddr)[0], 1);
        }
        auto *entry = reinterpret_cast<const FuncEntryDes *>(desc.funcEntryDesAddr);
        ASSERT_NE(entry, nullptr);
        EXPECT_EQ(entry->fpDeltaPrevFrameSp_, FP_DELTA_PREV_FRAME_SP);
        EXPECT_EQ(entry->funcSize_, 4U);
    }
}
#endif
}  // namespace panda::test
