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

#include <cstdint>
#include <string>

#include "ecmascript/dfx/cpu_profiler/signal_safe_frame_iterator.h"
#include "ecmascript/ecma_vm.h"
#include "ecmascript/global_env.h"
#include "ecmascript/js_function.h"
#include "ecmascript/jspandafile/js_pandafile_manager.h"
#include "ecmascript/object_factory.h"
#include "ecmascript/tests/test_helper.h"
#include "securec.h"

using namespace panda::ecmascript;

namespace panda::test {
class SignalSafeFrameIteratorTest : public testing::Test {
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
    }

    void TearDown() override
    {
        TestHelper::DestroyEcmaVMWithScope(instance, scope);
    }

    EcmaVM *instance {nullptr};
    EcmaHandleScope *scope {nullptr};
    JSThread *thread {nullptr};
};

namespace {
// Slots are pointer-sized on every supported arch (EAS-aligned frame members).
constexpr size_t SLOT = sizeof(JSTaggedType);
constexpr int BUF_SLOTS = 160; // 160: slots for several synthetic frames
constexpr int TOP_SP_IDX = 40; // 40: sp of the top (deepest) frame
constexpr int CALLER_SP_IDX = 80; // 80: sp of the caller frame

// Frame-layout facts (frames.h): the type word always sits in the slot right
// below sp. The caller fp is read either from sp[0] (optimized-style frames)
// or from the slot two below sp (interpreter-style frames and the two entry
// bridge variants that keep InterpretedFrameBase last).
constexpr int PREV_AT_SP = 0;     // optimized-style: prevFp stored at sp[0]
constexpr int PREV_BELOW_SP = -2; // interpreter-style: base.prev at sp[-2]

void WriteTypeWord(JSTaggedType *buf, int spIdx, FrameType type)
{
    buf[spIdx - 1] = static_cast<JSTaggedType>(type);
}

void WritePrevFp(JSTaggedType *buf, int prevIdx, JSTaggedType *prev)
{
    buf[prevIdx] = reinterpret_cast<JSTaggedType>(prev);
}

void WriteTaggedSlot(JSTaggedType *buf, int slotIdx, JSTaggedValue value)
{
    buf[slotIdx] = value.GetRawData();
}

// Builds a two-frame chain of one frame type: top frame at TOP_SP_IDX, caller
// at CALLER_SP_IDX, caller's prev is nullptr (end of the JS stack).
void BuildChain(JSTaggedType *buf, FrameType type, int prevDelta)
{
    WriteTypeWord(buf, TOP_SP_IDX, type);
    WritePrevFp(buf, TOP_SP_IDX + prevDelta, &buf[CALLER_SP_IDX]);
    WriteTypeWord(buf, CALLER_SP_IDX, type);
    WritePrevFp(buf, CALLER_SP_IDX + prevDelta, nullptr);
}

// Scratch space for synthetic frames, carved from the bottom of the thread's
// JS stack region: IsLegalSp accepts that region, and neither the interpreter
// (which grows down from the region top) nor the GC (which scans from the
// current frame upward) can reach it during the test.
constexpr int REGION_BASE_IDX = 64; // 64: 512 bytes above the region bottom

JSTaggedType *FrameScratch(const JSThread *thread)
{
    // Region top = the SP-tracked frame pointer (frameBase_ + stack size at
    // thread setup); the interpreter's active frames live below it.
    const JSTaggedType *regionTop = thread->GetCurrentSPFrame();
    size_t maxStackSize = thread->GetEcmaVM()->GetEcmaParamConfiguration().GetMaxStackSize();
    auto *buf = const_cast<JSTaggedType *>(regionTop - maxStackSize) + REGION_BASE_IDX;
    errno_t ret = memset_s(buf, BUF_SLOTS * sizeof(JSTaggedType), 0, BUF_SLOTS * sizeof(JSTaggedType));
    if (ret != EOK) {
        return nullptr;
    }
    return buf;
}

// Loads cpuprofiler_1.abc so a real JSPandaFile with method literals exists.
const JSPandaFile *LoadProfilerAbc(EcmaVM *vm)
{
    std::string fileName = DEBUGGER_ABC_DIR "cpuprofiler_1.abc";
    std::string entryPoint = "cpuprofiler_1";
    if (!JSNApi::Execute(vm, fileName, entryPoint)) {
        return nullptr;
    }
    const JSPandaFile *found = nullptr;
    JSPandaFileManager::GetInstance()->EnumerateJSPandaFiles(
        [&found](const std::shared_ptr<JSPandaFile> &file) -> bool {
            if (file->GetJSPandaFileDesc().find("cpuprofiler_1.abc") != CString::npos) {
                found = file.get();
            }
            return true;
        });
    return found;
}
} // namespace

// Advance walks every optimized-style frame family and stops at the nullptr
// sentinel (end of the JS stack).
HWTEST_F_L0(SignalSafeFrameIteratorTest, AdvanceOptimizedStyleChains)
{
    const FrameType types[] = {
        FrameType::OPTIMIZED_FRAME,
        FrameType::OPTIMIZED_JS_FUNCTION_FRAME,
        FrameType::OPTIMIZED_JS_FAST_CALL_FUNCTION_FRAME,
        FrameType::FASTJIT_FUNCTION_FRAME,
        FrameType::FASTJIT_FAST_CALL_FUNCTION_FRAME,
        FrameType::ASM_BRIDGE_FRAME,
        FrameType::LEAVE_FRAME,
        FrameType::LEAVE_FRAME_WITH_ARGV,
        FrameType::BUILTIN_CALL_LEAVE_FRAME,
        FrameType::BUILTIN_FRAME,
        FrameType::BUILTIN_FRAME_WITH_ARGV,
        FrameType::BUILTIN_ENTRY_FRAME,
        FrameType::OPTIMIZED_JS_FUNCTION_ARGS_CONFIG_FRAME,
        FrameType::OPTIMIZED_JS_FUNCTION_UNFOLD_ARGV_FRAME,
    };
    JSTaggedType *buf = FrameScratch(thread);
    ASSERT_NE(buf, nullptr);
    for (FrameType type : types) {
        BuildChain(buf, type, PREV_AT_SP);
        SignalSafeProfilerFrameIterator it(&buf[TOP_SP_IDX], thread);
        ASSERT_FALSE(it.Done()) << "type=" << static_cast<int>(type);
        it.Advance();
        ASSERT_FALSE(it.Done()) << "caller of type=" << static_cast<int>(type);
        EXPECT_EQ(it.GetType(), type);
        it.Advance(); // caller's prev is nullptr: end of stack
        EXPECT_TRUE(it.Done());
        it.Advance(); // Advance on a finished iterator is a no-op
        EXPECT_TRUE(it.Done());
    }
}

// Advance walks every interpreter-style frame family.
HWTEST_F_L0(SignalSafeFrameIteratorTest, AdvanceInterpreterStyleChains)
{
    const FrameType types[] = {
        FrameType::INTERPRETER_FRAME,
        FrameType::INTERPRETER_FAST_NEW_FRAME,
        FrameType::ASM_INTERPRETER_FRAME,
        FrameType::INTERPRETER_CONSTRUCTOR_FRAME,
        FrameType::INTERPRETER_BUILTIN_FRAME,
        FrameType::INTERPRETER_ENTRY_FRAME,
        FrameType::ASM_INTERPRETER_ENTRY_FRAME,
        // Prev fp below sp although these are code frames: OptimizedEntry
        // returns preLeaveFrameFp and the bridge returns entry.base.prev.
        FrameType::OPTIMIZED_ENTRY_FRAME,
        FrameType::ASM_INTERPRETER_BRIDGE_FRAME,
    };
    JSTaggedType *buf = FrameScratch(thread);
    ASSERT_NE(buf, nullptr);
    for (FrameType type : types) {
        BuildChain(buf, type, PREV_BELOW_SP);
        SignalSafeProfilerFrameIterator it(&buf[TOP_SP_IDX], thread);
        ASSERT_FALSE(it.Done()) << "type=" << static_cast<int>(type);
        it.Advance();
        ASSERT_FALSE(it.Done()) << "caller of type=" << static_cast<int>(type);
        EXPECT_EQ(it.GetType(), type);
        it.Advance();
        EXPECT_TRUE(it.Done());
    }
}

// GetMethod resolves the Method of a frame that carries a real JSFunction.
HWTEST_F_L0(SignalSafeFrameIteratorTest, GetMethodResolvesFunctionFrames)
{
    auto factory = thread->GetEcmaVM()->GetFactory();
    JSHandle<GlobalEnv> globalEnv = thread->GetGlobalEnv();
    JSHandle<JSFunction> func = factory->NewJSFunction(globalEnv);
    Method *method = func->GetCallTarget(thread);
    ASSERT_NE(method, nullptr);
    JSTaggedValue funcValue = func.GetTaggedValue();

    // Function slot per frame family (frames.h layouts):
    //   jsFunc@sp[-2]: OptimizedJSFunction/FASTJIT frames
    //   function@sp[-9]: AsmInterpreted frames
    //   function@sp[-8]: Interpreted frames
    //   function@sp[-4]: InterpretedBuiltin frame
    //   function@sp[+4]: Builtin/BuiltinEntry/BuiltinCallLeave stack-args slot
    //   argc(int32)@sp[-2], function@sp[-5]: BuiltinWithArgv frame
    struct SlotCase {
        FrameType type;
        int prevDelta;
        int funcSlot;
    };
    const SlotCase cases[] = {
        {FrameType::OPTIMIZED_JS_FUNCTION_FRAME, PREV_AT_SP, -2},
        {FrameType::OPTIMIZED_JS_FAST_CALL_FUNCTION_FRAME, PREV_AT_SP, -2},
        {FrameType::FASTJIT_FUNCTION_FRAME, PREV_AT_SP, -2},
        {FrameType::FASTJIT_FAST_CALL_FUNCTION_FRAME, PREV_AT_SP, -2},
        {FrameType::INTERPRETER_FRAME, PREV_BELOW_SP, -8},
        {FrameType::INTERPRETER_FAST_NEW_FRAME, PREV_BELOW_SP, -8},
        {FrameType::ASM_INTERPRETER_FRAME, PREV_BELOW_SP, -9},
        {FrameType::INTERPRETER_CONSTRUCTOR_FRAME, PREV_BELOW_SP, -9},
        {FrameType::INTERPRETER_BUILTIN_FRAME, PREV_BELOW_SP, -4},
        {FrameType::BUILTIN_FRAME, PREV_AT_SP, 4},
        {FrameType::BUILTIN_ENTRY_FRAME, PREV_AT_SP, 4},
        {FrameType::BUILTIN_CALL_LEAVE_FRAME, PREV_AT_SP, 4},
        {FrameType::BUILTIN_FRAME_WITH_ARGV, PREV_AT_SP, -5},
    };

    JSTaggedType *buf = FrameScratch(thread);
    ASSERT_NE(buf, nullptr);
    for (const auto &slotCase : cases) {
        WriteTypeWord(buf, TOP_SP_IDX, slotCase.type);
        WritePrevFp(buf, TOP_SP_IDX + slotCase.prevDelta, nullptr);
        if (slotCase.type == FrameType::BUILTIN_FRAME_WITH_ARGV) {
            // argc lives in the slot below the frame; 0 mandatory-only argv.
            buf[TOP_SP_IDX - 2] = 0;
        }
        WriteTaggedSlot(buf, TOP_SP_IDX + slotCase.funcSlot, funcValue);
        SignalSafeProfilerFrameIterator it(&buf[TOP_SP_IDX], thread);
        ASSERT_FALSE(it.Done()) << "type=" << static_cast<int>(slotCase.type);
        EXPECT_EQ(it.GetFunction().GetRawData(), funcValue.GetRawData());
        EXPECT_EQ(it.GetMethod(), method) << "type=" << static_cast<int>(slotCase.type);
    }
}

// GetMethod returns nullptr for frames whose function slot is not a function.
HWTEST_F_L0(SignalSafeFrameIteratorTest, GetMethodRejectsNonFunctionFrames)
{
    JSTaggedType *buf = FrameScratch(thread);
    ASSERT_NE(buf, nullptr);
    const FrameType types[] = {
        FrameType::LEAVE_FRAME,
        FrameType::OPTIMIZED_FRAME,
        FrameType::ASM_INTERPRETER_ENTRY_FRAME,
    };
    for (FrameType type : types) {
        WriteTypeWord(buf, TOP_SP_IDX, type);
        WritePrevFp(buf, TOP_SP_IDX + PREV_BELOW_SP, nullptr);
        // Leave the function slot zeroed: no dereference may happen.
        SignalSafeProfilerFrameIterator it(&buf[TOP_SP_IDX], thread);
        ASSERT_FALSE(it.Done());
        EXPECT_TRUE(it.GetFunction().IsUndefined());
        EXPECT_EQ(it.GetMethod(), nullptr);
    }
}

// GetBytecodeOffset derives the offset from the frame pc and the method's
// bytecode array for both interpreter frame flavors.
HWTEST_F_L0(SignalSafeFrameIteratorTest, GetBytecodeOffsetInterpreterFrames)
{
    const JSPandaFile *pf = LoadProfilerAbc(instance);
    ASSERT_NE(pf, nullptr);
    ASSERT_GT(pf->GetNumMethods(), 0U);
    MethodLiteral *literal = pf->GetMethodLiterals();
    ASSERT_NE(literal, nullptr);

    auto factory = thread->GetEcmaVM()->GetFactory();
    JSHandle<GlobalEnv> globalEnv = thread->GetGlobalEnv();
    JSHandle<Method> methodHandle = factory->NewMethod(literal);
    JSHandle<JSFunction> func = factory->NewJSFunction(globalEnv, methodHandle);
    Method *method = func->GetCallTarget(thread);
    ASSERT_NE(method, nullptr);
    JSTaggedValue funcValue = func.GetTaggedValue();
    const uint8_t *bytecode = method->GetBytecodeArray();
    ASSERT_NE(bytecode, nullptr);
    constexpr uint32_t expectedOffset = 8; // 8: arbitrary in-method offset
    const uint8_t *pc = bytecode + expectedOffset;

    struct PcCase {
        FrameType type;
        int pcSlot;
    };
    const PcCase cases[] = {
        {FrameType::ASM_INTERPRETER_FRAME, -3}, // pc@sp[-3]
        {FrameType::INTERPRETER_CONSTRUCTOR_FRAME, -3},
        {FrameType::INTERPRETER_FRAME, -3}, // pc@sp[-3] (InterpretedFrame is 72 bytes)
        {FrameType::INTERPRETER_FAST_NEW_FRAME, -3},
    };

    JSTaggedType *buf = FrameScratch(thread);
    ASSERT_NE(buf, nullptr);
    for (const auto &pcCase : cases) {
        WriteTypeWord(buf, TOP_SP_IDX, pcCase.type);
        WritePrevFp(buf, TOP_SP_IDX + PREV_BELOW_SP, nullptr);
        int funcSlot = pcCase.type == FrameType::ASM_INTERPRETER_FRAME ||
            pcCase.type == FrameType::INTERPRETER_CONSTRUCTOR_FRAME ? -9 : -8;
        WriteTaggedSlot(buf, TOP_SP_IDX + funcSlot, funcValue);
        buf[TOP_SP_IDX + pcCase.pcSlot] = reinterpret_cast<JSTaggedType>(pc);
        SignalSafeProfilerFrameIterator it(&buf[TOP_SP_IDX], thread);
        ASSERT_FALSE(it.Done()) << "type=" << static_cast<int>(pcCase.type);
        EXPECT_EQ(it.GetBytecodeOffset(), expectedOffset);
    }

    // Non-interpreter frames report offset 0.
    WriteTypeWord(buf, TOP_SP_IDX, FrameType::LEAVE_FRAME);
    WritePrevFp(buf, TOP_SP_IDX + PREV_AT_SP, nullptr);
    SignalSafeProfilerFrameIterator leaveIt(&buf[TOP_SP_IDX], thread);
    ASSERT_FALSE(leaveIt.Done());
    EXPECT_EQ(leaveIt.GetBytecodeOffset(), 0U);
}

// ValidateNextSp rejects non-increasing chains, misaligned frame pointers,
// out-of-bounds pointers and unrecognized frame-type words.
HWTEST_F_L0(SignalSafeFrameIteratorTest, ValidateNextSpRejectsCorruptChains)
{
    JSTaggedType *buf = FrameScratch(thread);
    ASSERT_NE(buf, nullptr);
    const FrameType type = FrameType::ASM_INTERPRETER_FRAME;

    // 1. Loop: prev fp points back at the current frame.
    WriteTypeWord(buf, TOP_SP_IDX, type);
    WritePrevFp(buf, TOP_SP_IDX + PREV_BELOW_SP, &buf[TOP_SP_IDX]);
    SignalSafeProfilerFrameIterator loopIt(&buf[TOP_SP_IDX], thread);
    ASSERT_FALSE(loopIt.Done());
    loopIt.Advance();
    EXPECT_FALSE(loopIt.IsValid());

    // 2. Non-increasing: prev fp below the current sp.
    WritePrevFp(buf, TOP_SP_IDX + PREV_BELOW_SP, &buf[10]);
    SignalSafeProfilerFrameIterator lowerIt(&buf[TOP_SP_IDX], thread);
    ASSERT_FALSE(lowerIt.Done());
    lowerIt.Advance();
    EXPECT_FALSE(lowerIt.IsValid());

    // 3. Misaligned caller frame pointer.
    auto *misaligned = reinterpret_cast<JSTaggedType *>(
        reinterpret_cast<uintptr_t>(&buf[CALLER_SP_IDX]) + SLOT / 2);
    WritePrevFp(buf, TOP_SP_IDX + PREV_BELOW_SP, misaligned);
    SignalSafeProfilerFrameIterator misalignedIt(&buf[TOP_SP_IDX], thread);
    ASSERT_FALSE(misalignedIt.Done());
    misalignedIt.Advance();
    EXPECT_FALSE(misalignedIt.IsValid());

    // 4. Caller fp outside the thread stack bounds (heap allocation).
    auto *heapFake = new JSTaggedType[4]; // 4: one frame slot plus slack
    WritePrevFp(buf, TOP_SP_IDX + PREV_BELOW_SP, heapFake);
    SignalSafeProfilerFrameIterator heapIt(&buf[TOP_SP_IDX], thread);
    ASSERT_FALSE(heapIt.Done());
    heapIt.Advance();
    EXPECT_FALSE(heapIt.IsValid());
    delete[] heapFake;

    // 5. Caller slot's type word decodes outside the FrameType range.
    WritePrevFp(buf, TOP_SP_IDX + PREV_BELOW_SP, &buf[CALLER_SP_IDX]);
    WriteTypeWord(buf, CALLER_SP_IDX, type);
    buf[CALLER_SP_IDX - 1] = 0x9999; // 0x9999: garbage type word
    SignalSafeProfilerFrameIterator garbageIt(&buf[TOP_SP_IDX], thread);
    ASSERT_FALSE(garbageIt.Done());
    garbageIt.Advance();
    EXPECT_FALSE(garbageIt.IsValid());
}

// The constructor rejects a misaligned sp up front.
HWTEST_F_L0(SignalSafeFrameIteratorTest, ConstructorRejectsMisalignedSp)
{
    JSTaggedType *buf = FrameScratch(thread);
    ASSERT_NE(buf, nullptr);
    WriteTypeWord(buf, TOP_SP_IDX, FrameType::ASM_INTERPRETER_FRAME);
    auto *misaligned = reinterpret_cast<JSTaggedType *>(
        reinterpret_cast<uintptr_t>(&buf[TOP_SP_IDX]) + SLOT / 2);
    SignalSafeProfilerFrameIterator it(misaligned, thread);
    EXPECT_TRUE(it.Done());
    EXPECT_FALSE(it.IsValid());
}

// Leave-frame and frame-family predicates classify the current frame.
HWTEST_F_L0(SignalSafeFrameIteratorTest, FrameFamilyPredicates)
{
    JSTaggedType *buf = FrameScratch(thread);
    ASSERT_NE(buf, nullptr);
    auto check = [&buf, this](FrameType type, int prevDelta) {
        WriteTypeWord(buf, TOP_SP_IDX, type);
        WritePrevFp(buf, TOP_SP_IDX + prevDelta, nullptr);
        SignalSafeProfilerFrameIterator it(&buf[TOP_SP_IDX], thread);
        ASSERT_FALSE(it.Done()) << "type=" << static_cast<int>(type);
        EXPECT_EQ(it.GetType(), type);
        bool isLeave = type == FrameType::LEAVE_FRAME || type == FrameType::LEAVE_FRAME_WITH_ARGV;
        EXPECT_EQ(it.IsLeaveFrame(), isLeave);
        bool isOptimizedJs = type == FrameType::OPTIMIZED_JS_FUNCTION_FRAME ||
            type == FrameType::OPTIMIZED_JS_FAST_CALL_FUNCTION_FRAME;
        EXPECT_EQ(it.IsOptimizedJSFunctionFrame(), isOptimizedJs);
        bool isFastJit = type == FrameType::FASTJIT_FUNCTION_FRAME ||
            type == FrameType::FASTJIT_FAST_CALL_FUNCTION_FRAME;
        EXPECT_EQ(it.IsFastJitFunctionFrame(), isFastJit);
        EXPECT_FALSE(it.IsSteedFunctionFrame());
    };

    check(FrameType::LEAVE_FRAME, PREV_AT_SP);
    check(FrameType::LEAVE_FRAME_WITH_ARGV, PREV_AT_SP);
    check(FrameType::OPTIMIZED_JS_FUNCTION_FRAME, PREV_AT_SP);
    check(FrameType::OPTIMIZED_JS_FAST_CALL_FUNCTION_FRAME, PREV_AT_SP);
    check(FrameType::FASTJIT_FUNCTION_FRAME, PREV_AT_SP);
    check(FrameType::FASTJIT_FAST_CALL_FUNCTION_FRAME, PREV_AT_SP);
    check(FrameType::ASM_INTERPRETER_FRAME, PREV_BELOW_SP);
    check(FrameType::BUILTIN_FRAME, PREV_AT_SP);
}
} // namespace panda::test
