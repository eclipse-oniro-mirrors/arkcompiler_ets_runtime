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

#ifndef ECMASCRIPT_MODULE_DEPENDENCY_EXTRACTOR_H
#define ECMASCRIPT_MODULE_DEPENDENCY_EXTRACTOR_H

#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <mutex>

#include "ecmascript/jspandafile/js_pandafile.h"
#include "ecmascript/jspandafile/js_pandafile_manager.h"
#include "ecmascript/module/accessor/module_data_accessor.h"
#include "libpandabase/macros.h"

namespace panda::ecmascript {

class EcmaVM;

struct DependencyEdge {
    CString target;
    CString specifier;
    bool isDynamic {false};

    bool operator==(const DependencyEdge &other) const
    {
        return target == other.target && specifier == other.specifier && isDynamic == other.isDynamic;
    }
};

struct DependencyNode {
    CString recordName;
    CString fileName;
    std::vector<DependencyEdge> dependencies;

    void AddDependency(const DependencyEdge &edge)
    {
        for (const auto &dep : dependencies) {
            if (dep == edge) {
                return;
            }
        }
        dependencies.push_back(edge);
    }
};

class DependencyGraph {
public:
    DependencyGraph() = default;
    ~DependencyGraph() = default;

    DependencyNode &AddNode(const CString &recordName, const CString &fileName);

    void AddEdge(const CString &sourceRecord, const CString &sourceFileName,
                 const CString &target, const CString &specifier, bool isDynamic);

    const std::unordered_map<CString, DependencyNode> &GetNodes() const
    {
        return nodes_;
    }

    bool Empty() const
    {
        return nodes_.empty();
    }

    void Clear()
    {
        nodes_.clear();
    }

private:
    std::unordered_map<CString, DependencyNode> nodes_;
};

class DependencyExtractor {
public:
    static std::unique_ptr<DependencyGraph> BuildDependencyGraph();

    static std::vector<CString> ExtractModuleRequests(const JSPandaFile *jsPandaFile,
                                                      const CString &recordName);

private:
    static void ProcessRecord(const JSPandaFile *jsPandaFile,
                              const CString &recordName,
                              DependencyGraph &graph);
};

struct PendingDynamicImport {
    CString callerRecord;
    CString callerFile;
    CString targetModule;
    CString specifier;
};

class DynamicImportRecorder {
public:
    static void StartRecording();

    static void StopRecording();

    static void RecordDynamicImport(const CString &callerRecord,
                                    const CString &callerFile,
                                    const CString &targetModule,
                                    const CString &specifier);

    static void MergeDynamicEdges(DependencyGraph &graph);

    static bool HasDynamicEdges();

private:
    static std::vector<PendingDynamicImport> pendingImports_;
    static std::mutex pendingImportsMutex_;
    static bool recording_;
};

class DependencyLogger {
public:
    explicit DependencyLogger(EcmaVM *vm);
    ~DependencyLogger();

    static void SetDependencyLoggerTask(EcmaVM *vm);

    static void PrintDependencyGraphTask(void *data);

    void PrintDependencyGraph();

private:
    static constexpr uint64_t THREE_SECONDS = 3000;
    static constexpr std::string_view FILEDIR = "/data/storage/el2/base/files/";
    static constexpr std::string_view DEP_GRAPH_SUFFIX = "_dependency_graph.txt";

    void WriteToFile(const CString &filePath, const CString &content);

    EcmaVM *vm_ {nullptr};
};

}  // namespace panda::ecmascript

#endif  // ECMASCRIPT_MODULE_DEPENDENCY_EXTRACTOR_H
