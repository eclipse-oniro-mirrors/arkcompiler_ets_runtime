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

#include "ecmascript/dfx/cpu_profiler/samples_record.h"

#include <climits>
#include <sys/syscall.h>
#include <unistd.h>

#include "ecmascript/dfx/cpu_profiler/cpu_profiler.h"
#include "ecmascript/dfx/cpu_profiler/sampling_processor.h"
#include "ecmascript/ecma_vm.h"
#include "ecmascript/interpreter/interpreter.h"
#include "ecmascript/method.h"

namespace panda::ecmascript {
SamplesRecord::SamplesRecord()
{
    stackTopLines_.push_back(0);
    struct NodeKey nodeKey;
    struct CpuProfileNode methodNode;
    struct MethodKey methodKey;
    methodKey.methodIdentifier = reinterpret_cast<void *>(INT_MAX - 1);
    nodeKey.methodKey = methodKey;
    nodeMap_.emplace(nodeKey, nodeMap_.size() + 1);
    methodNode.parentId = 0;
    methodNode.codeEntry.codeType = "JS";
    methodNode.codeEntry.functionName = "(root)";
    methodNode.id = 1;
    profileInfo_ = std::make_unique<struct ProfileInfo>();
    profileInfo_->nodes[profileInfo_->nodeCount++] = methodNode;
    int tid = syscall(SYS_gettid);
    if (tid != -1) {
        profileInfo_->tid = static_cast<uint64_t>(tid);
    }
}

SamplesRecord::~SamplesRecord()
{
    if (fileHandle_.is_open()) {
        fileHandle_.close();
    }
}

void SamplesRecord::AddSample(uint64_t sampleTimeStamp)
{
    if (isBreakSample_.load()) {
        frameStackLength_ = 0;
        frameInfoTempLength_ = 0;
        return;
    }
    FrameInfoTempToMap();
    struct NodeKey nodeKey;
    struct CpuProfileNode methodNode;
        if (frameStackLength_ != 0) {
            frameStackLength_--;
        }
        methodNode.id = 1;
        for (; frameStackLength_ >= 1; frameStackLength_--) {
            nodeKey.methodKey = frameStack_[frameStackLength_ - 1];
            methodNode.parentId = nodeKey.parentId = methodNode.id;
            auto result = nodeMap_.find(nodeKey);
            if (result == nodeMap_.end()) {
                int id = static_cast<int>(nodeMap_.size() + 1);
                nodeMap_.emplace(nodeKey, id);
                previousId_ = methodNode.id = id;
                methodNode.codeEntry = GetMethodInfo(nodeKey.methodKey);
                stackTopLines_.push_back(methodNode.codeEntry.lineNumber);
                profileInfo_->nodes[profileInfo_->nodeCount++] = methodNode;
                if (!outToFile_) {
                    profileInfo_->nodes[methodNode.parentId - 1].children.push_back(id);
                }
            } else {
                previousId_ = methodNode.id = result->second;
            }
        }

    struct SampleInfo sampleInfo;
    int sampleNodeId = previousId_ == 0 ? 1 : methodNode.id;
    int timeDelta = static_cast<int>(sampleTimeStamp -
        (threadStartTime_ == 0 ? profileInfo_->startTime : threadStartTime_));
    StatisticStateTime(timeDelta, nodeKey.methodKey.state);
    if (outToFile_) {
        sampleInfo.id = sampleNodeId;
        sampleInfo.line = stackTopLines_[methodNode.id - 1];
        sampleInfo.timeStamp = timeDelta;
        samples_.push_back(sampleInfo);
    } else {
        profileInfo_->nodes[sampleNodeId - 1].hitCount++;
        profileInfo_->samples.push_back(sampleNodeId);
        profileInfo_->timeDeltas.push_back(timeDelta);
    }
    threadStartTime_ = sampleTimeStamp;
}

void SamplesRecord::AddSampleCallNapi(uint64_t *sampleTimeStamp)
{
    NapiFrameInfoTempToMap();
    struct NodeKey nodeKey;
    struct CpuProfileNode methodNode;
    int napiFrameStackLength = static_cast<int>(napiFrameStack_.size());
    if (napiFrameStackLength == 0) {
        return;
    }
    methodNode.id = 1;
    napiFrameStackLength--;
    for (; napiFrameStackLength >= 1; napiFrameStackLength--) {
        nodeKey.methodKey = napiFrameStack_[napiFrameStackLength - 1];
        methodNode.parentId = nodeKey.parentId = methodNode.id;
        auto result = nodeMap_.find(nodeKey);
        if (result == nodeMap_.end()) {
            int id = static_cast<int>(nodeMap_.size() + 1);
            nodeMap_.emplace(nodeKey, id);
            previousId_ = methodNode.id = id;
            methodNode.codeEntry = GetMethodInfo(nodeKey.methodKey);
            stackTopLines_.push_back(methodNode.codeEntry.lineNumber);
            profileInfo_->nodes[profileInfo_->nodeCount++] = methodNode;
            if (!outToFile_) {
                profileInfo_->nodes[methodNode.parentId - 1].children.push_back(id);
            }
        } else {
            previousId_ = methodNode.id = result->second;
        }
    }
    struct SampleInfo sampleInfo;
    int sampleNodeId = previousId_ == 0 ? 1 : methodNode.id;
    *sampleTimeStamp = SamplingProcessor::GetMicrosecondsTimeStamp();
    int timeDelta = static_cast<int>(*sampleTimeStamp -
        (threadStartTime_ == 0 ? profileInfo_->startTime : threadStartTime_));
    StatisticStateTime(timeDelta, nodeKey.methodKey.state);
    if (outToFile_) {
        sampleInfo.id = sampleNodeId;
        sampleInfo.line = stackTopLines_[methodNode.id - 1];
        sampleInfo.timeStamp = timeDelta;
        samples_.push_back(sampleInfo);
    } else {
        profileInfo_->nodes[sampleNodeId - 1].hitCount++;
        profileInfo_->samples.push_back(sampleNodeId);
        profileInfo_->timeDeltas.push_back(timeDelta);
    }
    threadStartTime_ = *sampleTimeStamp;
}

void SamplesRecord::WriteAddNodes()
{
    sampleData_ += "{\"args\":{\"data\":{\"cpuProfile\":{\"nodes\":[";
    int count = profileInfo_->nodeCount;
    for (int i = 0; i < count; i++) {
        auto it = profileInfo_->nodes[i];
        sampleData_ += "{\"callFrame\":{\"codeType\":\"" + it.codeEntry.codeType + "\",";
        if (it.parentId == 0) {
            sampleData_ += "\"functionName\":\"(root)\",\"scriptId\":0},\"id\":1},";
            continue;
        }
        if (it.codeEntry.codeType == "other" || it.codeEntry.codeType == "jsvm") {
            sampleData_ += "\"functionName\":\"" + it.codeEntry.functionName + "\",\"scriptId\":" +
                           std::to_string(it.codeEntry.scriptId) + "},\"id\":" + std::to_string(it.id);
        } else {
            sampleData_ += "\"columnNumber\":" + std::to_string(it.codeEntry.columnNumber) +
                           ",\"functionName\":\"" + it.codeEntry.functionName + "\",\"lineNumber\":\"" +
                           std::to_string(it.codeEntry.lineNumber) + "\",\"scriptId\":" +
                           std::to_string(it.codeEntry.scriptId) + ",\"url\":\"" + it.codeEntry.url +
                           "\"},\"id\":" + std::to_string(it.id);
        }
        sampleData_ += ",\"parent\":" + std::to_string(it.parentId) + "},";
    }
    sampleData_.pop_back();
    sampleData_ += "],\"samples\":[";
}

void SamplesRecord::WriteAddSamples()
{
    if (samples_.empty()) {
        return;
    }
    std::string sampleId = "";
    std::string sampleLine = "";
    std::string timeStamp = "";
    for (auto it : samples_) {
        sampleId += std::to_string(it.id) + ",";
        sampleLine += std::to_string(it.line) + ",";
        timeStamp += std::to_string(it.timeStamp) + ",";
    }
    sampleId.pop_back();
    sampleLine.pop_back();
    timeStamp.pop_back();
    sampleData_ += sampleId + "]},\"lines\":[" + sampleLine + "],\"timeDeltas\":[" + timeStamp + "]}},";
}

void SamplesRecord::WriteMethodsAndSampleInfo(bool timeEnd)
{
    if (profileInfo_->nodeCount >= 10) { // 10:Number of nodes currently stored
        WriteAddNodes();
        WriteAddSamples();
        profileInfo_->nodeCount = 0;
        samples_.clear();
    } else if (samples_.size() == 100 || timeEnd) { // 100:Number of samples currently stored
        if (!(profileInfo_->nodeCount == 0)) {
            WriteAddNodes();
            WriteAddSamples();
            profileInfo_->nodeCount = 0;
            samples_.clear();
        } else if (!samples_.empty()) {
            sampleData_ += "{\"args\":{\"data\":{\"cpuProfile\":{\"samples\":[";
            WriteAddSamples();
            samples_.clear();
        } else {
            return;
        }
    }
    sampleData_ += "\"cat\":\"disabled-by-default-ark.cpu_profiler\",\"id\":"
                    "\"0x2\",\"name\":\"ProfileChunk\",\"ph\":\"P\",\"pid\":";
    pid_t pid = getpid();
    int64_t tid = syscall(SYS_gettid);
    uint64_t tts = SamplingProcessor::GetMicrosecondsTimeStamp();
    sampleData_ += std::to_string(pid) + ",\"tid\":" +
                   std::to_string(tid) + ",\"ts\":" +
                   std::to_string(threadStartTime_) + ",\"tts\":" +
                   std::to_string(tts) + "},\n";
}

void SamplesRecord::WriteStateTimeStatistic()
{
    pid_t pid = getpid();
    int64_t tid = syscall(SYS_gettid);
    uint64_t tts = SamplingProcessor::GetMicrosecondsTimeStamp();
    sampleData_ += "{\"args\":{\"data\":{\"stopTime\":" + std::to_string(threadStartTime_) + "}},"
        + "\"cat\":\"disabled-by-default-ark.cpu_profiler\",\"id\":\"0x2\","
        + "\"name\":\"Profile\",\"ph\":\"P\",\"pid\":"
        + std::to_string(pid) + ",\"tid\":"
        + std::to_string(tid) + ",\"ts\":"
        + std::to_string(threadStartTime_) + ",\"tts\":"
        + std::to_string(tts) + ",\"gcTime\":"
        + std::to_string(profileInfo_->gcTime) + ",\"cInterpreterTime\":"
        + std::to_string(profileInfo_->cInterpreterTime) + ",\"asmInterpreterTime\":"
        + std::to_string(profileInfo_->asmInterpreterTime) + ",\"aotTime\":"
        + std::to_string(profileInfo_->aotTime) + ",\"builtinTime\":"
        + std::to_string(profileInfo_->builtinTime) + ",\"napiTime\":"
        + std::to_string(profileInfo_->napiTime) + ",\"arkuiEngineTime\":"
        + std::to_string(profileInfo_->arkuiEngineTime) + ",\"runtimeTime\":"
        + std::to_string(profileInfo_->runtimeTime) + ",\"otherTime\":"
        + std::to_string(profileInfo_->otherTime) + "}]\n";
}

int SamplesRecord::GetMethodNodeCount() const
{
    return profileInfo_->nodeCount;
}

CDeque<struct SampleInfo> SamplesRecord::GetSamples() const
{
    return samples_;
}

std::string SamplesRecord::GetSampleData() const
{
    return sampleData_;
}

struct FrameInfo SamplesRecord::GetMethodInfo(struct MethodKey &methodKey)
{
    struct FrameInfo entry;
    auto iter = stackInfoMap_.find(methodKey);
    if (iter != stackInfoMap_.end()) {
        entry = iter->second;
    }
    return entry;
}

std::string SamplesRecord::AddRunningStateToName(char *functionName, RunningState state)
{
    std::string temp = functionName;
    switch (state) {
        case RunningState::GC:
            return temp.append("(GC)");
        case RunningState::CINT:
            return temp.append("(CINT)");
        case RunningState::AINT:
            return temp.append("(AINT)");
        case RunningState::AOT:
            return temp.append("(AOT)");
        case RunningState::BUILTIN:
            return temp.append("(BUILTIN)");
        case RunningState::NAPI:
            return temp.append("");
        case RunningState::ARKUI_ENGINE:
            return temp.append("(ARKUI_ENGINE)");
        case RunningState::RUNTIME:
            return temp.append("(RUNTIME)");
        default:
            return temp.append("(OTHER)");
    }
}

void SamplesRecord::StatisticStateTime(int timeDelta, RunningState state)
{
    switch (state) {
        case RunningState::GC: {
            profileInfo_->gcTime += static_cast<uint64_t>(timeDelta);
            return;
        }
        case RunningState::CINT: {
            profileInfo_->cInterpreterTime += static_cast<uint64_t>(timeDelta);
            return;
        }
        case RunningState::AINT: {
            profileInfo_->asmInterpreterTime += static_cast<uint64_t>(timeDelta);
            return;
        }
        case RunningState::AOT: {
            profileInfo_->aotTime += static_cast<uint64_t>(timeDelta);
            return;
        }
        case RunningState::BUILTIN: {
            profileInfo_->builtinTime += static_cast<uint64_t>(timeDelta);
            return;
        }
        case RunningState::NAPI: {
            profileInfo_->napiTime += static_cast<uint64_t>(timeDelta);
            return;
        }
        case RunningState::ARKUI_ENGINE: {
            profileInfo_->arkuiEngineTime += static_cast<uint64_t>(timeDelta);
            return;
        }
        case RunningState::RUNTIME: {
            profileInfo_->runtimeTime += static_cast<uint64_t>(timeDelta);
            return;
        }
        default: {
            profileInfo_->otherTime += static_cast<uint64_t>(timeDelta);
            return;
        }
    }
}

struct FrameInfo SamplesRecord::GetGcInfo()
{
    struct FrameInfo gcEntry;
    gcEntry.codeType = "jsvm";
    gcEntry.functionName = "garbage collector";
    return gcEntry;
}

void SamplesRecord::SetThreadStartTime(uint64_t threadStartTime)
{
    profileInfo_->startTime = threadStartTime;
}

void SamplesRecord::SetThreadStopTime(uint64_t threadStopTime)
{
    profileInfo_->stopTime = threadStopTime;
}

void SamplesRecord::SetStartsampleData(std::string sampleData)
{
    sampleData_ += sampleData;
}

void SamplesRecord::SetFileName(std::string &fileName)
{
    fileName_ = fileName;
}

const std::string SamplesRecord::GetFileName() const
{
    return fileName_;
}

void SamplesRecord::ClearSampleData()
{
    sampleData_.clear();
}

std::unique_ptr<struct ProfileInfo> SamplesRecord::GetProfileInfo()
{
    return std::move(profileInfo_);
}

void SamplesRecord::SetIsBreakSampleFlag(bool sampleFlag)
{
    isBreakSample_.store(sampleFlag);
}

void SamplesRecord::SetBeforeGetCallNapiStackFlag(bool flag)
{
    beforeCallNapi_.store(flag);
}

bool SamplesRecord::GetBeforeGetCallNapiStackFlag()
{
    return beforeCallNapi_.load();
}

void SamplesRecord::SetAfterGetCallNapiStackFlag(bool flag)
{
    afterCallNapi_.store(flag);
}

bool SamplesRecord::GetAfterGetCallNapiStackFlag()
{
    return afterCallNapi_.load();
}

void SamplesRecord::SetCallNapiFlag(bool flag)
{
    callNapi_.store(flag);
}

bool SamplesRecord::GetCallNapiFlag()
{
    return callNapi_.load();
}

int SamplesRecord::SemInit(int index, int pshared, int value)
{
    return sem_init(&sem_[index], pshared, value);
}

int SamplesRecord::SemPost(int index)
{
    return sem_post(&sem_[index]);
}

int SamplesRecord::SemWait(int index)
{
    return sem_wait(&sem_[index]);
}

int SamplesRecord::SemDestroy(int index)
{
    return sem_destroy(&sem_[index]);
}

const CMap<struct MethodKey, struct FrameInfo> &SamplesRecord::GetStackInfo() const
{
    return stackInfoMap_;
}

void SamplesRecord::InsertStackInfo(struct MethodKey &methodKey, struct FrameInfo &codeEntry)
{
    stackInfoMap_.emplace(methodKey, codeEntry);
}

bool SamplesRecord::PushFrameStack(struct MethodKey &methodKey)
{
    if (UNLIKELY(frameStackLength_ >= MAX_ARRAY_COUNT)) {
        return false;
    }
    frameStack_[frameStackLength_++] = methodKey;
    return true;
}

bool SamplesRecord::PushNapiFrameStack(struct MethodKey &methodKey)
{
    if (UNLIKELY(napiFrameStack_.size() >= MAX_ARRAY_COUNT)) {
        return false;
    }
    napiFrameStack_.push_back(methodKey);
    return true;
}

void SamplesRecord::ClearNapiStack()
{
    napiFrameStack_.clear();
    napiFrameInfoTemps_.clear();
}

int SamplesRecord::GetNapiFrameStackLength()
{
    return napiFrameStack_.size();
}

bool SamplesRecord::GetGcState() const
{
    return gcState_.load();
}

void SamplesRecord::SetGcState(bool gcState)
{
    gcState_.store(gcState);
}

bool SamplesRecord::GetRuntimeState() const
{
    return runtimeState_.load();
}

void SamplesRecord::SetRuntimeState(bool runtimeState)
{
    runtimeState_.store(runtimeState);
}

bool SamplesRecord::GetIsStart() const
{
    return isStart_.load();
}

void SamplesRecord::SetIsStart(bool isStart)
{
    isStart_.store(isStart);
}

bool SamplesRecord::GetOutToFile() const
{
    return outToFile_;
}

void SamplesRecord::SetOutToFile(bool outToFile)
{
    outToFile_ = outToFile;
}

bool SamplesRecord::PushStackInfo(const FrameInfoTemp &frameInfoTemp)
{
    if (UNLIKELY(frameInfoTempLength_ >= MAX_ARRAY_COUNT)) {
        return false;
    }
    frameInfoTemps_[frameInfoTempLength_++] = frameInfoTemp;
    return true;
}

bool SamplesRecord::PushNapiStackInfo(const FrameInfoTemp &frameInfoTemp)
{
    if (UNLIKELY(napiFrameInfoTemps_.size() == MAX_ARRAY_COUNT)) {
        return false;
    }
    napiFrameInfoTemps_.push_back(frameInfoTemp);
    return true;
}

void SamplesRecord::FrameInfoTempToMap()
{
    if (frameInfoTempLength_ == 0) {
        return;
    }
    struct FrameInfo frameInfo;
    for (int i = 0; i < frameInfoTempLength_; ++i) {
        frameInfo.url = frameInfoTemps_[i].url;
        auto iter = scriptIdMap_.find(frameInfo.url);
        if (iter == scriptIdMap_.end()) {
            scriptIdMap_.emplace(frameInfo.url, scriptIdMap_.size() + 1);
            frameInfo.scriptId = static_cast<int>(scriptIdMap_.size());
        } else {
            frameInfo.scriptId = iter->second;
        }
        if (i == 0 && frameInfoTemps_[0].methodKey.state != RunningState::OTHER) {
            frameInfo.functionName = AddRunningStateToName(frameInfoTemps_[0].functionName,
                                                           frameInfoTemps_[0].methodKey.state);
        } else {
            frameInfo.functionName = frameInfoTemps_[i].functionName;
        }
        frameInfo.codeType = frameInfoTemps_[i].codeType;
        frameInfo.columnNumber = frameInfoTemps_[i].columnNumber;
        frameInfo.lineNumber = frameInfoTemps_[i].lineNumber;
        stackInfoMap_.emplace(frameInfoTemps_[i].methodKey, frameInfo);
    }
    frameInfoTempLength_ = 0;
}

void SamplesRecord::NapiFrameInfoTempToMap()
{
    size_t length = napiFrameInfoTemps_.size();
    if (length == 0) {
        return;
    }
    struct FrameInfo frameInfo;
    for (size_t i = 0; i < length; ++i) {
        frameInfo.url = napiFrameInfoTemps_[i].url;
        auto iter = scriptIdMap_.find(frameInfo.url);
        if (iter == scriptIdMap_.end()) {
            scriptIdMap_.emplace(frameInfo.url, scriptIdMap_.size() + 1);
            frameInfo.scriptId = static_cast<int>(scriptIdMap_.size());
        } else {
            frameInfo.scriptId = iter->second;
        }
        if (i == 0 && napiFrameInfoTemps_[0].methodKey.state == RunningState::NAPI) {
            frameInfo.functionName = AddRunningStateToName(napiFrameInfoTemps_[0].functionName,
                                                           napiFrameInfoTemps_[0].methodKey.state);
        } else {
            frameInfo.functionName = napiFrameInfoTemps_[i].functionName;
        }
        frameInfo.codeType = napiFrameInfoTemps_[i].codeType;
        frameInfo.columnNumber = napiFrameInfoTemps_[i].columnNumber;
        frameInfo.lineNumber = napiFrameInfoTemps_[i].lineNumber;
        stackInfoMap_.emplace(napiFrameInfoTemps_[i].methodKey, frameInfo);
    }
}

int SamplesRecord::GetframeStackLength() const
{
    return frameStackLength_;
}
} // namespace panda::ecmascript
