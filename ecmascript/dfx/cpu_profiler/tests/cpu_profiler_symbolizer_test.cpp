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

#include <cstring>
#include <string>

#include "ecmascript/dfx/cpu_profiler/cpu_profiler_symbolizer.h"
#include "ecmascript/jspandafile/js_pandafile_manager.h"
#include "ecmascript/tests/test_helper.h"

using namespace panda::ecmascript;

namespace panda::test {
class CpuProfilerSymbolizerTest : public testing::Test {
public:
    static void SetUpTestCase()
    {
        GTEST_LOG_(INFO) << "SetUpTestCase";
    }

    static void TearDownTestCase()
    {
        GTEST_LOG_(INFO) << "TearDownCase";
    }

    void SetUp() override {}
    void TearDown() override {}

    static ProfilerMetadataRegistry::Event MakeEvent(uint64_t sequence, bool isDelete,
                                                      const JSPandaFile *file)
    {
        ProfilerMetadataRegistry::Event event;
        event.sequence = sequence;
        event.isDelete = isDelete;
        event.file = file;
        return event;
    }
};

HWTEST_F_L0(CpuProfilerSymbolizerTest, BeginSessionClearsState)
{
    CpuProfilerSymbolizer symbolizer;
    symbolizer.SeedLiveFile(reinterpret_cast<const JSPandaFile *>(0x1000));
    symbolizer.AdvanceCursor(5);
    ASSERT_TRUE(symbolizer.IsJSPandaFileLive(static_cast<uintptr_t>(0x1000)));
    EXPECT_EQ(symbolizer.MetadataCursor(), 5U);

    symbolizer.BeginSession();
    EXPECT_EQ(symbolizer.MetadataCursor(), 0U);
    EXPECT_FALSE(symbolizer.IsJSPandaFileLive(static_cast<uintptr_t>(0x1000)));
}

HWTEST_F_L0(CpuProfilerSymbolizerTest, SeedLiveFileAndLiveCheck)
{
    CpuProfilerSymbolizer symbolizer;
    symbolizer.BeginSession();
    const JSPandaFile *fileA = reinterpret_cast<const JSPandaFile *>(0x2000);
    const JSPandaFile *fileB = reinterpret_cast<const JSPandaFile *>(0x3000);
    symbolizer.SeedLiveFile(fileA);
    symbolizer.SeedLiveFile(fileB);

    EXPECT_TRUE(symbolizer.IsJSPandaFileLive(reinterpret_cast<uintptr_t>(fileA)));
    EXPECT_TRUE(symbolizer.IsJSPandaFileLive(reinterpret_cast<uintptr_t>(fileB)));
    EXPECT_FALSE(symbolizer.IsJSPandaFileLive(0));
    EXPECT_FALSE(symbolizer.IsJSPandaFileLive(static_cast<uintptr_t>(0x4000)));
}

HWTEST_F_L0(CpuProfilerSymbolizerTest, ApplyEventLoadAndUnload)
{
    CpuProfilerSymbolizer symbolizer;
    symbolizer.BeginSession();
    const JSPandaFile *fileA = reinterpret_cast<const JSPandaFile *>(0x2000);
    const JSPandaFile *fileB = reinterpret_cast<const JSPandaFile *>(0x3000);

    symbolizer.ApplyEvent(MakeEvent(1, false, fileA));
    EXPECT_TRUE(symbolizer.IsJSPandaFileLive(reinterpret_cast<uintptr_t>(fileA)));

    symbolizer.ApplyEvent(MakeEvent(2, false, fileB));
    EXPECT_TRUE(symbolizer.IsJSPandaFileLive(reinterpret_cast<uintptr_t>(fileB)));

    symbolizer.ApplyEvent(MakeEvent(3, true, fileA));
    EXPECT_FALSE(symbolizer.IsJSPandaFileLive(reinterpret_cast<uintptr_t>(fileA)));
    EXPECT_TRUE(symbolizer.IsJSPandaFileLive(reinterpret_cast<uintptr_t>(fileB)));

    // Reload after unload makes the file live again (real loader behavior).
    symbolizer.ApplyEvent(MakeEvent(4, false, fileA));
    EXPECT_TRUE(symbolizer.IsJSPandaFileLive(reinterpret_cast<uintptr_t>(fileA)));
}

HWTEST_F_L0(CpuProfilerSymbolizerTest, CursorAdvances)
{
    CpuProfilerSymbolizer symbolizer;
    symbolizer.BeginSession();
    EXPECT_EQ(symbolizer.MetadataCursor(), 0U);

    symbolizer.AdvanceCursor(3);
    EXPECT_EQ(symbolizer.MetadataCursor(), 3U);

    symbolizer.AdvanceCursor(0);
    EXPECT_EQ(symbolizer.MetadataCursor(), 3U);

    symbolizer.AdvanceCursor(7);
    EXPECT_EQ(symbolizer.MetadataCursor(), 10U);
}

HWTEST_F_L0(CpuProfilerSymbolizerTest, SymbolizeBuiltinPlaceholder)
{
    // A native frame must symbolize to the documented "(native) 0x..." placeholder
    // without touching any JS property (plan §7.3).
    CpuProfilerSymbolizer symbolizer;
    symbolizer.BeginSession();

    RawSample sample;
    sample.frameCount = 1;
    sample.frames[0].value = 0x1234000;
    sample.frames[0].kind = RawFrameKind::BUILTIN;
    sample.frames[0].jsPandaFile = 0;
    sample.frames[0].bytecodeOffset = 0;

    MethodKey methodKey;
    FrameInfoTemp codeEntry;
    methodKey.methodIdentifier = reinterpret_cast<void *>(sample.frames[0].value);
    methodKey.lineNumber = 0;
    ASSERT_TRUE(symbolizer.SymbolizeFrame(sample, 0, methodKey, codeEntry));
    EXPECT_STRNE(codeEntry.functionName, "");
    EXPECT_NE(strstr(codeEntry.functionName, "(native)"), nullptr);
    EXPECT_NE(strstr(codeEntry.functionName, "1234000"), nullptr);
}

HWTEST_F_L0(CpuProfilerSymbolizerTest, SymbolizeFrameRejectsNullFile)
{
    // Interpreter frame without a backing file cannot be resolved: rejected.
    CpuProfilerSymbolizer symbolizer;
    symbolizer.BeginSession();

    RawSample sample;
    sample.frameCount = 1;
    sample.frames[0].value = 0x1000;
    sample.frames[0].kind = RawFrameKind::INTERPRETER;
    sample.frames[0].jsPandaFile = 0;

    MethodKey methodKey;
    FrameInfoTemp codeEntry;
    methodKey.methodIdentifier = reinterpret_cast<void *>(sample.frames[0].value);
    methodKey.lineNumber = 0;
    EXPECT_FALSE(symbolizer.SymbolizeFrame(sample, 0, methodKey, codeEntry));
}

HWTEST_F_L0(CpuProfilerSymbolizerTest, ResolveNativeCallLineWithoutCaller)
{
    // Native frame with no caller (top of stack): call line is 0.
    CpuProfilerSymbolizer symbolizer;
    symbolizer.BeginSession();
    RawSample sample;
    sample.frameCount = 1;
    sample.frames[0].kind = RawFrameKind::BUILTIN;
    EXPECT_EQ(symbolizer.ResolveNativeCallLine(sample, 0), 0);
}

HWTEST_F_L0(CpuProfilerSymbolizerTest, ResolveNativeCallLineNonInterpreterCaller)
{
    // Caller exists but is not an interpreter frame: no line resolution.
    CpuProfilerSymbolizer symbolizer;
    symbolizer.BeginSession();
    RawSample sample;
    sample.frameCount = 2;
    sample.frames[0].kind = RawFrameKind::BUILTIN;
    sample.frames[1].kind = RawFrameKind::AOT;
    sample.frames[1].jsPandaFile = 0;
    EXPECT_EQ(symbolizer.ResolveNativeCallLine(sample, 0), 0);
}

// Fixture with a live VM so real JSPandaFiles and debug info can be loaded.
class CpuProfilerSymbolizerFileTest : public testing::Test {
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
        const JSPandaFile *pf = nullptr;
        std::string fileName = DEBUGGER_ABC_DIR "cpuprofiler_1.abc";
        std::string entryPoint = "cpuprofiler_1";
        executed = JSNApi::Execute(instance, fileName, entryPoint);
        if (!executed) {
            return;
        }
        JSPandaFileManager::GetInstance()->EnumerateJSPandaFiles(
            [&pf](const std::shared_ptr<JSPandaFile> &file) -> bool {
                if (file->GetJSPandaFileDesc().find("cpuprofiler_1.abc") != CString::npos) {
                    pf = file.get();
                }
                return true;
            });
        jsPandaFile = pf;
    }

    void TearDown() override
    {
        TestHelper::DestroyEcmaVMWithScope(instance, scope);
    }

    EcmaVM *instance {nullptr};
    EcmaHandleScope *scope {nullptr};
    JSThread *thread {nullptr};
    const JSPandaFile *jsPandaFile {nullptr};
    bool executed {false};
};

// A native frame called from an interpreter frame resolves its call line from
// the caller's debug info.
HWTEST_F_L0(CpuProfilerSymbolizerFileTest, ResolveNativeCallLineFromInterpreterCaller)
{
    ASSERT_TRUE(executed);
    ASSERT_NE(jsPandaFile, nullptr);
    ASSERT_GT(jsPandaFile->GetNumMethods(), 0U);
    MethodLiteral *literal = jsPandaFile->GetMethodLiterals();

    CpuProfilerSymbolizer symbolizer;
    symbolizer.BeginSession();
    RawSample sample;
    sample.frameCount = 2;
    sample.frames[0].kind = RawFrameKind::BUILTIN;
    sample.frames[0].value = 0x1234000;
    sample.frames[1].kind = RawFrameKind::INTERPRETER;
    sample.frames[1].value = reinterpret_cast<uintptr_t>(literal);
    sample.frames[1].jsPandaFile = reinterpret_cast<uintptr_t>(jsPandaFile);
    sample.frames[1].bytecodeOffset = 0;
    EXPECT_GE(symbolizer.ResolveNativeCallLine(sample, 0), 1);
}

// An interpreter caller without a backing file resolves nothing.
HWTEST_F_L0(CpuProfilerSymbolizerFileTest, ResolveNativeCallLineInterpreterCallerWithoutFile)
{
    CpuProfilerSymbolizer symbolizer;
    symbolizer.BeginSession();
    RawSample sample;
    sample.frameCount = 2;
    sample.frames[0].kind = RawFrameKind::BUILTIN;
    sample.frames[1].kind = RawFrameKind::INTERPRETER;
    sample.frames[1].jsPandaFile = 0;
    EXPECT_EQ(symbolizer.ResolveNativeCallLine(sample, 0), 0);
}

// A builtin frame with an interpreter caller inherits the caller's source
// file and keeps the native call line as its line number.
HWTEST_F_L0(CpuProfilerSymbolizerFileTest, SymbolizeBuiltinFrameWithInterpreterCaller)
{
    ASSERT_TRUE(executed);
    ASSERT_NE(jsPandaFile, nullptr);
    ASSERT_GT(jsPandaFile->GetNumMethods(), 0U);
    MethodLiteral *literal = jsPandaFile->GetMethodLiterals();

    CpuProfilerSymbolizer symbolizer;
    symbolizer.BeginSession();
    RawSample sample;
    sample.frameCount = 2;
    sample.frames[0].kind = RawFrameKind::BUILTIN;
    sample.frames[0].value = 0x1234000;
    sample.frames[1].kind = RawFrameKind::INTERPRETER;
    sample.frames[1].value = reinterpret_cast<uintptr_t>(literal);
    sample.frames[1].jsPandaFile = reinterpret_cast<uintptr_t>(jsPandaFile);
    sample.frames[1].bytecodeOffset = 0;

    MethodKey methodKey;
    FrameInfoTemp codeEntry;
    methodKey.methodIdentifier = reinterpret_cast<void *>(sample.frames[0].value);
    methodKey.lineNumber = 7; // 7: the call line resolved from the caller
    ASSERT_TRUE(symbolizer.SymbolizeFrame(sample, 0, methodKey, codeEntry));
    EXPECT_NE(strstr(codeEntry.functionName, "(native)"), nullptr);
    EXPECT_EQ(codeEntry.lineNumber, 7);
    EXPECT_STRNE(codeEntry.url, "");
}

// A builtin frame with a non-interpreter caller keeps no source file.
HWTEST_F_L0(CpuProfilerSymbolizerFileTest, SymbolizeBuiltinFrameWithNativeCaller)
{
    CpuProfilerSymbolizer symbolizer;
    symbolizer.BeginSession();
    RawSample sample;
    sample.frameCount = 2;
    sample.frames[0].kind = RawFrameKind::BUILTIN;
    sample.frames[0].value = 0x1234000;
    sample.frames[1].kind = RawFrameKind::AOT;

    MethodKey methodKey;
    FrameInfoTemp codeEntry;
    methodKey.methodIdentifier = reinterpret_cast<void *>(sample.frames[0].value);
    methodKey.lineNumber = 3; // 3: call line from a non-interpreter caller
    ASSERT_TRUE(symbolizer.SymbolizeFrame(sample, 0, methodKey, codeEntry));
    EXPECT_EQ(codeEntry.lineNumber, 3);
    EXPECT_STREQ(codeEntry.url, "");
}
} // namespace panda::test
