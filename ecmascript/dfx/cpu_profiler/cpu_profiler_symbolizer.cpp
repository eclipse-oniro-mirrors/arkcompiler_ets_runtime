/**
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

#include "ecmascript/dfx/cpu_profiler/cpu_profiler_symbolizer.h"

#include <cstdio>
#include <cstring>

#include "ecmascript/jspandafile/debug_info_extractor.h"
#include "ecmascript/jspandafile/js_pandafile_manager.h"
#include "securec.h"

namespace panda::ecmascript {
void CpuProfilerSymbolizer::BeginSession()
{
    metadataCursor_ = 0;
    liveFiles_.clear();
}

void CpuProfilerSymbolizer::SeedLiveFile(const JSPandaFile *file)
{
    liveFiles_.insert(file);
}

void CpuProfilerSymbolizer::ApplyEvent(const ProfilerMetadataRegistry::Event &event)
{
    if (event.isDelete) {
        liveFiles_.erase(event.file);
    } else {
        liveFiles_.insert(event.file);
    }
}

bool CpuProfilerSymbolizer::IsJSPandaFileLive(uintptr_t jsPandaFile) const
{
    return liveFiles_.find(reinterpret_cast<const JSPandaFile *>(jsPandaFile)) != liveFiles_.end();
}

// Call line of a native frame, resolved from the caller frame captured in the same
// raw sample (mirrors the previous JsStackGetter::GetCallLineNumber data flow).
int CpuProfilerSymbolizer::ResolveNativeCallLine(const RawSample &sample, uint16_t frameIndex) const
{
    if (frameIndex + 1 >= sample.frameCount) {
        return 0;
    }
    const RawFrame &caller = sample.frames[frameIndex + 1];
    if (caller.kind != RawFrameKind::INTERPRETER || caller.jsPandaFile == 0) {
        return 0;
    }
    MethodLiteral *methodLiteral = reinterpret_cast<MethodLiteral *>(caller.value);
    const JSPandaFile *jsPandaFile = reinterpret_cast<const JSPandaFile *>(caller.jsPandaFile);
    DebugInfoExtractor *debugExtractor = JSPandaFileManager::GetInstance()->GetJSPtExtractor(jsPandaFile);
    if (debugExtractor == nullptr) {
        return 0;
    }
    int lineNumber = 0;
    auto callbackLineFunc = [&lineNumber](int32_t line) -> bool {
        lineNumber = line + 1;
        return true;
    };
    if (!debugExtractor->MatchLineWithOffset(callbackLineFunc, methodLiteral->GetMethodId(),
                                             caller.bytecodeOffset)) {
        return 0;
    }
    return lineNumber;
}

bool CpuProfilerSymbolizer::SymbolizeFrame(const RawSample &sample, uint16_t frameIndex,
                                           struct MethodKey &methodKey, FrameInfoTemp &codeEntry)
{
    codeEntry.methodKey = methodKey;
    const RawFrame &rawFrame = sample.frames[frameIndex];
    if (rawFrame.kind == RawFrameKind::BUILTIN) {
        return SymbolizeBuiltinFrame(sample, frameIndex, methodKey, codeEntry);
    }
    return SymbolizeBytecodeFrame(rawFrame, codeEntry);
}

bool CpuProfilerSymbolizer::SymbolizeBuiltinFrame(const RawSample &sample, uint16_t frameIndex,
                                                  struct MethodKey &methodKey, FrameInfoTemp &codeEntry)
{
    const RawFrame &rawFrame = sample.frames[frameIndex];
    // Phase 1 (plan §7.3): the native name property is no longer read anywhere;
    // unregistered callbacks are shown as an address placeholder.
    int len = snprintf_s(codeEntry.functionName, sizeof(codeEntry.functionName),
        sizeof(codeEntry.functionName) - 1, "(native) 0x%lx",
        static_cast<unsigned long>(rawFrame.value));
    if (len <= 0 || static_cast<size_t>(len) >= sizeof(codeEntry.functionName) - 1) {
        return false;
    }
    if (!FillCallerSource(sample, frameIndex, codeEntry)) {
        return false;
    }
    codeEntry.lineNumber = methodKey.lineNumber;
    return true;
}

bool CpuProfilerSymbolizer::FillCallerSource(const RawSample &sample, uint16_t frameIndex,
                                             FrameInfoTemp &codeEntry)
{
    if (frameIndex + 1 >= sample.frameCount) {
        return true;
    }
    const RawFrame &caller = sample.frames[frameIndex + 1];
    if (caller.kind != RawFrameKind::INTERPRETER || caller.jsPandaFile == 0) {
        return true;
    }
    MethodLiteral *methodLiteral = reinterpret_cast<MethodLiteral *>(caller.value);
    const JSPandaFile *jsPandaFile = reinterpret_cast<const JSPandaFile *>(caller.jsPandaFile);
    DebugInfoExtractor *debugExtractor = JSPandaFileManager::GetInstance()->GetJSPtExtractor(jsPandaFile);
    if (debugExtractor == nullptr) {
        return true;
    }
    panda_file::File::EntityId methodId = methodLiteral->GetMethodId();
    const std::string &sourceFile = debugExtractor->GetSourceFile(methodId);
    if (!sourceFile.empty() &&
        !JsStackGetter::CheckAndCopy(codeEntry.url, sizeof(codeEntry.url), sourceFile.c_str())) {
        return false;
    }
    debugExtractor->MatchColumnWithOffset(
        [&codeEntry](int32_t column) -> bool {
            codeEntry.columnNumber = column + 1;
            return true;
        }, methodId, caller.bytecodeOffset);
    return true;
}

bool CpuProfilerSymbolizer::SymbolizeBytecodeFrame(const RawFrame &rawFrame, FrameInfoTemp &codeEntry)
{
    // INTERPRETER/AOT/JIT: value is a MethodLiteral* captured at sample time.
    MethodLiteral *methodLiteral = reinterpret_cast<MethodLiteral *>(rawFrame.value);
    const JSPandaFile *jsPandaFile = reinterpret_cast<const JSPandaFile *>(rawFrame.jsPandaFile);
    if (jsPandaFile == nullptr) {
        return false;
    }
    EntityId methodId = methodLiteral->GetMethodId();
    const char *functionName = MethodLiteral::GetMethodName(jsPandaFile, methodId, true);
    uint8_t length = strlen(functionName);
    if (length != 0 && functionName[0] == '#') {
        uint8_t index = length - 1;
        while (functionName[index] != '#') {
            index--;
        }
        functionName += (index + 1);  // #...#functionName
    }
    if (strlen(functionName) == 0) {
        functionName = "anonymous";
    }
    if (!JsStackGetter::CheckAndCopy(codeEntry.functionName, sizeof(codeEntry.functionName), functionName)) {
        return false;
    }
    uint8_t specialIndex = strlen(codeEntry.functionName) - 1;
    while (specialIndex > 0 && codeEntry.functionName[specialIndex] != '^') {
        specialIndex--;
    }
    if (codeEntry.functionName[specialIndex] == '^') {
        codeEntry.functionName[specialIndex] = '\0';  // #...#functionName^1
    }
    const char *recordName = MethodLiteral::GetRecordNameWithSymbol(jsPandaFile, methodId);
    if (strlen(recordName) != 0) {
        if (!JsStackGetter::CheckAndCopy(codeEntry.recordName, sizeof(codeEntry.recordName), recordName)) {
            return false;
        }
    }

    DebugInfoExtractor *debugExtractor = JSPandaFileManager::GetInstance()->GetJSPtExtractor(jsPandaFile);
    if (debugExtractor == nullptr) {
        return false;
    }
    const std::string &sourceFile = debugExtractor->GetSourceFile(methodId);
    if (!sourceFile.empty()) {
        if (!JsStackGetter::CheckAndCopy(codeEntry.url, sizeof(codeEntry.url), sourceFile.c_str())) {
            return false;
        }
    }
    codeEntry.lineNumber = debugExtractor->GetFristLine(methodId);
    codeEntry.columnNumber = debugExtractor->GetFristColumn(methodId);
    return true;
}
} // namespace panda::ecmascript
