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

#ifndef ECMASCRIPT_SAMPLES_RECORD_H
#define ECMASCRIPT_SAMPLES_RECORD_H

#include <atomic>
#include <ctime>
#include <fstream>
#include <cstring>
#include <semaphore.h>

#include "ecmascript/js_thread.h"
#include "ecmascript/mem/c_containers.h"

namespace panda::ecmascript {
const int MAX_ARRAY_COUNT = 100; // 100:the maximum size of the array
struct FrameInfo {
    std::string codeType = "";
    std::string functionName = "";
    int columnNumber = 0;
    int lineNumber = 0;
    int scriptId = 0;
    std::string url = "";
};
struct CpuProfileNode {
    int id = 0;
    int parentId = 0;
    int hitCount = 0;
    struct FrameInfo codeEntry;
    CVector<int> children;
};
struct ProfileInfo {
    uint64_t startTime = 0;
    uint64_t stopTime = 0;
    CVector<struct CpuProfileNode> nodes;
    CVector<int> samples;
    CVector<int> timeDeltas;
};
struct SampleInfo {
    int id = 0;
    int line = 0;
    int timeStamp = 0;
};

struct FrameInfoTemp {
    char codeType[20] = {0}; // 20:the maximum size of the codeType
    char functionName[50] = {0}; // 50:the maximum size of the functionName
    int columnNumber = 0;
    int lineNumber = 0;
    int scriptId = 0;
    char url[500] = {0}; // 500:the maximum size of the url
    JSMethod *method = nullptr;
};
struct MethodKey {
    JSMethod *method = nullptr;
    int parentId = 0;
    bool operator< (const MethodKey &methodKey) const
    {
        return parentId < methodKey.parentId || (parentId == methodKey.parentId && method < methodKey.method);
    }
};

class SamplesRecord {
public:
    explicit SamplesRecord();
    virtual ~SamplesRecord();

    void AddSample(uint64_t sampleTimeStamp, bool outToFile);
    void WriteMethodsAndSampleInfo(bool timeEnd);
    CVector<struct CpuProfileNode> GetMethodNodes() const;
    CDeque<struct SampleInfo> GetSamples() const;
    std::string GetSampleData() const;
    void SetThreadStartTime(uint64_t threadStartTime);
    void SetThreadStopTime(uint64_t threadStopTime);
    void SetStartsampleData(std::string sampleData);
    void SetFileName(std::string &fileName);
    const std::string GetFileName() const;
    void ClearSampleData();
    std::unique_ptr<struct ProfileInfo> GetProfileInfo();
    bool GetIsStart() const;
    void SetIsStart(bool isStart);
    bool GetGcState() const;
    void SetGcState(bool gcState);
    void SetSampleFlag(bool sampleFlag);
    int SemInit(int index, int pshared, int value);
    int SemPost(int index);
    int SemWait(int index);
    int SemDestroy(int index);
    const CMap<JSMethod *, struct FrameInfo> &GetStackInfo() const;
    void InsertStackInfo(JSMethod *method, struct FrameInfo &codeEntry);
    void PushFrameStack(JSMethod *method, int count);
    void PushStackInfo(const FrameInfoTemp &frameInfoTemp, int index);
    std::ofstream fileHandle_;
private:
    void WriteAddNodes();
    void WriteAddSamples();
    struct FrameInfo GetMethodInfo(JSMethod *method);
    struct FrameInfo GetGcInfo();
    void FrameInfoTempToMap();

    int previousId_ = 0;
    uint64_t threadStartTime_ = 0;
    std::atomic_bool isLastSample_ = false;
    std::atomic_bool gcState_ = false;
    std::atomic_bool isStart_ = false;
    std::unique_ptr<struct ProfileInfo> profileInfo_;
    CVector<int> stackTopLines_;
    CMap<struct MethodKey, int> methodMap_;
    CDeque<struct SampleInfo> samples_;
    std::string sampleData_ = "";
    std::string fileName_ = "";
    sem_t sem_[2]; // 2 : sem_ size is two.
    CMap<JSMethod *, struct FrameInfo> stackInfoMap_;
    JSMethod *frameStack_[MAX_ARRAY_COUNT] = {};
    int frameStackLength_ = 0;
    CMap<std::string, int> scriptIdMap_;
    FrameInfoTemp frameInfoTemps_[MAX_ARRAY_COUNT] = {};
    int frameInfoTempLength_ = 0;
};
} // namespace panda::ecmascript
#endif // ECMASCRIPT_SAMPLES_RECORD_H