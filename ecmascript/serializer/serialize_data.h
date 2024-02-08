/*
 * Copyright (c) 2023 Huawei Device Co., Ltd.
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

#ifndef ECMASCRIPT_SERIALIZER_SERIALIZE_DATA_H
#define ECMASCRIPT_SERIALIZER_SERIALIZE_DATA_H

#include "ecmascript/js_tagged_value-inl.h"
#include "ecmascript/mem/dyn_chunk.h"
#include "ecmascript/snapshot/mem/snapshot_env.h"

namespace panda::ecmascript {
constexpr size_t INITIAL_CAPACITY = 64;
constexpr int CAPACITY_INCREASE_RATE = 2;
enum class EncodeFlag : uint8_t {
    // 0x00~0x03 represent new object to different space:
    // 0x00: old space
    // 0x01: non movable space
    // 0x02: machine code space
    // 0x03: huge space
    NEW_OBJECT = 0x00,
    REFERENCE = 0x04,
    WEAK,
    PRIMITIVE,
    MULTI_RAW_DATA,
    ROOT_OBJECT,
    OBJECT_PROTO,
    ARRAY_BUFFER,
    TRANSFER_ARRAY_BUFFER,
    METHOD,
    NATIVE_BINDING_OBJECT,
    JS_ERROR,
    JS_REG_EXP,
    JS_FUNCTION_IN_SHARED,
    LAST
};

enum class SerializedObjectSpace : uint8_t {
    OLD_SPACE = 0,
    NON_MOVABLE_SPACE,
    MACHINE_CODE_SPACE,
    HUGE_SPACE
};

enum class SerializeType : uint8_t {
    VALUE_SERIALIZE,
    PGO_SERIALIZE
};

class SerializeData {
public:
    explicit SerializeData(JSThread *thread) : thread_(thread) {}
    ~SerializeData()
    {
        regionRemainSizeVector_.clear();
        free(buffer_);
    }
    NO_COPY_SEMANTIC(SerializeData);
    NO_MOVE_SEMANTIC(SerializeData);

    static uint8_t EncodeNewObject(SerializedObjectSpace space)
    {
        return static_cast<uint8_t>(space) | static_cast<uint8_t>(EncodeFlag::NEW_OBJECT);
    }

    static SerializedObjectSpace DecodeSpace(uint8_t type)
    {
        ASSERT(type < static_cast<uint8_t>(EncodeFlag::REFERENCE));
        return static_cast<SerializedObjectSpace>(type);
    }

    static size_t AlignUpRegionAvailableSize(size_t size)
    {
        if (size == 0) {
            return Region::GetRegionAvailableSize();
        }
        size_t regionAvailableSize = Region::GetRegionAvailableSize();
        return ((size - 1) / regionAvailableSize + 1) * regionAvailableSize; // 1: align up
    }

    bool ExpandBuffer(size_t requestedSize)
    {
        size_t newCapacity = bufferCapacity_ * CAPACITY_INCREASE_RATE;
        newCapacity = std::max(newCapacity, requestedSize);
        if (newCapacity > sizeLimit_) {
            return false;
        }
        uint8_t *newBuffer = reinterpret_cast<uint8_t *>(malloc(newCapacity));
        if (newBuffer == nullptr) {
            return false;
        }
        if (memcpy_s(newBuffer, newCapacity, buffer_, bufferSize_) != EOK) {
            LOG_FULL(ERROR) << "Failed to memcpy_s Data";
            free(newBuffer);
            return false;
        }
        free(buffer_);
        buffer_ = newBuffer;
        bufferCapacity_ = newCapacity;
        return true;
    }

    bool AllocateBuffer(size_t bytes)
    {
        // Get internal heap size
        if (sizeLimit_ == 0) {
            uint64_t heapSize = thread_->GetEcmaVM()->GetJSOptions().GetSerializerBufferSizeLimit();
            sizeLimit_ = heapSize;
        }
        size_t oldSize = bufferSize_;
        size_t newSize = oldSize + bytes;
        if (newSize > sizeLimit_) {
            return false;
        }
        if (bufferCapacity_ == 0) {
            if (bytes < INITIAL_CAPACITY) {
                buffer_ = reinterpret_cast<uint8_t *>(malloc(INITIAL_CAPACITY));
                if (buffer_ != nullptr) {
                    bufferCapacity_ = INITIAL_CAPACITY;
                    return true;
                } else {
                    return false;
                }
            } else {
                buffer_ = reinterpret_cast<uint8_t *>(malloc(bytes));
                if (buffer_ != nullptr) {
                    bufferCapacity_ = bytes;
                    return true;
                } else {
                    return false;
                }
            }
        }
        if (newSize > bufferCapacity_) {
            if (!ExpandBuffer(newSize)) {
                return false;
            }
        }
        return true;
    }

    bool RawDataEmit(const void *data, size_t length)
    {
        if (length <= 0) {
            return false;
        }
        if ((bufferSize_ + length) > bufferCapacity_) {
            if (!AllocateBuffer(length)) {
                return false;
            }
        }
        if (memcpy_s(buffer_ + bufferSize_, bufferCapacity_ - bufferSize_, data, length) != EOK) {
            LOG_FULL(ERROR) << "Failed to memcpy_s Data";
            return false;
        }
        bufferSize_ += length;
        return true;
    }

    void EmitChar(uint8_t c)
    {
        RawDataEmit(&c, U8_SIZE);
    }

    void EmitU64(uint64_t c)
    {
        RawDataEmit(reinterpret_cast<uint8_t *>(&c), U64_SIZE);
    }

    void WriteUint8(uint8_t data)
    {
        RawDataEmit(&data, 1);
    }

    uint8_t ReadUint8()
    {
        ASSERT(position_ < Size());
        return *(buffer_ + (position_++));
    }

    void WriteEncodeFlag(EncodeFlag flag)
    {
        EmitChar(static_cast<uint8_t>(flag));
    }

    void WriteUint32(uint32_t data)
    {
        RawDataEmit(reinterpret_cast<uint8_t *>(&data), U32_SIZE);
    }

    uint32_t ReadUint32()
    {
        ASSERT(position_ < Size());
        uint32_t value = *reinterpret_cast<uint32_t *>(buffer_ + position_);
        position_ += sizeof(uint32_t);
        return value;
    }

    void WriteRawData(uint8_t *data, size_t length)
    {
        RawDataEmit(data, length);
    }

    void WriteJSTaggedValue(JSTaggedValue value)
    {
        EmitU64(value.GetRawData());
    }

    void WriteJSTaggedType(JSTaggedType value)
    {
        EmitU64(value);
    }

    JSTaggedType ReadJSTaggedType()
    {
        ASSERT(position_ < Size());
        JSTaggedType value = *reinterpret_cast<uint64_t *>(buffer_ + position_);
        position_ += sizeof(JSTaggedType);
        return value;
    }

    void ReadRawData(uintptr_t addr, size_t len)
    {
        ASSERT(position_ + len <= Size());
        if (memcpy_s(reinterpret_cast<void *>(addr), len, buffer_ + position_, len) != EOK) {
            LOG_ECMA(FATAL) << "this branch is unreachable";
            UNREACHABLE();
        }
        position_ += len;
    }

    uint8_t* Data() const
    {
        return buffer_;
    }

    size_t Size() const
    {
        return bufferSize_;
    }

    size_t GetPosition() const
    {
        return position_;
    }

    void SetIncompleteData(bool incomplete)
    {
        incompleteData_ = incomplete;
    }

    bool IsIncompleteData() const
    {
        return incompleteData_;
    }

    const std::vector<size_t>& GetRegionRemainSizeVector() const
    {
        return regionRemainSizeVector_;
    }

    size_t GetOldSpaceSize() const
    {
        return oldSpaceSize_;
    }

    size_t GetNonMovableSpaceSize() const
    {
        return nonMovableSpaceSize_;
    }

    size_t GetMachineCodeSpaceSize() const
    {
        return machineCodeSpaceSize_;
    }

    void CalculateSerializedObjectSize(SerializedObjectSpace space, size_t objectSize)
    {
        switch (space) {
            case SerializedObjectSpace::OLD_SPACE:
                AlignSpaceObjectSize(oldSpaceSize_, objectSize);
                break;
            case SerializedObjectSpace::NON_MOVABLE_SPACE:
                AlignSpaceObjectSize(nonMovableSpaceSize_, objectSize);
                break;
            case SerializedObjectSpace::MACHINE_CODE_SPACE:
                AlignSpaceObjectSize(machineCodeSpaceSize_, objectSize);
                break;
            default:
                break;
        }
    }

    void AlignSpaceObjectSize(size_t &spaceSize, size_t objectSize)
    {
        size_t alignRegionSize = AlignUpRegionAvailableSize(spaceSize);
        if (UNLIKELY(spaceSize + objectSize > alignRegionSize)) {
            regionRemainSizeVector_.push_back(alignRegionSize - spaceSize);
            spaceSize = alignRegionSize;
        }
        spaceSize += objectSize;
        ASSERT(spaceSize <= SnapshotEnv::MAX_UINT_32);
    }

    void ResetPosition()
    {
        position_ = 0;
    }

private:
    static constexpr size_t U8_SIZE = 1;
    static constexpr size_t U16_SIZE = 2;
    static constexpr size_t U32_SIZE = 4;
    static constexpr size_t U64_SIZE = 8;
    JSThread *thread_;
    uint8_t *buffer_ = nullptr;
    uint64_t sizeLimit_ = 0;
    size_t bufferSize_ = 0;
    size_t bufferCapacity_ = 0;
    size_t oldSpaceSize_ {0};
    size_t nonMovableSpaceSize_ {0};
    size_t machineCodeSpaceSize_ {0};
    size_t position_ {0};
    bool incompleteData_ {false};
    std::vector<size_t> regionRemainSizeVector_;
};
}

#endif  // ECMASCRIPT_SERIALIZER_SERIALIZE_DATA_H