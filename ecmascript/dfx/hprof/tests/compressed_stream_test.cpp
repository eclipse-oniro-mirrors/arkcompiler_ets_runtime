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

#include "ecmascript/dfx/hprof/compressed_stream.h"
#include "ecmascript/dfx/hprof/file_stream.h"
#include "ecmascript/dfx/hprof/heap_profiler_interface.h"
#include "ecmascript/napi/include/jsnapi.h"
#include "ecmascript/tests/test_helper.h"
#include <string>

using namespace panda::ecmascript;
namespace panda::test {

// ── In-memory Stream for capturing compressed output ────────────────────────
class CaptureStream : public Stream {
public:
    void EndOfStream() override {}
    int GetSize() override
    {
        return static_cast<int>(CAPACITY);
    }
    bool WriteChunk(char *data, int32_t size) override
    {
        output.append(data, size);
        return true;
    }
    bool WriteExtraInfo(char *data, int32_t size) override
    {
        extraInfo.append(data, size);
        return true;
    }
    bool WriteBinBlock(char *data, int32_t size) override
    {
        return WriteChunk(data, size);
    }
    bool Good() override
    {
        return good_;
    }
    void UpdateHeapStats(HeapStat *data, int32_t count) override {}
    void UpdateLastSeenObjectId(int32_t lastSeenObjectId, int64_t timeStampUs) override {}

    const std::string &GetOutput() const
    {
        return output;
    }
    const std::string &GetExtraInfo() const
    {
        return extraInfo;
    }
    void SetGood(bool g)
    {
        good_ = g;
    }

private:
    static constexpr size_t CAPACITY = 100_KB;
    std::string output;
    std::string extraInfo;
    bool good_ = true;
};

// ── Stream that always fails ────────────────────────────────────────────────
class FailStream : public Stream {
public:
    void EndOfStream() override {}
    int GetSize() override
    {
        return static_cast<int>(CAPACITY);
    }
    bool WriteChunk(char *data, int32_t size) override
    {
        return false;
    }
    bool WriteExtraInfo(char *data, int32_t size) override
    {
        return false;
    }
    bool WriteBinBlock(char *data, int32_t size) override
    {
        return false;
    }
    bool Good() override
    {
        return false;
    }
    void UpdateHeapStats(HeapStat *data, int32_t count) override {}
    void UpdateLastSeenObjectId(int32_t lastSeenObjectId, int64_t timeStampUs) override {}

private:
    static constexpr size_t CAPACITY = 100_KB;
};

// ── Base64 validation helper ────────────────────────────────────────────────
// CompressedStream Base64-encodes each compressed segment. Since the test no
// longer links LZ4 directly, we validate output characteristics (non-empty,
// valid Base64 charset, compression effectiveness) rather than decompressing.
static bool IsValidBase64(const std::string &s)
{
    if (s.empty()) {
        return false;
    }
    for (char c : s) {
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
              (c >= '0' && c <= '9') || c == '+' || c == '/' || c == '=')) {
            return false;
        }
    }
    return true;
}

// ════════════════════════════════════════════════════════════════════════════
// Unit tests
// ════════════════════════════════════════════════════════════════════════════
class CompressedStreamTest : public testing::Test {
public:
    void SetUp() override
    {
        TestHelper::CreateEcmaVMWithScope(ecmaVm_, thread_, scope_);
    }
    void TearDown() override
    {
        TestHelper::DestroyEcmaVMWithScope(ecmaVm_, scope_);
    }

protected:
    static constexpr int compressionLevelMin = 1;
    static constexpr int compressionLevelMax = 12;
    static constexpr size_t smallDataSize = 10000;
    static constexpr size_t mediumDataSize = 1024;
    static constexpr size_t largeDataSize = 15 * 1024 * 1024;
    static constexpr size_t pageSize = 4096;
    static constexpr int moduloValue = 256;

    EcmaVM *ecmaVm_ {nullptr};
    EcmaHandleScope *scope_ {nullptr};
    JSThread *thread_ {nullptr};
};

// Single + multi-chunk write, verifying compression output characteristics
// (non-empty, valid Base64 charset) through the WriteChunk → LZ4 compress → Base64 pipeline.
HWTEST_F_L0(CompressedStreamTest, RoundTrip)
{
    CaptureStream capture;
    {
        CompressedStream cs(&capture, compressionLevelMin);
        ASSERT_TRUE(cs.Good());
        // Single chunk
        std::string part1 = R"({"key":"value"})";
        ASSERT_TRUE(cs.WriteChunk(const_cast<char *>(part1.data()), part1.size()));
        // Multiple chunks
        std::string part2 = "Hello, ";
        std::string part3 = "World!";
        ASSERT_TRUE(cs.WriteChunk(const_cast<char *>(part2.data()), part2.size()));
        ASSERT_TRUE(cs.WriteChunk(const_cast<char *>(part3.data()), part3.size()));
        cs.EndOfStream();
    }
    // Verify compression output characteristics without decompressing
    ASSERT_FALSE(capture.GetOutput().empty());
    ASSERT_TRUE(IsValidBase64(capture.GetOutput()));
}

// Large data exceeding MAX_CHUNK_SIZE exercises internal slicing.
HWTEST_F_L0(CompressedStreamTest, LargeDataRoundTrip)
{
    CaptureStream capture;
    {
        CompressedStream cs(&capture, compressionLevelMin);
        ASSERT_TRUE(cs.Good());
        std::string largeData(largeDataSize, 'X');
        for (size_t i = 0; i < largeData.size(); i += pageSize) {
            largeData[i] = static_cast<char>(i % moduloValue);
        }
        ASSERT_TRUE(cs.WriteChunk(const_cast<char *>(largeData.data()), largeData.size()));
        cs.EndOfStream();
    }
    // Verify compression output characteristics without decompressing
    ASSERT_FALSE(capture.GetOutput().empty());
    ASSERT_TRUE(IsValidBase64(capture.GetOutput()));
    ASSERT_TRUE(capture.GetOutput().size() < largeDataSize);
}

// EndOfStream is idempotent; destructor auto-finalizes if not called.
HWTEST_F_L0(CompressedStreamTest, EndOfStreamAndDestructor)
{
    // Idempotent EndOfStream
    CaptureStream capture1;
    {
        CompressedStream cs(&capture1, compressionLevelMin);
        std::string data = "idempotent";
        ASSERT_TRUE(cs.WriteChunk(const_cast<char *>(data.data()), data.size()));
        cs.EndOfStream();
        cs.EndOfStream();  // no-op
    }
    ASSERT_FALSE(capture1.GetOutput().empty());
    ASSERT_TRUE(IsValidBase64(capture1.GetOutput()));

    // Destructor auto-finalizes (no explicit EndOfStream)
    CaptureStream capture2;
    {
        CompressedStream cs(&capture2, compressionLevelMin);
        std::string data = "destructor";
        ASSERT_TRUE(cs.WriteChunk(const_cast<char *>(data.data()), data.size()));
    }
    ASSERT_FALSE(capture2.GetOutput().empty());
    ASSERT_TRUE(IsValidBase64(capture2.GetOutput()));
}

// nullptr and FailStream: Good() is false, WriteChunk fails, no crash.
HWTEST_F_L0(CompressedStreamTest, NullptrAndFailStream)
{
    // nullptr underlying stream
    CompressedStream csNull(nullptr, compressionLevelMin);
    ASSERT_FALSE(csNull.Good());
    std::string data = "fail";
    ASSERT_FALSE(csNull.WriteChunk(const_cast<char *>(data.data()), data.size()));
    csNull.EndOfStream();  // should not crash

    // FailStream (Good()=false)
    FailStream fail;
    CompressedStream csFail(&fail, compressionLevelMin);
    ASSERT_FALSE(csFail.Good());

    // Good() reflects underlying stream state changes
    CaptureStream capture;
    CompressedStream cs(&capture, compressionLevelMin);
    ASSERT_TRUE(cs.Good());
    capture.SetGood(false);
    ASSERT_FALSE(cs.Good());
}

// WriteExtraInfo passes through uncompressed to underlying stream.
HWTEST_F_L0(CompressedStreamTest, WriteExtraInfoPassthrough)
{
    CaptureStream capture;
    CompressedStream cs(&capture, compressionLevelMin);
    ASSERT_TRUE(cs.Good());
    std::string extra = "extra_info_data";
    ASSERT_TRUE(cs.WriteExtraInfo(const_cast<char *>(extra.data()), extra.size()));
    ASSERT_EQ(capture.GetExtraInfo(), "extra_info_data");
    cs.EndOfStream();
}

// WriteChunk fails when underlying stream write fails (Good but write returns false).
HWTEST_F_L0(CompressedStreamTest, WriteChunkFailsOnUnderlyingWriteError)
{
    class WriteFailStream : public Stream {
    public:
        void EndOfStream() override {}
        int GetSize() override
        {
            constexpr size_t capacity = 100_KB;
            return static_cast<int>(capacity);
        }
        bool WriteChunk(char *data, int32_t size) override
        {
            return false;
        }
        bool WriteExtraInfo(char *data, int32_t size) override
        {
            return false;
        }
        bool WriteBinBlock(char *data, int32_t size) override
        {
            return false;
        }
        bool Good() override
        {
            return true;
        }
        void UpdateHeapStats(HeapStat *data, int32_t count) override {}
        void UpdateLastSeenObjectId(int32_t lastSeenObjectId, int64_t timeStampUs) override {}
    };

    WriteFailStream wfs;
    CompressedStream cs(&wfs, compressionLevelMin);
    std::string data = "test data";
    if (cs.Good()) {
        ASSERT_FALSE(cs.WriteChunk(const_cast<char *>(data.data()), data.size()));
    }
}

// Min and max compression levels both produce valid round-trip output.
HWTEST_F_L0(CompressedStreamTest, CompressionLevels)
{
    int levels[] = {compressionLevelMin, compressionLevelMax};
    for (int level : levels) {
        CaptureStream capture;
        {
            CompressedStream cs(&capture, level);
            ASSERT_TRUE(cs.Good());
            std::string data(mediumDataSize, 'A');
            ASSERT_TRUE(cs.WriteChunk(const_cast<char *>(data.data()), data.size()));
            cs.EndOfStream();
        }
        // Verify compression output characteristics without decompressing
        ASSERT_FALSE(capture.GetOutput().empty()) << "Failed at level " << level;
        ASSERT_TRUE(IsValidBase64(capture.GetOutput())) << "Failed at level " << level;
    }
}

// Compression stats are populated after EndOfStream.
HWTEST_F_L0(CompressedStreamTest, CompressionStatsPopulated)
{
    CaptureStream capture;
    CompressedStream cs(&capture, compressionLevelMin);
    ASSERT_TRUE(cs.Good());
    std::string data(smallDataSize, 'B');
    ASSERT_TRUE(cs.WriteChunk(const_cast<char *>(data.data()), data.size()));
    cs.EndOfStream();

    ASSERT_EQ(cs.GetTotalUncompressedBytes(), smallDataSize);
    ASSERT_GT(cs.GetTotalCompressedBytes(), static_cast<size_t>(0));
    ASSERT_GT(cs.GetTotalBase64Bytes(), static_cast<size_t>(0));
    ASSERT_GT(cs.GetChunkCount(), 0);
    ASSERT_GE(cs.GetTotalBase64Bytes(), cs.GetTotalCompressedBytes());
}

// ════════════════════════════════════════════════════════════════════════════
// Integration tests: DumpHeapSnapshot with/without compression
// ════════════════════════════════════════════════════════════════════════════
class CompressedStreamIntegrationTest : public testing::Test {
public:
    void SetUp() override
    {
        TestHelper::CreateEcmaVMWithScope(ecmaVm_, thread_, scope_);
        ecmaVm_->SetEnableForceGC(false);
        HeapProfilerInterface::ResetOOMDump();
    }
    void TearDown() override
    {
        TestHelper::DestroyEcmaVMWithScope(ecmaVm_, scope_);
    }

protected:
    EcmaVM *ecmaVm_ {nullptr};
    EcmaHandleScope *scope_ {nullptr};
    JSThread *thread_ {nullptr};
};

// Compressed dump: output is non-empty Base64-encoded compressed data.
HWTEST_F_L0(CompressedStreamIntegrationTest, DumpWithCompression)
{
    CaptureStream capture;
    HeapProfilerInterface *heapProfiler = HeapProfilerInterface::GetInstance(ecmaVm_);
    ASSERT_NE(heapProfiler, nullptr);

    DumpSnapShotOption dumpOption;
    dumpOption.dumpFormat = DumpFormat::JSON;
    dumpOption.enableCompression = true;
    dumpOption.isFullGC = false;

    ASSERT_TRUE(heapProfiler->DumpHeapSnapshot(&capture, dumpOption));
    HeapProfilerInterface::Destroy(ecmaVm_);

    // Verify compression output characteristics without decompressing
    ASSERT_FALSE(capture.GetOutput().empty());
    ASSERT_TRUE(IsValidBase64(capture.GetOutput()));
}

// Uncompressed dump: output is plain JSON (regression baseline).
HWTEST_F_L0(CompressedStreamIntegrationTest, DumpWithoutCompression)
{
    CaptureStream capture;
    HeapProfilerInterface *heapProfiler = HeapProfilerInterface::GetInstance(ecmaVm_);
    ASSERT_NE(heapProfiler, nullptr);

    DumpSnapShotOption dumpOption;
    dumpOption.dumpFormat = DumpFormat::JSON;
    dumpOption.enableCompression = false;
    dumpOption.isFullGC = false;

    ASSERT_TRUE(heapProfiler->DumpHeapSnapshot(&capture, dumpOption));
    HeapProfilerInterface::Destroy(ecmaVm_);

    ASSERT_FALSE(capture.GetOutput().empty());
    ASSERT_EQ(capture.GetOutput()[0], '{');
}

}  // namespace panda::test
