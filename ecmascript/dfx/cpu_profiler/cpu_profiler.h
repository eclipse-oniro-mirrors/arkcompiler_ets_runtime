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

#ifndef ECMASCRIPT_DFX_CPU_PROFILER_CPU_PROFILER_H
#define ECMASCRIPT_DFX_CPU_PROFILER_CPU_PROFILER_H

#include <csignal>

#include "ecmascript/dfx/cpu_profiler/cpu_profiler_symbolizer.h"
#include "ecmascript/dfx/cpu_profiler/profiler_metadata_registry.h"
#include "ecmascript/dfx/cpu_profiler/profiler_signal_context.h"
#include "ecmascript/dfx/cpu_profiler/raw_sample.h"
#include "ecmascript/dfx/cpu_profiler/samples_record.h"
#include "ecmascript/dfx/cpu_profiler/sampling_processor.h"
#include "ecmascript/dfx/cpu_profiler/signal_safe_frame_iterator.h"
#include "ecmascript/debugger/js_debugger_manager.h"
#include "ecmascript/interpreter/frame_handler.h"
#include "ecmascript/js_thread.h"

#include "ecmascript/platform/mutex.h"

namespace panda::ecmascript {
class AnFileDataManager;
const int THRESHOLD_GROWTH_FACTORY = 2; // 2:TimeDelta Threshold Growth Factory
const int THRESHOLD_FIXED_INCREMENT = 2000; // 2000:TimeDelta Threshold Fixed Increment
using JSTaggedType = uint64_t;
using JsDebuggerManager = tooling::JsDebuggerManager;
class SamplesRecord;

struct TaskInfo {
    const EcmaVM *vm_ { nullptr };
    void *taskHandle_ { nullptr };
};

class GcStateScope {
public:
    inline explicit GcStateScope(JSThread *thread)
    {
        thread_ = thread;
        thread_->SetGcState(true);
    }

    inline ~GcStateScope()
    {
        thread_->SetGcState(false);
    }
private:
    JSThread *thread_ = nullptr;
};

class SignalStateScope {
public:
    inline explicit SignalStateScope(JsDebuggerManager *jsDebuggerManager)
    {
        jsDebuggerManager_ = jsDebuggerManager;
        jsDebuggerManager_->SetSignalState(true);
    }

    inline ~SignalStateScope()
    {
        jsDebuggerManager_->SetSignalState(false);
    }
private:
    JsDebuggerManager *jsDebuggerManager_ = nullptr;
};

class RuntimeStateScope {
public:
    inline explicit RuntimeStateScope(JSThread *thread)
    {
        thread_ = thread;
        oldState_ = thread_->GetRuntimeState();
        thread_->SetRuntimeState(true);
    }

    inline ~RuntimeStateScope()
    {
        thread_->SetRuntimeState(oldState_);
    }
private:
    bool oldState_ = false;
    JSThread *thread_ = nullptr;
};

class CpuProfiler {
public:
    static const int CPUPROFILER_DEFAULT_INTERVAL = 500; // 500:Default Sampling interval 500 microseconds
    static const int INTERVAL_OF_ACTIVE_SAMPLING = 300; // 300:interval of active sampling
    static const int INTERVAL_OF_INNER_START = 100; // 100:interval of inner start(stake in runtime) sampling

    bool InHeaderOrTail(uint64_t pc, uint64_t entryBegin, uint64_t entryDuration, uint64_t headerSize,
                        uint64_t tailSize) const;
    bool IfNeedSkipBarrierStubHeaderOrTail(JSThread *thread, uint64_t pc) const;
    bool IsEntryFrameHeaderOrTail(JSThread *thread, uint64_t pc) const;
    bool GetStackBeforeCallNapi(JSThread *thread);
    void GetStackAfterCallNapi(JSThread *thread);
    bool GetStackCallNapi(JSThread *thread, bool beforeCallNapi);
#if defined(ECMASCRIPT_SUPPORT_CPUPROFILER)
    // siginfo_t may be undefined on some platforms
#if defined(ECMASCRIPT_SUPPORT_CPUPROFILER_LEGACY)
    // Legacy handler (default name retained): symbolizes inside the signal
    // context and ACKs the sampling thread through sem_[0]. NOT
    // async-signal-safe (issue #13618); historical behavior baseline.
    // Defining ECMASCRIPT_SUPPORT_CPUPROFILER_LEGACY selects this scheme for
    // the whole build (handler, tick loop, stop path); it is off by default.
    static void GetStackSignalHandler(int signal, siginfo_t *siginfo, void *context);
#else
    // Lock-free two-phase handler (default scheme): raw capture only,
    // deferred symbolization — the only scheme compiled unless
    // ECMASCRIPT_SUPPORT_CPUPROFILER_LEGACY is defined.
    static void GetStackSignalHandlerLockFree(int signal, siginfo_t *siginfo, void *context);
#endif
#endif

#if defined(PANDA_JS_ETS_HYBRID_MODE)
    // Hybrid stack processing; the lock-free handler also routes hybrid samples
    // here temporarily (see CollectSampleInto).
    void ProcessHybridStack(JSThread *thread);
    void ProcessStaticFrame(const void *frame);
    void ProcessDynamicFrame(const void *frame, JSThread *thread);
#endif

    bool StartCpuProfilerForInfo();
    bool StopCpuProfilerForInfo(std::unique_ptr<struct ProfileInfo> &profileInfo);
    bool StartCpuProfilerForFile(const std::string &fileName);
    bool StopCpuProfilerForFile();
    void SetCpuSamplingInterval(int interval);
    void SetBuildNapiStack(bool flag);
    bool GetBuildNapiStack();
    bool GetOutToFile();
    explicit CpuProfiler(const EcmaVM *vm, const int interval = CPUPROFILER_DEFAULT_INTERVAL);
    virtual ~CpuProfiler();

    // Read by the tracing integration (both modes) and the legacy handler.
    static CMap<pthread_t, struct TaskInfo> profilerMap_;
    static EcmaVM *GetVmbyTid(pthread_t tid);

    // Processor-thread side: send one sampling request (pending bit + timestamp) and
    // consume whatever raw samples have arrived. Never blocks on the handler.
    void ScheduleSample(pthread_t jsThreadId);
    bool ConsumeRawSamples();
    // Sampling-thread side: migrate the signal-context tid binding when the FFRT
    // task moves to another worker (called via SamplingProcessor::Run).
    void RebindSignalContext(uint64_t newTid);
private:
    static Mutex synchronizationMutex_;

#if defined(ECMASCRIPT_SUPPORT_CPUPROFILER_LEGACY) || defined(PANDA_JS_ETS_HYBRID_MODE)
    // Legacy in-handler symbolization walk: used by GetStackSignalHandler, and
    // by the hybrid routing's ProcessDynamicFrame (see CollectSampleInto).
    void GetStack(FrameIterator &it);
#endif
    // Signal-context side: register/PC validation and plain-load frame walk filling a
    // preallocated raw sample slot. Must stay free of locks, allocation and logging.
    void CollectSampleInto(RawSample &sample, CpuProfilerSignalContext *ctx, void *context);
    void CollectUContextFrames(RawSample &sample, CpuProfilerSignalContext *ctx, void *context);
    void CollectRawFrames(RawSample &sample, CpuProfilerSignalContext *ctx,
                          SignalSafeProfilerFrameIterator &it);
    // Processor-thread side: orchestrates symbolization of one raw sample; all
    // allocation and metadata lookups live in CpuProfilerSymbolizer/SamplesRecord.
    void ProcessRawSample(const RawSample &sample);
    void ResetSamplingStats();
    void LogSamplingStats();
    void WaitHandlersDrained();
    void WaitHandlersDrainedUnbounded();
    static uint64_t GetPcFromContext(void *context);
    bool IsAddrAtStubOrAot(AnFileDataManager *manager, uint64_t pc) const;
#if defined(ECMASCRIPT_SUPPORT_CPUPROFILER_LEGACY)
    // Legacy blocking form used by GetStackSignalHandler only.
    bool IsAddrAtStubOrAot(uint64_t pc) const;
#endif
    bool CheckFileName(const std::string &fileName, std::string &absoluteFilePath) const;
    bool RegisterGetStackSignal();
    bool PrepareProfilerSession(void **taskHandle);
    void SetProfilerSignalState(ProfilerSignalState state);
    bool StartProfilerCommon();
    bool StopProfilerCommon();
    void AbortProfilerStart();

    bool isProfiling_ = false;
    bool outToFile_ = false;
    std::string fileName_ = "";
    SamplesRecord *generator_ = nullptr;
    pthread_t tid_ = 0;
    const EcmaVM *vm_ = nullptr;
    uint32_t interval_ = 0;
    uint64_t beforeCallNapiTimeStamp_ = 0;
    std::atomic_bool isBuildNapiStack_ {false};
    bool enableVMTag_ {false};
    // Signal-safe sampling data path (plan §4/§5): the queue is written by the
    // SIGPROF handler only and consumed by the sampling processor thread only.
    RawSamplesQueue rawQueue_;
    CpuProfilerSignalContext signalCtx_;
    // Table key of the currently published signalCtx_ binding; Start and the
    // sampling-thread rebind maintain it under synchronizationMutex_.
    uint64_t signalCtxKey_ {0};
    // Processor-thread-only symbolization and metadata view (plan §7.1/§7.2).
    CpuProfilerSymbolizer symbolizer_;

    friend class CpuProfilerFriendTest;
};

class CallNapiScope {
public:
    inline explicit CallNapiScope(CpuProfiler *profiler)
    {
        profiler_ = profiler;
        profiler_->SetBuildNapiStack(true);
    }

    inline ~CallNapiScope()
    {
        profiler_->SetBuildNapiStack(false);
    }
private:
    CpuProfiler *profiler_ {nullptr};
};
} // namespace panda::ecmascript
#endif // ECMASCRIPT_DFX_CPU_PROFILER_CPU_PROFILER_H
