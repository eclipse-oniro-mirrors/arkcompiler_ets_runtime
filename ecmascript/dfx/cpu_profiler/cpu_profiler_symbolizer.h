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

#ifndef ECMASCRIPT_DFX_CPU_PROFILER_CPU_PROFILER_SYMBOLIZER_H
#define ECMASCRIPT_DFX_CPU_PROFILER_CPU_PROFILER_SYMBOLIZER_H

#include "ecmascript/dfx/cpu_profiler/profiler_metadata_registry.h"
#include "ecmascript/dfx/cpu_profiler/raw_sample.h"
#include "ecmascript/dfx/stackinfo/js_stackgetter.h"
#include "ecmascript/mem/c_containers.h"

namespace panda::ecmascript {
// Processor-thread-only symbolization of raw samples (plan §7.1). Nothing here may
// ever run in the SIGPROF handler: every method may allocate, take locks or touch
// lazily-initialized metadata singletons. The metadata cursor and live-file set
// implement the sample-sequence alignment of plan §7.2: events are applied in
// order until the view matches the sample's capture-time sequence, and frames
// owned by files that were not live at that sequence are rejected.
class CpuProfilerSymbolizer {
public:
    CpuProfilerSymbolizer() = default;
    ~CpuProfilerSymbolizer() = default;

    NO_COPY_SEMANTIC(CpuProfilerSymbolizer);
    NO_MOVE_SEMANTIC(CpuProfilerSymbolizer);

    // Control thread, at session start.
    void BeginSession();

    // Control thread, while enumerating files loaded before the session.
    void SeedLiveFile(const JSPandaFile *file);

    // Processor thread: apply one metadata event (sequence order is the caller's
    // responsibility, enforced by ConsumeRawSamples).
    void ApplyEvent(const ProfilerMetadataRegistry::Event &event);

    // Number of events already applied; GetEvents(cursor) resumes after them.
    uint64_t MetadataCursor() const
    {
        return metadataCursor_;
    }

    void AdvanceCursor(uint64_t appliedCount)
    {
        metadataCursor_ += appliedCount;
    }

    // True when the file was live at the sample's metadata sequence.
    bool IsJSPandaFileLive(uintptr_t jsPandaFile) const;

    // Call line of a native frame, resolved from the caller frame captured in the
    // same raw sample (mirrors the old JsStackGetter::GetCallLineNumber flow).
    int ResolveNativeCallLine(const RawSample &sample, uint16_t frameIndex) const;

    // Fills codeEntry for one raw frame; false when the frame cannot be resolved
    // (the caller then skips the frame).
    bool SymbolizeFrame(const RawSample &sample, uint16_t frameIndex,
                        struct MethodKey &methodKey, FrameInfoTemp &codeEntry);

private:
    bool SymbolizeBuiltinFrame(const RawSample &sample, uint16_t frameIndex,
                               struct MethodKey &methodKey, FrameInfoTemp &codeEntry);
    bool SymbolizeBytecodeFrame(const RawFrame &rawFrame, FrameInfoTemp &codeEntry);
    bool FillCallerSource(const RawSample &sample, uint16_t frameIndex, FrameInfoTemp &codeEntry);

    uint64_t metadataCursor_ {0};
    CUnorderedSet<const JSPandaFile *> liveFiles_;
};
} // namespace panda::ecmascript
#endif // ECMASCRIPT_DFX_CPU_PROFILER_CPU_PROFILER_SYMBOLIZER_H
