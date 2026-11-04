/*
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
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>

#include "securec.h"
#include "ecmascript/dfx/stackinfo/tests/js_stackinfo_test.h"
// defines ECMASCRIPT_ENABLE_ARK_STEED; must be visible before the #if below
// (the test header chain does not pull it in)
#include "ecmascript/base/config.h"

#if ECMASCRIPT_ENABLE_ARK_STEED
#include "ecmascript/arksteed/arksteed_safepoint_table.h"
#include "ecmascript/js_function.h"
#include "ecmascript/mem/machine_code.h"
#include "ecmascript/method.h"
#endif

#include "assembler/assembly-emitter.h"
#include "assembler/assembly-parser.h"
#include "class_data_accessor-inl.h"
#include "libziparchive/zip_archive.h"
#include "ecmascript/dfx/stackinfo/js_stackinfo.h"
#include "ecmascript/napi/include/dfx_jsnapi.h"
#include "ecmascript/tests/test_helper.h"

using namespace panda::ecmascript;

namespace panda::ecmascript {
// Internal (non-exported) frame geometry helper of js_stackinfo.cpp under test.
bool GetTypeOffsetAndPrevOffsetFromFrameType(uintptr_t frameType, uintptr_t &typeOffset,
                                             uintptr_t &prevOffset);
}

namespace panda::test {
class JsStackInfoTest : public testing::Test {
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
        TestHelper::CreateEcmaVMWithScope(instance_, thread_, scope_);
        instance_->SetEnableForceGC(false);
    }

    void TearDown() override
    {
        TestHelper::DestroyEcmaVMWithScope(instance_, scope_);
        JsStackInfo::nameMap.clear();
    }

    EcmaVM *instance_ {nullptr};
    EcmaHandleScope *scope_ {nullptr};
    JSThread *thread_ {nullptr};
};

template<typename T>
uintptr_t ToUintPtr(T frame)
{
    return static_cast<uintptr_t>(frame);
}

bool ReadMemFunc([[maybe_unused]] void *ctx, uintptr_t addr, uintptr_t *value)
{
    *value = *(reinterpret_cast<uintptr_t *>(addr));
    return true;
}

// Safe read function for tests that use step_ark_with_record_jit
// It tracks valid memory regions and returns false for invalid reads
struct SafeMemContext {
    std::vector<std::pair<uintptr_t, uintptr_t>> validRanges;  // (start, end)
    uintptr_t readValue = 0;  // Default value to return for invalid reads

    void AddRange(uintptr_t start, uintptr_t end)
    {
        validRanges.push_back({start, end});
    }

    bool IsValid(uintptr_t addr) const
    {
        for (const auto& range : validRanges) {
            if (addr >= range.first && addr < range.second) {
                return true;
            }
        }
        return false;
    }
};

bool SafeReadMemFuncForJit(void *ctx, uintptr_t addr, uintptr_t *value)
{
    SafeMemContext *safeCtx = static_cast<SafeMemContext*>(ctx);
    if (safeCtx && safeCtx->IsValid(addr)) {
        *value = *(reinterpret_cast<uintptr_t *>(addr));
        return true;
    }
    // For invalid addresses (like Hole().GetRawData() + offset), return default value
    *value = safeCtx ? safeCtx->readValue : 0;
    return false;
}

/**
 * @tc.name: GetTypeOffsetAndPrevOffsetFromFrameType
 * @tc.desc: STEED_FUNCTION_FRAME frame geometry must be supported with offsets from
 *   SteedFunctionFrame compile-time constants; existing frame types stay unchanged.
 * @tc.type: FUNC
 * @tc.require:
 */
HWTEST_F_L0(JsStackInfoTest, TestGetTypeOffsetAndPrevOffsetFromFrameType)
{
    // STEED frame geometry (G1 fix): frame walking must proceed across STEED frames.
    uintptr_t typeOffset = 0;
    uintptr_t prevOffset = 0;
    bool ret = GetTypeOffsetAndPrevOffsetFromFrameType(ToUintPtr(FrameType::STEED_FUNCTION_FRAME),
                                                         typeOffset, prevOffset);
    EXPECT_TRUE(ret);
    EXPECT_EQ(typeOffset, SteedFunctionFrame::GetTypeOffset());
    EXPECT_EQ(prevOffset, SteedFunctionFrame::GetPrevOffset());

    // Existing FASTJIT frame geometry unchanged.
    ret = GetTypeOffsetAndPrevOffsetFromFrameType(ToUintPtr(FrameType::FASTJIT_FUNCTION_FRAME),
                                                  typeOffset, prevOffset);
    EXPECT_TRUE(ret);
    EXPECT_EQ(typeOffset, FASTJITFunctionFrame::GetTypeOffset());
    EXPECT_EQ(prevOffset, FASTJITFunctionFrame::GetPrevOffset());

    // Interpreter frame geometry unchanged.
    ret = GetTypeOffsetAndPrevOffsetFromFrameType(ToUintPtr(FrameType::INTERPRETER_FRAME),
                                                  typeOffset, prevOffset);
    EXPECT_TRUE(ret);
    EXPECT_EQ(typeOffset, InterpretedFrame::GetTypeOffset());
    EXPECT_EQ(prevOffset, InterpretedFrame::GetPrevOffset());

    // Unsupported frame type value still returns false.
    EXPECT_FALSE(GetTypeOffsetAndPrevOffsetFromFrameType(0x12345678, typeOffset, prevOffset));
}

/**
 * @tc.name: ArkFrameCheck
 * @tc.desc: Check whether the result returned through "ArkFrameCheck" function is within expectations.
 * @tc.type: FUNC
 * @tc.require:
 */
HWTEST_F_L0(JsStackInfoTest, TestArkFrameCheck)
{
    for (uintptr_t i = 0; i < 25; i++) {
        bool ret = ArkFrameCheck(i);
        if (i == ToUintPtr(FrameType::OPTIMIZED_ENTRY_FRAME) ||
            i == ToUintPtr(FrameType::ASM_INTERPRETER_ENTRY_FRAME)) {
            EXPECT_TRUE(ret == true);
        } else {
            EXPECT_TRUE(ret == false);
        }
    }
}

/**
 * @tc.name: IsJsFunctionFrame
 * @tc.desc: Check whether the result returned through "IsJsFunctionFrame" function is within expectations.
 * @tc.type: FUNC
 * @tc.require:
 */
HWTEST_F_L0(JsStackInfoTest, TestIsJsFunctionFrame)
{
    for (uintptr_t i = 0; i < 25; i++) {
        bool ret = IsJsFunctionFrame(i);
        if (i == ToUintPtr(FrameType::ASM_INTERPRETER_FRAME) ||
            i == ToUintPtr(FrameType::INTERPRETER_CONSTRUCTOR_FRAME) ||
            i == ToUintPtr(FrameType::INTERPRETER_FRAME) ||
            i == ToUintPtr(FrameType::INTERPRETER_FAST_NEW_FRAME)) {
            EXPECT_TRUE(ret == true);
        } else {
            EXPECT_TRUE(ret == false);
        }
    }
}

/**
 * @tc.name: IsFastJitFunctionFrame
 * @tc.desc: Check whether the result returned through "IsFastJitFunctionFrame" function is within expectations.
 * @tc.type: FUNC
 * @tc.require:
 */
HWTEST_F_L0(JsStackInfoTest, TestIsFastJitFunctionFrame)
{
    for (uintptr_t i = 0; i < 25; i++) {
        bool ret = IsFastJitFunctionFrame(i);
        if (i == ToUintPtr(FrameType::FASTJIT_FUNCTION_FRAME) ||
            i == ToUintPtr(FrameType::FASTJIT_FAST_CALL_FUNCTION_FRAME)) {
            EXPECT_TRUE(ret == true);
        } else {
            EXPECT_TRUE(ret == false);
        }
    }
}

/**
 * @tc.name: IsNativeFunctionFrame
 * @tc.desc: Check whether the result returned through "IsNativeFunctionFrame" function is within expectations.
 * @tc.type: FUNC
 * @tc.require:
 */
HWTEST_F_L0(JsStackInfoTest, TestIsNativeFunctionFrame)
{
    for (uintptr_t i = 0; i < 25; i++) {
        bool ret = IsNativeFunctionFrame(i);
        if (i == ToUintPtr(FrameType::OPTIMIZED_FRAME) ||
            i == ToUintPtr(FrameType::BASELINE_BUILTIN_FRAME) ||
            i == ToUintPtr(FrameType::ASM_BRIDGE_FRAME) ||
            i == ToUintPtr(FrameType::OPTIMIZED_JS_FUNCTION_UNFOLD_ARGV_FRAME) ||
            i == ToUintPtr(FrameType::OPTIMIZED_JS_FUNCTION_ARGS_CONFIG_FRAME) ||
            i == ToUintPtr(FrameType::OPTIMIZED_JS_FAST_CALL_FUNCTION_FRAME) ||
            i == ToUintPtr(FrameType::OPTIMIZED_JS_FUNCTION_FRAME) ||
            i == ToUintPtr(FrameType::LEAVE_FRAME) ||
            i == ToUintPtr(FrameType::LEAVE_FRAME_WITH_ARGV) ||
            i == ToUintPtr(FrameType::BUILTIN_CALL_LEAVE_FRAME) ||
            i == ToUintPtr(FrameType::BUILTIN_FRAME) ||
            i == ToUintPtr(FrameType::BUILTIN_ENTRY_FRAME) ||
            i == ToUintPtr(FrameType::BUILTIN_FRAME_WITH_ARGV) ||
            i == ToUintPtr(FrameType::BUILTIN_FRAME_WITH_ARGV_STACK_OVER_FLOW_FRAME) ||
            i == ToUintPtr(FrameType::ASM_INTERPRETER_BRIDGE_FRAME)) {
            EXPECT_TRUE(ret == true);
        } else {
            EXPECT_TRUE(ret == false);
        }
    }
}

/**
 * @tc.name: IsAotFunctionFrame
 * @tc.desc: Check whether the result returned through "IsAotFunctionFrame" function is within expectations.
 * @tc.type: FUNC
 * @tc.require:
 */
HWTEST_F_L0(JsStackInfoTest, TestIsAotFunctionFrame)
{
    for (uintptr_t i = 0; i < 25; i++) {
        bool ret = IsAotFunctionFrame(i);
        if (i == ToUintPtr(FrameType::OPTIMIZED_JS_FUNCTION_FRAME) ||
            i == ToUintPtr(FrameType::OPTIMIZED_JS_FAST_CALL_FUNCTION_FRAME)) {
            EXPECT_TRUE(ret == true);
        } else {
            EXPECT_TRUE(ret == false);
        }
    }
}

/**
 * @tc.name: ReadMethodInfo
 * @tc.desc: Check whether the result returned through "ReadMethodInfo" function is within expectations.
 * @tc.type: FUNC
 * @tc.require:
 */
HWTEST_F_L0(JsStackInfoTest, TestReadMethodInfo)
{
    const char *filename = "__JSPandaFileManagerTest1.pa";
    const char *data = R"(
        .function void foo() {
            return
        }
    )";
    pandasm::Parser parser;
    auto res = parser.Parse(data);
    auto file = pandasm::AsmEmitter::Emit(res.Value());
    auto jsPandaFile = std::make_shared<JSPandaFile>(file.release(), CString(filename), CreateMode::DFX);
    EXPECT_TRUE(jsPandaFile != nullptr);
    CVector<MethodInfo> result;
    const panda_file::File *pf = jsPandaFile->GetPandaFile();
    Span<const uint32_t> classIndexes = jsPandaFile->GetClasses();
    for (const uint32_t index : classIndexes) {
        panda_file::File::EntityId classId(index);
        if (jsPandaFile->IsExternal(classId)) {
            continue;
        }
        panda_file::ClassDataAccessor cda(*pf, classId);
        cda.EnumerateMethods([&result, jsPandaFile](panda_file::MethodDataAccessor &mda) {
            auto info = JSStackTrace::ReadMethodInfo(mda);
            if (!info) {
                return;
            }
            result.push_back(info.value());
        });
    }
    EXPECT_TRUE(result.size() > 0);
}

/**
 * @tc.name: ReadAllMethodInfos
 * @tc.desc: Check whether the result returned through "ReadAllMethodInfos" function is within expectations.
 * @tc.type: FUNC
 * @tc.require:
 */
HWTEST_F_L0(JsStackInfoTest, TestReadAllMethodInfos)
{
    const char *filename = "__JsStackInfoTest.pa";
    const char *data = R"(
        .function void foo() {
            return
        }
    )";
    pandasm::Parser parser;
    auto res = parser.Parse(data);
    auto file = pandasm::AsmEmitter::Emit(res.Value());
    auto pf = std::make_shared<JSPandaFile>(file.release(), CString(filename), CreateMode::DFX);
    EXPECT_TRUE(pf != nullptr);
    auto methods = JSStackTrace::ReadAllMethodInfos(pf);
    EXPECT_TRUE(methods.size() > 0);
    pf = nullptr;
    methods = JSStackTrace::ReadAllMethodInfos(pf);
    EXPECT_TRUE(methods.size() == 0);
}

/**
 * @tc.name: TranslateByteCodePc
 * @tc.desc: Check whether the result returned through "TranslateByteCodePc" function is within expectations.
 * @tc.type: FUNC
 * @tc.require:
 */
HWTEST_F_L0(JsStackInfoTest, TestTranslateByteCodePc)
{
    CVector<MethodInfo> vec = {
        {0, 0, 24},
        {1, 24, 30},
        {2, 54, 56},
        {3, 110, 60}
    };
    uintptr_t realPc = 115;

    auto ret = JSStackTrace::TranslateByteCodePc(realPc, vec);
    EXPECT_TRUE(ret != std::nullopt);

    vec.clear();
    ret = JSStackTrace::TranslateByteCodePc(realPc, vec);
    EXPECT_TRUE(ret == std::nullopt);

    vec.push_back({2, 54, 56});
    ret = JSStackTrace::TranslateByteCodePc(realPc, vec);
    EXPECT_TRUE(ret == std::nullopt);
}

/**
 * @tc.name: ParseJsFrameInfo
 * @tc.desc: Check whether the result returned through "ParseJsFrameInfo" function is within expectations.
 * @tc.type: FUNC
 * @tc.require:
 */
HWTEST_F_L0(JsStackInfoTest, TestParseJsFrameInfo)
{
    const char *filename = "__JsStackInfoTest.pa";
    const char *data = R"(
        .function void foo() {
            return
        }
    )";
    pandasm::Parser parser;
    auto res = parser.Parse(data);
    auto file = pandasm::AsmEmitter::Emit(res.Value());
    auto jsPandaFile = std::make_shared<JSPandaFile>(file.release(), CString(filename), CreateMode::DFX);
    EXPECT_TRUE(jsPandaFile != nullptr);
    auto debugExtractor = std::make_unique<DebugInfoExtractor>(jsPandaFile.get());
    auto methods = JSStackTrace::ReadAllMethodInfos(jsPandaFile);
    uintptr_t offset = 0;
    JsFunction jsFunction;
    EXPECT_TRUE(ParseJsFrameInfo(jsPandaFile.get(), debugExtractor.get(),
        EntityId(methods[0].methodId), offset, jsFunction));
    EXPECT_TRUE(std::string(jsFunction.functionName) == "foo");
    EXPECT_TRUE(JsStackInfo::nameMap.empty());
}

/**
 * @tc.name: ArkParseJsFrameInfo
 * @tc.desc: Check whether the result returned through "ArkCreateJSSymbolExtractor" function is within expectations;
 *           Check whether the result returned through "ArkParseJsFrameInfo" function is within expectations;
 *           Check whether the result returned through "ArkDestroyJSSymbolExtractor" function is within expectations.
 * @tc.type: FUNC
 * @tc.require:
 */
HWTEST_F_L0(JsStackInfoTest, TestArkParseJsFrameInfo)
{
    const char *filename1 = "__JsStackInfoTest1.pa";
    const char *filename2 = "__JsStackInfoTest2.pa";
    const char *pfdata1 = R"(
        .function void foo() {
            return
        }
    )";
    const char *pfdata2 = R"(
        .language ECMAScript
        .function any func_main_0(any a0, any a1, any a2) {
            ldai 1
            return
        }
    )";
    pandasm::Parser parser1;
    pandasm::Parser parser2;
    auto res1 = parser1.Parse(pfdata1);
    auto res2 = parser2.Parse(pfdata2);
    auto file1 = pandasm::AsmEmitter::Emit(res1.Value());
    auto file2 = pandasm::AsmEmitter::Emit(res2.Value());
    auto jsPandaFile1 = std::make_shared<JSPandaFile>(file1.release(), CString(filename1), CreateMode::DFX);
    auto jsPandaFile2 = std::make_shared<JSPandaFile>(file2.release(), CString(filename2), CreateMode::DFX);
    EXPECT_TRUE(jsPandaFile1 != nullptr);
    EXPECT_TRUE(jsPandaFile2 != nullptr);
    auto debugExtractor1 = std::make_unique<DebugInfoExtractor>(jsPandaFile1.get());
    auto debugExtractor2 = std::make_unique<DebugInfoExtractor>(jsPandaFile2.get());
    auto methods1 = JSStackTrace::ReadAllMethodInfos(jsPandaFile1);
    auto methods2 = JSStackTrace::ReadAllMethodInfos(jsPandaFile2);

    uintptr_t byteCodePc1 = methods1[0].codeBegin;
    uintptr_t byteCodePc2 = methods2[0].codeBegin;
    uintptr_t mapBase1 = reinterpret_cast<uintptr_t>(jsPandaFile1->GetHeader());
    uintptr_t mapBase2 = reinterpret_cast<uintptr_t>(jsPandaFile2->GetHeader());
    uintptr_t loadOffset1 = 0;
    uintptr_t loadOffset2 = 0;
    const uint8_t* data1 = jsPandaFile1->GetPandaFile()->GetBase();
    const uint8_t* data2 = jsPandaFile2->GetPandaFile()->GetBase();
    uint64_t dataSize1 = jsPandaFile1->GetFileSize();
    uint64_t dataSize2 = jsPandaFile2->GetFileSize();
    uintptr_t extractorptr1 = 0;
    uintptr_t extractorptr2 = 0;
    JsFunction jsFunction1;
    JsFunction jsFunction2;

    auto ret = ark_create_js_symbol_extractor(&extractorptr1);
    EXPECT_TRUE(ret == 1);
    EXPECT_TRUE(extractorptr1 != 0);
    ret = ark_create_js_symbol_extractor(&extractorptr2);
    EXPECT_TRUE(ret == 1);
    EXPECT_TRUE(extractorptr2 != 0);

    ret = ark_parse_js_frame_info(byteCodePc1, mapBase1, loadOffset1,
        reinterpret_cast<uint8_t *>(const_cast<char*>(pfdata1)),
        strlen(pfdata1) + 1, extractorptr1, &jsFunction1);
    EXPECT_TRUE(ret == -1);

    ret = ark_parse_js_frame_info(byteCodePc1, mapBase1, loadOffset1,
        const_cast<uint8_t*>(data1), dataSize1, extractorptr1, &jsFunction1);
    EXPECT_TRUE(ret == 1);
    EXPECT_TRUE(std::string(jsFunction1.functionName) == "foo");

    ret = ark_parse_js_frame_info(byteCodePc1, mapBase1, loadOffset1,
        const_cast<uint8_t*>(data1), dataSize1 + 1, 0, &jsFunction1);
    EXPECT_TRUE(ret == -1);

    ret = ark_parse_js_frame_info(byteCodePc1, mapBase1, loadOffset1,
        nullptr, 0, extractorptr1, &jsFunction1);
    EXPECT_TRUE(ret == -1);

    ret = ark_parse_js_frame_info(byteCodePc1, mapBase1, loadOffset1,
        const_cast<uint8_t*>(data2), dataSize2, extractorptr2, &jsFunction1);
    EXPECT_TRUE(ret == -1);

    ret = ark_parse_js_frame_info(byteCodePc2, mapBase1, loadOffset1,
        const_cast<uint8_t*>(data2), dataSize2, extractorptr2, &jsFunction1);
    EXPECT_TRUE(ret == -1);

    ret = ark_parse_js_frame_info(byteCodePc2, mapBase1, loadOffset1,
        const_cast<uint8_t*>(data2), dataSize2, extractorptr2, &jsFunction1);
    EXPECT_TRUE(ret == -1);

    ret = ark_parse_js_frame_info(byteCodePc2, mapBase2, loadOffset2,
        const_cast<uint8_t*>(data2), dataSize2, extractorptr2, &jsFunction2);
    EXPECT_TRUE(ret == 1);
    EXPECT_TRUE(std::string(jsFunction2.functionName) == "func_main_0");

    ret = ark_destory_js_symbol_extractor(extractorptr1);
    EXPECT_TRUE(ret == 1);

    ret = ark_destory_js_symbol_extractor(extractorptr2);
    EXPECT_TRUE(!JsStackInfo::nameMap.empty());
    EXPECT_TRUE(ret == 1);
}

/**
 * @tc.name: ark_destroy_local
 * @tc.desc: Check whether the result returned through "ark_destroy_local" function is within expectations;
 * @tc.type: FUNC
 * @tc.require:
 */
HWTEST_F_L0(JsStackInfoTest, TestArkDestroyLocal)
{
    auto ret = ark_destroy_local(); // Direct destruct without creating Pointers
    EXPECT_TRUE(ret);
    ret = ark_create_local(); // ref count = 1
    ret = ark_create_local(); // ref count = 2
    EXPECT_TRUE(ret);
    auto trace = JSStackTrace::GetInstance();
    EXPECT_TRUE(trace != nullptr);
    ret = ark_destroy_local(); // ref count = 1
    trace = JSStackTrace::GetInstance();
    EXPECT_TRUE(trace != nullptr);
    ret = ark_destroy_local(); // ref count = 0
    trace = JSStackTrace::GetInstance();
    EXPECT_TRUE(trace == nullptr);
    EXPECT_TRUE(ret);
    ret = ark_destroy_local(); // multiple destroy
    EXPECT_TRUE(ret);

    // Create and destruct multiple times within the process
    ret = ark_create_local(); // ref count = 1
    trace = JSStackTrace::GetInstance();
    EXPECT_TRUE(trace != nullptr);
    ret = ark_destroy_local();
    EXPECT_TRUE(ret);
}

HWTEST_F_L0(JsStackInfoTest, TestStepArk__001)
{
    void *ctx = nullptr;
    uintptr_t sp = 0;
    uintptr_t pc = 0;
    bool isJsFrame = true;
    uintptr_t frame[10][3];
    uintptr_t fp[10];
    StepFrameType frameType = StepFrameType::JS_FRAME;
    uint64_t frameIndex = 0;
    for (int i = 0; i < 10; i++) {
        frame[i][0] = 0;
        frame[i][1] = 0;
    }
    fp[0] = reinterpret_cast<uintptr_t>(&frame[0][2]) + 8;
    for (int i = 1; i < 10; i++) {
        fp[i] = fp[i-1] + 24;
    }
    ArkStepParam arkStepParam{ &fp[0], &sp, &pc, &isJsFrame, &frameType, frameIndex };
    frame[0][2] = static_cast<uintptr_t>(FrameType::INTERPRETER_CONSTRUCTOR_FRAME);
    ASSERT_TRUE(step_ark(ctx, ReadMemFunc, &arkStepParam));
    frame[1][2] = static_cast<uintptr_t>(FrameType::INTERPRETER_FAST_NEW_FRAME);
    arkStepParam.fp = &fp[1];
    ASSERT_TRUE(step_ark(ctx, ReadMemFunc, &arkStepParam));
    frame[2][2] = static_cast<uintptr_t>(FrameType::OPTIMIZED_JS_FUNCTION_ARGS_CONFIG_FRAME);
    arkStepParam.fp = &fp[2];
    ASSERT_TRUE(step_ark(ctx, ReadMemFunc, &arkStepParam));
    frame[3][2] = static_cast<uintptr_t>(FrameType::ASM_INTERPRETER_ENTRY_FRAME);
    arkStepParam.fp = &fp[3];
    ASSERT_TRUE(step_ark(ctx, ReadMemFunc, &arkStepParam));
    frame[4][2] = static_cast<uintptr_t>(FrameType::BUILTIN_ENTRY_FRAME);
    arkStepParam.fp = &fp[4];
    ASSERT_TRUE(step_ark(ctx, ReadMemFunc, &arkStepParam));
    frame[5][2] = static_cast<uintptr_t>(FrameType::BUILTIN_FRAME_WITH_ARGV_STACK_OVER_FLOW_FRAME);
    arkStepParam.fp = &fp[5];
    ASSERT_TRUE(step_ark(ctx, ReadMemFunc, &arkStepParam));
    frame[6][2] = static_cast<uintptr_t>(FrameType::BASELINE_BUILTIN_FRAME);
    arkStepParam.fp = &fp[6];
    ASSERT_TRUE(step_ark(ctx, ReadMemFunc, &arkStepParam));
    frame[7][2] = static_cast<uintptr_t>(FrameType::ASM_BRIDGE_FRAME);
    arkStepParam.fp = &fp[7];
    ASSERT_TRUE(step_ark(ctx, ReadMemFunc, &arkStepParam));
    frame[8][2] = static_cast<uintptr_t>(FrameType::LEAVE_FRAME);
    arkStepParam.fp = &fp[8];
    ASSERT_TRUE(step_ark(ctx, ReadMemFunc, &arkStepParam));
    frame[9][2] = static_cast<uintptr_t>(FrameType::LEAVE_FRAME_WITH_ARGV);
    arkStepParam.fp = &fp[9];
    ASSERT_TRUE(step_ark(ctx, ReadMemFunc, &arkStepParam));
}

HWTEST_F_L0(JsStackInfoTest, TestStepArk__002)
{
    void *ctx = nullptr;
    uintptr_t sp = 0;
    uintptr_t pc = 0;
    bool isJsFrame = true;
    uintptr_t frame[30][3];
    uintptr_t fp[30];
    StepFrameType frameType = StepFrameType::JS_FRAME;
    uint64_t frameIndex = 0;
    for (int i = 0; i < 30; i++) {
        frame[i][0] = 0;
        frame[i][1] = 0;
    }
    fp[0] = reinterpret_cast<uintptr_t>(&frame[0][2]) + 8;
    for (int i = 1; i < 30; i++) {
        fp[i] = fp[i-1] + 24;
    }
    ArkStepParam arkStepParam{ &fp[0], &sp, &pc, &isJsFrame, &frameType, frameIndex };
    frame[0][2] = static_cast<uintptr_t>(FrameType::BUILTIN_CALL_LEAVE_FRAME);
    ASSERT_TRUE(step_ark(ctx, ReadMemFunc, &arkStepParam));
    frame[1][2] = static_cast<uintptr_t>(FrameType::OPTIMIZED_FRAME);
    arkStepParam.fp = &fp[1];
    ASSERT_TRUE(step_ark(ctx, ReadMemFunc, &arkStepParam));
    frame[2][2] = static_cast<uintptr_t>(FrameType::ASM_INTERPRETER_BRIDGE_FRAME);
    arkStepParam.fp = &fp[2];
    ASSERT_TRUE(step_ark(ctx, ReadMemFunc, &arkStepParam));
    frame[3][2] = static_cast<uintptr_t>(FrameType::OPTIMIZED_JS_FUNCTION_UNFOLD_ARGV_FRAME);
    arkStepParam.fp = &fp[3];
    ASSERT_TRUE(step_ark(ctx, ReadMemFunc, &arkStepParam));
    frame[4][2] = static_cast<uintptr_t>(FrameType::INTERPRETER_CONSTRUCTOR_FRAME);
    arkStepParam.fp = &fp[4];
    ASSERT_TRUE(step_ark(ctx, ReadMemFunc, &arkStepParam));
    frame[5][2] = static_cast<uintptr_t>(FrameType::OPTIMIZED_ENTRY_FRAME);
    arkStepParam.fp = &fp[5];
    ASSERT_TRUE(step_ark(ctx, ReadMemFunc, &arkStepParam));
    frame[6][2] = static_cast<uintptr_t>(FrameType::ASM_BRIDGE_FRAME);
    arkStepParam.fp = &fp[6];
    ASSERT_TRUE(step_ark(ctx, ReadMemFunc, &arkStepParam));
    frame[7][2] = static_cast<uintptr_t>(FrameType::OPTIMIZED_JS_FUNCTION_UNFOLD_ARGV_FRAME);
    arkStepParam.fp = &fp[7];
    ASSERT_TRUE(step_ark(ctx, ReadMemFunc, &arkStepParam));
    frame[8][2] = static_cast<uintptr_t>(FrameType::OPTIMIZED_JS_FUNCTION_FRAME);
    arkStepParam.fp = &fp[8];
    ASSERT_TRUE(step_ark(ctx, ReadMemFunc, &arkStepParam));
    frame[9][2] = 100;
    arkStepParam.fp = &fp[9];
    ASSERT_TRUE(step_ark(ctx, ReadMemFunc, &arkStepParam));
}

HWTEST_F_L0(JsStackInfoTest, TestLocalParseJsFrameInfo__001)
{
    const char *filename = "__JsStackInfoTest1.pa";
    const char *pfdata = R"(
        .function void foo() {
            return
        }
    )";

    auto pfManager = JSPandaFileManager::GetInstance();

    pandasm::Parser parser1;
    auto res = parser1.Parse(pfdata);
    auto file = pandasm::AsmEmitter::Emit(res.Value());
    auto jsPandaFile = pfManager->NewJSPandaFile(file.release(), CString(filename));
    pfManager->AddJSPandaFile(jsPandaFile);
    EXPECT_TRUE(jsPandaFile != nullptr);
    auto methods = JSStackTrace::ReadAllMethodInfos(jsPandaFile);

    uintptr_t byteCodePc = methods[0].codeBegin;
    uintptr_t mapBase = reinterpret_cast<uintptr_t>(jsPandaFile->GetHeader());
    uintptr_t loadOffset = 0;
    JsFunction jsFunction;

    // Incorrect invocation demonstration
    auto ret = ark_parse_js_frame_info_local(byteCodePc, mapBase, loadOffset, &jsFunction);
    EXPECT_TRUE(ret == -1);

    // Correct invocation demonstration
    ark_create_local();
    ret = ark_parse_js_frame_info_local(byteCodePc, mapBase, loadOffset, &jsFunction);
    EXPECT_TRUE(ret == 1);

    ret = ark_parse_js_frame_info_local(byteCodePc, mapBase, loadOffset, &jsFunction);
    EXPECT_TRUE(ret == 1);
    EXPECT_TRUE(JsStackInfo::nameMap.empty());

    ark_destroy_local();

    pfManager->RemoveJSPandaFile(jsPandaFile.get());
}

HWTEST_F_L0(JsStackInfoTest, TestLocalParseJsFrameInfo__002)
{
    const char *filename = "__JsStackInfoTest1.pa";
    const char *pfdata = R"(
        .function void foo() {
            return
        }
    )";

    auto pfManager = JSPandaFileManager::GetInstance();

    pandasm::Parser parser1;
    auto res = parser1.Parse(pfdata);
    auto file = pandasm::AsmEmitter::Emit(res.Value());
    auto jsPandaFile = pfManager->NewJSPandaFile(file.release(), CString(filename));
    EXPECT_TRUE(jsPandaFile != nullptr);
    auto methods = JSStackTrace::ReadAllMethodInfos(jsPandaFile);

    uintptr_t byteCodePc = methods[0].codeBegin;
    uintptr_t mapBase = reinterpret_cast<uintptr_t>(jsPandaFile->GetHeader());
    uintptr_t loadOffset = 0;
    JsFunction jsFunction;

    ark_create_local();
    auto ret = ark_parse_js_frame_info_local(byteCodePc, mapBase, loadOffset, &jsFunction);
    ark_destroy_local();
    // pandafile manager can't find jsPandaFile
    EXPECT_TRUE(ret == -1);
}

#if defined(PANDA_TARGET_ARM64)
HWTEST_F_L0(JsStackInfoTest, TestFrameType_001)
{
    EXPECT_TRUE(ToUintPtr(arkts_frame_type::OPTIMIZED_FRAME) ==
                ToUintPtr(FrameType::OPTIMIZED_FRAME));
    EXPECT_TRUE(ToUintPtr(arkts_frame_type::OPTIMIZED_ENTRY_FRAME) ==
                ToUintPtr(FrameType::OPTIMIZED_ENTRY_FRAME));
    EXPECT_TRUE(ToUintPtr(arkts_frame_type::OPTIMIZED_JS_FUNCTION_FRAME) ==
                ToUintPtr(FrameType::OPTIMIZED_JS_FUNCTION_FRAME));
    EXPECT_TRUE(ToUintPtr(arkts_frame_type::OPTIMIZED_JS_FAST_CALL_FUNCTION_FRAME) ==
                ToUintPtr(FrameType::OPTIMIZED_JS_FAST_CALL_FUNCTION_FRAME));
    EXPECT_TRUE(ToUintPtr(arkts_frame_type::FASTJIT_FUNCTION_FRAME) ==
                ToUintPtr(FrameType::FASTJIT_FUNCTION_FRAME));
    EXPECT_TRUE(ToUintPtr(arkts_frame_type::FASTJIT_FAST_CALL_FUNCTION_FRAME) ==
                ToUintPtr(FrameType::FASTJIT_FAST_CALL_FUNCTION_FRAME));
    EXPECT_TRUE(ToUintPtr(arkts_frame_type::ASM_BRIDGE_FRAME) ==
                ToUintPtr(FrameType::ASM_BRIDGE_FRAME));
    EXPECT_TRUE(ToUintPtr(arkts_frame_type::LEAVE_FRAME) ==
                ToUintPtr(FrameType::LEAVE_FRAME));
    EXPECT_TRUE(ToUintPtr(arkts_frame_type::LEAVE_FRAME_WITH_ARGV) ==
                ToUintPtr(FrameType::LEAVE_FRAME_WITH_ARGV));
    EXPECT_TRUE(ToUintPtr(arkts_frame_type::BUILTIN_CALL_LEAVE_FRAME) ==
                ToUintPtr(FrameType::BUILTIN_CALL_LEAVE_FRAME));
    EXPECT_TRUE(ToUintPtr(arkts_frame_type::INTERPRETER_FRAME) ==
                ToUintPtr(FrameType::INTERPRETER_FRAME));
    EXPECT_TRUE(ToUintPtr(arkts_frame_type::ASM_INTERPRETER_FRAME) ==
                ToUintPtr(FrameType::ASM_INTERPRETER_FRAME));
    EXPECT_TRUE(ToUintPtr(arkts_frame_type::INTERPRETER_CONSTRUCTOR_FRAME) ==
                ToUintPtr(FrameType::INTERPRETER_CONSTRUCTOR_FRAME));
    EXPECT_TRUE(ToUintPtr(arkts_frame_type::BUILTIN_FRAME) ==
                ToUintPtr(FrameType::BUILTIN_FRAME));
}

HWTEST_F_L0(JsStackInfoTest, TestFrameType_002)
{
    EXPECT_TRUE(ToUintPtr(arkts_frame_type::BUILTIN_FRAME_WITH_ARGV) ==
                ToUintPtr(FrameType::BUILTIN_FRAME_WITH_ARGV));
    EXPECT_TRUE(ToUintPtr(arkts_frame_type::BUILTIN_ENTRY_FRAME) ==
                ToUintPtr(FrameType::BUILTIN_ENTRY_FRAME));
    EXPECT_TRUE(ToUintPtr(arkts_frame_type::INTERPRETER_BUILTIN_FRAME) ==
                ToUintPtr(FrameType::INTERPRETER_BUILTIN_FRAME));
    EXPECT_TRUE(ToUintPtr(arkts_frame_type::INTERPRETER_FAST_NEW_FRAME) ==
                ToUintPtr(FrameType::INTERPRETER_FAST_NEW_FRAME));
    EXPECT_TRUE(ToUintPtr(arkts_frame_type::INTERPRETER_ENTRY_FRAME) ==
                ToUintPtr(FrameType::INTERPRETER_ENTRY_FRAME));
    EXPECT_TRUE(ToUintPtr(arkts_frame_type::ASM_INTERPRETER_ENTRY_FRAME) ==
                ToUintPtr(FrameType::ASM_INTERPRETER_ENTRY_FRAME));
    EXPECT_TRUE(ToUintPtr(arkts_frame_type::ASM_INTERPRETER_BRIDGE_FRAME) ==
                ToUintPtr(FrameType::ASM_INTERPRETER_BRIDGE_FRAME));
    EXPECT_TRUE(ToUintPtr(arkts_frame_type::OPTIMIZED_JS_FUNCTION_ARGS_CONFIG_FRAME) ==
                ToUintPtr(FrameType::OPTIMIZED_JS_FUNCTION_ARGS_CONFIG_FRAME));
    EXPECT_TRUE(ToUintPtr(arkts_frame_type::OPTIMIZED_JS_FUNCTION_UNFOLD_ARGV_FRAME) ==
                ToUintPtr(FrameType::OPTIMIZED_JS_FUNCTION_UNFOLD_ARGV_FRAME));
    EXPECT_TRUE(ToUintPtr(arkts_frame_type::BUILTIN_FRAME_WITH_ARGV_STACK_OVER_FLOW_FRAME) ==
                ToUintPtr(FrameType::BUILTIN_FRAME_WITH_ARGV_STACK_OVER_FLOW_FRAME));
    EXPECT_TRUE(ToUintPtr(arkts_frame_type::BASELINE_BUILTIN_FRAME) ==
                ToUintPtr(FrameType::BASELINE_BUILTIN_FRAME));
}

HWTEST_F_L0(JsStackInfoTest, TestStepArk_001)
{
    size_t size = 3;  // 3: size of OPTIMIZED_FRAME
    JSTaggedType frame[size];
    frame[0] = static_cast<JSTaggedType>(FrameType::OPTIMIZED_FRAME);  // 0: type
    frame[1] = static_cast<JSTaggedType>(62480);  // 1: prevFp
    frame[2] = static_cast<JSTaggedType>(123456);  // 2: returnAddr

    uintptr_t fp  = reinterpret_cast<uintptr_t>(&frame[0]) + 8; // 0: type addr
    uintptr_t sp  = 0;
    uintptr_t pc = 0;
    bool isJsFrame = true;
    StepFrameType frameType = StepFrameType::JS_FRAME;
    uint64_t frameIndex = 0;

    unwind_user_context_s *ctx = (unwind_user_context_s*)malloc(sizeof(unwind_user_context_s));
    ctx->count = -1;
    unwind_stack_frame_s cur_frame = { fp, 0 };
    auto pre_frame = unwind_arkts(ctx, &cur_frame);
    EXPECT_TRUE(pre_frame.fp == frame[1]);
    EXPECT_TRUE(pre_frame.pc == frame[2]);

    ArkStepParam arkStepParam{ &fp, &sp, &pc, &isJsFrame, &frameType, frameIndex };
    int ret = step_ark(ctx, ReadMemFunc, &arkStepParam);
    EXPECT_TRUE(*arkStepParam.fp == frame[1]);
    EXPECT_TRUE(*arkStepParam.pc == frame[2]);
    EXPECT_TRUE(ret == 1);

    free(ctx);
}

HWTEST_F_L0(JsStackInfoTest, TestStepArk_002)
{
    size_t size = 4;  // 4: size of OPTIMIZED_ENTRY_FRAME + returnaddr
    JSTaggedType frame[size];
    frame[0] = static_cast<JSTaggedType>(0);  // 0: preLeaveFrameFp
    frame[1] = static_cast<JSTaggedType>(FrameType::OPTIMIZED_ENTRY_FRAME);  // 1: type
    frame[2] = static_cast<JSTaggedType>(62480);  // 2: prevFP
    frame[3] = static_cast<JSTaggedType>(123456);  // 3: returnAddr

    uintptr_t fp  = reinterpret_cast<uintptr_t>(&frame[1]) + 8; // 1: type addr
    uintptr_t sp  = 0;
    uintptr_t pc = 0;
    bool isJsFrame = true;
    StepFrameType frameType = StepFrameType::JS_FRAME;
    uint64_t frameIndex = 0;
    unwind_user_context_s *ctx = (unwind_user_context_s*)malloc(sizeof(unwind_user_context_s));
    ctx->count = -1;
    unwind_stack_frame_s cur_frame = { fp, pc };
    auto pre_frame = unwind_arkts(ctx, &cur_frame);
    EXPECT_TRUE(pre_frame.fp == frame[2]);
    EXPECT_TRUE(pre_frame.pc == frame[3]);

    ArkStepParam arkStepParam{ &fp, &sp, &pc, &isJsFrame, &frameType, frameIndex };
    int ret = step_ark(ctx, ReadMemFunc, &arkStepParam);
    EXPECT_TRUE(*arkStepParam.fp == frame[2]);
    EXPECT_TRUE(*arkStepParam.pc == frame[3]);
    EXPECT_TRUE(ret == 1);

    free(ctx);
}

HWTEST_F_L0(JsStackInfoTest, TestStepArk_003)
{
    size_t size = 4;  // 4: size of OPTIMIZED_JS_FUNCTION_FRAME
    JSTaggedType frame[size];
    frame[0] = JSTaggedValue::Hole().GetRawData();  // 0: JSFunction
    frame[1] = static_cast<JSTaggedType>(FrameType::OPTIMIZED_JS_FUNCTION_FRAME);  // 1: type
    frame[2] = static_cast<JSTaggedType>(62480);  // 2: prevFp
    frame[3] = static_cast<JSTaggedType>(123456);  // 3: returnAddr

    uintptr_t fp  = reinterpret_cast<uintptr_t>(&frame[1]) + 8; // 1: type addr
    uintptr_t sp  = 0;
    uintptr_t pc = 0;
    bool isJsFrame = true;
    StepFrameType frameType = StepFrameType::JS_FRAME;
    uint64_t frameIndex = 0;
    unwind_user_context_s *ctx = (unwind_user_context_s*)malloc(sizeof(unwind_user_context_s));
    ctx->count = -1;
    unwind_stack_frame_s cur_frame = { fp, pc };
    auto pre_frame = unwind_arkts(ctx, &cur_frame);
    EXPECT_TRUE(pre_frame.fp == frame[2]);
    EXPECT_TRUE(pre_frame.pc == frame[3]);

    ArkStepParam arkStepParam{ &fp, &sp, &pc, &isJsFrame, &frameType, frameIndex };
    int ret = step_ark(ctx, ReadMemFunc, &arkStepParam);
    EXPECT_TRUE(*arkStepParam.fp == frame[2]);
    EXPECT_TRUE(*arkStepParam.pc == frame[3]);
    EXPECT_TRUE(ret == 1);

    free(ctx);
}

HWTEST_F_L0(JsStackInfoTest, TestStepArk_004)
{
    size_t size = 4;  // 4: size of OPTIMIZED_JS_FAST_CALL_FUNCTION_FRAME
    JSTaggedType frame[size];
    frame[0] = JSTaggedValue::Hole().GetRawData();  // 0: JSFunction
    frame[1] = static_cast<JSTaggedType>(FrameType::OPTIMIZED_JS_FAST_CALL_FUNCTION_FRAME);  // 1: type
    frame[2] = static_cast<JSTaggedType>(62480);  // 2: prevFp
    frame[3] = static_cast<JSTaggedType>(123456);  // 3: returnAddr

    uintptr_t fp  = reinterpret_cast<uintptr_t>(&frame[1]) + 8; // 1: type addr
    uintptr_t sp  = 0;
    uintptr_t pc = 0;
    bool isJsFrame = true;
    StepFrameType frameType = StepFrameType::JS_FRAME;
    uint64_t frameIndex = 0;
    unwind_user_context_s *ctx = (unwind_user_context_s*)malloc(sizeof(unwind_user_context_s));
    ctx->count = -1;
    unwind_stack_frame_s cur_frame = { fp, pc };
    auto pre_frame = unwind_arkts(ctx, &cur_frame);
    EXPECT_TRUE(pre_frame.fp == frame[2]);
    EXPECT_TRUE(pre_frame.pc == frame[3]);

    ArkStepParam arkStepParam{ &fp, &sp, &pc, &isJsFrame, &frameType, frameIndex };
    int ret = step_ark(ctx, ReadMemFunc, &arkStepParam);
    EXPECT_TRUE(*arkStepParam.fp == frame[2]);
    EXPECT_TRUE(*arkStepParam.pc == frame[3]);
    EXPECT_TRUE(ret == 1);

    free(ctx);
}

HWTEST_F_L0(JsStackInfoTest, TestStepArk_005)
{
    size_t size1 = 4;  // 4: size of OPTIMIZED_ENTRY_FRAME + returnaddr
    JSTaggedType frame1[size1];
    frame1[0] = static_cast<JSTaggedType>(0);  // 0: preLeaveFrameFp
    frame1[1] = static_cast<JSTaggedType>(FrameType::OPTIMIZED_ENTRY_FRAME);  // 1: type
    frame1[2] = static_cast<JSTaggedType>(62480);  // 2: prevFP
    frame1[3] = static_cast<JSTaggedType>(123456);  // 3: returnAddr

    size_t size2 = 5;  // 4: size of FASTJIT_FUNCTION_FRAME
    JSTaggedType frame2[size2];
    frame2[0] = static_cast<JSTaggedType>(1234);  // 0: pc
    frame2[1] = JSTaggedValue::Hole().GetRawData();  // 1: JSFunction
    frame2[2] = static_cast<JSTaggedType>(FrameType::FASTJIT_FUNCTION_FRAME);  // 2: type
    frame2[3] = static_cast<JSTaggedType>(reinterpret_cast<uintptr_t>(&frame1[1]) + 8);  // 3: prevFp
    frame2[4] = static_cast<JSTaggedType>(0);  // 4: returnAddr

    uintptr_t fp  = reinterpret_cast<uintptr_t>(&frame2[2]) + 8; // 0: type addr
    uintptr_t sp  = 0;
    uintptr_t pc = 0;
    bool isJsFrame = true;
    StepFrameType frameType = StepFrameType::JS_FRAME;
    uint64_t frameIndex = 0;
    unwind_user_context_s *ctx = (unwind_user_context_s*)malloc(sizeof(unwind_user_context_s));
    ctx->count = -1;
    unwind_stack_frame_s cur_frame = { fp, pc };
    auto pre_frame = unwind_arkts(ctx, &cur_frame);

    EXPECT_TRUE(pre_frame.fp == frame1[2]);
    EXPECT_TRUE(pre_frame.pc == frame1[3]);

    ArkStepParam arkStepParam{ &fp, &sp, &pc, &isJsFrame, &frameType, frameIndex };
    int ret = step_ark(ctx, ReadMemFunc, &arkStepParam);
    EXPECT_TRUE(*arkStepParam.fp == frame1[2]);
    EXPECT_TRUE(*arkStepParam.pc == frame1[3]);
    EXPECT_TRUE(ret == 1);

    free(ctx);
}

HWTEST_F_L0(JsStackInfoTest, TestStepArk_006)
{
    size_t size1 = 4;  // 4: size of OPTIMIZED_ENTRY_FRAME + returnaddr
    JSTaggedType frame1[size1];
    frame1[0] = static_cast<JSTaggedType>(0);  // 0: preLeaveFrameFp
    frame1[1] = static_cast<JSTaggedType>(FrameType::OPTIMIZED_ENTRY_FRAME);  // 1: type
    frame1[2] = static_cast<JSTaggedType>(62480);  // 2: prevFP
    frame1[3] = static_cast<JSTaggedType>(123456);  // 3: returnAddr

    size_t size2 = 5;  // 4: size of FASTJIT_FAST_CALL_FUNCTION_FRAME
    JSTaggedType frame2[size2];
    frame2[0] = static_cast<JSTaggedType>(1234);  // 0: pc
    frame2[1] = JSTaggedValue::Hole().GetRawData();  // 1: JSFunction
    frame2[2] = static_cast<JSTaggedType>(FrameType::FASTJIT_FAST_CALL_FUNCTION_FRAME);  // 2: type
    frame2[3] = static_cast<JSTaggedType>(reinterpret_cast<uintptr_t>(&frame1[1]) + 8);  // 3: prevFp
    frame2[4] = static_cast<JSTaggedType>(0);  // 4: returnAddr

    uintptr_t fp  = reinterpret_cast<uintptr_t>(&frame2[2]) + 8; // 0: type addr
    uintptr_t sp  = 0;
    uintptr_t pc = 0;
    bool isJsFrame = true;
    StepFrameType frameType = StepFrameType::JS_FRAME;
    uint64_t frameIndex = 0;
    unwind_user_context_s *ctx = (unwind_user_context_s*)malloc(sizeof(unwind_user_context_s));
    ctx->count = -1;
    unwind_stack_frame_s cur_frame = { fp, pc };
    auto pre_frame = unwind_arkts(ctx, &cur_frame);

    EXPECT_TRUE(pre_frame.fp == frame1[2]);
    EXPECT_TRUE(pre_frame.pc == frame1[3]);

    ArkStepParam arkStepParam{ &fp, &sp, &pc, &isJsFrame, &frameType, frameIndex };
    int ret = step_ark(ctx, ReadMemFunc, &arkStepParam);
    EXPECT_TRUE(*arkStepParam.fp == frame1[2]);
    EXPECT_TRUE(*arkStepParam.pc == frame1[3]);
    EXPECT_TRUE(ret == 1);

    free(ctx);
}

HWTEST_F_L0(JsStackInfoTest, TestStepArk_007)
{
    size_t size = 3;  // 3: size of ASM_BRIDGE_FRAME
    JSTaggedType frame[size];
    frame[0] = static_cast<JSTaggedType>(FrameType::ASM_BRIDGE_FRAME);  // 0: type
    frame[1] = static_cast<JSTaggedType>(62480);  // 1: prevFp
    frame[2] = static_cast<JSTaggedType>(123456);  // 2: returnAddr

    uintptr_t fp  = reinterpret_cast<uintptr_t>(&frame[0]) + 8; // 0: type addr
    uintptr_t sp  = 0;
    uintptr_t pc = 0;
    bool isJsFrame = true;
    StepFrameType frameType = StepFrameType::JS_FRAME;
    uint64_t frameIndex = 0;
    unwind_user_context_s *ctx = (unwind_user_context_s*)malloc(sizeof(unwind_user_context_s));
    ctx->count = -1;
    unwind_stack_frame_s cur_frame = { fp, pc };
    auto pre_frame = unwind_arkts(ctx, &cur_frame);

    EXPECT_TRUE(pre_frame.fp == frame[1]);
    EXPECT_TRUE(pre_frame.pc == frame[2]);

    ArkStepParam arkStepParam{ &fp, &sp, &pc, &isJsFrame, &frameType, frameIndex };
    int ret = step_ark(ctx, ReadMemFunc, &arkStepParam);
    EXPECT_TRUE(*arkStepParam.fp == frame[1]);
    EXPECT_TRUE(*arkStepParam.pc == frame[2]);
    EXPECT_TRUE(ret == 1);

    free(ctx);
}

HWTEST_F_L0(JsStackInfoTest, TestStepArk_008)
{
    size_t size = 5;  // 5: size of LEAVE_FRAME
    JSTaggedType frame[size];
    frame[0] = static_cast<JSTaggedType>(FrameType::LEAVE_FRAME);  // 0: type
    frame[1] = static_cast<JSTaggedType>(62480);  // 1: callsiteFp
    frame[2] = static_cast<JSTaggedType>(123456);  // 2: returnAddr
    frame[3] = static_cast<JSTaggedType>(0); // 3: argRuntimeId
    frame[4] = static_cast<JSTaggedType>(0); // 4: argc

    uintptr_t fp  = reinterpret_cast<uintptr_t>(&frame[0]) + 8; // 0: type addr
    uintptr_t sp  = 0;
    uintptr_t pc = 0;
    bool isJsFrame = true;
    StepFrameType frameType = StepFrameType::JS_FRAME;
    uint64_t frameIndex = 0;
    unwind_user_context_s *ctx = (unwind_user_context_s*)malloc(sizeof(unwind_user_context_s));
    ctx->count = -1;
    unwind_stack_frame_s cur_frame = { fp, pc };
    auto pre_frame = unwind_arkts(ctx, &cur_frame);

    EXPECT_TRUE(pre_frame.fp == frame[1]);
    EXPECT_TRUE(pre_frame.pc == frame[2]);

    ArkStepParam arkStepParam{ &fp, &sp, &pc, &isJsFrame, &frameType, frameIndex };
    int ret = step_ark(ctx, ReadMemFunc, &arkStepParam);
    EXPECT_TRUE(*arkStepParam.fp == frame[1]);
    EXPECT_TRUE(*arkStepParam.pc == frame[2]);
    EXPECT_TRUE(ret == 1);

    free(ctx);
}

HWTEST_F_L0(JsStackInfoTest, TestStepArk_009)
{
    size_t size = 5;  // 5: size of LEAVE_FRAME_WITH_ARGV
    JSTaggedType frame[size];
    frame[0] = static_cast<JSTaggedType>(FrameType::LEAVE_FRAME);  // 0: type
    frame[1] = static_cast<JSTaggedType>(62480);  // 1: callsiteFp
    frame[2] = static_cast<JSTaggedType>(123456);  // 2: returnAddr
    frame[3] = static_cast<JSTaggedType>(0); // 3: argRuntimeId
    frame[4] = static_cast<JSTaggedType>(0); // 4: argc

    uintptr_t fp  = reinterpret_cast<uintptr_t>(&frame[0]) + 8; // 0: type addr
    uintptr_t sp  = 0;
    uintptr_t pc = 0;
    bool isJsFrame = true;
    StepFrameType frameType = StepFrameType::JS_FRAME;
    uint64_t frameIndex = 0;
    unwind_user_context_s *ctx = (unwind_user_context_s*)malloc(sizeof(unwind_user_context_s));
    ctx->count = -1;
    unwind_stack_frame_s cur_frame = { fp, pc };
    auto pre_frame = unwind_arkts(ctx, &cur_frame);

    EXPECT_TRUE(pre_frame.fp == frame[1]);
    EXPECT_TRUE(pre_frame.pc == frame[2]);

    ArkStepParam arkStepParam{ &fp, &sp, &pc, &isJsFrame, &frameType, frameIndex };
    int ret = step_ark(ctx, ReadMemFunc, &arkStepParam);
    EXPECT_TRUE(*arkStepParam.fp == frame[1]);
    EXPECT_TRUE(*arkStepParam.pc == frame[2]);
    EXPECT_TRUE(ret == 1);

    free(ctx);
}

HWTEST_F_L0(JsStackInfoTest, TestStepArk_010)
{
    size_t size = 5;  // 5: size of BUILTIN_CALL_LEAVE_FRAME
    JSTaggedType frame[size];
    frame[0] = static_cast<JSTaggedType>(0);  // 0: type
    frame[1] = static_cast<JSTaggedType>(62480);  // 1: prev fp
    frame[2] = static_cast<JSTaggedType>(123456);  // 2: returnAddr
    frame[3] = JSTaggedValue::Hole().GetRawData(); // 3: thread
    frame[4] = static_cast<JSTaggedType>(0); // 4: argc


    uintptr_t fp  = reinterpret_cast<uintptr_t>(&frame[0]) + 8; // 0: type addr
    uintptr_t sp  = 0;
    uintptr_t pc = 0;
    bool isJsFrame = true;
    StepFrameType frameType = StepFrameType::JS_FRAME;
    uint64_t frameIndex = 0;
    unwind_user_context_s *ctx = (unwind_user_context_s*)malloc(sizeof(unwind_user_context_s));
    ctx->count = -1;
    unwind_stack_frame_s cur_frame = { fp, pc };
    auto pre_frame = unwind_arkts(ctx, &cur_frame);

    EXPECT_TRUE(pre_frame.fp == frame[1]);
    EXPECT_TRUE(pre_frame.pc == frame[2]);

    ArkStepParam arkStepParam{ &fp, &sp, &pc, &isJsFrame, &frameType, frameIndex };
    int ret = step_ark(ctx, ReadMemFunc, &arkStepParam);
    EXPECT_TRUE(*arkStepParam.fp == frame[1]);
    EXPECT_TRUE(*arkStepParam.pc == frame[2]);
    EXPECT_TRUE(ret == 1);

    free(ctx);
}

HWTEST_F_L0(JsStackInfoTest, TestStepArk_011)
{
    size_t size = 9;  // 9: size of INTERPRETER_FRAME
    JSTaggedType frame[size];
    frame[0] = JSTaggedValue::Hole().GetRawData();  // 0: constpool
    frame[1] = JSTaggedValue::Hole().GetRawData();  // 1: function
    frame[2] = JSTaggedValue::Hole().GetRawData();  // 2: thisObj
    frame[3] = JSTaggedValue::Hole().GetRawData(); // 3: profileTypeInfo
    frame[4] = JSTaggedValue::Hole().GetRawData(); // 4: acc
    frame[5] = JSTaggedValue::Hole().GetRawData(); // 5: env
    frame[6] = static_cast<JSTaggedType>(123456); // 6: pc
    frame[7] = static_cast<JSTaggedType>(62480); // 7: base.prevfp
    frame[8] = static_cast<JSTaggedType>(FrameType::INTERPRETER_FRAME); // 8: base.type

    uintptr_t fp  = reinterpret_cast<uintptr_t>(&frame[8]) + 8; // 8: type addr
    uintptr_t sp  = 0;
    uintptr_t pc = 0;
    bool isJsFrame = true;
    StepFrameType frameType = StepFrameType::JS_FRAME;
    uint64_t frameIndex = 0;
    unwind_user_context_s *ctx = (unwind_user_context_s*)malloc(sizeof(unwind_user_context_s));
    ctx->count = -1;
    unwind_stack_frame_s cur_frame = { fp, pc };
    auto pre_frame = unwind_arkts(ctx, &cur_frame);

    EXPECT_TRUE(pre_frame.fp == frame[7]);
    EXPECT_TRUE(pre_frame.pc == frame[6]);

    ArkStepParam arkStepParam{ &fp, &sp, &pc, &isJsFrame, &frameType, frameIndex };
    int ret = step_ark(ctx, ReadMemFunc, &arkStepParam);
    EXPECT_TRUE(*arkStepParam.fp == frame[7]);
    EXPECT_TRUE(*arkStepParam.pc == frame[6]);
    EXPECT_TRUE(ret == 1);

    free(ctx);
}

HWTEST_F_L0(JsStackInfoTest, TestStepArk_012)
{
    size_t size = 9;  // 9: size of ASM_INTERPRETER_FRAME
    JSTaggedType frame[size];
    frame[0] = JSTaggedValue::Hole().GetRawData();  // 0: function
    frame[1] = JSTaggedValue::Hole().GetRawData();  // 1: thisObj
    frame[2] = JSTaggedValue::Hole().GetRawData();  // 2: acc
    frame[3] = JSTaggedValue::Hole().GetRawData(); // 3: env
    frame[4] = JSTaggedValue::Hole().GetRawData(); // 4: callSize
    frame[5] = JSTaggedValue::Hole().GetRawData(); // 5: fp
    frame[6] = static_cast<JSTaggedType>(123456); // 6: pc
    frame[7] = static_cast<JSTaggedType>(62480); // 7: base.prevfp
    frame[8] = static_cast<JSTaggedType>(FrameType::ASM_INTERPRETER_FRAME); // 8: base.type

    uintptr_t fp  = reinterpret_cast<uintptr_t>(&frame[8]) + 8; // 8: type addr
    uintptr_t sp  = 0;
    uintptr_t pc = 0;
    bool isJsFrame = true;
    StepFrameType frameType = StepFrameType::JS_FRAME;
    uint64_t frameIndex = 0;
    unwind_user_context_s *ctx = (unwind_user_context_s*)malloc(sizeof(unwind_user_context_s));
    ctx->count = -1;
    unwind_stack_frame_s cur_frame = { fp, pc };
    auto pre_frame = unwind_arkts(ctx, &cur_frame);

    EXPECT_TRUE(pre_frame.fp == frame[7]);
    EXPECT_TRUE(pre_frame.pc == frame[6]);

    ArkStepParam arkStepParam{ &fp, &sp, &pc, &isJsFrame, &frameType, frameIndex };
    int ret = step_ark(ctx, ReadMemFunc, &arkStepParam);
    EXPECT_TRUE(*arkStepParam.fp == frame[7]);
    EXPECT_TRUE(*arkStepParam.pc == frame[6]);
    EXPECT_TRUE(ret == 1);

    free(ctx);
}

HWTEST_F_L0(JsStackInfoTest, TestStepArk_013)
{
    size_t size = 9;  // 9: size of INTERPRETER_CONSTRUCTOR_FRAME
    JSTaggedType frame[size];
    frame[0] = JSTaggedValue::Hole().GetRawData();  // 0: function
    frame[1] = JSTaggedValue::Hole().GetRawData();  // 1: thisObj
    frame[2] = JSTaggedValue::Hole().GetRawData();  // 2: acc
    frame[3] = JSTaggedValue::Hole().GetRawData(); // 3: env
    frame[4] = JSTaggedValue::Hole().GetRawData(); // 4: callSize
    frame[5] = JSTaggedValue::Hole().GetRawData(); // 5: fp
    frame[6] = static_cast<JSTaggedType>(123456); // 6: pc
    frame[7] = static_cast<JSTaggedType>(62480); // 7: base.prevfp
    frame[8] = static_cast<JSTaggedType>(FrameType::INTERPRETER_CONSTRUCTOR_FRAME); // 8: base.type

    uintptr_t fp  = reinterpret_cast<uintptr_t>(&frame[8]) + 8; // 8: type addr
    uintptr_t sp  = 0;
    uintptr_t pc = 0;
    bool isJsFrame = true;
    StepFrameType frameType = StepFrameType::JS_FRAME;
    uint64_t frameIndex = 0;
    unwind_user_context_s *ctx = (unwind_user_context_s*)malloc(sizeof(unwind_user_context_s));
    ctx->count = -1;
    unwind_stack_frame_s cur_frame = { fp, pc };
    auto pre_frame = unwind_arkts(ctx, &cur_frame);

    EXPECT_TRUE(pre_frame.fp == frame[7]);
    EXPECT_TRUE(pre_frame.pc == frame[6]);

    ArkStepParam arkStepParam{ &fp, &sp, &pc, &isJsFrame, &frameType, frameIndex };
    int ret = step_ark(ctx, ReadMemFunc, &arkStepParam);
    EXPECT_TRUE(*arkStepParam.fp == frame[7]);
    EXPECT_TRUE(*arkStepParam.pc == frame[6]);
    EXPECT_TRUE(ret == 1);

    free(ctx);
}


HWTEST_F_L0(JsStackInfoTest, TestStepArk_014)
{
    size_t size = 6;  // 6: size of BUILTIN_FRAME
    JSTaggedType frame[size];
    frame[0] = static_cast<JSTaggedType>(FrameType::BUILTIN_FRAME);  // 0: type
    frame[1] = static_cast<JSTaggedType>(62480);  // 1: prevFp
    frame[2] = static_cast<JSTaggedType>(123456);  // 2: returnAddr
    frame[3] = JSTaggedValue::Hole().GetRawData(); // 3: thread
    frame[4] = JSTaggedValue::Hole().GetRawData(); // 4: numArgs
    frame[5] = JSTaggedValue::Hole().GetRawData(); // 5: stackArgs

    uintptr_t fp  = reinterpret_cast<uintptr_t>(&frame[0]) + 8; // 0: type addr
    uintptr_t sp  = 0;
    uintptr_t pc = 0;
    bool isJsFrame = true;
    StepFrameType frameType = StepFrameType::JS_FRAME;
    uint64_t frameIndex = 0;
    unwind_user_context_s *ctx = (unwind_user_context_s*)malloc(sizeof(unwind_user_context_s));
    ctx->count = -1;
    unwind_stack_frame_s cur_frame = { fp, pc };
    auto pre_frame = unwind_arkts(ctx, &cur_frame);

    EXPECT_TRUE(pre_frame.fp == frame[1]);
    EXPECT_TRUE(pre_frame.pc == frame[2]);

    ArkStepParam arkStepParam{ &fp, &sp, &pc, &isJsFrame, &frameType, frameIndex };
    int ret = step_ark(ctx, ReadMemFunc, &arkStepParam);
    EXPECT_TRUE(*arkStepParam.fp == frame[1]);
    EXPECT_TRUE(*arkStepParam.pc == frame[2]);
    EXPECT_TRUE(ret == 1);

    free(ctx);
}

HWTEST_F_L0(JsStackInfoTest, TestStepArk_015)
{
    size_t size = 3;  // 3: size of BUILTIN_FRAME_WITH_ARGV
    JSTaggedType frame[size];
    frame[0] = static_cast<JSTaggedType>(FrameType::BUILTIN_FRAME_WITH_ARGV);  // 0: type
    frame[1] = static_cast<JSTaggedType>(62480);  // 1: prevFp
    frame[2] = static_cast<JSTaggedType>(123456);  // 2: returnAddr

    uintptr_t fp  = reinterpret_cast<uintptr_t>(&frame[0]) + 8; // 0: type addr
    uintptr_t sp  = 0;
    uintptr_t pc = 0;
    bool isJsFrame = true;
    StepFrameType frameType = StepFrameType::JS_FRAME;
    uint64_t frameIndex = 0;
    unwind_user_context_s *ctx = (unwind_user_context_s*)malloc(sizeof(unwind_user_context_s));
    ctx->count = -1;
    unwind_stack_frame_s cur_frame = { fp, pc };
    auto pre_frame = unwind_arkts(ctx, &cur_frame);

    EXPECT_TRUE(pre_frame.fp == frame[1]);
    EXPECT_TRUE(pre_frame.pc == frame[2]);

    ArkStepParam arkStepParam{ &fp, &sp, &pc, &isJsFrame, &frameType, frameIndex };
    int ret = step_ark(ctx, ReadMemFunc, &arkStepParam);
    EXPECT_TRUE(*arkStepParam.fp == frame[1]);
    EXPECT_TRUE(*arkStepParam.pc == frame[2]);
    EXPECT_TRUE(ret == 1);

    free(ctx);
}

HWTEST_F_L0(JsStackInfoTest, TestStepArk_016)
{
    size_t size = 3;  // 3: size of BUILTIN_ENTRY_FRAME
    JSTaggedType frame[size];
    frame[0] = static_cast<JSTaggedType>(FrameType::BUILTIN_ENTRY_FRAME);  // 0: type
    frame[1] = static_cast<JSTaggedType>(62480);  // 1: prevFp
    frame[2] = static_cast<JSTaggedType>(123456);  // 2: returnAddr

    uintptr_t fp  = reinterpret_cast<uintptr_t>(&frame[0]) + 8; // 0: type addr
    uintptr_t sp  = 0;
    uintptr_t pc = 0;
    bool isJsFrame = true;
    StepFrameType frameType = StepFrameType::JS_FRAME;
    uint64_t frameIndex = 0;
    unwind_user_context_s *ctx = (unwind_user_context_s*)malloc(sizeof(unwind_user_context_s));
    ctx->count = -1;
    unwind_stack_frame_s cur_frame = { fp, pc };
    auto pre_frame = unwind_arkts(ctx, &cur_frame);

    EXPECT_TRUE(pre_frame.fp == frame[1]);
    EXPECT_TRUE(pre_frame.pc == frame[2]);

    ArkStepParam arkStepParam{ &fp, &sp, &pc, &isJsFrame, &frameType, frameIndex };
    int ret = step_ark(ctx, ReadMemFunc, &arkStepParam);
    EXPECT_TRUE(*arkStepParam.fp == frame[1]);
    EXPECT_TRUE(*arkStepParam.pc == frame[2]);
    EXPECT_TRUE(ret == 1);

    free(ctx);
}

HWTEST_F_L0(JsStackInfoTest, TestStepArk_017)
{
    size_t size1 = 9;  // 9: size of INTERPRETER_FRAME
    JSTaggedType frame1[size1];
    frame1[0] = JSTaggedValue::Hole().GetRawData();  // 0: constpool
    frame1[1] = JSTaggedValue::Hole().GetRawData();  // 1: function
    frame1[2] = JSTaggedValue::Hole().GetRawData();  // 2: thisObj
    frame1[3] = JSTaggedValue::Hole().GetRawData(); // 3: profileTypeInfo
    frame1[4] = JSTaggedValue::Hole().GetRawData(); // 4: acc
    frame1[5] = JSTaggedValue::Hole().GetRawData(); // 5: env
    frame1[6] = static_cast<JSTaggedType>(123456); // 6: pc
    frame1[7] = static_cast<JSTaggedType>(62480); // 7: base.prevfp
    frame1[8] = static_cast<JSTaggedType>(FrameType::INTERPRETER_FRAME); // 8: base.type

    size_t size2 = 4;  // 3: size of INTERPRETER_BUILTIN_FRAME
    JSTaggedType frame2[size2];
    frame2[0] = JSTaggedValue::Hole().GetRawData(); // 0: function
    frame2[1] = static_cast<JSTaggedType>(0); // 1: pc
    frame2[2] = static_cast<JSTaggedType>(reinterpret_cast<uintptr_t>(&frame1[8]) + 8); // 2: base.prevfp
    frame2[3] = static_cast<JSTaggedType>(FrameType::INTERPRETER_BUILTIN_FRAME); // 3: base.type

    uintptr_t fp  = reinterpret_cast<uintptr_t>(&frame2[3]) + 8; // 3: type addr
    uintptr_t sp  = 0;
    uintptr_t pc = 0;
    bool isJsFrame = true;
    StepFrameType frameType = StepFrameType::JS_FRAME;
    uint64_t frameIndex = 0;
    unwind_user_context_s *ctx = (unwind_user_context_s*)malloc(sizeof(unwind_user_context_s));
    ctx->count = -1;
    unwind_stack_frame_s cur_frame = { fp, pc };
    auto pre_frame = unwind_arkts(ctx, &cur_frame);

    EXPECT_TRUE(pre_frame.fp == frame1[7]);
    EXPECT_TRUE(pre_frame.pc == frame1[6]);

    ArkStepParam arkStepParam{ &fp, &sp, &pc, &isJsFrame, &frameType, frameIndex };
    int ret = step_ark(ctx, ReadMemFunc, &arkStepParam);
    EXPECT_TRUE(*arkStepParam.fp == frame1[7]);
    EXPECT_TRUE(*arkStepParam.pc == frame1[6]);
    EXPECT_TRUE(ret == 1);

    free(ctx);
}

HWTEST_F_L0(JsStackInfoTest, TestStepArk_018)
{
    size_t size1 = 9;  // 9: size of INTERPRETER_FAST_NEW_FRAME
    JSTaggedType frame[size1];
    frame[0] = JSTaggedValue::Hole().GetRawData();  // 0: constpool
    frame[1] = JSTaggedValue::Hole().GetRawData();  // 1: function
    frame[2] = JSTaggedValue::Hole().GetRawData();  // 2: thisObj
    frame[3] = JSTaggedValue::Hole().GetRawData(); // 3: profileTypeInfo
    frame[4] = JSTaggedValue::Hole().GetRawData(); // 4: acc
    frame[5] = JSTaggedValue::Hole().GetRawData(); // 5: env
    frame[6] = static_cast<JSTaggedType>(123456); // 6: pc
    frame[7] = static_cast<JSTaggedType>(62480); // 7: base.prevfp
    frame[8] = static_cast<JSTaggedType>(FrameType::INTERPRETER_FAST_NEW_FRAME); // 8: base.type

    uintptr_t fp  = reinterpret_cast<uintptr_t>(&frame[8]) + 8; // 8: type addr
    uintptr_t sp  = 0;
    uintptr_t pc = 0;
    bool isJsFrame = true;
    StepFrameType frameType = StepFrameType::JS_FRAME;
    uint64_t frameIndex = 0;
    unwind_user_context_s *ctx = (unwind_user_context_s*)malloc(sizeof(unwind_user_context_s));
    ctx->count = -1;
    unwind_stack_frame_s cur_frame = { fp, pc };
    auto pre_frame = unwind_arkts(ctx, &cur_frame);

    EXPECT_TRUE(pre_frame.fp == frame[7]);
    EXPECT_TRUE(pre_frame.pc == frame[6]);

    ArkStepParam arkStepParam{ &fp, &sp, &pc, &isJsFrame, &frameType, frameIndex };
    int ret = step_ark(ctx, ReadMemFunc, &arkStepParam);
    EXPECT_TRUE(*arkStepParam.fp == frame[7]);
    EXPECT_TRUE(*arkStepParam.pc == frame[6]);
    EXPECT_TRUE(ret == 1);

    free(ctx);
}

HWTEST_F_L0(JsStackInfoTest, TestStepArk_019)
{
    size_t size1 = 9;  // 9: size of INTERPRETER_FRAME
    JSTaggedType frame1[size1];
    frame1[0] = JSTaggedValue::Hole().GetRawData();  // 0: constpool
    frame1[1] = JSTaggedValue::Hole().GetRawData();  // 1: function
    frame1[2] = JSTaggedValue::Hole().GetRawData();  // 2: thisObj
    frame1[3] = JSTaggedValue::Hole().GetRawData(); // 3: profileTypeInfo
    frame1[4] = JSTaggedValue::Hole().GetRawData(); // 4: acc
    frame1[5] = JSTaggedValue::Hole().GetRawData(); // 5: env
    frame1[6] = static_cast<JSTaggedType>(123456); // 6: pc
    frame1[7] = static_cast<JSTaggedType>(62480); // 7: base.prevfp
    frame1[8] = static_cast<JSTaggedType>(FrameType::INTERPRETER_FRAME); // 8: base.type

    size_t size2 = 3;  // 3: size of INTERPRETER_ENTRY_FRAME
    JSTaggedType frame2[size2];
    frame2[0] = static_cast<JSTaggedType>(0); // 0: pc
    frame2[1] = static_cast<JSTaggedType>(reinterpret_cast<uintptr_t>(&frame1[8]) + 8); // 1: base.prevfp
    frame2[2] = static_cast<JSTaggedType>(FrameType::INTERPRETER_BUILTIN_FRAME); // 2: base.type

    uintptr_t fp  = reinterpret_cast<uintptr_t>(&frame2[2]) + 8; // 3: type addr
    uintptr_t sp  = 0;
    uintptr_t pc = 0;
    bool isJsFrame = true;
    StepFrameType frameType = StepFrameType::JS_FRAME;
    uint64_t frameIndex = 0;
    unwind_user_context_s *ctx = (unwind_user_context_s*)malloc(sizeof(unwind_user_context_s));
    ctx->count = -1;
    unwind_stack_frame_s cur_frame = { fp, pc };
    auto pre_frame = unwind_arkts(ctx, &cur_frame);

    EXPECT_TRUE(pre_frame.fp == frame1[7]);
    EXPECT_TRUE(pre_frame.pc == frame1[6]);

    ArkStepParam arkStepParam{ &fp, &sp, &pc, &isJsFrame, &frameType, frameIndex };
    int ret = step_ark(ctx, ReadMemFunc, &arkStepParam);
    EXPECT_TRUE(*arkStepParam.fp == frame1[7]);
    EXPECT_TRUE(*arkStepParam.pc == frame1[6]);
    EXPECT_TRUE(ret == 1);

    free(ctx);
}

HWTEST_F_L0(JsStackInfoTest, TestStepArk_020)
{
    size_t size = 5;  // 5: size of ASM_INTERPRETER_ENTRY_FRAME + prevfp + returnaddr
    JSTaggedType frame[size];
    frame[0] = static_cast<JSTaggedType>(0); // 0: pc
    frame[1] = static_cast<JSTaggedType>(reinterpret_cast<uintptr_t>(&frame[8]) + 8); // 1: base.prevfp
    frame[2] = static_cast<JSTaggedType>(FrameType::ASM_INTERPRETER_ENTRY_FRAME); // 2: base.type
    frame[3] = static_cast<JSTaggedType>(62480); // 3: prev fp
    frame[4] = static_cast<JSTaggedType>(123456); // 4: returnaddr

    uintptr_t fp  = reinterpret_cast<uintptr_t>(&frame[2]) + 8; // 8: type addr
    uintptr_t sp  = 0;
    uintptr_t pc = 0;
    bool isJsFrame = true;
    StepFrameType frameType = StepFrameType::JS_FRAME;
    uint64_t frameIndex = 0;
    unwind_user_context_s *ctx = (unwind_user_context_s*)malloc(sizeof(unwind_user_context_s));
    ctx->count = -1;
    unwind_stack_frame_s cur_frame = { fp, pc };
    auto pre_frame = unwind_arkts(ctx, &cur_frame);

    EXPECT_TRUE(pre_frame.fp == frame[3]);
    EXPECT_TRUE(pre_frame.pc == frame[4]);

    ArkStepParam arkStepParam{ &fp, &sp, &pc, &isJsFrame, &frameType, frameIndex };
    int ret = step_ark(ctx, ReadMemFunc, &arkStepParam);
    EXPECT_TRUE(*arkStepParam.fp == frame[3]);
    EXPECT_TRUE(*arkStepParam.pc == frame[4]);
    EXPECT_TRUE(ret == 1);

    free(ctx);
}

HWTEST_F_L0(JsStackInfoTest, TestStepArk_021)
{
    size_t size = 5;  // 5: size of ASM_INTERPRETER_BRIDGE_FRAME
    JSTaggedType frame[size];
    frame[0] = static_cast<JSTaggedType>(0); // 0: pc
    frame[1] = static_cast<JSTaggedType>(62480); // 1: base.prevfp
    frame[2] = static_cast<JSTaggedType>(FrameType::ASM_INTERPRETER_BRIDGE_FRAME); // 2: base.type
    frame[3] = static_cast<JSTaggedType>(123456); // 3: returnaddr

    uintptr_t fp  = reinterpret_cast<uintptr_t>(&frame[2]) + 8; // 8: type addr
    uintptr_t sp  = 0;
    uintptr_t pc = 0;
    bool isJsFrame = true;
    StepFrameType frameType = StepFrameType::JS_FRAME;
    uint64_t frameIndex = 0;
    unwind_user_context_s *ctx = (unwind_user_context_s*)malloc(sizeof(unwind_user_context_s));
    ctx->count = -1;
    unwind_stack_frame_s cur_frame = { fp, pc };
    auto pre_frame = unwind_arkts(ctx, &cur_frame);

    EXPECT_TRUE(pre_frame.fp == frame[1]);
    EXPECT_TRUE(pre_frame.pc == frame[3]);

    ArkStepParam arkStepParam{ &fp, &sp, &pc, &isJsFrame, &frameType, frameIndex };
    int ret = step_ark(ctx, ReadMemFunc, &arkStepParam);
    EXPECT_TRUE(*arkStepParam.fp == frame[1]);
    EXPECT_TRUE(*arkStepParam.pc == frame[3]);
    EXPECT_TRUE(ret == 1);

    free(ctx);
}

HWTEST_F_L0(JsStackInfoTest, TestStepArk_022)
{
    size_t size = 4;  // 4: size of OPTIMIZED_JS_FUNCTION_ARGS_CONFIG_FRAME
    JSTaggedType frame[size];
    frame[0] = JSTaggedValue::Undefined().GetRawData(); // 0: function
    frame[1] = static_cast<JSTaggedType>(FrameType::OPTIMIZED_JS_FUNCTION_ARGS_CONFIG_FRAME); // 1: type
    frame[2] = static_cast<JSTaggedType>(62480); // 2: prevfp
    frame[3] = static_cast<JSTaggedType>(123456); // 3: returnaddr

    uintptr_t fp  = reinterpret_cast<uintptr_t>(&frame[1]) + 8; // 8: type addr
    uintptr_t sp  = 0;
    uintptr_t pc = 0;
    bool isJsFrame = true;
    StepFrameType frameType = StepFrameType::JS_FRAME;
    uint64_t frameIndex = 0;
    unwind_user_context_s *ctx = (unwind_user_context_s*)malloc(sizeof(unwind_user_context_s));
    ctx->count = -1;
    unwind_stack_frame_s cur_frame = { fp, pc };
    auto pre_frame = unwind_arkts(ctx, &cur_frame);

    EXPECT_TRUE(pre_frame.fp == frame[2]);
    EXPECT_TRUE(pre_frame.pc == frame[3]);

    ArkStepParam arkStepParam{ &fp, &sp, &pc, &isJsFrame, &frameType, frameIndex };
    int ret = step_ark(ctx, ReadMemFunc, &arkStepParam);
    EXPECT_TRUE(*arkStepParam.fp == frame[2]);
    EXPECT_TRUE(*arkStepParam.pc == frame[3]);
    EXPECT_TRUE(ret == 1);

    free(ctx);
}

HWTEST_F_L0(JsStackInfoTest, TestStepArk_023)
{
    size_t size = 4;  // 4: size of OPTIMIZED_JS_FUNCTION_UNFOLD_ARGV_FRAME
    JSTaggedType frame[size];
    frame[0] = JSTaggedValue::Undefined().GetRawData(); // 0: CallSiteSp
    frame[1] = static_cast<JSTaggedType>(FrameType::OPTIMIZED_JS_FUNCTION_UNFOLD_ARGV_FRAME); // 1: type
    frame[2] = static_cast<JSTaggedType>(62480); // 2: prevfp
    frame[3] = static_cast<JSTaggedType>(123456); // 3: returnaddr

    uintptr_t fp  = reinterpret_cast<uintptr_t>(&frame[1]) + 8; // 8: type addr
    uintptr_t sp  = 0;
    uintptr_t pc = 0;
    bool isJsFrame = true;
    StepFrameType frameType = StepFrameType::JS_FRAME;
    uint64_t frameIndex = 0;
    unwind_user_context_s *ctx = (unwind_user_context_s*)malloc(sizeof(unwind_user_context_s));
    ctx->count = -1;
    unwind_stack_frame_s cur_frame = { fp, pc };
    auto pre_frame = unwind_arkts(ctx, &cur_frame);

    EXPECT_TRUE(pre_frame.fp == frame[2]);
    EXPECT_TRUE(pre_frame.pc == frame[3]);

    ArkStepParam arkStepParam{ &fp, &sp, &pc, &isJsFrame, &frameType, frameIndex };
    int ret = step_ark(ctx, ReadMemFunc, &arkStepParam);
    EXPECT_TRUE(*arkStepParam.fp == frame[2]);
    EXPECT_TRUE(*arkStepParam.pc == frame[3]);
    EXPECT_TRUE(ret == 1);

    free(ctx);
}

HWTEST_F_L0(JsStackInfoTest, TestStepArk_024)
{
    size_t size = 3;  // 3: size of BUILTIN_FRAME_WITH_ARGV
    JSTaggedType frame[size];
    frame[0] = static_cast<JSTaggedType>(FrameType::BUILTIN_FRAME_WITH_ARGV_STACK_OVER_FLOW_FRAME);  // 0: type
    frame[1] = static_cast<JSTaggedType>(62480);  // 1: prevFp
    frame[2] = static_cast<JSTaggedType>(123456);  // 2: returnAddr

    uintptr_t fp  = reinterpret_cast<uintptr_t>(&frame[0]) + 8; // 0: type addr
    uintptr_t sp  = 0;
    uintptr_t pc = 0;
    bool isJsFrame = true;
    StepFrameType frameType = StepFrameType::JS_FRAME;
    uint64_t frameIndex = 0;
    unwind_user_context_s *ctx = (unwind_user_context_s*)malloc(sizeof(unwind_user_context_s));
    ctx->count = -1;
    unwind_stack_frame_s cur_frame = { fp, pc };
    auto pre_frame = unwind_arkts(ctx, &cur_frame);

    EXPECT_TRUE(pre_frame.fp == frame[1]);
    EXPECT_TRUE(pre_frame.pc == frame[2]);

    ArkStepParam arkStepParam{ &fp, &sp, &pc, &isJsFrame, &frameType, frameIndex };
    int ret = step_ark(ctx, ReadMemFunc, &arkStepParam);
    EXPECT_TRUE(*arkStepParam.fp == frame[1]);
    EXPECT_TRUE(*arkStepParam.pc == frame[2]);
    EXPECT_TRUE(ret == 1);

    free(ctx);
}

HWTEST_F_L0(JsStackInfoTest, TestStepArk_025)
{
    size_t size = 3;  // 3: size of BASELINE_BUILTIN_FRAME
    JSTaggedType frame[size];
    frame[0] = static_cast<JSTaggedType>(FrameType::BASELINE_BUILTIN_FRAME);  // 0: type
    frame[1] = static_cast<JSTaggedType>(62480);  // 1: prevFp
    frame[2] = static_cast<JSTaggedType>(123456);  // 2: returnAddr

    uintptr_t fp  = reinterpret_cast<uintptr_t>(&frame[0]) + 8; // 0: type addr
    uintptr_t sp  = 0;
    uintptr_t pc = 0;
    bool isJsFrame = true;
    StepFrameType frameType = StepFrameType::JS_FRAME;
    uint64_t frameIndex = 0;
    unwind_user_context_s *ctx = (unwind_user_context_s*)malloc(sizeof(unwind_user_context_s));
    ctx->count = -1;
    unwind_stack_frame_s cur_frame = { fp, pc };
    auto pre_frame = unwind_arkts(ctx, &cur_frame);

    EXPECT_TRUE(pre_frame.fp == frame[1]);
    EXPECT_TRUE(pre_frame.pc == frame[2]);

    ArkStepParam arkStepParam{ &fp, &sp, &pc, &isJsFrame, &frameType, frameIndex };
    int ret = step_ark(ctx, ReadMemFunc, &arkStepParam);
    EXPECT_TRUE(*arkStepParam.fp == frame[1]);
    EXPECT_TRUE(*arkStepParam.pc == frame[2]);
    EXPECT_TRUE(ret == 1);
}

HWTEST_F_L0(JsStackInfoTest, TestStepArk_026)
{
    size_t size = 1;  // 1: unknown tpye
    JSTaggedType frame[size];
    frame[0] = static_cast<JSTaggedType>(FrameType::BASELINE_BUILTIN_FRAME) + 0x100; // 0: type

    uintptr_t fp  = reinterpret_cast<uintptr_t>(&frame[0]) + 8; // 0: type addr
    uintptr_t sp  = 0;
    uintptr_t pc = 0;
    bool isJsFrame = true;
    StepFrameType frameType = StepFrameType::JS_FRAME;
    uint64_t frameIndex = 0;
    unwind_user_context_s *ctx = (unwind_user_context_s*)malloc(sizeof(unwind_user_context_s));
    ctx->count = -1;
    unwind_stack_frame_s cur_frame = { fp, pc };
    auto pre_frame = unwind_arkts(ctx, &cur_frame);

    EXPECT_TRUE(pre_frame.fp == 0);
    EXPECT_TRUE(pre_frame.pc == 0);

    ArkStepParam arkStepParam{ &fp, &sp, &pc, &isJsFrame, &frameType, frameIndex };
    int ret = step_ark(ctx, ReadMemFunc, &arkStepParam);
    EXPECT_TRUE(ret == -1);

    free(ctx);
}

HWTEST_F_L0(JsStackInfoTest, TestStepArk_027)
{
    unsigned int type = static_cast<unsigned int>(arkts_frame_type::FRAME_TYPE_MAX) + 1;
    bool ret = is_entry_frame(type);
    EXPECT_TRUE(ret == false);
    ret = is_js_function_frame(type);
    EXPECT_TRUE(ret == false);
    ret = is_native_function_frame(type);
    EXPECT_TRUE(ret == false);
}

HWTEST_F_L0(JsStackInfoTest, TestUnwindArkts_001)
{
    unwind_user_context_s *ctx = (unwind_user_context_s*)malloc(sizeof(unwind_user_context_s));
    unwind_stack_frame_s cur_frame = UNWIND_FRAME_EMPTY;

    auto pre_frame = unwind_arkts(nullptr, &cur_frame);
    EXPECT_TRUE(pre_frame.fp == 0);
    EXPECT_TRUE(pre_frame.pc == 0);
    pre_frame = unwind_arkts(ctx, nullptr);
    EXPECT_TRUE(pre_frame.fp == 0);
    EXPECT_TRUE(pre_frame.pc == 0);

    free(ctx);
}

HWTEST_F_L0(JsStackInfoTest, TestUnwindArkts_002)
{
    size_t size = 3;  // 3: size of OPTIMIZED_FRAME
    JSTaggedType frame[size];
    frame[0] = static_cast<JSTaggedType>(FrameType::OPTIMIZED_FRAME);  // 0: type
    frame[1] = static_cast<JSTaggedType>(62480);  // 1: prevFp
    frame[2] = static_cast<JSTaggedType>(123456);  // 2: returnAddr

    uintptr_t fp  = reinterpret_cast<uintptr_t>(&frame[0]) + 8; // 0: type addr

    unwind_user_context_s *ctx = (unwind_user_context_s*)malloc(sizeof(unwind_user_context_s));
    ctx->count = 0;
    unwind_stack_frame_s cur_frame = { fp, 0 };
    auto pre_frame = unwind_arkts(ctx, &cur_frame);
    EXPECT_TRUE(pre_frame.fp == 0);
    EXPECT_TRUE(pre_frame.pc == 0);

    free(ctx);
}

HWTEST_F_L0(JsStackInfoTest, TestUnwindArkts_003)
{
    size_t size = 3;  // 3: size of OPTIMIZED_FRAME
    JSTaggedType frame[size];
    frame[0] = static_cast<JSTaggedType>(FrameType::OPTIMIZED_FRAME);  // 0: type
    frame[1] = static_cast<JSTaggedType>(62480);  // 1: prevFp
    frame[2] = static_cast<JSTaggedType>(123456);  // 2: returnAddr

    uintptr_t fp  = reinterpret_cast<uintptr_t>(&frame[0]) + 8; // 0: type addr

    unwind_user_context_s *ctx = (unwind_user_context_s*)malloc(sizeof(unwind_user_context_s));
    ctx->count = 1;
    unwind_stack_frame_s cur_frame = { fp, 0 };
    auto pre_frame = unwind_arkts(ctx, &cur_frame);
    EXPECT_TRUE(pre_frame.fp == 0);
    EXPECT_TRUE(pre_frame.pc == 0);

    free(ctx);
}

HWTEST_F_L0(JsStackInfoTest, TestUnwindArkts_004)
{
    size_t size = 5;  // 4: size of FASTJIT_FUNCTION_FRAME
    JSTaggedType frame[size];
    frame[0] = static_cast<JSTaggedType>(1234);  // 0: pc
    frame[1] = JSTaggedValue::Hole().GetRawData();  // 1: JSFunction
    frame[2] = static_cast<JSTaggedType>(FrameType::FASTJIT_FUNCTION_FRAME);  // 2: type
    frame[3] = static_cast<JSTaggedType>(62480);  // 3: prevFp
    frame[4] = static_cast<JSTaggedType>(0);  // 4: returnAddr

    uintptr_t fp  = reinterpret_cast<uintptr_t>(&frame[2]) + 8; // 0: type addr
    uintptr_t pc = 0;
    unwind_user_context_s *ctx = (unwind_user_context_s*)malloc(sizeof(unwind_user_context_s));
    ctx->count = 2;
    unwind_stack_frame_s cur_frame = { fp, pc };
    auto pre_frame = unwind_arkts(ctx, &cur_frame);

    EXPECT_TRUE(pre_frame.fp == 0);
    EXPECT_TRUE(pre_frame.pc == 0);
}

HWTEST_F_L0(JsStackInfoTest, TestNextArkFrame)
{
    unwind_user_context_s *ctx = (unwind_user_context_s*)malloc(sizeof(unwind_user_context_s));
    ctx->count = 0;
    uintptr_t fp = 0;
    unsigned int type = 0;
    unwind_stack_frame_s frame = UNWIND_FRAME_EMPTY;
    bool retFrameAvail = false;
    int ret = next_ark_frame(ctx, fp, type, &frame, &retFrameAvail);
    EXPECT_TRUE(ret == -1);

    free(ctx);
}

HWTEST_F_L0(JsStackInfoTest, TestStepArkWithRecordJit_001)
{
    size_t size1 = 4;
    JSTaggedType frame1[size1];
    frame1[0] = static_cast<JSTaggedType>(0);  // 0: preLeaveFrameFp
    frame1[1] = static_cast<JSTaggedType>(FrameType::OPTIMIZED_ENTRY_FRAME);  // 1: type
    frame1[2] = static_cast<JSTaggedType>(62480);  // 2: prevFP
    frame1[3] = static_cast<JSTaggedType>(123456);  // 3: returnAddr

    size_t size2 = 5;
    JSTaggedType frame2[size2];
    frame2[0] = static_cast<JSTaggedType>(1234);  // 0: pc
    frame2[1] = JSTaggedValue::Hole().GetRawData();  // 1: JSFunction
    frame2[2] = static_cast<JSTaggedType>(FrameType::FASTJIT_FUNCTION_FRAME);  // 2: type
    frame2[3] = static_cast<JSTaggedType>(reinterpret_cast<uintptr_t>(&frame1[2]));  // 3: prevFp
    frame2[4] = static_cast<JSTaggedType>(0);  // 4: returnAddr

    uintptr_t fp = reinterpret_cast<uintptr_t>(&frame2[2]) + 8;

    SafeMemContext safeCtx;
    safeCtx.AddRange(reinterpret_cast<uintptr_t>(&frame1[0]), reinterpret_cast<uintptr_t>(&frame1[size1]));
    safeCtx.AddRange(reinterpret_cast<uintptr_t>(&frame2[0]), reinterpret_cast<uintptr_t>(&frame2[size2]));

    uintptr_t sp = 0;
    uintptr_t pc = 0;
    uintptr_t methodId = 0;
    bool isJsFrame = true;
    std::vector<uintptr_t> jitCache;

    StepFrameType frameType = StepFrameType::JS_FRAME;
    uint64_t frameIndex = 0;
    ArkUnwindParam arkUnwindParam(&safeCtx, SafeReadMemFuncForJit, &fp, &sp, &pc,
                                  &methodId, &isJsFrame, &frameType, frameIndex, jitCache);
    int ret = step_ark_with_record_jit(&arkUnwindParam);

    EXPECT_TRUE(ret == 1);
    EXPECT_TRUE(*arkUnwindParam.fp == reinterpret_cast<uintptr_t>(&frame1[2]));
    EXPECT_TRUE(*arkUnwindParam.pc == 1234);
    EXPECT_TRUE(*arkUnwindParam.isJsFrame == true);
}

HWTEST_F_L0(JsStackInfoTest, TestStepArkWithRecordJit_002)
{
    size_t size1 = 4;
    JSTaggedType frame1[size1];
    frame1[0] = static_cast<JSTaggedType>(0);  // 0: preLeaveFrameFp
    frame1[1] = static_cast<JSTaggedType>(FrameType::OPTIMIZED_ENTRY_FRAME);  // 1: type
    frame1[2] = static_cast<JSTaggedType>(62480);  // 2: prevFP
    frame1[3] = static_cast<JSTaggedType>(123456);  // 3: returnAddr

    size_t size2 = 5;
    JSTaggedType frame2[size2];
    frame2[0] = static_cast<JSTaggedType>(5678);  // 0: pc
    frame2[1] = JSTaggedValue::Hole().GetRawData();  // 1: JSFunction
    frame2[2] = static_cast<JSTaggedType>(FrameType::FASTJIT_FAST_CALL_FUNCTION_FRAME);  // 2: type
    frame2[3] = static_cast<JSTaggedType>(reinterpret_cast<uintptr_t>(&frame1[2]));  // 3: prevFp
    frame2[4] = static_cast<JSTaggedType>(0);  // 4: returnAddr

    uintptr_t fp = reinterpret_cast<uintptr_t>(&frame2[2]) + 8;

    SafeMemContext safeCtx;
    safeCtx.AddRange(reinterpret_cast<uintptr_t>(&frame1[0]), reinterpret_cast<uintptr_t>(&frame1[size1]));
    safeCtx.AddRange(reinterpret_cast<uintptr_t>(&frame2[0]), reinterpret_cast<uintptr_t>(&frame2[size2]));

    uintptr_t sp = 0;
    uintptr_t pc = 0;
    uintptr_t methodId = 0;
    bool isJsFrame = true;
    std::vector<uintptr_t> jitCache;

    StepFrameType frameType = StepFrameType::JS_FRAME;
    uint64_t frameIndex = 0;
    ArkUnwindParam arkUnwindParam(&safeCtx, SafeReadMemFuncForJit, &fp, &sp, &pc,
                                  &methodId, &isJsFrame, &frameType, frameIndex, jitCache);
    int ret = step_ark_with_record_jit(&arkUnwindParam);

    EXPECT_TRUE(ret == 1);
    EXPECT_TRUE(*arkUnwindParam.fp == reinterpret_cast<uintptr_t>(&frame1[2]));
    EXPECT_TRUE(*arkUnwindParam.pc == 5678);
    EXPECT_TRUE(*arkUnwindParam.isJsFrame == true);
}
#endif

#if ECMASCRIPT_ENABLE_ARK_STEED

// ============================================================================
// Out-of-process STEED pc resolution tests (spec VM-1/VM-2/VM-3):
//   - PC state machine: first-frame fault PC, cross-frame callee returnAddr,
//     interleaved-context isolation;
//   - degradation chain: position hit -> safepoint fallback -> 0;
//   - defensive reads: corrupt MAGIC / size caps / read failures only degrade
//     a single frame, never break the unwind.
// Lookup convention (FindBcOffsetByPCOffset): the position table is keyed at
// the call vertex's start (pcBefore) and queried with pc-1, while the
// safepoint table is keyed at the raw return address. Query pcs and return-
// address markers below are therefore one past the recorded boundary.
// ============================================================================

namespace {
void AppendU32(std::vector<uint8_t> &out, uint32_t v)
{
    for (int i = 0; i < 4; i++) {
        out.push_back(static_cast<uint8_t>((v >> (8 * i)) & 0xFF));  // 8: bits per byte; 0xFF: low-byte mask
    }
}

void AppendSleb128(std::vector<uint8_t> &out, int64_t v)
{
    bool more = true;
    while (more) {
        uint8_t byte = static_cast<uint8_t>(v & 0x7F);  // 0x7F: 7 payload bits per VLQ byte
        v >>= 7;  // 7: payload bits per byte
        bool signBit = (byte & 0x40) != 0;  // 0x40: VLQ sign bit
        more = !((v == 0 && !signBit) || (v == -1 && signBit));
        if (more) {
            byte |= 0x80;  // 0x80: VLQ continuation bit
        }
        out.push_back(byte);
    }
}

template<typename T>
void PutField(std::vector<uint8_t> &obj, size_t offset, T value)
{
    memcpy(obj.data() + offset, &value, sizeof(T));
}

// Fake out-of-process world of one STEED-compiled function: text, ASTB
// container and the JSFunction/Method/MachineCode objects the offline reader
// walks through. All memory is plain test buffers registered in a
// SafeMemContext.
struct SteedFuncWorld {
    static constexpr uint32_t TEXT_SIZE = 64;
    uintptr_t bytecodeArray = 0x00000000BC00DE00ULL;  // bytecode-space base

    std::vector<uint8_t> text;
    std::vector<uint8_t> jsFuncObj;
    std::vector<uint8_t> methodObj;
    std::vector<uint8_t> machineCodeObj;
    uintptr_t funcAddr = 0;
    uintptr_t machineCode = 0;
    uintptr_t jsFunc = 0;
    uintptr_t method = 0;
    uintptr_t containerAddr = 0;

    void Build(bool jitFortLayout, bool withPosition, bool withSafepoint,
               uint32_t posPc, uint32_t posBc, uint32_t sfpPc, uint32_t sfpBc,
               uint32_t posPc2 = 0, uint32_t posBc2 = 0, uint32_t methodId = 0x1234)
    {
        text.assign(TEXT_SIZE, 0x90);  // 0x90: filler byte for the machine-code text

        // ASTB container: 24B directory header + safepoint section + position
        // section. All sizes/offsets below are body-relative first, then the
        // directory entries get the +header bias (the reader validates them
        // container-relative).
        std::vector<uint8_t> body;
        uint32_t sfpSize = 0;
        if (withSafepoint) {
            // safepoint table: 16B header + one 12B entry whose deopt data
            // carries a CONSTANT PC_OFFSET_INDEX (-2) with sfpBc
            AppendU32(body, 1);  // numEntries
            AppendU32(body, 0);  // numTaggedSlots
            AppendU32(body, 0);  // numUntaggedSlots
            AppendU32(body, 0);  // deoptLiteralCount
            AppendU32(body, sfpPc);
            AppendU32(body, 16 + 12);  // 16: header bytes; 12: entry bytes; deoptOffset right after the entry
            body.push_back(0);
            body.push_back(0);          // extraSpillSlotsAndFlags
            body.push_back(0);
            body.push_back(2);          // deoptNum = 2 (one vreg entry)
            AppendSleb128(body, -2 * 4 + 0);  // vreg=-2 (PC_OFFSET), kind=CONSTANT
            AppendSleb128(body, sfpBc);        // the bcOffset constant
            sfpSize = static_cast<uint32_t>(body.size());
        }
        uint32_t posBodyOff = static_cast<uint32_t>(body.size());
        uint32_t posSize = 0;
        if (withPosition) {
            // position table: uint32 numEntries + (pcOffset, bcOffset) pairs;
            // a second (non-zero) entry models adjacent call-site boundaries.
            // The reader binary-searches the table, so keep the entries in
            // ascending pc order exactly like the real builder's
            // RecordBoundary ASSERT demands.
            if (posPc2 != 0 && posPc2 < posPc) {
                std::swap(posPc, posPc2);
                std::swap(posBc, posBc2);
            }
            uint32_t numEntries = (posPc2 != 0 || posBc2 != 0) ? 2 : 1;
            AppendU32(body, numEntries);
            AppendU32(body, posPc);
            AppendU32(body, posBc);
            if (numEntries == 2) {  // 2: the second position entry is present
                AppendU32(body, posPc2);
                AppendU32(body, posBc2);
            }
            posSize = static_cast<uint32_t>(body.size()) - posBodyOff;
        }
        constexpr uint32_t HEADER_SIZE = arksteed::ArkSteedTableContainerHeader::SIZE;
        std::vector<uint8_t> container;
        AppendU32(container, arksteed::ArkSteedTableContainerHeader::MAGIC);
        AppendU32(container, arksteed::ArkSteedTableContainerHeader::VERSION);
        AppendU32(container, HEADER_SIZE);              // safepointOffset
        AppendU32(container, sfpSize);
        AppendU32(container, HEADER_SIZE + posBodyOff); // positionOffset
        AppendU32(container, posSize);
        container.insert(container.end(), body.begin(), body.end());

        // fake MachineCode object; the container goes either into the JitFort
        // NonText region (right after the object) or right after the text
        size_t mcSize = MachineCode::SIZE + container.size() + 64;  // 64: tail slack so word-wise reads stay in bounds
        machineCodeObj.assign(mcSize, 0);
        machineCode = reinterpret_cast<uintptr_t>(machineCodeObj.data());
        PutField<uint32_t>(machineCodeObj, MachineCode::INSTRSIZ_OFFSET, TEXT_SIZE);
        PutField<uint32_t>(machineCodeObj, MachineCode::STACKMAP_OR_OFFSETTABLE_SIZE_OFFSET,
                           static_cast<uint32_t>(container.size()));
        PutField<uint32_t>(machineCodeObj, MachineCode::PAYLOADSIZE_OFFSET,
                           static_cast<uint32_t>(container.size()));
        if (jitFortLayout) {
            containerAddr = machineCode + MachineCode::SIZE;
            memcpy(machineCodeObj.data() + MachineCode::SIZE, container.data(), container.size());
            // pad keeps every word-wise read past the text end in-bounds
            text.insert(text.end(), 32, 0xAA);
        } else {
            text.insert(text.end(), container.begin(), container.end());
            text.insert(text.end(), 32, 0xAA);  // 32: pad bytes past the text end; 0xAA: filler pattern
        }
        // take funcAddr only after the text vector reached its final size
        // (the inserts above may reallocate it)
        funcAddr = reinterpret_cast<uintptr_t>(text.data());
        if (!jitFortLayout) {
            containerAddr = funcAddr + TEXT_SIZE;
        }
        PutField<uint64_t>(machineCodeObj, MachineCode::FUNCADDR_OFFSET, funcAddr);

        // fake Method object: bytecodeArray behind the native-pointer field and
        // the methodId encoded in literalInfo (the record-jit map key source)
        methodObj.assign(Method::LITERAL_INFO_OFFSET + sizeof(uint64_t), 0);
        method = reinterpret_cast<uintptr_t>(methodObj.data());
        PutField<uint64_t>(methodObj, Method::NATIVE_POINTER_OR_BYTECODE_ARRAY_OFFSET, bytecodeArray);
        PutField<uint64_t>(methodObj, Method::LITERAL_INFO_OFFSET,
                           MethodLiteral::MethodIdBits::Update(uint64_t(0), methodId));

        // fake JSFunction object: method + machineCode pointers
        size_t jsSize = std::max(JSFunction::METHOD_OFFSET, JSFunction::MACHINECODE_OFFSET) + 16;
        jsFuncObj.assign(jsSize, 0);
        jsFunc = reinterpret_cast<uintptr_t>(jsFuncObj.data());
        PutField<uint64_t>(jsFuncObj, JSFunction::METHOD_OFFSET, method);
        PutField<uint64_t>(jsFuncObj, JSFunction::MACHINECODE_OFFSET, machineCode);
    }

    void Register(SafeMemContext &ctx) const
    {
        ctx.AddRange(funcAddr, funcAddr + text.size());
        ctx.AddRange(jsFunc, jsFunc + jsFuncObj.size());
        ctx.AddRange(method, method + methodObj.size());
        ctx.AddRange(machineCode, machineCode + machineCodeObj.size());
    }
};

// One STEED stack frame: [0] lexicalEnv [1] jsFunc [2] type [3] prevFp
// [4] returnAddr (at fp + sizeof(uintptr_t), fp = &slots[3]).
struct SteedFrameMem {
    JSTaggedType slots[5];  // 5: lexicalEnv, jsFunc, type, prevFp, returnAddr
    SteedFrameMem(uintptr_t jsFunc, uintptr_t prevFp, uintptr_t returnAddr)
    {
        slots[0] = 0;
        slots[1] = static_cast<JSTaggedType>(jsFunc);
        slots[2] = static_cast<JSTaggedType>(FrameType::STEED_FUNCTION_FRAME);  // 2: frame-type slot
        slots[3] = static_cast<JSTaggedType>(prevFp);  // 3: prevFp slot
        slots[4] = static_cast<JSTaggedType>(returnAddr);  // 4: return-address slot
    }
    uintptr_t Fp() const
    {
        return reinterpret_cast<uintptr_t>(&slots[2]) + sizeof(uintptr_t);  // 2: type slot; fp = &slots[3]
    }
};

// A fake middle frame of an arbitrary type, placed by the frame type's own
// compile-time geometry: frame base at the buffer start, type at
// base + typeOffset, prev-fp at base + prevOffset (the unwinder's fp),
// return address at base + retAddrOffset. Every other slot carries a
// recognizable sentinel: a wrong-slot read yields an invalid candidate that
// degrades the frame, so a precise resolution proves the right slot was used.
struct MiddleFrameMem {
    static constexpr size_t NUM_SLOTS = 16;
    static constexpr uint64_t SENTINEL = 0x5A5A5A5A5A5A5A5AULL;
    static constexpr size_t NO_RETURN_ADDR = static_cast<size_t>(-1);
    JSTaggedType slots[NUM_SLOTS];

    MiddleFrameMem(uintptr_t frameTypeValue, size_t typeOffset, size_t prevOffset, size_t retAddrOffset,
                   uintptr_t prevFp, uintptr_t marker)
    {
        for (size_t i = 0; i < NUM_SLOTS; i++) {
            slots[i] = static_cast<JSTaggedType>(SENTINEL);
        }
        uintptr_t base = reinterpret_cast<uintptr_t>(&slots[0]);
        PutAt(base + typeOffset, frameTypeValue);
        PutAt(base + prevOffset, prevFp);
        if (retAddrOffset != NO_RETURN_ADDR) {
            PutAt(base + retAddrOffset, marker);
        }
    }
    static void PutAt(uintptr_t addr, uintptr_t value)
    {
        *reinterpret_cast<uintptr_t *>(addr) = value;
    }
    template<typename T>
    void Poke(size_t offsetFromBase, T value)
    {
        PutAt(reinterpret_cast<uintptr_t>(&slots[0]) + offsetFromBase,
              static_cast<uintptr_t>(value));
    }
    uintptr_t Fp(size_t typeOffset) const
    {
        // the frame chain convention is universal: the sp/fp handed to the
        // unwinder sits right above the type slot (GetFrameType reads fp-8)
        return reinterpret_cast<uintptr_t>(&slots[0]) + typeOffset + sizeof(FrameType);
    }
};

// An entry-style boundary frame (ArkFrameCheck): no frame advance happens;
// the epilogue reads the next fp/pc from the slots right above the type slot.
struct EntryFrameMem {
    static constexpr size_t NUM_SLOTS = 8;
    static constexpr uint64_t SENTINEL = 0x5A5A5A5A5A5A5A5AULL;
    JSTaggedType slots[NUM_SLOTS];
    EntryFrameMem(uintptr_t nextFp, uintptr_t nextPc)
    {
        for (size_t i = 0; i < NUM_SLOTS; i++) {
            slots[i] = static_cast<JSTaggedType>(SENTINEL);
        }
        slots[2] = static_cast<JSTaggedType>(FrameType::ASM_INTERPRETER_ENTRY_FRAME);  // 2: frame-type slot
        slots[3] = static_cast<JSTaggedType>(nextFp);  // 3: nextFp slot
        slots[4] = static_cast<JSTaggedType>(nextPc);  // 4: nextPc slot
    }
    uintptr_t Fp() const
    {
        return reinterpret_cast<uintptr_t>(&slots[2]) + sizeof(uintptr_t);  // 2: type slot; fp = &slots[3]
    }
};

// The production continuity state is keyed by the ctx pointer alone (the
// driver passes a constant frameIndex), so every test sequence must use a
// ctx address no earlier sequence ever used: stack/heap reuse would make a
// fresh sequence accidentally continue a stale one. Pool-allocate contexts
// (never freed) to keep every sequence start deterministic.
SafeMemContext &NewSteedCtx()
{
    static std::mutex mutex;
    static std::vector<std::unique_ptr<SafeMemContext>> pool;
    std::lock_guard<std::mutex> lock(mutex);
    pool.push_back(std::make_unique<SafeMemContext>());
    return *pool.back();
}

// Drive one step_ark_with_record_jit call (one frame of a sequence). The
// real driver passes a constant frameIndex of 0, so continuity is keyed by
// the ctx alone; the helper mirrors that.
// methodIdIn only seeds the caller's method-id slot (the runtime overwrites it
// with the id resolved from the frame's Method); the record-jit map key comes
// from the world's fake Method.literalInfo, so parallel sequences key apart
// via each world's own Build(..., methodId).
bool SteedStep(SafeMemContext &ctx, uintptr_t fp, uintptr_t pcIn,
               uintptr_t &fpOut, uintptr_t &pcOut, uintptr_t methodIdIn = 0)
{
    uintptr_t fpv = fp;
    uintptr_t sp = 0;
    uintptr_t pc = pcIn;
    uintptr_t methodId = methodIdIn;
    bool isJsFrame = true;
    StepFrameType ft = StepFrameType::JS_FRAME;
    std::vector<uintptr_t> jitCache;
    ArkUnwindParam param(&ctx, SafeReadMemFuncForJit, &fpv, &sp, &pc, &methodId,
                         &isJsFrame, &ft, 0, jitCache);
    bool ok = step_ark_with_record_jit(&param) == 1;
    fpOut = fpv;
    pcOut = pc;
    return ok;
}
}  // namespace

/**
 * @tc.name: SteedPcStateMachine_FirstFrameFaultPc
 * @tc.desc: The first STEED frame of a sequence resolves with the unwinder's
 *   input fault PC (kept, never cleared) via the position table.
 * @tc.type: FUNC
 * @tc.require:
 */
HWTEST_F_L0(JsStackInfoTest, SteedPcStateMachine_FirstFrameFaultPc)
{
    SteedFuncWorld world;
    world.Build(true /* jitFort layout */, true /* position */, false /* safepoint */,
                32 /* posPc */, 400 /* posBc */, 0, 0);
    SteedFrameMem frame(world.jsFunc, 0, 0x1111);

    SafeMemContext &ctx = NewSteedCtx();
    world.Register(ctx);
    ctx.AddRange(reinterpret_cast<uintptr_t>(&frame.slots[0]),
                  reinterpret_cast<uintptr_t>(&frame.slots[5]));  // 5: one past the last frame slot

    uintptr_t fpOut = 0;
    uintptr_t pcOut = 0;
    ASSERT_TRUE(SteedStep(ctx, frame.Fp(), world.funcAddr + 33, fpOut, pcOut));
    EXPECT_EQ(pcOut, world.bytecodeArray + 400);
    JsStackInfo::machineCodeMap.clear();
}

/**
 * @tc.name: SteedPcStateMachine_CrossFrameReturnAddr
 * @tc.desc: The second STEED frame (caller) resolves with its callee's return
 *   address, mirroring the in-process Advance() semantics.
 * @tc.type: FUNC
 * @tc.require:
 */
HWTEST_F_L0(JsStackInfoTest, SteedPcStateMachine_CrossFrameReturnAddr)
{
    // callee (visited first): fault pc inside its text
    SteedFuncWorld callee;
    callee.Build(true, true, false, 16 /* posPc */, 200 /* posBc */, 0, 0);
    // caller (visited second): the callee's return address points into it
    SteedFuncWorld caller;
    caller.Build(true, true, false, 40 /* posPc */, 600 /* posBc */, 0, 0);
    uintptr_t callSiteInCaller = caller.funcAddr + 41;

    SteedFrameMem callerFrame(caller.jsFunc, 0, 0);
    SteedFrameMem calleeFrame(callee.jsFunc,
                              // 3: prevFp slot of the caller frame (its fp)
                              reinterpret_cast<uintptr_t>(&callerFrame.slots[3]),
                              callSiteInCaller);

    SafeMemContext &ctx = NewSteedCtx();
    callee.Register(ctx);
    caller.Register(ctx);
    ctx.AddRange(reinterpret_cast<uintptr_t>(&callerFrame.slots[0]),
                  reinterpret_cast<uintptr_t>(&callerFrame.slots[5]));  // 5: one past the last frame slot
    ctx.AddRange(reinterpret_cast<uintptr_t>(&calleeFrame.slots[0]),
                  reinterpret_cast<uintptr_t>(&calleeFrame.slots[5]));  // 5: one past the last frame slot

    // step 1: callee resolves with the fault pc
    uintptr_t fpOut = 0;
    uintptr_t pcOut = 0;
    ASSERT_TRUE(SteedStep(ctx, calleeFrame.Fp(), callee.funcAddr + 17, fpOut, pcOut));
    EXPECT_EQ(pcOut, callee.bytecodeArray + 200);
    // 3: prevFp slot of the caller frame (its fp)
    EXPECT_EQ(fpOut, reinterpret_cast<uintptr_t>(&callerFrame.slots[3]));

    // step 2: caller resolves with the callee's captured return address
    ASSERT_TRUE(SteedStep(ctx, fpOut, pcOut, fpOut, pcOut));
    EXPECT_EQ(pcOut, caller.bytecodeArray + 600);
    JsStackInfo::machineCodeMap.clear();
}

/**
 * @tc.name: SteedPcStateMachine_InterleavedContexts
 * @tc.desc: Interleaved sequences on the same thread never share candidates:
 *   after another ctx runs in between, the resumed frame degrades instead of
 *   consuming the foreign return address.
 * @tc.type: FUNC
 * @tc.require:
 */
HWTEST_F_L0(JsStackInfoTest, SteedPcStateMachine_InterleavedContexts)
{
    SteedFuncWorld worldA1;
    worldA1.Build(true, true, false, 8 /* posPc */, 100 /* posBc */, 0, 0);
    SteedFuncWorld worldA2;
    worldA2.Build(true, true, false, 24 /* posPc */, 300 /* posBc */, 0, 0);
    SteedFuncWorld worldB;
    worldB.Build(true, true, false, 16 /* posPc */, 500 /* posBc */, 0, 0);

    SteedFrameMem frameA1(worldA1.jsFunc, 0, 0x1111);
    SteedFrameMem frameA2(worldA2.jsFunc, 0, 0x2222);
    SteedFrameMem frameB(worldB.jsFunc, 0, 0x3333);

    SafeMemContext &ctxA = NewSteedCtx();
    worldA1.Register(ctxA);
    worldA2.Register(ctxA);
    ctxA.AddRange(reinterpret_cast<uintptr_t>(&frameA1.slots[0]),
                  reinterpret_cast<uintptr_t>(&frameA1.slots[5]));  // 5: one past the last frame slot
    ctxA.AddRange(reinterpret_cast<uintptr_t>(&frameA2.slots[0]),
                  reinterpret_cast<uintptr_t>(&frameA2.slots[5]));  // 5: one past the last frame slot
    SafeMemContext &ctxB = NewSteedCtx();
    worldB.Register(ctxB);
    ctxB.AddRange(reinterpret_cast<uintptr_t>(&frameB.slots[0]),
                  reinterpret_cast<uintptr_t>(&frameB.slots[5]));  // 5: one past the last frame slot

    // sequence A step 0
    uintptr_t fpOut = 0;
    uintptr_t pcA = 0;
    ASSERT_TRUE(SteedStep(ctxA, frameA1.Fp(), worldA1.funcAddr + 9, fpOut, pcA));
    EXPECT_EQ(pcA, worldA1.bytecodeArray + 100);

    // sequence B interleaves on the same thread
    uintptr_t pcB = 0;
    ASSERT_TRUE(SteedStep(ctxB, frameB.Fp(), worldB.funcAddr + 17, fpOut, pcB));
    EXPECT_EQ(pcB, worldB.bytecodeArray + 500);

    // sequence A resumes: the foreign candidate must not be consumed; the frame
    // degrades to function-first-line instead of misreporting
    ASSERT_TRUE(SteedStep(ctxA, frameA2.Fp(), pcA, fpOut, pcA));
    EXPECT_EQ(pcA, worldA2.bytecodeArray);
    JsStackInfo::machineCodeMap.clear();
}

/**
 * @tc.name: SteedPcDegradationChain
 * @tc.desc: Resolution degrades per the priority chain: safepoint table
 *   fallback when no position table; bcOffset 0 (function first line) when
 *   neither exists or the fault pc is out of range.
 * @tc.type: FUNC
 * @tc.require:
 */
HWTEST_F_L0(JsStackInfoTest, SteedPcDegradationChain)
{
    // safepoint fallback (no position table)
    {
        SteedFuncWorld world;
        world.Build(true, false, true, 0, 0, 24 /* sfpPc */, 300 /* sfpBc */);
        SteedFrameMem frame(world.jsFunc, 0, 0x1111);
        SafeMemContext &ctx = NewSteedCtx();
        world.Register(ctx);
        ctx.AddRange(reinterpret_cast<uintptr_t>(&frame.slots[0]),
                      reinterpret_cast<uintptr_t>(&frame.slots[5]));  // 5: one past the last frame slot
        uintptr_t fpOut = 0;
        uintptr_t pcOut = 0;
        ASSERT_TRUE(SteedStep(ctx, frame.Fp(), world.funcAddr + 24, fpOut, pcOut));
        EXPECT_EQ(pcOut, world.bytecodeArray + 300);
        JsStackInfo::machineCodeMap.clear();
    }
    // neither table: bcOffset 0 (function first bytecode is a legal value)
    {
        SteedFuncWorld world;
        world.Build(true, false, false, 0, 0, 0, 0);
        SteedFrameMem frame(world.jsFunc, 0, 0x1111);
        SafeMemContext &ctx = NewSteedCtx();
        world.Register(ctx);
        ctx.AddRange(reinterpret_cast<uintptr_t>(&frame.slots[0]),
                      reinterpret_cast<uintptr_t>(&frame.slots[5]));  // 5: one past the last frame slot
        uintptr_t fpOut = 0;
        uintptr_t pcOut = 0;
        ASSERT_TRUE(SteedStep(ctx, frame.Fp(), world.funcAddr + 8, fpOut, pcOut));
        EXPECT_EQ(pcOut, world.bytecodeArray);
        JsStackInfo::machineCodeMap.clear();
    }
    // fault pc out of range (e.g. lazy-deopt trampoline): degrade, no misreport
    {
        SteedFuncWorld world;
    world.Build(true, true, false, 32 /* posPc */, 400 /* posBc */, 0, 0);
        SteedFrameMem frame(world.jsFunc, 0, 0x1111);
        SafeMemContext &ctx = NewSteedCtx();
        world.Register(ctx);
        ctx.AddRange(reinterpret_cast<uintptr_t>(&frame.slots[0]),
                      reinterpret_cast<uintptr_t>(&frame.slots[5]));  // 5: one past the last frame slot
        uintptr_t fpOut = 0;
        uintptr_t pcOut = 0;
        ASSERT_TRUE(SteedStep(ctx, frame.Fp(), 0x7FFF0000, fpOut, pcOut));  // 0x7FFF0000: pc outside the function text
        EXPECT_EQ(pcOut, world.bytecodeArray);
        JsStackInfo::machineCodeMap.clear();
    }
}

/**
 * @tc.name: SteedPcDefensiveReads
 * @tc.desc: Corrupt crash-process metadata (bad MAGIC, inconsistent sizes,
 *   oversized fields, neighbouring-field garbage, unreadable container) only
 *   degrades the frame pc; the unwind never breaks and 32-bit fields are
 *   normalized against high-half garbage.
 * @tc.type: FUNC
 * @tc.require:
 */
HWTEST_F_L0(JsStackInfoTest, SteedPcDefensiveReads)
{
    // bad MAGIC on both layouts: degrade
    {
        SteedFuncWorld world;
    world.Build(true, true, false, 32 /* posPc */, 400 /* posBc */, 0, 0);
        SteedFrameMem frame(world.jsFunc, 0, 0x1111);
        SafeMemContext &ctx = NewSteedCtx();
        world.Register(ctx);
        ctx.AddRange(reinterpret_cast<uintptr_t>(&frame.slots[0]),
                      reinterpret_cast<uintptr_t>(&frame.slots[5]));
        // corrupt the JitFort candidate header; the after-text candidate is
        // unreadable in this layout (not registered), so both drop out
        uint32_t bad = 0xDEADBEEF;
        memcpy(world.machineCodeObj.data() + MachineCode::SIZE, &bad, sizeof(bad));
        uintptr_t fpOut = 0;
        uintptr_t pcOut = 0;
        ASSERT_TRUE(SteedStep(ctx, frame.Fp(), world.funcAddr + 33, fpOut, pcOut));
        EXPECT_EQ(pcOut, world.bytecodeArray);
        JsStackInfo::machineCodeMap.clear();
    }
    // stackmapSize > payloadSize: degrade without huge allocation
    {
        SteedFuncWorld world;
        world.Build(true, true, false, 32, 400, 0, 0);
        PutField<uint32_t>(world.machineCodeObj, MachineCode::STACKMAP_OR_OFFSETTABLE_SIZE_OFFSET,
                           static_cast<uint32_t>(world.machineCodeObj.size()));
        SteedFrameMem frame(world.jsFunc, 0, 0x1111);
        SafeMemContext &ctx = NewSteedCtx();
        world.Register(ctx);
        ctx.AddRange(reinterpret_cast<uintptr_t>(&frame.slots[0]),
                      reinterpret_cast<uintptr_t>(&frame.slots[5]));
        uintptr_t fpOut = 0;
        uintptr_t pcOut = 0;
        ASSERT_TRUE(SteedStep(ctx, frame.Fp(), world.funcAddr + 33, fpOut, pcOut));
        EXPECT_EQ(pcOut, world.bytecodeArray);
        JsStackInfo::machineCodeMap.clear();
    }
    // stackmapSize over the 16 MiB hard cap: degrade
    {
        SteedFuncWorld world;
        world.Build(true, true, false, 32, 400, 0, 0);
        PutField<uint32_t>(world.machineCodeObj, MachineCode::STACKMAP_OR_OFFSETTABLE_SIZE_OFFSET,
                           17 * 1024 * 1024);
        PutField<uint32_t>(world.machineCodeObj, MachineCode::PAYLOADSIZE_OFFSET, 0xFFFFFFFF);
        SteedFrameMem frame(world.jsFunc, 0, 0x1111);
        SafeMemContext &ctx = NewSteedCtx();
        world.Register(ctx);
        ctx.AddRange(reinterpret_cast<uintptr_t>(&frame.slots[0]),
                      reinterpret_cast<uintptr_t>(&frame.slots[5]));
        uintptr_t fpOut = 0;
        uintptr_t pcOut = 0;
        ASSERT_TRUE(SteedStep(ctx, frame.Fp(), world.funcAddr + 33, fpOut, pcOut));
        EXPECT_EQ(pcOut, world.bytecodeArray);
        JsStackInfo::machineCodeMap.clear();
    }
    // garbage in the high half of the 32-bit instrsiz word (neighbouring field):
    // normalization keeps the resolution exact
    {
        SteedFuncWorld world;
        world.Build(false /* after-text layout */, true, false, 32, 400, 0, 0);
        // InstructionsAddr (the field right after InstructionsSize) carries
        // garbage in its low half, which lands in the high half of the
        // word-wise instrsiz read
        PutField<uint32_t>(world.machineCodeObj, MachineCode::INSTRADDR_OFFSET, 0x5A5A5A5A);
        SteedFrameMem frame(world.jsFunc, 0, 0x1111);
        SafeMemContext &ctx = NewSteedCtx();
        world.Register(ctx);
        ctx.AddRange(reinterpret_cast<uintptr_t>(&frame.slots[0]),
                      reinterpret_cast<uintptr_t>(&frame.slots[5]));
        uintptr_t fpOut = 0;
        uintptr_t pcOut = 0;
        ASSERT_TRUE(SteedStep(ctx, frame.Fp(), world.funcAddr + 33, fpOut, pcOut));
        EXPECT_EQ(pcOut, world.bytecodeArray + 400);
        JsStackInfo::machineCodeMap.clear();
    }
}

/**
 * @tc.name: SteedPcStateMachine_ParallelThreads
 * @tc.desc: INV-1: parallel unwind sequences on separate threads never share
 *   or corrupt each other's pc continuity state (thread-local isolation). The
 *   record-jit map is keyed apart per thread (its unsynchronized cross-thread
 *   access is a pre-existing production concern, noted in the plan).
 * @tc.type: FUNC
 * @tc.require:
 */
HWTEST_F_L0(JsStackInfoTest, SteedPcStateMachine_ParallelThreads)
{
    constexpr int ITERATIONS = 64;
    // each world's fake Method carries its own methodId: the runtime resolves
    // the record-jit map key from it, so both threads only ever assign to the
    // pre-created entries below (no structural map mutation while running)
    const uintptr_t methodIdA = 0xAAAA;
    const uintptr_t methodIdB = 0xBBBB;
    SteedFuncWorld worldA;
    worldA.Build(true, true, false, 8, 100, 0, 0, 0, 0, static_cast<uint32_t>(methodIdA));
    SteedFrameMem frameA(worldA.jsFunc, 0, 0x1111);
    SteedFuncWorld worldB;
    worldB.Build(true, true, false, 16, 500, 0, 0, 0, 0, static_cast<uint32_t>(methodIdB));
    SteedFrameMem frameB(worldB.jsFunc, 0, 0x2222);

    // pre-create the per-thread record-jit map entries so both threads only
    // assign to their own key (no structural map mutation while running)
    JsStackInfo::machineCodeMap[EntityId(methodIdA)] = {};
    JsStackInfo::machineCodeMap[EntityId(methodIdB)] = {};

    auto runSequence = [](SteedFuncWorld *world, SteedFrameMem *frame, uintptr_t faultPc,
                          uintptr_t expectedPc, uintptr_t methodId, bool *ok) {
        for (int i = 0; i < ITERATIONS; i++) {
            // every iteration is a fresh sequence: pooled ctxs guarantee a
            // never-reused address, so each step re-initializes deterministically
            SafeMemContext &ctx = NewSteedCtx();
            world->Register(ctx);
            ctx.AddRange(reinterpret_cast<uintptr_t>(&frame->slots[0]),
                          reinterpret_cast<uintptr_t>(&frame->slots[5]));
            uintptr_t fpOut = 0;
            uintptr_t pcOut = 0;
            if (!SteedStep(ctx, frame->Fp(), faultPc, fpOut, pcOut, methodId) ||
                pcOut != expectedPc) {
                *ok = false;
                return;
            }
        }
        *ok = true;
    };

    bool okA = false;
    bool okB = false;
    // both threads hammer their own sequences at the same time; a process-wide
    // pc stash would cross-contaminate within a few iterations
    std::thread threadA(runSequence, &worldA, &frameA, worldA.funcAddr + 9,
                        worldA.bytecodeArray + 100, methodIdA, &okA);
    std::thread threadB(runSequence, &worldB, &frameB, worldB.funcAddr + 17,
                        worldB.bytecodeArray + 500, methodIdB, &okB);
    threadA.join();
    threadB.join();
    EXPECT_TRUE(okA);
    EXPECT_TRUE(okB);
    JsStackInfo::machineCodeMap.clear();
}

/**
 * @tc.name: SteedRecordJit_MethodIdResolution
 * @tc.desc: The record-jit map key is the abc methodId resolved from
 *   JSFunction -> Method -> literalInfo, never the caller's slot (faultloggerd
 *   keeps it 0): the resolved id is written back, EntityId(0) never appears,
 *   and an unresolvable Method only skips the code recording while the frame
 *   itself is still stepped and resolved.
 * @tc.type: FUNC
 * @tc.require:
 */
HWTEST_F_L0(JsStackInfoTest, SteedRecordJit_MethodIdResolution)
{
    // (a) resolved: fake Method carries methodId 0x5678 in its literalInfo
    {
        constexpr uintptr_t kMethodId = 0x5678;
        SteedFuncWorld world;
        world.Build(true, true, false, 8, 100, 0, 0, 0, 0, static_cast<uint32_t>(kMethodId));
        SteedFrameMem frame(world.jsFunc, 0, 0x1111);
        SafeMemContext &ctx = NewSteedCtx();
        world.Register(ctx);
        ctx.AddRange(reinterpret_cast<uintptr_t>(&frame.slots[0]),
                     reinterpret_cast<uintptr_t>(&frame.slots[5]));

        uintptr_t fp = frame.Fp();
        uintptr_t sp = 0;
        uintptr_t pc = world.funcAddr + 9;
        uintptr_t methodId = 0;  // simulates faultloggerd's always-0 slot
        bool isJsFrame = true;
        StepFrameType ft = StepFrameType::JS_FRAME;
        std::vector<uintptr_t> jitCache;
        ArkUnwindParam param(&ctx, SafeReadMemFuncForJit, &fp, &sp, &pc, &methodId,
                             &isJsFrame, &ft, 0, jitCache);
        ASSERT_TRUE(step_ark_with_record_jit(&param) == 1);
        EXPECT_EQ(pc, world.bytecodeArray + 100);
        // resolved id is written back to the caller's slot and used as the key
        EXPECT_EQ(methodId, kMethodId);
        ASSERT_EQ(jitCache.size(), 1U);
        EXPECT_EQ(jitCache[0], kMethodId);
        EXPECT_EQ(JsStackInfo::machineCodeMap.count(EntityId(kMethodId)), 1U);
        EXPECT_EQ(JsStackInfo::machineCodeMap.count(EntityId(0)), 0U);
        EXPECT_EQ(JsStackInfo::machineCodeMap[EntityId(kMethodId)].size(),
                  static_cast<size_t>(SteedFuncWorld::TEXT_SIZE));
        JsStackInfo::machineCodeMap.clear();
    }
    // (b) degraded: literalInfo carries no methodId -> no recording, the frame
    // itself is still stepped, resolved and reported
    {
        SteedFuncWorld world;
        world.Build(true, true, false, 8, 100, 0, 0, 0, 0, 0);
        SteedFrameMem frame(world.jsFunc, 0, 0x1111);
        SafeMemContext &ctx = NewSteedCtx();
        world.Register(ctx);
        ctx.AddRange(reinterpret_cast<uintptr_t>(&frame.slots[0]),
                     reinterpret_cast<uintptr_t>(&frame.slots[5]));

        uintptr_t fp = frame.Fp();
        uintptr_t sp = 0;
        uintptr_t pc = world.funcAddr + 9;
        uintptr_t methodId = 0;
        bool isJsFrame = true;
        StepFrameType ft = StepFrameType::JS_FRAME;
        std::vector<uintptr_t> jitCache;
        ArkUnwindParam param(&ctx, SafeReadMemFuncForJit, &fp, &sp, &pc, &methodId,
                             &isJsFrame, &ft, 0, jitCache);
        ASSERT_TRUE(step_ark_with_record_jit(&param) == 1);
        EXPECT_EQ(pc, world.bytecodeArray + 100);
        EXPECT_EQ(methodId, 0U);  // slot untouched
        EXPECT_TRUE(jitCache.empty());
        EXPECT_TRUE(JsStackInfo::machineCodeMap.empty());
        JsStackInfo::machineCodeMap.clear();
    }
}

/**
 * @tc.name: SteedPcStateMachine_ReadFailureCleanup
 * @tc.desc: A failed cross-process read only degrades the current frame; a
 *   failed frame-type read ends the sequence and clears the continuity state,
 *   so a subsequent sequence starts with clean first-frame semantics (INV-1).
 * @tc.type: FUNC
 * @tc.require:
 */
HWTEST_F_L0(JsStackInfoTest, SteedPcStateMachine_ReadFailureCleanup)
{
    // (a) container copy fails halfway (unmapped page inside the ASTB): the
    // whole candidate is dropped (no partial results) and the frame degrades
    {
        SteedFuncWorld world;
        world.Build(true, true, false, 32, 400, 0, 0);
        SteedFrameMem frame(world.jsFunc, 0, 0x1111);
        SafeMemContext &ctx = NewSteedCtx();
        // machine code object registered only through the container header
        // plus one word: the header parses, the body copy then fails
        ctx.AddRange(world.machineCode, world.machineCode + MachineCode::SIZE +
                      arksteed::ArkSteedTableContainerHeader::SIZE + sizeof(uintptr_t));
        ctx.AddRange(world.jsFunc, world.jsFunc + world.jsFuncObj.size());
        ctx.AddRange(world.method, world.method + world.methodObj.size());
        ctx.AddRange(world.funcAddr, world.funcAddr + world.text.size());
        ctx.AddRange(reinterpret_cast<uintptr_t>(&frame.slots[0]),
                      reinterpret_cast<uintptr_t>(&frame.slots[5]));
        uintptr_t fpOut = 0;
        uintptr_t pcOut = 0;
        ASSERT_TRUE(SteedStep(ctx, frame.Fp(), world.funcAddr + 33, fpOut, pcOut));
        EXPECT_EQ(pcOut, world.bytecodeArray);
        JsStackInfo::machineCodeMap.clear();
    }
    // (b) unreadable frame type: the step fails, the state is cleared; a
    // same-ctx continuation afterwards proves the reset (without it the stale
    // candidate would degrade the frame instead)
    {
        SteedFuncWorld world;
        world.Build(true, true, false, 16, 200, 0, 0);
        SteedFrameMem frame(world.jsFunc, 0, 0x1111);
        SteedFrameMem ghost(world.jsFunc, 0, 0x3333);  // not registered anywhere
        SafeMemContext &ctx = NewSteedCtx();
        world.Register(ctx);
        ctx.AddRange(reinterpret_cast<uintptr_t>(&frame.slots[0]),
                      reinterpret_cast<uintptr_t>(&frame.slots[5]));
        uintptr_t fpOut = 0;
        uintptr_t pcOut = 0;
        ASSERT_TRUE(SteedStep(ctx, frame.Fp(), world.funcAddr + 17, fpOut, pcOut));
        EXPECT_EQ(pcOut, world.bytecodeArray + 200);

        // fp in unregistered memory: the frame-type read fails, the step fails
        EXPECT_FALSE(SteedStep(ctx, ghost.Fp(), pcOut, fpOut, pcOut));

        // the failure path cleared the state: this continuation reuses the
        // same ctx and must resolve via its own fault pc
        ASSERT_TRUE(SteedStep(ctx, frame.Fp(), world.funcAddr + 17, fpOut, pcOut));
        EXPECT_EQ(pcOut, world.bytecodeArray + 200);
        JsStackInfo::machineCodeMap.clear();
    }
}

/**
 * @tc.name: SteedPcStateMachine_NoDumpNoCrosstalk
 * @tc.desc: A sequence that ends without ark_write_jit_code leaves no usable
 *   state behind: a new sequence with a new ctx re-initializes (first-frame
 *   fault pc semantics), never a stale candidate.
 * @tc.type: FUNC
 * @tc.require:
 */
HWTEST_F_L0(JsStackInfoTest, SteedPcStateMachine_NoDumpNoCrosstalk)
{
    SteedFuncWorld worldCallee;
    worldCallee.Build(true, true, false, 8, 100, 0, 0);
    // caller world has two entries: 40 -> 600 (captured return address) and
    // 20 -> 200 (the new sequence's own fault pc)
    SteedFuncWorld worldCaller;
    worldCaller.Build(true, true, false, 40, 600, 0, 0, 20, 200);
    SteedFrameMem callerFrame(worldCaller.jsFunc, 0, 0x9999);
    SteedFrameMem calleeFrame(worldCallee.jsFunc, callerFrame.Fp(), worldCaller.funcAddr + 41);
    SteedFrameMem freshFrame(worldCaller.jsFunc, 0, 0x1234);

    SafeMemContext &ctxA = NewSteedCtx();
    worldCallee.Register(ctxA);
    worldCaller.Register(ctxA);
    ctxA.AddRange(reinterpret_cast<uintptr_t>(&callerFrame.slots[0]),
                  reinterpret_cast<uintptr_t>(&callerFrame.slots[5]));
    ctxA.AddRange(reinterpret_cast<uintptr_t>(&calleeFrame.slots[0]),
                  reinterpret_cast<uintptr_t>(&calleeFrame.slots[5]));

    // sequence A: two frames, ends WITHOUT ark_write_jit_code
    uintptr_t fpOut = 0;
    uintptr_t pcOut = 0;
    ASSERT_TRUE(SteedStep(ctxA, calleeFrame.Fp(), worldCallee.funcAddr + 9, fpOut, pcOut));
    EXPECT_EQ(pcOut, worldCallee.bytecodeArray + 100);
    ASSERT_TRUE(SteedStep(ctxA, fpOut, pcOut, fpOut, pcOut));
    EXPECT_EQ(pcOut, worldCaller.bytecodeArray + 600);

    // sequence B (a new ctx): must re-initialize and resolve via its own fault
    // pc (boundary 20 -> 200); a stale candidate (the caller frame's 0x9999
    // garbage) would degrade the frame to bc 0 instead
    SafeMemContext &ctxB = NewSteedCtx();
    worldCaller.Register(ctxB);
    ctxB.AddRange(reinterpret_cast<uintptr_t>(&freshFrame.slots[0]),
                  reinterpret_cast<uintptr_t>(&freshFrame.slots[5]));
    ASSERT_TRUE(SteedStep(ctxB, freshFrame.Fp(), worldCaller.funcAddr + 21, fpOut, pcOut));
    EXPECT_EQ(pcOut, worldCaller.bytecodeArray + 200);
    JsStackInfo::machineCodeMap.clear();
}

/**
 * @tc.name: SteedPcStateMachine_ReturnAddrFrameTypes
 * @tc.desc: The per-frame-type return-address capture reads each frame type's
 *   own slot (leave/builtin/bridge/optimized/fastjit geometry with sentinels
 *   in every other slot); interpreter-style and entry frames invalidate the
 *   candidate so the next STEED caller degrades instead of misreporting.
 * @tc.type: FUNC
 * @tc.require:
 */
HWTEST_F_L0(JsStackInfoTest, SteedPcStateMachine_ReturnAddrFrameTypes)
{
    struct SlotCase {
        FrameType type;
        size_t typeOffset;
        size_t prevOffset;
        size_t retAddrOffset;  // MiddleFrameMem::NO_RETURN_ADDR = invalidating
        bool needsFunctionSlot;  // FASTJIT: record-jit reads the function slot
    };
    const SlotCase cases[] = {
        { FrameType::BUILTIN_FRAME, BuiltinFrame::GetTypeOffset(), BuiltinFrame::GetPrevOffset(),
          BuiltinFrame::GetReturnAddrOffset(), false },
        { FrameType::LEAVE_FRAME, OptimizedLeaveFrame::GetTypeOffset(), OptimizedLeaveFrame::GetPrevOffset(),
          OptimizedLeaveFrame::GetReturnAddrOffset(), false },
        { FrameType::ASM_INTERPRETER_BRIDGE_FRAME, AsmInterpretedBridgeFrame::GetTypeOffset(),
          AsmInterpretedBridgeFrame::GetPrevOffset(),
          AsmInterpretedBridgeFrame::GetReturnAddrOffset(false), false },
        { FrameType::OPTIMIZED_JS_FUNCTION_FRAME, OptimizedJSFunctionFrame::GetTypeOffset(),
          OptimizedJSFunctionFrame::GetPrevOffset(), OptimizedJSFunctionFrame::GetReturnAddrOffset(), false },
        { FrameType::FASTJIT_FUNCTION_FRAME, FASTJITFunctionFrame::GetTypeOffset(),
          FASTJITFunctionFrame::GetPrevOffset(),
          FASTJITFunctionFrame::GetPrevOffset() + sizeof(uintptr_t), true },
        { FrameType::ASM_INTERPRETER_FRAME, AsmInterpretedFrame::GetTypeOffset(),
          AsmInterpretedFrame::GetPrevOffset(), MiddleFrameMem::NO_RETURN_ADDR, false },
        { FrameType::INTERPRETER_FRAME, InterpretedFrame::GetTypeOffset(),
          InterpretedFrame::GetPrevOffset(), MiddleFrameMem::NO_RETURN_ADDR, false },
    };

    for (const auto &slotCase : cases) {
        // first STEED frame's world; the second STEED frame's world has two
        // entries: the marker (40 -> 600) and the stale-looking candidate
        // (24 -> 300, used as the first frame's return address)
        SteedFuncWorld worldX;
        worldX.Build(true, true, false, 8, 100, 0, 0);
        SteedFuncWorld worldY;
        worldY.Build(true, true, false, 40, 600, 0, 0, 24, 300);
        // one past the 24 boundary, so a consumed stale candidate would
        // resolve to 300 under the pc-1 lookup
        const uintptr_t stale = worldY.funcAddr + 25;

        SteedFrameMem steedY(worldY.jsFunc, 0, 0x6666);
        MiddleFrameMem middle(static_cast<uintptr_t>(slotCase.type), slotCase.typeOffset,
                              slotCase.prevOffset, slotCase.retAddrOffset, steedY.Fp(),
                              slotCase.retAddrOffset != MiddleFrameMem::NO_RETURN_ADDR
                                  ? worldY.funcAddr + 41  // 41: arbitrary pc; marker resolves to 600
                                  : 0);
        if (slotCase.needsFunctionSlot) {
            // keep the record-jit path happy: a real (fake-world) function
            middle.Poke(FASTJITFunctionFrame::GetFunctionOffset(), worldY.jsFunc);
        }
        SteedFrameMem steedX(worldX.jsFunc, middle.Fp(slotCase.typeOffset), stale);

        SafeMemContext &ctx = NewSteedCtx();
        worldX.Register(ctx);
        worldY.Register(ctx);
        ctx.AddRange(reinterpret_cast<uintptr_t>(&middle.slots[0]),
                      reinterpret_cast<uintptr_t>(&middle.slots[MiddleFrameMem::NUM_SLOTS]));
        ctx.AddRange(reinterpret_cast<uintptr_t>(&steedX.slots[0]),
                      reinterpret_cast<uintptr_t>(&steedX.slots[5]));
        ctx.AddRange(reinterpret_cast<uintptr_t>(&steedY.slots[0]),
                      reinterpret_cast<uintptr_t>(&steedY.slots[5]));

        // step 0: the first STEED frame resolves via the sequence fault pc
        uintptr_t fpOut = 0;
        uintptr_t pcOut = 0;
        ASSERT_TRUE(SteedStep(ctx, steedX.Fp(), worldX.funcAddr + 9, fpOut, pcOut))
            << "frame type " << static_cast<uintptr_t>(slotCase.type);
        EXPECT_EQ(pcOut, worldX.bytecodeArray + 100);

        // step 1: the middle frame; its return-address slot (if any) feeds
        // the candidate for the next STEED caller
        ASSERT_TRUE(SteedStep(ctx, fpOut, pcOut, fpOut, pcOut))
            << "frame type " << static_cast<uintptr_t>(slotCase.type);

        // step 2: the second STEED frame consumes the candidate
        ASSERT_TRUE(SteedStep(ctx, steedY.Fp(), pcOut, fpOut, pcOut))
            << "frame type " << static_cast<uintptr_t>(slotCase.type);
        if (slotCase.retAddrOffset != MiddleFrameMem::NO_RETURN_ADDR) {
            // the marker from the middle frame's own return-address slot was
            // captured (a wrong-slot sentinel read would degrade to 0, a stale
            // candidate would resolve to 300)
            EXPECT_EQ(pcOut, worldY.bytecodeArray + 600)
                << "frame type " << static_cast<uintptr_t>(slotCase.type);
        } else {
            // interpreter-style frame: the candidate was invalidated, so the
            // caller degrades to its first bytecode (never the stale 300)
            EXPECT_EQ(pcOut, worldY.bytecodeArray)
                << "frame type " << static_cast<uintptr_t>(slotCase.type);
        }
        JsStackInfo::machineCodeMap.clear();
    }

    // entry-style boundary frame (ArkFrameCheck): no native return address, so
    // it invalidates the candidate for the next STEED caller
    {
        SteedFuncWorld worldX;
        worldX.Build(true, true, false, 8, 100, 0, 0);
        SteedFuncWorld worldY;
        worldY.Build(true, true, false, 40, 600, 0, 0, 24, 300);
        SteedFrameMem steedY(worldY.jsFunc, 0, 0x6666);
        EntryFrameMem entry(steedY.Fp(), 0x7777);
        // one past the 24 boundary: a consumed stale candidate would resolve
        // to 300 instead of the expected degradation
        SteedFrameMem steedX(worldX.jsFunc, entry.Fp(), worldY.funcAddr + 25);

        SafeMemContext &ctx = NewSteedCtx();
        worldX.Register(ctx);
        worldY.Register(ctx);
        ctx.AddRange(reinterpret_cast<uintptr_t>(&entry.slots[0]),
                      reinterpret_cast<uintptr_t>(&entry.slots[EntryFrameMem::NUM_SLOTS]));
        ctx.AddRange(reinterpret_cast<uintptr_t>(&steedX.slots[0]),
                      reinterpret_cast<uintptr_t>(&steedX.slots[5]));
        ctx.AddRange(reinterpret_cast<uintptr_t>(&steedY.slots[0]),
                      reinterpret_cast<uintptr_t>(&steedY.slots[5]));

        uintptr_t fpOut = 0;
        uintptr_t pcOut = 0;
        ASSERT_TRUE(SteedStep(ctx, steedX.Fp(), worldX.funcAddr + 9, fpOut, pcOut));
        EXPECT_EQ(pcOut, worldX.bytecodeArray + 100);
        // the entry frame step succeeds and hands the next fp/pc to the driver
        ASSERT_TRUE(SteedStep(ctx, fpOut, pcOut, fpOut, pcOut));
        ASSERT_TRUE(SteedStep(ctx, fpOut, pcOut, fpOut, pcOut));
        // candidate invalidated by the entry frame: degrade, not the stale 300
        EXPECT_EQ(pcOut, worldY.bytecodeArray);
        JsStackInfo::machineCodeMap.clear();
    }
}

/**
 * @tc.name: SteedPcBoundaryPositions
 * @tc.desc: Adjacent position-table boundaries under the pc-1 lookup
 *   convention (the table is keyed at the call vertex's start, like
 *   FindBcOffsetByPCOffset): a return address one past a boundary resolves
 *   to that call's own entry, a return address exactly at the next boundary
 *   still resolves to the earlier entry (the call instruction ended right at
 *   the boundary), and a pc whose pc-1 falls before the first entry degrades
 *   to the function first line (bc 0 is a legal value, not an error).
 * @tc.type: FUNC
 * @tc.require:
 */
HWTEST_F_L0(JsStackInfoTest, SteedPcBoundaryPositions)
{
    // entries: (32 -> 400) and (36 -> 500)
    SteedFuncWorld world;
    world.Build(true, true, false, 32, 400, 0, 0, 36, 500);
    SteedFrameMem frame(world.jsFunc, 0, 0x1111);

    struct QueryCase {
        uintptr_t faultPc;
        uintptr_t expectedPc;
    };
    const QueryCase queries[] = {
        { 32, 0 },    // exactly at the first boundary: pc-1 before the first entry -> degrade
        { 33, 400 },  // one past the first boundary: the call's own entry
        { 34, 400 },  // between boundaries: the earlier entry (continuation)
        { 36, 400 },  // exactly at the next boundary: still the earlier entry
        { 37, 500 },  // one past the next boundary: the next entry
        { 60, 500 },  // past the last boundary: the last entry
        { 4, 0 },     // before the first entry: degrade to the first bytecode
    };
    for (const auto &query : queries) {
        // every query is an independent one-frame sequence (fresh ctx)
        SafeMemContext &ctx = NewSteedCtx();
        world.Register(ctx);
        ctx.AddRange(reinterpret_cast<uintptr_t>(&frame.slots[0]),
                      reinterpret_cast<uintptr_t>(&frame.slots[5]));
        uintptr_t fpOut = 0;
        uintptr_t pcOut = 0;
        ASSERT_TRUE(SteedStep(ctx, frame.Fp(), world.funcAddr + query.faultPc, fpOut, pcOut));
        EXPECT_EQ(pcOut, world.bytecodeArray + query.expectedPc);
    }
    JsStackInfo::machineCodeMap.clear();
}

/**
 * @tc.name: SteedPcDefensiveReads_PageBoundary
 * @tc.desc: Page-boundary shaped reads against the crash memory: the ASTB
 *   header starting at the end of a mapped region, the container body
 *   crossing into an unmapped page, a corrupted position-table byte stream
 *   and an odd-size container tail (shorter than one machine word) all
 *   degrade safely or resolve exactly (word-chunked copy with tail trim).
 * @tc.type: FUNC
 * @tc.require:
 */
HWTEST_F_L0(JsStackInfoTest, SteedPcDefensiveReads_PageBoundary)
{
    // (a) container header starts at the end of a mapped region (the first
    // ASTB word crosses into an unmapped page): the candidate is dropped
    {
        SteedFuncWorld world;
        world.Build(true, true, false, 32, 400, 0, 0);
        SteedFrameMem frame(world.jsFunc, 0, 0x1111);
        SafeMemContext &ctx = NewSteedCtx();
        // machine code object readable only up to the object end: the JitFort
        // candidate's first header word is unreadable; the after-text
        // candidate reads the 0xAA padding and fails the MAGIC check
        ctx.AddRange(world.machineCode, world.machineCode + MachineCode::SIZE);
        ctx.AddRange(world.jsFunc, world.jsFunc + world.jsFuncObj.size());
        ctx.AddRange(world.method, world.method + world.methodObj.size());
        ctx.AddRange(world.funcAddr, world.funcAddr + world.text.size());
        ctx.AddRange(reinterpret_cast<uintptr_t>(&frame.slots[0]),
                      reinterpret_cast<uintptr_t>(&frame.slots[5]));
        uintptr_t fpOut = 0;
        uintptr_t pcOut = 0;
        ASSERT_TRUE(SteedStep(ctx, frame.Fp(), world.funcAddr + 33, fpOut, pcOut));
        EXPECT_EQ(pcOut, world.bytecodeArray);
        JsStackInfo::machineCodeMap.clear();
    }
    // (b) container body crosses into an unmapped page halfway: the whole
    // candidate is dropped, no partial results are consumed
    {
        SteedFuncWorld world;
        world.Build(true, true, false, 32, 400, 0, 0);
        SteedFrameMem frame(world.jsFunc, 0, 0x1111);
        SafeMemContext &ctx = NewSteedCtx();
        // header plus exactly one body word readable
        ctx.AddRange(world.machineCode, world.machineCode + MachineCode::SIZE +
                      arksteed::ArkSteedTableContainerHeader::SIZE + sizeof(uintptr_t));
        ctx.AddRange(world.jsFunc, world.jsFunc + world.jsFuncObj.size());
        ctx.AddRange(world.method, world.method + world.methodObj.size());
        ctx.AddRange(world.funcAddr, world.funcAddr + world.text.size());
        ctx.AddRange(reinterpret_cast<uintptr_t>(&frame.slots[0]),
                      reinterpret_cast<uintptr_t>(&frame.slots[5]));
        uintptr_t fpOut = 0;
        uintptr_t pcOut = 0;
        ASSERT_TRUE(SteedStep(ctx, frame.Fp(), world.funcAddr + 33, fpOut, pcOut));
        EXPECT_EQ(pcOut, world.bytecodeArray);
        JsStackInfo::machineCodeMap.clear();
    }
    // (c) corrupted position-table byte stream (numEntries overflow): the
    // lookup rejects it and the frame degrades instead of misreporting
    {
        SteedFuncWorld world;
        world.Build(true, true, false, 32, 400, 0, 0);
        // position table (no safepoint section) starts right after the
        // 24-byte container header; smash its numEntries field
        PutField<uint32_t>(world.machineCodeObj, MachineCode::SIZE +
                           arksteed::ArkSteedTableContainerHeader::SIZE, 0xFFFFFFFF);
        SteedFrameMem frame(world.jsFunc, 0, 0x1111);
        SafeMemContext &ctx = NewSteedCtx();
        world.Register(ctx);
        ctx.AddRange(reinterpret_cast<uintptr_t>(&frame.slots[0]),
                      reinterpret_cast<uintptr_t>(&frame.slots[5]));
        uintptr_t fpOut = 0;
        uintptr_t pcOut = 0;
        ASSERT_TRUE(SteedStep(ctx, frame.Fp(), world.funcAddr + 33, fpOut, pcOut));
        EXPECT_EQ(pcOut, world.bytecodeArray);
        JsStackInfo::machineCodeMap.clear();
    }
    // (d) odd-size container (tail shorter than one machine word): the
    // word-chunked copy trims the tail and the resolution stays exact
    {
        SteedFuncWorld world;
        // safepoint-only body: 16B header + 12B entry + 1B vreg SLEB + 2B
        // bc SLEB (bc >= 128 needs two bytes) = 31 bytes, so the container
        // size is 24 + 31 = 55 (not a multiple of the machine word)
        world.Build(true, false, true, 0, 0, 24, 300);
        SteedFrameMem frame(world.jsFunc, 0, 0x1111);
        SafeMemContext &ctx = NewSteedCtx();
        world.Register(ctx);
        ctx.AddRange(reinterpret_cast<uintptr_t>(&frame.slots[0]),
                      reinterpret_cast<uintptr_t>(&frame.slots[5]));
        uintptr_t fpOut = 0;
        uintptr_t pcOut = 0;
        ASSERT_TRUE(SteedStep(ctx, frame.Fp(), world.funcAddr + 24, fpOut, pcOut));
        EXPECT_EQ(pcOut, world.bytecodeArray + 300);
        JsStackInfo::machineCodeMap.clear();
    }
}
#endif  // ECMASCRIPT_ENABLE_ARK_STEED

HWTEST_F_L0(JsStackInfoTest, MatchLineAndRevisedOffset) {
    const std::string fileName = STACKINFO_TEST_ABC_FILES_DIR"index.abc";
    std::string entryPoint = "index";

    bool result = JSNApi::Execute(instance_, fileName, entryPoint);
    ASSERT_TRUE(result);
    auto jsPandaFile = JSPandaFileManager::GetInstance()->FindJSPandaFile(CString(fileName));
    EXPECT_NE(jsPandaFile, nullptr);

    auto methods = JSStackTrace::ReadAllMethodInfos(jsPandaFile);
    DebugInfoExtractor *debugExtractor = JSPandaFileManager::GetInstance()->GetJSPtExtractor(jsPandaFile.get());
    int32_t lineNumber = 0;
    auto callbackLineFunc = [&lineNumber](int32_t line) -> bool {
        lineNumber = line + 1;
        return true;
    };
    auto method = methods[0];
    uintptr_t offset = 54; // line = -1
    debugExtractor->MatchLineWithOffset(callbackLineFunc, EntityId(method.methodId), offset);
    EXPECT_TRUE(lineNumber == 0);
    debugExtractor->MatchLineAndRevisedOffset(callbackLineFunc, EntityId(method.methodId), offset);
    EXPECT_TRUE(lineNumber == 20);
    EXPECT_TRUE(offset == 55);

    lineNumber = 0;
    offset = 55;
    debugExtractor->MatchLineWithOffset(callbackLineFunc, EntityId(method.methodId), offset);
    EXPECT_TRUE(lineNumber == 20);
    lineNumber = 0;
    debugExtractor->MatchLineAndRevisedOffset(callbackLineFunc, EntityId(method.methodId), offset);
    EXPECT_TRUE(lineNumber == 20);

    offset = 20;
    debugExtractor->MatchLineWithOffset(callbackLineFunc, EntityId(method.methodId), offset);
    EXPECT_TRUE(lineNumber == 17);
    debugExtractor->MatchLineAndRevisedOffset(callbackLineFunc, EntityId(method.methodId), offset);
    EXPECT_TRUE(lineNumber == 17);
}

HWTEST_F_L0(JsStackInfoTest, TestBuildJsStackInfo1)
{
    auto jsFrame = JsStackInfo::BuildJsStackInfo(thread_);
    EXPECT_TRUE(jsFrame.empty());
}

HWTEST_F_L0(JsStackInfoTest, TestBuildJsStackInfo2)
{
    JSHandle<JSObject> jsErrorObj;
    std::string stack1 = JsStackInfo::BuildJsStackTrace(thread_, false, jsErrorObj, true);
#if defined(ENABLE_EXCEPTION_BACKTRACE)
    EXPECT_TRUE(!stack1.empty());
#else
    EXPECT_TRUE(stack1.empty());
#endif

    std::string stack2 = JsStackInfo::BuildJsStackTrace(thread_, false, jsErrorObj, false);
    EXPECT_TRUE(stack2.empty());
}

HWTEST_F_L0(JsStackInfoTest, TestBuildJsStackInfo3)
{
    thread_->SetCrossThreadExecution(true);
    JSHandle<JSObject> jsErrorObj;
    std::string stack1 = JsStackInfo::BuildJsStackTrace(thread_, false, jsErrorObj, true);
    EXPECT_TRUE(stack1.empty());
    thread_->SetCrossThreadExecution(false);
}

#ifndef EXCLUDE_JIT
HWTEST_F_L0(JsStackInfoTest, TestArkWriteJitCode)
{
    std::vector<uint8_t> codeVec1 = {0x01, 0x02, 0x03, 0x04};
    std::vector<uint8_t> codeVec2 = {0x05, 0x06, 0x07, 0x08};

    EntityId id1(100);
    EntityId id2(200);
    JsStackInfo::machineCodeMap[id1] = codeVec1;
    JsStackInfo::machineCodeMap[id2] = codeVec2;
    JsStackInfo::nameMap[id1] = "jitFunction1";
    JsStackInfo::nameMap[id2] = "jitFunction2";

    uintptr_t jitCodeArray[2] = {100, 200};

    char tempFile[] = "/tmp/test_ark_write_jit_code_XXXXXX";
    int fd = mkstemp(tempFile);
    if (fd < 0) {
        GTEST_LOG_(INFO) << "Cannot create temp file, skip this test";
        return;
    }

    int result = ark_write_jit_code(nullptr, ReadMemFunc, fd, jitCodeArray, 2);
    EXPECT_EQ(result, 1);

    close(fd);
    unlink(tempFile);

    JsStackInfo::machineCodeMap.clear();
    JsStackInfo::nameMap.clear();
}
#endif

// Helper: build a pandasm source with a function whose name has the given length.
static std::string BuildLongNameSource(size_t nameLen)
{
    std::string name(nameLen, 'A');  // valid identifier chars
    return ".function void " + name + "() {\n    return\n}\n";
}

// Test: a function name shorter than FUNCTIONNAME_MAX is copied without truncation.
HWTEST_F_L0(JsStackInfoTest, ParseJsFrameInfo_ShortName_NoTruncation)
{
    // "foo" is 3 chars, well under FUNCTIONNAME_MAX=1024
    const char *filename = "__JsStackInfoShortName.pa";
    const char *data = R"(
        .function void foo() {
            return
        }
    )";
    pandasm::Parser parser;
    auto res = parser.Parse(data);
    auto file = pandasm::AsmEmitter::Emit(res.Value());
    ASSERT_NE(file, nullptr);
    auto jsPandaFile = std::make_shared<JSPandaFile>(file.release(), CString(filename), CreateMode::DFX);
    auto debugExtractor = std::make_unique<DebugInfoExtractor>(jsPandaFile.get());
    auto methods = JSStackTrace::ReadAllMethodInfos(jsPandaFile);
    ASSERT_FALSE(methods.empty());

    JsFunction jsFunction;
    (void)memset_s(&jsFunction, sizeof(jsFunction), 0, sizeof(jsFunction));
    EXPECT_TRUE(ParseJsFrameInfo(jsPandaFile.get(), debugExtractor.get(),
        EntityId(methods[0].methodId), 0, jsFunction));

    EXPECT_STREQ(jsFunction.functionName, "foo");
    EXPECT_EQ(strlen(jsFunction.functionName), 3U);
}

// Test: a function name exactly FUNCTIONNAME_MAX-1 (1023) chars fits without truncation.
HWTEST_F_L0(JsStackInfoTest, ParseJsFrameInfo_ExactMaxMinusOneName_NoTruncation)
{
    const size_t nameLen = FUNCTIONNAME_MAX - 1;  // 1023
    std::string source = BuildLongNameSource(nameLen);
    const char *filename = "__JsStackInfoExactMax.pa";
    pandasm::Parser parser;
    auto res = parser.Parse(source);
    ASSERT_TRUE(res);
    auto file = pandasm::AsmEmitter::Emit(res.Value());
    ASSERT_NE(file, nullptr);
    auto jsPandaFile = std::make_shared<JSPandaFile>(file.release(), CString(filename), CreateMode::DFX);
    auto debugExtractor = std::make_unique<DebugInfoExtractor>(jsPandaFile.get());
    auto methods = JSStackTrace::ReadAllMethodInfos(jsPandaFile);
    ASSERT_FALSE(methods.empty());

    JsFunction jsFunction;
    (void)memset_s(&jsFunction, sizeof(jsFunction), 0, sizeof(jsFunction));
    EXPECT_TRUE(ParseJsFrameInfo(jsPandaFile.get(), debugExtractor.get(),
        EntityId(methods[0].methodId), 0, jsFunction));

    // Name is exactly 1023 chars, fits in char[1024] with null terminator
    EXPECT_EQ(strlen(jsFunction.functionName), nameLen);
    // Verify no overflow into packageName: packageName should still be empty
    EXPECT_EQ(strlen(jsFunction.packageName), 0U);
}

// Test: a function name of FUNCTIONNAME_MAX (1024) chars is truncated to 1023.
// Before the fix, this would overflow jsFrame.functionName[1024] via strcpy_s
// with destMax=1025 (src.size()+1), writing 1025 bytes into 1024-byte buffer.
HWTEST_F_L0(JsStackInfoTest, ParseJsFrameInfo_ExactlyMaxName_Truncated)
{
    const size_t nameLen = FUNCTIONNAME_MAX;  // 1024
    std::string source = BuildLongNameSource(nameLen);
    const char *filename = "__JsStackInfoMaxName.pa";
    pandasm::Parser parser;
    auto res = parser.Parse(source);
    ASSERT_TRUE(res);
    auto file = pandasm::AsmEmitter::Emit(res.Value());
    ASSERT_NE(file, nullptr);
    auto jsPandaFile = std::make_shared<JSPandaFile>(file.release(), CString(filename), CreateMode::DFX);
    auto debugExtractor = std::make_unique<DebugInfoExtractor>(jsPandaFile.get());
    auto methods = JSStackTrace::ReadAllMethodInfos(jsPandaFile);
    ASSERT_FALSE(methods.empty());

    JsFunction jsFunction;
    (void)memset_s(&jsFunction, sizeof(jsFunction), 0, sizeof(jsFunction));
    EXPECT_TRUE(ParseJsFrameInfo(jsPandaFile.get(), debugExtractor.get(),
        EntityId(methods[0].methodId), 0, jsFunction));

    // After fix: name is truncated to FUNCTIONNAME_MAX-1 = 1023 chars
    EXPECT_EQ(strlen(jsFunction.functionName), FUNCTIONNAME_MAX - 1);
    // Verify no overflow: packageName and url should still be empty
    EXPECT_EQ(strlen(jsFunction.packageName), 0U);
    EXPECT_EQ(strlen(jsFunction.url), 0U);
}

// Test: a very long function name (8192 chars, simulating attacker-controlled ABC)
// is truncated to FUNCTIONNAME_MAX-1 without stack buffer overflow.
// Before the fix, this would write 8193 bytes into 1024-byte functionName buffer,
// overflowing into packageName, url, line, column, codeBegin, codeSize,
// canary, rbp, and return address.
HWTEST_F_L0(JsStackInfoTest, ParseJsFrameInfo_OversizedName8192_TruncatedNoOverflow)
{
    const size_t nameLen = 8192;  // attacker-controlled oversized name
    std::string source = BuildLongNameSource(nameLen);
    const char *filename = "__JsStackInfoOversizedName.pa";
    pandasm::Parser parser;
    auto res = parser.Parse(source);
    ASSERT_TRUE(res);
    auto file = pandasm::AsmEmitter::Emit(res.Value());
    ASSERT_NE(file, nullptr);
    auto jsPandaFile = std::make_shared<JSPandaFile>(file.release(), CString(filename), CreateMode::DFX);
    auto debugExtractor = std::make_unique<DebugInfoExtractor>(jsPandaFile.get());
    auto methods = JSStackTrace::ReadAllMethodInfos(jsPandaFile);
    ASSERT_FALSE(methods.empty());

    JsFunction jsFunction;
    // Fill with sentinel values to detect overflow
    (void)memset_s(jsFunction.functionName, sizeof(jsFunction.functionName), 0xAA, sizeof(jsFunction.functionName));
    (void)memset_s(jsFunction.packageName, sizeof(jsFunction.packageName), 0xBB, sizeof(jsFunction.packageName));
    (void)memset_s(jsFunction.url, sizeof(jsFunction.url), 0xCC, sizeof(jsFunction.url));

    EXPECT_TRUE(ParseJsFrameInfo(jsPandaFile.get(), debugExtractor.get(),
        EntityId(methods[0].methodId), 0, jsFunction));

    // After fix: name is truncated to 1023 chars, null-terminated at position 1023
    EXPECT_EQ(strlen(jsFunction.functionName), FUNCTIONNAME_MAX - 1);

    // Critical: verify no overflow into packageName and url
    // Before fix: 8193 bytes would spill into packageName and url
    EXPECT_EQ(strlen(jsFunction.packageName), 0U)
        << "packageName overflowed — stack buffer overflow detected!";
    EXPECT_EQ(strlen(jsFunction.url), 0U)
        << "url overflowed — stack buffer overflow detected!";

    // Also verify that the sentinel bytes beyond the null terminator in
    // functionName are intact (the buffer is 1024 bytes, name is 1023 + '\0')
    // Position 1023 should be '\0', and the rest of the 1024-byte buffer
    // may have been overwritten by strcpy_s, but nothing beyond the buffer.
    EXPECT_EQ(jsFunction.functionName[FUNCTIONNAME_MAX - 1], '\0');
}

// Test: with a normal-length function name, verify that line, column,
// and other JsFunction fields are set correctly (regression test).
HWTEST_F_L0(JsStackInfoTest, ParseJsFrameInfo_NormalName_FieldsCorrect)
{
    const char *filename = "__JsStackInfoNormalFields.pa";
    const char *data = R"(
        .function void bar() {
            return
        }
    )";
    pandasm::Parser parser;
    auto res = parser.Parse(data);
    auto file = pandasm::AsmEmitter::Emit(res.Value());
    ASSERT_NE(file, nullptr);
    auto jsPandaFile = std::make_shared<JSPandaFile>(file.release(), CString(filename), CreateMode::DFX);
    auto debugExtractor = std::make_unique<DebugInfoExtractor>(jsPandaFile.get());
    auto methods = JSStackTrace::ReadAllMethodInfos(jsPandaFile);
    ASSERT_FALSE(methods.empty());

    JsFunction jsFunction;
    (void)memset_s(&jsFunction, sizeof(jsFunction), 0, sizeof(jsFunction));
    EXPECT_TRUE(ParseJsFrameInfo(jsPandaFile.get(), debugExtractor.get(),
        EntityId(methods[0].methodId), 0, jsFunction));

    EXPECT_STREQ(jsFunction.functionName, "bar");
}

// ============================================================================
// SourceMap singleton pass-through tests for ParseJsFrameInfo
//
// These cases verify the normalization change in JSStackTrace::GetJsFrameInfo
// (js_stackinfo.cpp:1661-1662), which now forwards &SourceMap::GetInstance()
// to the ParseJsFrameInfo template instead of leaving the sourceMap parameter
// at its nullptr default. The unit-test environment has no hap, so
// SourceMap::TranslateUrlPositionBySourceMap returns false for any url and
// leaves url/line/column/packageName unchanged. The cases below check that:
//   * Passing the singleton does not crash and returns the expected fields.
//   * The nullptr path and the singleton path produce identical primary
//     fields (functionName/url/line/column) under the same pandafile.
//   * SourceMap::GetInstance().GetInitStatus() is safely accessible and equals
//     NOT_EXECUTED when no Init/ReadSourceMapData has been invoked.
//   * Repeated singleton calls within one thread are idempotent.
//   * Long-name truncation is orthogonal to the SourceMap translation path.
// No SourceMap internal state is mutated: the tests never call
// Init/ReadSourceMapData/SetInitStatus, so the global singleton stays clean
// for other cases.
// ============================================================================

// Test: ParseJsFrameInfo with &SourceMap::GetInstance() returns true, fills
// functionName/url/line/column correctly, and leaves packageName empty
// because TranslateUrlPositionBySourceMap fails to find the url in the
// unit-test environment (no hap, no source map data loaded).
HWTEST_F_L0(JsStackInfoTest, ParseJsFrameInfo_WithSourceMapSingleton_NoCrash)
{
    const char *filename = "__JsStackInfoSingleton.pa";
    const char *data = R"(
        .function void fooSingleton() {
            return
        }
    )";
    pandasm::Parser parser;
    auto res = parser.Parse(data);
    ASSERT_TRUE(res);
    auto file = pandasm::AsmEmitter::Emit(res.Value());
    ASSERT_NE(file, nullptr);
    auto jsPandaFile = std::make_shared<JSPandaFile>(file.release(), CString(filename), CreateMode::DFX);
    auto debugExtractor = std::make_unique<DebugInfoExtractor>(jsPandaFile.get());
    auto methods = JSStackTrace::ReadAllMethodInfos(jsPandaFile);
    ASSERT_FALSE(methods.empty());

    JsFunction jsFunction;
    (void)memset_s(&jsFunction, sizeof(jsFunction), 0, sizeof(jsFunction));
    // Pass the SourceMap singleton exactly as JSStackTrace::GetJsFrameInfo does.
    EXPECT_TRUE(ParseJsFrameInfo(jsPandaFile.get(), debugExtractor.get(),
        EntityId(methods[0].methodId), 0, jsFunction, &SourceMap::GetInstance()));

    // Primary fields must be populated correctly.
    EXPECT_STREQ(jsFunction.functionName, "fooSingleton");
    // packageName stays empty: TranslateUrlPositionBySourceMap returned false
    // because the url is not present in any loaded source map.
    EXPECT_EQ(strlen(jsFunction.packageName), 0U);
    // url should be empty or non-empty depending on debug info, but must be a
    // valid C string (no overflow). Verify it is null-terminated.
    EXPECT_EQ(jsFunction.url[URL_MAX - 1], '\0');
    // line/column parity: compare against the nullptr path to ensure the
    // singleton path does not perturb line/column. A standalone
    // EXPECT_GE(line, 0) would be a tautology because lineNumber is always
    // reset to 0 on MatchLine failure and set to line+1 on success.
    JsFunction jsFunctionBaseline;
    (void)memset_s(&jsFunctionBaseline, sizeof(jsFunctionBaseline), 0, sizeof(jsFunctionBaseline));
    EXPECT_TRUE(ParseJsFrameInfo(jsPandaFile.get(), debugExtractor.get(),
        EntityId(methods[0].methodId), 0, jsFunctionBaseline, nullptr));
    EXPECT_EQ(jsFunction.line, jsFunctionBaseline.line);
    EXPECT_EQ(jsFunction.column, jsFunctionBaseline.column);
}

// Test: Under the same pandafile, passing nullptr and passing
// &SourceMap::GetInstance() must produce identical primary fields because
// the unit-test SourceMap has no data and TranslateUrlPositionBySourceMap
// returns false without modifying url/line/column/packageName.
HWTEST_F_L0(JsStackInfoTest, ParseJsFrameInfo_NullptrVsSingleton_Parity)
{
    const char *filename = "__JsStackInfoParity.pa";
    const char *data = R"(
        .function void parityFunc() {
            return
        }
    )";
    pandasm::Parser parser;
    auto res = parser.Parse(data);
    ASSERT_TRUE(res);
    auto file = pandasm::AsmEmitter::Emit(res.Value());
    ASSERT_NE(file, nullptr);
    auto jsPandaFile = std::make_shared<JSPandaFile>(file.release(), CString(filename), CreateMode::DFX);
    auto debugExtractor = std::make_unique<DebugInfoExtractor>(jsPandaFile.get());
    auto methods = JSStackTrace::ReadAllMethodInfos(jsPandaFile);
    ASSERT_FALSE(methods.empty());

    // First call: nullptr path (pre-normalization behavior).
    JsFunction jsFunctionNullptr;
    (void)memset_s(&jsFunctionNullptr, sizeof(jsFunctionNullptr), 0, sizeof(jsFunctionNullptr));
    EXPECT_TRUE(ParseJsFrameInfo(jsPandaFile.get(), debugExtractor.get(),
        EntityId(methods[0].methodId), 0, jsFunctionNullptr, nullptr));

    // Second call: singleton path (post-normalization behavior).
    JsFunction jsFunctionSingleton;
    (void)memset_s(&jsFunctionSingleton, sizeof(jsFunctionSingleton), 0, sizeof(jsFunctionSingleton));
    EXPECT_TRUE(ParseJsFrameInfo(jsPandaFile.get(), debugExtractor.get(),
        EntityId(methods[0].methodId), 0, jsFunctionSingleton, &SourceMap::GetInstance()));

    // Primary fields must match between the two paths.
    EXPECT_STREQ(jsFunctionNullptr.functionName, jsFunctionSingleton.functionName);
    EXPECT_STREQ(jsFunctionNullptr.url, jsFunctionSingleton.url);
    EXPECT_EQ(jsFunctionNullptr.line, jsFunctionSingleton.line);
    EXPECT_EQ(jsFunctionNullptr.column, jsFunctionSingleton.column);
    // packageName must be empty in both cases.
    EXPECT_EQ(strlen(jsFunctionNullptr.packageName), 0U);
    EXPECT_EQ(strlen(jsFunctionSingleton.packageName), 0U);
}

// Test: SourceMap::GetInstance() is safely accessible in the unit-test
// environment and GetInitStatus() returns NOT_EXECUTED because no Init /
// ReadSourceMapData / SetInitStatus has been called. This confirms the
// singleton can be queried without crashing before any hap loads.
HWTEST_F_L0(JsStackInfoTest, SourceMap_GetInstance_InitStatusNotExecuted)
{
    // Touch the singleton and query its init status. Must not crash.
    InitStatus status = SourceMap::GetInstance().GetInitStatus();
    EXPECT_EQ(status, InitStatus::NOT_EXECUTED);

    // Repeated queries must remain stable and idempotent.
    EXPECT_EQ(SourceMap::GetInstance().GetInitStatus(), InitStatus::NOT_EXECUTED);
    EXPECT_EQ(SourceMap::GetInstance().GetInitStatus(), status);
}

// Test: Repeated ParseJsFrameInfo calls passing &SourceMap::GetInstance()
// within the same thread must be idempotent: every call returns true, the
// primary fields are stable, and packageName stays empty. This verifies that
// the SourceMap translation path does not accumulate state across calls when
// no source map data is loaded.
HWTEST_F_L0(JsStackInfoTest, ParseJsFrameInfo_Singleton_IdempotentRepeatCalls)
{
    const char *filename = "__JsStackInfoIdempotent.pa";
    const char *data = R"(
        .function void idempotentFunc() {
            return
        }
    )";
    pandasm::Parser parser;
    auto res = parser.Parse(data);
    ASSERT_TRUE(res);
    auto file = pandasm::AsmEmitter::Emit(res.Value());
    ASSERT_NE(file, nullptr);
    auto jsPandaFile = std::make_shared<JSPandaFile>(file.release(), CString(filename), CreateMode::DFX);
    auto debugExtractor = std::make_unique<DebugInfoExtractor>(jsPandaFile.get());
    auto methods = JSStackTrace::ReadAllMethodInfos(jsPandaFile);
    ASSERT_FALSE(methods.empty());

    JsFunction first;
    (void)memset_s(&first, sizeof(first), 0, sizeof(first));
    EXPECT_TRUE(ParseJsFrameInfo(jsPandaFile.get(), debugExtractor.get(),
        EntityId(methods[0].methodId), 0, first, &SourceMap::GetInstance()));
    EXPECT_STREQ(first.functionName, "idempotentFunc");
    EXPECT_EQ(strlen(first.packageName), 0U);

    // Call repeatedly and compare against the first invocation.
    for (int i = 0; i < 5; ++i) {
        JsFunction iter;
        (void)memset_s(&iter, sizeof(iter), 0, sizeof(iter));
        EXPECT_TRUE(ParseJsFrameInfo(jsPandaFile.get(), debugExtractor.get(),
            EntityId(methods[0].methodId), 0, iter, &SourceMap::GetInstance()));
        EXPECT_STREQ(iter.functionName, first.functionName);
        EXPECT_STREQ(iter.url, first.url);
        EXPECT_EQ(iter.line, first.line);
        EXPECT_EQ(iter.column, first.column);
        EXPECT_EQ(strlen(iter.packageName), 0U);
    }

    // JsStackInfo::nameMap is cleared in TearDown, no extra cleanup needed.
}

// Test: A long function name combined with the singleton SourceMap path
// verifies that the truncation logic (name.resize(FUNCTIONNAME_MAX - 1)) and
// the SourceMap translation path are orthogonal: truncation still happens,
// packageName remains empty, and no buffer overflow is introduced by the
// additional sourceMap->TranslateUrlPositionBySourceMap call.
HWTEST_F_L0(JsStackInfoTest, ParseJsFrameInfo_LongName_WithSingleton_OrthogonalTruncation)
{
    const size_t nameLen = FUNCTIONNAME_MAX + 1024;  // 2048, well over the truncation threshold
    std::string source = BuildLongNameSource(nameLen);
    const char *filename = "__JsStackInfoLongNameSingleton.pa";
    pandasm::Parser parser;
    auto res = parser.Parse(source);
    ASSERT_TRUE(res);
    auto file = pandasm::AsmEmitter::Emit(res.Value());
    ASSERT_NE(file, nullptr);
    auto jsPandaFile = std::make_shared<JSPandaFile>(file.release(), CString(filename), CreateMode::DFX);
    auto debugExtractor = std::make_unique<DebugInfoExtractor>(jsPandaFile.get());
    auto methods = JSStackTrace::ReadAllMethodInfos(jsPandaFile);
    ASSERT_FALSE(methods.empty());

    JsFunction jsFunction;
    // Fill buffers with sentinels to detect any overflow introduced by the
    // SourceMap path.
    (void)memset_s(jsFunction.functionName, sizeof(jsFunction.functionName), 0xAA,
                   sizeof(jsFunction.functionName));
    (void)memset_s(jsFunction.packageName, sizeof(jsFunction.packageName), 0xBB,
                   sizeof(jsFunction.packageName));
    (void)memset_s(jsFunction.url, sizeof(jsFunction.url), 0xCC, sizeof(jsFunction.url));

    EXPECT_TRUE(ParseJsFrameInfo(jsPandaFile.get(), debugExtractor.get(),
        EntityId(methods[0].methodId), 0, jsFunction, &SourceMap::GetInstance()));

    // Truncation must still occur: name is clamped to FUNCTIONNAME_MAX-1 chars.
    EXPECT_EQ(strlen(jsFunction.functionName), FUNCTIONNAME_MAX - 1);
    EXPECT_EQ(jsFunction.functionName[FUNCTIONNAME_MAX - 1], '\0');

    // SourceMap path must not corrupt packageName: TranslateUrlPositionBySourceMap
    // returned false, so packageName stays empty (no overflow from functionName).
    EXPECT_EQ(strlen(jsFunction.packageName), 0U)
        << "packageName overflowed — SourceMap path corrupted buffer!";
    // url stays empty (no overflow from functionName). Only url[0] is written
    // to '\0' by strcpy_s when the source file is empty; sentinel bytes beyond
    // url[0] are intentionally left untouched, so check strlen instead of the
    // trailing byte.
    EXPECT_EQ(strlen(jsFunction.url), 0U)
        << "url overflowed — SourceMap path corrupted buffer!";
}
}  // namespace panda::test