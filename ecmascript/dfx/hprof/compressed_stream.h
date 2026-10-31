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

#ifndef ECMASCRIPT_DFX_HPROF_COMPRESSED_STREAM_H
#define ECMASCRIPT_DFX_HPROF_COMPRESSED_STREAM_H

#if defined(ENABLE_LZ4_COMPRESSED)

#include "ecmascript/dfx/hprof/stream.h"
#include "ecmascript/mem/mem_common.h"
#include <chrono>
#include <vector>
#include <memory>
#include <string>

namespace panda::ecmascript {

// ── Shared constants for Base64 encoding / LZ4 compression ──────────────────
static constexpr int BASE64_BITS_PER_CHAR = 6;
static constexpr int BITS_PER_BYTE = 8;
static constexpr unsigned int BASE64_CHAR_MASK = 0x3F;
static constexpr int BASE64_ALPHABET_SIZE = 64;
static constexpr size_t BASE64_BLOCK_SIZE = 4;
static constexpr char BASE64_PADDING_CHAR = '=';
static constexpr size_t DECOMPRESS_CHUNK_SIZE = 64 * 1024;

// CompressedStream wraps a text/JSON Stream (e.g. FileStream) and transparently
// LZ4-compresses + Base64-encodes data written via WriteChunk.
//
// IMPORTANT: This class must NOT be used with binary rawheap dumps.
// BinaryWriter calls WriteBinBlock to emit raw binary records; CompressedStream
// treats WriteBinBlock as a fatal error (LOG_FULL(FATAL)) because binary data
// cannot be compressed+base64-encoded without corrupting the rawheap wire format.
// Binary dumps should always go through BinaryDump() which never wraps the
// stream with CompressedStream.
class CompressedStream : public Stream {
public:
explicit CompressedStream(Stream *underlyingStream,
                                int compressionLevel = 1);
    ~CompressedStream() override;

    void EndOfStream() override;

    int GetSize() override;

    bool WriteChunk(char *data, int32_t size) override;
    bool WriteExtraInfo(char *data, int32_t size) override;
    bool WriteBinBlock(char *data, int32_t size) override;
    bool Good() override;
    void UpdateHeapStats(HeapStat* data, int32_t count) override;
    void UpdateLastSeenObjectId(int32_t lastSeenObjectId, int64_t timeStampUs) override;

    size_t GetTotalUncompressedBytes() const { return totalUncompressedBytes_; }
    size_t GetTotalCompressedBytes() const { return totalCompressedBytes_; }
    size_t GetTotalBase64Bytes() const { return totalBase64Bytes_; }
    int GetChunkCount() const { return chunkCompressCount_; }

    // Maximum uncompressed bytes fed to LZ4 in a single WriteChunk/slice.
    // Drives: GetSize() (chunk size advertised to upstream serializer),
    // compressedBuffer_ sizing (LZ4F_compressFrameBound), and the slicing
    // loop in CompressAndWrite. Keep these three uses in sync via this
    // single constant to avoid buffer overrun.
    static constexpr size_t MAX_CHUNK_SIZE = static_cast<size_t>(8_MB);

private:
    bool InitializeCompressor();
    void FinalizeCompression();
    bool CompressAndWrite(const char *data, size_t size);
    bool WriteCompressedDataToUnderlyingStream(const char *data, size_t size);

    std::string Base64Encode(const char* data, size_t size);
    bool WriteToUnderlyingStream(const char* data, size_t size);

    Stream *underlyingStream_ {nullptr};
    // LZ4 compression context, defined in .cpp to avoid exposing lz4frame.h in header
    void* compressionContext_ {nullptr};
    // LZ4 preferences, allocated in .cpp to avoid exposing lz4frame.h in header
    void* preferences_ {nullptr};
    int compressionLevel_ {1};

    std::vector<char> compressedBuffer_;
    size_t compressedBufferSize_ {0};

    size_t totalUncompressedBytes_ {0};
    size_t totalCompressedBytes_ {0};
    size_t totalBase64Bytes_ {0};

    bool compressionInitialized_ {false};

    int chunkCompressCount_ {0};
    std::chrono::high_resolution_clock::time_point compressionStartTime_ {};
};

}  // namespace panda::ecmascript

#endif  // ENABLE_LZ4_COMPRESSED

#endif  // ECMASCRIPT_DFX_HPROF_COMPRESSED_STREAM_H
