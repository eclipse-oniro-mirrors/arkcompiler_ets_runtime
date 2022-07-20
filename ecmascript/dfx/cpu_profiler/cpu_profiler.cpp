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

#include "ecmascript/frames.h"
#include "ecmascript/file_loader.h"
#include "ecmascript/taskpool/taskpool.h"
#include "ecmascript/jspandafile/js_pandafile_manager.h"
#include "ecmascript/dfx/cpu_profiler/samples_record.h"
#include "ecmascript/dfx/cpu_profiler/sampling_processor.h"

namespace panda::ecmascript {
std::atomic<CpuProfiler*> CpuProfiler::singleton_ = nullptr;
os::memory::Mutex CpuProfiler::synchronizationMutex_;
CpuProfiler *CpuProfiler::pThis_ = nullptr;
CpuProfiler::CpuProfiler()
{
    pThis_ = this;
    generator_ = new SamplesRecord();
    if (generator_->SemInit(0, 0, 0) != 0) {
        LOG_ECMA(ERROR) << "sem_[0] init failed";
    }
    if (generator_->SemInit(1, 0, 0) != 0) {
        LOG_ECMA(ERROR) << "sem_[1] init failed";
    }
}

CpuProfiler *CpuProfiler::GetInstance()
{
    CpuProfiler *temp = singleton_;
    if (temp == nullptr) {
        os::memory::LockHolder lock(synchronizationMutex_);
        if ((temp = CpuProfiler::singleton_) == nullptr) {
            CpuProfiler::singleton_ = temp = new CpuProfiler();
        }
    }
    return CpuProfiler::singleton_;
}

void CpuProfiler::StartCpuProfilerForInfo(const EcmaVM *vm)
{
    if (isProfiling_) {
        return;
    }
    isProfiling_ = true;
    vm_ = const_cast<EcmaVM *>(vm);
#if ECMASCRIPT_ENABLE_ACTIVE_CPUPROFILER
#else
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
#endif
    tid_ = static_cast<pthread_t>(syscall(SYS_gettid));
    generator_->SetIsStart(true);
    Taskpool::GetCurrentTaskpool()->PostTask(
        std::make_unique<SamplingProcessor>(generator_, interval_, outToFile_));
}

void CpuProfiler::StartCpuProfilerForFile(const EcmaVM *vm, const std::string &fileName)
{
    if (isProfiling_) {
        return;
    }
    isProfiling_ = true;
    vm_ = const_cast<EcmaVM *>(vm);
    std::string absoluteFilePath("");
    if (!CheckFileName(fileName, absoluteFilePath)) {
        LOG_ECMA(ERROR) << "The fileName contains illegal characters";
        isProfiling_ = false;
        return;
    }
    fileName_ = absoluteFilePath;
    if (fileName_.empty()) {
        fileName_ = GetProfileName();
    }
    generator_->SetFileName(fileName_);
    generator_->fileHandle_.open(fileName_.c_str());
    if (generator_->fileHandle_.fail()) {
        LOG_ECMA(ERROR) << "File open failed";
        isProfiling_ = false;
        return;
    }
#if ECMASCRIPT_ENABLE_ACTIVE_CPUPROFILER
#else
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
#endif
    uint64_t ts = SamplingProcessor::GetMicrosecondsTimeStamp();
    ts = ts % TIME_CHANGE;
    SetProfileStart(ts);
    outToFile_ = true;
    generator_->SetIsStart(true);
    Taskpool::GetCurrentTaskpool()->PostTask(
        std::make_unique<SamplingProcessor>(generator_, interval_, outToFile_));
}

std::unique_ptr<struct ProfileInfo> CpuProfiler::StopCpuProfilerForInfo()
{
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
    if (static_cast<long>(tid_) != syscall(SYS_gettid)) {
        LOG_ECMA(ERROR) << "Thread attempted to close other sampling threads";
        return profileInfo;
    }
    isProfiling_ = false;
    generator_->SetIsStart(false);
    generator_->SetLastSampleFlag(true);
    if (generator_->SemPost(0) != 0) {
        LOG_ECMA(ERROR) << "sem_[0] post failed";
        return profileInfo;
    }
    if (generator_->SemWait(1) != 0) {
        LOG_ECMA(ERROR) << "sem_[1] wait failed";
        return profileInfo;
    }

    profileInfo = generator_->GetProfileInfo();
    return profileInfo;
}

void CpuProfiler::SetCpuSamplingInterval(int interval)
{
    interval_ = interval;
}

void CpuProfiler::StopCpuProfilerForFile()
{
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

    if (static_cast<long>(tid_) != syscall(SYS_gettid)) {
        LOG_ECMA(ERROR) << "Thread attempted to close other sampling threads";
        return;
    }
    isProfiling_ = false;
    generator_->SetIsStart(false);
    generator_->SetLastSampleFlag(true);
    if (generator_->SemPost(0) != 0) {
        LOG_ECMA(ERROR) << "sem_[0] post failed";
        return;
    }
    if (generator_->SemWait(1) != 0) {
        LOG_ECMA(ERROR) << "sem_[1] wait failed";
        return;
    }
    generator_->WriteMethodsAndSampleInfo(true);
    std::string fileData = generator_->GetSampleData();
    fileData.replace(fileData.size() - 2, 1, "]"); // 2: Subscript with comma at end of string
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
    if (generator_ != nullptr) {
        delete generator_;
        generator_ = nullptr;
    }
    singleton_ = nullptr;
}

void CpuProfiler::SetProfileStart(uint64_t nowTimeStamp)
{
    uint64_t ts = SamplingProcessor::GetMicrosecondsTimeStamp();
    ts = ts % TIME_CHANGE;
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
    ts = ts % TIME_CHANGE;
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
    currentProcessInfo.nowTimeStamp = SamplingProcessor::GetMicrosecondsTimeStamp() % TIME_CHANGE;
    currentProcessInfo.pid = getpid();
    if (syscall(SYS_gettid) == -1) {
        LOG_FULL(FATAL) << "syscall failed";
        UNREACHABLE();
    }
    tid_ = currentProcessInfo.tid = static_cast<pthread_t>(syscall(SYS_gettid));
    struct timespec time = {0, 0};
    clock_gettime(CLOCK_MONOTONIC, &time);
    currentProcessInfo.tts = static_cast<uint64_t>(time.tv_nsec) / 1000; // 1000:Nanoseconds to milliseconds.
}

void CpuProfiler::GetFrameStack(FrameHandler &frameHandler)
{
    generator_->ClearFrameStack();
    const CMap<JSMethod *, struct FrameInfo> &stackInfo = generator_->GetStackInfo();
    generator_->SetGcState(vm_->GetAssociatedJSThread()->GetGcState());
    if (!generator_->GetGcState()) {
        for (; frameHandler.HasFrame(); frameHandler.PrevInterpretedFrame()) {
            if (!frameHandler.IsInterpretedFrame()) {
                continue;
            }
            auto method = frameHandler.CheckAndGetMethod();
            if (method == nullptr) {
                break;
            }

            if (stackInfo.count(method) == 0) {
                ParseMethodInfo(method, frameHandler);
            }
            generator_->PushFrameStack(method);
        }
    }
}

void CpuProfiler::ParseMethodInfo(JSMethod *method, FrameHandler &frameHandler)
{
    struct FrameInfo codeEntry;
    if (method != nullptr && method->IsNativeWithCallField()) {
        codeEntry.codeType = "other";
        auto addr = method->GetNativePointer();
        std::stringstream strm;
        strm << addr;
        codeEntry.functionName = "native(" + strm.str() + ")";
        generator_->InsertStackInfo(method, codeEntry);
    } else if (method != nullptr) {
        codeEntry.codeType = "JS";
        const std::string &functionName = method->ParseFunctionName();
        if (functionName.empty()) {
            codeEntry.functionName = "anonymous";
        } else {
            codeEntry.functionName = functionName;
        }
        // source file
        tooling::JSPtExtractor *debugExtractor =
            JSPandaFileManager::GetInstance()->GetJSPtExtractor(method->GetJSPandaFile());
        const std::string &sourceFile = debugExtractor->GetSourceFile(method->GetMethodId());
        if (sourceFile.empty()) {
            codeEntry.url = "";
        } else {
            codeEntry.url = sourceFile;
            const CMap<std::string, int> &scriptIdMap = generator_->GetScriptIdMap();
            auto iter = scriptIdMap.find(codeEntry.url);
            if (iter == scriptIdMap.end()) {
                generator_->InsertScriptId(codeEntry.url, scriptIdMap.size() + 1);
                codeEntry.scriptId = static_cast<int>(scriptIdMap.size());
            } else {
                codeEntry.scriptId = iter->second;
            }
        }
        // line number
        int32_t lineNumber = 0;
        int32_t columnNumber = 0;
        auto callbackLineFunc = [&](int32_t line) -> bool {
            lineNumber = line + 1;
            return true;
        };
        auto callbackColumnFunc = [&](int32_t column) -> bool {
            columnNumber = column + 1;
            return true;
        };
        panda_file::File::EntityId methodId = method->GetMethodId();
        uint32_t offset = frameHandler.GetBytecodeOffset();
        if (!debugExtractor->MatchLineWithOffset(callbackLineFunc, methodId, offset) ||
            !debugExtractor->MatchColumnWithOffset(callbackColumnFunc, methodId, offset)) {
            codeEntry.lineNumber = 0;
            codeEntry.columnNumber = 0;
        } else {
            codeEntry.lineNumber = lineNumber;
            codeEntry.columnNumber = columnNumber;
        }
        CMap<JSMethod *, struct FrameInfo> value;
        value[method] = codeEntry;
        generator_->InsertStackInfo(method, codeEntry);
    }
}

void CpuProfiler::IsNeedAndGetStack(JSThread *thread)
{
    if (thread->GetStackSignal()) {
        FrameHandler frameHandler(thread);
        GetFrameStack(frameHandler);
        if (generator_->SemPost(0) != 0) {
            LOG_ECMA(ERROR) << "sem_[0] post failed";
            return;
        }
        thread->SetGetStackSignal(false);
    }
}

void CpuProfiler::GetStackSignalHandler(int signal, [[maybe_unused]] siginfo_t *siginfo, void *context)
{
    if (signal != SIGPROF) return;
    JSThread *thread = pThis_->vm_->GetAssociatedJSThread();
    if (thread->IsAsmInterpreter() && pThis_->IsAddrAtStub(context)) {
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
        LOG_ECMA(FATAL) << "Cpuprofiler does not currently support other platforms, please run on x64 and arm64";
        return;
#endif
        if (reinterpret_cast<uint64_t*>(sp) > reinterpret_cast<uint64_t*>(fp)) {
            LOG_ECMA(FATAL) << "sp > fp, stack frame exception";
        }
        if (pThis_->CheckFrameType(thread, reinterpret_cast<JSTaggedType *>(fp))) {
            FrameHandler frameHandler(thread, fp);
            pThis_->GetFrameStack(frameHandler);
        }
    } else {
        if (thread->GetCurrentFrame() != nullptr) {
            if (pThis_->CheckFrameType(thread, const_cast<JSTaggedType *>(thread->GetCurrentFrame()))) {
                FrameHandler frameHandler(thread);
                pThis_->GetFrameStack(frameHandler);
            }
        }
    }
    if (pThis_->generator_->SemPost(0) != 0) {
        LOG_ECMA(ERROR) << "sem_[0] post failed";
        return;
    }
}

bool CpuProfiler::CheckFrameType(JSThread *thread, JSTaggedType *sp)
{
    FrameType type = FrameHandler::GetFrameType(sp);
    if (type > FrameType::FRAME_TYPE_END || type < FrameType::FRAME_TYPE_BEGIN) {
        return false;
    }

    FrameIterator iterator(sp, thread);
    iterator.Advance();
    JSTaggedType *preSp = iterator.GetSp();
    if (preSp == nullptr) {
        return true;
    }
    type = FrameHandler::GetFrameType(preSp);
    if (type > FrameType::FRAME_TYPE_END || type < FrameType::FRAME_TYPE_BEGIN) {
        return false;
    }
    return true;
}

bool CpuProfiler::IsAddrAtStub(void *context)
{
    [[maybe_unused]] ucontext_t *ucontext = reinterpret_cast<ucontext_t*>(context);
    [[maybe_unused]] mcontext_t &mcontext = ucontext->uc_mcontext;
    [[maybe_unused]] uint64_t pc = 0;
#if defined(PANDA_TARGET_AMD64)
    pc = static_cast<uint64_t>(mcontext.gregs[REG_RIP]);
#elif defined(PANDA_TARGET_ARM64)
    pc = static_cast<uint64_t>(mcontext.pc);
#else
    LOG_ECMA(FATAL) << "Cpuprofiler does not currently support other platforms, please run on x64 and arm64";
    return true;
#endif
    FileLoader *loader = vm_->GetFileLoader();
    if (loader == nullptr) {
        return false;
    }
    const StubModulePackInfo &stubPackInfo = loader->GetStubPackInfo();
    uint64_t stubStartAddr = stubPackInfo.GetAsmStubAddr();
    uint64_t stubEndAddr = stubStartAddr + stubPackInfo.GetAsmStubSize();
    if (pc >= stubStartAddr && pc <= stubEndAddr) {
        return true;
    }

    const std::vector<ModuleSectionDes> &des = stubPackInfo.GetCodeUnits();
    stubStartAddr = des[0].GetSecAddr(ElfSecName::TEXT);
    stubEndAddr = stubStartAddr + des[0].GetSecSize(ElfSecName::TEXT);
    if (pc >= stubStartAddr && pc <= stubEndAddr) {
        return true;
    }

    stubStartAddr = des[1].GetSecAddr(ElfSecName::TEXT);
    stubEndAddr = stubStartAddr + des[1].GetSecSize(ElfSecName::TEXT);
    if (pc >= stubStartAddr && pc <= stubEndAddr) {
        return true;
    }
    return false;
}

std::string CpuProfiler::GetProfileName() const
{
    char time1[16] = {0}; // 16:Time format length
    char time2[16] = {0}; // 16:Time format length
    time_t timep = std::time(nullptr);
    struct tm nowTime1;
    localtime_r(&timep, &nowTime1);
    size_t result = 0;
    result = strftime(time1, sizeof(time1), "%Y%m%d", &nowTime1);
    if (result == 0) {
        LOG_ECMA(ERROR) << "get time failed";
        return "";
    }
    result = strftime(time2, sizeof(time2), "%H%M%S", &nowTime1);
    if (result == 0) {
        LOG_ECMA(ERROR) << "get time failed";
        return "";
    }
    std::string profileName = "cpuprofile-";
    profileName += time1;
    profileName += "TO";
    profileName += time2;
    profileName += ".json";
    return profileName;
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
    }
    std::ofstream file(resolvedPath.data());
    if (!file.good()) {
        return false;
    }
    file.close();
    absoluteFilePath = resolvedPath.data();
    return true;
}
} // namespace panda::ecmascript
