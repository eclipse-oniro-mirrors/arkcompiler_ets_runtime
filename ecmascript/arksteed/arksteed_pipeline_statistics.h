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

#ifndef ECMASCRIPT_ARKSTEED_ARKSTEED_PIPELINE_STATISTICS_H
#define ECMASCRIPT_ARKSTEED_ARKSTEED_PIPELINE_STATISTICS_H

#include <cctype>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <string>
#include <vector>

#include "ecmascript/log_wrapper.h"
#include "ecmascript/mem/chunk.h"
#include "ecmascript/mem/c_string.h"
#include "ecmascript/mem/cpu_clock_scope.h"
#include "libpandabase/macros.h"

namespace panda::ecmascript::arksteed {

// One record per instrumented pipeline stage (13 instrumentation points).
// durationUs/chunkDeltaBytes are back-filled by PipelineStatisticsCollector::Finish.
struct PipelineStatisticsEntry {
    const char *name {nullptr};
    uint32_t depth {0};             // 0 = top-level pass, 1 = sub-pass
    uint64_t startUs {0};           // start timestamp, microseconds since epoch
    uint64_t durationUs {0};        // back-filled on Finish
    size_t chunkBytesAtStart {0};   // task chunk allocated bytes at Start()
    size_t chunkDeltaBytes {0};     // chunk bytes allocated during this pass
};

// Per-compilation-task collector, owned by ArkSteedCompilerTask. Used from a
// single compiler thread only (no locking). Bounded: at most one entry per
// instrumentation point. enabled=false (option off): Start/Finish are never
// called, cost is one bool branch per pass scope.
class PipelineStatisticsCollector {
public:
    explicit PipelineStatisticsCollector(bool enabled) : enabled_(enabled) {}

    bool Enabled() const
    {
        return enabled_;
    }

    void AttachChunk(const Chunk *chunk)
    {
        chunk_ = chunk;
    }

    const Chunk *GetChunk() const
    {
        return chunk_;
    }

    void Start(const char *name)
    {
        entries_.push_back(PipelineStatisticsEntry {name, depth_, NowUs(), 0, ChunkAllocatedSize(), 0});
        open_.push_back(entries_.size() - 1);
        depth_++;
    }

    void Finish()
    {
        // LIFO pairing is guaranteed by ArkSteedPipelineStatistics scope nesting.
        size_t index = open_.back();
        open_.pop_back();
        depth_--;
        PipelineStatisticsEntry &entry = entries_[index];
        entry.durationUs = NowUs() - entry.startUs;
        entry.chunkDeltaBytes = ChunkAllocatedSize() - entry.chunkBytesAtStart;
    }

    const std::vector<PipelineStatisticsEntry> &Entries() const
    {
        return entries_;
    }

private:
    static uint64_t NowUs()
    {
        return CPUClockScope::NowUs();
    }

    size_t ChunkAllocatedSize() const
    {
        return chunk_ != nullptr ? chunk_->GetAllocatedSize() : 0;
    }

    bool enabled_ {false};
    uint32_t depth_ {0};
    const Chunk *chunk_ {nullptr};
    std::vector<PipelineStatisticsEntry> entries_;
    std::vector<size_t> open_;
};

// RAII scope per pipeline stage. Option off: one bool branch on construct and
// destruct, no sampling. Option on: Start() samples time and chunk bytes; the
// destructor (including early-return paths) back-fills the entry.
class ArkSteedPipelineStatistics {
public:
    ArkSteedPipelineStatistics(PipelineStatisticsCollector &collector, const char *name) : collector_(collector)
    {
        if (collector_.Enabled()) {
            collector_.Start(name);
            active_ = true;
        }
    }

    ~ArkSteedPipelineStatistics()
    {
        if (active_) {
            collector_.Finish();
        }
    }

    NO_COPY_SEMANTIC(ArkSteedPipelineStatistics);
    NO_MOVE_SEMANTIC(ArkSteedPipelineStatistics);

private:
    PipelineStatisticsCollector &collector_;
    bool active_ {false};
};

// Returns true when methodName exactly matches any entry of the comma-separated
// filter. Entries are trimmed of surrounding whitespace; empty entries are
// ignored. An empty filter matches nothing (callers treat empty as "collect all").
inline bool MatchMethodName(const std::string &csvFilter, const CString &methodName)
{
    const auto isSpace = [](char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; };
    size_t begin = 0;
    while (begin < csvFilter.size()) {
        size_t end = csvFilter.find(',', begin);
        if (end == std::string::npos) {
            end = csvFilter.size();
        }
        while (begin < end && isSpace(csvFilter[begin])) {
            begin++;
        }
        size_t last = end;
        while (last > begin && isSpace(csvFilter[last - 1])) {
            last--;
        }
        if (last > begin && csvFilter.compare(begin, last - begin, methodName.c_str()) == 0) {
            return true;
        }
        begin = end + 1;
    }
    return false;
}

// Formats the task-end summary tree (pure function, unit-testable).
// Line 0 (header):
//   [ArkSteed] Pipeline statistics for '<method>' (total: X.XXms, chunk: N[, aborted]):
// One line per entry, indented 2 * (depth + 1) spaces, in execution order:
//   <name>: X.XXms, +<delta> (cum <cumulative>)
inline std::vector<CString> FormatPipelineStatisticsSummary(const CString &methodName, float totalMs,
    size_t chunkTotalBytes, bool aborted, const std::vector<PipelineStatisticsEntry> &entries)
{
    constexpr size_t KB = 1024;
    constexpr size_t MB = 1024 * KB;
    auto formatBytes = [](size_t bytes) {
        CStringStream stream;
        if (bytes >= MB) {
            stream << std::fixed << std::setprecision(2) << (static_cast<double>(bytes) / MB) << "MB";
        } else if (bytes >= KB) {
            stream << (bytes / KB) << "KB";
        } else {
            stream << bytes << "B";
        }
        return stream.str();
    };

    std::vector<CString> lines;
    CStringStream header;
    header << "[ArkSteed] Pipeline statistics for '" << methodName << "' (total: " << std::fixed
           << std::setprecision(2) << totalMs << "ms, chunk: " << formatBytes(chunkTotalBytes);
    if (aborted) {
        header << ", aborted";
    }
    header << "):";
    lines.push_back(header.str());

    for (const auto &entry : entries) {
        CStringStream line;
        line << CString(2 * (entry.depth + 1), ' ') << entry.name << ": " << std::fixed << std::setprecision(2)
             << (static_cast<double>(entry.durationUs) / 1000.0) << "ms, +" << formatBytes(entry.chunkDeltaBytes)
             << " (cum " << formatBytes(entry.chunkBytesAtStart + entry.chunkDeltaBytes) << ")";
        lines.push_back(line.str());
    }
    return lines;
}

// RAII per compilation task. Declare in Compile() after ArkSteedCompileTimeScope
// so the summary is logged before the total compile time line. Destructor
// outputs the summary via LOG_JIT(INFO) when the option is on and the JIT log
// component is loggable at INFO; call MarkSuccess() right before returning true.
class ArkSteedPipelineStatisticsSummary : public CPUClockScope {
public:
    ArkSteedPipelineStatisticsSummary(PipelineStatisticsCollector &collector, CString methodName)
        : collector_(collector), methodName_(std::move(methodName))
    {
    }

    ~ArkSteedPipelineStatisticsSummary()
    {
        if (!collector_.Enabled()) {
            return;
        }
        if (!common::Log::LogIsLoggable(Level::INFO, Component::JIT)) {
            return;
        }
        const Chunk *chunk = collector_.GetChunk();
        size_t chunkTotalBytes = chunk != nullptr ? chunk->GetAllocatedSize() : 0;
        for (const CString &line : FormatPipelineStatisticsSummary(methodName_, TotalSpentTime(), chunkTotalBytes,
             !success_, collector_.Entries())) {
            LOG_JIT(INFO) << line;
        }
    }

    void MarkSuccess()
    {
        success_ = true;
    }

    bool IsSuccess() const
    {
        return success_;
    }

    NO_COPY_SEMANTIC(ArkSteedPipelineStatisticsSummary);
    NO_MOVE_SEMANTIC(ArkSteedPipelineStatisticsSummary);

private:
    PipelineStatisticsCollector &collector_;
    CString methodName_;
    bool success_ {false};
};

}  // namespace panda::ecmascript::arksteed
#endif  // ECMASCRIPT_ARKSTEED_ARKSTEED_PIPELINE_STATISTICS_H
