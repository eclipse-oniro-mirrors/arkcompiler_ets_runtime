/*
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
#include "ecmascript/dfx/cpu_profiler/sampling_processor.h"

#include <memory>

#include "ecmascript/dfx/cpu_profiler/samples_record.h"
#if defined(ENABLE_FFRT_INTERFACES)
#include "c/executor_task.h"
#endif

namespace panda::ecmascript {
const int USEC_PER_SEC = 1000 * 1000;
const int NSEC_PER_USEC = 1000;
SamplingProcessor::~SamplingProcessor() {}

#if defined(ENABLE_FFRT_INTERFACES)
namespace {
// One FFRT rebind check: updates the target thread and returns false when the
// task is currently hung (tid == 0), meaning the tick must be skipped.
bool RebindFfrtThread(const RunParams *runParams, pthread_t &jsThreadId)
{
    // When the ffrt is disabled for js thread, including main thread and worker thread,
    // then the taskHandle is a nullptr
    if (runParams->taskHandle_ == nullptr) {
        return true;
    }
    // When the ffrt task is not running on any threads (hang),
    // the tid returned by ffrt_task_get_tid will be zero
    pthread_t tid = ffrt_task_get_tid(runParams->taskHandle_);
    if (tid != 0 && jsThreadId != tid) {
        runParams->profiler_->RebindSignalContext(static_cast<uint64_t>(tid));
        jsThreadId = tid;
    }
    return tid != 0;
}
} // namespace
#endif // defined(ENABLE_FFRT_INTERFACES)

namespace {
// Sleeps out the remainder of the sampling interval and returns the new tick
// time (both wall-clock stamps in microseconds).
uint64_t SleepToNextTick(uint64_t startTime, uint64_t endTime, uint32_t interval)
{
    int64_t ts = static_cast<int64_t>(interval) - static_cast<int64_t>(startTime - endTime);
    if (ts > 0) {
        usleep(ts);
        return SamplingProcessor::GetMicrosecondsTimeStamp();
    }
    return startTime;
}

// True when the profile tree hit MAX_NODE_COUNT and the sampling loop must stop.
bool ProfileTreeExceedsLimit(SamplesRecord *generator)
{
    return generator->GetMethodNodeCount() + generator->GetframeStackLength() >= MAX_NODE_COUNT;
}

#if !defined(ECMASCRIPT_SUPPORT_CPUPROFILER_LEGACY)
// Drains the raw queue and the legacy NAPI queue, folds samples into the
// profile, and maintains the idle-fallback counter (two consecutive empty
// ticks before an idle sample, keeping timestamps monotonic; Oracle F2).
int ConsumeAndFold(SamplesRecord *generator, CpuProfiler *profiler, int emptyTicks)
{
    bool consumedRaw = profiler->ConsumeRawSamples();
    bool consumedLegacy = !generator->samplesQueue_->IsEmpty();
    if (consumedLegacy) {
        // Legacy queue carries NAPI active-sampling frames posted by the JS
        // thread itself; drain it every tick like the old loop did.
        auto frame = std::make_unique<FrameStackAndInfo>();
        while (generator->samplesQueue_->PopFrame(*frame)) {
            generator->AddSample(*frame);
        }
    }
    if (consumedRaw || consumedLegacy) {
        return 0;
    }
    if (++emptyTicks >= 2) { // 2: emit idle only after two empty ticks
        generator->AddEmptyStackSample(SamplingProcessor::GetMicrosecondsTimeStamp());
    }
    return emptyTicks;
}

// One lock-free tick: fire-and-forget signal, then fold whatever arrived.
// Returns false when the profile tree hit MAX_NODE_COUNT (loop must stop).
bool RunOneLockFreeTick(CpuProfiler *profiler, SamplesRecord *generator, pthread_t jsThreadId,
                        int &emptyTicks)
{
    profiler->ScheduleSample(jsThreadId);
    if (ProfileTreeExceedsLimit(generator)) {
        LOG_ECMA(ERROR) << "SamplingProcessor::Run, exceed MAX_NODE_COUNT";
        return false;
    }
    emptyTicks = ConsumeAndFold(generator, profiler, emptyTicks);
    return true;
}
#endif // !defined(ECMASCRIPT_SUPPORT_CPUPROFILER_LEGACY)

#if defined(ECMASCRIPT_SUPPORT_CPUPROFILER_LEGACY)
// One legacy tick: per-sample handshake — block until the handler finished
// symbolizing, then fold synchronously. Returns false when the tick failed
// fatally (loop must exit) or the profile tree hit MAX_NODE_COUNT.
bool RunOneLegacyTick(SamplesRecord *generator, pthread_t jsThreadId)
{
    if (pthread_kill(jsThreadId, SIGPROF) != 0) {
        LOG(ERROR, RUNTIME) << "pthread_kill signal failed";
        return false;
    }
    if (generator->SemWait(0) != 0) {
        LOG_ECMA(ERROR) << "sem_[0] wait failed";
        return false;
    }
    if (ProfileTreeExceedsLimit(generator)) {
        LOG_ECMA(ERROR) << "SamplingProcessor::Run, exceed MAX_NODE_COUNT";
        return false;
    }
    SamplingProcessor::AddSample(generator);
    return true;
}
#endif // ECMASCRIPT_SUPPORT_CPUPROFILER_LEGACY

} // namespace

void *SamplingProcessor::Run(void *arg)
{
    LOG_ECMA(INFO) << "SamplingProcessor::Run start";
    std::unique_ptr<RunParams> runParams(reinterpret_cast<RunParams*>(arg));
    SamplesRecord *generator = runParams->generator_;
    uint32_t interval = runParams->interval_;
    pthread_t jsThreadId = runParams->tid_;
    CpuProfiler *profiler = runParams->profiler_;
    pthread_t samplingThreadId = pthread_self();
    pthread_setname_np(samplingThreadId, "SamplingThread");
    uint64_t startTime = generator->GetThreadStartTime();
    uint64_t endTime = startTime;
#if !defined(ECMASCRIPT_SUPPORT_CPUPROFILER_LEGACY)
    int emptyTicks = 0;
#endif
    generator->AddStartTraceEvent();
    while (generator->GetIsStart()) {
        endTime = SleepToNextTick(startTime, endTime, interval);
        startTime = endTime;
#if defined(ENABLE_FFRT_INTERFACES)
        if (!RebindFfrtThread(runParams.get(), jsThreadId)) {
#if !defined(ECMASCRIPT_SUPPORT_CPUPROFILER_LEGACY)
            // Task hung: fold whatever arrived under the normal empty-tick idle
            // policy instead of emitting an idle sample every tick.
            emptyTicks = ConsumeAndFold(generator, profiler, emptyTicks);
#else
            // The samplesQueue_ may not be empty due to the call of PostNapiFrame
            SamplingProcessor::AddSample(generator);
#endif
            generator->AddTraceEvent(false);
            continue;
        }
#endif // defined(ENABLE_FFRT_INTERFACES)
#if defined(ECMASCRIPT_SUPPORT_CPUPROFILER_LEGACY)
        bool tickOk = RunOneLegacyTick(generator, jsThreadId);
#else
        bool tickOk = RunOneLockFreeTick(profiler, generator, jsThreadId, emptyTicks);
#endif
        if (!tickOk) {
            break;
        }
        generator->AddTraceEvent(false);
    }
#if !defined(ECMASCRIPT_SUPPORT_CPUPROFILER_LEGACY)
    profiler->ConsumeRawSamples();
#endif
    generator->SetThreadStopTime();
    generator->AddTraceEvent(true);
    return PostSemAndLogEnd(generator, samplingThreadId);
}

void SamplingProcessor::AddSample(SamplesRecord *generator)
{
    if (generator->samplesQueue_->IsEmpty()) {
        uint64_t sampleTimeStamp = SamplingProcessor::GetMicrosecondsTimeStamp();
        generator->AddEmptyStackSample(sampleTimeStamp);
    } else {
        auto frame = std::make_unique<FrameStackAndInfo>();
        while (generator->samplesQueue_->PopFrame(*frame)) {
            generator->AddSample(*frame);
        }
    }
}

void *SamplingProcessor::PostSemAndLogEnd(SamplesRecord *generator, pthread_t tid)
{
    pthread_setname_np(tid, "OS_GC_Thread");
    if (generator->SemPost() != 0) {
        LOG_ECMA(ERROR) << "sem post failed, errno = " << errno;
        return nullptr;
    }
    LOG_ECMA(INFO) << "SamplingProcessor::Run end";
    return nullptr;
}

uint64_t SamplingProcessor::GetMicrosecondsTimeStamp()
{
    struct timespec time;
    clock_gettime(CLOCK_MONOTONIC, &time);
    return time.tv_sec * USEC_PER_SEC + time.tv_nsec / NSEC_PER_USEC;
}
} // namespace panda::ecmascript
