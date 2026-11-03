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
#include "ecmascript/log_wrapper.h"
#include "securec.h"

// LZ4 header included only in .cpp to avoid exposing it to other components
#include "lz4frame.h"

namespace panda::ecmascript {

// Helper casts between void* and LZ4 types (details hidden from header)
static inline LZ4F_cctx* ToCctx(void* p)
{
    return static_cast<LZ4F_cctx*>(p);
}
static inline LZ4F_preferences_t* ToPrefs(void* p)
{
    return static_cast<LZ4F_preferences_t*>(p);
}

// Statistics display
static constexpr double PERCENT_MULTIPLIER = 100.0;

// LZ4 preferences
static constexpr int LZ4_AUTOFLUSH_ENABLED = 1;

CompressedStream::CompressedStream(Stream *underlyingStream, int compressionLevel)
    : underlyingStream_(underlyingStream), compressionLevel_(compressionLevel)
{
    if (underlyingStream_ == nullptr) {
        LOG_ECMA(ERROR) << "CompressedStream: underlying stream is nullptr";
        return;
    }

    // Allocate preferences on heap to avoid exposing LZ4F_preferences_t in header
    preferences_ = new LZ4F_preferences_t();

    compressionStartTime_ = std::chrono::high_resolution_clock::now();
    chunkCompressCount_ = 0;

    if (!InitializeCompressor()) {
        LOG_ECMA(ERROR) << "CompressedStream: failed to initialize compressor";
    }
}

CompressedStream::~CompressedStream()
{
    EndOfStream();
    delete ToPrefs(preferences_);
    preferences_ = nullptr;
}

bool CompressedStream::InitializeCompressor()
{
    LZ4F_cctx* cctx = nullptr;
    LZ4F_errorCode_t errorCode = LZ4F_createCompressionContext(&cctx, LZ4F_VERSION);
    compressionContext_ = cctx;
    if (LZ4F_isError(errorCode)) {
        LOG_ECMA(ERROR) << "CompressedStream: LZ4F_createCompressionContext failed: "
                       << LZ4F_getErrorName(errorCode);
        compressionContext_ = nullptr;
        return false;
    }

    LZ4F_preferences_t* prefs = ToPrefs(preferences_);
    if (memset_s(prefs, sizeof(LZ4F_preferences_t), 0, sizeof(LZ4F_preferences_t)) != EOK) {
        LOG_ECMA(ERROR) << "CompressedStream: memset_s preferences failed";
        LZ4F_freeCompressionContext(ToCctx(compressionContext_));
        compressionContext_ = nullptr;
        return false;
    }

    prefs->compressionLevel = compressionLevel_;
    prefs->frameInfo.blockSizeID = LZ4F_max4MB;
    prefs->frameInfo.blockMode = LZ4F_blockIndependent;
    prefs->frameInfo.contentChecksumFlag = LZ4F_contentChecksumEnabled;
    prefs->autoFlush = LZ4_AUTOFLUSH_ENABLED;

    // Calculate compressed buffer size based on max chunk size
    // LZ4F_compressFrameBound returns the maximum compressed size for a given input,
    // which can be larger than the input when data is incompressible.
    compressedBufferSize_ = LZ4F_compressFrameBound(MAX_CHUNK_SIZE, prefs);
    compressedBuffer_.resize(compressedBufferSize_);

    // Write frame header
    cctx = ToCctx(compressionContext_);
    size_t headerSize = LZ4F_compressBegin(cctx, compressedBuffer_.data(),
                                           compressedBuffer_.size(), prefs);
    if (LZ4F_isError(headerSize)) {
        LOG_ECMA(ERROR) << "CompressedStream: LZ4F_compressBegin failed: "
                       << LZ4F_getErrorName(headerSize);
        LZ4F_freeCompressionContext(cctx);
        compressionContext_ = nullptr;
        return false;
    }

    // Send frame header as the first chunk
    if (!WriteCompressedDataToUnderlyingStream(compressedBuffer_.data(), headerSize)) {
        LOG_ECMA(ERROR) << "CompressedStream: failed to write frame header";
        LZ4F_freeCompressionContext(cctx);
        compressionContext_ = nullptr;
        return false;
    }

    compressionInitialized_ = true;
    return true;
}

void CompressedStream::EndOfStream()
{
    FinalizeCompression();

    auto compressionEndTime = std::chrono::high_resolution_clock::now();
    auto compressionDuration = std::chrono::duration_cast<std::chrono::milliseconds>(
        compressionEndTime - compressionStartTime_);

    double compressionRatio = (totalUncompressedBytes_ > 0) ?
        (static_cast<double>(totalCompressedBytes_) * PERCENT_MULTIPLIER / totalUncompressedBytes_) : 0.0;
    double base64Overhead = (totalCompressedBytes_ > 0) ?
        (static_cast<double>(totalBase64Bytes_) / totalCompressedBytes_ - 1.0) * PERCENT_MULTIPLIER : 0.0;

    LOG_ECMA(INFO) << "CompressedStream: done — chunks=" << chunkCompressCount_
                   << " uncompressed=" << totalUncompressedBytes_
                   << " compressed=" << totalCompressedBytes_
                   << " ratio=" << compressionRatio << "%"
                   << " base64=" << totalBase64Bytes_
                   << " overhead=" << base64Overhead << "%"
                   << " duration=" << compressionDuration.count() << "ms";

    if (underlyingStream_ != nullptr) {
        underlyingStream_->EndOfStream();
    }
}

void CompressedStream::FinalizeCompression()
{
    if (!compressionInitialized_) {
        return;
    }

    LZ4F_compressOptions_t compressOpts {};
    compressOpts.stableSrc = 0;

    LZ4F_cctx* cctx = ToCctx(compressionContext_);
    size_t result = LZ4F_compressEnd(cctx, compressedBuffer_.data(),
                                     compressedBuffer_.size(), &compressOpts);
    if (LZ4F_isError(result)) {
        LOG_ECMA(ERROR) << "CompressedStream: LZ4F_compressEnd failed: "
                       << LZ4F_getErrorName(result);
    } else if (result > 0) {
        WriteCompressedDataToUnderlyingStream(compressedBuffer_.data(), result);
    }

    LZ4F_freeCompressionContext(cctx);
    compressionContext_ = nullptr;
    compressionInitialized_ = false;
}

int CompressedStream::GetSize()
{
    return static_cast<int>(MAX_CHUNK_SIZE);
}

bool CompressedStream::WriteChunk(char *data, int32_t size)
{
    if (!compressionInitialized_ || underlyingStream_ == nullptr) {
        return false;
    }

    return CompressAndWrite(data, static_cast<size_t>(size));
}

bool CompressedStream::CompressAndWrite(const char *data, size_t size)
{
    chunkCompressCount_++;
    totalUncompressedBytes_ += size;

    LZ4F_compressOptions_t compressOpts {};
    compressOpts.stableSrc = 0;

    LZ4F_cctx* cctx = ToCctx(compressionContext_);
    // compressedBuffer_ is sized for MAX_CHUNK_SIZE via
    // LZ4F_compressFrameBound in InitializeCompressor. Feed data in
    // MAX_CHUNK_SIZE slices so the compressed output never exceeds the buffer.
    size_t offset = 0;
    while (offset < size) {
        size_t chunkSize = std::min(size - offset, MAX_CHUNK_SIZE);
        size_t compressedSize = LZ4F_compressUpdate(cctx, compressedBuffer_.data(), compressedBuffer_.size(),
                                                    data + offset, chunkSize, &compressOpts);
        if (LZ4F_isError(compressedSize)) {
            LOG_ECMA(ERROR) << "CompressedStream: LZ4F_compressUpdate failed on chunk #" << chunkCompressCount_
                           << ": " << LZ4F_getErrorName(compressedSize);
            return false;
        }

        if (compressedSize > 0) {
            totalCompressedBytes_ += compressedSize;
            if (!WriteCompressedDataToUnderlyingStream(compressedBuffer_.data(), compressedSize)) {
                LOG_ECMA(ERROR) << "CompressedStream: underlying stream write failed";
                return false;
            }
        }
        offset += chunkSize;
    }

    return true;
}

bool CompressedStream::WriteCompressedDataToUnderlyingStream(const char *data, size_t size)
{
    std::string base64Data = Base64Encode(data, size);
    totalBase64Bytes_ += base64Data.size();
    return WriteToUnderlyingStream(base64Data.c_str(), base64Data.size());
}

std::string CompressedStream::Base64Encode(const char* data, size_t size)
{
    static const char* base64Chars =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    std::string result;
    int val = 0;
    int valb = -BASE64_BITS_PER_CHAR;

    for (size_t i = 0; i < size; i++) {
        unsigned char c = static_cast<unsigned char>(data[i]);
        val = (val << BITS_PER_BYTE) + c;
        valb += BITS_PER_BYTE;
        while (valb >= 0) {
            result.push_back(base64Chars[(val >> valb) & BASE64_CHAR_MASK]);
            valb -= BASE64_BITS_PER_CHAR;
        }
    }

    if (valb > -BASE64_BITS_PER_CHAR) {
        result.push_back(base64Chars[((val << BITS_PER_BYTE) >> (valb + BITS_PER_BYTE)) & BASE64_CHAR_MASK]);
    }

    while (result.size() % BASE64_BLOCK_SIZE) {
        result.push_back(BASE64_PADDING_CHAR);
    }

    return result;
}

bool CompressedStream::WriteToUnderlyingStream(const char* data, size_t size)
{
    if (underlyingStream_ == nullptr) {
        return false;
    }

    return underlyingStream_->WriteChunk(const_cast<char*>(data), static_cast<int32_t>(size));
}

bool CompressedStream::WriteExtraInfo(char *data, int32_t size)
{
    if (underlyingStream_ != nullptr) {
        return underlyingStream_->WriteExtraInfo(data, size);
    }
    return false;
}

bool CompressedStream::WriteBinBlock([[maybe_unused]] char *data, [[maybe_unused]] int32_t size)
{
    return false;
}

bool CompressedStream::Good()
{
    return compressionInitialized_ && (underlyingStream_ != nullptr) && underlyingStream_->Good();
}

void CompressedStream::UpdateHeapStats(HeapStat* data, int32_t count)
{
    if (underlyingStream_ != nullptr) {
        underlyingStream_->UpdateHeapStats(data, count);
    }
}

void CompressedStream::UpdateLastSeenObjectId(int32_t lastSeenObjectId, int64_t timeStampUs)
{
    if (underlyingStream_ != nullptr) {
        underlyingStream_->UpdateLastSeenObjectId(lastSeenObjectId, timeStampUs);
    }
}

}  // namespace panda::ecmascript
