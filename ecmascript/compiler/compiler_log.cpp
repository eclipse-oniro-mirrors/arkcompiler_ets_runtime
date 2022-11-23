/*
 * Copyright (c) 2022 Huawei Device Co., Ltd.
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

#include "ecmascript/compiler/compiler_log.h"

namespace panda::ecmascript::kungfu {
CompilerLog::CompilerLog(const std::string &logOpt, bool enableBCTrace)
{
    outputCIR_ = logOpt.find("cir") != std::string::npos ||
        logOpt.find("0") != std::string::npos;
    outputLLIR_ = logOpt.find("llir") != std::string::npos ||
        logOpt.find("1") != std::string::npos;
    outputASM_ = logOpt.find("asm") != std::string::npos ||
        logOpt.find("2") != std::string::npos;
    outputType_ = logOpt.find("type") != std::string::npos ||
        logOpt.find("3") != std::string::npos;
    allMethod_ = logOpt.find("all") != std::string::npos;
    cerMethod_ = logOpt.find("all") == std::string::npos &&
        logOpt.find("cer") != std::string::npos;
    noneMethod_ = logOpt.find("all") == std::string::npos &&
        logOpt.find("cer") == std::string::npos;
    enableBCTrace_ = enableBCTrace;
}

bool MethodLogList::IncludesMethod(const std::string &methodName) const
{
    bool empty = methodName.empty();
    bool found = methods_.find(methodName) != std::string::npos;
    return !empty && found;
}

bool AotMethodLogList::IncludesMethod(const std::string &fileName, const std::string &methodName) const
{
    if (fileMethods_.find(fileName) == fileMethods_.end()) {
        return false;
    }
    std::vector mehtodVector = fileMethods_.at(fileName);
    auto it = find(mehtodVector.begin(), mehtodVector.end(), methodName);
    return (it != mehtodVector.end());
}

std::vector<std::string> AotMethodLogList::spiltString(const std::string &str, const char ch)
{
    std::vector<std::string> vec {};
    std::istringstream sstr(str.c_str());
    std::string spilt;
    while (getline(sstr, spilt, ch)) {
        vec.emplace_back(spilt);
    }
    return vec;
}

void AotMethodLogList::ParseFileMethodsName(const std::string &logMethods)
{
    std::vector<std::string> fileVector = spiltString(logMethods, fileSplitSign);
    std::vector<std::string> itemVector;
    for (size_t index = 0; index < fileVector.size(); ++index) {
        itemVector = spiltString(fileVector[index], methodSplitSign);
        std::vector<std::string> methodVector(itemVector.begin() + 1, itemVector.end());
        fileMethods_[itemVector[0]] = methodVector;
    }
}

TimeScope::TimeScope(std::string name, std::string methodName, CompilerLog* log)
    : ClockScope(), name_(std::move(name)), methodName_(std::move(methodName)), log_(log)
{
    if (log_->GetEnableCompilerLogTime()) {
        startTime_ = ClockScope().GetCurTime();
    }
}

TimeScope::~TimeScope()
{
    if (log_->GetEnableCompilerLogTime()) {
        timeUsed_ = ClockScope().GetCurTime() - startTime_;
        LOG_COMPILER(INFO) << std::setw(PASS_LENS) << name_ << " " << std::setw(TIME_LENS) <<
        GetShortName(methodName_) << " time used:" << timeUsed_ / MILLION_TIME << "ms";
        std::string shortName = GetShortName(methodName_);
        log_->AddPassTime(name_, timeUsed_);
        log_->AddMethodTime(shortName, timeUsed_);
    }
}

const std::string TimeScope::GetShortName(const std::string& methodName)
{
    std::string KeyStr = "@";
    if (methodName.find(KeyStr) != std::string::npos) {
        std::string::size_type index = methodName.find(KeyStr);
        std::string extracted = methodName.substr(0, index);
        return extracted;
    } else {
        return methodName;
    }
}

void CompilerLog::PrintPassTime() const
{
    double allPassTimeforAllMethods = 0;
    auto myMap = timePassMap_;
    std::multimap<double, std::string> PassTimeMap;
    std::map<std::string, double>::iterator it;
    for (it = myMap.begin(); it != myMap.end(); it++) {
        PassTimeMap.insert(make_pair(it->second, it->first));
    }
    for (auto [key, val] : PassTimeMap) {
        allPassTimeforAllMethods += key;
    }
    for (auto [key, val] : PassTimeMap) {
        LOG_COMPILER(INFO) << std::setw(PASS_LENS) << val << " Total cost time is "<< std::setw(TIME_LENS)
        << key / MILLION_TIME << "ms " << "percentage:" << std::fixed << std::setprecision(PERCENT_LENS)
        << key / allPassTimeforAllMethods * HUNDRED_TIME << "% ";
    }
}

void CompilerLog::PrintMethodTime() const
{
    double methodTotalTime = 0;
    auto myMap = timeMethodMap_;
    std::multimap<double, std::string> MethodTimeMap;
    std::map<std::string, double>::iterator it;
    for (it = myMap.begin(); it != myMap.end(); it++) {
        MethodTimeMap.insert(make_pair(it->second, it->first));
    }
    for (auto [key, val] : MethodTimeMap) {
        methodTotalTime += key;
    }
    for (auto [key, val] : MethodTimeMap) {
        LOG_COMPILER(INFO) << "method:" << std::setw(METHOD_LENS) << val << " all pass cost time is "
        << std::setw(TIME_LENS) << key / MILLION_TIME << "ms " << "percentage:" << std::fixed
        << std::setprecision(PERCENT_LENS) << key / methodTotalTime * HUNDRED_TIME << "% ";
    }
}

void CompilerLog::PrintTime() const
{
    LOG_COMPILER(INFO) << " ";
    PrintPassTime();
    LOG_COMPILER(INFO) << " ";
    PrintMethodTime();
}

void CompilerLog::AddMethodTime(const std::string& name, double time)
{
    if (timeMethodMap_.count(name) == 0) {
        timeMethodMap_.insert(std::make_pair(name, time));
    } else {
        timeMethodMap_.insert(std::make_pair(name, time + timeMethodMap_.at(name)));
    }
}

void CompilerLog::AddPassTime(const std::string& name, double time)
{
    if (timePassMap_.count(name) == 0) {
        timePassMap_.insert(std::make_pair(name, time));
    } else {
        timePassMap_.insert(std::make_pair(name, time + timePassMap_.at(name)));
    }
}
// namespace panda::ecmascript::kungfu
}