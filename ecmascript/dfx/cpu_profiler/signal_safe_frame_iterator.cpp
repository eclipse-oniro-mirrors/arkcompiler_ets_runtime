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

#include "ecmascript/dfx/cpu_profiler/signal_safe_frame_iterator.h"

#include "ecmascript/dfx/cpu_profiler/profiler_raw_reads.h"
#include "ecmascript/interpreter/frame_handler.h"
#include "ecmascript/js_object.h"

namespace panda::ecmascript {
namespace {
// Raw frame-type read without FrameHandler's debug ASSERT landing pad: the
// signal-handler call graph must stay closed (plan §6.5).
FrameType ReadFrameType(const JSTaggedType *sp)
{
    FrameType *typeAddr = reinterpret_cast<FrameType *>(
        reinterpret_cast<uintptr_t>(sp) - sizeof(FrameType));
    return static_cast<FrameType>(static_cast<uintptr_t>(*typeAddr) &
        FrameIterator::CLEARD_LAZY_DEOPT_FLAG);
}
}

SignalSafeProfilerFrameIterator::SignalSafeProfilerFrameIterator(JSTaggedType *sp, const JSThread *thread)
    : current_(sp), thread_(thread)
{
    if (current_ == nullptr || !thread_->IsLegalSp(reinterpret_cast<uintptr_t>(current_))) {
        valid_ = false;
        return;
    }
    if (reinterpret_cast<uintptr_t>(current_) % sizeof(JSTaggedType) != 0) {
        valid_ = false;
        return;
    }
    FrameType type = GetType();
    if (type > FrameType::FRAME_TYPE_LAST || type < FrameType::FRAME_TYPE_FIRST) {
        valid_ = false;
    }
}

bool SignalSafeProfilerFrameIterator::ValidateNextSp(JSTaggedType *next)
{
    if (next == nullptr) {
        return true; // end of the JS stack
    }
    if (reinterpret_cast<uintptr_t>(next) <= reinterpret_cast<uintptr_t>(current_)) {
        return false; // non-increasing chain: loop or corruption
    }
    if (reinterpret_cast<uintptr_t>(next) % sizeof(JSTaggedType) != 0) {
        return false; // misaligned frame pointer
    }
    if (!thread_->IsLegalSp(reinterpret_cast<uintptr_t>(next))) {
        return false; // outside the thread stack bounds
    }
    FrameType type = ReadFrameType(next);
    if (type > FrameType::FRAME_TYPE_LAST || type < FrameType::FRAME_TYPE_FIRST) {
        return false; // next slot is not a recognizable frame
    }
    return true;
}

bool SignalSafeProfilerFrameIterator::PrevFrameSpCode(FrameType type, JSTaggedType *sp, JSTaggedType **next)
{
    switch (type) {
        case FrameType::OPTIMIZED_FRAME:
            *next = OptimizedFrame::GetFrameFromSp(sp)->GetPrevFrameFp();
            return true;
        case FrameType::OPTIMIZED_ENTRY_FRAME:
            *next = OptimizedEntryFrame::GetFrameFromSp(sp)->GetPrevFrameFp();
            return true;
        case FrameType::OPTIMIZED_JS_FUNCTION_FRAME:
        case FrameType::OPTIMIZED_JS_FAST_CALL_FUNCTION_FRAME:
        case FrameType::OPTIMIZED_JS_FUNCTION_ARGS_CONFIG_FRAME:
            *next = OptimizedJSFunctionFrame::GetFrameFromSp(sp)->GetPrevFrameFp();
            return true;
        case FrameType::FASTJIT_FUNCTION_FRAME:
        case FrameType::FASTJIT_FAST_CALL_FUNCTION_FRAME:
            *next = FASTJITFunctionFrame::GetFrameFromSp(sp)->GetPrevFrameFp();
            return true;
        case FrameType::ASM_BRIDGE_FRAME:
            *next = AsmBridgeFrame::GetFrameFromSp(sp)->GetPrevFrameFp();
            return true;
        case FrameType::LEAVE_FRAME:
            *next = OptimizedLeaveFrame::GetFrameFromSp(sp)->GetPrevFrameFp();
            return true;
        case FrameType::LEAVE_FRAME_WITH_ARGV:
            *next = OptimizedWithArgvLeaveFrame::GetFrameFromSp(sp)->GetPrevFrameFp();
            return true;
        case FrameType::BUILTIN_CALL_LEAVE_FRAME:
            *next = OptimizedBuiltinLeaveFrame::GetFrameFromSp(sp)->GetPrevFrameFp();
            return true;
        case FrameType::BUILTIN_FRAME:
        case FrameType::BUILTIN_FRAME_WITH_ARGV_STACK_OVER_FLOW_FRAME:
            *next = BuiltinFrame::GetFrameFromSp(sp)->GetPrevFrameFp();
            return true;
        case FrameType::ASM_INTERPRETER_BRIDGE_FRAME:
            *next = AsmInterpretedBridgeFrame::GetFrameFromSp(sp)->GetPrevFrameFp();
            return true;
        case FrameType::OPTIMIZED_JS_FUNCTION_UNFOLD_ARGV_FRAME:
            *next = OptimizedJSFunctionUnfoldArgVFrame::GetFrameFromSp(sp)->GetPrevFrameFp();
            return true;
        case FrameType::BASELINE_BUILTIN_FRAME:
            *next = BaselineBuiltinFrame::GetFrameFromSp(sp)->GetPrevFrameFp();
            return true;
        case FrameType::STEED_FUNCTION_FRAME:
            *next = SteedFunctionFrame::GetFrameFromSp(sp)->GetPrevFrameFp();
            return true;
        default:
            return false;
    }
}

bool SignalSafeProfilerFrameIterator::PrevFrameSpInterpreter(FrameType type, JSTaggedType *sp, JSTaggedType **next)
{
    switch (type) {
        case FrameType::INTERPRETER_FRAME:
        case FrameType::INTERPRETER_FAST_NEW_FRAME:
            *next = InterpretedFrame::GetFrameFromSp(sp)->GetPrevFrameFp();
            return true;
        case FrameType::ASM_INTERPRETER_FRAME:
        case FrameType::INTERPRETER_CONSTRUCTOR_FRAME:
            *next = AsmInterpretedFrame::GetFrameFromSp(sp)->GetPrevFrameFp();
            return true;
        case FrameType::BUILTIN_ENTRY_FRAME:
            *next = BuiltinFrame::GetFrameFromSp(sp)->GetPrevFrameFp();
            return true;
        case FrameType::BUILTIN_FRAME_WITH_ARGV:
            *next = BuiltinWithArgvFrame::GetFrameFromSp(sp)->GetPrevFrameFp();
            return true;
        case FrameType::INTERPRETER_BUILTIN_FRAME:
            *next = InterpretedBuiltinFrame::GetFrameFromSp(sp)->GetPrevFrameFp();
            return true;
        case FrameType::INTERPRETER_ENTRY_FRAME:
            *next = InterpretedEntryFrame::GetFrameFromSp(sp)->GetPrevFrameFp();
            return true;
        case FrameType::ASM_INTERPRETER_ENTRY_FRAME:
            *next = AsmInterpretedEntryFrame::GetFrameFromSp(sp)->GetPrevFrameFp();
            return true;
        default:
            return false;
    }
}
void SignalSafeProfilerFrameIterator::Advance()
{
    if (Done()) {
        return;
    }
    JSTaggedType *next = nullptr;
    FrameType type = GetType();
    if (!PrevFrameSpCode(type, current_, &next) &&
        !PrevFrameSpInterpreter(type, current_, &next)) {
        valid_ = false; // unknown frame type: stop instead of trapping
        return;
    }
    if (!ValidateNextSp(next)) {
        valid_ = false;
        return;
    }
    current_ = next;
}

FrameType SignalSafeProfilerFrameIterator::GetType() const
{
    return ReadFrameType(current_);
}

JSTaggedValue SignalSafeProfilerFrameIterator::GetFunction() const
{
    switch (GetType()) {
        case FrameType::OPTIMIZED_JS_FAST_CALL_FUNCTION_FRAME:
        case FrameType::OPTIMIZED_JS_FUNCTION_FRAME:
            return Frame<OptimizedJSFunctionFrame>()->GetFunction();
        case FrameType::ASM_INTERPRETER_FRAME:
        case FrameType::INTERPRETER_CONSTRUCTOR_FRAME:
            return Frame<AsmInterpretedFrame>()->function;
        case FrameType::INTERPRETER_FRAME:
        case FrameType::INTERPRETER_FAST_NEW_FRAME:
            return Frame<InterpretedFrame>()->function;
        case FrameType::INTERPRETER_BUILTIN_FRAME:
            return Frame<InterpretedBuiltinFrame>()->function;
        case FrameType::BUILTIN_FRAME_WITH_ARGV:
            return Frame<BuiltinWithArgvFrame>()->GetFunction();
        case FrameType::BUILTIN_ENTRY_FRAME:
        case FrameType::BUILTIN_FRAME:
            return Frame<BuiltinFrame>()->GetFunction();
        case FrameType::BUILTIN_CALL_LEAVE_FRAME:
            return JSTaggedValue(*(Frame<OptimizedBuiltinLeaveFrame>()->GetArgv()));
        case FrameType::FASTJIT_FUNCTION_FRAME:
        case FrameType::FASTJIT_FAST_CALL_FUNCTION_FRAME:
            return Frame<FASTJITFunctionFrame>()->GetFunction();
        case FrameType::STEED_FUNCTION_FRAME:
            return Frame<SteedFunctionFrame>()->GetFunction();
        default:
            return JSTaggedValue::Undefined();
    }
}

Method *SignalSafeProfilerFrameIterator::GetMethod() const
{
    auto function = GetFunction();
    if (function.CheckIsJSFunctionBase() || function.CheckIsJSProxy()) {
        // No-barrier read: GetCallTarget's tagged loads carry read barriers
        // that can synchronously enter CC-GC evacuation from the handler.
        return GetMethodFieldNoBarrier(function.GetTaggedObject());
    }
    return nullptr;
}

uint32_t SignalSafeProfilerFrameIterator::GetBytecodeOffset() const
{
    switch (GetType()) {
        case FrameType::ASM_INTERPRETER_FRAME:
        case FrameType::INTERPRETER_CONSTRUCTOR_FRAME: {
            auto *frame = Frame<AsmInterpretedFrame>();
            // No-barrier read: GetCallTarget's tagged loads carry read barriers
            // that can synchronously enter CC-GC evacuation from the handler.
            Method *method = GetMethodFieldNoBarrier(frame->function.GetTaggedObject());
            return static_cast<uint32_t>(frame->GetPc() - method->GetBytecodeArray());
        }
        case FrameType::INTERPRETER_FRAME:
        case FrameType::INTERPRETER_FAST_NEW_FRAME: {
            auto *frame = Frame<InterpretedFrame>();
            // No-barrier read: GetCallTarget's tagged loads carry read barriers
            // that can synchronously enter CC-GC evacuation from the handler.
            Method *method = GetMethodFieldNoBarrier(frame->function.GetTaggedObject());
            return static_cast<uint32_t>(frame->GetPc() - method->GetBytecodeArray());
        }
        default:
            return 0;
    }
}

bool SignalSafeProfilerFrameIterator::IsLeaveFrame() const
{
    FrameType type = GetType();
    return type == FrameType::LEAVE_FRAME || type == FrameType::LEAVE_FRAME_WITH_ARGV;
}

bool SignalSafeProfilerFrameIterator::IsOptimizedJSFunctionFrame() const
{
    FrameType type = GetType();
    return type == FrameType::OPTIMIZED_JS_FUNCTION_FRAME ||
        type == FrameType::OPTIMIZED_JS_FAST_CALL_FUNCTION_FRAME;
}

bool SignalSafeProfilerFrameIterator::IsFastJitFunctionFrame() const
{
    FrameType type = GetType();
    return type == FrameType::FASTJIT_FUNCTION_FRAME ||
        type == FrameType::FASTJIT_FAST_CALL_FUNCTION_FRAME;
}

bool SignalSafeProfilerFrameIterator::IsSteedFunctionFrame() const
{
    return GetType() == FrameType::STEED_FUNCTION_FRAME;
}
} // namespace panda::ecmascript
