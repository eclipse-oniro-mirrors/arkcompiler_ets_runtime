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

#include <sys/time.h>

#include "ecmascript/dfx/stackinfo/js_stackinfo.h"
#include "ecmascript/base/builtins_base.h"
#include "ecmascript/ecma_vm.h"
#include "ecmascript/interpreter/frame_handler.h"
#include "ecmascript/interpreter/interpreter.h"
#include "ecmascript/jspandafile/js_pandafile_manager.h"
#include "ecmascript/jspandafile/program_object.h"
#include "ecmascript/mem/heap-inl.h"
#include "ecmascript/message_string.h"
#include "ecmascript/platform/os.h"
#if defined(PANDA_TARGET_OHOS)
#include "ecmascript/extractortool/src/extractor.h"
#endif
#if defined(ENABLE_EXCEPTION_BACKTRACE)
#include "ecmascript/platform/backtrace.h"
#endif

namespace panda::ecmascript {
[[maybe_unused]]static bool g_needCheck = true;

const int USEC_PER_SEC = 1000 * 1000;
const int NSEC_PER_USEC = 1000;
std::string JsStackInfo::BuildMethodTrace(Method *method, uint32_t pcOffset, bool enableStackSourceFile)
{
    std::string data;
    data.append("    at ");
    std::string name = method->ParseFunctionName();
    if (name.empty()) {
        name = "anonymous";
    }
    data += name;
    data.append(" (");
    // source file
    DebugInfoExtractor *debugExtractor =
        JSPandaFileManager::GetInstance()->GetJSPtExtractor(method->GetJSPandaFile());
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
    data.push_back(')');
    data.push_back('\n');
    return data;
}

std::string JsStackInfo::BuildInlinedMethodTrace(const JSPandaFile *pf, std::map<uint32_t, uint32_t> &methodOffsets)
{
    std::string data;
    std::map<uint32_t, uint32_t>::reverse_iterator it;
    for (it = methodOffsets.rbegin(); it != methodOffsets.rend(); it++) {
        uint32_t methodId = it->second;
        std::string name;
        if (methodId == 0) {
            name = "unknown";
        } else {
            name = std::string(MethodLiteral::GetMethodName(pf, EntityId(methodId)));
            if (name == "") {
                name = "anonymous";
            }
        }
        data.append("    at ");
        data.append(name);
        data.append(" (maybe inlined).");
        data.append(" depth: ");
        data.append(std::to_string(it->first));

        data.push_back('\n');
    }
    return data;
}

std::string JsStackInfo::BuildJsStackTrace(JSThread *thread, bool needNative)
{
    std::string data;
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
        if (!method->IsNativeWithCallField()) {
            auto pcOffset = it.GetBytecodeOffset();
            const JSPandaFile *pf = method->GetJSPandaFile();
            std::map<uint32_t, uint32_t> methodOffsets = it.GetInlinedMethodInfo();
            data += BuildInlinedMethodTrace(pf, methodOffsets);
            data += BuildMethodTrace(method, pcOffset, thread->GetEnableStackSourceFile());
        } else if (needNative) {
            auto addr = method->GetNativePointer();
            std::stringstream strm;
            strm << addr;
            data.append("    at native method (").append(strm.str()).append(")\n");
        }
    }
    if (data.empty()) {
#if defined(ENABLE_EXCEPTION_BACKTRACE)
        std::ostringstream stack;
        Backtrace(stack);
        data = stack.str();
#endif
    }
    return data;
}

uint64_t GetMicrosecondsTimeStamp()
{
    struct timespec time;
    clock_gettime(CLOCK_MONOTONIC, &time);
    return time.tv_sec * USEC_PER_SEC + time.tv_nsec / NSEC_PER_USEC;
}

void JsStackInfo::BuildCrashInfo(bool isJsCrash, uintptr_t pc)
{
    if (JsStackInfo::loader != nullptr && !JsStackInfo::loader->IsEnableAOT() &&
        JsStackInfo::options != nullptr && !JsStackInfo::options->IsEnableJIT()) {
        return;
    }
    std::string realOutPath;
    std::string sanboxPath = panda::os::file::File::GetExtendedFilePath(ohos::AotCrashInfo::GetSandBoxPath());
    if (!ecmascript::RealPath(sanboxPath, realOutPath, false)) {
        return;
    }
    uint64_t timestamp = GetMicrosecondsTimeStamp();
    ohos::AotCrashInfo aotCrashInfo;
    std::string soBuildId = aotCrashInfo.GetRuntimeBuildId();
    if (soBuildId == "") {
        LOG_ECMA(ERROR) << "can't get so buildId";
        return;
    }
    realOutPath = realOutPath + "/" + ohos::AotCrashInfo::GetCrashFileName();
    std::ifstream ifile(realOutPath.c_str());
    std::vector<std::string> lines;
    if (ifile.is_open()) {
        std::string iline;
        while (ifile >> iline) {
            std::string buildId = ohos::AotCrashInfo::GetBuildId(iline);
            if (buildId != soBuildId) {
                continue;
            }
            lines.emplace_back(iline);
        }
        ifile.close();
    }
    ohos::CrashType type;
    if (isJsCrash) {
        type = ohos::CrashType::JS;
    } else if (pc != 0 && JsStackInfo::loader != nullptr && JsStackInfo::loader->InsideAOT(pc)) {
        type = ohos::CrashType::AOT;
    } else {
        type = ohos::CrashType::OTHERS;
    }
    std::string buildLine = ohos::AotCrashInfo::BuildAotCrashInfo(soBuildId, std::to_string(timestamp), type);
    lines.emplace_back(buildLine);
    std::ofstream file(realOutPath.c_str(), std::ofstream::out);
    for (const std::string& line : lines) {
        file << line << "\n";
    }
    file.close();
}

std::vector<struct JsFrameInfo> JsStackInfo::BuildJsStackInfo(JSThread *thread, bool currentStack)
{
    std::vector<struct JsFrameInfo> jsFrame;
    uintptr_t *native = nullptr;
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
        if (native != nullptr) {
            frameInfo.nativePointer = native;
            native = nullptr;
        }
        if (!method->IsNativeWithCallField()) {
            std::string name = method->ParseFunctionName();
            if (name.empty()) {
                frameInfo.functionName = "anonymous";
            } else {
                frameInfo.functionName = name;
            }
            // source file
            DebugInfoExtractor *debugExtractor =
                JSPandaFileManager::GetInstance()->GetJSPtExtractor(method->GetJSPandaFile());
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
        } else {
            JSTaggedValue function = it.GetFunction();
            JSHandle<JSTaggedValue> extraInfoValue(
                thread, JSFunction::Cast(function.GetTaggedObject())->GetFunctionExtraInfo());
            if (extraInfoValue->IsJSNativePointer()) {
                JSHandle<JSNativePointer> extraInfo(extraInfoValue);
                native = reinterpret_cast<uintptr_t *>(extraInfo->GetData());
            }
        }
    }
    return jsFrame;
}

void CrashCallback(char *buf __attribute__((unused)), size_t len __attribute__((unused)),
                   void *ucontext __attribute__((unused)))
{
#if defined(__aarch64__) && !defined(PANDA_TARGET_MACOS) && !defined(PANDA_TARGET_IOS)
    if (ucontext == nullptr) {
        // should not happen
        return;
    }
    auto uctx = static_cast<ucontext_t *>(ucontext);
    uintptr_t pc = uctx->uc_mcontext.pc;
    uintptr_t fp = uctx->uc_mcontext.regs[29];  // 29: fp
    // 1. check pc is between ark code heap
    // 2. assemble crash info for ark code with signal-safe code
    // 3. do not do much things inside callback, stack size is limited
    // 4. do not use normal log
    if (JsStackInfo::loader == nullptr) {
        return;
    }
    if (!JsStackInfo::loader->InsideStub(pc) && !JsStackInfo::loader->InsideAOT(pc)) {
        return;
    }
    LOG_ECMA(ERROR) << std::hex << "CrashCallback pc:" << pc << " fp:" << fp;
    FrameIterator frame(reinterpret_cast<JSTaggedType *>(fp));
    bool isBuiltinStub = (frame.GetFrameType() == FrameType::OPTIMIZED_FRAME);
    Method *method = frame.CheckAndGetMethod();
    while (method == nullptr) {
        frame.Advance();
        if (frame.Done()) {
            break;
        }
        method = frame.CheckAndGetMethod();
    }
    std::string faultInfo;
    if (method != nullptr) {
        std::string methodName = method->GetMethodName();
        std::string recordName = method->GetRecordNameStr().c_str();
        faultInfo = "Method Name:" + methodName + " Record Name:" + recordName;
    } else {
        faultInfo = "method is nullptr!";
    }
    if (isBuiltinStub) {
        uintptr_t func = uctx->uc_mcontext.regs[2];  // 2: func
        JSTaggedValue builtinMethod = JSFunction::Cast(reinterpret_cast<TaggedObject *>(func))->GetMethod();
        uint8_t builtinId = Method::Cast(builtinMethod.GetTaggedObject())->GetBuiltinId();
        size_t builtinStart = static_cast<size_t>(GET_MESSAGE_STRING_ID(StringCharCodeAt) - 1);  // 1: offset NONE
        std::string builtinStr = MessageString::GetMessageString(builtinStart + builtinId);
        faultInfo += " " + builtinStr;
    }
    if (memcpy_s(buf, len, faultInfo.c_str(), faultInfo.length()) != EOK) {
        LOG_ECMA(ERROR) << "memcpy_s fail in CrashCallback()!";  // not FATAL to avoid further crash
    }
#endif
}

bool ReadUintptrFromAddr(int pid, uintptr_t addr, uintptr_t &value, bool needCheckRegion)
{
    if (pid == getpid()) {
        if (needCheckRegion) {
            bool flag = false;
            auto callback = [addr, &flag](Region *region) {
                uintptr_t regionBegin = region->GetBegin();
                uintptr_t regionEnd = region->GetEnd();
                if (regionBegin <= addr && addr <= regionEnd) {
                    flag = true;
                }
            };
            if (JsStackInfo::loader != nullptr) {
                const Heap *heap = JsStackInfo::loader->GetHeap();
                if (heap != nullptr) {
                    heap->EnumerateRegions(callback);
                }
            }
            if (!flag) {
                LOG_ECMA(ERROR) << "addr not in Region, addr: " << addr;
                return false;
            }
        }
        value = *(reinterpret_cast<uintptr_t *>(addr));
        return true;
    }
    long *retAddr = reinterpret_cast<long *>(&value);
    // note: big endian
    for (size_t i = 0; i < sizeof(uintptr_t) / sizeof(long); i++) {
        *retAddr = PtracePeektext(pid, addr);
        if (*retAddr == -1) {
            LOG_ECMA(ERROR) << "ReadFromAddr ERROR, addr: " << addr;
            return false;
        }
        addr += sizeof(long);
        retAddr++;
    }
    return true;
}

bool GetTypeOffsetAndPrevOffsetFromFrameType(uintptr_t frameType, uintptr_t &typeOffset, uintptr_t &prevOffset)
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

bool IsFunctionFrame(uintptr_t frameType)
{
    return static_cast<FrameType>(frameType) == FrameType::ASM_INTERPRETER_FRAME ||
           static_cast<FrameType>(frameType) == FrameType::INTERPRETER_CONSTRUCTOR_FRAME ||
           static_cast<FrameType>(frameType) == FrameType::INTERPRETER_FRAME ||
           static_cast<FrameType>(frameType) == FrameType::INTERPRETER_FAST_NEW_FRAME ||
           static_cast<FrameType>(frameType) == FrameType::OPTIMIZED_JS_FUNCTION_FRAME ||
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
    int32_t left = 0;
    ASSERT(vec.size() > 0);
    int32_t right = vec.size() - 1;
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
    return std::nullopt;
}

template<typename T>
void ParseJsFrameInfo(JSPandaFile *jsPandaFile, DebugInfoExtractor *debugExtractor,
                      EntityId methodId, uintptr_t offset, T &jsFrame, SourceMap *sourceMap = nullptr)
{
    if (jsPandaFile == nullptr) {
        LOG_ECMA(ERROR) << "Parse jsFrame info failed, jsPandaFile is nullptr.";
        return;
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

    if (!debugExtractor->MatchLineWithOffset(callbackLineFunc, methodId, offset) ||
        !debugExtractor->MatchColumnWithOffset(callbackColumnFunc, methodId, offset)) {
        lineNumber = 0;
        columnNumber = 0;
    }

    if (sourceMap != nullptr) {
        sourceMap->TranslateUrlPositionBySourceMap(url, lineNumber, columnNumber);
    }

    size_t urlSize = url.size() + 1;
    size_t nameSize = name.size() + 1;
    if (strcpy_s(jsFrame.url, urlSize, url.c_str()) != EOK ||
        strcpy_s(jsFrame.functionName, nameSize, name.c_str()) != EOK) {
        LOG_ECMA(FATAL) << "jsFrame strcpy_s failed";
        UNREACHABLE();
    }
    jsFrame.line = lineNumber;
    jsFrame.column = columnNumber;
}

bool ArkParseJsFrameInfo(uintptr_t byteCodePc, uintptr_t methodId, uintptr_t mapBase, uintptr_t loadOffset,
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
    auto jsPandaFile = extractor->GetJSPandaFile(data, dataSize);
    if (jsPandaFile == nullptr) {
        LOG_ECMA(ERROR) << "Parse JSframe info failed, panda file is nullptr.";
        return false;
    }
    auto debugExtractor = extractor->GetDebugExtractor();
    auto methodInfos = extractor->GetMethodInfos();
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
    if (!methodId) {
        methodId = codeInfo->methodId;
    }
    auto offset = codeInfo->offset;
    ParseJsFrameInfo(jsPandaFile, debugExtractor, EntityId(methodId), offset, *jsFunction);

    jsFunction->codeBegin = byteCodePc - offset;
    jsFunction->codeSize = codeInfo->codeSize;
    return true;
}

bool ArkTranslateJsFrameInfo(uint8_t *data, size_t dataSize, JsFunction *jsFunction)
{
    SourceMap sourceMap;
    std::string strUrl = jsFunction->url;
    sourceMap.Init(data, dataSize);
    bool ret = sourceMap.TranslateUrlPositionBySourceMap(strUrl, jsFunction->line, jsFunction->column);
    size_t strUrlSize = strUrl.size() + 1;
    if (strcpy_s(jsFunction->url, strUrlSize, strUrl.c_str()) != EOK) {
        LOG_FULL(FATAL) << "strcpy_s failed";
        UNREACHABLE();
    }
    return ret;
}

uintptr_t GetBytecodeOffset(void *ctx, ReadMemFunc readMem, uintptr_t frameType, uintptr_t currentPtr)
{
    // currentPtr points to the frametype.
    uintptr_t bytecodePc = 0;
    FrameType type = static_cast<FrameType>(frameType);
    switch (type) {
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
        // aot get native pc
        case FrameType::OPTIMIZED_JS_FAST_CALL_FUNCTION_FRAME:
        case FrameType::OPTIMIZED_JS_FUNCTION_FRAME: {
            currentPtr -= OptimizedJSFunctionFrame::GetTypeOffset();
            currentPtr += OptimizedJSFunctionFrame::GetReturnAddrOffset();
            readMem(ctx, currentPtr, &bytecodePc);
            return bytecodePc;
        }
        default: {
            break;
        }
    }
    return 0;
}

uintptr_t ArkGetFunction(void *ctx, ReadMemFunc readMem, uintptr_t currentPtr, uintptr_t frameType)
{
    FrameType type = static_cast<FrameType>(frameType);
    uintptr_t funcAddr = currentPtr;
    switch (type) {
        case FrameType::OPTIMIZED_JS_FAST_CALL_FUNCTION_FRAME:
        case FrameType::OPTIMIZED_JS_FUNCTION_FRAME: {
            funcAddr -= OptimizedJSFunctionFrame::GetTypeOffset();
            funcAddr += OptimizedJSFunctionFrame::GetFunctionOffset();
            break;
        }
        case FrameType::ASM_INTERPRETER_FRAME:
        case FrameType::INTERPRETER_CONSTRUCTOR_FRAME: {
            funcAddr -= AsmInterpretedFrame::GetTypeOffset();
            funcAddr += AsmInterpretedFrame::GetFunctionOffset(false);
            break;
        }
        case FrameType::INTERPRETER_FRAME:
        case FrameType::INTERPRETER_FAST_NEW_FRAME: {
            funcAddr -= InterpretedFrame::GetTypeOffset();
            funcAddr += InterpretedFrame::GetFunctionOffset();
            break;
        }
        default: {
            return 0;
        }
    }
    uintptr_t function = 0;
    if (!readMem(ctx, funcAddr, &function)) {
        return 0;
    }
    return function;
}

bool ArkCheckIsJSFunctionBaseOrJSProxy(void *ctx, ReadMemFunc readMem, uintptr_t objAddr, bool &isJSFunctionBase)
{
    bool isHeapObj = ((objAddr & JSTaggedValue::TAG_HEAPOBJECT_MASK) == 0U);
    bool isInvalidValue = (objAddr <= JSTaggedValue::INVALID_VALUE_LIMIT);
    if (isHeapObj && !isInvalidValue) {
        ASSERT_PRINT(((objAddr & JSTaggedValue::TAG_WEAK) == 0U),
                     "can not convert JSTaggedValue to HeapObject :" << std::hex << objAddr);
        uintptr_t hclassAddr = objAddr + TaggedObject::HCLASS_OFFSET;
        uintptr_t hclass = 0;
        if (!readMem(ctx, hclassAddr, &hclass)) {
            return false;
        }
        if (hclass != 0) {
            uintptr_t bitsAddr = reinterpret_cast<uintptr_t>(hclass + JSHClass::BIT_FIELD_OFFSET);
            uintptr_t bits = 0;
            if (!readMem(ctx, bitsAddr, &bits)) {
                return false;
            }
            JSType jsType = JSHClass::ObjectTypeBits::Decode(bits);
            isJSFunctionBase = (jsType >= JSType::JS_FUNCTION_BASE && jsType <= JSType::JS_BOUND_FUNCTION);
            bool isJSProxy = (jsType == JSType::JS_PROXY);
            return isJSFunctionBase || isJSProxy;
        }
    }
    return false;
}

uintptr_t ArkCheckAndGetMethod(void *ctx, ReadMemFunc readMem, uintptr_t value)
{
    bool isJSFunctionBase = 0;
    if (ArkCheckIsJSFunctionBaseOrJSProxy(ctx, readMem, value, isJSFunctionBase)) {
        if (isJSFunctionBase) {
            value += JSFunctionBase::METHOD_OFFSET;
        } else {
            value += JSProxy::METHOD_OFFSET;
        }
        uintptr_t method = 0;
        if (!readMem(ctx, value, &method)) {
            return 0;
        }
        return method;
    }
    return 0;
}

bool ArkGetMethodIdFromMethod(void *ctx, ReadMemFunc readMem, uintptr_t method, uintptr_t &methodId)
{
    uintptr_t methodLiteralAddr = method + Method::LITERAL_INFO_OFFSET;
    uintptr_t methodLiteral = 0;
    if (!readMem(ctx, methodLiteralAddr, &methodLiteral)) {
        return false;
    }
    methodId = MethodLiteral::MethodIdBits::Decode(methodLiteral);
    return true;
}

bool ArkGetMethodId(void *ctx, ReadMemFunc readMem, uintptr_t frameType, uintptr_t currentPtr, uintptr_t &methodId)
{
    uintptr_t function = ArkGetFunction(ctx, readMem, currentPtr, frameType);
    if (!function) {
        LOG_ECMA(DEBUG) << "Failed to get function";
        return false;
    }

    uintptr_t method = ArkCheckAndGetMethod(ctx, readMem, function);
    if (!method) {
        LOG_ECMA(DEBUG) << std::hex << "Failed to get method: " << function;
        return false;
    }

    if (!ArkGetMethodIdFromMethod(ctx, readMem, method, methodId)) {
        LOG_ECMA(DEBUG) << std::hex << "ArkGetJsFrameDebugInfo failed, method: " << method;
        return false;
    }
    return true;
}

bool ArkGetNextFrame(void *ctx, ReadMemFunc readMem, uintptr_t &currentPtr,
                     uintptr_t &frameType, uintptr_t &pc, uintptr_t *methodId)
{
    currentPtr -= sizeof(FrameType);
    if (!readMem(ctx, currentPtr, &frameType)) {
        return false;
    }
    if (ArkFrameCheck(frameType)) {
        return true;
    }
    bool ret = false;
    if (IsFunctionFrame(frameType)) {
        pc = GetBytecodeOffset(ctx, readMem, frameType, currentPtr);
        ret = true;
        if (methodId != nullptr) {
            ret = ArkGetMethodId(ctx, readMem, frameType, currentPtr, *methodId);
        }
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
    return ArkGetNextFrame(ctx, readMem, currentPtr, frameType, pc, methodId);
}

bool StepArk(void *ctx, ReadMemFunc readMem, uintptr_t *fp, uintptr_t *sp,
             uintptr_t *pc, uintptr_t *methodId, bool *isJsFrame)
{
    constexpr size_t FP_SIZE = sizeof(uintptr_t);
    uintptr_t currentPtr = *fp;
    if (currentPtr == 0) {
        LOG_ECMA(ERROR) << "fp is nullptr in StepArk()!";
        return false;
    }

    uintptr_t frameType = 0;
    if (ArkGetNextFrame(ctx, readMem, currentPtr, frameType, *pc, methodId)) {
        if (ArkFrameCheck(frameType)) {
            currentPtr += sizeof(FrameType);
            *sp = currentPtr;
            bool ret = readMem(ctx, currentPtr, fp);
            currentPtr += FP_SIZE;
            ret &= readMem(ctx, currentPtr, pc);
            *isJsFrame = false;
            return ret;
        } else {
            *fp = currentPtr;
            *sp = currentPtr;
            *isJsFrame = true;
        }
    } else {
        LOG_ECMA(ERROR) << std::hex << "ArkGetNextFrame failed, addr: " << currentPtr;
        return false;
    }

    return true;
}

uintptr_t ArkGetFunction(int pid, uintptr_t currentPtr, uintptr_t frameType)
{
    FrameType type = static_cast<FrameType>(frameType);
    uintptr_t funcAddr = currentPtr;
    switch (type) {
        case FrameType::OPTIMIZED_JS_FAST_CALL_FUNCTION_FRAME:
        case FrameType::OPTIMIZED_JS_FUNCTION_FRAME: {
            funcAddr -= OptimizedJSFunctionFrame::GetTypeOffset();
            funcAddr += OptimizedJSFunctionFrame::GetFunctionOffset();
            break;
        }
        case FrameType::ASM_INTERPRETER_FRAME:
        case FrameType::INTERPRETER_CONSTRUCTOR_FRAME: {
            funcAddr -= AsmInterpretedFrame::GetTypeOffset();
            funcAddr += AsmInterpretedFrame::GetFunctionOffset(false);
            break;
        }
        case FrameType::INTERPRETER_FRAME:
        case FrameType::INTERPRETER_FAST_NEW_FRAME: {
            funcAddr -= InterpretedFrame::GetTypeOffset();
            funcAddr += InterpretedFrame::GetFunctionOffset();
            break;
        }
        case FrameType::INTERPRETER_BUILTIN_FRAME: {
            funcAddr -= InterpretedBuiltinFrame::GetTypeOffset();
            funcAddr += InterpretedBuiltinFrame::GetFunctionOffset();
            break;
        }
        case FrameType::BUILTIN_FRAME_WITH_ARGV: {
            funcAddr += sizeof(FrameType);
            auto topAddress = funcAddr +
                (static_cast<int>(BuiltinWithArgvFrame::Index::StackArgsTopIndex) * sizeof(uintptr_t));
            uintptr_t argcAddress = static_cast<uintptr_t>(funcAddr + (static_cast<int>
                                    (BuiltinWithArgvFrame::Index::NumArgsIndex) * sizeof(uintptr_t)));
            if (!ReadUintptrFromAddr(pid, argcAddress, argcAddress, g_needCheck)) {
                return 0;
            }
            auto numberArgs = argcAddress + NUM_MANDATORY_JSFUNC_ARGS;
            funcAddr = topAddress - static_cast<uint32_t>(numberArgs) * sizeof(uintptr_t);
            break;
        }
        case FrameType::BUILTIN_ENTRY_FRAME:
        case FrameType::BUILTIN_FRAME: {
            funcAddr -= BuiltinFrame::GetTypeOffset();
            funcAddr += BuiltinFrame::GetStackArgsOffset();
            break;
        }
        case FrameType::BUILTIN_CALL_LEAVE_FRAME: {
            funcAddr -= OptimizedBuiltinLeaveFrame::GetTypeOffset();
            funcAddr += OptimizedBuiltinLeaveFrame::GetFunctionOffset();
            break;
        }
        case FrameType::BUILTIN_FRAME_WITH_ARGV_STACK_OVER_FLOW_FRAME :
        case FrameType::OPTIMIZED_FRAME:
        case FrameType::OPTIMIZED_ENTRY_FRAME:
        case FrameType::ASM_BRIDGE_FRAME:
        case FrameType::LEAVE_FRAME:
        case FrameType::LEAVE_FRAME_WITH_ARGV:
        case FrameType::INTERPRETER_ENTRY_FRAME:
        case FrameType::ASM_INTERPRETER_ENTRY_FRAME:
        case FrameType::ASM_INTERPRETER_BRIDGE_FRAME:
        case FrameType::OPTIMIZED_JS_FUNCTION_ARGS_CONFIG_FRAME:
        case FrameType::OPTIMIZED_JS_FUNCTION_UNFOLD_ARGV_FRAME: {
            return 0;
        }
        default: {
            LOG_FULL(FATAL) << "Unknown frame type: " << static_cast<uintptr_t>(type);
            UNREACHABLE();
        }
    }
    uintptr_t function = 0;
    if (!ReadUintptrFromAddr(pid, funcAddr, function, g_needCheck)) {
        return 0;
    }
    return function;
}

bool ArkCheckIsJSFunctionBaseOrJSProxy(int pid, uintptr_t objAddr, bool &isJSFunctionBase)
{
    bool isHeapObj = ((objAddr & JSTaggedValue::TAG_HEAPOBJECT_MASK) == 0U);
    bool isInvalidValue = (objAddr <= JSTaggedValue::INVALID_VALUE_LIMIT);
    if (isHeapObj && !isInvalidValue) {
        ASSERT_PRINT(((objAddr & JSTaggedValue::TAG_WEAK) == 0U),
                     "can not convert JSTaggedValue to HeapObject :" << std::hex << objAddr);
        uintptr_t hclassAddr = objAddr + TaggedObject::HCLASS_OFFSET;
        uintptr_t hclass = 0;
        if (!ReadUintptrFromAddr(pid, hclassAddr, hclass, g_needCheck)) {
            return false;
        }
        if (hclass != 0) {
            uintptr_t bitsAddr = reinterpret_cast<uintptr_t>(hclass + JSHClass::BIT_FIELD_OFFSET);
            uintptr_t bits = 0;
            if (!ReadUintptrFromAddr(pid, bitsAddr, bits, g_needCheck)) {
                return false;
            }
            JSType jsType = JSHClass::ObjectTypeBits::Decode(bits);
            isJSFunctionBase = (jsType >= JSType::JS_FUNCTION_BASE && jsType <= JSType::JS_BOUND_FUNCTION);
            bool isJSProxy = (jsType == JSType::JS_PROXY);
            return isJSFunctionBase || isJSProxy;
        }
    }
    return false;
}

uintptr_t ArkCheckAndGetMethod(int pid, uintptr_t value)
{
    bool isJSFunctionBase = 0;
    if (ArkCheckIsJSFunctionBaseOrJSProxy(pid, value, isJSFunctionBase)) {
        if (isJSFunctionBase) {
            value += JSFunctionBase::METHOD_OFFSET;
        } else {
            value += JSProxy::METHOD_OFFSET;
        }
        uintptr_t method = 0;
        if (!ReadUintptrFromAddr(pid, value, method, g_needCheck)) {
            return 0;
        }
        return method;
    }
    return 0;
}

bool ArkGetMethodIdandJSPandaFileAddr(int pid, uintptr_t method, uintptr_t &methodId, uintptr_t &jsPandaFileAddr)
{
    uintptr_t methodLiteralAddr = method + Method::LITERAL_INFO_OFFSET;
    uintptr_t methodLiteral = 0;
    if (!ReadUintptrFromAddr(pid, methodLiteralAddr, methodLiteral, g_needCheck)) {
            return false;
    }
    methodId = MethodLiteral::MethodIdBits::Decode(methodLiteral);
    uintptr_t constantpoolAddr = method + Method::CONSTANT_POOL_OFFSET;
    uintptr_t constantpool = 0;
    if (!ReadUintptrFromAddr(pid, constantpoolAddr, constantpool, g_needCheck)) {
        return false;
    }
    if (constantpool == JSTaggedValue::VALUE_UNDEFINED) {
        return false;
    }
    uintptr_t lengthAddr = constantpool + TaggedArray::LENGTH_OFFSET;
    uintptr_t length = 0;
    if (!ReadUintptrFromAddr(pid, lengthAddr, length, g_needCheck)) {
        return false;
    }
    jsPandaFileAddr = constantpool + TaggedArray::DATA_OFFSET +
                    JSTaggedValue::TaggedTypeSize() * (length - ConstantPool::JS_PANDA_FILE_INDEX);
    if (!ReadUintptrFromAddr(pid, jsPandaFileAddr, jsPandaFileAddr, g_needCheck)) {
        return false;
    }
    return true;
}

uint32_t ArkGetOffsetFromMethod(int pid, uintptr_t currentPtr, uintptr_t method)
{
    uintptr_t pc = 0;
    if (!ReadUintptrFromAddr(pid, currentPtr, pc, g_needCheck)) {
        return 0;
    }
    uintptr_t byteCodeArrayAddr = method + Method::NATIVE_POINTER_OR_BYTECODE_ARRAY_OFFSET;
    uintptr_t byteCodeArray = 0;
    if (!ReadUintptrFromAddr(pid, byteCodeArrayAddr, byteCodeArray, g_needCheck)) {
        return 0;
    }
    uintptr_t offset = pc - byteCodeArray;
    return static_cast<uint32_t>(offset);
}

uint32_t ArkGetBytecodeOffset(int pid, uintptr_t method, uintptr_t frameType, uintptr_t currentPtr)
{
    FrameType type = static_cast<FrameType>(frameType);
    switch (type) {
        case FrameType::ASM_INTERPRETER_FRAME:
        case FrameType::INTERPRETER_CONSTRUCTOR_FRAME: {
            currentPtr -= AsmInterpretedFrame::GetTypeOffset();
            currentPtr += AsmInterpretedFrame::GetPcOffset(false);
            return ArkGetOffsetFromMethod(pid, currentPtr, method);
        }
        case FrameType::INTERPRETER_FRAME:
        case FrameType::INTERPRETER_FAST_NEW_FRAME: {
            currentPtr -= InterpretedFrame::GetTypeOffset();
            currentPtr += InterpretedFrame::GetPcOffset(false);
            return ArkGetOffsetFromMethod(pid, currentPtr, method);
        }
        // aot need stackmaps
        case FrameType::OPTIMIZED_JS_FAST_CALL_FUNCTION_FRAME:
        case FrameType::OPTIMIZED_JS_FUNCTION_FRAME: {
            break;
        }
        default: {
            break;
        }
    }
    return 0;
}

std::string ArkGetFilePath(std::string &fileName)
{
    auto lastSlash = fileName.rfind("/");
    if (lastSlash == std::string::npos) {
        LOG_ECMA(ERROR) << "ArkGetFilePath can't find fisrt /: " << fileName;
        return "";
    }

    auto secondLastSlash = fileName.rfind("/", lastSlash - 1);
    if (secondLastSlash == std::string::npos) {
        LOG_ECMA(ERROR) << "ArkGetFilePath can't second fisrt /: " << fileName;
        return "";
    }

    std::string mapPath = fileName.substr(secondLastSlash + 1);
    return mapPath;
}

bool ArkIsNativeWithCallField(int pid, uintptr_t method)
{
    uintptr_t callFieldAddr = method + Method::CALL_FIELD_OFFSET;
    uintptr_t callField = 0;
    if (!ReadUintptrFromAddr(pid, callFieldAddr, callField, g_needCheck)) {
        return true;
    }
    return Method::IsNativeBit::Decode(callField);
}

std::string ArkReadCStringFromAddr(int pid, uintptr_t descAddr)
{
    std::string name;
    bool key = true;
    while (key) {
        uintptr_t desc = 0;
        if (!ReadUintptrFromAddr(pid, descAddr, desc, g_needCheck)) {
            LOG_ECMA(ERROR) << "ArkReadCStringFromAddr failed, descAddr: " << descAddr;
            return name;
        }
        size_t shiftAmount = 8;
        for (size_t i = 0; i < sizeof(long); i++) {
            char bottomEightBits = desc;
            desc = desc >> shiftAmount;
            if (!bottomEightBits) {
                key = false;
                break;
            }
            name += bottomEightBits;
        }
        if (!key) {
            break;
        }
        descAddr += sizeof(long);
    }
    return name;
}

std::string ArkGetFileName(int pid, uintptr_t jsPandaFileAddr, std::string &hapPath)
{
    size_t size = sizeof(JSPandaFile) / sizeof(long);
    uintptr_t *jsPandaFilePart = new uintptr_t[size]();
    for (size_t i = 0; i < size; i++) {
        if (!ReadUintptrFromAddr(pid, jsPandaFileAddr, jsPandaFilePart[i], g_needCheck)) {
            LOG_ECMA(ERROR) << "ArkGetFilePath failed, jsPandaFileAddr: " << jsPandaFileAddr;
            return "";
        }
        jsPandaFileAddr += sizeof(long);
    }
    JSPandaFile *jsPandaFile = reinterpret_cast<JSPandaFile *>(jsPandaFilePart);

    uintptr_t hapPathAddr = reinterpret_cast<uintptr_t>(
        const_cast<char *>(jsPandaFile->GetJSPandaFileHapPath().c_str()));
    hapPath = ArkReadCStringFromAddr(pid, hapPathAddr);

    uintptr_t descAddr = reinterpret_cast<uintptr_t>(
        const_cast<char *>(jsPandaFile->GetJSPandaFileDesc().c_str()));
    delete []jsPandaFilePart;
    return ArkReadCStringFromAddr(pid, descAddr);
}

std::unique_ptr<uint8_t[]> ArkReadData([[maybe_unused]] const std::string &hapPath,
                                       [[maybe_unused]] const std::string &fileName,
                                       [[maybe_unused]] size_t &dataSize)
{
    std::unique_ptr<uint8_t[]> dataPtr = nullptr;
#if defined(PANDA_TARGET_OHOS)
    bool newCreate = false;
    std::shared_ptr<Extractor> extractor = ExtractorUtil::GetExtractor(
        ExtractorUtil::GetLoadFilePath(hapPath), newCreate);
    if (extractor == nullptr) {
        LOG_ECMA(ERROR) << "Ark read data failed, hapPath: " << hapPath;
        return dataPtr;
    }
    if (!extractor->ExtractToBufByName(fileName, dataPtr, dataSize)) {
        LOG_ECMA(ERROR) << "Ark read data failed, hap/hsp path: " << hapPath << ", file name: " << fileName;
        return dataPtr;
    }
#endif
    return dataPtr;
}

std::shared_ptr<JSPandaFile> OpenJSPandaFileByReadData(const std::string &hapPath, const std::string &fileName)
{
    size_t dataSize = 0;
    auto data = ArkReadData(hapPath, fileName, dataSize);
    if (data == nullptr) {
        return nullptr;
    }
    auto pf = panda_file::OpenPandaFileFromMemory(data.get(), dataSize);
    if (pf == nullptr) {
        return nullptr;
    }
    return std::make_shared<JSPandaFile>(pf.release(), fileName.c_str());
}

void ArkParseJsFrameDebugInfos([[maybe_unused]] const std::vector<JsFrameDebugInfo> &JsFrameDebugInfos,
                               [[maybe_unused]] size_t size, [[maybe_unused]] JsFrame *jsFrame,
                               [[maybe_unused]] size_t &jsFrameIndex)
{
#if defined(PANDA_TARGET_OHOS)
    jsFrameIndex = 0;
    size = JsFrameDebugInfos.size() > size ? size : JsFrameDebugInfos.size();
    std::unordered_map<std::string, std::shared_ptr<JSPandaFile>> jsPandaFileTable;
    for (size_t i = 0; i < size; ++i) {
        auto fileIter = jsPandaFileTable.find(JsFrameDebugInfos[i].hapPath);
        if (fileIter == jsPandaFileTable.end()) {
            auto jsPandaFile = OpenJSPandaFileByReadData(JsFrameDebugInfos[i].hapPath, JsFrameDebugInfos[i].filePath);
            if (jsPandaFile != nullptr) {
                jsPandaFileTable.emplace(JsFrameDebugInfos[i].hapPath, jsPandaFile);
                auto debugExtractor = std::make_unique<DebugInfoExtractor>(jsPandaFile.get());
                ParseJsFrameInfo(jsPandaFile.get(), debugExtractor.get(), JsFrameDebugInfos[i].methodId,
                    JsFrameDebugInfos[i].offset, jsFrame[jsFrameIndex]);
                jsFrameIndex++;
            }
        } else {
            auto jsPandaFile = fileIter->second;
            auto debugExtractor = std::make_unique<DebugInfoExtractor>(jsPandaFile.get());
            ParseJsFrameInfo(jsPandaFile.get(), debugExtractor.get(), JsFrameDebugInfos[i].methodId,
                JsFrameDebugInfos[i].offset, jsFrame[jsFrameIndex]);
            jsFrameIndex++;
        }
    }
#endif
}

bool ArkGetJsFrameDebugInfo(int pid, uintptr_t currentPtr, uintptr_t frameType,
                            std::vector<JsFrameDebugInfo> &JsFrameDebugInfos)
{
    uintptr_t function = ArkGetFunction(pid, currentPtr, frameType);
    if (!function) {
        return false;
    }

    uintptr_t method = ArkCheckAndGetMethod(pid, function);
    if (!method || ArkIsNativeWithCallField(pid, method)) {
        return false;
    }
    uintptr_t jsPandaFileAddr = 0;
    uintptr_t methodId = 0;
    if (!ArkGetMethodIdandJSPandaFileAddr(pid, method, methodId, jsPandaFileAddr)) {
        LOG_ECMA(ERROR) << "ArkGetJsFrameDebugInfo failed, method: " << method;
        return false;
    }
    uintptr_t offset = ArkGetBytecodeOffset(pid, method, frameType, currentPtr);
    std::string hapPath;
    std::string fileName = ArkGetFileName(pid, jsPandaFileAddr, hapPath);
    if (fileName.empty() || hapPath.empty()) {
        LOG_ECMA(DEBUG) << "ArkGetJsFrameDebugInfo get filename or hapPath failed, fileName: "
                        << fileName << ", hapPath: "<< hapPath;
        return false;
    }
    std::string filePath = ArkGetFilePath(fileName);
    if (filePath.empty()) {
        return false;
    }
    JsFrameDebugInfo JsFrameDebugInfo(EntityId(methodId), offset, hapPath, filePath);
    JsFrameDebugInfos.push_back(std::move(JsFrameDebugInfo));
    return true;
}

bool ArkGetNextFrame(int pid, uintptr_t frameType, uintptr_t &currentPtr)
{
    uintptr_t typeOffset = 0;
    uintptr_t prevOffset = 0;
    if (!GetTypeOffsetAndPrevOffsetFromFrameType(frameType, typeOffset, prevOffset)) {
        LOG_ECMA(ERROR) << "FrameType ERROR, addr: " << currentPtr << ", frameType: " << frameType;
        return false;
    }
    currentPtr -= typeOffset;
    currentPtr += prevOffset;
    if (!ReadUintptrFromAddr(pid, currentPtr, currentPtr, g_needCheck)) {
        return false;
    }
    if (currentPtr == 0) {
        LOG_ECMA(ERROR) << "currentPtr is nullptr in GetArkNativeFrameInfo()!";
        return false;
    }
    return true;
}

bool GetArkNativeFrameInfo([[maybe_unused]] int pid, [[maybe_unused]] uintptr_t *pc,
                           [[maybe_unused]] uintptr_t *fp, [[maybe_unused]] uintptr_t *sp,
                           [[maybe_unused]] JsFrame *jsFrame, [[maybe_unused]] size_t &size)
{
#if defined(PANDA_TARGET_OHOS)
    constexpr size_t FP_SIZE = sizeof(uintptr_t);
    uintptr_t currentPtr = *fp;
    if (pid == getpid()) {
        g_needCheck = false;
    }
    if (currentPtr == 0) {
        LOG_ECMA(ERROR) << "fp is nullptr in GetArkNativeFrameInfo()!";
        return false;
    }

    if (pid == getpid() && JsStackInfo::loader != nullptr &&
        !JsStackInfo::loader->InsideStub(*pc) && !JsStackInfo::loader->InsideAOT(*pc)) {
        LOG_ECMA(ERROR) << "invalid pc in StepArkManagedNativeFrame()!";
        return false;
    }

    std::vector<JsFrameDebugInfo> JsFrameDebugInfos;
    bool ret = true;
    while (true) {
        currentPtr -= sizeof(FrameType);
        uintptr_t frameType = 0;
        if (!ReadUintptrFromAddr(pid, currentPtr, frameType, g_needCheck)) {
            return false;
        }
        if (g_needCheck && IsFunctionFrame(frameType)) {
            ArkGetJsFrameDebugInfo(pid, currentPtr, frameType, JsFrameDebugInfos);
        } else if (ArkFrameCheck(frameType)) {
            currentPtr += sizeof(FrameType);
            *sp = currentPtr;
            ret &= ReadUintptrFromAddr(pid, currentPtr, *fp, g_needCheck);
            currentPtr += FP_SIZE;
            ret &= ReadUintptrFromAddr(pid, currentPtr, *pc, g_needCheck);
            break;
        }

        if (!ArkGetNextFrame(pid, frameType, currentPtr)) {
            return false;
        }
    }
    if (g_needCheck && !JsFrameDebugInfos.empty()) {
        ArkParseJsFrameDebugInfos(JsFrameDebugInfos, size, jsFrame, size);
    } else {
        size = 0;
    }
    return ret;
#else
    return false;
#endif
}

uint8_t* JSSymbolExtractor::GetData()
{
    return data_.get();
}

uintptr_t JSSymbolExtractor::GetLoadOffset()
{
    return loadOffset_;
}

uintptr_t JSSymbolExtractor::GetDataSize()
{
    return dataSize_;
}

bool JSSymbolExtractor::ParseHapFileData([[maybe_unused]] std::string& hapName)
{
    bool ret = false;
#if defined(PANDA_TARGET_OHOS)
    if (hapName.empty()) {
        return false;
    }
    auto zipFile = std::make_unique<ZipFile>(hapName);
    if (zipFile == nullptr || !zipFile->Open()) {
        return false;
    }
    auto &entrys = zipFile->GetAllEntries();
    for (const auto &entry : entrys) {
        std::string fileName = entry.first;
        if (fileName.rfind("modules.abc") == std::string::npos) {
            continue;
        }

        ZipPos offset = 0;
        uint32_t length = 0;
        if (!zipFile->GetDataOffsetRelative(fileName, offset, length)) {
            break;
        }

        loadOffset_ = static_cast<uintptr_t>(offset);
        if (zipFile->ExtractToBufByName(fileName, data_, dataSize_)) {
            ret = true;
            break;
        }
    }
#endif
    return ret;
}

bool ArkParseJSFileInfo([[maybe_unused]] uintptr_t byteCodePc, [[maybe_unused]] uintptr_t methodId,
                        [[maybe_unused]] uintptr_t mapBase, [[maybe_unused]] const char* filePath,
                        [[maybe_unused]] uintptr_t extractorptr, [[maybe_unused]] JsFunction *jsFunction)
{
    bool ret = false;
#if defined(PANDA_TARGET_OHOS)
    if (filePath == nullptr) {
        LOG_ECMA(ERROR) << "FilePath from dfx is nullptr.";
        return false;
    }
    auto extractor = reinterpret_cast<JSSymbolExtractor*>(extractorptr);
    if (extractor == nullptr) {
        LOG_ECMA(ERROR) << "Parse JSframe info failed, extractor is nullptr.";
        return false;
    }
    if (extractor->GetJSPandaFile() == nullptr) {
        std::string hapName = std::string(filePath);
        extractor->ParseHapFileData(hapName);
        extractor->CreateJSPandaFile();
    }
    ret = ArkParseJsFrameInfo(byteCodePc, methodId, mapBase, extractor->GetLoadOffset(),
            extractor->GetData(), extractor->GetDataSize(), extractorptr, jsFunction);
#endif
    return ret;
}

bool StepArkManagedNativeFrame(int pid, uintptr_t *pc, uintptr_t *fp,
    uintptr_t *sp, [[maybe_unused]] char *buf, [[maybe_unused]] size_t buf_sz)
{
    constexpr size_t FP_SIZE = 8;
    constexpr size_t LR_SIZE = 8;
    uintptr_t currentPtr = *fp;
    if (currentPtr == 0) {
        LOG_ECMA(ERROR) << "fp is nullptr in StepArkManagedNativeFrame()!";
        return false;
    }
    if (pid == getpid() && JsStackInfo::loader != nullptr &&
        !JsStackInfo::loader->InsideStub(*pc) && !JsStackInfo::loader->InsideAOT(*pc)) {
        LOG_ECMA(ERROR) << "invalid pc in StepArkManagedNativeFrame()!";
        return false;
    }
    while (true) {
        currentPtr -= sizeof(FrameType);
        uintptr_t frameType = 0;
        if (!ReadUintptrFromAddr(pid, currentPtr, frameType, true)) {
            return false;
        }
        uintptr_t typeOffset = 0;
        uintptr_t prevOffset = 0;
        if (!GetTypeOffsetAndPrevOffsetFromFrameType(frameType, typeOffset, prevOffset)) {
            LOG_ECMA(ERROR) << "FrameType ERROR, addr: " << currentPtr << ", frameType: " << frameType;
            return false;
        }
        if (static_cast<FrameType>(frameType) == FrameType::OPTIMIZED_ENTRY_FRAME ||
            static_cast<FrameType>(frameType) == FrameType::ASM_INTERPRETER_ENTRY_FRAME ||
            static_cast<FrameType>(frameType) == FrameType::BUILTIN_ENTRY_FRAME) {
            break;
        }
        currentPtr -= typeOffset;
        currentPtr += prevOffset;
        if (!ReadUintptrFromAddr(pid, currentPtr, currentPtr, true)) {
            return false;
        }
        if (currentPtr == 0) {
            LOG_ECMA(ERROR) << "currentPtr is nullptr in StepArkManagedNativeFrame()!";
            return false;
        }
    }
    currentPtr += sizeof(FrameType);
    *fp = currentPtr;
    currentPtr += FP_SIZE;
    if (!ReadUintptrFromAddr(pid, currentPtr, *pc, true)) {
        return false;
    }
    currentPtr += LR_SIZE;
    *sp = currentPtr;
    return true;
}

void CopyBytecodeInfoToBuffer(const char *prefix, uintptr_t fullBytecode, size_t &strIdx, char *outStr, size_t strLen)
{
    // note: big endian
    ASSERT(strLen > 0);
    for (size_t i = 0; prefix[i] != '\0' && strIdx < strLen - 1; i++) {  // 1: last '\0'
        outStr[strIdx++] = prefix[i];
    }
    size_t start = static_cast<size_t>(GET_MESSAGE_STRING_ID(HandleLdundefined));
    size_t bytecode = fullBytecode & 0xff;  // 0xff: last byte
    const char *bytecodeName = MessageString::GetMessageString(start + bytecode).c_str();
    for (size_t i = 0; bytecodeName[i] != '\0' && strIdx < strLen - 1; i++) {  // 1: last '\0'
        outStr[strIdx++] = bytecodeName[i];
    }
    if (start + bytecode == static_cast<size_t>(GET_MESSAGE_STRING_ID(HandleDeprecated)) ||
        start + bytecode == static_cast<size_t>(GET_MESSAGE_STRING_ID(HandleWide)) ||
        start + bytecode == static_cast<size_t>(GET_MESSAGE_STRING_ID(HandleThrow)) ||
        start + bytecode == static_cast<size_t>(GET_MESSAGE_STRING_ID(HandleCallRuntime))) {
        size_t startSecond = start;
        if (start + bytecode == static_cast<size_t>(GET_MESSAGE_STRING_ID(HandleDeprecated))) {
            startSecond = static_cast<size_t>(GET_MESSAGE_STRING_ID(HandleDeprecatedLdlexenvPrefNone));
        } else if (start + bytecode == static_cast<size_t>(GET_MESSAGE_STRING_ID(HandleWide))) {
            startSecond = static_cast<size_t>(GET_MESSAGE_STRING_ID(
                HandleWideCreateobjectwithexcludedkeysPrefImm16V8V8));
        } else if (start + bytecode == static_cast<size_t>(GET_MESSAGE_STRING_ID(HandleThrow))) {
            startSecond = static_cast<size_t>(GET_MESSAGE_STRING_ID(HandleThrowPrefNone));
        } else if (start + bytecode == static_cast<size_t>(GET_MESSAGE_STRING_ID(HandleCallRuntime))) {
            startSecond = static_cast<size_t>(GET_MESSAGE_STRING_ID(HandleCallRuntimeNotifyConcurrentResultPrefNone));
        }
        size_t bytecodeSecond = (fullBytecode >> 8) & 0xff;  // 8, 0xff: second last byte
        const char *bytecodeNameSecond = MessageString::GetMessageString(startSecond + bytecodeSecond).c_str();
        if (strIdx < strLen - 1) {  // 1: last '\0'
            outStr[strIdx++] = '/';
        }
        for (size_t i = 0; bytecodeNameSecond[i] != '\0' && strIdx < strLen - 1; i++) {  // 1: last '\0'
            outStr[strIdx++] = bytecodeNameSecond[i];
        }
    }
    outStr[strIdx] = '\0';
}

bool GetArkJSHeapCrashInfo(int pid, uintptr_t *bytecodePc, uintptr_t *fp, bool outJSInfo, char *outStr, size_t strLen)
{
    // bytecodePc: X20 in ARM
    // fp: X29 in ARM
    // outJSInfo: not async-safe, more info
    uintptr_t currentPtr = *fp;
    if (currentPtr == 0) {
        LOG_ECMA(ERROR) << "fp is nullptr in GetArkJSHeapCrashInfo()!";
        return false;
    }
    currentPtr -= sizeof(FrameType);
    uintptr_t frameType = 0;
    if (!ReadUintptrFromAddr(pid, currentPtr, frameType, false)) {
        return false;
    }
    if (static_cast<FrameType>(frameType) != FrameType::ASM_INTERPRETER_FRAME) {
        return false;
    }
    size_t strIndex = 0;
    uintptr_t registerBytecode = 0;
    if (!ReadUintptrFromAddr(pid, *bytecodePc, registerBytecode, false)) {
        return false;
    }
    CopyBytecodeInfoToBuffer("RegisterBytecode:", registerBytecode, strIndex, outStr, strLen);
    uintptr_t typeOffset = MEMBER_OFFSET(AsmInterpretedFrame, base) + MEMBER_OFFSET(InterpretedFrameBase, type);
    uintptr_t pcOffset = MEMBER_OFFSET(AsmInterpretedFrame, pc);
    currentPtr -= typeOffset;
    currentPtr += pcOffset;
    uintptr_t framePc = 0;
    uintptr_t frameBytecode = 0;
    if (!ReadUintptrFromAddr(pid, currentPtr, framePc, false)) {
        return false;
    }
    if (!ReadUintptrFromAddr(pid, framePc, frameBytecode, false)) {
        return false;
    }
    CopyBytecodeInfoToBuffer(" FrameBytecode:", frameBytecode, strIndex, outStr, strLen);
    if (outJSInfo) {
        uintptr_t functionOffset = MEMBER_OFFSET(AsmInterpretedFrame, function);
        currentPtr -= pcOffset;
        currentPtr += functionOffset;
        uintptr_t functionAddress = 0;
        if (!ReadUintptrFromAddr(pid, currentPtr, functionAddress, false)) {
            return false;
        }
        JSTaggedValue functionValue(static_cast<JSTaggedType>(functionAddress));
        Method *method = ECMAObject::Cast(functionValue.GetTaggedObject())->GetCallTarget();
        auto bytecodeOffset = static_cast<uint32_t>(reinterpret_cast<uint8_t *>(*bytecodePc) -
                                                    method->GetBytecodeArray());
        std::string info = JsStackInfo::BuildMethodTrace(method, bytecodeOffset);
        const char *infoChar = info.c_str();
        if (strIndex < strLen - 1) {  // 1: last '\0'
            outStr[strIndex++] = ' ';
        }
        for (size_t i = 0; infoChar[i] != '\0' && strIndex < strLen - 1; i++) {  // 1: last '\0'
            outStr[strIndex++] = infoChar[i];
        }
        outStr[strIndex] = '\0';
    }
    return true;
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

bool JSSymbolExtractor::Destory(JSSymbolExtractor *extractor)
{
    if (extractor == nullptr) {
        LOG_ECMA(ERROR) << "Destory ark symbol extractor failed, extractor is nullptr.";
        return false;
    }
    delete extractor;
    extractor = nullptr;
    return true;
}

CVector<MethodInfo> JSSymbolExtractor::GetMethodInfos()
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
    auto pf = panda_file::OpenPandaFileFromSecureMemory(data_.get(), dataSize_);
    if (pf == nullptr) {
        LOG_ECMA(ERROR) << "Failed to open panda file.";
        return;
    }
    jsPandaFile_ = std::make_shared<JSPandaFile>(pf.release(), "");
}

void JSSymbolExtractor::CreateJSPandaFile(uint8_t *data, size_t dataSize)
{
    auto pf = panda_file::OpenPandaFileFromSecureMemory(data, dataSize);
    if (pf == nullptr) {
        LOG_ECMA(ERROR) << "Failed to open panda file.";
        return;
    }
    jsPandaFile_ = std::make_shared<JSPandaFile>(pf.release(), "");
}

SourceMap* JSSymbolExtractor::GetSourceMap(uint8_t *data, size_t dataSize)
{
    if (sourceMap_ == nullptr) {
        JSSymbolExtractor::CreateSourceMap(data, dataSize);
    }
    return sourceMap_.get();
}

void JSSymbolExtractor::CreateSourceMap(uint8_t *data, size_t dataSize)
{
    SourceMap sourcemap;
    sourcemap.Init(data, dataSize);
    sourceMap_ = std::make_shared<SourceMap>(sourcemap);
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
    return extractorptr;
}

bool ArkDestoryJSSymbolExtractor(uintptr_t extractorptr)
{
    auto extractor = reinterpret_cast<JSSymbolExtractor*>(extractorptr);
    return JSSymbolExtractor::Destory(extractor);
}

JSStackTrace *JSStackTrace::GetInstance()
{
    static JSStackTrace *jsStackTrace = new JSStackTrace();
    return jsStackTrace;
}

JSStackTrace::~JSStackTrace()
{
    methodInfo_.clear();
    methodInfos_.clear();
    jsPandaFiles_.clear();
}

bool JSStackTrace::AddMethodInfos(uintptr_t mapBase)
{
    auto pandaFile =
        JSPandaFileManager::GetInstance()->FindJSPandaFileByMapBase(mapBase);
    jsPandaFiles_[mapBase] = pandaFile;
    auto methodInfos = JSStackTrace::ReadAllMethodInfos(pandaFile);
    methodInfos_[mapBase] = std::move(methodInfos);
    if (pandaFile == nullptr) {
        LOG_ECMA(ERROR) << "Can't find JSPandaFile by mapBase: " << mapBase;
    }
    return true;
}

bool JSStackTrace::GetJsFrameInfo(uintptr_t byteCodePc, uintptr_t mapBase, uintptr_t loadOffset, JsFunction *jsFunction)
{
    bool ret = true;
    auto iter = methodInfos_.find(mapBase);
    if (iter == methodInfos_.end()) {
        ret = AddMethodInfos(mapBase);
    }
    loadOffset = loadOffset % PageSize();
    byteCodePc = byteCodePc - loadOffset;
    auto codeInfo = TranslateByteCodePc(byteCodePc, methodInfos_[mapBase]);
    if (!codeInfo) {
        LOG_ECMA(ERROR) << std::hex << "Failed to get methodId, pc: " << byteCodePc;
        return false;
    }
    auto methodId = EntityId(codeInfo->methodId);
    auto offset = codeInfo->offset;
    auto debugInfoExtractor =
        JSPandaFileManager::GetInstance()->GetJSPtExtractor(jsPandaFiles_[mapBase].get());
    ParseJsFrameInfo(jsPandaFiles_[mapBase].get(), debugInfoExtractor, methodId, offset, *jsFunction);
    jsFunction->codeBegin = byteCodePc - offset;
    jsFunction->codeSize = codeInfo->codeSize;
    return ret;
}

void JSStackTrace::Destory(JSStackTrace* trace)
{
    if (trace == nullptr) {
        return;
    }
    delete trace;
    trace = nullptr;
}

bool ArkParseJsFrameInfoLocal(uintptr_t byteCodePc, uintptr_t mapBase, uintptr_t loadOffset, JsFunction *jsFunction)
{
    bool ret =
        JSStackTrace::GetInstance()->GetJsFrameInfo(byteCodePc, mapBase, loadOffset, jsFunction);
    return ret;
}

void ArkDestoryLocal()
{
    auto trace = JSStackTrace::GetInstance();
    return JSStackTrace::Destory(trace);
}

} // namespace panda::ecmascript

__attribute__((visibility("default"))) int ark_create_js_symbol_extractor(uintptr_t *extractorptr)
{
    *extractorptr = panda::ecmascript::ArkCreateJSSymbolExtractor();
    return 1;
}

__attribute__((visibility("default"))) int ark_destory_js_symbol_extractor(uintptr_t extractorptr)
{
    if (panda::ecmascript::ArkDestoryJSSymbolExtractor(extractorptr)) {
        return 1;
    }
    return -1;
}

__attribute__((visibility("default"))) int ark_destory_local()
{
    panda::ecmascript::ArkDestoryLocal();
    return 1;
}

__attribute__((visibility("default"))) int step_ark(
    void *ctx, panda::ecmascript::ReadMemFunc readMem, uintptr_t *fp, uintptr_t *sp,
    uintptr_t *pc, uintptr_t *methodId, bool *isJsFrame)
{
    if (panda::ecmascript::StepArk(ctx, readMem, fp, sp, pc, methodId, isJsFrame)) {
        return 1;
    }
    return -1;
}

__attribute__((visibility("default"))) int ark_parse_js_frame_info(
    uintptr_t byteCodePc, uintptr_t methodId, uintptr_t mapBase, uintptr_t loadOffset, uint8_t *data,
    uint64_t dataSize, uintptr_t extractorptr, panda::ecmascript::JsFunction *jsFunction)
{
    if (panda::ecmascript::ArkParseJsFrameInfo(byteCodePc, methodId, mapBase, loadOffset, data,
                                               dataSize, extractorptr, jsFunction)) {
        return 1;
    }
    return -1;
}

__attribute__((visibility("default"))) int ark_parse_js_file_info(
    uintptr_t byteCodePc, uintptr_t methodId, uintptr_t mapBase, const char* filePath, uintptr_t extractorptr,
    panda::ecmascript::JsFunction *jsFunction)
{
    if (panda::ecmascript::ArkParseJSFileInfo(byteCodePc, methodId, mapBase, filePath, extractorptr, jsFunction)) {
        return 1;
    }
    return -1;
}

__attribute__((visibility("default"))) int ark_translate_js_frame_info(
    uint8_t *data, size_t dataSize, panda::ecmascript::JsFunction *jsFunction)
{
    if (panda::ecmascript::ArkTranslateJsFrameInfo(data, dataSize, jsFunction)) {
        return 1;
    }
    return -1;
}

__attribute__((visibility("default"))) int step_ark_managed_native_frame(
    int pid, uintptr_t *pc, uintptr_t *fp, uintptr_t *sp, char *buf, size_t buf_sz)
{
    if (panda::ecmascript::StepArkManagedNativeFrame(pid, pc, fp, sp, buf, buf_sz)) {
        return 1;
    }
    return -1;
}

__attribute__((visibility("default"))) int get_ark_js_heap_crash_info(
    int pid, uintptr_t *x20, uintptr_t *fp, int outJsInfo, char *buf, size_t buf_sz)
{
    if (panda::ecmascript::GetArkJSHeapCrashInfo(pid, x20, fp, outJsInfo != 0, buf, buf_sz)) {
        return 1;
    }
    return -1;
}

__attribute__((visibility("default"))) int get_ark_native_frame_info(
    int pid, uintptr_t *pc, uintptr_t *fp, uintptr_t *sp,
    panda::ecmascript::JsFrame *jsFrame, size_t &size)
{
    if (panda::ecmascript::GetArkNativeFrameInfo(pid, pc, fp, sp, jsFrame, size)) {
        return 1;
    }
    return -1;
}

__attribute__((visibility("default"))) int ark_parse_js_frame_info_local(
    uintptr_t byteCodePc, uintptr_t mapBase, uintptr_t loadOffset,
    panda::ecmascript::JsFunction *jsFunction)
{
    if (panda::ecmascript::ArkParseJsFrameInfoLocal(byteCodePc, mapBase, loadOffset, jsFunction)) {
        return 1;
    }
    return -1;
}