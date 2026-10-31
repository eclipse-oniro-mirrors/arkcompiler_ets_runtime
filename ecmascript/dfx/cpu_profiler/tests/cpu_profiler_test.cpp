/**
 * Copyright (c) 2024-2026 Huawei Device Co., Ltd.
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

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <thread>
#include <ucontext.h>

#include "ecmascript/compiler/aot_file/an_file_data_manager.h"
#include "ecmascript/compiler/aot_file/stub_file_info.h"
#include "ecmascript/dfx/cpu_profiler/cpu_profiler.h"
#include "ecmascript/jspandafile/js_pandafile_manager.h"
#include "ecmascript/jspandafile/program_object.h"
#include "ecmascript/platform/profiler_signal_guard.h"
#include "ecmascript/tests/test_helper.h"

using namespace panda::ecmascript;

namespace panda::ecmascript {
class CpuProfilerFriendTest {
public:
    explicit CpuProfilerFriendTest(const EcmaVM *vm, const int interval) : cpu_profiler(vm, interval) {}

    // SIGPROF interrupts plain usleep mid-wait (usleep does not retry), so
    // waits that must outlive the sampling thread's ticks loop in 1ms slices.
    static constexpr useconds_t sleepSliceUs = 1000; // 1000us = 1ms slice

    static void SleepThroughSignalInterruptions(uint64_t micros)
    {
        uint64_t deadline = SamplingProcessor::GetMicrosecondsTimeStamp() + micros;
        while (SamplingProcessor::GetMicrosecondsTimeStamp() < deadline) {
            usleep(sleepSliceUs); // one interrupted slice only shortens by 1ms
        }
    }

    void SetIsProfiling(bool isProfiling)
    {
        cpu_profiler.isProfiling_ = isProfiling;
    }

    void SetOutToFile(bool outToFile)
    {
        cpu_profiler.outToFile_ = outToFile;
    }

    void SetInterval(uint32_t intervalTest)
    {
        cpu_profiler.interval_ = intervalTest;
    }

    bool StopCpuProfilerForInfoTest(std::unique_ptr<ProfileInfo> &profileInfo)
    {
        return cpu_profiler.StopCpuProfilerForInfo(profileInfo);
    }

    bool StartCpuProfilerForFileTest(const std::string &fileName)
    {
        return cpu_profiler.StartCpuProfilerForFile(fileName);
    }

    bool StopCpuProfilerForFileTest()
    {
        return cpu_profiler.StopCpuProfilerForFile();
    }

    bool GetStackBeforeCallNapiTest(JSThread *thread)
    {
        return cpu_profiler.GetStackBeforeCallNapi(thread);
    }

    void ProcessHybridStackTest(JSThread *thread)
    {
#if defined(PANDA_JS_ETS_HYBRID_MODE) && defined(ECMASCRIPT_SUPPORT_CPUPROFILER_LEGACY)
        cpu_profiler.ProcessHybridStack(thread);
#else
        return;
#endif
    }

    void ProcessStaticFrameTest(const void *frame)
    {
#if defined(PANDA_JS_ETS_HYBRID_MODE) && defined(ECMASCRIPT_SUPPORT_CPUPROFILER_LEGACY)
        cpu_profiler.ProcessStaticFrame(frame);
#else
        return;
#endif
    }

    void ProcessDynamicFrameTest(const void *frame, JSThread *thread)
    {
#if defined(PANDA_JS_ETS_HYBRID_MODE) && defined(ECMASCRIPT_SUPPORT_CPUPROFILER_LEGACY)
        cpu_profiler.ProcessDynamicFrame(frame, thread);
#else
        return;
#endif
    }

    int GetFrameStackLengthTest()
    {
        if (cpu_profiler.generator_ != nullptr) {
            return cpu_profiler.generator_->GetframeStackLength();
        }
        return 0;
    }

    void ProcessRawSampleTest(const RawSample &sample)
    {
        cpu_profiler.ProcessRawSample(sample);
    }

    std::unique_ptr<ProfileInfo> GetProfileInfoTest()
    {
        if (cpu_profiler.generator_ == nullptr) {
            return nullptr;
        }
        return cpu_profiler.generator_->GetProfileInfo();
    }

    bool StartProfilerForInfoTest()
    {
        return cpu_profiler.StartCpuProfilerForInfo();
    }

    SamplesRecord *GetGenerator()
    {
        return cpu_profiler.generator_;
    }

    CpuProfilerSignalContext *GetSignalCtx()
    {
        return &cpu_profiler.signalCtx_;
    }

    void CollectSampleIntoTest(RawSample &sample, CpuProfilerSignalContext *ctx, void *context)
    {
        cpu_profiler.CollectSampleInto(sample, ctx, context);
    }

    static uint64_t GetPcFromContextTest(void *context)
    {
        return CpuProfiler::GetPcFromContext(context);
    }

    void WaitHandlersDrainedTest()
    {
        cpu_profiler.WaitHandlersDrained();
    }

    void BreakSamplingLoop()
    {
        cpu_profiler.generator_->SetIsStart(false);
    }

    bool InHeaderOrTailTest(uint64_t pc, uint64_t entryBegin, uint64_t entryDuration,
                            uint64_t headerSize, uint64_t tailSize) const
    {
        return cpu_profiler.InHeaderOrTail(pc, entryBegin, entryDuration, headerSize, tailSize);
    }

    bool IfNeedSkipBarrierStubHeaderOrTailTest(JSThread *thread, uint64_t pc) const
    {
        return cpu_profiler.IfNeedSkipBarrierStubHeaderOrTail(thread, pc);
    }

    bool IsEntryFrameHeaderOrTailTest(JSThread *thread, uint64_t pc) const
    {
        return cpu_profiler.IsEntryFrameHeaderOrTail(thread, pc);
    }

    void SetBuildNapiStackTest(bool flag)
    {
        cpu_profiler.SetBuildNapiStack(flag);
    }

    void RebindSignalContextTest(uint64_t newTid)
    {
        cpu_profiler.RebindSignalContext(newTid);
    }

private:
    CpuProfiler cpu_profiler;
};
}

namespace panda::test {
class CpuProfilerTest : public testing::Test {
public:
    static void SetUpTestCase()
    {
        GTEST_LOG_(INFO) << "SetUpTestCase";
    }

    static void TearDownTestCase()
    {
        GTEST_LOG_(INFO) << "TearDownCase";
    }

    void SetUp() override
    {
        TestHelper::CreateEcmaVMWithScope(instance, thread, scope);
        instance->SetEnableForceGC(false);
    }

    void TearDown() override
    {
        TestHelper::DestroyEcmaVMWithScope(instance, scope);
    }

    EcmaVM *instance {nullptr};
    EcmaHandleScope *scope {nullptr};
    JSThread *thread {nullptr};
};

HWTEST_F_L0(CpuProfilerTest, TestStopCpuProfilerForFile)
{
    int interval = 1;
    bool ret;
    CpuProfilerFriendTest cpuProfilerFriend(instance, interval);
    ret = cpuProfilerFriend.StopCpuProfilerForFileTest();
    EXPECT_TRUE(ret);

    cpuProfilerFriend.SetIsProfiling(true);
    ret = cpuProfilerFriend.StopCpuProfilerForFileTest();
    EXPECT_FALSE(ret);
}

HWTEST_F_L0(CpuProfilerTest, TestStartCpuProfilerForFile)
{
    int interval = 1;
    bool ret;
    CpuProfilerFriendTest cpuProfilerFriend(instance, interval);
    std::string filename1("__CpuprofilerTest.abc");
    std::string filename2("");
    std::string filename3(PATH_MAX + 1, ' ');

    ret = cpuProfilerFriend.StartCpuProfilerForFileTest(filename1);
    EXPECT_FALSE(ret);
    ret = cpuProfilerFriend.StartCpuProfilerForFileTest(filename2);
    EXPECT_FALSE(ret);
    ret = cpuProfilerFriend.StartCpuProfilerForFileTest(filename3);
    EXPECT_FALSE(ret);

    cpuProfilerFriend.SetIsProfiling(true);
    ret = cpuProfilerFriend.StartCpuProfilerForFileTest(filename1);
    EXPECT_FALSE(ret);
}

HWTEST_F_L0(CpuProfilerTest, TestStopCpuProfilerForInfo)
{
    int interval = 1;
    bool ret;
    CpuProfilerFriendTest cpuProfilerFriend(instance, interval);
    std::unique_ptr<ProfileInfo> profileInfo;

    ret = cpuProfilerFriend.StopCpuProfilerForInfoTest(profileInfo);
    EXPECT_TRUE(ret);

    ret = DFXJSNApi::StartCpuProfilerForInfo(instance, interval);
    EXPECT_TRUE(ret);
    profileInfo = DFXJSNApi::StopCpuProfilerForInfo(instance);
    EXPECT_NE(profileInfo, nullptr);
    
    cpuProfilerFriend.SetIsProfiling(true);
    cpuProfilerFriend.SetOutToFile(true);
    ret = cpuProfilerFriend.StopCpuProfilerForInfoTest(profileInfo);
    EXPECT_FALSE(ret);
}

HWTEST_F_L0(CpuProfilerTest, TestGetStackBeforeCallNapi)
{
    uint32_t interval = 100;
    CpuProfilerFriendTest cpuProfilerFriend(instance, interval);

    bool flag = cpuProfilerFriend.GetStackBeforeCallNapiTest(thread);
    EXPECT_TRUE(flag);
    
    cpuProfilerFriend.SetInterval(UINT32_MAX);
    flag = cpuProfilerFriend.GetStackBeforeCallNapiTest(thread);
    EXPECT_FALSE(flag);
}

HWTEST_F_L0(CpuProfilerTest, TestCpuProfilerHasSameName)
{
    int interval = 500;

    bool res = DFXJSNApi::StartCpuProfilerForInfo(instance, interval);
    EXPECT_TRUE(res);
    // The cpuprofiler_1.js only support test A->B->C, not support test A->C, B->C
    std::string fileName = DEBUGGER_ABC_DIR "cpuprofiler_1.abc";
    std::string entryPoint = "cpuprofiler_1";
    res = JSNApi::Execute(instance, fileName, entryPoint);
    ASSERT_TRUE(res);
    std::unique_ptr<ProfileInfo> profileInfo = DFXJSNApi::StopCpuProfilerForInfo(instance);
    size_t nodesLen = profileInfo->nodeCount;
    std::unordered_set<std::string> functionNames;
    // Check if the function has multiple nodes
    for (size_t i = 0; i < nodesLen; ++i) {
        const auto &cpuProfileNode = profileInfo->nodes[i];
        std::string functionName = cpuProfileNode.codeEntry.functionName;
        auto node = functionNames.find(functionName);
        if (node == functionNames.end()) {
            functionNames.insert(functionName);
        } else {
            res = false;
            break;
        }
    }
    ASSERT_TRUE(res);
}

#if defined(ECMASCRIPT_SUPPORT_CPUPROFILER_LEGACY)
HWTEST_F_L0(CpuProfilerTest, TestCpuProfilerHasSameNameLegacy)
{
    // Legacy scheme build (ECMASCRIPT_SUPPORT_CPUPROFILER_LEGACY): the plain
    // Start interface runs the legacy scheme, keeping the in-handler
    // symbolization baseline covered.
    int interval = 500;

    bool res = DFXJSNApi::StartCpuProfilerForInfo(instance, interval);
    EXPECT_TRUE(res);
    std::string fileName = DEBUGGER_ABC_DIR "cpuprofiler_1.abc";
    std::string entryPoint = "cpuprofiler_1";
    res = JSNApi::Execute(instance, fileName, entryPoint);
    ASSERT_TRUE(res);
    std::unique_ptr<ProfileInfo> profileInfo = DFXJSNApi::StopCpuProfilerForInfo(instance);
    ASSERT_NE(profileInfo, nullptr);
    EXPECT_GT(profileInfo->nodeCount, 0);
}
#endif // ECMASCRIPT_SUPPORT_CPUPROFILER_LEGACY

#if !defined(ECMASCRIPT_SUPPORT_CPUPROFILER_LEGACY)
HWTEST_F_L0(CpuProfilerTest, TestTimeDeltasMonotonic)
{
    int interval = 1; // 1: minimal interval to force interleaved late samples
    uint32_t violationsBefore = GetProfilerSignalHandlerViolationsForTest();
    bool res = DFXJSNApi::StartCpuProfilerForInfo(instance, interval);
    EXPECT_TRUE(res);
    std::string fileName = DEBUGGER_ABC_DIR "cpuprofiler_1.abc";
    std::string entryPoint = "cpuprofiler_1";
    res = JSNApi::Execute(instance, fileName, entryPoint);
    ASSERT_TRUE(res);
    std::unique_ptr<ProfileInfo> profileInfo = DFXJSNApi::StopCpuProfilerForInfo(instance);
    ASSERT_NE(profileInfo, nullptr);
    ASSERT_FALSE(profileInfo->timeDeltas.empty());
    // Fire-and-forget consumption must never emit non-monotonic timestamps:
    // negative deltas would poison the uint64 state-time statistics.
    for (size_t i = 0; i < profileInfo->timeDeltas.size(); ++i) {
        EXPECT_GE(profileInfo->timeDeltas[i], MIN_TIME_DELTA) << "timeDelta[" << i << "]";
    }
    // SC2: aggressive real sampling must not trip any interception hook.
    EXPECT_EQ(GetProfilerSignalHandlerViolationsForTest(), violationsBefore);
}
#endif // !defined(ECMASCRIPT_SUPPORT_CPUPROFILER_LEGACY)

HWTEST_F_L0(CpuProfilerTest, TestNoBarrierAccessorsMatchBarriered)
{
    // Both accessor families must return the same results in normal thread
    // context (no GC racing): the no-barrier variants only differ in skipping
    // the read barrier, never in the resolved identity.
    EcmaVM *vm = thread->GetEcmaVM();
    JSHandle<JSFunction> func = vm->GetFactory()->NewJSFunction(
        vm->GetGlobalEnv(), nullptr);

    Method *method = func->GetCallTarget(thread);
    ASSERT_NE(method, nullptr);

    const JSPandaFile *barriered = method->GetJSPandaFile(thread);
    const JSPandaFile *raw = method->GetJSPandaFileNoBarrier();
    EXPECT_EQ(barriered, raw);

    MethodLiteral *barrieredLiteral = method->GetMethodLiteral(thread);
    MethodLiteral *rawLiteral = method->GetMethodLiteralNoBarrier();
    EXPECT_EQ(barrieredLiteral, rawLiteral);
}

HWTEST_F_L0(CpuProfilerTest, TestSignalSafeFrameIteratorValidation)
{
    // Null sp invalidates the iterator.
    SignalSafeProfilerFrameIterator nullIt(nullptr, thread);
    EXPECT_TRUE(nullIt.Done());

    // A heap allocation is outside the thread stack bounds.
    auto *heapFake = new uint64_t[2]; // 2: one frame slot
    SignalSafeProfilerFrameIterator heapIt(reinterpret_cast<JSTaggedType *>(heapFake), thread);
    EXPECT_TRUE(heapIt.Done());
    delete[] heapFake;

    // In-bounds sp whose frame-type slot decodes outside the valid enum range.
    JSTaggedType locals[4] = {0x9999, 0x9999, 0, 0}; // 4: type word sits before sp
    SignalSafeProfilerFrameIterator garbageIt(&locals[2], thread);
    EXPECT_TRUE(garbageIt.Done());

    // Misaligned sp is rejected up front.
    SignalSafeProfilerFrameIterator misalignedIt(
        reinterpret_cast<JSTaggedType *>(reinterpret_cast<uintptr_t>(locals) + sizeof(JSTaggedType) / 2),
        thread);
    EXPECT_TRUE(misalignedIt.Done());
}

/**
 * @tc.name: CpuProfilerHybridStackTest_ProcessStaticFrame_NullPointer
 * @tc.desc: Test the handling of null pointers in ProcessStaticFrame.
             A null pointer input should not cause a crash, and the function should return safely.
 * @tc.type: FUNC
 */
HWTEST_F_L0(CpuProfilerTest, CpuProfilerHybridStackTest_ProcessStaticFrame_NullPointer)
{
    int interval = 500;
    CpuProfilerFriendTest cpuProfilerFriend(instance, interval);

    int frameCountBefore = cpuProfilerFriend.GetFrameStackLengthTest();

    cpuProfilerFriend.ProcessStaticFrameTest(nullptr);

    int frameCountAfter = cpuProfilerFriend.GetFrameStackLengthTest();

    EXPECT_EQ(frameCountBefore, frameCountAfter);
}

/**
 * @tc.name: CpuProfilerHybridStackTest_ProcessDynamicFrame_NullPointer
 * @tc.desc: Test the handling of null pointers in ProcessDynamicFrame.
             A null pointer input should not cause a crash, and the function should return safely.
 * @tc.type: FUNC
 */
HWTEST_F_L0(CpuProfilerTest, CpuProfilerHybridStackTest_ProcessDynamicFrame_NullPointer)
{
    int interval = 500;
    CpuProfilerFriendTest cpuProfilerFriend(instance, interval);

    int frameCountBefore = cpuProfilerFriend.GetFrameStackLengthTest();

    cpuProfilerFriend.ProcessDynamicFrameTest(nullptr, thread);

    int frameCountAfter = cpuProfilerFriend.GetFrameStackLengthTest();

    EXPECT_EQ(frameCountBefore, frameCountAfter);
}

/**
 * @tc.name: CpuProfilerHybridStackTest_ProcessHybridStack_NullThread
 * @tc.desc: Test the handling of null threads in ProcessHybridStack.
             A null thread input should not cause a crash, and the function should return safely.
 * @tc.type: FUNC
 */
HWTEST_F_L0(CpuProfilerTest, CpuProfilerHybridStackTest_ProcessHybridStack_NullThread)
{
    int interval = 500;
    CpuProfilerFriendTest cpuProfilerFriend(instance, interval);

    int frameCountBefore = cpuProfilerFriend.GetFrameStackLengthTest();

    cpuProfilerFriend.ProcessHybridStackTest(nullptr);

    int frameCountAfter = cpuProfilerFriend.GetFrameStackLengthTest();

    EXPECT_EQ(frameCountBefore, frameCountAfter);
}

/**
 * @tc.name: CpuProfilerHybridStackTest_ProcessHybridStack_NormalThread
 * @tc.desc: Test the handling of normal threads in ProcessHybridStack.
 *           The length of the frame stack should remain consistent after normal thread processing(not hybrid stack).
 * @tc.type: FUNC
 */
HWTEST_F_L0(CpuProfilerTest, CpuProfilerHybridStackTest_ProcessHybridStack_NormalThread)
{
    int interval = 500;
    CpuProfilerFriendTest cpuProfilerFriend(instance, interval);

    int frameCountBefore = cpuProfilerFriend.GetFrameStackLengthTest();

    cpuProfilerFriend.ProcessHybridStackTest(thread);

    int frameCountAfter = cpuProfilerFriend.GetFrameStackLengthTest();

    EXPECT_GE(frameCountAfter, frameCountBefore);
}

/**
 * @tc.name: CpuProfilerHybridStackTest_ProfileInfoValidity
 * @tc.desc: Test the basic validity of the ProfileInfo structure.
 *           The nodeCount should be >= 0, and the nodes array should be valid.
 * @tc.type: FUNC
 */
HWTEST_F_L0(CpuProfilerTest, CpuProfilerHybridStackTest_ProfileInfoValidity)
{
    int interval = 500;
    bool started = DFXJSNApi::StartCpuProfilerForInfo(instance, interval);
    ASSERT_TRUE(started);

    std::unique_ptr<ProfileInfo> profileInfo = DFXJSNApi::StopCpuProfilerForInfo(instance);
    ASSERT_NE(profileInfo, nullptr);

    EXPECT_GE(profileInfo->nodeCount, 0);
    EXPECT_LE(profileInfo->nodeCount, MAX_NODE_COUNT);
}

HWTEST_F_L0(CpuProfilerTest, TestSignalGuardInterception)
{
    // SC2 negative control: locking in normal context never trips the guard.
    uint32_t before = GetProfilerSignalHandlerViolationsForTest();
    {
        Mutex probe;
        LockHolder holder(probe);
    }
    RWLock rwprobe;
    rwprobe.ReadLock();
    rwprobe.Unlock();
    EXPECT_EQ(GetProfilerSignalHandlerViolationsForTest(), before);

#ifndef NDEBUG
    // SC2 positive control: the same operations under the simulated handler
    // flag must be counted (exactly the hook the signal path would trip).
    SetProfilerSignalGuardForTest(true);
    {
        Mutex probe;
        LockHolder holder(probe);
    }
    SetProfilerSignalGuardForTest(false);
    EXPECT_EQ(GetProfilerSignalHandlerViolationsForTest(), before + 1);
#endif
}

#if !defined(ECMASCRIPT_SUPPORT_CPUPROFILER_LEGACY)
HWTEST_F_L0(CpuProfilerTest, TestDeepRecursionSampling)
{
    // Drive the JS stack to its limit while sampling at the highest rate: the
    // handler runs on the interrupted thread's own stack and must stay within
    // the runtime's JS-stack-limit headroom. SC2: no lock/handle hook may fire.
    uint32_t before = GetProfilerSignalHandlerViolationsForTest();
    bool started = DFXJSNApi::StartCpuProfilerForInfo(instance, 1);
    ASSERT_TRUE(started);
    std::string fileName = DEBUGGER_ABC_DIR "cpuprofiler_deep_recursion.abc";
    std::string entryPoint = "cpuprofiler_deep_recursion";
    bool res = JSNApi::Execute(instance, fileName, entryPoint);
    ASSERT_TRUE(res);
    std::unique_ptr<ProfileInfo> profileInfo = DFXJSNApi::StopCpuProfilerForInfo(instance);
    ASSERT_NE(profileInfo, nullptr);
    EXPECT_EQ(GetProfilerSignalHandlerViolationsForTest(), before);
    EXPECT_GT(profileInfo->nodeCount, 0);
}
#endif // !defined(ECMASCRIPT_SUPPORT_CPUPROFILER_LEGACY)

#if !defined(ECMASCRIPT_SUPPORT_CPUPROFILER_LEGACY)
HWTEST_F_L0(CpuProfilerTest, TestProcessRawSampleDropsHandlerMarkedSamples)
{
    // raw_sample.h contract: handler-marked samples must be dropped by the
    // processor instead of entering the profile as idle samples or partial
    // INVALID_FRAME walks.
    int interval = 500;
    CpuProfilerFriendTest cpuProfilerFriend(instance, interval);

    // Regression guard for the session filter that runs before the status
    // filter: a stale-generation sample is dropped first (an unstarted session
    // keeps generation 0).
    RawSample staleSession;
    staleSession.profilerGeneration = 1;
    cpuProfilerFriend.ProcessRawSampleTest(staleSession);

    const SampleStatus dropStatuses[] = {
        SampleStatus::GC_SKIPPED,
        SampleStatus::STACK_NOT_ITERABLE,
        SampleStatus::INVALID_FRAME,
        SampleStatus::UNSUPPORTED_HYBRID_FRAME,
    };
    for (SampleStatus status : dropStatuses) {
        RawSample dropped;
        dropped.status = status;
        if (status == SampleStatus::INVALID_FRAME) {
            // INVALID_FRAME keeps the frames collected before the walk aborted;
            // they must not be symbolized into the tree either.
            dropped.frameCount = 2; // 2: partial walk
        }
        cpuProfilerFriend.ProcessRawSampleTest(dropped);
    }

    // Positive control: an OK empty sample does enter the profile as one
    // (program) idle sample, proving the drops above are caused by status.
    RawSample okEmpty;
    okEmpty.status = SampleStatus::OK;
    okEmpty.timestamp = 1000; // 1000: small delta against the zero start time
    cpuProfilerFriend.ProcessRawSampleTest(okEmpty);

    std::unique_ptr<ProfileInfo> profileInfo = cpuProfilerFriend.GetProfileInfoTest();
    ASSERT_NE(profileInfo, nullptr);
    ASSERT_EQ(profileInfo->samples.size(), 1U);
    EXPECT_EQ(profileInfo->samples[0], PROGRAM_NODE_ID);
    EXPECT_EQ(profileInfo->nodes[PROGRAM_NODE_ID - 1].hitCount, 1);
}
#endif // !defined(ECMASCRIPT_SUPPORT_CPUPROFILER_LEGACY)

// InHeaderOrTail classifies the header/tail windows around an entry.
HWTEST_F_L0(CpuProfilerTest, TestInHeaderOrTail)
{
    int interval = 500;
    CpuProfilerFriendTest cpuProfilerFriend(instance, interval);
    constexpr uint64_t entryBegin = 0x1000;
    constexpr uint64_t entryDuration = 0x100;
    constexpr uint64_t headerSize = 0x10;
    constexpr uint64_t tailSize = 0x20;

    EXPECT_TRUE(cpuProfilerFriend.InHeaderOrTailTest(entryBegin, entryBegin, entryDuration,
                                                     headerSize, tailSize));
    EXPECT_TRUE(cpuProfilerFriend.InHeaderOrTailTest(entryBegin + headerSize, entryBegin, entryDuration,
                                                     headerSize, tailSize));
    EXPECT_TRUE(cpuProfilerFriend.InHeaderOrTailTest(entryBegin + entryDuration, entryBegin, entryDuration,
                                                     headerSize, tailSize));
    EXPECT_TRUE(cpuProfilerFriend.InHeaderOrTailTest(entryBegin + entryDuration - tailSize, entryBegin,
                                                     entryDuration, headerSize, tailSize));
    EXPECT_FALSE(cpuProfilerFriend.InHeaderOrTailTest(entryBegin + headerSize + 1, entryBegin, entryDuration,
                                                      headerSize, tailSize));
    EXPECT_FALSE(cpuProfilerFriend.InHeaderOrTailTest(entryBegin - 1, entryBegin, entryDuration,
                                                      headerSize, tailSize));
    EXPECT_FALSE(cpuProfilerFriend.InHeaderOrTailTest(entryBegin + entryDuration + 1, entryBegin,
                                                      entryDuration, headerSize, tailSize));
    EXPECT_FALSE(cpuProfilerFriend.InHeaderOrTailTest(entryBegin + entryDuration - tailSize - 1, entryBegin,
                                                      entryDuration, headerSize, tailSize));
}

// IfNeedSkipBarrierStubHeaderOrTail matches pcs inside the GetValueWithBarrier
// stub entry header window only.
HWTEST_F_L0(CpuProfilerTest, TestIfNeedSkipBarrierStubHeaderOrTail)
{
    int interval = 500;
    CpuProfilerFriendTest cpuProfilerFriend(instance, interval);
    uintptr_t entryBegin = thread->GetFastStubEntry(kungfu::CommonStubCSigns::ID::GetValueWithBarrier);
    constexpr uint64_t headerSize = 20; // 20: same window as the implementation

    EXPECT_TRUE(cpuProfilerFriend.IfNeedSkipBarrierStubHeaderOrTailTest(thread, entryBegin));
    EXPECT_TRUE(cpuProfilerFriend.IfNeedSkipBarrierStubHeaderOrTailTest(thread, entryBegin + headerSize));
    EXPECT_FALSE(cpuProfilerFriend.IfNeedSkipBarrierStubHeaderOrTailTest(thread, entryBegin + headerSize + 1));
    EXPECT_FALSE(cpuProfilerFriend.IfNeedSkipBarrierStubHeaderOrTailTest(thread, entryBegin - 1));
}

// IsEntryFrameHeaderOrTail matches pcs inside the interpreter entry windows.
HWTEST_F_L0(CpuProfilerTest, TestIsEntryFrameHeaderOrTail)
{
    int interval = 500;
    CpuProfilerFriendTest cpuProfilerFriend(instance, interval);
    uintptr_t entryBegin = thread->GetRTInterface(kungfu::RuntimeStubCSigns::ID_AsmInterpreterEntry);
    uintptr_t reEnterBegin = thread->GetRTInterface(kungfu::RuntimeStubCSigns::ID_GeneratorReEnterAsmInterp);

    EXPECT_TRUE(cpuProfilerFriend.IsEntryFrameHeaderOrTailTest(thread, entryBegin));
    EXPECT_TRUE(cpuProfilerFriend.IsEntryFrameHeaderOrTailTest(thread, reEnterBegin));
    EXPECT_FALSE(cpuProfilerFriend.IsEntryFrameHeaderOrTailTest(thread, entryBegin + 0x100000)); // 1MB away
    EXPECT_FALSE(cpuProfilerFriend.IsEntryFrameHeaderOrTailTest(thread, 0));
}

// GetPcFromContext extracts the interrupted pc from a ucontext.
HWTEST_F_L0(CpuProfilerTest, TestGetPcFromContext)
{
#if defined(PANDA_TARGET_AMD64)
    ucontext_t context;
    memset_s(&context, sizeof(context), 0, sizeof(context));
    constexpr uint64_t expectedPc = 0x12345678abcdef00ULL;
    context.uc_mcontext.gregs[REG_RIP] = static_cast<greg_t>(expectedPc);
    EXPECT_EQ(CpuProfilerFriendTest::GetPcFromContextTest(&context), expectedPc);
#else
    ASSERT_TRUE(true);
#endif
}

// GetVmbyTid returns nullptr for tids without a profiling session.
HWTEST_F_L0(CpuProfilerTest, TestGetVmbyTidUnknownTid)
{
    EXPECT_EQ(CpuProfiler::GetVmbyTid(0), nullptr);
    EXPECT_EQ(CpuProfiler::GetVmbyTid(static_cast<pthread_t>(0xDEADBEEF)), nullptr);
}

#if !defined(ECMASCRIPT_SUPPORT_CPUPROFILER_LEGACY)
// CollectSampleInto drops the sample with GC_SKIPPED while the thread is in GC
// or while an active NAPI stack is being built.
HWTEST_F_L0(CpuProfilerTest, TestCollectSampleIntoSkipsGcAndNapi)
{
    int interval = 500;
    CpuProfilerFriendTest cpuProfilerFriend(instance, interval);
    CpuProfilerSignalContext *ctx = cpuProfilerFriend.GetSignalCtx();
    ctx->jsThread = thread;

    {
        GcStateScope gcScope(thread);
        RawSample gcSample;
        cpuProfilerFriend.CollectSampleIntoTest(gcSample, ctx, nullptr);
        EXPECT_EQ(gcSample.status, SampleStatus::GC_SKIPPED);
        EXPECT_EQ(ctx->dropGcSkipped.load(std::memory_order_relaxed), 1U);
    }

    cpuProfilerFriend.SetBuildNapiStackTest(true);
    RawSample napiSample;
    cpuProfilerFriend.CollectSampleIntoTest(napiSample, ctx, nullptr);
    EXPECT_EQ(napiSample.status, SampleStatus::GC_SKIPPED);
    cpuProfilerFriend.SetBuildNapiStackTest(false);
}
#endif // !defined(ECMASCRIPT_SUPPORT_CPUPROFILER_LEGACY)

#if !defined(ECMASCRIPT_SUPPORT_CPUPROFILER_LEGACY)
// WaitHandlersDrained gives up after the bounded poll budget when a handler
// never leaves.
HWTEST_F_L0(CpuProfilerTest, TestWaitHandlersDrainedTimesOut)
{
    int interval = 500;
    CpuProfilerFriendTest cpuProfilerFriend(instance, interval);
    CpuProfilerSignalContext *ctx = cpuProfilerFriend.GetSignalCtx();
    ctx->activeHandlerCount.store(1, std::memory_order_release);
    cpuProfilerFriend.WaitHandlersDrainedTest();
    ctx->activeHandlerCount.store(0, std::memory_order_release);
    SUCCEED();
}
#endif // !defined(ECMASCRIPT_SUPPORT_CPUPROFILER_LEGACY)

// GetMethodLiteralNoBarrier: the deopt/AOT path resolves the literal through
// the constant pool and returns nullptr when the pool is missing.
HWTEST_F_L0(CpuProfilerTest, TestGetMethodLiteralNoBarrierDeoptAndAot)
{
    auto factory = instance->GetFactory();
    JSHandle<GlobalEnv> globalEnv = instance->GetGlobalEnv();

    // Factory method without a constant pool: the fallback returns nullptr.
    JSHandle<JSFunction> plainFunc = factory->NewJSFunction(globalEnv);
    Method *plainMethod = plainFunc->GetCallTarget(thread);
    ASSERT_NE(plainMethod, nullptr);
    plainMethod->SetDeoptType(kungfu::DeoptType::LAZYDEOPT);
    EXPECT_TRUE(plainMethod->IsDeoptimized());
    EXPECT_EQ(plainMethod->GetJSPandaFileNoBarrier(), nullptr);
    EXPECT_EQ(plainMethod->GetMethodLiteralNoBarrier(), nullptr);
    plainMethod->SetDeoptType(kungfu::DeoptType::NONE);

    // Method bound to a real JSPandaFile constant pool: both the deopt and the
    // AOT-with-callfield paths resolve back to the owning literal.
    ASSERT_TRUE(JSNApi::Execute(instance, DEBUGGER_ABC_DIR "cpuprofiler_1.abc", "cpuprofiler_1"));
    const JSPandaFile *pf = nullptr;
    JSPandaFileManager::GetInstance()->EnumerateJSPandaFiles(
        [&pf](const std::shared_ptr<JSPandaFile> &file) -> bool {
            if (file->GetJSPandaFileDesc().find("cpuprofiler_1.abc") != CString::npos) {
                pf = file.get();
            }
            return true;
        });
    ASSERT_NE(pf, nullptr);
    ASSERT_GT(pf->GetNumMethods(), 0U);
    MethodLiteral *literal = pf->GetMethodLiterals();

    JSHandle<Method> methodHandle = factory->NewMethod(literal);
    JSHandle<ConstantPool> constpool = factory->NewConstantPool(1); // 1: minimal pool
    constpool->SetJSPandaFile(pf);
    methodHandle->SetConstantPool(thread, constpool.GetTaggedValue());
    ASSERT_EQ(methodHandle->GetJSPandaFileNoBarrier(), pf);

    methodHandle->SetDeoptType(kungfu::DeoptType::LAZYDEOPT);
    EXPECT_EQ(methodHandle->GetMethodLiteralNoBarrier(), literal);
    EXPECT_EQ(methodHandle->GetMethodLiteral(thread), literal);

    methodHandle->SetDeoptType(kungfu::DeoptType::NONE);
    methodHandle->SetAotCodeBit(true);
    EXPECT_TRUE(methodHandle->IsAotWithCallField());
    EXPECT_EQ(methodHandle->GetMethodLiteralNoBarrier(), literal);
    methodHandle->SetAotCodeBit(false);
}

#if !defined(ECMASCRIPT_SUPPORT_CPUPROFILER_LEGACY)
// SamplingProcessor::AddSample folds an empty queue into an idle sample and
// drains queued NAPI frames into the profile tree.
HWTEST_F_L0(CpuProfilerTest, TestSamplingProcessorAddSample)
{
    int interval = 500;
    CpuProfilerFriendTest cpuProfilerFriend(instance, interval);
    SamplesRecord *generator = cpuProfilerFriend.GetGenerator();

    // Empty queue: one (program) idle sample is recorded.
    SamplingProcessor::AddSample(generator);
    EXPECT_TRUE(generator->samplesQueue_->IsEmpty());

    // Queued NAPI frames: all of them are folded into the tree.
    struct MethodKey key;
    key.methodIdentifier = reinterpret_cast<void *>(0x12340000);
    key.state = RunningState::NAPI;
    struct FrameInfoTemp info;
    strcpy_s(info.functionName, sizeof(info.functionName), "napi_addsample_func");
    info.methodKey = key;
    ASSERT_TRUE(generator->PushNapiFrameStack(key));
    ASSERT_TRUE(generator->PushNapiStackInfo(info));
    generator->PostNapiFrame();
    ASSERT_FALSE(generator->samplesQueue_->IsEmpty());

    SamplingProcessor::AddSample(generator);
    EXPECT_TRUE(generator->samplesQueue_->IsEmpty());

    std::unique_ptr<ProfileInfo> profileInfo = generator->GetProfileInfo();
    ASSERT_NE(profileInfo, nullptr);
    EXPECT_GE(profileInfo->nodeCount, 4); // 4: root/program/idle plus the NAPI node
    bool found = false;
    for (int i = 0; i < profileInfo->nodeCount; ++i) {
        if (strstr(profileInfo->nodes[i].codeEntry.functionName.c_str(), "napi_addsample_func") != nullptr) {
            found = true;
        }
    }
    EXPECT_TRUE(found);
}
#endif // !defined(ECMASCRIPT_SUPPORT_CPUPROFILER_LEGACY)

#if !defined(ECMASCRIPT_SUPPORT_CPUPROFILER_LEGACY)
// The stop path folds legacy-channel frames that were posted after the last
// in-loop ConsumeAndFold tick.
HWTEST_F_L0(CpuProfilerTest, TestStopFoldsResidualNapiFrames)
{
    int interval = 500;
    CpuProfilerFriendTest cpuProfilerFriend(instance, interval);
    ASSERT_TRUE(cpuProfilerFriend.StartProfilerForInfoTest());
    SamplesRecord *generator = cpuProfilerFriend.GetGenerator();

    cpuProfilerFriend.BreakSamplingLoop();
    CpuProfilerFriendTest::SleepThroughSignalInterruptions(50000); // 50ms: thread exits

    // Post frames that only the stop path can fold anymore.
    struct MethodKey key;
    key.methodIdentifier = reinterpret_cast<void *>(0x56780000);
    key.state = RunningState::NAPI;
    struct FrameInfoTemp info;
    strcpy_s(info.functionName, sizeof(info.functionName), "napi_residual_func");
    info.methodKey = key;
    ASSERT_TRUE(generator->PushNapiFrameStack(key));
    ASSERT_TRUE(generator->PushNapiStackInfo(info));
    generator->PostNapiFrame();
    ASSERT_FALSE(generator->samplesQueue_->IsEmpty());

    std::unique_ptr<ProfileInfo> profileInfo;
    ASSERT_TRUE(cpuProfilerFriend.StopCpuProfilerForInfoTest(profileInfo));
    ASSERT_NE(profileInfo, nullptr);
    EXPECT_TRUE(generator->samplesQueue_->IsEmpty());
    bool found = false;
    for (int i = 0; i < profileInfo->nodeCount; ++i) {
        if (strstr(profileInfo->nodes[i].codeEntry.functionName.c_str(), "napi_residual_func") != nullptr) {
            found = true;
        }
    }
    EXPECT_TRUE(found);
}
#endif // !defined(ECMASCRIPT_SUPPORT_CPUPROFILER_LEGACY)

#if !defined(ECMASCRIPT_SUPPORT_CPUPROFILER_LEGACY)
// The sampling loop stops by itself once the profile tree hits MAX_NODE_COUNT
// and Stop still completes the handshake afterwards.
HWTEST_F_L0(CpuProfilerTest, TestProfileTreeLimitStopsSamplingLoop)
{
    int interval = 500;
    CpuProfilerFriendTest cpuProfilerFriend(instance, interval);
    ASSERT_TRUE(cpuProfilerFriend.StartProfilerForInfoTest());
    SamplesRecord *generator = cpuProfilerFriend.GetGenerator();

    // Feed unique frames through the legacy queue in bounded rounds: the queue
    // silently drops when full, so pace the rounds by tick intervals.
    constexpr int framesPerSample = 16; // 16: unique keys per queue item
    constexpr int itemsNeeded = MAX_NODE_COUNT / framesPerSample + 8; // 8: slack
    constexpr int batch = 40; // 40: below QUEUE_CAPACITY-1
    int posted = 0;
    uintptr_t id = 0x100000;
    while (posted < itemsNeeded) {
        int round = std::min(batch, itemsNeeded - posted);
        for (int i = 0; i < round; i++) {
            generator->ClearNapiStack();
            struct MethodKey key;
            key.methodIdentifier = reinterpret_cast<void *>(id);
            struct FrameInfoTemp info;
            strcpy_s(info.functionName, sizeof(info.functionName), "napi_tree_limit_func");
            info.methodKey = key;
            for (int f = 0; f < framesPerSample; f++) {
                key.methodIdentifier = reinterpret_cast<void *>(id + f);
                ASSERT_TRUE(generator->PushNapiFrameStack(key));
                ASSERT_TRUE(generator->PushNapiStackInfo(info));
            }
            id += framesPerSample;
            generator->PostNapiFrame();
        }
        posted += round;
        CpuProfilerFriendTest::SleepThroughSignalInterruptions(5000); // 5ms: ~10 ticks
    }

    // The loop must have hit the limit and exited by now.
    CpuProfilerFriendTest::SleepThroughSignalInterruptions(20000); // 20ms: limit fires
    std::unique_ptr<ProfileInfo> profileInfo;
    ASSERT_TRUE(cpuProfilerFriend.StopCpuProfilerForInfoTest(profileInfo));
    ASSERT_NE(profileInfo, nullptr);
    EXPECT_LE(profileInfo->nodeCount, MAX_NODE_COUNT);
    EXPECT_FALSE(profileInfo->samples.empty());
}
#endif // !defined(ECMASCRIPT_SUPPORT_CPUPROFILER_LEGACY)

#if !defined(ECMASCRIPT_SUPPORT_CPUPROFILER_LEGACY)
// ProcessRawSample maps every RawRunningState to its profile-facing state and
// folds one sample per state into the tree.
HWTEST_F_L0(CpuProfilerTest, TestProcessRawSampleAllRunningStates)
{
    int interval = 500;
    CpuProfilerFriendTest cpuProfilerFriend(instance, interval);

    const RawRunningState states[] = {
        RawRunningState::OTHER,
        RawRunningState::GC,
        RawRunningState::CINT,
        RawRunningState::AINT,
        RawRunningState::AOT,
        RawRunningState::AINT_D,
        RawRunningState::BUILTIN,
        RawRunningState::NAPI,
        RawRunningState::ARKUI_ENGINE,
        RawRunningState::RUNTIME,
        RawRunningState::JIT,
    };
    for (RawRunningState state : states) {
        RawSample sample;
        sample.timestamp = SamplingProcessor::GetMicrosecondsTimeStamp();
        sample.frameCount = 2; // 2: one interpreter frame plus a builtin caller
        sample.frames[0].value = 0x2000;
        sample.frames[0].kind = RawFrameKind::BUILTIN;
        sample.frames[0].state = state;
        sample.frames[1].value = 0x3000;
        sample.frames[1].kind = RawFrameKind::INTERPRETER;
        sample.frames[1].state = state;
        cpuProfilerFriend.ProcessRawSampleTest(sample);
    }

    std::unique_ptr<ProfileInfo> profileInfo = cpuProfilerFriend.GetProfileInfoTest();
    ASSERT_NE(profileInfo, nullptr);
    // 11 samples folded, one per state, each hitting its own leaf node.
    EXPECT_EQ(profileInfo->samples.size(), 11U);
}
#endif // !defined(ECMASCRIPT_SUPPORT_CPUPROFILER_LEGACY)

#if !defined(ECMASCRIPT_SUPPORT_CPUPROFILER_LEGACY)
// With the VM-tag option enabled the handler classifies interpreted frames as
// AINT; files loaded before Start are seeded into the symbolizer session.
HWTEST_F_L0(CpuProfilerTest, TestVmTagSamplingClassifiesAint)
{
    auto &options = const_cast<EcmaVM *>(instance)->GetJSOptions();
    options.SetArkProperties(options.GetArkProperties() | ArkProperties::ENABLE_CPU_PROFILER_VM_TAG);

    // Load the abc first so StartProfilerCommon seeds the live-file set.
    std::string fileName = DEBUGGER_ABC_DIR "cpuprofiler_1.abc";
    std::string entryPoint = "cpuprofiler_1";
    ASSERT_TRUE(JSNApi::Execute(instance, fileName, entryPoint));

    CpuProfilerFriendTest cpuProfilerFriend(instance, 500);
    ASSERT_TRUE(cpuProfilerFriend.StartProfilerForInfoTest());
    ASSERT_TRUE(JSNApi::Execute(instance, fileName, entryPoint));
    CpuProfilerFriendTest::SleepThroughSignalInterruptions(30000); // 30ms of ticks

    std::unique_ptr<ProfileInfo> profileInfo;
    ASSERT_TRUE(cpuProfilerFriend.StopCpuProfilerForInfoTest(profileInfo));
    ASSERT_NE(profileInfo, nullptr);
    EXPECT_GT(profileInfo->nodeCount, 3); // 3: root/program/idle plus real frames
    EXPECT_FALSE(profileInfo->samples.empty());
}
#endif // !defined(ECMASCRIPT_SUPPORT_CPUPROFILER_LEGACY)

#if !defined(ECMASCRIPT_SUPPORT_CPUPROFILER_LEGACY)
// RebindSignalContext migrates the tid binding of a running session.
HWTEST_F_L0(CpuProfilerTest, TestRebindSignalContextMigratesBinding)
{
    int interval = 500;
    CpuProfilerFriendTest cpuProfilerFriend(instance, interval);
    ASSERT_TRUE(cpuProfilerFriend.StartProfilerForInfoTest());

    constexpr uint64_t migratedTid = 0x12345678; // 0x12345678: no real thread
    cpuProfilerFriend.RebindSignalContextTest(migratedTid);

    std::unique_ptr<ProfileInfo> profileInfo;
    ASSERT_TRUE(cpuProfilerFriend.StopCpuProfilerForInfoTest(profileInfo));
    ASSERT_NE(profileInfo, nullptr);
}
#endif // !defined(ECMASCRIPT_SUPPORT_CPUPROFILER_LEGACY)

#if !defined(ECMASCRIPT_SUPPORT_CPUPROFILER_LEGACY)
// Start refuses to begin a session when the signal-context table has no free
// slot left.
HWTEST_F_L0(CpuProfilerTest, TestStartFailsWhenSignalContextTableFull)
{
    int interval = 500;
    CpuProfilerFriendTest cpuProfilerFriend(instance, interval);
    CpuProfilerSignalContext *ctx = cpuProfilerFriend.GetSignalCtx();

    // Occupy all 32 table slots with unrelated keys.
    constexpr uint64_t baseFakeTid = 0x11110000; // 0x11110000: no real thread
    for (uint64_t i = 0; i < ProfilerSignalContextTable::TABLE_CAPACITY; i++) {
        g_profilerSignalContextTable.Publish(baseFakeTid + i, ctx);
    }
    EXPECT_FALSE(cpuProfilerFriend.StartProfilerForInfoTest());
    g_profilerSignalContextTable.UnpublishContext(ctx);
}
#endif // !defined(ECMASCRIPT_SUPPORT_CPUPROFILER_LEGACY)

#if !defined(ECMASCRIPT_SUPPORT_CPUPROFILER_LEGACY)
// The destructor's unbounded drain keeps polling while a handler is still
// active instead of tearing the context down under it.
HWTEST_F_L0(CpuProfilerTest, TestDestructorWaitsForActiveHandler)
{
    std::atomic<bool> handlerLeft {false};
    {
        CpuProfilerFriendTest cpuProfilerFriend(instance, 500);
        CpuProfilerSignalContext *ctx = cpuProfilerFriend.GetSignalCtx();
        ctx->activeHandlerCount.store(1, std::memory_order_release);
        std::thread handler([ctx, &handlerLeft]() {
            usleep(10000); // 10ms: the destructor polls across this window
            ctx->activeHandlerCount.store(0, std::memory_order_release);
            handlerLeft.store(true);
        });
        handler.detach();
        // The friend (and its context) is destroyed at scope end while the
        // count is still one: the drain loop must wait for the store above.
    }
    EXPECT_TRUE(handlerLeft.load());
}
#endif // !defined(ECMASCRIPT_SUPPORT_CPUPROFILER_LEGACY)

#if !defined(ECMASCRIPT_SUPPORT_CPUPROFILER_LEGACY)
// A pc inside the loaded stub's own machine-code window is reported by the
// try-lock range query (the stub address-range branch of StubRangeContains).
HWTEST_F_L0(CpuProfilerTest, TestTryInsideStubOrAotStubAddressRange)
{
    std::shared_ptr<StubFileInfo> stubInfo = AnFileDataManager::GetInstance()->SafeGetStubFileInfo();
    if (stubInfo == nullptr) {
        GTEST_LOG_(INFO) << "no stub file loaded in this environment, skipping";
        return;
    }
    uint64_t stubStart = stubInfo->GetAsmStubAddr();
    uint64_t stubSize = stubInfo->GetAsmStubSize();
    ASSERT_GT(stubSize, 0U);
    EXPECT_TRUE(AnFileDataManager::GetInstance()->SafeTryInsideStubOrAOT(
        static_cast<uintptr_t>(stubStart + stubSize / 2))); // 2: middle of the window
    EXPECT_FALSE(AnFileDataManager::GetInstance()->SafeTryInsideStubOrAOT(0x10)); // 0x10: unmapped
}
#endif // !defined(ECMASCRIPT_SUPPORT_CPUPROFILER_LEGACY)

}  // namespace panda::test
