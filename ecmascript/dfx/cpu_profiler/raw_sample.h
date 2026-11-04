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

#ifndef ECMASCRIPT_DFX_CPU_PROFILER_RAW_SAMPLE_H
#define ECMASCRIPT_DFX_CPU_PROFILER_RAW_SAMPLE_H

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace panda::ecmascript {
// Maximum number of frames captured by the signal handler in one sample.
// Must equal MAX_STACK_SIZE in samples_record.h; cpu_profiler.cpp static_asserts the match.
constexpr uint16_t RAW_MAX_STACK_SIZE = 128; // 128:the maximum size of the js stack

// Classification of a raw frame, derived from frame/method metadata in the signal handler.
// It only decides how the processor symbolizes the frame; the profile-facing running state
// is carried separately by RawRunningState.
enum class RawFrameKind : uint8_t {
    INTERPRETER = 0,
    AOT,
    JIT,
    BUILTIN,
    NAPI,
    RUNTIME,
    HYBRID_STATIC,
    UNKNOWN,
};

// Mirrors RunningState values (js_stackgetter.h). Kept as a separate lean enum so this
// header stays includable from the signal-handler path without pulling VM headers.
// ToRunningState() in cpu_profiler.cpp performs the explicit mapping.
enum class RawRunningState : uint8_t {
    OTHER = 0,
    GC,
    CINT,
    AINT,
    AOT,
    AINT_D,
    BUILTIN,
    NAPI,
    ARKUI_ENGINE,
    RUNTIME,
    JIT,
};

// Outcome of one sampling attempt. Values recorded inside the sample (as opposed to
// counters) describe why a partially captured sample must be dropped by the processor.
enum class SampleStatus : uint8_t {
    OK = 0,
    GC_SKIPPED,
    STACK_NOT_ITERABLE,
    INVALID_FRAME,
    METADATA_STALE,           // processor-side drop reason; never written into a sample
                              // (detected via IsJSPandaFileLive in ProcessRawSample)
    UNSUPPORTED_HYBRID_FRAME, // handler-set: hybrid frames were captured via the
                              // legacy channel; the processor drops this raw slot
};

// A single frame captured in the signal handler. Trivially copyable, fixed width, owns
// nothing. Fields are read from the interrupted stack/heap with plain loads only.
struct RawFrame {
    // INTERPRETER/AOT/JIT: MethodLiteral* of the executing method (stable identity).
    // NAPI/BUILTIN: raw native callback address.
    uintptr_t value {0};
    // INTERPRETER/AOT/JIT only: JSPandaFile* owning the MethodLiteral; 0 for native kinds.
    uintptr_t jsPandaFile {0};
    // INTERPRETER only: bytecode offset of this frame; also used to resolve the call
    // line of the callee native frame during processor-side symbolization.
    uint32_t bytecodeOffset {0};
    RawFrameKind kind {RawFrameKind::UNKNOWN};
    RawRunningState state {RawRunningState::OTHER};
    // kungfu::DeoptType value (enum underlying type is uint8_t).
    uint8_t deoptType {0};
    uint8_t reserved {0};
};

// One sample produced by the signal handler and consumed by the sampling processor.
// The per-frame running state lives in RawFrame::state; frames[0] is the top frame.
struct RawSample {
    // Request timestamp taken by the sending thread (approximate interrupt time).
    uint64_t timestamp {0};
    // Reserved for the metadata event sequence (plan §5.4); Phase 1 keeps 0.
    uint64_t metadataSequence {0};
    // OS thread id (or FFRT cached task id) that produced this sample.
    uint64_t threadOrTaskId {0};
    // Profiler session generation, used to reject samples from a stale session.
    uint32_t profilerGeneration {0};
    uint16_t frameCount {0};
    // True while the timestamp comes from the sending thread (plan §5.2 H1 default).
    bool timestampIsApproximate {true};
    SampleStatus status {SampleStatus::OK};
    RawFrame frames[RAW_MAX_STACK_SIZE] {};
};

// The signal handler only touches types that are trivially copyable/destructible so
// queue slots can be published with release/acquire atomics and placement writes.
static_assert(std::is_standard_layout_v<RawFrame>);
static_assert(std::is_trivially_copyable_v<RawFrame>);
static_assert(std::is_trivially_destructible_v<RawFrame>);
static_assert(std::is_standard_layout_v<RawSample>);
static_assert(std::is_trivially_copyable_v<RawSample>);
static_assert(std::is_trivially_destructible_v<RawSample>);

// Atomics used on the handler path must be native lock-free implementations; a
// libatomic fallback would reintroduce hidden locking inside the signal handler.
static_assert(std::atomic<uint8_t>::is_always_lock_free);
static_assert(std::atomic<bool>::is_always_lock_free);
static_assert(std::atomic<uint16_t>::is_always_lock_free);
static_assert(std::atomic<uint32_t>::is_always_lock_free);
static_assert(std::atomic<uint64_t>::is_always_lock_free);
static_assert(std::atomic<uintptr_t>::is_always_lock_free);
} // namespace panda::ecmascript
#endif // ECMASCRIPT_DFX_CPU_PROFILER_RAW_SAMPLE_H
