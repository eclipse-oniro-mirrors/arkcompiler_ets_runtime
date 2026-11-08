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

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include "ecmascript/jit/jit.h"
#include "ecmascript/napi/include/jsnapi.h"
#include "ecmascript/napi/include/jsnapi_expo.h"
#include "ecmascript/tests/test_helper.h"

using namespace panda::ecmascript;

namespace panda::test {
namespace {
constexpr double MSEC_PER_SEC = 1000.0;
constexpr double NSEC_PER_MSEC = 1000000.0;
constexpr size_t MAX_STATUS_LINE_LENGTH = 4096;

double NowMs()
{
    return std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

double ThreadCpuMs()
{
    struct timespec ts;
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
    return static_cast<double>(ts.tv_sec) * MSEC_PER_SEC + static_cast<double>(ts.tv_nsec) / NSEC_PER_MSEC;
}

size_t ReadVmHwmKb()
{
    std::ifstream status("/proc/self/status");
    std::string key;
    size_t value = 0;
    std::string unit;
    while (status >> key) {
        if (key == "VmHWM:") {
            status >> value >> unit;
            return value;
        }
        status.ignore(MAX_STATUS_LINE_LENGTH, '\n');
    }
    return 0;
}

std::string GetEnv(const char *name, const char *def)
{
    const char *v = getenv(name);
    return (v == nullptr || *v == '\0') ? std::string(def) : std::string(v);
}

struct WorkerStat {
    double wallMs {0.0};
    double cpuMs {0.0};
};

WorkerStat RunWorkerVm(EcmaVM *mainVm, const std::string &abcPath, const std::string &entry, bool enableJit, int index)
{
    JSRuntimeOptions option;
    option.SetIsWorker(true);
    option.SetEnableJIT(enableJit);
    option.SetLogLevel("info");
    if (GetEnv("WORKER_PGO", "0") == "1") {
        option.SetEnablePGOProfiler(true);
        option.SetPGOProfilerPath(GetEnv("WORKER_PGO_DIR", "/tmp/worker_contention_pgo"));
    }
    std::string stubFile = GetEnv("WORKER_STUB_FILE", "");
    if (!stubFile.empty()) {
        option.SetStubFile(stubFile);
    }
    EcmaVM *vm = JSNApi::CreateEcmaVM(option);
    WorkerStat stat;
    if (vm == nullptr) {
        printf("[WSTAT] worker=%d create_failed\n", index);
        return stat;
    }
    JSNApi::AddWorker(mainVm, vm);
    double t0 = NowMs();
    double c0 = ThreadCpuMs();
    bool ok = JSNApi::Execute(vm, abcPath, entry, false);
    stat.wallMs = NowMs() - t0;
    stat.cpuMs = ThreadCpuMs() - c0;
    printf("[WSTAT] worker=%d ok=%d wall_ms=%.1f cpu_ms=%.1f\n", index, static_cast<int>(ok),
           stat.wallMs, stat.cpuMs);
    fflush(stdout);
    JSNApi::DeleteWorker(mainVm, vm);
    JSNApi::DestroyJSVM(vm);
    return stat;
}

JSRuntimeOptions BuildMainOption(bool enablePgo)
{
    JSRuntimeOptions option;
    option.SetEnableJIT(true);
    option.SetLogLevel("info");
    if (enablePgo) {
        option.SetEnablePGOProfiler(true);
        option.SetPGOProfilerPath(GetEnv("WORKER_PGO_DIR", "/tmp/worker_contention_pgo"));
    }
    std::string stubFile = GetEnv("WORKER_STUB_FILE", "");
    if (!stubFile.empty()) {
        option.SetStubFile(stubFile);
    }
    return option;
}
}  // namespace

class WorkerContentionTest : public testing::Test {
public:
    static void SetUpTestCase() {}
    static void TearDownTestCase() {}
    void SetUp() override {}
    void TearDown() override {}
};

/*
 * @tc.name: WorkerThroughput
 * @tc.desc: N worker runtimes run a fixed-workload hot script; reports per-worker wall/cpu time
 *           and process VmHWM for A/B comparison of worker tiering gating.
 * @tc.type: FUNC
 * @tc.require:
 */
HWTEST_F_L0(WorkerContentionTest, WorkerThroughput)
{
    std::string abcPath = GetEnv("WORKER_ABC_PATH", "");
    if (abcPath.empty()) {
        printf("[WSTAT] skip: WORKER_ABC_PATH not set\n");
        return;
    }
    std::string entry = GetEnv("WORKER_ABC_ENTRY", "hot");
    int workerCount = atoi(GetEnv("WORKER_COUNT", "4").c_str());
    bool workerJit = GetEnv("WORKER_DISABLE_JIT", "0") != "1";

    JSRuntimeOptions mainOption = BuildMainOption(GetEnv("WORKER_PGO", "0") == "1");
    EcmaVM *mainVm = JSNApi::CreateEcmaVM(mainOption);
    ASSERT_TRUE(mainVm != nullptr);
    printf("[WSTAT] diag asm=%d jitpgo=%d osr=%d jitFast=%d jitBaseline=%d jitApp=%d\n",
           static_cast<int>(mainVm->GetJSOptions().GetEnableAsmInterpreter()),
           static_cast<int>(mainVm->GetJSOptions().IsEnableJITPGO()),
           static_cast<int>(mainVm->IsEnableOsr()),
           static_cast<int>(Jit::GetInstance()->IsEnableFastJit()),
           static_cast<int>(Jit::GetInstance()->IsEnableBaselineJit()),
           static_cast<int>(mainVm->GetJSOptions().IsEnableAPPJIT()));
    fflush(stdout);

    std::vector<std::thread> threads;
    std::vector<WorkerStat> stats(workerCount);
    double t0 = NowMs();
    for (int i = 0; i < workerCount; i++) {
        threads.emplace_back([&, i]() {
            stats[i] = RunWorkerVm(mainVm, abcPath, entry, workerJit, i);
        });
    }
    for (auto &t : threads) {
        t.join();
    }
    double totalMs = NowMs() - t0;
    double sumWall = 0.0;
    double sumCpu = 0.0;
    for (auto &s : stats) {
        sumWall += s.wallMs;
        sumCpu += s.cpuMs;
    }
    printf("[WSTAT] summary workers=%d jit=%d total_wall_ms=%.1f sum_wall_ms=%.1f sum_cpu_ms=%.1f vmhwm_kb=%zu\n",
           workerCount, static_cast<int>(workerJit), totalMs, sumWall, sumCpu, ReadVmHwmKb());
    fflush(stdout);
    JSNApi::DestroyJSVM(mainVm);
}

/*
 * @tc.name: MainTierUpUnderContention
 * @tc.desc: workers start first (head start), then main VM runs the hot script; main wall time
 *           reflects compile-queue contention on the single JIT thread.
 * @tc.type: FUNC
 * @tc.require:
 */
HWTEST_F_L0(WorkerContentionTest, MainTierUpUnderContention)
{
    std::string abcPath = GetEnv("WORKER_ABC_PATH", "");
    if (abcPath.empty()) {
        printf("[WSTAT] skip: WORKER_ABC_PATH not set\n");
        return;
    }
    std::string entry = GetEnv("WORKER_ABC_ENTRY", "hot");
    int workerCount = atoi(GetEnv("WORKER_COUNT", "4").c_str());
    int headStartMs = atoi(GetEnv("WORKER_HEADSTART_MS", "400").c_str());

    JSRuntimeOptions mainOption;
    mainOption.SetEnableJIT(true);
    mainOption.SetLogLevel("info");
    EcmaVM *mainVm = JSNApi::CreateEcmaVM(mainOption);
    ASSERT_TRUE(mainVm != nullptr);

    std::vector<std::thread> threads;
    for (int i = 0; i < workerCount; i++) {
        threads.emplace_back([&, i]() {
            RunWorkerVm(mainVm, abcPath, entry, true, i);
        });
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(headStartMs));

    double t0 = NowMs();
    bool ok = JSNApi::Execute(mainVm, abcPath, entry, false);
    double firstMs = NowMs() - t0;
    printf("[WSTAT] main_first ok=%d wall_ms=%.1f\n", static_cast<int>(ok), firstMs);

    for (auto &t : threads) {
        t.join();
    }
    double t1 = NowMs();
    bool ok2 = JSNApi::Execute(mainVm, abcPath, entry, false);
    double secondMs = NowMs() - t1;
    printf("[WSTAT] main_second ok=%d wall_ms=%.1f vmhwm_kb=%zu\n", static_cast<int>(ok2), secondMs,
           ReadVmHwmKb());
    fflush(stdout);
    JSNApi::DestroyJSVM(mainVm);
}
}  // namespace panda::test
