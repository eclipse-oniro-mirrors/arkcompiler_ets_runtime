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

#include "ecmascript/ecma_vm.h"
#include "ecmascript/global_env.h"
#include "ecmascript/js_promise.h"
#include "ecmascript/js_runtime_options.h"
#include "ecmascript/module/dependency_extractor.h"
#include "ecmascript/module/js_dynamic_import.h"
#include "ecmascript/module/js_module_manager.h"
#include "ecmascript/module/js_module_source_text.h"
#include "ecmascript/tests/test_helper.h"

#include <string>
#include <thread>
#include <vector>

using namespace panda::ecmascript;
using namespace testing::ext;

namespace panda::test {
namespace {
struct TimerTaskCapture {
    EcmaVM *vm {nullptr};
    void *data {nullptr};
    TimerCallbackFunc callback {nullptr};
    uint64_t timeout {0};
    bool repeat {false};
    uint32_t callCount {0};
};

TimerTaskCapture g_timerTaskCapture;

void *CaptureTimerTask(EcmaVM *vm, void *data, TimerCallbackFunc callback, uint64_t timeout, bool repeat)
{
    g_timerTaskCapture.vm = vm;
    g_timerTaskCapture.data = data;
    g_timerTaskCapture.callback = callback;
    g_timerTaskCapture.timeout = timeout;
    g_timerTaskCapture.repeat = repeat;
    g_timerTaskCapture.callCount++;
    return &g_timerTaskCapture;
}

void ClearPendingDynamicImports()
{
    DependencyGraph graph;
    DynamicImportRecorder::MergeDynamicEdges(graph);
}
}  // namespace

class DependencyExtractorTest : public testing::Test {
public:
    static JSTaggedValue TestFunc(EcmaRuntimeCallInfo *argv)
    {
        return JSTaggedValue::Undefined();
    }

    static void SetUpTestCase()
    {
        GTEST_LOG_(INFO) << "SetUpTestCase";
    }

    static void TearDownTestCase()
    {
        GTEST_LOG_(INFO) << "TearDownTestCase";
    }

    void SetUp() override
    {
        TestHelper::CreateEcmaVMWithScope(instance, thread, scope);
        originalArkProperties = instance->GetJSOptions().GetArkProperties();
        originalIsWorker = instance->IsWorkerThread();
        g_timerTaskCapture = {};
        ClearPendingDynamicImports();
    }

    void TearDown() override
    {
        ClearPendingDynamicImports();
        instance->SetTimerTaskCallback(nullptr);
        instance->GetJSOptions().SetArkProperties(originalArkProperties);
        instance->GetJSOptions().SetIsWorker(originalIsWorker);
        TestHelper::DestroyEcmaVMWithScope(instance, scope);
    }

    void ExecuteNativeImport(const CString &specifier, const CString &callerRecord, const CString &callerFile)
    {
        ObjectFactory *factory = instance->GetFactory();
        ModuleManager *moduleManager = thread->GetModuleManager();
        JSHandle<SourceTextModule> module = factory->NewSourceTextModule();
        module->SetEcmaModuleRecordNameString(specifier);
        module->SetEcmaModuleFilenameString(thread, "");
        module->SetTypes(ModuleTypes::NATIVE_MODULE);
        module->SetStatus(ModuleStatus::EVALUATING);
        moduleManager->AddResolveImportedModule(specifier, module.GetTaggedValue());

        JSHandle<JSFunction> resolve = factory->NewJSFunction(
            instance->GetGlobalEnv(), reinterpret_cast<void *>(DependencyExtractorTest::TestFunc));
        JSHandle<JSFunction> reject = factory->NewJSFunction(
            instance->GetGlobalEnv(), reinterpret_cast<void *>(DependencyExtractorTest::TestFunc));
        JSHandle<JSPromiseReactionsFunction> resolveHandle(resolve);
        JSHandle<JSPromiseReactionsFunction> rejectHandle(reject);

        JSTaggedValue result = DynamicImport::ExecuteNativeOrJsonModule(thread, specifier, ModuleTypes::NATIVE_MODULE,
            resolveHandle, rejectHandle, nullptr, callerRecord, callerFile);
        EXPECT_FALSE(thread->HasPendingException());
        EXPECT_TRUE(result.IsUndefined());
    }

    EcmaVM *instance {nullptr};
    EcmaHandleScope *scope {nullptr};
    JSThread *thread {nullptr};
    int64_t originalArkProperties {0};
    bool originalIsWorker {false};
};

/**
 * @tc.name: DependencyGraph_AddEdgeAndDeduplicate
 * @tc.desc: Verify source and target nodes are created and duplicate edges are ignored.
 * @tc.type: FUNC
 */
HWTEST_F_L0(DependencyExtractorTest, DependencyGraph_AddEdgeAndDeduplicate)
{
    DependencyGraph graph;
    EXPECT_TRUE(graph.Empty());

    graph.AddEdge("source", "source.abc", "target", "./target", false);
    graph.AddEdge("source", "source.abc", "target", "./target", false);

    ASSERT_EQ(graph.GetNodes().size(), 2U);
    const auto &source = graph.GetNodes().at("source");
    EXPECT_EQ(source.recordName, "source");
    EXPECT_EQ(source.fileName, "source.abc");
    ASSERT_EQ(source.dependencies.size(), 1U);
    EXPECT_EQ(source.dependencies[0].target, "target");
    EXPECT_EQ(source.dependencies[0].specifier, "./target");
    EXPECT_FALSE(source.dependencies[0].isDynamic);
    EXPECT_NE(graph.GetNodes().find("target"), graph.GetNodes().end());

    graph.Clear();
    EXPECT_TRUE(graph.Empty());
}

/**
 * @tc.name: DependencyGraph_StaticAndDynamicEdgesAreDistinct
 * @tc.desc: Verify edges with identical paths but different import types are both retained.
 * @tc.type: FUNC
 */
HWTEST_F_L0(DependencyExtractorTest, DependencyGraph_StaticAndDynamicEdgesAreDistinct)
{
    DependencyGraph graph;
    graph.AddEdge("source", "source.abc", "target", "target", false);
    graph.AddEdge("source", "source.abc", "target", "target", true);

    const auto &dependencies = graph.GetNodes().at("source").dependencies;
    ASSERT_EQ(dependencies.size(), 2U);
    EXPECT_FALSE(dependencies[0].isDynamic);
    EXPECT_TRUE(dependencies[1].isDynamic);
}

/**
 * @tc.name: DependencyExtractor_NullPandaFileReturnsEmpty
 * @tc.desc: Verify missing panda files are handled without dereferencing external module data.
 * @tc.type: FUNC
 */
HWTEST_F_L0(DependencyExtractorTest, DependencyExtractor_NullPandaFileReturnsEmpty)
{
    EXPECT_TRUE(DependencyExtractor::ExtractModuleRequests(nullptr, "record").empty());
    EXPECT_TRUE(DependencyExtractor::ExtractModuleRequests(nullptr, "").empty());
}

/**
 * @tc.name: DependencyGraph_SelfDependencyUsesSingleNode
 * @tc.desc: Verify a self dependency is represented by one node with one edge.
 * @tc.type: FUNC
 */
HWTEST_F_L0(DependencyExtractorTest, DependencyGraph_SelfDependencyUsesSingleNode)
{
    DependencyGraph graph;
    graph.AddEdge("record", "record.abc", "record", "./record", false);

    ASSERT_EQ(graph.GetNodes().size(), 1U);
    const auto &node = graph.GetNodes().at("record");
    EXPECT_EQ(node.recordName, "record");
    EXPECT_EQ(node.fileName, "record.abc");
    ASSERT_EQ(node.dependencies.size(), 1U);
    EXPECT_EQ(node.dependencies[0].target, "record");
    EXPECT_EQ(node.dependencies[0].specifier, "./record");
    EXPECT_FALSE(node.dependencies[0].isDynamic);
}

/**
 * @tc.name: DependencyGraph_DifferentSpecifiersForSameTargetAreDistinct
 * @tc.desc: Verify different import specifiers resolving to the same target are retained.
 * @tc.type: FUNC
 */
HWTEST_F_L0(DependencyExtractorTest, DependencyGraph_DifferentSpecifiersForSameTargetAreDistinct)
{
    DependencyGraph graph;
    graph.AddEdge("source", "source.abc", "target", "./target", false);
    graph.AddEdge("source", "source.abc", "target", "package/target", false);

    const auto &dependencies = graph.GetNodes().at("source").dependencies;
    ASSERT_EQ(dependencies.size(), 2U);
    EXPECT_EQ(dependencies[0].specifier, "./target");
    EXPECT_EQ(dependencies[1].specifier, "package/target");
}

/**
 * @tc.name: DynamicImportRecorder_MergeAndClear
 * @tc.desc: Verify pending dynamic imports are merged, deduplicated, and consumed once.
 * @tc.type: FUNC
 */
HWTEST_F_L0(DependencyExtractorTest, DynamicImportRecorder_MergeAndClear)
{
    DynamicImportRecorder::StartRecording();
    DynamicImportRecorder::RecordDynamicImport("caller", "caller.abc", "target", "./target");
    DynamicImportRecorder::RecordDynamicImport("caller", "caller.abc", "target", "./target");
    DynamicImportRecorder::RecordDynamicImport("caller", "caller.abc", "other", "./other");
    EXPECT_TRUE(DynamicImportRecorder::HasDynamicEdges());

    DependencyGraph graph;
    DynamicImportRecorder::MergeDynamicEdges(graph);

    EXPECT_FALSE(DynamicImportRecorder::HasDynamicEdges());
    const auto &caller = graph.GetNodes().at("caller");
    EXPECT_EQ(caller.fileName, "caller.abc");
    ASSERT_EQ(caller.dependencies.size(), 2U);
    EXPECT_TRUE(caller.dependencies[0].isDynamic);
    EXPECT_TRUE(caller.dependencies[1].isDynamic);

    DynamicImportRecorder::MergeDynamicEdges(graph);
    EXPECT_EQ(graph.GetNodes().at("caller").dependencies.size(), 2U);
}

/**
 * @tc.name: DynamicImportRecorder_ConcurrentRecord
 * @tc.desc: Verify concurrent writers retain every unique dynamic import.
 * @tc.type: FUNC
 */
HWTEST_F_L0(DependencyExtractorTest, DynamicImportRecorder_ConcurrentRecord)
{
    DynamicImportRecorder::StartRecording();
    constexpr uint32_t threadCount = 4;
    constexpr uint32_t importsPerThread = 8;
    std::vector<std::thread> workers;
    for (uint32_t threadIndex = 0; threadIndex < threadCount; threadIndex++) {
        workers.emplace_back([threadIndex]() {
            for (uint32_t importIndex = 0; importIndex < importsPerThread; importIndex++) {
                std::string target = "target_" + std::to_string(threadIndex) + "_" + std::to_string(importIndex);
                DynamicImportRecorder::RecordDynamicImport(
                    "concurrent_caller", "caller.abc", target.c_str(), target.c_str());
            }
        });
    }
    for (auto &worker : workers) {
        worker.join();
    }

    DependencyGraph graph;
    DynamicImportRecorder::MergeDynamicEdges(graph);
    const auto &dependencies = graph.GetNodes().at("concurrent_caller").dependencies;
    EXPECT_EQ(dependencies.size(), threadCount * importsPerThread);
    EXPECT_FALSE(DynamicImportRecorder::HasDynamicEdges());
}

/**
 * @tc.name: DynamicImportRecorder_DoesNotRecordAfterMerge
 * @tc.desc: Verify the one-shot recorder stops accepting imports after the dependency graph is built.
 * @tc.type: FUNC
 */
HWTEST_F_L0(DependencyExtractorTest, DynamicImportRecorder_DoesNotRecordAfterMerge)
{
    DynamicImportRecorder::StartRecording();
    DynamicImportRecorder::RecordDynamicImport("caller", "caller.abc", "first", "./first");

    DependencyGraph graph;
    DynamicImportRecorder::MergeDynamicEdges(graph);
    DynamicImportRecorder::RecordDynamicImport("caller", "caller.abc", "late", "./late");

    EXPECT_FALSE(DynamicImportRecorder::HasDynamicEdges());
    const auto &dependencies = graph.GetNodes().at("caller").dependencies;
    ASSERT_EQ(dependencies.size(), 1U);
    EXPECT_EQ(dependencies[0].target, "first");
}

/**
 * @tc.name: DynamicImport_DisabledDoesNotRecordDependency
 * @tc.desc: Verify native dynamic imports are not recorded when dependency logging is disabled.
 * @tc.type: FUNC
 */
HWTEST_F_L0(DependencyExtractorTest, DynamicImport_DisabledDoesNotRecordDependency)
{
    DynamicImportRecorder::StartRecording();
    auto &options = instance->GetJSOptions();
    options.SetArkProperties(originalArkProperties & ~ArkProperties::ENABLE_MODULE_DEPENDENCY_LOG);

    ExecuteNativeImport("@ohos:dependency_log_disabled", "caller", "caller.abc");

    EXPECT_FALSE(DynamicImportRecorder::HasDynamicEdges());
}

/**
 * @tc.name: DynamicImport_EnabledRecordsDependency
 * @tc.desc: Verify native dynamic imports record caller and target data when logging is enabled.
 * @tc.type: FUNC
 */
HWTEST_F_L0(DependencyExtractorTest, DynamicImport_EnabledRecordsDependency)
{
    DynamicImportRecorder::StartRecording();
    auto &options = instance->GetJSOptions();
    options.SetArkProperties(originalArkProperties | ArkProperties::ENABLE_MODULE_DEPENDENCY_LOG);

    CString specifier = "@ohos:dependency_log_enabled";
    ExecuteNativeImport(specifier, "caller", "caller.abc");
    ASSERT_TRUE(DynamicImportRecorder::HasDynamicEdges());

    DependencyGraph graph;
    DynamicImportRecorder::MergeDynamicEdges(graph);
    const auto &caller = graph.GetNodes().at("caller");
    ASSERT_EQ(caller.dependencies.size(), 1U);
    EXPECT_EQ(caller.dependencies[0].target, specifier);
    EXPECT_EQ(caller.dependencies[0].specifier, specifier);
    EXPECT_TRUE(caller.dependencies[0].isDynamic);
}

/**
 * @tc.name: DynamicImport_JsonModuleIsNotRecordedTwice
 * @tc.desc: Verify a JSON import keeps the resolved target and original specifier as one dependency edge.
 * @tc.type: FUNC
 */
HWTEST_F_L0(DependencyExtractorTest, DynamicImport_JsonModuleIsNotRecordedTwice)
{
    DynamicImportRecorder::StartRecording();
    auto &options = instance->GetJSOptions();
    options.SetArkProperties(originalArkProperties | ArkProperties::ENABLE_MODULE_DEPENDENCY_LOG);

    CString callerRecord = "caller";
    CString callerFile = "caller.abc";
    CString target = "@bundle:bundle/module/ets/config.json";
    CString specifier = "./config.json";
    DynamicImportRecorder::RecordDynamicImport(callerRecord, callerFile, target, specifier);

    ObjectFactory *factory = instance->GetFactory();
    ModuleManager *moduleManager = thread->GetModuleManager();
    JSHandle<SourceTextModule> module = factory->NewSourceTextModule();
    module->SetEcmaModuleRecordNameString(target);
    module->SetEcmaModuleFilenameString(thread, callerFile);
    module->SetTypes(ModuleTypes::JSON_MODULE);
    module->SetStatus(ModuleStatus::EVALUATING);
    moduleManager->AddResolveImportedModule(target, module.GetTaggedValue());

    JSHandle<JSFunction> resolve = factory->NewJSFunction(
        instance->GetGlobalEnv(), reinterpret_cast<void *>(DependencyExtractorTest::TestFunc));
    JSHandle<JSFunction> reject = factory->NewJSFunction(
        instance->GetGlobalEnv(), reinterpret_cast<void *>(DependencyExtractorTest::TestFunc));
    JSHandle<JSPromiseReactionsFunction> resolveHandle(resolve);
    JSHandle<JSPromiseReactionsFunction> rejectHandle(reject);
    JSTaggedValue result = DynamicImport::ExecuteNativeOrJsonModule(
        thread, target, ModuleTypes::JSON_MODULE, resolveHandle, rejectHandle, nullptr, callerRecord, callerFile);
    EXPECT_FALSE(thread->HasPendingException());
    EXPECT_TRUE(result.IsUndefined());

    DependencyGraph graph;
    DynamicImportRecorder::MergeDynamicEdges(graph);
    const auto &dependencies = graph.GetNodes().at(callerRecord).dependencies;
    ASSERT_EQ(dependencies.size(), 1U);
    EXPECT_EQ(dependencies[0].target, target);
    EXPECT_EQ(dependencies[0].specifier, specifier);
}

/**
 * @tc.name: DependencyLogger_DisabledDoesNotScheduleTask
 * @tc.desc: Verify the timer is not registered when module dependency logging is disabled.
 * @tc.type: FUNC
 */
HWTEST_F_L0(DependencyExtractorTest, DependencyLogger_DisabledDoesNotScheduleTask)
{
    auto &options = instance->GetJSOptions();
    options.SetArkProperties(originalArkProperties & ~ArkProperties::ENABLE_MODULE_DEPENDENCY_LOG);
    options.SetIsWorker(false);
    instance->SetTimerTaskCallback(CaptureTimerTask);

    DependencyLogger::SetDependencyLoggerTask(instance);

    EXPECT_EQ(g_timerTaskCapture.callCount, 0U);
}

/**
 * @tc.name: DependencyLogger_WorkerDoesNotScheduleTask
 * @tc.desc: Verify a worker VM does not register the process-level dependency logger timer.
 * @tc.type: FUNC
 */
HWTEST_F_L0(DependencyExtractorTest, DependencyLogger_WorkerDoesNotScheduleTask)
{
    auto &options = instance->GetJSOptions();
    options.SetArkProperties(originalArkProperties | ArkProperties::ENABLE_MODULE_DEPENDENCY_LOG);
    options.SetIsWorker(true);
    instance->SetTimerTaskCallback(CaptureTimerTask);

    DependencyLogger::SetDependencyLoggerTask(instance);

    EXPECT_EQ(g_timerTaskCapture.callCount, 0U);
}

/**
 * @tc.name: DependencyLogger_NullTimerCallback
 * @tc.desc: Verify an enabled main VM safely handles a missing timer callback.
 * @tc.type: FUNC
 */
HWTEST_F_L0(DependencyExtractorTest, DependencyLogger_NullTimerCallback)
{
    auto &options = instance->GetJSOptions();
    options.SetArkProperties(originalArkProperties | ArkProperties::ENABLE_MODULE_DEPENDENCY_LOG);
    options.SetIsWorker(false);
    instance->SetTimerTaskCallback(nullptr);

    DependencyLogger::SetDependencyLoggerTask(instance);

    EXPECT_EQ(g_timerTaskCapture.callCount, 0U);
}

/**
 * @tc.name: DependencyLogger_MainVmSchedulesTaskWithVmData
 * @tc.desc: Verify the main VM registers a one-shot timer and passes itself as callback data.
 * @tc.type: FUNC
 */
HWTEST_F_L0(DependencyExtractorTest, DependencyLogger_MainVmSchedulesTaskWithVmData)
{
    auto &options = instance->GetJSOptions();
    options.SetArkProperties(originalArkProperties | ArkProperties::ENABLE_MODULE_DEPENDENCY_LOG);
    options.SetIsWorker(false);
    instance->SetTimerTaskCallback(CaptureTimerTask);

    DependencyLogger::SetDependencyLoggerTask(instance);

    ASSERT_EQ(g_timerTaskCapture.callCount, 1U);
    EXPECT_EQ(g_timerTaskCapture.vm, instance);
    EXPECT_EQ(g_timerTaskCapture.data, instance);
    EXPECT_EQ(g_timerTaskCapture.callback, DependencyLogger::PrintDependencyGraphTask);
    EXPECT_EQ(g_timerTaskCapture.timeout, 3000U);
    EXPECT_FALSE(g_timerTaskCapture.repeat);
}

/**
 * @tc.name: DependencyLogger_NullTaskData
 * @tc.desc: Verify the timer callback safely ignores null callback data.
 * @tc.type: FUNC
 */
HWTEST_F_L0(DependencyExtractorTest, DependencyLogger_NullTaskData)
{
    DependencyLogger::PrintDependencyGraphTask(nullptr);
    SUCCEED();
}
}  // namespace panda::test
