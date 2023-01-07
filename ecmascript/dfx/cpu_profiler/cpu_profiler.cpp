/*
 * Copyright (c) 2021 Huawei Device Co., Ltd.
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

#include <atomic>
#include <chrono>
#include <climits>
#include <fstream>

#include "ecmascript/compiler/assembler/assembler.h"
#include "ecmascript/dfx/cpu_profiler/sampling_processor.h"
#include "ecmascript/frames.h"
#include "ecmascript/aot_file_manager.h"
#include "ecmascript/jspandafile/js_pandafile_manager.h"
#include "ecmascript/taskpool/taskpool.h"

namespace panda::ecmascript {
os::memory::Mutex CpuProfiler::synchronizationMutex_;
CMap<pthread_t, const EcmaVM *> CpuProfiler::profilerMap_ = CMap<pthread_t, const EcmaVM *>();
CpuProfiler::CpuProfiler(const EcmaVM *vm, const int interval) : vm_(vm), interval_(interval)
{
    generator_ = new SamplesRecord();
    if (generator_->SemInit(0, 0, 0) != 0) {
        LOG_ECMA(ERROR) << "sem_[0] init failed";
    }
    if (generator_->SemInit(1, 0, 0) != 0) {
        LOG_ECMA(ERROR) << "sem_[1] init failed";
    }
    if (generator_->SemInit(2, 0, 0) != 0) { // 2: signal 2
        LOG_ECMA(ERROR) << "sem_[2] init failed";
    }
}

void CpuProfiler::StartCpuProfilerForInfo()
{
    LOG_ECMA(INFO) << "StartCpuProfilerForInfo, Sampling interval is: " << interval_
                   << " ,napi active record is: " << callNapiGetStack_;
    if (isProfiling_) {
        LOG_ECMA(ERROR) << "Can not StartCpuProfilerForInfo when CpuProfiler is Profiling";
        return;
    }
    isProfiling_ = true;
    struct sigaction sa;
    sa.sa_sigaction = &GetStackSignalHandler;
    if (sigemptyset(&sa.sa_mask) != 0) {
        LOG_ECMA(ERROR) << "Parameter set signal set initialization and emptying failed";
        isProfiling_ = false;
        return;
    }
    sa.sa_flags = SA_RESTART | SA_SIGINFO;
    if (sigaction(SIGPROF, &sa, nullptr) != 0) {
        LOG_ECMA(ERROR) << "sigaction failed to set signal";
        isProfiling_ = false;
        return;
    }
    tid_ = static_cast<pthread_t>(syscall(SYS_gettid));
    {
        os::memory::LockHolder lock(synchronizationMutex_);
        profilerMap_[tid_] = vm_;
    }
    vm_->GetJSThread()->SetCallNapiGetStack(callNapiGetStack_);
    generator_->SetIsStart(true);
    Taskpool::GetCurrentTaskpool()->PostTask(
        std::make_unique<SamplingProcessor>(vm_->GetJSThread()->GetThreadId(), generator_, interval_));
}

void CpuProfiler::StartCpuProfilerForFile(const std::string &fileName)
{
    LOG_ECMA(INFO) << "StartCpuProfilerForFile, Sampling interval is: " << interval_
                   << " ,napi active record is: " << callNapiGetStack_;
    if (isProfiling_) {
        LOG_ECMA(ERROR) << "Can not StartCpuProfilerForFile when CpuProfiler is Profiling";
        return;
    }
    isProfiling_ = true;
    std::string absoluteFilePath("");
    if (!CheckFileName(fileName, absoluteFilePath)) {
        LOG_ECMA(ERROR) << "The filename contains illegal characters";
        isProfiling_ = false;
        return;
    }
    fileName_ = absoluteFilePath;
    if (fileName_.empty()) {
        LOG_ECMA(ERROR) << "CpuProfiler filename is empty!";
        isProfiling_ = false;
        return;
    }
    generator_->SetFileName(fileName_);
    generator_->fileHandle_.open(fileName_.c_str());
    if (generator_->fileHandle_.fail()) {
        LOG_ECMA(ERROR) << "File open failed";
        isProfiling_ = false;
        return;
    }
    struct sigaction sa;
    sa.sa_sigaction = &GetStackSignalHandler;
    if (sigemptyset(&sa.sa_mask) != 0) {
        LOG_ECMA(ERROR) << "Parameter set signal set initialization and emptying failed";
        isProfiling_ = false;
        return;
    }
    sa.sa_flags = SA_RESTART | SA_SIGINFO;
    if (sigaction(SIGPROF, &sa, nullptr) != 0) {
        LOG_ECMA(ERROR) << "sigaction failed to set signal";
        isProfiling_ = false;
        return;
    }
    tid_ = static_cast<pthread_t>(syscall(SYS_gettid));
    {
        os::memory::LockHolder lock(synchronizationMutex_);
        profilerMap_[tid_] = vm_;
    }
    outToFile_ = true;
    vm_->GetJSThread()->SetCallNapiGetStack(callNapiGetStack_);
    generator_->SetIsStart(true);
    Taskpool::GetCurrentTaskpool()->PostTask(
        std::make_unique<SamplingProcessor>(vm_->GetJSThread()->GetThreadId(), generator_, interval_));
}

std::unique_ptr<struct ProfileInfo> CpuProfiler::StopCpuProfilerForInfo()
{
    LOG_ECMA(INFO) << "StopCpuProfilerForInfo enter";
    std::unique_ptr<struct ProfileInfo> profileInfo;
    if (!isProfiling_) {
        LOG_ECMA(ERROR) << "Do not execute stop cpuprofiler twice in a row or didn't execute the start\
                                or the sampling thread is not started";
        return profileInfo;
    }
    if (outToFile_) {
        LOG_ECMA(ERROR) << "Can not Stop a CpuProfiler sampling which is for file output by this stop method";
        return profileInfo;
    }
    isProfiling_ = false;
    vm_->GetJSThread()->SetCallNapiGetStack(false);
    generator_->SetIsStart(false);
    generator_->SetIsBreakSampleFlag(true);
    if (generator_->SemPost(0) != 0) {
        LOG_ECMA(ERROR) << "sem_[0] post failed";
        return profileInfo;
    }
    if (generator_->SemWait(1) != 0) {
        LOG_ECMA(ERROR) << "sem_[1] wait failed";
        return profileInfo;
    }
    generator_->FinetuneSampleData();
    generator_->ClearNapiCall();
    profileInfo = generator_->GetProfileInfo();
    return profileInfo;
}

void CpuProfiler::SetCpuSamplingInterval(int interval)
{
    interval_ = static_cast<uint32_t>(interval);
}

void CpuProfiler::SetCallNapiGetStack(bool getStack)
{
    callNapiGetStack_ = getStack;
}

void CpuProfiler::StopCpuProfilerForFile()
{
    LOG_ECMA(INFO) << "StopCpuProfilerForFile enter";
    if (!isProfiling_) {
        LOG_ECMA(ERROR) << "Do not execute stop cpuprofiler twice in a row or didn't execute the start\
                                or the sampling thread is not started";
        return;
    }

    if (!outToFile_) {
        LOG_ECMA(ERROR) << "Can not Stop a CpuProfiler sampling which is for return profile info by\
                                this stop method";
        return;
    }

    isProfiling_ = false;
    vm_->GetJSThread()->SetCallNapiGetStack(false);
    generator_->SetIsStart(false);
    generator_->SetIsBreakSampleFlag(true);
    if (generator_->SemPost(0) != 0) {
        LOG_ECMA(ERROR) << "sem_[0] post failed";
        return;
    }
    if (generator_->SemWait(1) != 0) {
        LOG_ECMA(ERROR) << "sem_[1] wait failed";
        return;
    }
    generator_->FinetuneSampleData();
    generator_->ClearNapiCall();
    generator_->StringifySampleData();
    std::string fileData = generator_->GetSampleData();
    generator_->fileHandle_ << fileData;
}

CpuProfiler::~CpuProfiler()
{
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

void CpuProfiler::SetProfileStart(uint64_t nowTimeStamp)
{
    uint64_t ts = SamplingProcessor::GetMicrosecondsTimeStamp();
    struct CurrentProcessInfo currentProcessInfo = {0};
    GetCurrentProcessInfo(currentProcessInfo);
    std::string data = "";
    data = "[{\"args\":{\"data\":{\"frames\":[{\"processId\":" + std::to_string(currentProcessInfo.pid) + "}]"
            + ",\"persistentIds\":true}},\"cat\":\"disabled-by-default-devtools.timeline\","
            + "\"name\":\"TracingStartedInBrowser\",\"ph\":\"I\",\"pid\":"
            + std::to_string(currentProcessInfo.pid) + ",\"s\":\"t\",\"tid\":"
            + std::to_string(currentProcessInfo.tid) + ",\"ts\":"
            + std::to_string(ts) + ",\"tts\":178460227},\n";
    ts = SamplingProcessor::GetMicrosecondsTimeStamp();
    data += "{\"args\":{\"data\":{\"startTime\":" + std::to_string(nowTimeStamp) + "}},"
            + "\"cat\":\"disabled-by-default-ark.cpu_profiler\",\"id\":\"0x2\","
            + "\"name\":\"Profile\",\"ph\":\"P\",\"pid\":"
            + std::to_string(currentProcessInfo.pid) + ",\"tid\":"
            + std::to_string(currentProcessInfo.tid) + ",\"ts\":"
            + std::to_string(ts) + ",\"tts\":" + std::to_string(currentProcessInfo.tts)
            + "},\n";
    generator_->SetStartsampleData(data);
}

void CpuProfiler::GetCurrentProcessInfo(struct CurrentProcessInfo &currentProcessInfo)
{
    currentProcessInfo.nowTimeStamp = SamplingProcessor::GetMicrosecondsTimeStamp();
    currentProcessInfo.pid = getpid();
    if (syscall(SYS_gettid) == -1) {
        LOG_FULL(FATAL) << "syscall failed";
        UNREACHABLE();
    }
    currentProcessInfo.tts = SamplingProcessor::GetMicrosecondsTimeStamp();
}

RunningState CpuProfiler::GetRunningState(const FrameIterator &it,
                                          const JSPandaFile *jsPandaFile, bool isLeaveFrame) const
{
    JSThread *thread = vm_->GetAssociatedJSThread();
    if (thread->GetGcState()) {
        return RunningState::GC;
    }
    JSFunction* function = JSFunction::Cast(it.GetFunction().GetTaggedObject());
    JSTaggedValue extraInfoValue = function->GetFunctionExtraInfo();
    if (extraInfoValue.IsJSNativePointer() || jsPandaFile == nullptr) {
        // napi method
        if (function->IsCallNative()) {
            return RunningState::NAPI;
        }
        if (extraInfoValue.IsJSNativePointer()) {
            return RunningState::ARKUI_ENGINE;
        }
        return RunningState::BUILTIN;
    }
    if (isLeaveFrame) {
        return RunningState::RUNTIME;
    }
    if (it.GetFrameType() == FrameType::OPTIMIZED_JS_FUNCTION_FRAME) {
        return RunningState::AOT;
    }
    if (thread->GetRuntimeState()) {
        return RunningState::RUNTIME;
    }
    if (thread->IsAsmInterpreter()) {
        return RunningState::AINT;
    }
    return RunningState::CINT;
}

void CpuProfiler::GetFrameStack(FrameIterator &it, bool isLeaveFrame)
{
    const CMap<struct MethodKey, struct FrameInfo> &stackInfo = generator_->GetStackInfo();
    bool firstFrame = true;
    for (; !it.Done(); it.Advance<>()) {
        auto method = it.CheckAndGetMethod();
        if (method == nullptr) {
            continue;
        }
        const JSPandaFile *jsPandaFile = method->GetJSPandaFile();
        struct MethodKey methodKey;
        if (firstFrame) {
            methodKey.state = GetRunningState(it, jsPandaFile, isLeaveFrame);
            JSFunction* function = JSFunction::Cast(it.GetFunction().GetTaggedObject());
            if (function->IsCallNative()) {
                methodKey.napiCallCount = napiCallCount_;
            }
            firstFrame = false;
        }
        methodKey.methodIdentifier = GetMethodIdentifier(method, it);

        if (stackInfo.count(methodKey) == 0) {
            if (UNLIKELY(!ParseMethodInfo(methodKey, it, jsPandaFile, false))) {
                generator_->SetIsBreakSampleFlag(true);
                return;
            }
        }
        if (UNLIKELY(!generator_->PushFrameStack(methodKey))) {
            generator_->SetIsBreakSampleFlag(true);
            return;
        }
    }
}

bool CpuProfiler::GetFrameStackCallNapi(JSThread *thread)
{
    FrameHandler frameHandler(thread);
    const CMap<struct MethodKey, struct FrameInfo> &stackInfo = generator_->GetStackInfo();
    generator_->ClearNapiStack();
    bool firstFrame = true;
    for (FrameIterator it(frameHandler.GetSp(), thread); !it.Done(); it.Advance<GCVisitedFlag::IGNORED>()) {
        auto method = it.CheckAndGetMethod();
        if (method == nullptr) {
            continue;
        }

        struct MethodKey methodKey;
        if (firstFrame) {
            methodKey.state = RunningState::NAPI;
            napiCallCount_++;
            methodKey.napiCallCount = napiCallCount_;
            firstFrame = false;
        }
        methodKey.methodIdentifier = GetMethodIdentifier(method, it);
        if (stackInfo.count(methodKey) == 0) {
            if (UNLIKELY(!ParseMethodInfo(methodKey, it, method->GetJSPandaFile(), true))) {
                return false;
            }
        }
        if (UNLIKELY(!generator_->PushNapiFrameStack(methodKey))) {
            return false;
        }
    }
    return true;
}

bool CpuProfiler::ParseMethodInfo(struct MethodKey &methodKey,
                                  const FrameIterator &it,
                                  const JSPandaFile *jsPandaFile,
                                  bool isCallNapi)
{
    struct FrameInfoTemp codeEntry;
    codeEntry.methodKey = methodKey;
    JSFunction* function = JSFunction::Cast(it.GetFunction().GetTaggedObject());
    JSTaggedValue extraInfoValue = function->GetNativeFunctionExtraInfo();
    if (extraInfoValue.IsJSNativePointer() || jsPandaFile == nullptr) {
        if (!CheckAndCopy(codeEntry.codeType, sizeof(codeEntry.codeType), "other")) {
            return false;
        }
        GetNativeStack(it, codeEntry.functionName, sizeof(codeEntry.functionName));
    } else {
        if (!CheckAndCopy(codeEntry.codeType, sizeof(codeEntry.codeType), "JS")) {
            return false;
        }
        const char *tempVariable = MethodLiteral::GetMethodName(jsPandaFile,
            reinterpret_cast<MethodLiteral *>(methodKey.methodIdentifier)->GetMethodId());
        uint8_t length = strlen(tempVariable);
        if (length != 0 && tempVariable[0] == '#') {
            uint8_t index = length - 1;
            while (tempVariable[index] != '#') {
                index--;
            }
            tempVariable += (index + 1);
        }
        if (strlen(tempVariable) == 0) {
            tempVariable = "anonymous";
        }
        if (!CheckAndCopy(codeEntry.functionName, sizeof(codeEntry.functionName), tempVariable)) {
            return false;
        }
        // source file
        DebugInfoExtractor *debugExtractor =
            JSPandaFileManager::GetInstance()->GetJSPtExtractor(jsPandaFile);
        const std::string &sourceFile = debugExtractor->GetSourceFile(
                                        reinterpret_cast<MethodLiteral *>(methodKey.methodIdentifier)->GetMethodId());
        if (sourceFile.empty()) {
            tempVariable = "";
        } else {
            tempVariable = sourceFile.c_str();
        }
        if (!CheckAndCopy(codeEntry.url, sizeof(codeEntry.url), tempVariable)) {
            return false;
        }
        // line number and clomn number
        panda_file::File::EntityId methodId =
                                   reinterpret_cast<MethodLiteral *>(methodKey.methodIdentifier)->GetMethodId();
        codeEntry.lineNumber = debugExtractor->GetFristLine(methodId);
        codeEntry.columnNumber = debugExtractor->GetFristColumn(methodId);
    }
    if (isCallNapi) {
        return generator_->PushNapiStackInfo(codeEntry);
    }
    return generator_->PushStackInfo(codeEntry);
}

void CpuProfiler::GetNativeStack(const FrameIterator &it, char *functionName, size_t size)
{
    std::stringstream stream;
    JSFunction* function = JSFunction::Cast(it.GetFunction().GetTaggedObject());
    JSTaggedValue extraInfoValue = function->GetNativeFunctionExtraInfo();
    JSThread *thread = vm_->GetJSThread();
    const GlobalEnvConstants *globalConst = thread->GlobalConstants();
    JSHandle<JSTaggedValue> nameKey = globalConst->GetHandledNameString();
    JSHandle<JSTaggedValue> func = JSHandle<JSTaggedValue>(thread, function);
    JSHandle<JSTaggedValue> funcNameValue = JSObject::GetProperty(thread, func, nameKey).GetValue();
    std::string methodNameStr;
    if (funcNameValue->IsString()) {
        JSHandle<EcmaString> methodName(funcNameValue);
        methodNameStr = EcmaStringAccessor(methodName).ToStdString();
    }
    // napi method
    if (function->IsCallNative() && extraInfoValue.CheckIsJSNativePointer()) {
        JSNativePointer *extraInfo = JSNativePointer::Cast(extraInfoValue.GetTaggedObject());
        auto cb = vm_->GetNativePtrGetter();
        if (cb != nullptr  && extraInfo != nullptr) {
            if (!vm_->GetJSThread()->CpuProfilerCheckJSTaggedType(extraInfoValue.GetRawData())) {
                return;
            }
            auto addr = cb(reinterpret_cast<void *>(extraInfo->GetData()));
            stream << addr;
            CheckAndCopy(functionName, size, methodNameStr.c_str());
            const uint8_t methodNameStrLength = methodNameStr.size();
            CheckAndCopy(functionName + methodNameStrLength, size - methodNameStrLength, "(");
            const uint8_t napiBeginLength = 1; // 1:the length of "("
            CheckAndCopy(functionName + methodNameStrLength + napiBeginLength, size - methodNameStrLength - napiBeginLength, stream.str().c_str());
            uint8_t srcLength = stream.str().size();
            CheckAndCopy(functionName + methodNameStrLength + napiBeginLength + srcLength, size - methodNameStrLength - napiBeginLength - srcLength, ")");
            return;
        }
    }
    if (extraInfoValue.CheckIsJSNativePointer()) {
        stream << JSNativePointer::Cast(extraInfoValue.GetTaggedObject())->GetExternalPointer();
        CheckAndCopy(functionName, size, "arkui(");
        const uint8_t arkuiBeginLength = 6; // 6:the length of "arkui("
        CheckAndCopy(functionName + arkuiBeginLength, size - arkuiBeginLength, methodNameStr.c_str());
        const uint8_t methodNameStrLength = methodNameStr.size();
        CheckAndCopy(functionName + arkuiBeginLength + methodNameStrLength, size - arkuiBeginLength - methodNameStrLength, stream.str().c_str());
        uint8_t srcLength = stream.str().size();
        CheckAndCopy(functionName + arkuiBeginLength + methodNameStrLength + srcLength, size - arkuiBeginLength - methodNameStrLength - srcLength, ")");
        return;
    }
    // builtin method
    auto method = it.CheckAndGetMethod();
    auto addr = method->GetNativePointer();
    stream << addr;
    CheckAndCopy(functionName, size, "builtin(");
    const uint8_t builtinBeginLength = 8; // 8:the length of "builtin("
    CheckAndCopy(functionName + builtinBeginLength, size - builtinBeginLength, methodNameStr.c_str());
    const uint8_t methodNameStrLength = methodNameStr.size();
    CheckAndCopy(functionName + builtinBeginLength + methodNameStrLength, size - builtinBeginLength - methodNameStrLength, stream.str().c_str());
    uint8_t srcLength = stream.str().size();
    CheckAndCopy(functionName + builtinBeginLength + methodNameStrLength + srcLength, size - builtinBeginLength - methodNameStrLength - srcLength, ")");
}

void CpuProfiler::IsNeedAndGetStack(JSThread *thread)
{
    if (thread->GetStackSignal()) {
        FrameHandler frameHandler(thread);
        FrameIterator it(frameHandler.GetSp(), thread);
        GetFrameStack(it, false);
        if (generator_->SemPost(0) != 0) {
            LOG_ECMA(ERROR) << "sem_[0] post failed";
            return;
        }
        thread->SetGetStackSignal(false);
    }
}

void CpuProfiler::GetStackBeforeCallNapi(JSThread *thread, const std::string &methodAddr)
{
    generator_->SetBeforeGetCallNapiStackFlag(true);
    if (GetFrameStackCallNapi(thread)) {
        generator_->SetCallNapiFlag(true);
        generator_->SetAfterGetCallNapiStackFlag(true);
        if (generator_->SemWait(2) != 0) { // 2: signal 2
            LOG_ECMA(ERROR) << "sem_[2] wait failed";
            return;
        }
    } else {
        generator_->SetBeforeGetCallNapiStackFlag(false);
    }
    RecordCallNapiInfo(methodAddr);
}

void CpuProfiler::GetStackSignalHandler(int signal, [[maybe_unused]] siginfo_t *siginfo, void *context)
{
    if (signal != SIGPROF) return;
    CpuProfiler *profiler = nullptr;
    JSThread *thread = nullptr;
    {
        os::memory::LockHolder lock(synchronizationMutex_);
        pthread_t tid = static_cast<pthread_t>(syscall(SYS_gettid));
        const EcmaVM *vm = profilerMap_[tid];
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
    uint64_t pc = 0;
    if (thread->IsAsmInterpreter()) {
        pc = GetPcFromContext(context);
    }
    if (thread->IsAsmInterpreter() && profiler->IsAddrAtStubOrAot(pc) &&
        !profiler->IsEntryFrameHeaderOrTail(thread, pc)) {
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
        if (reinterpret_cast<uint64_t*>(sp) > reinterpret_cast<uint64_t*>(fp)) {
            LOG_ECMA(ERROR) << "sp > fp, stack frame exception";
            if (profiler->generator_->SemPost(0) != 0) {
                LOG_ECMA(ERROR) << "sem_[0] post failed";
                return;
            }
            return;
        }
        if (profiler->CheckFrameType(thread, reinterpret_cast<JSTaggedType *>(fp))) {
            FrameIterator it(reinterpret_cast<JSTaggedType *>(fp), thread);
            profiler->GetFrameStack(it, false);
        }
    } else if (thread->IsAsmInterpreter()) {
        if (thread->GetLastLeaveFrame() != nullptr) {
            JSTaggedType *leaveFrame = const_cast<JSTaggedType *>(thread->GetLastLeaveFrame());
            if (profiler->CheckFrameType(thread, leaveFrame)) {
                FrameIterator it(leaveFrame, thread);
                profiler->GetFrameStack(it, true);
            }
        }
    } else {
        if (thread->GetCurrentFrame() != nullptr) {
            if (profiler->CheckFrameType(thread, const_cast<JSTaggedType *>(thread->GetCurrentFrame()))) {
                FrameHandler frameHandler(thread);
                FrameIterator it(frameHandler.GetSp(), thread);
                profiler->GetFrameStack(it, false);
            }
        }
    }
    if (profiler->generator_->SemPost(0) != 0) {
        LOG_ECMA(ERROR) << "sem_[0] post failed";
        return;
    }
}

bool CpuProfiler::CheckFrameType(JSThread *thread, JSTaggedType *sp)
{
    FrameType type = FrameHandler::GetFrameType(sp);
    if (type > FrameType::FRAME_TYPE_LAST || type < FrameType::FRAME_TYPE_FIRST) {
        return false;
    }

    FrameIterator iterator(sp, thread);
    iterator.Advance();
    JSTaggedType *preSp = iterator.GetSp();
    if (preSp == nullptr) {
        return true;
    }
#if defined(PANDA_TARGET_64)
    if (thread->IsAsmInterpreter() && !thread->IsLegalAsmSp(reinterpret_cast<uintptr_t>(preSp))) {
        return false;
    }
#endif
    type = FrameHandler::GetFrameType(preSp);
    if (type > FrameType::FRAME_TYPE_LAST || type < FrameType::FRAME_TYPE_FIRST) {
        return false;
    }
    return true;
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
    [[maybe_unused]] uint64_t pc = 0;
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

bool CpuProfiler::IsAddrAtStubOrAot(uint64_t pc) const
{
    AOTFileManager *loader = vm_->GetAOTFileManager();
    return loader->InsideStub(pc) || loader->InsideAOT(pc);
}

bool CpuProfiler::CheckFileName(const std::string &fileName, std::string &absoluteFilePath) const
{
    if (fileName.empty()) {
        return true;
    }

    if (fileName.size() > PATH_MAX) {
        return false;
    }

    CVector<char> resolvedPath(PATH_MAX);
    auto result = realpath(fileName.c_str(), resolvedPath.data());
    if (result == nullptr) {
        LOG_ECMA(INFO) << "The file path does not exist";
        return false;
    }
    std::ofstream file(resolvedPath.data());
    if (!file.good()) {
        return false;
    }
    file.close();
    absoluteFilePath = resolvedPath.data();
    return true;
}

bool CpuProfiler::CheckAndCopy(char *dest, size_t length, const char *src) const
{
    int srcLength = strlen(src);
    if (length <= static_cast<size_t>(srcLength) || strcpy_s(dest, srcLength + 1, src) != EOK) {
        LOG_ECMA(ERROR) << "CpuProfiler parseMethodInfo strcpy_s failed, maybe srcLength more than destLength";
        return false;
    }
    dest[srcLength] = '\0';
    return true;
}

void *CpuProfiler::GetMethodIdentifier(Method *method, const FrameIterator &it)
{
    MethodLiteral *methodLiteral = method->GetMethodLiteral();
    if (methodLiteral != nullptr) {
        return reinterpret_cast<void *>(methodLiteral);
    }

    JSFunction* function = JSFunction::Cast(it.GetFunction().GetTaggedObject());
    JSTaggedValue extraInfoValue = function->GetNativeFunctionExtraInfo();
    if (extraInfoValue.IsUndefined()) {
        return const_cast<void *>(method->GetNativePointer());
    }
    JSNativePointer *extraInfo = JSNativePointer::Cast(extraInfoValue.GetTaggedObject());
    return reinterpret_cast<void *>(extraInfo->GetData());
}

void CpuProfiler::RecordCallNapiInfo(const std::string &methodAddr)
{
    uint64_t currentTime = SamplingProcessor::GetMicrosecondsTimeStamp();
    generator_->RecordCallNapiTime(currentTime);
    generator_->RecordCallNapiAddr(methodAddr);
}
} // namespace panda::ecmascript
