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
            profileInfo_->nodes[methodNode.parentId - 1].children.push_back(id);
        } else {
            previousId_ = methodNode.id = result->second;
        }
    }

    int sampleNodeId = previousId_ == 0 ? 1 : methodNode.id;
    int timeDelta = static_cast<int>(sampleTimeStamp -
        (threadStartTime_ == 0 ? profileInfo_->startTime : threadStartTime_));
    StatisticStateTime(timeDelta, previousState_);
    previousState_ = nodeKey.methodKey.state;
    profileInfo_->nodes[sampleNodeId - 1].hitCount++;
    profileInfo_->samples.push_back(sampleNodeId);
    profileInfo_->timeDeltas.push_back(timeDelta);
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
            profileInfo_->nodes[methodNode.parentId - 1].children.push_back(id);
        } else {
            previousId_ = methodNode.id = result->second;
        }
    }

    int sampleNodeId = previousId_ == 0 ? 1 : methodNode.id;
    *sampleTimeStamp = SamplingProcessor::GetMicrosecondsTimeStamp();
    int timeDelta = static_cast<int>(*sampleTimeStamp -
        (threadStartTime_ == 0 ? profileInfo_->startTime : threadStartTime_));
    StatisticStateTime(timeDelta, previousState_);
    previousState_ = nodeKey.methodKey.state;
    profileInfo_->nodes[sampleNodeId - 1].hitCount++;
    profileInfo_->samples.push_back(sampleNodeId);
    profileInfo_->timeDeltas.push_back(timeDelta);
    threadStartTime_ = *sampleTimeStamp;
}

void SamplesRecord::StringifySampleData()
{
    sampleData_ += "{\"tid\":"
        + std::to_string(profileInfo_->tid) + ",\"startTime\":"
        + std::to_string(profileInfo_->startTime) + ",\"endTime\":"
        + std::to_string(profileInfo_->stopTime) + ",";

    StringifyStateTimeStatistic();
    StringifyNodes();
    StringifySamples();
}

void SamplesRecord::StringifyStateTimeStatistic()
{
    sampleData_ += "\"gcTime\":"
        + std::to_string(profileInfo_->gcTime) + ",\"cInterpreterTime\":"
        + std::to_string(profileInfo_->cInterpreterTime) + ",\"asmInterpreterTime\":"
        + std::to_string(profileInfo_->asmInterpreterTime) + ",\"aotTime\":"
        + std::to_string(profileInfo_->aotTime) + ",\"builtinTime\":"
        + std::to_string(profileInfo_->builtinTime) + ",\"napiTime\":"
        + std::to_string(profileInfo_->napiTime) + ",\"arkuiEngineTime\":"
        + std::to_string(profileInfo_->arkuiEngineTime) + ",\"runtimeTime\":"
        + std::to_string(profileInfo_->runtimeTime) + ",\"otherTime\":"
        + std::to_string(profileInfo_->otherTime) + ",";
}

void SamplesRecord::StringifyNodes()
{
    sampleData_ += "\"nodes\":[";
    size_t nodeCount = profileInfo_->nodeCount;
    for (size_t i = 0; i < nodeCount; i++) {
        struct CpuProfileNode node = profileInfo_->nodes[i];
        struct FrameInfo codeEntry = node.codeEntry;
        sampleData_ += "{\"id\":"
        + std::to_string(node.id) + ",\"callFrame\":{\"functionName\":\""
        + codeEntry.functionName + "\",\"scriptId\":\""
        + std::to_string(codeEntry.scriptId) + "\",\"url\":\""
        + codeEntry.url + "\",\"lineNumber\":"
        + std::to_string(codeEntry.lineNumber) + ",\"columnNumber\":"
        + std::to_string(codeEntry.columnNumber) + "},\"hitCount\":"
        + std::to_string(node.hitCount) + ",\"children\":[";
        CVector<int> children = node.children;
        size_t childrenCount = children.size();
        for (size_t j = 0; j < childrenCount; j++) {
            sampleData_ += std::to_string(children[j]) + ",";
        }
        if (childrenCount > 0) {
            sampleData_.pop_back();
        }
        sampleData_ += "]},";
    }
    sampleData_.pop_back();
    sampleData_ += "],";
}

void SamplesRecord::StringifySamples()
{
    CVector<int> samples = profileInfo_->samples;
    CVector<int> timeDeltas = profileInfo_->timeDeltas;

    size_t samplesCount = samples.size();
    std::string samplesIdStr = "";
    std::string timeDeltasStr = "";
    for (size_t i = 0; i < samplesCount; i++) {
        samplesIdStr += std::to_string(samples[i]) + ",";
        timeDeltasStr += std::to_string(timeDeltas[i]) + ",";
    }
    samplesIdStr.pop_back();
    timeDeltasStr.pop_back();

    sampleData_ += "\"samples\":[" + samplesIdStr + "],\"timeDeltas\":[" + timeDeltasStr + "]}";
}

/*
 * Description: Finetune samples timedelta when stop cpuprofiler
 * Use two-pointer algorithm to iterate samples and actively recorded napiCallTimeVec_ and napiCallAddrVec_
 *     Accumulate timeDelta and startTime to get the current timestamp
 *     When current timestamp larger than napiCall start time, then
 *         Loop backward PRE_IDX_RANGE times from previous index, find same address napi
 *         If find the same address napi, then call FindSampleAndFinetune to finetune timedelta
 * Parameters: null
 * Return: null
 */
void SamplesRecord::FinetuneSampleData()
{
    CVector<int> &samples = profileInfo_->samples;

    size_t samplesCount = samples.size();           // samples count
    size_t napiCallCount = napiCallTimeVec_.size(); // napiCall count
    size_t sampleIdx = 0;                           // samples index
    size_t napiCallIdx = 0;                         // napiCall index
    uint64_t sampleTime = profileInfo_->startTime;  // current timestamp

    while (sampleIdx < samplesCount && napiCallIdx < napiCallCount) {
        // accumulate timeDelta to get current timestamp until larger than napiCall start time
        sampleTime += static_cast<uint64_t>(profileInfo_->timeDeltas[sampleIdx]);
        if (sampleTime < napiCallTimeVec_[napiCallIdx]) {
            sampleIdx++;
            continue;
        }
        bool findFlag = false;
        size_t findIdx = sampleIdx;
        size_t preIdx = sampleIdx;
        uint64_t preSampleTime = sampleTime -
            static_cast<uint64_t>(profileInfo_->timeDeltas[sampleIdx]); // preIdx's timestamp
        std::string napiFunctionAddr = napiCallAddrVec_[napiCallIdx];
        if (sampleIdx - 1 >= 0) {
            preIdx = sampleIdx - 1;
            preSampleTime -= static_cast<uint64_t>(profileInfo_->timeDeltas[sampleIdx - 1]);
        }
        // loop backward PRE_IDX_RANGE times from previous index, find same address napi
        for (size_t k = preIdx; k - preIdx < PRE_IDX_RANGE && k < samplesCount; k++) {
            std::string samplesFunctionName = profileInfo_->nodes[samples[k] - 1].codeEntry.functionName;
            preSampleTime += static_cast<uint64_t>(profileInfo_->timeDeltas[k]);
            if (samplesFunctionName.find(napiFunctionAddr) != std::string::npos) {
                findFlag = true;
                findIdx = k;
                break;
            }
        }
        // found the same address napi
        if (findFlag) {
            FindSampleAndFinetune(findIdx, napiCallIdx, sampleIdx, preSampleTime, sampleTime);
        } else {
            sampleTime -= static_cast<uint64_t>(profileInfo_->timeDeltas[sampleIdx]);
        }
        napiCallIdx += NAPI_CALL_SETP;
    }
}

/*
 * Description: Finetune samples timedelta when find the same address napi
 *      1. get a continuous sample: loop the samples until find the first sample that is different from findIdx
 *      2. startIdx and endIdx: beginning and end of the continuous sample
 *      3. call FinetuneTimeDeltas to finetune startIdx and endIdx's timedelta
 * Parameters:
 *      1. findIdx: sample index of same address with napi
 *      2. napiCallIdx: napi call index
 *      3. sampleIdx: sample index
 *      4. startSampleTime: start sample timestamp
 *      5. sampleTime: current timestamp
 * Return: null
 */
void SamplesRecord::FindSampleAndFinetune(size_t findIdx, size_t napiCallIdx, size_t &sampleIdx,
                                          uint64_t startSampleTime, uint64_t &sampleTime)
{
    size_t startIdx = findIdx;
    size_t endIdx = findIdx + 1;
    size_t samplesCount = profileInfo_->samples.size();
    uint64_t endSampleTime = startSampleTime;                 // end sample timestamp
    uint64_t startNapiTime = napiCallTimeVec_[napiCallIdx];   // call napi start timestamp
    uint64_t endNapiTime = napiCallTimeVec_[napiCallIdx + 1]; // call napi end timestamp
    // get a continuous sample, accumulate endSampleTime but lack last timeDeltas
    for (; endIdx < samplesCount && profileInfo_->samples[endIdx - 1] == profileInfo_->samples[endIdx]; endIdx++) {
        endSampleTime += static_cast<uint64_t>(profileInfo_->timeDeltas[endIdx]);
    }
    // finetune startIdx‘s timedelta
    FinetuneTimeDeltas(startIdx, startNapiTime, startSampleTime, false);
    // if the continuous sample' size is 1, endSampleTime need to adjust
    if (startIdx + 1 == endIdx) {
        endSampleTime -= (startSampleTime - startNapiTime);
    }
    // finetune endIdx's timedelta
    FinetuneTimeDeltas(endIdx, endNapiTime, endSampleTime, true);
    sampleTime = endSampleTime;
    sampleIdx = endIdx;
}

/*
 * Description: Finetune time deltas
 * Parameters:
 *      1. idx: sample index
 *      2. napiTime: napi timestamp
 *      3. sampleTime: sample timestamp
 *      4. isEndSample: if is endIdx, isEndSample is true, else isEndSample is false
 * Return: null
 */
void SamplesRecord::FinetuneTimeDeltas(size_t idx, uint64_t napiTime, uint64_t &sampleTime, bool isEndSample)
{
    // timeDeltas[idx] minus a period of time, timeDeltas[idx+1] needs to add the same time
    if (isEndSample) {
        // if is endIdx, sampleTime add endTimeDelta is real current timestamp
        int endTimeDelta = profileInfo_->timeDeltas[idx];
        profileInfo_->timeDeltas[idx] -= static_cast<int>(sampleTime - napiTime) + endTimeDelta;
        profileInfo_->timeDeltas[idx + 1] += static_cast<int>(sampleTime - napiTime) + endTimeDelta;
    } else {
        profileInfo_->timeDeltas[idx] -= static_cast<int>(sampleTime - napiTime);
        profileInfo_->timeDeltas[idx + 1] += static_cast<int>(sampleTime - napiTime);
    }

    // if timeDeltas[idx] < 0, timeDeltas[idx] = MIN_TIME_DELTA
    if (profileInfo_->timeDeltas[idx] < 0) {
        // timeDelta is added part, needs other sample reduce to balance
        int timeDelta = MIN_TIME_DELTA - profileInfo_->timeDeltas[idx];
        // profileInfo_->timeDeltas[idx - 1] to reduce
        if (idx - 1 >= 0 && profileInfo_->timeDeltas[idx - 1] > timeDelta) {
            profileInfo_->timeDeltas[idx - 1] -= timeDelta;
            if (isEndSample) {
                sampleTime -= static_cast<uint64_t>(timeDelta);
            }
        // if timeDeltas[idx - 1] < timeDelta, timeDeltas[idx - 1] = MIN_TIME_DELTA
        } else if (idx - 1 >= 0) {
            // The remaining timeDeltas[idx + 1] to reduce
            profileInfo_->timeDeltas[idx + 1] -= timeDelta - profileInfo_->timeDeltas[idx - 1] + MIN_TIME_DELTA;
            if (isEndSample) {
                sampleTime -= static_cast<uint64_t>(profileInfo_->timeDeltas[idx - 1] - MIN_TIME_DELTA);
            }
            profileInfo_->timeDeltas[idx - 1] = MIN_TIME_DELTA;
        } else {
            profileInfo_->timeDeltas[idx + 1] -= timeDelta;
        }
        profileInfo_->timeDeltas[idx] = MIN_TIME_DELTA;
    }

    // if timeDeltas[idx + 1] < 0, timeDeltas equals MIN_TIME_DELTA, timeDeltas[idx] reduce added part
    if (profileInfo_->timeDeltas[idx + 1] < 0) {
        int timeDelta = MIN_TIME_DELTA - profileInfo_->timeDeltas[idx];
        profileInfo_->timeDeltas[idx] -= timeDelta;
        profileInfo_->timeDeltas[idx + 1] = MIN_TIME_DELTA;
    }
}

int SamplesRecord::GetMethodNodeCount() const
{
    return profileInfo_->nodeCount;
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
            return temp.append("NAPI");
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

void SamplesRecord::ClearNapiCall()
{
    napiCallTimeVec_.clear();
    napiCallAddrVec_.clear();
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

void SamplesRecord::RecordCallNapiTime(uint64_t currentTime)
{
    napiCallTimeVec_.emplace_back(currentTime);
}

void SamplesRecord::RecordCallNapiAddr(const std::string &methodAddr)
{
    napiCallAddrVec_.emplace_back(methodAddr);
}
} // namespace panda::ecmascript
