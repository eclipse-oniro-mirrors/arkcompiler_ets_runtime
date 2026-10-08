/*
 * Copyright (c) 2024-2026 Huawei Device Co., Ltd.
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

#include "ecmascript/dfx/stackinfo/js_stackinfo.h"

#include "ecmascript/base/error_helper.h"
#include "ecmascript/base/string_helper.h"
#include "ecmascript/compiler/aot_file/elf_reader.h"
#include "ecmascript/module/module_path_helper.h"
#include "ecmascript/platform/aot_crash_info.h"
#include "ecmascript/platform/file.h"
#include "ecmascript/platform/os.h"
#include "ecmascript/snapshot/common/modules_snapshot_helper.h"
#include "ecmascript/stubs/runtime_stubs-inl.h"
#include "ecmascript/jit/jit.h"
#include "ecmascript/ohos/aot_runtime_info.h"


#if ECMASCRIPT_ENABLE_ARK_STEED
#include <limits>
#include "ecmascript/arksteed/arksteed_safepoint_table.h"
#endif

#if defined(ENABLE_EXCEPTION_BACKTRACE)
#include "ecmascript/platform/backtrace.h"
#endif
#if defined(ENABLE_STATIC_BACKTRACE)
#include "tooling/backtrace/backtrace.h"
#endif

namespace panda::ecmascript {
std::unordered_map<EntityId, std::string> JsStackInfo::nameMap;
std::unordered_map<EntityId, std::vector<uint8>> JsStackInfo::machineCodeMap;
JSStackTrace *JSStackTrace::trace_ = nullptr;
std::mutex JSStackTrace::mutex_;
size_t JSStackTrace::count_ = 0;
#if defined(ENABLE_STATIC_BACKTRACE)
void *JSStackTrace::arkStaticHandle_ = nullptr;
ParseStaticArkLocalFunc JSStackTrace::parseStaticArkLocalFunc_ = nullptr;
CreateStaticArkLocalFunc JSStackTrace::createStaticArkLocalFunc_ = nullptr;
DestroyStaticArkLocalFunc JSStackTrace::destroyStaticArkLocalFunc_ = nullptr;
static_local_trace JSStackTrace::arkStaticLocalTracePtr_ = nullptr;
#endif
static std::mutex nameMapMutex;

bool IsFastJitFunctionFrame(const FrameType frameType)
{
    return frameType == FrameType::FASTJIT_FUNCTION_FRAME || frameType == FrameType::FASTJIT_FAST_CALL_FUNCTION_FRAME;
}

bool IsFastJitFunctionFrame(uintptr_t frameType)
{
    return static_cast<FrameType>(frameType) == FrameType::FASTJIT_FUNCTION_FRAME ||
           static_cast<FrameType>(frameType) == FrameType::FASTJIT_FAST_CALL_FUNCTION_FRAME;
}

bool IsSteedFunctionFrame(const FrameType frameType)
{
    return frameType == FrameType::STEED_FUNCTION_FRAME;
}

bool IsSteedFunctionFrame(uintptr_t frameType)
{
    return static_cast<FrameType>(frameType) == FrameType::STEED_FUNCTION_FRAME;
}

void JsStackInfo::AppendMethodTrace(std::string &data, const JSThread *thread, Method *method, uint32_t pcOffset,
                                    LastBuilderCache &lastCache, bool enableStackSourceFile)
{
    data.append("    at ");
    std::string name = method->ParseFunctionName(thread);
    if (name.empty()) {
        data.append("anonymous (");
    } else {
        data.append(name).append(" (");
    }
    // source file
    DebugInfoExtractor *debugExtractor = nullptr;
    const JSPandaFile *pandaFile = method->GetJSPandaFile(thread);
    if (pandaFile == lastCache.pf) {
        debugExtractor = lastCache.extractor;
    } else {
        debugExtractor = JSPandaFileManager::GetInstance()->GetJSPtExtractor(pandaFile);
        lastCache.pf = pandaFile;
        lastCache.extractor = debugExtractor;
    }
    if (enableStackSourceFile) {
        const std::string &sourceFile = debugExtractor->GetSourceFile(method->GetMethodId());
        if (sourceFile.empty()) {
            data.push_back('?');
        } else {
            data += sourceFile;
        }
    } else {
        data.append("hidden");
    }

    data.push_back(':');
    // line number and column number
    auto callbackLineFunc = [&data](int32_t line) -> bool {
        data += std::to_string(line + 1);
        data.push_back(':');
        return true;
    };
    auto callbackColumnFunc = [&data](int32_t column) -> bool {
        data += std::to_string(column + 1);
        return true;
    };
    panda_file::File::EntityId methodId = method->GetMethodId();
    if (!debugExtractor->MatchLineWithOffset(callbackLineFunc, methodId, pcOffset) ||
        !debugExtractor->MatchColumnWithOffset(callbackColumnFunc, methodId, pcOffset)) {
        data.push_back('?');
    }
    data.append(")\n");
}

void JsStackInfo::DumpJitCode(JSThread *thread)
{
#ifndef EXCLUDE_JIT
    JSTaggedType exception = thread->GetException().GetRawData();
    auto &jitCodeMaps = thread->GetJitCodeMaps();
    auto jitCode = jitCodeMaps.find(exception);
    if (jitCode == jitCodeMaps.end()) {
        return;
    }
    std::set<MachineCode*> memos;
    JsJitDumpElf jitDumpElf;
    jitDumpElf.Init();
    int64 idx = 0;
    size_t offset = 0;
    auto jitCodeVec = jitCodeMaps[exception];
    for (size_t i = 0; i < jitCodeVec->size(); i++) {
        auto item = (*jitCodeVec)[i];
        auto machineCode = std::get<0>(item);
        std::string methodName = std::get<1>(item);
        uintptr_t pcOffset = std::get<2>(item);
        auto res = memos.insert(machineCode);
        if (res.second) {
            LOG_ECMA(ERROR) << "jit : js crash at method : " << methodName << ", offset :" << pcOffset;
            char *funcAddr = reinterpret_cast<char *>(machineCode->GetFuncAddr());
            size_t len = machineCode->GetTextSize();
            std::vector<uint8> vec(len);
            if (memmove_s(vec.data(), len, funcAddr, len) != EOK) {
                LOG_ECMA(ERROR) << "Fail to get machineCode on function addr: " << funcAddr;
            }
            jitDumpElf.AppendData(vec);
            jitDumpElf.AppendSymbolToSymTab(idx++, offset, len, methodName);
            offset += len;
        }
    }
    std::string fileName = "jitCode-" + std::to_string(getpid());
    std::string realOutPath;
    std::string sanboxPath = panda::os::file::File::GetExtendedFilePath(AotCrashInfo::GetSandBoxPath());
    if (!ecmascript::RealPath(sanboxPath, realOutPath, false)) {
        return;
    }
    std::string outFile = realOutPath + "/" + fileName;
    int fd = open(outFile.c_str(), O_RDWR | O_CREAT | O_TRUNC, 0644);
    FdsanExchangeOwnerTag(reinterpret_cast<fd_t>(fd));
    jitDumpElf.WriteJitElfFile(fd);
    Close(reinterpret_cast<fd_t>(fd));
#endif
}

void AssembleJitCodeMap(JSThread *thread, const JSHandle<JSObject> &jsErrorObj, JSFunction *func, Method *method,
                        uintptr_t offset)
{
    ASSERT(!jsErrorObj.GetTaggedValue().IsUndefined());
    JSTaggedValue machineCodeTagVal = func->GetMachineCode(thread);
    if (!machineCodeTagVal.IsMachineCodeObject()) {
        // The function may have been deoptimized and its MachineCode field cleared (set to Undefined)
        // while a stale JIT frame is still on the stack during stacktrace construction.
        return;
    }
    MachineCode *machineCode = MachineCode::Cast(machineCodeTagVal.GetTaggedObject());
    std::string methodName = method->ParseFunctionName(thread);
    if (methodName.empty()) {
        methodName = "anonymous";
    }
    thread->SetJitCodeMap(jsErrorObj.GetTaggedValue().GetRawData(), machineCode, methodName, offset);
}

std::string JsStackInfo::BuildJsStackTrace(JSThread *thread, bool needNative, const JSHandle<JSObject> &jsErrorObj,
                                           bool needNativeStack, uint32_t depth)
{
    std::string data;
    // disallow cross thread execution
    if (thread->IsCrossThreadExecutionEnable()) {
        return data;
    }
    data.reserve(InitialDeeps * InitialLength);
    JSTaggedType *current = const_cast<JSTaggedType *>(thread->GetCurrentFrame());
    FrameIterator it(current, thread);
    uintptr_t baselineNativePc = 0;

    LastBuilderCache lastCache;
    for (; !it.Done() && depth > 0; it.Advance<GCVisitedFlag::HYBRID_STACK>()) {
        if (it.GetFrameType() == FrameType::BASELINE_BUILTIN_FRAME) {
            auto *frame = it.GetFrame<BaselineBuiltinFrame>();
            baselineNativePc = frame->GetReturnAddr();
            continue;
        }
        if (!it.IsJSFrame()) {
            continue;
        }
        auto method = it.CheckAndGetMethod();
        if (method == nullptr) {
            continue;
        }
        if (!method->IsNativeWithCallField()) {
            uint32_t pcOffset = 0;
            bool needBaselineSpecialHandling =
                (it.GetFrameType() == FrameType::ASM_INTERPRETER_FRAME && baselineNativePc != 0);
            if (needBaselineSpecialHandling) {
                // the pcOffste in baseline frame slot is always uint64::max(), so pcOffset should be computed
                JSHandle<JSFunction> function(thread, it.GetFunction());
                pcOffset = RuntimeStubs::RuntimeGetBytecodePcOfstForBaseline(thread, function, baselineNativePc);
                baselineNativePc = 0;
            }
            AppendJsStackTraceInfo(data, thread, method, it, jsErrorObj, lastCache,
                                   needBaselineSpecialHandling, pcOffset);
            --depth;
        } else if (needNative) {
            auto addr = JSFunction::Cast(it.GetFunction().GetTaggedObject())->GetNativePointer();
            std::stringstream strm;
            strm << addr;
            data.append("    at native method (").append(strm.str()).append(")\n");
            --depth;
        }
    }
#if defined(ENABLE_EXCEPTION_BACKTRACE)
    if (!needNativeStack) {
        return data;
    }
    if (data.empty()) {
        std::ostringstream stack;
        Backtrace(stack, true);
        data = stack.str();
    } else if (thread->IsMainThread()) {
        ECMA_BYTRACE_NAME(HITRACE_LEVEL_COMMERCIAL, HITRACE_TAG_ARK, "BacktraceFromFp", "");
        auto vm = thread->GetEcmaVM();
        int size = BacktraceHybrid(vm->GetPcVectorData());
        vm->SetPcVectorSize(size);
    }
#endif
    return data;
}

void JsStackInfo::AppendJsStackTraceInfo(std::string &data, JSThread *thread, Method *method, FrameIterator &it,
                                         const JSHandle<JSObject> &jsErrorObj,
                                         LastBuilderCache &lastCache,
                                         bool needBaselineSpecialHandling, uint32_t pcOffset)
{
    FrameType frameType = it.GetFrameType();
    if (IsFastJitFunctionFrame(frameType) || IsSteedFunctionFrame(frameType)) {
        JSFunction *func = static_cast<JSFunction*>(it.GetFunction().GetTaggedObject());
        if (!jsErrorObj.GetTaggedValue().IsUndefined()) {
            AssembleJitCodeMap(thread, jsErrorObj, func, method, it.GetOptimizedReturnAddr());
        }
    }
    std::vector<std::pair<JSTaggedType, uint32_t>> stackTraceInfos;
    it.GetStackTraceInfos(stackTraceInfos, needBaselineSpecialHandling, pcOffset);
    for (auto &info : stackTraceInfos) {
        Method *methodInline = ECMAObject::Cast(reinterpret_cast<TaggedObject *>(info.first))->GetCallTarget(thread);
        uint32_t pcOffsetInline = info.second;
        AppendMethodTrace(data, thread, methodInline, pcOffsetInline, lastCache,
                          thread->GetEnableStackSourceFile());
    }
}

void JsStackInfo::BuildCrashInfo(bool isJsCrash, uintptr_t pc, JSThread *thread)
{
    if (isJsCrash) {
        ModulesSnapshotHelper::TryDisableSnapshot(thread);
    }

    if (JsStackInfo::loader == nullptr) {
        return;
    }
#if !ECMASCRIPT_ENABLE_ARK_STEED
// In a non-arksteed build no JIT code can appear unless AOT/FastJit/PGO is enabled.
// In an arksteed build arksteed is the only JIT compiler and the forced compile
// entry (ArkTools.arkSteedCompileSync/Async) bypasses enable checks, so JIT code
// may exist even with all three disabled: never skip DumpJitCode there.
    bool isAotOrJitOrPgoEnabled = JsStackInfo::loader->IsEnableAOT();
#ifndef EXCLUDE_JIT
    isAotOrJitOrPgoEnabled = isAotOrJitOrPgoEnabled || Jit::GetInstance()->IsEnableFastJit();
#endif
#ifndef EXCLUDE_PGO
    isAotOrJitOrPgoEnabled = isAotOrJitOrPgoEnabled || pgo::PGOProfilerManager::GetInstance()->IsEnable();
#endif
    if (!isAotOrJitOrPgoEnabled) {
        return;
    }
#endif
    ohos::RuntimeInfoType type;
    if (isJsCrash) {
        type = ohos::RuntimeInfoType::JS;
    } else if (pc != 0 && JsStackInfo::loader != nullptr && JsStackInfo::loader->InsideAOT(pc)) {
        type = ohos::RuntimeInfoType::AOT_CRASH;
    } else {
        type = ohos::RuntimeInfoType::OTHERS;
    }
#if !ENABLE_MEMORY_OPTIMIZATION
    ohos::AotRuntimeInfo::GetInstance().BuildCrashRuntimeInfo(type);
#else
    ohos::AotRuntimeInfo::GetInstance().BuildCrashInfo();
#endif
    if (isJsCrash && thread != nullptr) {
#ifndef EXCLUDE_JIT
        DumpJitCode(thread);
#endif
    }
}

std::vector<struct JsFrameInfo> JsStackInfo::BuildJsStackInfo(JSThread *thread, bool currentStack)
{
    std::vector<struct JsFrameInfo> jsFrame;
    JSTaggedType *current = const_cast<JSTaggedType *>(thread->GetCurrentFrame());
    FrameIterator it(current, thread);
    for (; !it.Done(); it.Advance<GCVisitedFlag::HYBRID_STACK>()) {
        if (!it.IsJSFrame()) {
            continue;
        }
        auto method = it.CheckAndGetMethod();
        if (method == nullptr) {
            continue;
        }
        struct JsFrameInfo frameInfo;
        if (!method->IsNativeWithCallField()) {
            std::string name = method->ParseFunctionName(thread);
            if (name.empty()) {
                frameInfo.functionName = "anonymous";
            } else {
                frameInfo.functionName = name;
            }
            // source file
            DebugInfoExtractor *debugExtractor =
                JSPandaFileManager::GetInstance()->GetJSPtExtractor(method->GetJSPandaFile(thread));
            const std::string &sourceFile = debugExtractor->GetSourceFile(method->GetMethodId());
            if (sourceFile.empty()) {
                frameInfo.fileName = "?";
            } else {
                frameInfo.fileName = sourceFile;
            }
            // line number and column number
            int lineNumber = 0;
            auto callbackLineFunc = [&frameInfo, &lineNumber](int32_t line) -> bool {
                lineNumber = line + 1;
                frameInfo.pos = std::to_string(lineNumber) + ":";
                return true;
            };
            auto callbackColumnFunc = [&frameInfo](int32_t column) -> bool {
                frameInfo.pos += std::to_string(column + 1);
                return true;
            };
            panda_file::File::EntityId methodId = method->GetMethodId();
            uint32_t offset = it.GetBytecodeOffset();
            if (!debugExtractor->MatchLineWithOffset(callbackLineFunc, methodId, offset) ||
                !debugExtractor->MatchColumnWithOffset(callbackColumnFunc, methodId, offset)) {
                frameInfo.pos = "?";
            }
            jsFrame.push_back(std::move(frameInfo));
            if (currentStack) {
                return jsFrame;
            }
        }
    }
    return jsFrame;
}

__attribute__((no_sanitize("hwaddress"))) bool GetTypeOffsetAndPrevOffsetFromFrameType(uintptr_t frameType,
                                                                                       uintptr_t &typeOffset,
                                                                                       uintptr_t &prevOffset)
{
    FrameType type = static_cast<FrameType>(frameType);
    switch (type) {
        case FrameType::OPTIMIZED_FRAME:
            typeOffset = OptimizedFrame::GetTypeOffset();
            prevOffset = OptimizedFrame::GetPrevOffset();
            break;
        case FrameType::OPTIMIZED_ENTRY_FRAME:
            typeOffset = OptimizedEntryFrame::GetTypeOffset();
            prevOffset = OptimizedEntryFrame::GetLeaveFrameFpOffset();
            break;
        case FrameType::BASELINE_BUILTIN_FRAME:
            typeOffset = BaselineBuiltinFrame::GetTypeOffset();
            prevOffset = BaselineBuiltinFrame::GetPrevOffset();
            break;
        case FrameType::ASM_BRIDGE_FRAME:
            typeOffset = AsmBridgeFrame::GetTypeOffset();
            prevOffset = AsmBridgeFrame::GetPrevOffset();
            break;
        case FrameType::OPTIMIZED_JS_FUNCTION_UNFOLD_ARGV_FRAME:
            typeOffset = OptimizedJSFunctionUnfoldArgVFrame::GetTypeOffset();
            prevOffset = OptimizedJSFunctionUnfoldArgVFrame::GetPrevOffset();
            break;
        case FrameType::OPTIMIZED_JS_FUNCTION_ARGS_CONFIG_FRAME:
        case FrameType::OPTIMIZED_JS_FAST_CALL_FUNCTION_FRAME:
        case FrameType::OPTIMIZED_JS_FUNCTION_FRAME:
            typeOffset = OptimizedJSFunctionFrame::GetTypeOffset();
            prevOffset = OptimizedJSFunctionFrame::GetPrevOffset();
            break;
        case FrameType::LEAVE_FRAME:
            typeOffset = OptimizedLeaveFrame::GetTypeOffset();
            prevOffset = OptimizedLeaveFrame::GetPrevOffset();
            break;
        case FrameType::LEAVE_FRAME_WITH_ARGV:
            typeOffset = OptimizedWithArgvLeaveFrame::GetTypeOffset();
            prevOffset = OptimizedWithArgvLeaveFrame::GetPrevOffset();
            break;
        case FrameType::BUILTIN_CALL_LEAVE_FRAME:
            typeOffset = OptimizedBuiltinLeaveFrame::GetTypeOffset();
            prevOffset = OptimizedBuiltinLeaveFrame::GetPrevOffset();
            break;
        case FrameType::INTERPRETER_FRAME:
        case FrameType::INTERPRETER_FAST_NEW_FRAME:
            typeOffset = InterpretedFrame::GetTypeOffset();
            prevOffset = InterpretedFrame::GetPrevOffset();
            break;
        case FrameType::INTERPRETER_BUILTIN_FRAME:
            typeOffset = InterpretedBuiltinFrame::GetTypeOffset();
            prevOffset = InterpretedBuiltinFrame::GetPrevOffset();
            break;
        case FrameType::INTERPRETER_CONSTRUCTOR_FRAME:
        case FrameType::ASM_INTERPRETER_FRAME:
            typeOffset = AsmInterpretedFrame::GetTypeOffset();
            prevOffset = AsmInterpretedFrame::GetPrevOffset();
            break;
        case FrameType::BUILTIN_FRAME:
        case FrameType::BUILTIN_ENTRY_FRAME:
            typeOffset = BuiltinFrame::GetTypeOffset();
            prevOffset = BuiltinFrame::GetPrevOffset();
            break;
        case FrameType::BUILTIN_FRAME_WITH_ARGV:
        case FrameType::BUILTIN_FRAME_WITH_ARGV_STACK_OVER_FLOW_FRAME:
            typeOffset = BuiltinWithArgvFrame::GetTypeOffset();
            prevOffset = BuiltinWithArgvFrame::GetPrevOffset();
            break;
        case FrameType::INTERPRETER_ENTRY_FRAME:
            typeOffset = InterpretedEntryFrame::GetTypeOffset();
            prevOffset = InterpretedEntryFrame::GetPrevOffset();
            break;
        case FrameType::ASM_INTERPRETER_ENTRY_FRAME:
            typeOffset = AsmInterpretedEntryFrame::GetTypeOffset();
            prevOffset = AsmInterpretedEntryFrame::GetPrevOffset();
            break;
        case FrameType::ASM_INTERPRETER_BRIDGE_FRAME:
            typeOffset = AsmInterpretedBridgeFrame::GetTypeOffset();
            prevOffset = AsmInterpretedBridgeFrame::GetPrevOffset();
            break;
        case FrameType::FASTJIT_FUNCTION_FRAME:
        case FrameType::FASTJIT_FAST_CALL_FUNCTION_FRAME:
            typeOffset = FASTJITFunctionFrame::GetTypeOffset();
            prevOffset = FASTJITFunctionFrame::GetPrevOffset();
            break;
        case FrameType::STEED_FUNCTION_FRAME:
            typeOffset = SteedFunctionFrame::GetTypeOffset();
            prevOffset = SteedFunctionFrame::GetPrevOffset();
            break;
        default:
            return false;
    }
    return true;
}

bool ArkFrameCheck(uintptr_t frameType)
{
    return static_cast<FrameType>(frameType) == FrameType::OPTIMIZED_ENTRY_FRAME ||
           static_cast<FrameType>(frameType) == FrameType::ASM_INTERPRETER_ENTRY_FRAME;
}

bool IsJsFunctionFrame(uintptr_t frameType)
{
    return static_cast<FrameType>(frameType) == FrameType::ASM_INTERPRETER_FRAME ||
           static_cast<FrameType>(frameType) == FrameType::INTERPRETER_CONSTRUCTOR_FRAME ||
           static_cast<FrameType>(frameType) == FrameType::INTERPRETER_FRAME ||
           static_cast<FrameType>(frameType) == FrameType::INTERPRETER_FAST_NEW_FRAME;
}

bool IsNativeFunctionFrame(uintptr_t frameType)
{
    return static_cast<FrameType>(frameType) == FrameType::OPTIMIZED_FRAME ||
           static_cast<FrameType>(frameType) == FrameType::BASELINE_BUILTIN_FRAME ||
           static_cast<FrameType>(frameType) == FrameType::ASM_BRIDGE_FRAME ||
           static_cast<FrameType>(frameType) == FrameType::OPTIMIZED_JS_FUNCTION_UNFOLD_ARGV_FRAME ||
           static_cast<FrameType>(frameType) == FrameType::OPTIMIZED_JS_FUNCTION_ARGS_CONFIG_FRAME ||
           static_cast<FrameType>(frameType) == FrameType::OPTIMIZED_JS_FAST_CALL_FUNCTION_FRAME ||
           static_cast<FrameType>(frameType) == FrameType::OPTIMIZED_JS_FUNCTION_FRAME ||
           static_cast<FrameType>(frameType) == FrameType::LEAVE_FRAME ||
           static_cast<FrameType>(frameType) == FrameType::LEAVE_FRAME_WITH_ARGV ||
           static_cast<FrameType>(frameType) == FrameType::BUILTIN_CALL_LEAVE_FRAME ||
           static_cast<FrameType>(frameType) == FrameType::BUILTIN_FRAME ||
           static_cast<FrameType>(frameType) == FrameType::BUILTIN_ENTRY_FRAME ||
           static_cast<FrameType>(frameType) == FrameType::BUILTIN_FRAME_WITH_ARGV ||
           static_cast<FrameType>(frameType) == FrameType::BUILTIN_FRAME_WITH_ARGV_STACK_OVER_FLOW_FRAME ||
           static_cast<FrameType>(frameType) == FrameType::ASM_INTERPRETER_BRIDGE_FRAME;
}

bool IsAotFunctionFrame(uintptr_t frameType)
{
    return static_cast<FrameType>(frameType) == FrameType::OPTIMIZED_JS_FUNCTION_FRAME ||
           static_cast<FrameType>(frameType) == FrameType::OPTIMIZED_JS_FAST_CALL_FUNCTION_FRAME;
}

std::optional<MethodInfo> JSStackTrace::ReadMethodInfo(panda_file::MethodDataAccessor &mda)
{
    uintptr_t methodId = mda.GetMethodId().GetOffset();
    auto codeId = mda.GetCodeId();
    if (!codeId) {
        return std::nullopt;
    }
    panda_file::CodeDataAccessor cda(mda.GetPandaFile(), codeId.value());
    uint32_t codeSize = cda.GetCodeSize();
    uintptr_t codeBegin = reinterpret_cast<uintptr_t>(cda.GetInstructions());
    return std::make_optional<MethodInfo>(methodId, codeBegin, codeSize);
}

CVector<MethodInfo> JSStackTrace::ReadAllMethodInfos(std::shared_ptr<JSPandaFile> jsPandaFile)
{
    CVector<MethodInfo> result;
    if (jsPandaFile == nullptr) {
        LOG_ECMA(ERROR) << "Failed to read all methods info.";
        return result;
    }
    const panda_file::File *pf = jsPandaFile->GetPandaFile();
    Span<const uint32_t> classIndexes = jsPandaFile->GetClasses();
    for (const uint32_t index : classIndexes) {
        panda_file::File::EntityId classId(index);
        if (jsPandaFile->IsExternal(classId)) {
            continue;
        }
        panda_file::ClassDataAccessor cda(*pf, classId);
        cda.EnumerateMethods([&result, jsPandaFile](panda_file::MethodDataAccessor &mda) {
            auto info = JSStackTrace::ReadMethodInfo(mda);
            if (!info) {
                return;
            }
            result.push_back(info.value());
        });
    }

    std::sort(result.begin(), result.end());
    return result;
}

std::optional<CodeInfo> JSStackTrace::TranslateByteCodePc(uintptr_t realPc, const CVector<MethodInfo> &vec)
{
    if (vec.size() == 0) {
        LOG_ECMA(ERROR) << "Translate bytecode pc failed, vec is empty.";
        return std::nullopt;
    }
    int32_t left = 0;
    int32_t right = static_cast<int32_t>(vec.size()) - 1;
    for (; left <= right;) {
        int32_t mid = (left + right) / 2;
        bool isRight = realPc >= (vec[mid].codeBegin + vec[mid].codeSize);
        bool isLeft = realPc < vec[mid].codeBegin;
        // codeBegin <= realPc < codeBegin + codeSize
        if (!isRight && !isLeft) {
            return std::make_optional<CodeInfo>(realPc - vec[mid].codeBegin, vec[mid].methodId, vec[mid].codeSize);
        } else if (isRight) {
            left = mid + 1;
        } else {
            right = mid -1;
        }
    }
    LOG_ECMA(ERROR) << "Translate bytecode pc failed, pc: " << std::hex << realPc;
    return std::nullopt;
}

void SaveFuncName(EntityId entityId, const std::string &name)
{
    std::unique_lock<std::mutex> lock(nameMapMutex);
    size_t length = 256; // maximum stack length
    if (JsStackInfo::nameMap.size() > length) {
        auto it = JsStackInfo::nameMap.begin();
        JsStackInfo::nameMap.erase(it);
    }
    JsStackInfo::nameMap.emplace(entityId, name);
}

template<typename T>
bool ParseJsFrameInfo(JSPandaFile *jsPandaFile, DebugInfoExtractor *debugExtractor,
                      EntityId methodId, uintptr_t offset, T &jsFrame, SourceMap *sourceMap)
{
    if (jsPandaFile == nullptr) {
        LOG_ECMA(ERROR) << "Parse jsFrame info failed, jsPandaFile is nullptr.";
        return false;
    }
    std::string name = MethodLiteral::ParseFunctionName(jsPandaFile, methodId);
    name = name.empty() ? "anonymous" : name;
    std::string url = debugExtractor->GetSourceFile(methodId);

    // line number and column number
    int lineNumber = 0;
    int columnNumber = 0;
    auto callbackLineFunc = [&lineNumber](int32_t line) -> bool {
        lineNumber = line + 1;
        return true;
    };
    auto callbackColumnFunc = [&columnNumber](int32_t column) -> bool {
        columnNumber = column + 1;
        return true;
    };

    if (!debugExtractor->MatchLineAndRevisedOffset(callbackLineFunc, methodId, offset) ||
        !debugExtractor->MatchColumnWithOffset(callbackColumnFunc, methodId, offset)) {
        lineNumber = 0;
        columnNumber = 0;
    }

    std::string packageName;
    if (sourceMap != nullptr) {
        sourceMap->TranslateUrlPositionBySourceMap(url, lineNumber, columnNumber, packageName);
    }

    if (name.size() >= FUNCTIONNAME_MAX) {
        name.resize(FUNCTIONNAME_MAX - 1);
    }
    if (url.size() >= URL_MAX) {
        url.resize(URL_MAX - 1);
    }
    if (packageName.size() >= PACKAGENAME_MAX) {
        packageName.resize(PACKAGENAME_MAX - 1);
    }

    if (strcpy_s(jsFrame.functionName, FUNCTIONNAME_MAX, name.c_str()) != EOK ||
        strcpy_s(jsFrame.url, URL_MAX, url.c_str()) != EOK ||
        strcpy_s(jsFrame.packageName, PACKAGENAME_MAX, packageName.c_str()) != EOK) {
        LOG_ECMA(ERROR) << "jsFrame strcpy_s failed";
        return false;
    }
    jsFrame.line = lineNumber;
    jsFrame.column = columnNumber;
    return true;
}

#if defined(ENABLE_STATIC_BACKTRACE)
bool StepStaticArk(void *ctx, ReadMemFunc readMem, ark::tooling::ArkStepParam *arkStepParam)
{
    if (!ark::tooling::Backtrace::StepArkByNativeFrame(ctx, readMem, arkStepParam)) {
        return false;
    }
    return true;
}

bool SymbolizeStaticArk(uintptr_t pc, uintptr_t mapBase, uintptr_t loadOffset, uint8_t *abcData,
                        uint64_t abcSize, JsFunction *jsFunction, uintptr_t extractor)
{
    if (!ark::tooling::Backtrace::SymbolizeByNativeFrame(pc, mapBase, loadOffset, abcData, abcSize,
        reinterpret_cast<ark::tooling::Function *>(jsFunction), extractor)) {
        LOG_ECMA(ERROR) << "symbolizeStaticArkFunc failed";
        return false;
    }
    return true;
}

panda_file::PandaFileType JSSymbolExtractor::GetPandaFileType(const uint8_t *data)
{
    if (type_ != panda_file::PandaFileType::FILE_FORMAT_INVALID) {
        return type_;
    }

    auto header = reinterpret_cast<const panda_file::File::Header *>(data);
    if (header->version[panda_file::File::FILE_TYPE_OFFSET] == panda_file::File::FILE_TYPE_STATIC_FLAG ||
        header->version == panda_file::File::OLD_STATIC_VERSION) {
        type_ = panda_file::PandaFileType::FILE_STATIC;
    } else {
        type_ = panda_file::PandaFileType::FILE_DYNAMIC;
    }

    return type_;
}
#endif

bool ArkParseJsFrameInfo(uintptr_t byteCodePc, uintptr_t mapBase, uintptr_t loadOffset,
                         uint8_t *data, uint64_t dataSize, uintptr_t extractorptr, JsFunction *jsFunction)
{
    if (data == nullptr) {
        LOG_ECMA(ERROR) << "Parse JSframe info failed, buffer is nullptr.";
        return false;
    }
    loadOffset = loadOffset % PageSize();
    auto extractor = reinterpret_cast<JSSymbolExtractor*>(extractorptr);
    if (extractor == nullptr) {
        LOG_ECMA(ERROR) << "Parse JSframe info failed, extractor is nullptr.";
        return false;
    }

#if defined(ENABLE_STATIC_BACKTRACE)
    auto fileType = extractor->GetPandaFileType(data);
    if (fileType == panda_file::PandaFileType::FILE_STATIC) {
        auto staticExtractor = extractor->GetStaticSymbolExtractor();
        return SymbolizeStaticArk(byteCodePc, mapBase, loadOffset, data, dataSize, jsFunction, staticExtractor);
    }
#endif

    auto jsPandaFile = extractor->GetJSPandaFile(data, dataSize);
    if (jsPandaFile == nullptr) {
        LOG_ECMA(ERROR) << "Parse JSframe info failed, panda file is nullptr.";
        return false;
    }
    auto debugExtractor = extractor->GetDebugExtractor();
    const auto &methodInfos = extractor->GetMethodInfos();
    if (methodInfos.empty()) {
        LOG_ECMA(ERROR) << "Read all method info from JSPandaFile failed, methodInfos is empty.";
        return false;
    }
    uintptr_t realOffset = byteCodePc - mapBase - loadOffset;
    uintptr_t pfBasePtr = reinterpret_cast<uintptr_t>(jsPandaFile->GetBase());
    auto codeInfo = JSStackTrace::TranslateByteCodePc(realOffset + pfBasePtr, methodInfos);
    if (!codeInfo) {
        LOG_ECMA(ERROR) << std::hex << "Failed to get methodId, pc: " << byteCodePc;
        return false;
    }
    auto offset = codeInfo->offset;
    if (!ParseJsFrameInfo(jsPandaFile, debugExtractor, EntityId(codeInfo->methodId), offset,
                          *jsFunction, extractor->GetSourceMap())) {
        return false;
    }
    SaveFuncName(EntityId(codeInfo->methodId), jsFunction->functionName);
    jsFunction->codeBegin = byteCodePc - offset;
    jsFunction->codeSize = codeInfo->codeSize;
    return true;
}

#if ECMASCRIPT_ENABLE_ARK_STEED
// ============================================================================
// Out-of-process STEED frame pc resolution (G2).
//
// PC source model (mirrors the in-process FrameIterator::Advance semantics):
//   - a new unwind sequence keeps the fault PC passed by the unwinder as the
//     first-frame candidate (never cleared before use);
//   - each visited frame with a reliable native return-address slot feeds the
//     next STEED caller's candidate; interpreter/entry-style frames invalidate
//     it (Advance() sets optimizedReturnAddr_ = 0 for those);
//   - a candidate outside [funcAddr, funcAddr + instrsiz) degrades that frame
//     to function-first-line (bcOffset = 0) and never misreports (incl.
//     lazy-deopt trampoline addresses).
// The continuity state is thread-local and keyed by ctx, so parallel
// faultloggerd workers and interleaved sequences never share stale
// candidates; every termination path clears it idempotently.
// ============================================================================

// Hard cap for out-of-process arksteed metadata reads: the crash process memory
// is untrusted, so a corrupted size field must never drive huge allocations.
constexpr uint32_t MAX_OFFLINE_ARKSTEED_METADATA_SIZE = 16 * 1024 * 1024;  // 16 MiB

namespace {
struct UnwindPcState {
    void *ctx {nullptr};
    uintptr_t candidatePc {0};  // return addr of the last visited frame
    bool candidateValid {false};
    uintptr_t firstPc {0};      // original fault PC of the sequence
    bool seenSteedFrame {false};
};

thread_local UnwindPcState g_unwindPcState;
}  // namespace

// Idempotent; safe to call on every termination path (read failure, unsupported
// frame type, unwind end, jit ELF dump end).
static void ResetUnwindPcState()
{
    g_unwindPcState.ctx = nullptr;
    g_unwindPcState.candidatePc = 0;
    g_unwindPcState.candidateValid = false;
    g_unwindPcState.firstPc = 0;
    g_unwindPcState.seenSteedFrame = false;
}

// Initialize/verify the per-sequence continuity state. The unwinder driver
// passes a constant frameIndex, so a new sequence is identified by a changed
// ctx only; it re-initializes and preserves the unwinder's input fault PC as
// the first-frame candidate.
static void UpdateUnwindPcSequence(void *ctx, uintptr_t inputPc)
{
    if (g_unwindPcState.ctx != ctx) {
        g_unwindPcState.ctx = ctx;
        g_unwindPcState.candidatePc = inputPc;
        g_unwindPcState.candidateValid = inputPc != 0;
        g_unwindPcState.firstPc = inputPc;
        g_unwindPcState.seenSteedFrame = false;
    }
}

// Capture the current frame's native return address as the candidate pc for
// the next STEED caller (frame N's position is its callee M's return address,
// exactly like in-process Advance()). Interpreter/entry-style frames have no
// reliable native return-address slot and invalidate the candidate instead.
// typeSlot points at the frame's type slot; no-op when no sequence is active.
static void CaptureFrameReturnAddr(void *ctx, ReadMemFunc readMem, uintptr_t typeSlot,
                                   uintptr_t frameType)
{
    if (g_unwindPcState.ctx != ctx) {
        return;
    }
    FrameType type = static_cast<FrameType>(frameType);
    uintptr_t returnAddr = 0;
    bool hasSlot = true;
    switch (type) {
        // STEED/FASTJIT: the return address lives at fp + sizeof(uintptr_t),
        // where fp is the prev-frame slot address.
        case FrameType::STEED_FUNCTION_FRAME:
        case FrameType::FASTJIT_FUNCTION_FRAME:
        case FrameType::FASTJIT_FAST_CALL_FUNCTION_FRAME: {
            uintptr_t slot = typeSlot - SteedFunctionFrame::GetTypeOffset() +
                             SteedFunctionFrame::GetPrevOffset() + sizeof(uintptr_t);
            hasSlot = readMem(ctx, slot, &returnAddr);
            break;
        }
        // frames whose own return-address slot sits at frameBase + offset
        case FrameType::OPTIMIZED_JS_FAST_CALL_FUNCTION_FRAME:
        case FrameType::OPTIMIZED_JS_FUNCTION_FRAME:
        case FrameType::OPTIMIZED_JS_FUNCTION_ARGS_CONFIG_FRAME: {
            uintptr_t slot = typeSlot - OptimizedJSFunctionFrame::GetTypeOffset() +
                             OptimizedJSFunctionFrame::GetReturnAddrOffset();
            hasSlot = readMem(ctx, slot, &returnAddr);
            break;
        }
        case FrameType::BUILTIN_FRAME:
        case FrameType::BUILTIN_ENTRY_FRAME: {
            uintptr_t slot = typeSlot - BuiltinFrame::GetTypeOffset() +
                             BuiltinFrame::GetReturnAddrOffset();
            hasSlot = readMem(ctx, slot, &returnAddr);
            break;
        }
        case FrameType::BUILTIN_FRAME_WITH_ARGV:
        case FrameType::BUILTIN_FRAME_WITH_ARGV_STACK_OVER_FLOW_FRAME: {
            uintptr_t slot = typeSlot - BuiltinWithArgvFrame::GetTypeOffset() +
                            BuiltinWithArgvFrame::GetReturnAddrOffset();
            hasSlot = readMem(ctx, slot, &returnAddr);
            break;
        }
        case FrameType::BASELINE_BUILTIN_FRAME: {
            uintptr_t slot = typeSlot - BaselineBuiltinFrame::GetTypeOffset() +
                             BaselineBuiltinFrame::GetReturnAddrOffset();
            hasSlot = readMem(ctx, slot, &returnAddr);
            break;
        }
        case FrameType::ASM_BRIDGE_FRAME: {
            uintptr_t slot = typeSlot - AsmBridgeFrame::GetTypeOffset() +
                             AsmBridgeFrame::GetReturnAddrOffset();
            hasSlot = readMem(ctx, slot, &returnAddr);
            break;
        }
        case FrameType::LEAVE_FRAME: {
            uintptr_t slot = typeSlot - OptimizedLeaveFrame::GetTypeOffset() +
                             OptimizedLeaveFrame::GetReturnAddrOffset();
            hasSlot = readMem(ctx, slot, &returnAddr);
            break;
        }
        case FrameType::LEAVE_FRAME_WITH_ARGV: {
            uintptr_t slot = typeSlot - OptimizedWithArgvLeaveFrame::GetTypeOffset() +
                             OptimizedWithArgvLeaveFrame::GetReturnAddrOffset();
            hasSlot = readMem(ctx, slot, &returnAddr);
            break;
        }
        case FrameType::BUILTIN_CALL_LEAVE_FRAME: {
            uintptr_t slot = typeSlot - OptimizedBuiltinLeaveFrame::GetTypeOffset() +
                             OptimizedBuiltinLeaveFrame::GetReturnAddrOffset();
            hasSlot = readMem(ctx, slot, &returnAddr);
            break;
        }
        case FrameType::OPTIMIZED_FRAME: {
            uintptr_t slot = typeSlot - OptimizedFrame::GetTypeOffset() +
                             OptimizedFrame::GetReturnAddrOffset();
            hasSlot = readMem(ctx, slot, &returnAddr);
            break;
        }
        case FrameType::ASM_INTERPRETER_BRIDGE_FRAME: {
            uintptr_t slot = typeSlot - AsmInterpretedBridgeFrame::GetTypeOffset() +
                             AsmInterpretedBridgeFrame::GetReturnAddrOffset(false);
            hasSlot = readMem(ctx, slot, &returnAddr);
            break;
        }
        case FrameType::OPTIMIZED_JS_FUNCTION_UNFOLD_ARGV_FRAME: {
            uintptr_t slot = typeSlot - OptimizedJSFunctionUnfoldArgVFrame::GetTypeOffset() +
                             OptimizedJSFunctionUnfoldArgVFrame::GetReturnAddrOffset();
            hasSlot = readMem(ctx, slot, &returnAddr);
            break;
        }
        // interpreter-style frames: no reliable native return address
        case FrameType::ASM_INTERPRETER_FRAME:
        case FrameType::INTERPRETER_CONSTRUCTOR_FRAME:
        case FrameType::INTERPRETER_FRAME:
        case FrameType::INTERPRETER_FAST_NEW_FRAME:
        default:
            hasSlot = false;
            break;
    }
    if (hasSlot && returnAddr != 0) {
        g_unwindPcState.candidatePc = returnAddr;
        g_unwindPcState.candidateValid = true;
 	} else {
        g_unwindPcState.candidatePc = 0;
        g_unwindPcState.candidateValid = false;
    }
}

struct ReadSteedMachineCodeParam {
    void *ctx;
    ReadMemFunc readMem;
    std::vector<uint8_t>& container;
    uintptr_t stackmapSize;
    uintptr_t payloadSize;
    uintptr_t candAddr;
};

// Safely copy the ASTB container from one of the two possible layouts
// (JitFort NonText region after the MachineCode object / right after the
// text). The crash process memory is untrusted: bound the size, validate
// the fixed header and the section directory, check every read, drop the
// whole candidate on any failure.
static bool tryReadSteedTableContainer(const ReadSteedMachineCodeParam& param)
{
    auto ctx = param.ctx;
    auto readMem = param.readMem;
    auto stackmapSize = param.stackmapSize;
    auto payloadSize = param.payloadSize;
    auto candAddr = param.candAddr;
    auto& container = param.container;

    if (stackmapSize == 0 || stackmapSize > payloadSize ||
        stackmapSize > MAX_OFFLINE_ARKSTEED_METADATA_SIZE ||
        candAddr > std::numeric_limits<uintptr_t>::max() - stackmapSize) {
        return false;
    }
    // fixed header first (word-wise, tail-trimmed)
    uint8_t headerBytes[arksteed::ArkSteedTableContainerHeader::SIZE] = {0};
    for (size_t off = 0; off < sizeof(headerBytes); off += sizeof(uintptr_t)) {
        uintptr_t word = 0;
        if (!readMem(ctx, candAddr + off, &word)) {
            return false;
        }
        size_t n = std::min(sizeof(uintptr_t), sizeof(headerBytes) - off);
        if (memcpy_s(headerBytes + off, n, &word, n) != EOK) {
            return false;
        }
    }
    arksteed::ArkSteedTableContainerHeader header;
    if (memcpy_s(&header, sizeof(header), headerBytes, sizeof(headerBytes)) != EOK) {
        return false;
    }
    if (header.magic != arksteed::ArkSteedTableContainerHeader::MAGIC ||
        header.version != arksteed::ArkSteedTableContainerHeader::VERSION) {
        return false;
    }
    // section directory must stay inside the declared container size
    auto sectionOk = [&](uint32_t off, uint32_t size) {
        return off >= arksteed::ArkSteedTableContainerHeader::SIZE &&
               static_cast<uint64_t>(off) + size <= stackmapSize;
    };
    if (!sectionOk(header.safepointOffset, header.safepointSize) ||
        (header.positionSize != 0 && !sectionOk(header.positionOffset, header.positionSize))) {
        return false;
    }
    // machine-word chunked copy; the tail keeps only the valid bytes and
    // any failed read drops the whole candidate (no partial results)
    container.assign(stackmapSize, 0);
    size_t off = 0;
    while (off < stackmapSize) {
        uintptr_t word = 0;
        if (!readMem(ctx, candAddr + off, &word)) {
            container.clear();
            return false;
        }
        size_t n = std::min(sizeof(uintptr_t), stackmapSize - off);
        if (memcpy_s(container.data() + off, n, &word, n) != EOK) {
            container.clear();
            return false;
        }
        off += sizeof(uintptr_t);
    }
    return true;
}

// Resolve a STEED frame's bytecode-space pc: candidate pcs -> machine-code
// offset (range-checked) -> ASTB container (safely copied) -> position table
// first, safepoint table fallback, 0 last -> bytecodeArray + bcOffset.
// Returns 0 on any unreadable input (frame identity is kept by the caller).
static uintptr_t GetSteedBytecodePc(void *ctx, ReadMemFunc readMem, uintptr_t typeSlot,
                                    uintptr_t preferredPc, uintptr_t fallbackPc)
{
    // frame -> JSFunction -> MachineCode
    uintptr_t function = 0;
    if (!readMem(ctx, typeSlot - SteedFunctionFrame::GetTypeOffset() +
         SteedFunctionFrame::GetFunctionOffset(), &function) || function == 0) {
        return 0;
 	}

    uintptr_t method = 0;
    if (!readMem(ctx, function + JSFunction::METHOD_OFFSET, &method) || method == 0) {
        return 0;
    }
    uintptr_t bytecodeArray = 0;
    if (!readMem(ctx, method + Method::NATIVE_POINTER_OR_BYTECODE_ARRAY_OFFSET,
                 &bytecodeArray) || bytecodeArray == 0) {
        return 0;
    }

    uintptr_t machineCode = 0;
    if (!readMem(ctx, function + JSFunction::MACHINECODE_OFFSET, &machineCode) || machineCode == 0) {
        return bytecodeArray; // use method first line for fallback
    }
    // MachineCode fixed fields. 32-bit fields are read as words and normalized,
    // so garbage in the neighbouring high half never inflates them.
    uintptr_t funcAddr = 0;
    uintptr_t instrsizWord = 0;
    uintptr_t stackmapSizeWord = 0;
    uintptr_t payloadSizeWord = 0;
    if (!readMem(ctx, machineCode + MachineCode::FUNCADDR_OFFSET, &funcAddr) ||
        !readMem(ctx, machineCode + MachineCode::INSTRSIZ_OFFSET, &instrsizWord) ||
        !readMem(ctx, machineCode + MachineCode::STACKMAP_OR_OFFSETTABLE_SIZE_OFFSET, &stackmapSizeWord) ||
        !readMem(ctx, machineCode + MachineCode::PAYLOADSIZE_OFFSET, &payloadSizeWord)) {
        return bytecodeArray;
    }
    uint32_t instrsiz = static_cast<uint32_t>(instrsizWord);
    uint32_t stackmapSize = static_cast<uint32_t>(stackmapSizeWord);
    uint32_t payloadSize = static_cast<uint32_t>(payloadSizeWord);
    if (funcAddr == 0 || instrsiz == 0) {
        return bytecodeArray;
    }
    // Candidate range check: compare first, subtract after. An out-of-range
    // candidate (fault pc in other code, lazy-deopt trampoline, invalid) only
    // degrades this frame to function-first-line.
    uintptr_t codeOffset = 0;
    if (preferredPc >= funcAddr && preferredPc - funcAddr < instrsiz) {
        codeOffset = preferredPc - funcAddr;
    } else if (fallbackPc >= funcAddr && fallbackPc - funcAddr < instrsiz) {
        codeOffset = fallbackPc - funcAddr;
    }

    std::vector<uint8_t> container;
    ReadSteedMachineCodeParam readJitFortEnable{ctx, readMem, container, stackmapSize,
                                                payloadSize, machineCode + MachineCode::SIZE};
    ReadSteedMachineCodeParam readFallback{ctx, readMem, container, stackmapSize,
                                           payloadSize, funcAddr + instrsiz};

    bool copied = tryReadSteedTableContainer(readJitFortEnable) ||
                  tryReadSteedTableContainer(readFallback);

    // position table first -> safepoint table fallback -> 0 (bcOffset == 0 is a
    // legal value: the function's first bytecode). codeOffset == 0 means no
    // candidate was in range: degrade instead of consulting the tables, or
    // codeOffset-1 would wrap around to UINT32_MAX and floor-hit the last
    // position entry (a misreport).
    uint32_t bcOffset = 0;
    if (copied && codeOffset != 0) {
        size_t posSize = 0;
        size_t sfpSize = 0;
        const uint8_t *posTable =
            arksteed::GetArkSteedPositionTable(container.data(), container.size(), &posSize);
        auto bc = (posTable != nullptr) ?
            arksteed::LookupBcOffsetFromPositionTable(posTable, posSize,
                                                      static_cast<uint32_t>(codeOffset-1))
            : std::optional<uint32_t>(std::nullopt);
        if (!bc) {
            const uint8_t *sfpTable =
                arksteed::GetArkSteedSafepointTable(container.data(), container.size(), &sfpSize);
            if (sfpTable != nullptr) {
                bc = arksteed::ExtractBcOffsetFromSafepointTable(sfpTable, sfpSize,
                                                                 static_cast<uint32_t>(codeOffset));
            }
        }
        bcOffset = bc.value_or(0);
    }

    return bytecodeArray + bcOffset;
}
#endif  // ECMASCRIPT_ENABLE_ARK_STEED

__attribute__((no_sanitize("hwaddress"))) uintptr_t GetBytecodeOffset(void *ctx,
                                                                      ReadMemFunc readMem,
                                                                      uintptr_t frameType,
                                                                      uintptr_t currentPtr)
{
    // currentPtr points to the frametype.
    uintptr_t bytecodePc = 0;
    FrameType type = static_cast<FrameType>(frameType);
    switch (type) {
        // return bytecode pc
        case FrameType::ASM_INTERPRETER_FRAME:
        case FrameType::INTERPRETER_CONSTRUCTOR_FRAME: {
            currentPtr -= AsmInterpretedFrame::GetTypeOffset();
            currentPtr += AsmInterpretedFrame::GetPcOffset(false);
            readMem(ctx, currentPtr, &bytecodePc);
            return bytecodePc;
        }
        case FrameType::INTERPRETER_FRAME:
        case FrameType::INTERPRETER_FAST_NEW_FRAME: {
            currentPtr -= InterpretedFrame::GetTypeOffset();
            currentPtr += InterpretedFrame::GetPcOffset(false);
            readMem(ctx, currentPtr, &bytecodePc);
            return bytecodePc;
        }
        case FrameType::FASTJIT_FUNCTION_FRAME:
        case FrameType::FASTJIT_FAST_CALL_FUNCTION_FRAME: {
            currentPtr -= FASTJITFunctionFrame::GetTypeOffset();
            readMem(ctx, currentPtr, &bytecodePc);
            return bytecodePc;
        }
#if ECMASCRIPT_ENABLE_ARK_STEED
        case FrameType::STEED_FUNCTION_FRAME: {
            // Consume the unwind continuity state: the first STEED frame of a
            // sequence prefers the original fault PC, later ones the previous
            // callee's return address; both are range-checked inside and only
            // ever degrade (never misreport). Without an active sequence for
            // this ctx (e.g. driven outside step_ark_with_record_jit) the
            // frame degrades to its first bytecode.
            uintptr_t preferredPc = 0;
            uintptr_t fallbackPc = 0;
            if (g_unwindPcState.ctx == ctx) {
                if (!g_unwindPcState.seenSteedFrame) {
                    preferredPc = g_unwindPcState.firstPc;
                    fallbackPc = g_unwindPcState.candidateValid ? g_unwindPcState.candidatePc : 0;
                } else {
                    preferredPc = g_unwindPcState.candidateValid ? g_unwindPcState.candidatePc : 0;
                }
            }
            g_unwindPcState.seenSteedFrame = true;
            return GetSteedBytecodePc(ctx, readMem, currentPtr, preferredPc, fallbackPc);
        }
#endif
        // return returnaddr
        case FrameType::OPTIMIZED_JS_FAST_CALL_FUNCTION_FRAME:
        case FrameType::OPTIMIZED_JS_FUNCTION_FRAME:
        case FrameType::OPTIMIZED_JS_FUNCTION_ARGS_CONFIG_FRAME: {
            currentPtr -= OptimizedJSFunctionFrame::GetTypeOffset();
            currentPtr += OptimizedJSFunctionFrame::GetReturnAddrOffset();
            readMem(ctx, currentPtr, &bytecodePc);
            return bytecodePc;
        }
        case FrameType::BUILTIN_FRAME:
        case FrameType::BUILTIN_ENTRY_FRAME: {
            currentPtr -= BuiltinFrame::GetTypeOffset();
            currentPtr += BuiltinFrame::GetReturnAddrOffset();
            readMem(ctx, currentPtr, &bytecodePc);
            return bytecodePc;
        }
        case FrameType::BUILTIN_FRAME_WITH_ARGV:
        case FrameType::BUILTIN_FRAME_WITH_ARGV_STACK_OVER_FLOW_FRAME: {
            currentPtr -= BuiltinWithArgvFrame::GetTypeOffset();
            currentPtr += BuiltinWithArgvFrame::GetReturnAddrOffset();
            readMem(ctx, currentPtr, &bytecodePc);
            return bytecodePc;
        }
        case FrameType::BASELINE_BUILTIN_FRAME: {
            currentPtr -= BaselineBuiltinFrame::GetTypeOffset();
            currentPtr += BaselineBuiltinFrame::GetReturnAddrOffset();
            readMem(ctx, currentPtr, &bytecodePc);
            return bytecodePc;
        }
        case FrameType::ASM_BRIDGE_FRAME: {
            currentPtr -= AsmBridgeFrame::GetTypeOffset();
            currentPtr += AsmBridgeFrame::GetReturnAddrOffset();
            readMem(ctx, currentPtr, &bytecodePc);
            return bytecodePc;
        }
        case FrameType::LEAVE_FRAME: {
            currentPtr -= OptimizedLeaveFrame::GetTypeOffset();
            currentPtr += OptimizedLeaveFrame::GetReturnAddrOffset();
            readMem(ctx, currentPtr, &bytecodePc);
            return bytecodePc;
        }
        case FrameType::LEAVE_FRAME_WITH_ARGV: {
            currentPtr -= OptimizedWithArgvLeaveFrame::GetTypeOffset();
            currentPtr += OptimizedWithArgvLeaveFrame::GetReturnAddrOffset();
            readMem(ctx, currentPtr, &bytecodePc);
            return bytecodePc;
        }
        case FrameType::BUILTIN_CALL_LEAVE_FRAME: {
            currentPtr -= OptimizedBuiltinLeaveFrame::GetTypeOffset();
            currentPtr += OptimizedBuiltinLeaveFrame::GetReturnAddrOffset();
            readMem(ctx, currentPtr, &bytecodePc);
            return bytecodePc;
        }
        case FrameType::OPTIMIZED_FRAME: {
            currentPtr -= OptimizedFrame::GetTypeOffset();
            currentPtr += OptimizedFrame::GetReturnAddrOffset();
            readMem(ctx, currentPtr, &bytecodePc);
            return bytecodePc;
        }
        case FrameType::ASM_INTERPRETER_BRIDGE_FRAME: {
            currentPtr -= AsmInterpretedBridgeFrame::GetTypeOffset();
            currentPtr += AsmInterpretedBridgeFrame::GetReturnAddrOffset(false);
            readMem(ctx, currentPtr, &bytecodePc);
            return bytecodePc;
        }
        case FrameType::OPTIMIZED_JS_FUNCTION_UNFOLD_ARGV_FRAME: {
            currentPtr -= OptimizedJSFunctionUnfoldArgVFrame::GetTypeOffset();
            currentPtr += OptimizedJSFunctionUnfoldArgVFrame::GetReturnAddrOffset();
            readMem(ctx, currentPtr, &bytecodePc);
            return bytecodePc;
        }
        default: {
            break;
        }
    }
    return 0;
}

uintptr_t ArkGetFunction(void *ctx, ReadMemFunc readMem, uintptr_t frameType, uintptr_t currentPtr)
{
    // only for jit frame
    uintptr_t funcAddr = currentPtr;
    switch (static_cast<FrameType>(frameType)) {
        case FrameType::FASTJIT_FUNCTION_FRAME:
        case FrameType::FASTJIT_FAST_CALL_FUNCTION_FRAME: {
            funcAddr -= FASTJITFunctionFrame::GetTypeOffset();
            funcAddr += FASTJITFunctionFrame::GetFunctionOffset();
            break;
        }
        case FrameType::STEED_FUNCTION_FRAME: {
            funcAddr -= SteedFunctionFrame::GetTypeOffset();
            funcAddr += SteedFunctionFrame::GetFunctionOffset();
            break;
        }
        default: {
            LOG_ECMA(DEBUG) << "Unsupported frameType for ArkGetFunction: " << frameType;
            return 0;
        }
    }
    uintptr_t function = 0;
    if (!readMem(ctx, funcAddr, &function)) {
        return 0;
    }
    return function;
}

__attribute__((no_sanitize("hwaddress"))) bool ArkGetNextFrame(void *ctx,
                                                               ReadMemFunc readMem,
                                                               uintptr_t &currentPtr,
                                                               uintptr_t &frameType,
                                                               uintptr_t &pc)
{
    currentPtr -= sizeof(FrameType);
    if (!readMem(ctx, currentPtr, &frameType)) {
        return false;
    }
    FrameIterator::TryRemoveLazyDeoptFlag(frameType);
    if (ArkFrameCheck(frameType)) {
        return true;
    }
    bool ret = false;
    if (IsJsFunctionFrame(frameType) || IsNativeFunctionFrame(frameType)) {
        pc = GetBytecodeOffset(ctx, readMem, frameType, currentPtr);
        ret = true;
    }

    uintptr_t typeOffset = 0;
    uintptr_t prevOffset = 0;
    if (!GetTypeOffsetAndPrevOffsetFromFrameType(frameType, typeOffset, prevOffset)) {
        return false;
    }
    currentPtr -= typeOffset;
    currentPtr += prevOffset;
    if (!readMem(ctx, currentPtr, &currentPtr)) {
        return false;
    }

    if (ret) {
        return true;
    }
    return ArkGetNextFrame(ctx, readMem, currentPtr, frameType, pc);
}

bool ArkGetMethodIdWithJit(ArkUnwindParam *arkUnwindParam, uintptr_t frameType, uintptr_t currentPtr)
{
    // only for jit frame
    uintptr_t function = ArkGetFunction(arkUnwindParam->ctx, arkUnwindParam->readMem, frameType, currentPtr);
    if (!function) {
        LOG_ECMA(DEBUG) << "Failed to get function";
        return false;
    }
    // The record key must be the abc methodId (EntityId) carried in the Method's
    // literalInfo behind the JSFunction: the caller's slot holds no usable id
    // (faultloggerd keeps it 0), and ArkWriteJitCode joins nameMap by this same
    // EntityId (ArkParseJsFrameInfo saves names under it). Unreadable method
    // metadata only skips the code recording, never the frame itself.
    uintptr_t method = 0;
    uintptr_t literalInfo = 0;
    uintptr_t methodId = 0;
    if (arkUnwindParam->readMem(arkUnwindParam->ctx, function + JSFunction::METHOD_OFFSET, &method) &&
        method != 0 &&
        arkUnwindParam->readMem(arkUnwindParam->ctx, method + Method::LITERAL_INFO_OFFSET, &literalInfo)) {
        methodId = MethodLiteral::GetMethodId(literalInfo).GetOffset();
    }
    if (methodId == 0) {
        LOG_ECMA(DEBUG) << std::hex << "Failed to get methodId, function: " << function;
        return true;
    }
    *arkUnwindParam->methodId = methodId;
    uintptr_t machineCode = 0;
    uintptr_t functionAddr = function + JSFunction::MACHINECODE_OFFSET;
    arkUnwindParam->readMem(arkUnwindParam->ctx, functionAddr, &machineCode);
    uintptr_t size = 0;
    uintptr_t funcAddr = 0;
    if (machineCode) {
        arkUnwindParam->readMem(arkUnwindParam->ctx, machineCode + MachineCode::INSTRSIZ_OFFSET, &size);
        arkUnwindParam->readMem(arkUnwindParam->ctx, machineCode + MachineCode::FUNCADDR_OFFSET, &funcAddr);
    }
    if (size && funcAddr) {
        // take the lower four bytes
        size &= 0xFFFFFFFF;
        std::vector<uint8> codeVec;
        for (size_t l = 0; l < size; l++) {
            uintptr_t tmp = 0;
            arkUnwindParam->readMem(arkUnwindParam->ctx, funcAddr + l, &tmp);
            codeVec.push_back(tmp);
        }
        arkUnwindParam->jitCache.push_back(methodId);
        JsStackInfo::machineCodeMap[EntityId(methodId)] = codeVec;
    }
    return true;
}

// 跨进程回栈入口
bool ArkGetNextFrameWithJit(ArkUnwindParam *arkUnwindParam, uintptr_t &currentPtr, uintptr_t &frameType)
{
    currentPtr -= sizeof(FrameType);
    if (!arkUnwindParam->readMem(arkUnwindParam->ctx, currentPtr, &frameType)) {
#if ECMASCRIPT_ENABLE_ARK_STEED
        ResetUnwindPcState();
#endif
        return false;
    }
    FrameIterator::TryRemoveLazyDeoptFlag(frameType);
    if (ArkFrameCheck(frameType)) {
#if ECMASCRIPT_ENABLE_ARK_STEED
        // entry-style boundary frame: no reliable native return address, so it
        // invalidates the candidate for the next STEED caller
        CaptureFrameReturnAddr(arkUnwindParam->ctx, arkUnwindParam->readMem, currentPtr, frameType);
#endif
        return true;
    }
    bool ret = false;
    if (IsJsFunctionFrame(frameType) ||
        IsNativeFunctionFrame(frameType)) {
        *arkUnwindParam->pc = GetBytecodeOffset(arkUnwindParam->ctx, arkUnwindParam->readMem, frameType, currentPtr);
        ret = true;
    } else if (IsFastJitFunctionFrame(frameType) || IsSteedFunctionFrame(frameType)) {
        *arkUnwindParam->pc = GetBytecodeOffset(arkUnwindParam->ctx, arkUnwindParam->readMem, frameType, currentPtr);
        ret = ArkGetMethodIdWithJit(arkUnwindParam, frameType, currentPtr);
    }
#if ECMASCRIPT_ENABLE_ARK_STEED
    // the current frame's pc has been resolved above, so its own return address
    // can now feed the next STEED caller's candidate (never before consumption)
    CaptureFrameReturnAddr(arkUnwindParam->ctx, arkUnwindParam->readMem, currentPtr, frameType);
#endif

    uintptr_t typeOffset = 0;
    uintptr_t prevOffset = 0;
    if (!GetTypeOffsetAndPrevOffsetFromFrameType(frameType, typeOffset, prevOffset)) {
#if ECMASCRIPT_ENABLE_ARK_STEED
        ResetUnwindPcState();
#endif
        return false;
    }
    currentPtr -= typeOffset;
    currentPtr += prevOffset;
    if (!arkUnwindParam->readMem(arkUnwindParam->ctx, currentPtr, &currentPtr)) {
#if ECMASCRIPT_ENABLE_ARK_STEED
        ResetUnwindPcState();
#endif
        return false;
    }

    if (ret) {
        return true;
    }
    return ArkGetNextFrameWithJit(arkUnwindParam, currentPtr, frameType);
}

bool ArkWriteJitCode([[maybe_unused]] void *ctx, [[maybe_unused]] ReadMemFunc readMem,
                     int fd, const uintptr_t *const jitCodeArray, const size_t jitSize)
{
#ifndef EXCLUDE_JIT
    JsJitDumpElf jitDumpElf;
    jitDumpElf.Init();
    std::set<uintptr_t> memos;
    int64 idx = 0;
    size_t offset = 0;
    for (size_t i = 0; i < jitSize; i++) {
        uintptr_t methodId = jitCodeArray[i];
        auto res = memos.insert(methodId);
        if (res.second) {
            std::vector<uint8> codeVec = JsStackInfo::machineCodeMap[EntityId(methodId)];
            std::string name = JsStackInfo::nameMap[EntityId(methodId)];
            size_t len = codeVec.size();
            jitDumpElf.AppendData(codeVec);
            jitDumpElf.AppendSymbolToSymTab(idx++, offset, len, name);
            offset += len;
        }
    }
    jitDumpElf.WriteJitElfFile(fd);
    JsStackInfo::nameMap.clear();
    JsStackInfo::machineCodeMap.clear();
#if ECMASCRIPT_ENABLE_ARK_STEED
    // unwind sequence finished: clear the per-thread pc continuity state
    ResetUnwindPcState();
#endif
    return true;
#else
    return false;
#endif
}

bool StepArkWithRecordJit(ArkUnwindParam *arkUnwindParam)
{
    constexpr size_t FP_SIZE = sizeof(uintptr_t);
    uintptr_t currentPtr = *arkUnwindParam->fp;
    if (currentPtr == 0) {
        LOG_ECMA(ERROR) << "fp is nullptr in StepArkWithRecordJit()!";
#if ECMASCRIPT_ENABLE_ARK_STEED
        // unwind end: clear the per-thread pc continuity state
        ResetUnwindPcState();
#endif
        return false;
    }

#if ECMASCRIPT_ENABLE_ARK_STEED
    // new-sequence init / continuity check: keeps the unwinder's input fault PC
    // as the first-frame candidate (a changed ctx re-initializes the state, so
    // stale candidates never leak across sequences)
    UpdateUnwindPcSequence(arkUnwindParam->ctx, *arkUnwindParam->pc);
#endif

#if defined(ENABLE_STATIC_BACKTRACE)
    if (*arkUnwindParam->frameType == StepFrameType::STATIC_JS_FRAME) {
        ark::tooling::ArkStepParam staticParam;
        staticParam.fp = arkUnwindParam->fp;
        staticParam.sp = arkUnwindParam->sp;
        staticParam.pc = arkUnwindParam->pc;
        staticParam.isArkFrame = arkUnwindParam->isJsFrame;
        staticParam.frameType = reinterpret_cast<ark::tooling::StepFrameType*>(arkUnwindParam->frameType);
        staticParam.frameIndex = arkUnwindParam->frameIndex;
        return StepStaticArk(arkUnwindParam->ctx, arkUnwindParam->readMem, &staticParam);
    }
#endif

    uintptr_t frameType = 0;
    if (ArkGetNextFrameWithJit(arkUnwindParam, currentPtr, frameType)) {
        if (ArkFrameCheck(frameType)) {
            currentPtr += sizeof(FrameType);
            *arkUnwindParam->sp = currentPtr;
            bool ret = arkUnwindParam->readMem(arkUnwindParam->ctx, currentPtr, arkUnwindParam->fp);
            currentPtr += FP_SIZE;
            ret &= arkUnwindParam->readMem(arkUnwindParam->ctx, currentPtr, arkUnwindParam->pc);
            *arkUnwindParam->isJsFrame = false;
            *arkUnwindParam->frameType = StepFrameType::NATIVE_FRAME;
#if ECMASCRIPT_ENABLE_ARK_STEED
            if (!ret) {
                // read failure at the entry-frame boundary: sequence terminated
                ResetUnwindPcState();
            }
#endif
            return ret;
        } else {
            *arkUnwindParam->fp = currentPtr;
            *arkUnwindParam->sp = currentPtr;
            // js && jit -> true, native -> false
            *arkUnwindParam->isJsFrame = IsJsFunctionFrame(frameType) ||
                IsFastJitFunctionFrame(frameType) ||
                IsSteedFunctionFrame(frameType);
            if (*arkUnwindParam->isJsFrame) {
                *arkUnwindParam->frameType = StepFrameType::JS_FRAME;
            } else {
                *arkUnwindParam->frameType = StepFrameType::NATIVE_FRAME;
            }
        }
    } else {
        LOG_ECMA(ERROR) << "ArkGetNextFrameWithJit failed, currentPtr: " << currentPtr << ", frameType: " << frameType;
        return false;
    }
    return true;
}

__attribute__((no_sanitize("hwaddress"))) bool StepArk(void *ctx,
                                                       ReadMemFunc readMem,
                                                       ArkStepParam *arkStepParam)
{
    constexpr size_t FP_SIZE = sizeof(uintptr_t);
    uintptr_t currentPtr = *arkStepParam->fp;
    if (currentPtr == 0) {
        return false;
    }

#if defined(ENABLE_STATIC_BACKTRACE)
    if (*arkStepParam->frameType == StepFrameType::STATIC_JS_FRAME) {
        ark::tooling::ArkStepParam staticParam;
        staticParam.fp = arkStepParam->fp;
        staticParam.sp = arkStepParam->sp;
        staticParam.pc = arkStepParam->pc;
        staticParam.isArkFrame = arkStepParam->isJsFrame;
        staticParam.frameType = reinterpret_cast<ark::tooling::StepFrameType*>(arkStepParam->frameType);
        staticParam.frameIndex = arkStepParam->frameIndex;
        return StepStaticArk(ctx, readMem, &staticParam);
    }
#endif

    uintptr_t frameType = 0;
    if (ArkGetNextFrame(ctx, readMem, currentPtr, frameType, *arkStepParam->pc)) {
        if (ArkFrameCheck(frameType)) {
            currentPtr += sizeof(FrameType);
            *arkStepParam->sp = currentPtr;
            bool ret = readMem(ctx, currentPtr, arkStepParam->fp);
            currentPtr += FP_SIZE;
            ret &= readMem(ctx, currentPtr, arkStepParam->pc);
            *arkStepParam->isJsFrame = false;
            *arkStepParam->frameType = StepFrameType::NATIVE_FRAME;
            return ret;
        } else {
            *arkStepParam->fp = currentPtr;
            *arkStepParam->sp = currentPtr;
            // js -> true, native -> false
            *arkStepParam->isJsFrame = IsJsFunctionFrame(frameType);
            if (*arkStepParam->isJsFrame) {
                *arkStepParam->frameType = StepFrameType::JS_FRAME;
            } else {
                *arkStepParam->frameType = StepFrameType::NATIVE_FRAME;
            }
        }
    } else {
        return false;
    }

    return true;
}

uint8_t* JSSymbolExtractor::GetData()
{
    return data_;
}

uintptr_t JSSymbolExtractor::GetLoadOffset()
{
    return loadOffset_;
}

uintptr_t JSSymbolExtractor::GetDataSize()
{
    return dataSize_;
}

bool JSSymbolExtractor::InitializeHapFileInfo([[maybe_unused]]uintptr_t offset,
                                              [[maybe_unused]]bool needTranslate,
                                              [[maybe_unused]]const char* filePath)
{
#if defined(ENABLE_ABILITY_EXTRACTOR)
    if (isInitialized_) {
        return isInitialized_;
    }
    bool newCreate = false;
    std::string hapPath(filePath);
    std::shared_ptr<Extractor> extractor = ExtractorUtil::GetExtractor(hapPath, newCreate);
    if (extractor == nullptr) {
        LOG_ECMA(ERROR) << "GetExtractor failed, hap path: " << hapPath;
        return isInitialized_;
    }

    std::string pandaFilePath;
    auto zipFile = std::make_unique<ZipFile>(hapPath);
    bool zipOpenOk = zipFile->Open();
    if (!zipOpenOk) {
        LOG_ECMA(WARN) << "ZipFile open failed, hap path: " << hapPath;
        return isInitialized_;
    }
    pandaFilePath = GetFilePathByOffset(*zipFile, offset);
    if (pandaFilePath.empty()) {
        pandaFilePath = "ets/modules.abc";
        LOG_ECMA(WARN) << "Unknown offset, attempt to parse " << pandaFilePath;
    }
    fileMapper_ = GetMmapDataAsShared(*extractor, pandaFilePath);
    if (!fileMapper_) {
        LOG_ECMA(ERROR) << "GetSafeData failed, hap path: " << hapPath
                        << ", panda file: " << pandaFilePath;
        return isInitialized_;
    }

    data_ = fileMapper_->GetDataPtr();
    dataSize_ = fileMapper_->GetDataLen();
    loadOffset_ = static_cast<uintptr_t>(fileMapper_->GetOffset());
    isInitialized_ = true;
#if defined(ENABLE_STATIC_BACKTRACE)
    type_ = GetPandaFileType(data_);
    if (type_ == panda_file::PandaFileType::FILE_STATIC) {
        ark::tooling::Backtrace::SetExtractorData(data_, dataSize_, 0, staticSymbolExtractor_);
        return isInitialized_;
    }
#endif
    CreateJSPandaFile();
    if (zipOpenOk && isInitialized_ && needTranslate) {
        auto &entrys = zipFile->GetAllEntries();
        if (entrys.find("ets/sourceMaps.map") == entrys.end()) {
            LOG_ECMA(INFO) << "Can't find sourceMaps.map in hap/hsp";
            return isInitialized_;
        }
        CreateSourceMap(hapPath);
    }
#endif
    return isInitialized_;
}

bool JSSymbolExtractor::InitializeAbcFileInfo([[maybe_unused]]const char* filePath)
{
#if defined(ENABLE_ABILITY_EXTRACTOR)
    if (isInitialized_) {
        return isInitialized_;
    }
    fileMapMem_ = FileMap(filePath, O_RDONLY, PROT_READ, 0);
    if (fileMapMem_.GetMem() == nullptr) {
        return isInitialized_;
    }
    data_ = reinterpret_cast<uint8_t *>(fileMapMem_.GetMem());
    dataSize_ = reinterpret_cast<uintptr_t>(fileMapMem_.GetSize());
    isInitialized_ = true;
#if defined(ENABLE_STATIC_BACKTRACE)
    type_ = panda_file::GetFileType(data_, dataSize_);
    if (type_ == panda_file::PandaFileType::FILE_STATIC) {
        ark::tooling::Backtrace::SetExtractorData(data_, dataSize_, 0, staticSymbolExtractor_);
        return isInitialized_;
    }
#endif
    CreateJSPandaFile();
#endif
    return isInitialized_;
}

bool JSSymbolExtractor::InitializeELFFileInfo(uintptr_t offset, const char* filePath)
{
    // only for dynamic_abc, because dynamic_abc may be compiled into .so
#if defined(ENABLE_ABILITY_EXTRACTOR)
    if (isInitialized_) {
        return isInitialized_;
    }
    fileMapMem_ = FileMap(filePath, O_RDONLY, PROT_READ, 0);
    if (fileMapMem_.GetMem() == nullptr) {
        return isInitialized_;
    }

    ElfReader reader(fileMapMem_);
    uint64_t fileOffset = reader.RuntimeOffsetToFileOffset(offset);

    data_ = reinterpret_cast<uint8_t *>(reinterpret_cast<uintptr_t>(fileMapMem_.GetMem()) + fileOffset);
    auto header = reinterpret_cast<const panda_file::File::Header *>(data_);
    dataSize_ = header->file_size;
    isInitialized_ = true;
    CreateJSPandaFile();
#endif
    return isInitialized_;
}

bool JSSymbolExtractor::Initialize(uintptr_t offset, bool needTranslate, const char* filePath)
{
    uintptr_t elfOffset = 0;
    CString realPath;
    auto type = ModulePathHelper::ParseVMANameToFileName(filePath, elfOffset, realPath);
    switch (type) {
        case ModulePathType::HAP_MODULE_PATH:
            return InitializeHapFileInfo(offset, needTranslate, filePath);
        case ModulePathType::ABC_MODULE_PATH:
            return InitializeAbcFileInfo(realPath.c_str());
        case ModulePathType::SHORT_MODULE_PATH:
        case ModulePathType::SYSTEM_MODULE_PATH:
            return InitializeELFFileInfo(elfOffset, realPath.c_str());
        default:
            LOG_ECMA(ERROR) << "Invalid file path: " << filePath;
            return false;
    }
    return false;
}

bool ArkParseJSFileInfo([[maybe_unused]] uintptr_t byteCodePc, [[maybe_unused]] uintptr_t mapBase,
                        [[maybe_unused]] uintptr_t offset, [[maybe_unused]] const char* filePath,
                        [[maybe_unused]] uintptr_t extractorptr, [[maybe_unused]] bool needTranslate,
                        [[maybe_unused]] JsFunction *jsFunction)
{
    bool ret = false;
#if defined(ENABLE_ABILITY_EXTRACTOR)
    if (filePath == nullptr) {
        LOG_ECMA(ERROR) << "FilePath from dfx is nullptr.";
        return false;
    }
    auto extractor = reinterpret_cast<JSSymbolExtractor*>(extractorptr);
    if (extractor == nullptr) {
        LOG_ECMA(ERROR) << "Parse JSframe info failed, extractor is nullptr.";
        return false;
    }
    if (!extractor->Initialize(offset, needTranslate, filePath)) {
        return false;
    }
    ret = ArkParseJsFrameInfo(byteCodePc, mapBase, extractor->GetLoadOffset(),
            extractor->GetData(), extractor->GetDataSize(), extractorptr, jsFunction);
#endif
    return ret;
}

JSSymbolExtractor::~JSSymbolExtractor()
{
    if (sourceMap_ != nullptr) {
        sourceMap_.reset();
    }
    if (debugExtractor_ != nullptr) {
        debugExtractor_.reset();
    }
    if (jsPandaFile_ != nullptr) {
        jsPandaFile_.reset();
    }
    methodInfo_.clear();
}

JSSymbolExtractor* JSSymbolExtractor::Create()
{
    auto extractor = new JSSymbolExtractor();
    return extractor;
}

bool JSSymbolExtractor::Destroy(JSSymbolExtractor *extractor)
{
    if (extractor == nullptr) {
        LOG_ECMA(ERROR) << "Destroy ark symbol extractor failed, extractor is nullptr.";
        return false;
    }
    delete extractor;
    extractor = nullptr;
    return true;
}

void JSSymbolExtractor::SetStaticSymbolExtractor(uintptr_t extractorptr)
{
    if (!extractorptr) {
        LOG_ECMA(ERROR) << "Set static ark symbol extractor failed, extractor is nullptr.";
        return;
    }
    staticSymbolExtractor_ = extractorptr;
}

uintptr_t JSSymbolExtractor::GetStaticSymbolExtractor()
{
    return staticSymbolExtractor_;
}

const CVector<MethodInfo> &JSSymbolExtractor::GetMethodInfos() const
{
    if (methodInfo_.empty()) {
        methodInfo_ = JSStackTrace::ReadAllMethodInfos(jsPandaFile_);
    }

    return methodInfo_;
}

JSPandaFile* JSSymbolExtractor::GetJSPandaFile(uint8_t *data, size_t dataSize)
{
    if (jsPandaFile_ == nullptr && data != nullptr) {
        CreateJSPandaFile(data, dataSize);
    }
    return jsPandaFile_.get();
}

void JSSymbolExtractor::CreateJSPandaFile()
{
    auto pf = panda_file::OpenPandaFileFromSecureMemory(data_, dataSize_);
    if (pf == nullptr) {
        LOG_ECMA(ERROR) << "Failed to open panda file.";
        return;
    }
    jsPandaFile_ = std::make_shared<JSPandaFile>(pf.release(), "", CreateMode::DFX);
}

void JSSymbolExtractor::CreateJSPandaFile(uint8_t *data, size_t dataSize)
{
    auto pf = panda_file::OpenPandaFileFromSecureMemory(data, dataSize);
    if (pf == nullptr) {
        LOG_ECMA(ERROR) << "Failed to open panda file.";
        return;
    }
    jsPandaFile_ = std::make_shared<JSPandaFile>(pf.release(), "", CreateMode::DFX);
}

SourceMap* JSSymbolExtractor::GetSourceMap()
{
    return sourceMap_.get();
}

void JSSymbolExtractor::CreateSourceMap([[maybe_unused]] const std::string &hapPath)
{
#if defined(ENABLE_ABILITY_EXTRACTOR)
    if (sourceMap_ == nullptr) {
        sourceMap_ = std::make_shared<SourceMap>();
        sourceMap_->Init(hapPath);
    }
#endif
}

DebugInfoExtractor* JSSymbolExtractor::GetDebugExtractor()
{
    if (debugExtractor_ == nullptr) {
        JSSymbolExtractor::CreateDebugExtractor();
    }
    return debugExtractor_.get();
}

void JSSymbolExtractor::CreateDebugExtractor()
{
    debugExtractor_ = std::make_unique<DebugInfoExtractor>(jsPandaFile_.get());
}

uintptr_t ArkCreateJSSymbolExtractor()
{
    auto extractor = JSSymbolExtractor::Create();
    auto extractorptr = reinterpret_cast<uintptr_t>(extractor);
#if defined(ENABLE_STATIC_BACKTRACE)
    auto staticSymbolExtractor = ark::tooling::Backtrace::CreateArkSymbolExtractor();
    extractor->SetStaticSymbolExtractor(staticSymbolExtractor);
#endif
    return extractorptr;
}

bool ArkDestroyJSSymbolExtractor(uintptr_t extractorptr)
{
    auto extractor = reinterpret_cast<JSSymbolExtractor*>(extractorptr);
#if defined(ENABLE_STATIC_BACKTRACE)
    auto staticSymbolExtractor = extractor->GetStaticSymbolExtractor();
    ark::tooling::Backtrace::DestroyArkSymbolExtractor(staticSymbolExtractor);
#endif
    return JSSymbolExtractor::Destroy(extractor);
}

void JSStackTrace::AddReference()
{
    std::unique_lock<std::mutex> lock(mutex_);
    if (count_ == 0) {
        trace_ = new JSStackTrace();
#if defined(ENABLE_STATIC_BACKTRACE)
        arkStaticHandle_ = dlopen("libarkruntime.so", RTLD_LAZY | RTLD_NOLOAD | RTLD_NODELETE);
        if (arkStaticHandle_) {
            parseStaticArkLocalFunc_ = reinterpret_cast<ParseStaticArkLocalFunc>(dlsym(arkStaticHandle_,
                "ArkParseArkFrameInfoLocal"));
            createStaticArkLocalFunc_ = reinterpret_cast<CreateStaticArkLocalFunc>(dlsym(arkStaticHandle_,
                "ArkCreateLocalStackTrace"));
            destroyStaticArkLocalFunc_ = reinterpret_cast<DestroyStaticArkLocalFunc>(dlsym(arkStaticHandle_,
                "ArkDestroyLocalStackTrace"));
            if (parseStaticArkLocalFunc_ && createStaticArkLocalFunc_ && destroyStaticArkLocalFunc_) {
                createStaticArkLocalFunc_(&arkStaticLocalTracePtr_);
            }
        }
#endif
    }
    ++count_;
    LOG_ECMA(INFO) << "Add reference, count: " << count_;
}

void JSStackTrace::ReleaseReference()
{
    std::unique_lock<std::mutex> lock(mutex_);
    if (trace_ == nullptr) {
        return ;
    }
    --count_;
    LOG_ECMA(INFO) << "Release reference, count: " << count_;
    if (count_ == 0) {
        delete trace_;
        trace_ = nullptr;
#if defined(ENABLE_STATIC_BACKTRACE)
        if (arkStaticHandle_) {
            if (parseStaticArkLocalFunc_ && createStaticArkLocalFunc_ && destroyStaticArkLocalFunc_) {
                destroyStaticArkLocalFunc_(arkStaticLocalTracePtr_);
                arkStaticLocalTracePtr_ = nullptr;
            }
            dlclose(arkStaticHandle_);
            arkStaticHandle_ = nullptr;
            parseStaticArkLocalFunc_ = nullptr;
            createStaticArkLocalFunc_ = nullptr;
            destroyStaticArkLocalFunc_ = nullptr;
        }
#endif
    }
}

#if defined(ENABLE_STATIC_BACKTRACE)
bool JSStackTrace::GetArkStaticFrameInfo(uintptr_t byteCodePc, uintptr_t mapBase, uintptr_t loadOffset,
                                         JsFunction *jsFunction)
{
    if (!parseStaticArkLocalFunc_) {
        LOG_ECMA(ERROR) << "Can not call ParseStaticArkLocalFunc";
        return false;
    }
    return parseStaticArkLocalFunc_(arkStaticLocalTracePtr_, byteCodePc, mapBase, loadOffset, jsFunction);
}
#endif

JSStackTrace::~JSStackTrace()
{
    {
        std::unique_lock<std::shared_mutex> lock(infosMutex_);
        methodInfos_.clear();
    }
    {
        std::unique_lock<std::shared_mutex> lock(pfMutex_);
        jsPandaFiles_.clear();
    }
}

bool JSStackTrace::InitializeMethodInfo(uintptr_t mapBase)
{
    auto pandafile = FindJSpandaFile(mapBase);
    if (pandafile != nullptr) {
        return true;
    }
    pandafile =
        JSPandaFileManager::GetInstance()->FindJSPandaFileByMapBase(mapBase);
    if (pandafile == nullptr) {
        return false;
    }
    auto methodInfos = ReadAllMethodInfos(pandafile);
    SetMethodInfos(mapBase, methodInfos);
    SetJSpandaFile(mapBase, pandafile);
    return true;
}

std::shared_ptr<JSPandaFile> JSStackTrace::FindJSpandaFile(uintptr_t mapBase)
{
    std::shared_lock<std::shared_mutex> lock(pfMutex_);
    auto iter = jsPandaFiles_.find(mapBase);
    if (iter == jsPandaFiles_.end()) {
        return nullptr;
    }
    return iter->second;
}

void JSStackTrace::SetJSpandaFile(uintptr_t mapBase, std::shared_ptr<JSPandaFile> pandafile)
{
    std::unique_lock<std::shared_mutex> lock(pfMutex_);
    jsPandaFiles_.emplace(mapBase, pandafile);
}

const CVector<MethodInfo> &JSStackTrace::FindMethodInfos(uintptr_t mapBase)
{
    std::shared_lock<std::shared_mutex> lock(infosMutex_);
    auto iter = methodInfos_.find(mapBase);
    if (iter == methodInfos_.end()) {
        return methodInfo_;
    }
    return iter->second;
}

void JSStackTrace::SetMethodInfos(uintptr_t mapBase, CVector<MethodInfo> &infos)
{
    std::unique_lock<std::shared_mutex> lock(infosMutex_);
    methodInfos_.emplace(mapBase, std::move(infos));
}

bool JSStackTrace::GetJsFrameInfo(uintptr_t byteCodePc, uintptr_t mapBase,
                                  uintptr_t loadOffset, JsFunction *jsFunction)
{
    if (!InitializeMethodInfo(mapBase)) {
        return false;
    }
    loadOffset = loadOffset % PageSize();
    byteCodePc = byteCodePc - loadOffset;
    const auto &infos = FindMethodInfos(mapBase);
    auto codeInfo = TranslateByteCodePc(byteCodePc, infos);
    if (!codeInfo) {
        LOG_ECMA(ERROR) << std::hex << "Failed to get methodId, pc: " << byteCodePc;
        return false;
    }
    auto offset = codeInfo->offset;
    auto pandafile = FindJSpandaFile(mapBase);
    auto debugInfoExtractor =
        JSPandaFileManager::GetInstance()->GetJSPtExtractor(pandafile.get());
    if (!ParseJsFrameInfo(pandafile.get(), debugInfoExtractor, EntityId(codeInfo->methodId), offset,
                          *jsFunction, &SourceMap::GetInstance())) {
        return false;
    }
    jsFunction->codeBegin = byteCodePc - offset;
    jsFunction->codeSize = codeInfo->codeSize;
    return true;
}

void ArkCreateLocal()
{
    JSStackTrace::AddReference();
}

bool ArkParseJsFrameInfoLocal(uintptr_t byteCodePc, uintptr_t mapBase,
                              uintptr_t loadOffset, JsFunction *jsFunction)
{
    auto trace = JSStackTrace::GetInstance();
    if (trace == nullptr) {
        LOG_ECMA(ERROR) << "singleton is null, need create first.";
        return false;
    }
    if (trace->GetJsFrameInfo(byteCodePc, mapBase, loadOffset, jsFunction)) {
        return true;
    }
#if defined(ENABLE_STATIC_BACKTRACE)
    if (trace->GetArkStaticFrameInfo(byteCodePc, mapBase, loadOffset, jsFunction)) {
        return true;
    }
#endif
    LOG_ECMA(ERROR) << "ArkParseJsFrameInfoLocal failed, mapBase=0x"
                    << std::hex << mapBase << " byteCodePc=0x" << byteCodePc;
    return false;
}

void ArkDestroyLocal()
{
    JSStackTrace::ReleaseReference();
}

} // namespace panda::ecmascript

__attribute__((visibility("default"))) int ark_create_js_symbol_extractor(uintptr_t *extractorptr)
{
    *extractorptr = panda::ecmascript::ArkCreateJSSymbolExtractor();
    return 1;
}

__attribute__((visibility("default"))) int ark_destory_js_symbol_extractor(uintptr_t extractorptr)
{
    if (panda::ecmascript::ArkDestroyJSSymbolExtractor(extractorptr)) {
        return 1;
    }
    return -1;
}

__attribute__((visibility("default"))) int ark_destroy_local()
{
    panda::ecmascript::ArkDestroyLocal();
    return 1;
}

__attribute__((visibility("default"))) int ark_create_local()
{
    panda::ecmascript::ArkCreateLocal();
    return 1;
}

__attribute__((visibility("default"))) int step_ark_with_record_jit(panda::ecmascript::ArkUnwindParam *arkUnwindParam)
{
    if (panda::ecmascript::StepArkWithRecordJit(arkUnwindParam)) {
        return 1;
    }
    return -1;
}

__attribute__((visibility("default"))) int ark_write_jit_code(
    void *ctx, panda::ecmascript::ReadMemFunc readMem, int fd, const uintptr_t *const jitCodeArray,
    const size_t jitSize)
{
    if (panda::ecmascript::ArkWriteJitCode(ctx, readMem, fd, jitCodeArray, jitSize)) {
        return 1;
    }
    return -1;
}

__attribute__((visibility("default"))) int step_ark(
    void *ctx, panda::ecmascript::ReadMemFunc readMem, panda::ecmascript::ArkStepParam *arkStepParam)
{
    if (panda::ecmascript::StepArk(ctx, readMem, arkStepParam)) {
        return 1;
    }
    return -1;
}

__attribute__((visibility("default"))) int ark_parse_js_frame_info(
    uintptr_t byteCodePc, uintptr_t mapBase, uintptr_t loadOffset, uint8_t *data,
    uint64_t dataSize, uintptr_t extractorptr, panda::ecmascript::JsFunction *jsFunction)
{
    if (panda::ecmascript::ArkParseJsFrameInfo(byteCodePc, mapBase, loadOffset, data,
                                               dataSize, extractorptr, jsFunction)) {
        return 1;
    }
    return -1;
}

__attribute__((visibility("default"))) int ark_parse_js_file_info(
    uintptr_t byteCodePc, uintptr_t mapBase, uintptr_t offset, const char* filePath,
    uintptr_t extractorptr, bool needTranslate, panda::ecmascript::JsFunction *jsFunction)
{
    if (panda::ecmascript::ArkParseJSFileInfo(byteCodePc, mapBase, offset, filePath,
        extractorptr, needTranslate, jsFunction)) {
        return 1;
    }
    return -1;
}

__attribute__((visibility("default"))) int ark_parse_js_frame_info_local(
    uintptr_t byteCodePc, uintptr_t mapBase, uintptr_t loadOffset, panda::ecmascript::JsFunction *jsFunction)
{
    if (panda::ecmascript::ArkParseJsFrameInfoLocal(byteCodePc, mapBase, loadOffset, jsFunction)) {
        return 1;
    }
    return -1;
}

__attribute__((visibility("default"))) void EnableArkTsHook()
{
#if defined(ECMASCRIPT_SUPPORT_HEAPPROFILER)
    if (!panda::ecmascript::Runtime::HasInstance()) {
        return;
    }
    panda::ecmascript::Runtime::GetInstance()->SetHiProfilerEnabled(true);
    panda::ecmascript::Runtime::GetInstance()->IterateAllThreadList([](panda::ecmascript::JSThread *thread) {
        thread->SetIsStartHeapSampling(true);
    });
#endif
}

__attribute__((visibility("default"))) void DisableArkTsHook()
{
#if defined(ECMASCRIPT_SUPPORT_HEAPPROFILER)
    if (!panda::ecmascript::Runtime::HasInstance()) {
        return;
    }
    panda::ecmascript::Runtime::GetInstance()->SetHiProfilerEnabled(false);
    panda::ecmascript::Runtime::GetInstance()->IterateAllThreadList([](panda::ecmascript::JSThread *thread) {
        if (thread->IsJitThread() || thread->GetEcmaVM() == nullptr ||
            thread->GetEcmaVM()->GetHeapProfile() == nullptr) {
            thread->SetIsStartHeapSampling(false);
        }
    });
#endif
}
