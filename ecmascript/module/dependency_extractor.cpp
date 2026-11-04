/*
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

#include "ecmascript/module/dependency_extractor.h"
#include "ecmascript/ecma_vm.h"
#include "ecmascript/js_runtime_options.h"
#include "ecmascript/js_thread.h"
#include "ecmascript/jspandafile/js_pandafile_manager.h"
#include "ecmascript/module/module_path_helper.h"
#include "ecmascript/platform/parameters.h"

#include <cerrno>
#include <fstream>
#include <sstream>

namespace panda::ecmascript {

DependencyNode &DependencyGraph::AddNode(const CString &recordName, const CString &fileName)
{
    auto it = nodes_.find(recordName);
    if (it != nodes_.end()) {
        return it->second;
    }
    DependencyNode node;
    node.recordName = recordName;
    node.fileName = fileName;
    nodes_.emplace(recordName, std::move(node));
    return nodes_.at(recordName);
}

void DependencyGraph::AddEdge(const CString &sourceRecord, const CString &sourceFileName,
                              const CString &target, const CString &specifier, bool isDynamic)
{
    DependencyNode &node = AddNode(sourceRecord, sourceFileName);
    DependencyEdge edge;
    edge.target = target;
    edge.specifier = specifier;
    edge.isDynamic = isDynamic;
    node.AddDependency(edge);

    AddNode(target, "");
}

std::unique_ptr<DependencyGraph> DependencyExtractor::BuildDependencyGraph()
{
    auto graph = std::make_unique<DependencyGraph>();

    JSPandaFileManager::GetInstance()->EnumerateJSPandaFiles(
        [&graph](const std::shared_ptr<JSPandaFile> &jsPandaFile) -> bool {
            if (jsPandaFile == nullptr) {
                return true;
            }
            const auto &recordInfos = jsPandaFile->GetJSRecordInfo();
            for (const auto &[recordNameSv, recordInfo] : recordInfos) {
                if (recordInfo == nullptr) {
                    continue;
                }
                if (!jsPandaFile->IsModule(recordInfo)) {
                    continue;
                }
                CString recordName(recordNameSv.data(), recordNameSv.size());
                ProcessRecord(jsPandaFile.get(), recordName, *graph);
            }
            return true;
        });
    DynamicImportRecorder::MergeDynamicEdges(*graph);

    return graph;
}

void DependencyExtractor::ProcessRecord(const JSPandaFile *jsPandaFile,
                                        const CString &recordName,
                                        DependencyGraph &graph)
{
    int moduleRecordIdx = jsPandaFile->GetModuleRecordIdx(recordName);
    if (moduleRecordIdx < 0) {
        return;
    }

    auto moduleId = panda_file::File::EntityId(static_cast<uint32_t>(moduleRecordIdx));
    ModuleDataAccessor mda(jsPandaFile, moduleId);
    const std::vector<uint32_t> &moduleRequests = mda.getModuleRequests();

    CString fileName = jsPandaFile->GetJSPandaFileDesc();
    DependencyNode &node = graph.AddNode(recordName, fileName);

    for (uint32_t requestIdx : moduleRequests) {
        auto specId = panda_file::File::EntityId(requestIdx);
        StringData sd = jsPandaFile->GetStringData(specId);
        CString specifier(utf::Mutf8AsCString(sd.data));

        DependencyEdge edge;
        edge.target = specifier;
        edge.specifier = specifier;
        edge.isDynamic = false;
        node.AddDependency(edge);
    }
}

std::vector<CString> DependencyExtractor::ExtractModuleRequests(const JSPandaFile *jsPandaFile,
                                                                const CString &recordName)
{
    std::vector<CString> requests;
    if (jsPandaFile == nullptr) {
        return requests;
    }

    int moduleRecordIdx = jsPandaFile->GetModuleRecordIdx(recordName);
    if (moduleRecordIdx < 0) {
        return requests;
    }

    auto moduleId = panda_file::File::EntityId(static_cast<uint32_t>(moduleRecordIdx));
    ModuleDataAccessor mda(jsPandaFile, moduleId);
    const std::vector<uint32_t> &moduleRequests = mda.getModuleRequests();

    for (uint32_t requestIdx : moduleRequests) {
        auto specId = panda_file::File::EntityId(requestIdx);
        StringData sd = jsPandaFile->GetStringData(specId);
        CString specifier(utf::Mutf8AsCString(sd.data));
        requests.push_back(specifier);
    }
    return requests;
}

std::vector<PendingDynamicImport> DynamicImportRecorder::pendingImports_;
std::mutex DynamicImportRecorder::pendingImportsMutex_;
bool DynamicImportRecorder::recording_ = false;

void DynamicImportRecorder::StartRecording()
{
    std::lock_guard<std::mutex> lock(pendingImportsMutex_);
    recording_ = true;
}

void DynamicImportRecorder::StopRecording()
{
    std::lock_guard<std::mutex> lock(pendingImportsMutex_);
    recording_ = false;
    pendingImports_.clear();
}

void DynamicImportRecorder::RecordDynamicImport(const CString &callerRecord,
                                                const CString &callerFile,
                                                const CString &targetModule,
                                                const CString &specifier)
{
    std::lock_guard<std::mutex> lock(pendingImportsMutex_);
    if (!recording_) {
        return;
    }
    pendingImports_.push_back({callerRecord, callerFile, targetModule, specifier});
}

void DynamicImportRecorder::MergeDynamicEdges(DependencyGraph &graph)
{
    std::lock_guard<std::mutex> lock(pendingImportsMutex_);
    // Dependency logging is a one-shot cold-start diagnostic. Stop accepting imports before consuming the queue.
    recording_ = false;
    for (const auto &imp : pendingImports_) {
        graph.AddEdge(imp.callerRecord, imp.callerFile,
                      imp.targetModule, imp.specifier, true);
    }

    pendingImports_.clear();
}

bool DynamicImportRecorder::HasDynamicEdges()
{
    std::lock_guard<std::mutex> lock(pendingImportsMutex_);
    return !pendingImports_.empty();
}

DependencyLogger::DependencyLogger(EcmaVM *vm) : vm_(vm) {}

DependencyLogger::~DependencyLogger() {}

void DependencyLogger::SetDependencyLoggerTask(EcmaVM *vm)
{
    if (!vm->GetJSOptions().EnableModuleDependencyLog()) {
        return;
    }

    if (vm->IsWorkerThread()) {
        return;
    }

    TimerTaskCallback timerTaskCallBack = vm->GetTimerTaskCallback();
    if (timerTaskCallBack == nullptr) {
        LOG_FULL(ERROR) << "DependencyLogger: TimerTaskCallback is nullptr";
        return;
    }

    uint64_t importDurationInMs = GetImportDuration(THREE_SECONDS);
    DynamicImportRecorder::StartRecording();
    void *timerTask = timerTaskCallBack(vm, vm, PrintDependencyGraphTask, importDurationInMs, 0);
    if (timerTask == nullptr) {
        DynamicImportRecorder::StopRecording();
        LOG_FULL(ERROR) << "DependencyLogger: Failed to register timer task";
    }
}

void DependencyLogger::PrintDependencyGraphTask(void *data)
{
    auto *vm = static_cast<EcmaVM *>(data);
    if (vm == nullptr) {
        return;
    }
    DependencyLogger logger(vm);
    logger.PrintDependencyGraph();
}

void DependencyLogger::PrintDependencyGraph()
{
    auto graph = DependencyExtractor::BuildDependencyGraph();
    if (graph->Empty() && !DynamicImportRecorder::HasDynamicEdges()) {
        return;
    }

    std::ostringstream oss;
    oss << "=== Dependency Graph ===" << std::endl;
    oss << "Total nodes: " << graph->GetNodes().size() << std::endl;
    oss << std::endl;

    for (const auto &[recordName, node] : graph->GetNodes()) {
        oss << "Node: " << recordName << std::endl;
        oss << "  File: " << node.fileName << std::endl;
        if (node.dependencies.empty()) {
            oss << std::endl;
            continue;
        }
        oss << "  Dependencies:" << std::endl;
        for (const auto &dep : node.dependencies) {
            const char *tag = dep.isDynamic ? "[dynamic]" : "[static]";
            oss << "    " << tag << " " << dep.target;
            if (dep.isDynamic && dep.specifier != dep.target) {
                oss << " (specifier: " << dep.specifier << ")";
            }
            oss << std::endl;
        }
        oss << std::endl;
    }

    CString bundleName;
    if (vm_ != nullptr) {
        bundleName = vm_->GetBundleName();
    }
    CString filePath = CString(FILEDIR.data()) + bundleName + CString(DEP_GRAPH_SUFFIX.data());
    WriteToFile(filePath, CString(oss.str().c_str()));
}

void DependencyLogger::WriteToFile(const CString &filePath, const CString &content)
{
    std::ofstream fileHandle;
    fileHandle.open(filePath.c_str(), std::ios_base::trunc);
    if (!fileHandle.is_open()) {
        LOG_ECMA(ERROR) << "DependencyLogger: file open failed, errno = " << errno << " path:" << filePath;
        return;
    }
    fileHandle << content.c_str();
    fileHandle.close();
    if (fileHandle.fail()) {
        LOG_ECMA(ERROR) << "DependencyLogger: Failed to write dependency graph file: " << filePath;
        return;
    }
    LOG_ECMA(INFO) << "DependencyLogger: Dependency graph written to " << filePath;
}

}  // namespace panda::ecmascript
