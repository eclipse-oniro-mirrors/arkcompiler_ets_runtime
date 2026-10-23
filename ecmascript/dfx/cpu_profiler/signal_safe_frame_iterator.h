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

#ifndef ECMASCRIPT_DFX_CPU_PROFILER_SIGNAL_SAFE_FRAME_ITERATOR_H
#define ECMASCRIPT_DFX_CPU_PROFILER_SIGNAL_SAFE_FRAME_ITERATOR_H

#include "ecmascript/frames.h"
#include "ecmascript/js_thread.h"

namespace panda::ecmascript {
// Dedicated frame iterator for the SIGPROF handler (plan §6.4). It reuses the frame
// LAYOUT structs (pure inline address computations) but never calls the general
// FrameIterator, whose future changes could introduce locks, GC checks, logging or
// metadata access onto the signal path.
//
// Hardening beyond the general iterator:
//   - every walked sp is validated against the thread stack bounds and tag alignment;
//   - sp must strictly increase frame over frame (no loops, no bad chains);
//   - unknown frame types stop the walk instead of trapping (no UNREACHABLE/FATAL);
//   - the loop is bounded by the caller (RAW_MAX_STACK_SIZE).
//
// Every member is a plain memory read; no allocation, no locks, no logging.
class SignalSafeProfilerFrameIterator {
public:
    SignalSafeProfilerFrameIterator(JSTaggedType *sp, const JSThread *thread);

    bool Done() const
    {
        return current_ == nullptr || !valid_;
    }

    bool IsValid() const
    {
        return valid_;
    }

    // Advances to the caller frame; invalidates the iterator on any check failure.
    void Advance();

    FrameType GetType() const;

    // JSTaggedValue::Undefined() for frames that carry no function slot.
    JSTaggedValue GetFunction() const;

    // Resolves the executing Method of a JS frame; nullptr for non-JS frames.
    Method *GetMethod() const;

    // Interpreter frames only; 0 for other kinds.
    uint32_t GetBytecodeOffset() const;

    bool IsLeaveFrame() const;
    bool IsOptimizedJSFunctionFrame() const;
    bool IsFastJitFunctionFrame() const;
    bool IsSteedFunctionFrame() const;

private:
    template<class T>
    T *Frame() const
    {
        return T::GetFrameFromSp(current_);
    }

    bool ValidateNextSp(JSTaggedType *next);
    // Advance helpers split the frame-type switch into two families so each
    // stays within complexity limits; each resolves the caller's frame pointer
    // for its family and returns false for types it does not own.
    static bool PrevFrameSpCode(FrameType type, JSTaggedType *sp, JSTaggedType **next);
    static bool PrevFrameSpInterpreter(FrameType type, JSTaggedType *sp, JSTaggedType **next);

    JSTaggedType *current_ {nullptr};
    const JSThread *thread_ {nullptr};
    bool valid_ {true};
};
} // namespace panda::ecmascript
#endif // ECMASCRIPT_DFX_CPU_PROFILER_SIGNAL_SAFE_FRAME_ITERATOR_H
