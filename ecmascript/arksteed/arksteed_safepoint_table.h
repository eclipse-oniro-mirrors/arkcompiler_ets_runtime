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

#ifndef ECMASCRIPT_ARKSTEED_SAFEPOINT_TABLE_H
#define ECMASCRIPT_ARKSTEED_SAFEPOINT_TABLE_H

#include <memory>
#include <optional>
#include <vector>

#include "ecmascript/mem/chunk_containers.h"
#include "ecmascript/stackmap/ark_stackmap.h"

namespace panda::ecmascript {
    class MachineCode;
}

namespace panda::ecmascript::arksteed {

enum class ExceptionHandlerKind : uint16_t {
    NONE = 0,
    COMPILED_CATCH = 1,
    LAZY_DEOPT = 2,
};

enum class ArkSteedDeoptValueKind : uint8_t {
    CONSTANT = 0,
    STACK_SLOT = 1,
    HEAP_LITERAL = 2,
};

struct ArkSteedDeoptValue {
    kungfu::LLVMStackMapType::VRegId id;
    ArkSteedDeoptValueKind kind;
    int64_t value;
    kungfu::LLVMStackMapType::DwarfRegType reg {kungfu::LLVMStackMapType::INVALID_DWARF_REG};
};

// ArkSteed-style safepoint table
//
// Binary layout:
//   Header (16 bytes):
//     uint32_t numEntries
//     uint32_t numTaggedSlots      (function-level, same for all safepoints)
//     uint32_t numUntaggedSlots
//     uint32_t deoptLiteralCount   (exact number of entries in MachineCode's deopt literal table)
//   Entry[] (12 bytes each, sorted by pcOffset ascending):
//     uint32_t pcOffset            (return address offset from code start)
//     uint16_t extraSpillSlotsAndFlags
//                              (low 13 bits: pushed stack slots; bits 13-14: exception handler kind)
//     uint32_t deoptOffset         (relative to the table start; 0 if absent)
//     uint16_t deoptNum            (legacy logical count: two units per vreg entry)
// GC scanning:
//   1. All tagged stack slots (FP-relative) are roots at every safepoint
//   2. Per-safepoint: outgoing stack arguments are roots until the call returns

struct ArkSteedSafepointHeader {
    uint32_t numEntries;
    uint32_t numTaggedSlots;
    uint32_t numUntaggedSlots;
    uint32_t deoptLiteralCount;
};

struct ArkSteedSafepointEntry {
    static constexpr uint16_t EXCEPTION_HANDLER_KIND_SHIFT = 13U;
    static constexpr uint16_t EXCEPTION_HANDLER_KIND_MASK = 0x6000U;
    static constexpr uint16_t EXTRA_SPILL_SLOTS_MASK = 0x1FFFU;
    static constexpr uint16_t ENTRY_FLAGS_MASK = EXCEPTION_HANDLER_KIND_MASK;

    uint32_t pcOffset;
    uint32_t deoptOffset;
    uint16_t extraSpillSlotsAndFlags;
    uint16_t deoptNum;

    uint16_t GetNumExtraSpillSlots() const
    {
        return extraSpillSlotsAndFlags & EXTRA_SPILL_SLOTS_MASK;
    }

    ExceptionHandlerKind GetExceptionHandlerKind() const
    {
        uint16_t kind = static_cast<uint16_t>((extraSpillSlotsAndFlags & EXCEPTION_HANDLER_KIND_MASK) >>
                                              EXCEPTION_HANDLER_KIND_SHIFT);
        ASSERT(kind <= static_cast<uint16_t>(ExceptionHandlerKind::LAZY_DEOPT));
        return static_cast<ExceptionHandlerKind>(kind);
    }
};

static_assert(sizeof(ArkSteedSafepointHeader) == 16, "Header must be 16 bytes");  // 16: header size in bytes
static_assert(sizeof(ArkSteedSafepointEntry) == 12, "Entry must be 12 bytes");    // 12: entry size in bytes
// ============================================================================
// Builder — used during compilation to collect safepoint entries
// ============================================================================

class PUBLIC_API ArkSteedSafepointTableBuilder {
public:
    explicit ArkSteedSafepointTableBuilder(Chunk *chunk) : chunk_(chunk), entries_(chunk), encodedDeoptData_(chunk) {}
    ~ArkSteedSafepointTableBuilder() = default;

    class Safepoint {
    public:
        void SetNumExtraSpillSlots(uint32_t count)
        {
            ASSERT(count <= ArkSteedSafepointEntry::EXTRA_SPILL_SLOTS_MASK);
            entry_->extraSpillSlotsAndFlags = static_cast<uint16_t>(
                (entry_->extraSpillSlotsAndFlags & ArkSteedSafepointEntry::ENTRY_FLAGS_MASK) | count);
        }

    private:
        friend class ArkSteedSafepointTableBuilder;
        explicit Safepoint(ArkSteedSafepointEntry *entry) : entry_(entry) {}
        ArkSteedSafepointEntry *entry_;
    };

    Safepoint DefineSafepoint(uint32_t pcOffset);
    void DefineDeoptSafepoint(uint32_t pcOffset, std::vector<ArkSteedDeoptValue> deopts,
                              ExceptionHandlerKind exceptionHandlerKind = ExceptionHandlerKind::NONE);
    void SetFrameSlots(uint32_t tagged, uint32_t untagged);
    void SetDeoptLiteralCount(uint32_t count)
    {
        deoptLiteralCount_ = count;
    }

    size_t GetTableSize() const;
    void Emit(uint8_t *buffer) const;
    uint8_t *EmitToNewBuffer() const;

    std::string DumpMemoryUsage() const;

    uint32_t GetNumEntries() const
    {
        return static_cast<uint32_t>(entries_.size());
    }

private:
    Chunk *chunk_;
    uint32_t numTaggedSlots_ = 0;
    uint32_t numUntaggedSlots_ = 0;
    uint32_t deoptLiteralCount_ = 0;
    ChunkVector<ArkSteedSafepointEntry> entries_;
    ChunkVector<ChunkVector<uint8_t>> encodedDeoptData_;
};

// ============================================================================
// Table Reader — used at runtime (GC) to look up safepoint entries
// ============================================================================

class ArkSteedSafepointTable {
public:
    ArkSteedSafepointTable(const uint8_t *data, size_t size);

    uint32_t GetNumTaggedSlots() const
    {
        return header_->numTaggedSlots;
    }
    uint32_t GetNumUntaggedSlots() const
    {
        return header_->numUntaggedSlots;
    }
    uint32_t GetNumEntries() const
    {
        return header_->numEntries;
    }

    uint32_t GetDeoptLiteralCount() const
    {
        return header_->deoptLiteralCount;
    }

    const ArkSteedSafepointEntry *FindEntry(uint32_t pcOffset) const;
    bool GetDeoptInfo(uint32_t pcOffset, const uint64_t *deoptLiterals, uint32_t deoptLiteralCount,
                      std::vector<kungfu::ARKDeopt> &deopts) const;
    ExceptionHandlerKind GetExceptionHandlerKind(uint32_t pcOffset) const;

    bool IsValid() const
    {
        return header_ != nullptr;
    }

private:
    const ArkSteedSafepointHeader *header_ = nullptr;
    const ArkSteedSafepointEntry *entries_ = nullptr;
    const uint8_t *data_ = nullptr;
    size_t size_ = 0;
};

// ============================================================================
// SourcePositionTableBuilder — records pcOffset ↔ bcOffset boundaries during
// compilation.  Emitted into the position table section of the container.
//
// Binary layout:
//   uint32_t numEntries
//   Entry[] (2 × uint32_t each, sorted by pcOffset ascending):
//     uint32_t pcOffset
//     uint32_t bcOffset
// ============================================================================

class PUBLIC_API SourcePositionTableBuilder {
public:
    explicit SourcePositionTableBuilder(Chunk *chunk)
        : entries_(chunk)
    {}
    ~SourcePositionTableBuilder() = default;

    void RecordBoundary(uint32_t pcOffset, uint32_t bcOffset);
    size_t GetSize() const;
    void Emit(uint8_t *buffer) const;
    std::unique_ptr<uint8_t[]> EmitToNewBuffer() const;
    void Clear();

    uint32_t GetNumEntries() const
    {
        return static_cast<uint32_t>(entries_.size());
    }

private:
    struct Entry {
        uint32_t pcOffset;
        uint32_t bcOffset;
    };
    ChunkVector<Entry> entries_;
};

// ============================================================================
// Section Container — wraps safepoint table + position table into one payload
// ============================================================================

// Binary layout of the container that replaces the raw safepoint table in
// MachineCode's stack-map area:
//
//   Header (24 bytes):
//     uint32_t magic          "ASTB" = ArkSteed Table Binary
//     uint32_t version        1
//     uint32_t safepointOffset  offset from header start to safepoint table section
//     uint32_t safepointSize    size of safepoint table section
//     uint32_t positionOffset   offset from header start to position table (0 if absent)
//     uint32_t positionSize     size of position table (0 if absent)
//
//   safepoint table section:
//     raw ArkSteedSafepointTable data (header + entries + deopt data)
//
//   position table section (optional, TASK-3):
//     raw SourcePositionTable data

struct ArkSteedTableContainerHeader {
    static constexpr uint32_t MAGIC = 0x41535442;  // "ASTB"
    static constexpr uint32_t VERSION = 1;
    static constexpr size_t SIZE = 6 * sizeof(uint32_t);  // 24 bytes

    uint32_t magic;
    uint32_t version;
    uint32_t safepointOffset;
    uint32_t safepointSize;
    uint32_t positionOffset;
    uint32_t positionSize;
};

// Extract the safepoint table data pointer and size from a container.
// Returns nullptr and sets *outSize = 0 if the container is invalid.
// The returned pointer is within the container buffer — no ownership transfer.
const uint8_t* GetArkSteedSafepointTable(const uint8_t* container, size_t containerSize, size_t* outSize);

// Extract the position table data pointer and size from a container.
// Returns nullptr and sets *outSize = 0 if absent or invalid.
// The returned pointer is within the container buffer — no ownership transfer.
const uint8_t* GetArkSteedPositionTable(const uint8_t* container, size_t containerSize, size_t* outSize);

// Look up bcOffset from SourcePositionTable or SafepointTable
uint32_t FindBcOffsetByPCOffset(MachineCode *machineCode, uint32_t pcOffset);

// ============================================================================
// Pure functions (no runtime dependency — only const uint8_t* + size_t)
// ============================================================================

// Extract bcOffset from a safepoint table's deopt data.
// Scans the deopt entries of the safepoint at pcOffset for PC_OFFSET_INDEX
// (vreg == -2) with CONSTANT kind and returns the value.
// Returns std::nullopt if the entry is not found, has no deopt data, or
// does not contain PC_OFFSET_INDEX with CONSTANT kind.
std::optional<uint32_t> ExtractBcOffsetFromSafepointTable(
    const uint8_t *tableData, size_t tableSize, uint32_t pcOffset);

// Look up bcOffset from a SourcePositionTable.
// Performs binary search for the last boundary with pcOffset <= target.
// Format: uint32_t numEntries, then (uint32_t pcOffset, uint32_t bcOffset) pairs.
// Returns std::nullopt if the table is empty or pcOffset is before the first entry.
std::optional<uint32_t> LookupBcOffsetFromPositionTable(
    const uint8_t *tableData, size_t tableSize, uint32_t pcOffset);

}  // namespace panda::ecmascript::arksteed

#endif  // ECMASCRIPT_ARKSTEED_SAFEPOINT_TABLE_H
