/**
 * Copyright (c) 2021-2026 Huawei Device Co., Ltd.
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

#include "ecmascript/dfx/cpu_profiler/cpu_profiler.h"
#include "ecmascript/dfx/cpu_profiler/profiler_raw_reads.h"


#include <algorithm>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>

#include "ecmascript/compiler/aot_file/aot_file_manager.h"
#include "ecmascript/compiler/aot_file/an_file_data_manager.h"
#include "ecmascript/jspandafile/debug_info_extractor.h"
#include "ecmascript/jspandafile/js_pandafile_manager.h"
#include "ecmascript/platform/ffrt.h"
#include "libpandabase/os/thread.h"
#include "ecmascript/platform/profiler_signal_guard.h"
#ifdef PANDA_JS_ETS_HYBRID_MODE
#include "ecmascript/checkpoint/thread_state_transition.h"
#endif

#if defined(ENABLE_FFRT_INTERFACES)
#include "c/executor_task.h"
#endif

#if !defined(ECMASCRIPT_SUPPORT_CPUPROFILER)
    #error "ECMASCRIPT_SUPPORT_CPUPROFILER not defined"
#endif

// ECMASCRIPT_SUPPORT_CPUPROFILER_LEGACY selects the sampling scheme at compile
// time: defined -> the legacy in-handler-symbolization scheme (GetStackSignalHandler
// and its supporting paths) is active for the whole build; undefined (default)
// -> the lock-free two-phase scheme is the only scheme.

namespace panda::ecmascript {
static_assert(RAW_MAX_STACK_SIZE == static_cast<uint16_t>(MAX_STACK_SIZE));
namespace {
constexpr uint32_t HANDLER_DRAIN_POLL_INTERVAL_US = 100; // 100: microseconds per drain poll
constexpr int HANDLER_DRAIN_MAX_POLLS = 1000; // 1000: caps the Stop drain at 100ms
constexpr uint64_t HANDLER_DRAIN_ESCAPE_BUDGET_US = 5000000; // 5s: teardown drain escape before FATAL
} // namespace
Mutex CpuProfiler::synchronizationMutex_;
CMap<pthread_t, struct TaskInfo> CpuProfiler::profilerMap_ = CMap<pthread_t, struct TaskInfo>();
CpuProfiler::CpuProfiler(const EcmaVM *vm, const int interval) : vm_(vm), interval_(interval)
{
    enableVMTag_ = const_cast<EcmaVM *>(vm)->GetJSOptions().EnableCpuProfilerVMTag();
    generator_ = new SamplesRecord();
    generator_->SetEnableVMTag(enableVMTag_);
    generator_->NodeInit();
    if (generator_->SemInit(0, 0, 0) != 0) {
        LOG_ECMA(ERROR) << "sem_[0] init failed";
    }
    if (generator_->SemInit(1, 0, 0) != 0) {
        LOG_ECMA(ERROR) << "sem_[1] init failed";
    }
    if (generator_->SemInit(2, 0, 0) != 0) { // 2: signal 2
        LOG_ECMA(ERROR) << "sem_[2] init failed";
    }
    signalCtx_.profiler = this;
    signalCtx_.queue = &rawQueue_;
}

bool CpuProfiler::RegisterGetStackSignal()
{
    struct sigaction sa;
#if defined(ECMASCRIPT_SUPPORT_CPUPROFILER_LEGACY)
    sa.sa_sigaction = &GetStackSignalHandler;
#else
    sa.sa_sigaction = &GetStackSignalHandlerLockFree;
#endif
    if (sigemptyset(&sa.sa_mask) != 0) {
        LOG_ECMA(ERROR) << "CpuProfiler::RegisterGetStackSignal, sigemptyset failed, errno = " << errno;
        return false;
    }
    // No SA_ONSTACK: the handler runs on the interrupted thread's own stack,
    // within the runtime's JS-stack-limit headroom (same as the legacy scheme);
    // a per-thread altstack cannot cover FFRT carrier migration anyway.
    sa.sa_flags = SA_RESTART | SA_SIGINFO;
    if (sigaction(SIGPROF, &sa, nullptr) != 0) {
        LOG_ECMA(ERROR) << "CpuProfiler::RegisterGetStackSignal, sigaction failed, errno = " << errno;
        return false;
    }
    return true;
}

bool CpuProfiler::StartCpuProfilerForInfo()
{
    LOG_ECMA(INFO) << "CpuProfiler::StartCpuProfilerForInfo, sampling interval = " << interval_;
    if (isProfiling_) {
        LOG_ECMA(ERROR) << "CpuProfiler::StartCpuProfilerForInfo, can not start when CpuProfiler is Profiling";
        return false;
    }
    void *taskHandle = nullptr;
    if (!PrepareProfilerSession(&taskHandle)) {
        return false;
    }
    generator_->SetTimeDeltaThreshold(interval_ * THRESHOLD_GROWTH_FACTORY + THRESHOLD_FIXED_INCREMENT);
#if !defined(ECMASCRIPT_SUPPORT_CPUPROFILER_LEGACY)
    if (!StartProfilerCommon()) {
        return false;
    }
#endif
    generator_->SetIsStart(true);
    uint64_t startTime = SamplingProcessor::GetMicrosecondsTimeStamp();
    generator_->SetThreadStartTime(startTime);
    auto *params = new RunParams(generator_, static_cast<uint32_t>(interval_), pthread_self(), taskHandle, this);
    if (pthread_create(&tid_, nullptr, SamplingProcessor::Run, params) != 0) {
        LOG_ECMA(ERROR) << "CpuProfiler::StartCpuProfilerForInfo, pthread_create failed, errno = " << errno;
        delete params;
#if !defined(ECMASCRIPT_SUPPORT_CPUPROFILER_LEGACY)
        AbortProfilerStart();
#endif
        return false;
    }
    isProfiling_ = true;
    vm_->GetJSThread()->SetIsProfiling(true);
    outToFile_ = false;
    return true;
}

bool CpuProfiler::PrepareProfilerSession(void **taskHandle)
{
    if (!RegisterGetStackSignal()) {
        return false;
    }
    // when the ffrt is enabled for the thread, the tid_ will be task id
    tid_ = static_cast<pthread_t>(JSThread::GetCurrentThreadId());
    *taskHandle = nullptr;
    // when the ffrt is enabled for the thread,
    // we record the task handle, which can be used to obtain the running thread id in the task
#if defined(ENABLE_FFRT_INTERFACES)
    *taskHandle = ffrt_get_cur_task();
    TaskInfo taskInfo { vm_, *taskHandle };
#else
    TaskInfo taskInfo { vm_, nullptr };
#endif // defined(ENABLE_FFRT_INTERFACES)
    {
        LockHolder lock(synchronizationMutex_);
        profilerMap_[tid_] = taskInfo;
    }

    JSPandaFileManager *pandaFileManager = JSPandaFileManager::GetInstance();
    pandaFileManager->EnumerateJSPandaFiles([&](const std::shared_ptr<JSPandaFile> &file) -> bool {
        pandaFileManager->CpuProfilerGetJSPtExtractor(file.get());
        return true;
    });
    return true;
}

void CpuProfiler::SetProfilerSignalState(ProfilerSignalState state)
{
    signalCtx_.profilerState.store(static_cast<uint8_t>(state), std::memory_order_release);
}

bool CpuProfiler::StartProfilerCommon()
{
    signalCtx_.jsThread = vm_->GetAssociatedJSThread();
    signalCtx_.anFileDataManager = AnFileDataManager::GetInstance();
    WarmupProfilerSignalGuard();
    signalCtx_.generation.fetch_add(1, std::memory_order_acq_rel);
    ResetSamplingStats();
    RawSample stale;
    while (rawQueue_.TryPop(stale)) {}
    symbolizer_.BeginSession();
    ProfilerMetadataRegistry::Instance().Activate();
    signalCtx_.metadataSequence = ProfilerMetadataRegistry::Instance().PublishedSequence();
#ifdef PANDA_JS_ETS_HYBRID_MODE
    signalCtx_.crossVMOperator = vm_->GetCrossVMOperator();
    signalCtx_.crossVmOperatorPresent = (signalCtx_.crossVMOperator != nullptr);
#else
    signalCtx_.crossVmOperatorPresent = false;
#endif
    JSPandaFileManager *pandaFileManager = JSPandaFileManager::GetInstance();
    pandaFileManager->EnumerateJSPandaFiles([&](const std::shared_ptr<JSPandaFile> &file) -> bool {
        symbolizer_.SeedLiveFile(file.get());
        return true;
    });
    // Key by tid: the handler must not call FFRT (ffrt_get_cur_cached_task_id
    // lazily allocates an ExecuteCtx, which deadlocks or crashes in a signal
    // handler under malloc contention). RebindFfrtThread migrates the binding
    // when the FFRT task moves to another worker.
    signalCtx_.threadOrTaskId = GetThreadIdOrCachedTaskId();
    uint64_t key = static_cast<uint64_t>(os::thread::GetCurrentThreadId());
    {
        LockHolder lock(synchronizationMutex_);
        if (!g_profilerSignalContextTable.HasFreeSlot(key)) {
            LOG_ECMA(ERROR) << "CpuProfiler::StartProfilerCommon, signal context table full";
            ProfilerMetadataRegistry::Instance().Deactivate();
            return false;
        }
        g_profilerSignalContextTable.Publish(key, &signalCtx_);
        signalCtxKey_ = key;
    }
    SetProfilerSignalState(ProfilerSignalState::RUNNING);
    return true;
}

void CpuProfiler::AbortProfilerStart()
{
    SetProfilerSignalState(ProfilerSignalState::STOPPED);
    {
        LockHolder lock(synchronizationMutex_);
        g_profilerSignalContextTable.UnpublishContext(&signalCtx_);
    }
    ProfilerMetadataRegistry::Instance().Deactivate();
    generator_->SetIsStart(false);
}

void CpuProfiler::RebindSignalContext(uint64_t newTid)
{
    // Sampling thread only: migrate the tid binding when the FFRT task moves to
    // another worker. Unpublish frees the slot before Publish re-adds it, so
    // table occupancy cannot grow.
    LockHolder lock(synchronizationMutex_);
    g_profilerSignalContextTable.Unpublish(signalCtxKey_, &signalCtx_);
    g_profilerSignalContextTable.Publish(newTid, &signalCtx_);
    signalCtxKey_ = newTid;
}

bool CpuProfiler::StartCpuProfilerForFile(const std::string &fileName)
{
    LOG_ECMA(INFO) << "CpuProfiler::StartCpuProfilerForFile, sampling interval = " << interval_;
    if (isProfiling_) {
        LOG_ECMA(ERROR) << "CpuProfiler::StartCpuProfilerForFile, can not start when CpuProfiler is Profiling";
        return false;
    }
    std::string absoluteFilePath("");
    if (!CheckFileName(fileName, absoluteFilePath)) {
        return false;
    }
    fileName_ = absoluteFilePath;
    generator_->SetFileName(fileName_);
    generator_->fileHandle_.open(fileName_.c_str());
    if (generator_->fileHandle_.fail()) {
        LOG_ECMA(ERROR) << "CpuProfiler::StartCpuProfilerForFile, fileHandle_ open failed";
        return false;
    }
    void *taskHandle = nullptr;
    if (!PrepareProfilerSession(&taskHandle)) {
        return false;
    }
    generator_->SetTimeDeltaThreshold(interval_ * THRESHOLD_GROWTH_FACTORY + THRESHOLD_FIXED_INCREMENT);
#if !defined(ECMASCRIPT_SUPPORT_CPUPROFILER_LEGACY)
    if (!StartProfilerCommon()) {
        return false;
    }
#endif
    generator_->SetIsStart(true);
    uint64_t startTime = SamplingProcessor::GetMicrosecondsTimeStamp();
    generator_->SetThreadStartTime(startTime);
    auto *params = new RunParams(generator_, static_cast<uint32_t>(interval_), pthread_self(), taskHandle, this);
    if (pthread_create(&tid_, nullptr, SamplingProcessor::Run, params) != 0) {
        LOG_ECMA(ERROR) << "CpuProfiler::StartCpuProfilerForFile, pthread_create failed, errno = " << errno;
        delete params;
#if !defined(ECMASCRIPT_SUPPORT_CPUPROFILER_LEGACY)
        AbortProfilerStart();
#endif
        return false;
    }
    isProfiling_ = true;
    vm_->GetJSThread()->SetIsProfiling(true);
    outToFile_ = true;
    return true;
}

bool CpuProfiler::StopCpuProfilerForInfo(std::unique_ptr<struct ProfileInfo> &profileInfo)
{
    LOG_ECMA(INFO) << "CpuProfiler::StopCpuProfilerForInfo enter";
    if (!isProfiling_) {
        LOG_ECMA(WARN) << "CpuProfiler::StopCpuProfilerForInfo, not isProfiling_";
        return true;
    }
    if (outToFile_) {
        LOG_ECMA(ERROR) << "CpuProfiler::StopCpuProfilerForInfo, is outToFile_";
        return false;
    }
    if (!StopProfilerCommon()) {
        return false;
    }
    profileInfo = generator_->GetProfileInfo();
    return true;
}

void CpuProfiler::SetCpuSamplingInterval(int interval)
{
    interval_ = static_cast<uint32_t>(interval);
}

bool CpuProfiler::StopCpuProfilerForFile()
{
    LOG_ECMA(INFO) << "CpuProfiler::StopCpuProfilerForFile enter";
    if (!isProfiling_) {
        LOG_ECMA(WARN) << "CpuProfiler::StopCpuProfilerForFile, not isProfiling_";
        return true;
    }
    if (!outToFile_) {
        LOG_ECMA(ERROR) << "CpuProfiler::StopCpuProfilerForFile, not outToFile_";
        return false;
    }
    if (!StopProfilerCommon()) {
        return false;
    }
    generator_->StringifySampleData();
    std::string fileData = generator_->GetSampleData();
    generator_->fileHandle_ << fileData;
    return true;
}

bool CpuProfiler::StopProfilerCommon()
{
#if defined(ECMASCRIPT_SUPPORT_CPUPROFILER_LEGACY)
    // Legacy stop: break the sampling loop, wake a handler that may be
    // blocked in the per-sample ACK wait, then join the processor thread.
    generator_->SetIsStart(false);
    if (generator_->SemPost(0) != 0) {
        LOG_ECMA(ERROR) << "CpuProfiler::StopProfilerCommon, sem_[0] post failed, errno = " << errno;
        return false;
    }
    if (generator_->SemWait(1) != 0) {
        LOG_ECMA(ERROR) << "CpuProfiler::StopProfilerCommon, sem_[1] wait failed, errno = " << errno;
        return false;
    }
    isProfiling_ = false;
    vm_->GetJSThread()->SetIsProfiling(false);
    return true;
#else
    // Forbid new handler entries first; the handler checks the state before touching
    // the queue, so after STOPPING no new sample can be produced (plan §8.1).
    SetProfilerSignalState(ProfilerSignalState::STOPPING);
    generator_->SetIsStart(false);
    // Bounded drain of already-entered handlers; never blocks on a specific sample.
    WaitHandlersDrained();
    if (generator_->SemWait() != 0) {
        LOG_ECMA(ERROR) << "CpuProfiler::StopProfilerCommon, sem wait failed, errno = " << errno;
        return false;
    }
    ConsumeRawSamples();
    // Fold residual legacy-channel frames (hybrid/NAPI) posted by handlers that
    // finished during the drain; the in-loop ConsumeAndFold has already stopped.
    {
        auto frame = std::make_unique<FrameStackAndInfo>();
        while (generator_->samplesQueue_->PopFrame(*frame)) {
            generator_->AddSample(*frame);
        }
    }
    {
        LockHolder lock(synchronizationMutex_);
        g_profilerSignalContextTable.UnpublishContext(&signalCtx_);
    }
    SetProfilerSignalState(ProfilerSignalState::STOPPED);
    LogSamplingStats();
    // The last Deactivate releases keep-alives; no handler may still be alive.
    ASSERT(signalCtx_.activeHandlerCount.load(std::memory_order_acquire) == 0);
    ProfilerMetadataRegistry::Instance().Deactivate();
    isProfiling_ = false;
    vm_->GetJSThread()->SetIsProfiling(false);
    return true;
#endif
}

void CpuProfiler::ResetSamplingStats()
{
    // Called from StartProfilerCommon while the state is still STOPPED, so no
    // handler or processor can race the reset; stats therefore stay per-session.
    signalCtx_.dropQueueFull.store(0, std::memory_order_relaxed);
    signalCtx_.dropGcSkipped.store(0, std::memory_order_relaxed);
    signalCtx_.dropStackNotIterable.store(0, std::memory_order_relaxed);
    signalCtx_.dropInvalidFrame.store(0, std::memory_order_relaxed);
    signalCtx_.dropMetadataStale.store(0, std::memory_order_relaxed);
    signalCtx_.samplesRecorded.store(0, std::memory_order_relaxed);
    signalCtx_.handlerOverrunTicks.store(0, std::memory_order_relaxed);
}

void CpuProfiler::LogSamplingStats()
{
    // Called from StopProfilerCommon after handlers drained and the queue was
    // consumed, so every writer of the session has finished.
    LOG_ECMA(INFO) << "CpuProfiler session stats: samples="
        << signalCtx_.samplesRecorded.load(std::memory_order_relaxed)
        << ", dropQueueFull=" << signalCtx_.dropQueueFull.load(std::memory_order_relaxed)
        << ", dropGcSkipped=" << signalCtx_.dropGcSkipped.load(std::memory_order_relaxed)
        << ", dropStackNotIterable=" << signalCtx_.dropStackNotIterable.load(std::memory_order_relaxed)
        << ", dropInvalidFrame=" << signalCtx_.dropInvalidFrame.load(std::memory_order_relaxed)
        << ", dropMetadataStale=" << signalCtx_.dropMetadataStale.load(std::memory_order_relaxed)
        << ", handlerOverrunTicks=" << signalCtx_.handlerOverrunTicks.load(std::memory_order_relaxed)
        << ", tableMiss(process)=" << GetProfilerSignalTableMisses();
}

void CpuProfiler::WaitHandlersDrained()
{
    // The handler performs bounded work only, so the poll budget is a generous
    // upper bound on the drain time.
    for (int i = 0; i < HANDLER_DRAIN_MAX_POLLS; i++) {
        if (signalCtx_.activeHandlerCount.load(std::memory_order_acquire) == 0) {
            return;
        }
        usleep(HANDLER_DRAIN_POLL_INTERVAL_US);
    }
    LOG_ECMA(ERROR) << "CpuProfiler::WaitHandlersDrained timeout";
}

void CpuProfiler::WaitHandlersDrainedUnbounded()
{
    // Teardown, unlike Stop, may block: an in-flight handler must fully exit
    // before rawQueue_/signalCtx_ members are freed, otherwise it resumes on
    // freed memory (Oracle review F4). A handler stuck past the escape budget
    // is blocked on something the signal-safety contract forbids: abort with
    // a diagnosable crash rather than hang.
    uint64_t waitedUs = 0;
    uint32_t activeHandlers = signalCtx_.activeHandlerCount.load(std::memory_order_acquire);
    while (activeHandlers != 0) {
        usleep(HANDLER_DRAIN_POLL_INTERVAL_US);
        waitedUs += HANDLER_DRAIN_POLL_INTERVAL_US;
        if (waitedUs >= HANDLER_DRAIN_ESCAPE_BUDGET_US) {
            LOG_ECMA(FATAL) << "CpuProfiler::WaitHandlersDrainedUnbounded, handler drain exceeded "
                            << HANDLER_DRAIN_ESCAPE_BUDGET_US << "us escape budget";
        }
        activeHandlers = signalCtx_.activeHandlerCount.load(std::memory_order_acquire);
    }
}

CpuProfiler::~CpuProfiler()
{
    // Lock-free teardown primitives are benign in legacy mode: the state was
    // never RUNNING, no context was published, and the handler never raises
    // the active count.
    SetProfilerSignalState(ProfilerSignalState::STOPPED);
    WaitHandlersDrainedUnbounded();
    {
        LockHolder lock(synchronizationMutex_);
        g_profilerSignalContextTable.UnpublishContext(&signalCtx_);
    }
    if (generator_->SemDestroy(0) != 0) {
        LOG_ECMA(ERROR) << "sem_[0] destroy failed";
    }
    if (generator_->SemDestroy(1) != 0) {
        LOG_ECMA(ERROR) << "sem_[1] destroy failed";
    }
    if (generator_->SemDestroy(2) != 0) { // 2: signal 2
        LOG_ECMA(ERROR) << "sem_[2] destroy failed";
    }
    if (generator_ != nullptr) {
        delete generator_;
        generator_ = nullptr;
    }
}

#if defined(ECMASCRIPT_SUPPORT_CPUPROFILER_LEGACY) || defined(PANDA_JS_ETS_HYBRID_MODE)
void CpuProfiler::GetStack(FrameIterator &it)
{
    const CMap<struct MethodKey, struct FrameInfo> &stackInfo = generator_->GetStackInfo();
    bool topFrame = true;
    generator_->ResetFrameLength();
    for (; !it.Done(); it.Advance<>()) {
        auto method = it.CheckAndGetMethod();
        if (method == nullptr || !JSTaggedValue(method).IsMethod()) {
            continue;
        }
        bool isNative = method->IsNativeWithCallField();
        struct MethodKey methodKey;
        methodKey.deoptType = method->GetDeoptType();
        if (topFrame) {
            methodKey.state = JsStackGetter::GetRunningState(it, vm_, isNative, true, enableVMTag_);
            topFrame = false;
        } else {
            methodKey.state = JsStackGetter::GetRunningState(it, vm_, isNative, false, enableVMTag_);
        }
        if (isNative) {
            JsStackGetter::GetCallLineNumber(it, vm_, methodKey.lineNumber);
        }
        void *methodIdentifier = JsStackGetter::GetMethodIdentifier(method, it, vm_);
        if (methodIdentifier == nullptr) {
            continue;
        }
        methodKey.methodIdentifier = methodIdentifier;
        if (stackInfo.count(methodKey) == 0) {
            struct FrameInfoTemp codeEntry;
            if (UNLIKELY(!JsStackGetter::ParseMethodInfo(methodKey, it, vm_, codeEntry, true))) {
                continue;
            }
            if (UNLIKELY(!generator_->PushStackInfo(codeEntry))) {
                return;
            }
        }
        if (UNLIKELY(!generator_->PushFrameStack(methodKey))) {
            return;
        }
    }
    generator_->PostFrame();
}
#endif // defined(ECMASCRIPT_SUPPORT_CPUPROFILER_LEGACY) || defined(PANDA_JS_ETS_HYBRID_MODE)

bool CpuProfiler::GetStackBeforeCallNapi(JSThread *thread)
{
    uint64_t tempTimeStamp = SamplingProcessor::GetMicrosecondsTimeStamp();
    if (tempTimeStamp - beforeCallNapiTimeStamp_ < interval_) {
        return false;
    }

    if (GetStackCallNapi(thread, true)) {
        beforeCallNapiTimeStamp_ = tempTimeStamp;
        return true;
    }
    return false;
}

void CpuProfiler::GetStackAfterCallNapi(JSThread *thread)
{
    GetStackCallNapi(thread, false);
}

#if defined(PANDA_JS_ETS_HYBRID_MODE)
void CpuProfiler::ProcessStaticFrame(const void *frame)
{
    arkplatform::HybridFrameInfo frameInfo;

    auto *crossVMOperator = vm_->GetCrossVMOperator();
    if (crossVMOperator == nullptr || !crossVMOperator->GetStaticFrameInfo(frame, frameInfo)) {
        LOG_ECMA(ERROR) << "CpuProfiler::ProcessStaticFrame, GetStaticFrameInfo failed";
        return;
    }

    generator_->PostHybridStackFrame(frameInfo);
}

void CpuProfiler::ProcessDynamicFrame(const void *frame, JSThread *thread)
{
    if (frame == nullptr) {
        LOG_ECMA(ERROR) << "CpuProfiler::ProcessDynamicFrame, frame is nullptr";
        return;
    }

    auto *frameHandler = static_cast<FrameHandler *>(const_cast<void *>(frame));
    FrameIterator it(frameHandler->GetSp(), thread);
    GetStack(it);
}

void CpuProfiler::ProcessHybridStack(JSThread *thread)
{
    if (thread == nullptr) {
        LOG_ECMA(ERROR) << "CpuProfiler::ProcessHybridStack, thread is nullptr";
        return;
    }

    // Use ForEachFrameInUnionStack API to iterate over the hybrid stack.
    size_t staticFrameCount = 0;
    size_t dynamicFrameCount = 0;

    auto *crossVMOperator = vm_->GetCrossVMOperator();
    if (crossVMOperator != nullptr) {
        crossVMOperator->ForEachFrameInUnionStack(
            [this, thread, &staticFrameCount, &dynamicFrameCount](const void *frame, bool isStaticFrame) {
                if (frame == nullptr) {
                    LOG_ECMA(WARN) << "CpuProfiler::ProcessHybridStack, frame is nullptr, isStaticFrame="
                                   << isStaticFrame;
                    return;
                }
                if (isStaticFrame) {
                    ProcessStaticFrame(frame);
                    staticFrameCount++;
                } else {
                    ProcessDynamicFrame(frame, thread);
                    dynamicFrameCount++;
                }
            });
    }

    LOG_ECMA(INFO) << "CpuProfiler::ProcessHybridStack, processed " << staticFrameCount
                   << " static frames and " << dynamicFrameCount << " dynamic frames";
}
#endif // defined(PANDA_JS_ETS_HYBRID_MODE)

bool CpuProfiler::GetStackCallNapi(JSThread *thread, bool beforeCallNapi)
{
    [[maybe_unused]] CallNapiScope scope(this);
    const CMap<struct MethodKey, struct FrameInfo> &stackInfo = generator_->GetStackInfo();
    generator_->ClearNapiStack();
    bool topFrame = true;
    auto currentFrame = const_cast<JSTaggedType *>(thread->GetCurrentFrame());
    FrameIterator it(currentFrame, thread);
    if (!beforeCallNapi) {
        it.Advance<GCVisitedFlag::IGNORED>();
    }
    for (; !it.Done(); it.Advance<GCVisitedFlag::IGNORED>()) {
        auto method = it.CheckAndGetMethod();
        if (method == nullptr || !JSTaggedValue(method).IsMethod()) {
            continue;
        }

        bool isNative = method->IsNativeWithCallField();
        struct MethodKey methodKey;
        methodKey.deoptType = method->GetDeoptType();
        if (topFrame) {
            if (beforeCallNapi) {
                methodKey.state = RunningState::NAPI;
            } else {
                methodKey.state = JsStackGetter::GetRunningState(it, vm_, isNative, true, enableVMTag_);
            }
            topFrame = false;
        } else {
            methodKey.state = JsStackGetter::GetRunningState(it, vm_, isNative, false, enableVMTag_);
        }
        if (isNative) {
            JsStackGetter::GetCallLineNumber(it, vm_, methodKey.lineNumber);
        }
        void *methodIdentifier = JsStackGetter::GetMethodIdentifier(method, it, vm_);
        if (methodIdentifier == nullptr) {
            continue;
        }
        methodKey.methodIdentifier = methodIdentifier;
        if (stackInfo.count(methodKey) == 0) {
            struct FrameInfoTemp codeEntry;
            if (UNLIKELY(!JsStackGetter::ParseMethodInfo(methodKey, it, vm_, codeEntry, true))) {
                continue;
            }
            if (UNLIKELY(!generator_->PushNapiStackInfo(codeEntry))) {
                return false;
            }
        }
        if (UNLIKELY(!generator_->PushNapiFrameStack(methodKey))) {
            return false;
        }
    }
    generator_->PostNapiFrame();
    return true;
}

// Explicit mapping keeps the lean raw enum and the profile-facing RunningState from
// silently drifting apart.
static RunningState ToRunningState(RawRunningState raw)
{
    switch (raw) {
        case RawRunningState::GC:
            return RunningState::GC;
        case RawRunningState::CINT:
            return RunningState::CINT;
        case RawRunningState::AINT:
            return RunningState::AINT;
        case RawRunningState::AOT:
            return RunningState::AOT;
        case RawRunningState::AINT_D:
            return RunningState::AINT_D;
        case RawRunningState::BUILTIN:
            return RunningState::BUILTIN;
        case RawRunningState::NAPI:
            return RunningState::NAPI;
        case RawRunningState::ARKUI_ENGINE:
            return RunningState::ARKUI_ENGINE;
        case RawRunningState::RUNTIME:
            return RunningState::RUNTIME;
        case RawRunningState::JIT:
            return RunningState::JIT;
        case RawRunningState::OTHER:
        default:
            return RunningState::OTHER;
    }
}

static RawRunningState ToRawRunningState(RunningState state)
{
    switch (state) {
        case RunningState::GC:
            return RawRunningState::GC;
        case RunningState::CINT:
            return RawRunningState::CINT;
        case RunningState::AINT:
            return RawRunningState::AINT;
        case RunningState::AOT:
            return RawRunningState::AOT;
        case RunningState::AINT_D:
            return RawRunningState::AINT_D;
        case RunningState::BUILTIN:
            return RawRunningState::BUILTIN;
        case RunningState::NAPI:
            return RawRunningState::NAPI;
        case RunningState::ARKUI_ENGINE:
            return RawRunningState::ARKUI_ENGINE;
        case RunningState::RUNTIME:
            return RawRunningState::RUNTIME;
        case RunningState::JIT:
            return RawRunningState::JIT;
        case RunningState::OTHER:
        default:
            return RawRunningState::OTHER;
    }
}

static RunningState GetRunningStateRawTagged(const SignalSafeProfilerFrameIterator &it,
                                             const EcmaVM *vm, bool isNative, bool topFrame);

// Raw counterpart of JsStackGetter::GetRunningState for the signal-safe
// iterator: identical classification logic, every tagged read no-barrier.
static RunningState GetRunningStateRaw(const SignalSafeProfilerFrameIterator &it, const EcmaVM *vm,
                                       bool isNative, bool topFrame, bool enableVMTag)
{
    if (enableVMTag) {
        return GetRunningStateRawTagged(it, vm, isNative, topFrame);
    }

    JSThread *thread = vm->GetAssociatedJSThread();
    JSFunction *function = JSFunction::Cast(it.GetFunction().GetTaggedObject());

    if (topFrame) {
        if (function->IsCallNapi()) {
            return RunningState::NAPI;
        }
        if (thread->GetGcState()) {
            return RunningState::GC;
        }
    }

    if (function->IsCallNapi()) {
        return RunningState::NAPI;
    }
    if (isNative) {
        return GetNativeFunctionExtraInfoNoBarrier(function).CheckIsJSNativePointer()
            ? RunningState::ARKUI_ENGINE : RunningState::BUILTIN;
    }

    return RunningState::OTHER;
}

// VM-tag branch of GetRunningStateRaw: same no-barrier classification under
// the finer-grained execution-state tagging.
static RunningState GetRunningStateRawTagged(const SignalSafeProfilerFrameIterator &it,
                                             const EcmaVM *vm, bool isNative, bool topFrame)
{
    JSThread *thread = vm->GetAssociatedJSThread();
    JSFunction *function = JSFunction::Cast(it.GetFunction().GetTaggedObject());

    if (topFrame) {
        if (thread->GetGcState()) {
            return RunningState::GC;
        }
        if (isNative && (it.IsLeaveFrame() || thread->GetRuntimeState())) {
            return RunningState::RUNTIME;
        }
    }

    if (function->IsCallNapi()) {
        return RunningState::NAPI;
    }
    if (isNative) {
        return GetNativeFunctionExtraInfoNoBarrier(function).CheckIsJSNativePointer()
            ? RunningState::ARKUI_ENGINE : RunningState::BUILTIN;
    }
    if (it.IsFastJitFunctionFrame() || it.IsSteedFunctionFrame()) {
        return RunningState::JIT;
    }
    if (it.IsOptimizedJSFunctionFrame()) {
        return RunningState::AOT;
    }
    if (thread->IsAsmInterpreter()) {
        // For Methods that is compiled in AOT but deoptimized at runtime, we mark it as AINT-D
        Method *method = GetMethodFieldNoBarrier(function);
        MethodLiteral *methodLiteral = method->GetMethodLiteralNoBarrier();
        if (methodLiteral != nullptr && MethodLiteral::IsAotWithCallField(methodLiteral->GetCallField())) {
            return RunningState::AINT_D;
        }
        return RunningState::AINT;
    }
    return RunningState::CINT;
}

// Raw counterpart of JsStackGetter::GetMethodIdentifier for the signal-safe
// iterator: identical identity resolution, every tagged read no-barrier.
static void *GetMethodIdentifierRaw(Method *method, const SignalSafeProfilerFrameIterator &it,
                                    const EcmaVM *vm)
{
    JSFunction *function = JSFunction::Cast(it.GetFunction().GetTaggedObject());
    if (method->IsNativeWithCallField()) {
        JSTaggedValue extraInfoValue = GetNativeFunctionExtraInfoNoBarrier(function);
        if (extraInfoValue.CheckIsJSNativePointer()) {
            JSNativePointer *extraInfo = JSNativePointer::Cast(extraInfoValue.GetTaggedObject());
            return reinterpret_cast<void *>(extraInfo->GetData());
        }
        return function->GetNativePointer();
    }

    return reinterpret_cast<void *>(method->GetMethodLiteralNoBarrier());
}

static RawFrameKind ClassifyRawFrameKind(const SignalSafeProfilerFrameIterator &it, bool isNative)
{
    if (isNative) {
        return RawFrameKind::BUILTIN;
    }
    if (it.IsFastJitFunctionFrame() || it.IsSteedFunctionFrame()) {
        return RawFrameKind::JIT;
    }
    if (it.IsOptimizedJSFunctionFrame()) {
        return RawFrameKind::AOT;
    }
    return RawFrameKind::INTERPRETER;
}

#if defined(ECMASCRIPT_SUPPORT_CPUPROFILER_LEGACY)
// Legacy signal handler, retained verbatim from the pre-refactor implementation:
// it symbolizes the whole stack inside the signal context and ACKs the sampling
// thread through sem_[0]. Its calls are NOT async-signal-safe (see issue #13618)
// and it is compiled only when ECMASCRIPT_SUPPORT_CPUPROFILER_LEGACY is defined
// (off by default: the lock-free scheme is the default and only scheme).
void CpuProfiler::GetStackSignalHandler(int signal, [[maybe_unused]] siginfo_t *siginfo, void *context)
{
    if (signal != SIGPROF) {
        return;
    }
    CpuProfiler *profiler = nullptr;
    JSThread *thread = nullptr;
    {
        LockHolder lock(synchronizationMutex_);
        // If no task running in this thread, we get the id of the last task that ran in this thread
        pthread_t tid = static_cast<pthread_t>(GetThreadIdOrCachedTaskId());
        auto it = profilerMap_.find(tid);
        if (it == profilerMap_.end()) {
            LOG_ECMA(ERROR) << "CpuProfiler GetStackSignalHandler tid not found: " << tid;
            return;
        }
        const EcmaVM *vm = it->second.vm_;
        if (vm == nullptr) {
            LOG_ECMA(ERROR) << "CpuProfiler GetStackSignalHandler vm is nullptr";
            return;
        }
        profiler = vm->GetProfiler();
        thread = vm->GetAssociatedJSThread();
        if (profiler == nullptr) {
            LOG_ECMA(ERROR) << "CpuProfiler GetStackSignalHandler profiler is nullptr";
            return;
        }
    }
    [[maybe_unused]] SignalStateScope scope(thread->GetEcmaVM()->GetJsDebuggerManager());

    if (profiler->GetBuildNapiStack() || thread->GetGcState()) {
        if (profiler->generator_->SemPost(0) != 0) {
            LOG_ECMA(ERROR) << "sem_[0] post failed";
        }
        return;
    }

#ifdef PANDA_JS_ETS_HYBRID_MODE
    // Check whether the current stack is a hybrid(dynamic/static) stack.
    EcmaVM *vm = thread->GetEcmaVM();
    bool isEmpty = true;
    auto *crossVMOperator = vm->GetCrossVMOperator();
    if (crossVMOperator != nullptr) {
        crossVMOperator->UnionStackIsEmpty(&isEmpty);
    }
    if (!isEmpty) {
        // Process the hybrid stack.
        profiler->ProcessHybridStack(thread);

        if (profiler->generator_->SemPost(0) != 0) {
            LOG_ECMA(ERROR) << "sem_[0] post failed";
        }
        return;
    }
#endif

    uint64_t pc = 0;
    if (thread->IsAsmInterpreter()) {
        // If the attempt fails, the callback will be terminated directly to avoid the reentrancy deadlock,
        // and a sampling will be abandoned. Failures are rare, so the impact on the overall sampling results
        // is very limited.
        if (!thread->GetEcmaVM()->GetAOTFileManager()->TryReadLock()) {
            if (profiler->generator_->SemPost(0) != 0) {
                LOG_ECMA(ERROR) << "sem_[0] post failed";
            }
            return;
        }
        pc = GetPcFromContext(context);
    }
    if (thread->IsAsmInterpreter() && profiler->IsAddrAtStubOrAot(pc) &&
        !profiler->IsEntryFrameHeaderOrTail(thread, pc)) {
        if (profiler->IfNeedSkipBarrierStubHeaderOrTail(thread, pc)) {
            return;
        }
        [[maybe_unused]] ucontext_t *ucontext = reinterpret_cast<ucontext_t*>(context);
        [[maybe_unused]] mcontext_t &mcontext = ucontext->uc_mcontext;
        [[maybe_unused]] void *fp = nullptr;
        [[maybe_unused]] void *sp = nullptr;
#if defined(PANDA_TARGET_AMD64)
        fp = reinterpret_cast<void*>(mcontext.gregs[REG_RBP]);
        sp = reinterpret_cast<void*>(mcontext.gregs[REG_RSP]);
#elif defined(PANDA_TARGET_ARM64)
        fp = reinterpret_cast<void*>(mcontext.regs[29]); // FP is an alias for x29.
        sp = reinterpret_cast<void*>(mcontext.sp);
#else
        LOG_FULL(FATAL) << "AsmInterpreter does not currently support other platforms, please run on x64 and arm64";
        return;
#endif
        if (sp > fp) {
            LOG_ECMA(ERROR) << "sp > fp, stack frame exception";
            if (profiler->generator_->SemPost(0) != 0) {
                LOG_ECMA(ERROR) << "sem_[0] post failed";
            }
            return;
        }
        if (JsStackGetter::CheckFrameType(thread, reinterpret_cast<JSTaggedType *>(fp))) {
            FrameIterator it(reinterpret_cast<JSTaggedType *>(fp), thread);
            profiler->GetStack(it);
        }
    } else if (thread->IsAsmInterpreter()) {
        if (thread->GetLastLeaveFrame() != nullptr) {
            JSTaggedType *leaveFrame = const_cast<JSTaggedType *>(thread->GetLastLeaveFrame());
            if (JsStackGetter::CheckFrameType(thread, leaveFrame)) {
                FrameIterator it(leaveFrame, thread);
                profiler->GetStack(it);
            }
        }
    } else {
        if (thread->GetCurrentFrame() != nullptr) {
            if (JsStackGetter::CheckFrameType(thread, const_cast<JSTaggedType *>(thread->GetCurrentFrame()))) {
                FrameHandler frameHandler(thread);
                FrameIterator it(frameHandler.GetSp(), thread);
                profiler->GetStack(it);
            }
        }
    }
    if (profiler->generator_->SemPost(0) != 0) {
        LOG_ECMA(ERROR) << "sem_[0] post failed";
        return;
    }
}
#endif // ECMASCRIPT_SUPPORT_CPUPROFILER_LEGACY

#if !defined(ECMASCRIPT_SUPPORT_CPUPROFILER_LEGACY)
// Signal-context entry point. Contract (plan §6): this handler performs no heap
// allocation, takes no locks, creates no handles, reads no JS properties and writes
// no logs; it only publishes a raw sample through the lock-free queue or drops it.
void CpuProfiler::GetStackSignalHandlerLockFree(int signal, [[maybe_unused]] siginfo_t *siginfo, void *context)
{
    if (signal != SIGPROF) {
        return;
    }
    CpuProfilerSignalContext *ctx = g_profilerSignalContextTable.Lookup(
        static_cast<uint64_t>(os::thread::GetCurrentThreadId()));
    if (ctx == nullptr || !ctx->IsRunning()) {
        return;
    }
    if (!ctx->shouldRecordSample.exchange(false, std::memory_order_acquire)) {
        return;
    }
    ctx->activeHandlerCount.fetch_add(1, std::memory_order_acq_rel);
    // Re-check after publishing the count so Stop either observes this handler or
    // this handler observes Stop (plan §8.1 stop-safety).
    if (!ctx->IsRunning()) {
        ctx->activeHandlerCount.fetch_sub(1, std::memory_order_release);
        return;
    }
    ctx->handlerRunning.store(true, std::memory_order_release);
    [[maybe_unused]] SignalStateScope scope(ctx->jsThread->GetEcmaVM()->GetJsDebuggerManager());
    // Plan §6.6 debug interception: flags this thread for the lock/handle hooks.
    ProfilerSignalGuardScope signalGuard;

    RawSample *slot = ctx->queue->TryStartEnqueue();
    if (slot == nullptr) {
        ctx->dropQueueFull.fetch_add(1, std::memory_order_relaxed);
    } else {
        slot->timestamp = ctx->requestTimestamp.load(std::memory_order_acquire);
        slot->timestampIsApproximate = true;
        slot->metadataSequence = ctx->metadataSequence->load(std::memory_order_acquire);
        slot->threadOrTaskId = ctx->threadOrTaskId;
        slot->profilerGeneration = ctx->generation.load(std::memory_order_acquire);
        slot->frameCount = 0;
        slot->status = SampleStatus::OK;
        ctx->profiler->CollectSampleInto(*slot, ctx, context);
        ctx->queue->FinishEnqueue();
        ctx->samplesRecorded.fetch_add(1, std::memory_order_relaxed);
    }

    ctx->handlerRunning.store(false, std::memory_order_release);
    ctx->activeHandlerCount.fetch_sub(1, std::memory_order_release);
}
#endif // !defined(ECMASCRIPT_SUPPORT_CPUPROFILER_LEGACY)

void CpuProfiler::CollectSampleInto(RawSample &sample, CpuProfilerSignalContext *ctx, void *context)
{
    JSThread *thread = ctx->jsThread;

    if (ctx->profiler->GetBuildNapiStack() || thread->GetGcState()) {
        sample.status = SampleStatus::GC_SKIPPED;
        ctx->dropGcSkipped.fetch_add(1, std::memory_order_relaxed);
        return;
    }

#ifdef PANDA_JS_ETS_HYBRID_MODE
    // KNOWN SIGNAL-SAFETY DEBT (temporary): the UnionStackIsEmpty check and the
    // legacy hybrid processing call into the other VM and may allocate, lock or
    // log in the handler, risking deadlock/crash (issue #13618 class). Kept for
    // hybrid coverage; replace with the Phase 2 signal-safe snapshot.
    if (ctx->crossVmOperatorPresent) {
        bool isEmpty = true;
        ctx->crossVMOperator->UnionStackIsEmpty(&isEmpty);
        if (!isEmpty) {
            ProcessHybridStack(ctx->jsThread);
            // Frames went to samplesQueue_; mark the raw slot to avoid a spurious idle sample.
            sample.status = SampleStatus::UNSUPPORTED_HYBRID_FRAME;
            return;
        }
    }
#endif

    uint64_t pc = 0;
    if (thread->IsAsmInterpreter()) {
        // Try-lock only: on contention the sample is dropped instead of waiting,
        // which is what keeps this path free of the historical reentrancy deadlock.
        // The manager pointer comes from the context cache: routing through
        // AnFileDataManager::GetInstance() would add a magic-static guard here.
        if (!ctx->anFileDataManager->SafeTryReadLock()) {
            sample.status = SampleStatus::STACK_NOT_ITERABLE;
            ctx->dropStackNotIterable.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        pc = GetPcFromContext(context);
    }
    if (thread->IsAsmInterpreter() && IsAddrAtStubOrAot(ctx->anFileDataManager, pc) &&
        !IsEntryFrameHeaderOrTail(thread, pc)) {
        if (IfNeedSkipBarrierStubHeaderOrTail(thread, pc)) {
            return;
        }
        CollectUContextFrames(sample, ctx, context);
    } else if (thread->IsAsmInterpreter()) {
        if (thread->GetLastLeaveFrame() != nullptr) {
            JSTaggedType *leaveFrame = const_cast<JSTaggedType *>(thread->GetLastLeaveFrame());
            SignalSafeProfilerFrameIterator it(leaveFrame, thread);
            CollectRawFrames(sample, ctx, it);
        }
    } else {
        if (thread->GetCurrentFrame() != nullptr) {
            SignalSafeProfilerFrameIterator it(const_cast<JSTaggedType *>(thread->GetCurrentFrame()), thread);
            CollectRawFrames(sample, ctx, it);
        }
    }
}

void CpuProfiler::CollectUContextFrames(RawSample &sample, CpuProfilerSignalContext *ctx, void *context)
{
    [[maybe_unused]] ucontext_t *ucontext = reinterpret_cast<ucontext_t*>(context);
    [[maybe_unused]] mcontext_t &mcontext = ucontext->uc_mcontext;
    [[maybe_unused]] void *fp = nullptr;
    [[maybe_unused]] void *sp = nullptr;
#if defined(PANDA_TARGET_AMD64)
    fp = reinterpret_cast<void*>(mcontext.gregs[REG_RBP]);
    sp = reinterpret_cast<void*>(mcontext.gregs[REG_RSP]);
#elif defined(PANDA_TARGET_ARM64)
    fp = reinterpret_cast<void*>(mcontext.regs[29]); // FP is an alias for x29.
    sp = reinterpret_cast<void*>(mcontext.sp);
#else
    sample.status = SampleStatus::STACK_NOT_ITERABLE;
    ctx->dropStackNotIterable.fetch_add(1, std::memory_order_relaxed);
    return;
#endif
    if (sp > fp) {
        sample.status = SampleStatus::STACK_NOT_ITERABLE;
        ctx->dropStackNotIterable.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    SignalSafeProfilerFrameIterator it(reinterpret_cast<JSTaggedType *>(fp), ctx->jsThread);
    CollectRawFrames(sample, ctx, it);
}

// Signal-context frame walk: the dedicated iterator validates every sp (bounds,
// alignment, monotonicity) and stops on unknown frame types; reads are plain loads
// only. Writes only into the preallocated slot.
void CpuProfiler::CollectRawFrames(RawSample &sample, CpuProfilerSignalContext *ctx,
                                   SignalSafeProfilerFrameIterator &it)
{
    bool topFrame = true;
    uint16_t count = 0;
    for (; !it.Done() && count < RAW_MAX_STACK_SIZE; it.Advance()) {
        auto method = it.GetMethod();
        if (method == nullptr || !JSTaggedValue(method).IsMethod()) {
            continue;
        }
        bool isNative = method->IsNativeWithCallField();
        RunningState state = GetRunningStateRaw(it, vm_, isNative, topFrame, enableVMTag_);
        topFrame = false;
        void *methodIdentifier = GetMethodIdentifierRaw(method, it, vm_);
        if (methodIdentifier == nullptr) {
            continue;
        }
        RawFrame &frame = sample.frames[count];
        frame.value = reinterpret_cast<uintptr_t>(methodIdentifier);
        frame.kind = ClassifyRawFrameKind(it, isNative);
        frame.state = ToRawRunningState(state);
        frame.deoptType = static_cast<uint8_t>(method->GetDeoptType());
        frame.reserved = 0;
        if (isNative) {
            frame.jsPandaFile = 0;
            frame.bytecodeOffset = 0;
        } else {
            // No-barrier read: the barriered GetJSPandaFile(thread) walks the
            // constant-pool tagged field, whose read barrier can synchronously
            // enter CC-GC evacuation from the handler (gate bridge D1).
            frame.jsPandaFile = reinterpret_cast<uintptr_t>(method->GetJSPandaFileNoBarrier());
            frame.bytecodeOffset = it.GetBytecodeOffset();
        }
        count++;
    }
    sample.frameCount = count;
    if (!it.IsValid()) {
        sample.status = SampleStatus::INVALID_FRAME;
        ctx->dropInvalidFrame.fetch_add(1, std::memory_order_relaxed);
    }
}

void CpuProfiler::ScheduleSample(pthread_t jsThreadId)
{
    if (signalCtx_.handlerRunning.load(std::memory_order_acquire)) {
        signalCtx_.handlerOverrunTicks.fetch_add(1, std::memory_order_relaxed);
    }
    signalCtx_.requestTimestamp.store(SamplingProcessor::GetMicrosecondsTimeStamp(),
                                      std::memory_order_release);
    signalCtx_.shouldRecordSample.store(true, std::memory_order_release);
    if (pthread_kill(jsThreadId, SIGPROF) != 0) {
        // The signal was not delivered, so the requesting thread may retract the bit.
        signalCtx_.shouldRecordSample.store(false, std::memory_order_release);
    }
}

bool CpuProfiler::ConsumeRawSamples()
{
    // Fetch the metadata event log once per drain; events are applied per sample
    // in sequence order before symbolization (plan §7.2), so each sample is judged
    // against the code view that existed at its capture time.
    CVector<ProfilerMetadataRegistry::Event> events;
    ProfilerMetadataRegistry::Instance().GetEvents(symbolizer_.MetadataCursor(), events);
    size_t eventIdx = 0;
    uint64_t applied = 0;
    bool consumed = false;
    RawSample sample;
    while (rawQueue_.TryPop(sample)) {
        while (eventIdx < events.size() && events[eventIdx].sequence <= sample.metadataSequence) {
            symbolizer_.ApplyEvent(events[eventIdx]);
            eventIdx++;
            applied++;
        }
        ProcessRawSample(sample);
        consumed = true;
    }
    symbolizer_.AdvanceCursor(applied);
    return consumed;
}

// Processor-thread side: orchestrates symbolization of one raw sample; all
// allocation and JSPandaFile/DebugInfo lookups live in the symbolizer.
void CpuProfiler::ProcessRawSample(const RawSample &sample)
{
    // Drop samples left over from a previous session (late-delivered SIGPROF that
    // survived the start-time queue drain; Oracle review F5).
    if (sample.profilerGeneration != signalCtx_.generation.load(std::memory_order_acquire)) {
        return;
    }
    // Drop handler-marked samples (raw_sample.h contract): without this they
    // become idle (program-node) samples or leak a partial INVALID_FRAME walk
    // into the tree. Capture-side drop* counters were already incremented by
    // the handler, so no counter is touched here.
    if (sample.status != SampleStatus::OK) {
        return;
    }
    // Pre-validate every frame's owning file against the sample's metadata view:
    // symbolizing a frame dereferences its (and its caller's) MethodLiteral data,
    // so any dead file must abort the whole sample before the first dereference.
    for (uint16_t i = 0; i < sample.frameCount; i++) {
        if (sample.frames[i].jsPandaFile != 0 &&
            !symbolizer_.IsJSPandaFileLive(sample.frames[i].jsPandaFile)) {
            signalCtx_.dropMetadataStale.fetch_add(1, std::memory_order_relaxed);
            return;
        }
    }
    auto frame = std::make_unique<FrameStackAndInfo>();
    frame->timeStamp = sample.timestamp;
    const CMap<struct MethodKey, struct FrameInfo> &stackInfo = generator_->GetStackInfo();
    for (uint16_t i = 0; i < sample.frameCount; i++) {
        const RawFrame &rawFrame = sample.frames[i];
        struct MethodKey methodKey;
        methodKey.methodIdentifier = reinterpret_cast<void *>(rawFrame.value);
        methodKey.state = ToRunningState(rawFrame.state);
        methodKey.deoptType = static_cast<kungfu::DeoptType>(rawFrame.deoptType);
        methodKey.lineNumber = 0;
        if (rawFrame.kind == RawFrameKind::BUILTIN) {
            methodKey.lineNumber = symbolizer_.ResolveNativeCallLine(sample, i);
        }
        if (stackInfo.count(methodKey) == 0) {
            struct FrameInfoTemp codeEntry;
            if (UNLIKELY(!symbolizer_.SymbolizeFrame(sample, i, methodKey, codeEntry))) {
                continue;
            }
            frame->frameInfoTemps[frame->frameInfoTempsLength++] = codeEntry;
        }
        frame->frameStack[frame->frameStackLength++] = methodKey;
    }
    generator_->AddSample(*frame);
}

bool CpuProfiler::InHeaderOrTail(uint64_t pc, uint64_t entryBegin, uint64_t entryDuration, uint64_t headerSize,
                                 uint64_t tailSize) const
{
    uintptr_t entryEnd = entryBegin + entryDuration;
    if (pc >= entryBegin && pc <= (entryBegin + headerSize)) {
        return true;
    }
    if (pc <= entryEnd && pc >= (entryEnd - tailSize)) {
        return true;
    }
    return false;
}

bool CpuProfiler::IfNeedSkipBarrierStubHeaderOrTail(JSThread *thread, uint64_t pc) const
{
    uint64_t headerSize = 20;
    uintptr_t entryBegin = thread->GetFastStubEntry(kungfu::CommonStubCSigns::ID::GetValueWithBarrier);
    bool needSkip = InHeaderOrTail(pc, entryBegin, 0, headerSize, 0);
    return needSkip;
}

bool CpuProfiler::IsEntryFrameHeaderOrTail(JSThread *thread, uint64_t pc) const
{
    uint64_t headerSize = 0;
    uint64_t tailSize = 0;
    uint64_t entryDuration = 0;
    Assembler::GetFrameCompletionPos(headerSize, tailSize, entryDuration);
    uintptr_t entryBegin = thread->GetRTInterface(kungfu::RuntimeStubCSigns::ID_AsmInterpreterEntry);
    bool inAsmInterpreterEntry = InHeaderOrTail(pc, entryBegin, entryDuration, headerSize, tailSize);
    entryBegin = thread->GetRTInterface(kungfu::RuntimeStubCSigns::ID_GeneratorReEnterAsmInterp);
    bool inGeneratorReEnterAsmInterp = InHeaderOrTail(pc, entryBegin, entryDuration, headerSize, tailSize);
    return (inAsmInterpreterEntry || inGeneratorReEnterAsmInterp);
}

uint64_t CpuProfiler::GetPcFromContext(void *context)
{
    [[maybe_unused]] ucontext_t *ucontext = reinterpret_cast<ucontext_t*>(context);
    [[maybe_unused]] mcontext_t &mcontext = ucontext->uc_mcontext;
    uint64_t pc = 0;
#if defined(PANDA_TARGET_AMD64)
    pc = static_cast<uint64_t>(mcontext.gregs[REG_RIP]);
#elif defined(PANDA_TARGET_ARM64)
    pc = static_cast<uint64_t>(mcontext.pc);
#else
    LOG_FULL(FATAL) << "AsmInterpreter does not currently support other platforms, please run on x64 and arm64";
    pc = 0;
#endif
    return pc;
}

bool CpuProfiler::IsAddrAtStubOrAot(AnFileDataManager *manager, uint64_t pc) const
{
    // Signal-context contract: never block. On write-lock contention this returns
    // false and the caller falls back to the leave-frame walk (coarser but safe).
    return manager->SafeTryInsideStubOrAOT(pc);
}

#if defined(ECMASCRIPT_SUPPORT_CPUPROFILER_LEGACY)
bool CpuProfiler::IsAddrAtStubOrAot(uint64_t pc) const
{
    // Legacy form used by GetStackSignalHandler (blocking range query).
    AOTFileManager *loader = vm_->GetAOTFileManager();
    return loader->InsideStub(pc) || loader->InsideAOT(pc);
}
#endif

bool CpuProfiler::CheckFileName(const std::string &fileName, std::string &absoluteFilePath) const
{
    if (fileName.empty()) {
        LOG_ECMA(ERROR) << "CpuProfiler::CheckFileName, fileName is empty";
        return false;
    }

    if (fileName.size() > PATH_MAX) {
        LOG_ECMA(ERROR) << "CpuProfiler::CheckFileName, fileName exceed PATH_MAX";
        return false;
    }

    CVector<char> resolvedPath(PATH_MAX);
    auto result = realpath(fileName.c_str(), resolvedPath.data());
    if (result == nullptr) {
        LOG_ECMA(ERROR) << "CpuProfiler::CheckFileName, realpath fail, errno = " << errno;
        return false;
    }
    std::ofstream file(resolvedPath.data());
    if (!file.good()) {
        LOG_ECMA(ERROR) << "CpuProfiler::CheckFileName, file is not good, errno = " << errno;
        return false;
    }
    file.close();
    absoluteFilePath = resolvedPath.data();
    return true;
}

void CpuProfiler::SetBuildNapiStack(bool flag)
{
    isBuildNapiStack_.store(flag);
}

bool CpuProfiler::GetBuildNapiStack()
{
    return isBuildNapiStack_.load();
}

bool CpuProfiler::GetOutToFile()
{
    return outToFile_;
}

EcmaVM* CpuProfiler::GetVmbyTid(pthread_t tid)
{
    LockHolder lock(synchronizationMutex_);
    return const_cast<EcmaVM *>(profilerMap_[tid].vm_);
}

uint32_t GetProfilerSignalHandlerViolationsForTest()
{
    return g_profilerSignalHandlerViolations.load(std::memory_order_relaxed);
}

uint32_t GetProfilerSignalTableMisses()
{
    return g_profilerSignalTableMisses.load(std::memory_order_relaxed);
}

void SetProfilerSignalGuardForTest(bool active)
{
    if (active) {
        EnterProfilerSignalGuard();
    } else {
        ExitProfilerSignalGuard();
    }
}
} // namespace panda::ecmascript
