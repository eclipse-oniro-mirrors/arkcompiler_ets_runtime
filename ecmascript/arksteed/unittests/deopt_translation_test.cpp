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

// Unit domain for the persisted deopt translation and literal-table format (matrix row G5.4,
// contract C9 of arksteed-cpp-unit-test-domains).
//
// No VM is created: the domain is pure data. All expected values are re-derived in this file
// from the persisted format documented in ecmascript/arksteed/arksteed_deopt_helper.h
// ("Persisted deopt translation format"): fixed-width little-endian integers, ULEB128/SLEB128
// operands with the shortest canonical byte sequence, five-byte headers and a body-offset
// table. The reference stream below is computed by hand from that documentation and is never
// produced by the code under test.

#include <cmath>
#include <cstdint>
#include <cstring>

#include "securec.h"
#include <limits>
#include <set>
#include <string>
#include <vector>

#include "ecmascript/arksteed/arksteed_deopt_abi.h"
#include "ecmascript/arksteed/arksteed_deopt_helper.h"
#include "ecmascript/arksteed/arksteed_opcode.h"
#include "ecmascript/compiler/deopt_type.h"
#include "ecmascript/js_tagged_value.h"
#include "ecmascript/mem/chunk.h"
#include "ecmascript/mem/native_area_allocator.h"
#include "ecmascript/tests/test_helper.h"

namespace panda::test {
using namespace panda::ecmascript;
using namespace panda::ecmascript::arksteed;

namespace {

// ---------------------------------------------------------------------------------------------
// Documented persisted format
// ---------------------------------------------------------------------------------------------

constexpr size_t U32_BYTES = sizeof(uint32_t);
constexpr size_t U64_BYTES = sizeof(uint64_t);
// The documented header is uint32_t bodyId followed by uint8_t deoptType.
constexpr size_t TRANSLATION_HEADER_BYTES = U32_BYTES + sizeof(uint8_t);

// Opcode numbering of the persisted body opcode stream (DeoptTranslationOpcode order).
enum : uint8_t {
    OP_BEGIN = 0,
    OP_TAGGED_REGISTER = 1,
    OP_TAGGED_STACK_SLOT = 2,
    OP_TAGGED_HEAP_LITERAL = 3,
    OP_TAGGED_SPECIAL = 4,
    OP_TAGGED_INT_CONSTANT = 5,
    OP_TAGGED_DOUBLE_BITS = 6,
    OP_INT32_REGISTER = 7,
    OP_INT32_STACK_SLOT = 8,
    OP_INT32_CONSTANT = 9,
    OP_FLOAT64_REGISTER = 10,
    OP_FLOAT64_STACK_SLOT = 11,
    OP_FLOAT64_CONSTANT = 12,
    OP_RAW_INT32_REGISTER = 13,
    OP_RAW_INT32_STACK_SLOT = 14,
    OP_RAW_INT32_CONSTANT = 15,
    OP_COUNT = 16,
};

// Documented specialKind numbering: 0 undefined, 1 null, 2 true, 3 false, 4 hole, 5 exception.
constexpr uint8_t SPECIAL_UNDEFINED = 0;
constexpr uint8_t SPECIAL_NULL = 1;
constexpr uint8_t SPECIAL_TRUE = 2;
constexpr uint8_t SPECIAL_FALSE = 3;
constexpr uint8_t SPECIAL_HOLE = 4;
constexpr uint8_t SPECIAL_EXCEPTION = 5;
constexpr size_t SPECIAL_KIND_COUNT = 6;

// ---------------------------------------------------------------------------------------------
// Reference stream, computed by hand from the documented format
//
// translation 0: bytecodeOffset 5, deoptType LAZYDEOPT (1), one input
//                {vreg 3, TAGGED, STACK_SLOT, -16}
// translation 1: bytecodeOffset 300, deoptType NOTINT1 (2), one input
//                {vreg 7, INT32_TO_TAGGED, CONSTANT, 42}
//
// fixed part: deoptCount=2, bodyCount=2, header0={0, LAZYDEOPT}, header1={1, NOTINT1},
//             offsets {0, 6}
// body 0: BEGIN ULEB(5)=05 ULEB(1)=01 TAGGED_STACK_SLOT=02 SLEB(3)=03 SLEB(-16)=70
// body 1: BEGIN ULEB(300)=AC 02 ULEB(1)=01 INT32_CONSTANT=09 SLEB(7)=07 SLEB(42)=2A
// ---------------------------------------------------------------------------------------------

constexpr uint8_t REFERENCE_STREAM[] = {
    // deoptCount = 2
    0x02, 0x00, 0x00, 0x00,
    // bodyCount = 2
    0x02, 0x00, 0x00, 0x00,
    // header 0: bodyId = 0, deoptType = LAZYDEOPT (1)
    0x00, 0x00, 0x00, 0x00, 0x01,
    // header 1: bodyId = 1, deoptType = NOTINT1 (2)
    0x01, 0x00, 0x00, 0x00, 0x02,
    // body offsets: 0 and 6
    0x00, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00,
    // body 0
    0x00, 0x05, 0x01, 0x02, 0x03, 0x70,
    // body 1
    0x00, 0xAC, 0x02, 0x01, 0x09, 0x07, 0x2A,
};

constexpr size_t REF_DEOPT_COUNT_OFFSET = 0;
constexpr size_t REF_BODY_COUNT_OFFSET = U32_BYTES;
constexpr size_t REF_HEADER0_OFFSET = 2 * U32_BYTES;
constexpr size_t REF_HEADER1_OFFSET = REF_HEADER0_OFFSET + TRANSLATION_HEADER_BYTES;
constexpr size_t REF_OFFSET_TABLE_OFFSET = REF_HEADER1_OFFSET + TRANSLATION_HEADER_BYTES;
constexpr size_t REF_OFFSET1_OFFSET = REF_OFFSET_TABLE_OFFSET + U32_BYTES;
constexpr size_t REF_BODY_STREAM_OFFSET = REF_OFFSET_TABLE_OFFSET + 2 * U32_BYTES;
constexpr size_t REFERENCE_STREAM_SIZE = 39;
constexpr uint32_t REFERENCE_BYTECODE_OFFSET0 = 5;
constexpr uint32_t REFERENCE_BYTECODE_OFFSET1 = 300;
constexpr uint32_t REFERENCE_BODY_STREAM_BYTES = 13;

static_assert(sizeof(REFERENCE_STREAM) == REFERENCE_STREAM_SIZE);
static_assert(REF_BODY_STREAM_OFFSET + REFERENCE_BODY_STREAM_BYTES == REFERENCE_STREAM_SIZE);
static_assert(OP_COUNT == 16);  // 16: opcodes in the persisted stream (OP_BEGIN..OP_RAW_INT32_CONSTANT)

// ---------------------------------------------------------------------------------------------
// Independent writers/readers of the documented format
// ---------------------------------------------------------------------------------------------

void PushU32LE(std::vector<uint8_t> &out, uint32_t value)
{
    for (size_t index = 0; index < U32_BYTES; ++index) {
        out.push_back(static_cast<uint8_t>(value >> (index * 8U)));  // 8U: bits per byte, little-endian
    }
}

void PushU64LE(std::vector<uint8_t> &out, uint64_t value)
{
    for (size_t index = 0; index < U64_BYTES; ++index) {
        out.push_back(static_cast<uint8_t>(value >> (index * 8U)));  // 8U: bits per byte, little-endian
    }
}

void PushULEB128(std::vector<uint8_t> &out, uint64_t value)
{
    do {
        uint8_t byte = static_cast<uint8_t>(value & 0x7FU);  // 0x7F: 7 payload bits per ULEB128 byte
        value >>= 7U;  // 7U: payload bits per byte
        if (value != 0U) {
            byte |= 0x80U;  // 0x80U: continuation bit
        }
        out.push_back(byte);
    } while (value != 0U);
}

void PushSLEB128(std::vector<uint8_t> &out, int64_t value)
{
    bool hasMore = true;
    while (hasMore) {
        uint8_t byte = static_cast<uint8_t>(value) & 0x7FU;  // 0x7F: 7 payload bits per SLEB128 byte
        bool signBitSet = (byte & 0x40U) != 0U;  // 0x40U: sign bit of the 7-bit payload
        value >>= 7;  // 7: payload bits per byte
        hasMore = !((value == 0 && !signBitSet) || (value == -1 && signBitSet));
        if (hasMore) {
            byte |= 0x80U;  // 0x80U: continuation bit
        }
        out.push_back(byte);
    }
}

uint32_t ReadU32LE(const std::vector<uint8_t> &bytes, size_t offset)
{
    uint32_t value = 0;
    for (size_t index = 0; index < U32_BYTES; ++index) {
        value |= static_cast<uint32_t>(bytes[offset + index]) << (index * 8U);  // 8U: bits per byte, little-endian
    }
    return value;
}

void WriteU32LE(std::vector<uint8_t> &bytes, size_t offset, uint32_t value)
{
    for (size_t index = 0; index < U32_BYTES; ++index) {
        bytes[offset + index] = static_cast<uint8_t>(value >> (index * 8U));  // 8U: bits per byte, little-endian
    }
}

std::string HexBytes(const std::vector<uint8_t> &bytes)
{
    static const char *digits = "0123456789ABCDEF";
    std::string text;
    text.reserve(bytes.size() * 3U);  // 3U: hex characters per byte
    for (uint8_t byte : bytes) {
        if (!text.empty()) {
            text.push_back(' ');
        }
        text.push_back(digits[byte >> 4U]);  // 4U: shift for the high hex digit
        text.push_back(digits[byte & 0x0FU]);  // 0x0F: mask of the low hex digit
    }
    return text;
}

std::string HexBytes(const uint8_t *bytes, size_t size)
{
    return HexBytes(std::vector<uint8_t>(bytes, bytes + size));
}

// Documented specialKind of a tagged special value.
uint8_t SpecialKindOf(int64_t source)
{
    JSTaggedValue value(static_cast<JSTaggedType>(source));
    if (value.IsUndefined()) {
        return SPECIAL_UNDEFINED;
    }
    if (value.IsNull()) {
        return SPECIAL_NULL;
    }
    if (value.IsTrue()) {
        return SPECIAL_TRUE;
    }
    if (value.IsFalse()) {
        return SPECIAL_FALSE;
    }
    if (value.IsHole()) {
        return SPECIAL_HOLE;
    }
    CHECK(value.IsException());
    return SPECIAL_EXCEPTION;
}

// Documented opcode of one explicit input operand.
uint8_t OpcodeOfInput(const DeoptTranslationInput &input)
{
    switch (input.valueKind) {
        case DeoptTranslationKind::TAGGED:
            if (input.sourceKind == DeoptSourceKind::CONSTANT) {
                JSTaggedValue value(static_cast<JSTaggedType>(input.source));
                if (value.IsUndefined() || value.IsNull() || value.IsTrue() || value.IsFalse() ||
                    value.IsHole() || value.IsException()) {
                    return OP_TAGGED_SPECIAL;
                }
                return value.IsInt() ? OP_TAGGED_INT_CONSTANT : OP_TAGGED_DOUBLE_BITS;
            }
            return input.sourceKind == DeoptSourceKind::GP_REGISTER ? OP_TAGGED_REGISTER
                   : input.sourceKind == DeoptSourceKind::STACK_SLOT ? OP_TAGGED_STACK_SLOT
                                                                     : OP_TAGGED_HEAP_LITERAL;
        case DeoptTranslationKind::INT32_TO_TAGGED:
            return input.sourceKind == DeoptSourceKind::GP_REGISTER ? OP_INT32_REGISTER
                   : input.sourceKind == DeoptSourceKind::STACK_SLOT ? OP_INT32_STACK_SLOT
                                                                     : OP_INT32_CONSTANT;
        case DeoptTranslationKind::FLOAT64_TO_TAGGED_DOUBLE:
            return input.sourceKind == DeoptSourceKind::FP_REGISTER ? OP_FLOAT64_REGISTER
                   : input.sourceKind == DeoptSourceKind::STACK_SLOT ? OP_FLOAT64_STACK_SLOT
                                                                     : OP_FLOAT64_CONSTANT;
        case DeoptTranslationKind::RAW_INT32:
            return input.sourceKind == DeoptSourceKind::GP_REGISTER ? OP_RAW_INT32_REGISTER
                   : input.sourceKind == DeoptSourceKind::STACK_SLOT ? OP_RAW_INT32_STACK_SLOT
                                                                     : OP_RAW_INT32_CONSTANT;
    }
    UNREACHABLE();
}

// Documented operand encoding of one explicit input (see the operand table in the header).
void PushInput(std::vector<uint8_t> &out, const DeoptTranslationInput &input)
{
    out.push_back(OpcodeOfInput(input));
    PushSLEB128(out, input.vreg);
    switch (OpcodeOfInput(input)) {
        case OP_TAGGED_REGISTER:
        case OP_INT32_REGISTER:
        case OP_RAW_INT32_REGISTER:
        case OP_FLOAT64_REGISTER:
        case OP_TAGGED_HEAP_LITERAL:
            PushULEB128(out, static_cast<uint64_t>(input.source));
            return;
        case OP_TAGGED_SPECIAL:
            PushULEB128(out, SpecialKindOf(input.source));
            return;
        case OP_TAGGED_DOUBLE_BITS:
            PushU64LE(out, static_cast<uint64_t>(input.source));
            return;
        case OP_TAGGED_INT_CONSTANT:
            // The documented operand is the integer value, not the raw tagged word.
            PushSLEB128(out, static_cast<int64_t>(JSTaggedValue(static_cast<JSTaggedType>(input.source)).GetInt()));
            return;
        case OP_FLOAT64_CONSTANT:
            PushU64LE(out, static_cast<uint64_t>(input.source));
            return;
        default:
            PushSLEB128(out, input.source);
            return;
    }
}

struct ExpectedTranslation {
    uint32_t bytecodeOffset {0};
    kungfu::DeoptType type {kungfu::DeoptType::LAZYDEOPT};
    std::vector<DeoptTranslationInput> inputs;
};

DeoptTranslationInput MakeInput(int32_t vreg, DeoptTranslationKind valueKind, DeoptSourceKind sourceKind,
                                int64_t source)
{
    DeoptTranslationInput input {};
    input.vreg = vreg;
    input.valueKind = valueKind;
    input.sourceKind = sourceKind;
    input.source = source;
    return input;
}

// Composes the documented byte layout. It is only valid for translations with pairwise distinct
// bodies and pairwise distinct (body, type) pairs, i.e. without the builder's deduplication.
std::vector<uint8_t> ComposeStream(const std::vector<ExpectedTranslation> &translations)
{
    std::vector<std::vector<uint8_t>> bodies;
    bodies.reserve(translations.size());
    for (const auto &translation : translations) {
        std::vector<uint8_t> body;
        body.push_back(OP_BEGIN);
        PushULEB128(body, translation.bytecodeOffset);
        PushULEB128(body, translation.inputs.size());
        for (const auto &input : translation.inputs) {
            PushInput(body, input);
        }
        bodies.push_back(std::move(body));
    }

    std::vector<uint8_t> out;
    PushU32LE(out, static_cast<uint32_t>(translations.size()));
    PushU32LE(out, static_cast<uint32_t>(bodies.size()));
    for (size_t index = 0; index < translations.size(); ++index) {
        PushU32LE(out, static_cast<uint32_t>(index));
        out.push_back(static_cast<uint8_t>(translations[index].type));
    }
    uint32_t offset = 0;
    for (const auto &body : bodies) {
        PushU32LE(out, offset);
        offset += static_cast<uint32_t>(body.size());
    }
    for (const auto &body : bodies) {
        out.insert(out.end(), body.begin(), body.end());
    }
    return out;
}

// Wraps raw body byte streams into a complete persisted stream: deoptCount == bodyCount ==
// bodies.size(), header i points at body i with deoptType LAZYDEOPT, offsets are cumulative.
std::vector<uint8_t> WrapRawBodies(const std::vector<std::vector<uint8_t>> &bodies)
{
    std::vector<uint8_t> out;
    PushU32LE(out, static_cast<uint32_t>(bodies.size()));
    PushU32LE(out, static_cast<uint32_t>(bodies.size()));
    for (size_t index = 0; index < bodies.size(); ++index) {
        PushU32LE(out, static_cast<uint32_t>(index));
        out.push_back(static_cast<uint8_t>(kungfu::DeoptType::LAZYDEOPT));
    }
    uint32_t offset = 0;
    for (const auto &body : bodies) {
        PushU32LE(out, offset);
        offset += static_cast<uint32_t>(body.size());
    }
    for (const auto &body : bodies) {
        out.insert(out.end(), body.begin(), body.end());
    }
    return out;
}

std::vector<uint8_t> ReferenceStreamBytes()
{
    return std::vector<uint8_t>(REFERENCE_STREAM, REFERENCE_STREAM + sizeof(REFERENCE_STREAM));
}

}  // namespace

class ArkSteedDeoptTranslationTest : public testing::Test {
protected:
    // Chunk-backed scratch memory for the constant vertices and for the copies of encoded
    // streams the reader is pointed at. No VM and no JS heap is involved.
    NativeAreaAllocator native;
    Chunk chunk {&native};

    const uint8_t *CopyToChunk(const std::vector<uint8_t> &bytes)
    {
        if (bytes.empty()) {
            return nullptr;
        }
        uint8_t *copy = chunk.NewArray<uint8_t>(bytes.size());
        if (memcpy_s(copy, bytes.size(), bytes.data(), bytes.size()) != EOK) {
            return nullptr;
        }
        return copy;
    }

    ValueVertex *NewInt32Constant(int32_t value)
    {
        return Vertex::New<Int32ConstantVertex>(&chunk, static_cast<size_t>(0), value);
    }

    ValueVertex *NewInt64Constant(int64_t value)
    {
        return Vertex::New<Int64ConstantVertex>(&chunk, static_cast<size_t>(0), value);
    }

    ValueVertex *NewFloat64Constant(double value)
    {
        return Vertex::New<Float64ConstantVertex>(&chunk, static_cast<size_t>(0), value);
    }

    ValueVertex *NewTaggedConstant(int64_t raw)
    {
        return Vertex::New<TaggedConstantVertex>(&chunk, static_cast<size_t>(0),
                                                 static_cast<uint64_t>(raw));
    }
};

/**
 * @tc.name: LazyDeoptOffset_AllKindsRoundTrip
 * @tc.desc: the low three FP-relative offset bits carry every DeoptTranslationKind tag and the
 *           tagged word round-trips back to (offset, kind) for aligned offsets
 * @tc.type: FUNC
 */
HWTEST_F_L0(ArkSteedDeoptTranslationTest, LazyDeoptOffset_AllKindsRoundTrip)
{
    // Documented: safepoint offsets are 8-byte aligned, so the low three bits are free
    // (arksteed_deopt_helper.h, LAZY_DEOPT_OFFSET_TAG_MASK).
    EXPECT_EQ(LAZY_DEOPT_OFFSET_TAG_MASK, 0x7);
    const std::vector<DeoptTranslationKind> kinds = {
        DeoptTranslationKind::TAGGED,
        DeoptTranslationKind::INT32_TO_TAGGED,
        DeoptTranslationKind::FLOAT64_TO_TAGGED_DOUBLE,
        DeoptTranslationKind::RAW_INT32,
    };
    ASSERT_EQ(kinds.size(), 4U);
    const std::vector<int32_t> offsets = {0, 8, 1024, -16, 0x7FFFF8};
    std::set<int> tags;
    for (DeoptTranslationKind kind : kinds) {
        int32_t tag = static_cast<int32_t>(static_cast<uint8_t>(kind));
        EXPECT_GE(tag, 0);
        EXPECT_LE(tag, LAZY_DEOPT_OFFSET_TAG_MASK);
        EXPECT_TRUE(tags.insert(tag).second) << "tag " << tag << " is not unique";
        for (int32_t offset : offsets) {
            EXPECT_EQ(offset & LAZY_DEOPT_OFFSET_TAG_MASK, 0) << "offset must be 8-byte aligned";
            int32_t encoded = EncodeLazyDeoptOffset(offset, kind);
            EXPECT_EQ(encoded, offset | tag);
            EXPECT_EQ(static_cast<uint8_t>(DecodeLazyDeoptOffsetTag(encoded)),
                      static_cast<uint8_t>(kind));
            EXPECT_EQ(StripLazyDeoptOffsetTag(encoded), offset);
        }
    }
    EXPECT_EQ(tags.size(), kinds.size());
}

/**
 * @tc.name: LazyDeoptOffset_UnalignedOffsetRejectedInDebug
 * @tc.desc: a non-8-byte-aligned offset is rejected by the Debug assertion instead of losing the
 *           low bits silently; in both modes the tag word still keeps and recovers the low bits
 * @tc.type: FUNC
 */
HWTEST_F_L0(ArkSteedDeoptTranslationTest, LazyDeoptOffset_UnalignedOffsetRejectedInDebug)
{
#ifndef NDEBUG
    // Debug: production ASSERT (arksteed_deopt_helper.h, EncodeLazyDeoptOffset) fires.
    EXPECT_DEATH(EncodeLazyDeoptOffset(1, DeoptTranslationKind::TAGGED), "ASSERTION FAILED");
    // 0x1004: misaligned w.r.t. the 3-bit lazy-deopt tag mask
    EXPECT_DEATH(EncodeLazyDeoptOffset(0x1004, DeoptTranslationKind::RAW_INT32), "ASSERTION FAILED");
#endif
    // Both modes: the low three bits of a tagged word survive as the kind tag and the aligned
    // offset is recovered exactly.
    int32_t tagged = 0x1008 | static_cast<int32_t>(DeoptTranslationKind::RAW_INT32);
    EXPECT_EQ(StripLazyDeoptOffsetTag(tagged), 0x1008);  // 0x1008: aligned offset passes through
    EXPECT_EQ(static_cast<uint8_t>(DecodeLazyDeoptOffsetTag(tagged)),
              static_cast<uint8_t>(DeoptTranslationKind::RAW_INT32));
    // 0x1009: 0x1008 tagged with a kind
    int32_t unaligned = 0x1009 | static_cast<int32_t>(DeoptTranslationKind::INT32_TO_TAGGED);
    EXPECT_EQ(StripLazyDeoptOffsetTag(unaligned), 0x1008);  // 0x1008: tag stripped back to the aligned offset
    EXPECT_EQ(static_cast<uint8_t>(DecodeLazyDeoptOffsetTag(unaligned)),
              static_cast<uint8_t>(DeoptTranslationKind::INT32_TO_TAGGED));
    EXPECT_EQ(static_cast<uint8_t>(DecodeLazyDeoptOffsetTag(1)),
              static_cast<uint8_t>(DeoptTranslationKind::INT32_TO_TAGGED));
    EXPECT_EQ(static_cast<uint8_t>(DecodeLazyDeoptOffsetTag(LAZY_DEOPT_OFFSET_TAG_MASK)),
              static_cast<uint8_t>(LAZY_DEOPT_OFFSET_TAG_MASK));
}

/**
 * @tc.name: LiteralTable_DeduplicatesAndKeepsInsertionOrder
 * @tc.desc: DeoptLiteralTableBuilder::GetOrAdd returns the first literal index of a repeated
 *           handle index and preserves insertion order of distinct handle indices
 * @tc.type: FUNC
 */
HWTEST_F_L0(ArkSteedDeoptTranslationTest, LiteralTable_DeduplicatesAndKeepsInsertionOrder)
{
    DeoptLiteralTableBuilder table;
    EXPECT_EQ(table.GetOrAdd(10U), 0U);
    EXPECT_EQ(table.GetOrAdd(20U), 1U);
    EXPECT_EQ(table.GetOrAdd(10U), 0U);
    EXPECT_EQ(table.GetOrAdd(30U), 2U);
    EXPECT_EQ(table.GetOrAdd(20U), 1U);
    EXPECT_EQ(table.GetOrAdd(30U), 2U);
    EXPECT_EQ(table.GetOrAdd(10U), 0U);
    EXPECT_EQ(table.GetOrAdd(0U), 3U);
    const std::vector<uint32_t> &indices = table.GetHandleIndices();
    ASSERT_EQ(indices.size(), 4U);
    EXPECT_EQ(indices[0], 10U);
    EXPECT_EQ(indices[1], 20U);
    EXPECT_EQ(indices[2], 30U);
    EXPECT_EQ(indices[3], 0U);
}

/**
 * @tc.name: LiteralTable_HeapLiteralOpcodeStoresLiteralIndex
 * @tc.desc: a heap-literal input is persisted as its literal index (not the handle index) and
 *           the reader restores that index with the TAGGED/HEAP_LITERAL kind
 * @tc.type: FUNC
 */
HWTEST_F_L0(ArkSteedDeoptTranslationTest, LiteralTable_HeapLiteralOpcodeStoresLiteralIndex)
{
    DeoptLiteralTableBuilder table;
    uint32_t firstLiteral = table.GetOrAdd(0x11U);
    uint32_t secondLiteral = table.GetOrAdd(0x22U);
    EXPECT_EQ(firstLiteral, 0U);
    EXPECT_EQ(secondLiteral, 1U);
    // The handle index 0x11 maps to literal index 0, so the persisted source is the literal index.
    EXPECT_EQ(table.GetOrAdd(0x11U), 0U);

    ExpectedTranslation expected {
        9,
        kungfu::DeoptType::LAZYDEOPT,
        {
            MakeInput(0, DeoptTranslationKind::TAGGED, DeoptSourceKind::HEAP_LITERAL,
                      static_cast<int64_t>(firstLiteral)),
            MakeInput(1, DeoptTranslationKind::TAGGED, DeoptSourceKind::HEAP_LITERAL,
                      static_cast<int64_t>(secondLiteral)),
        },
    };

    DeoptTranslationBuilder builder;
    DeoptId deoptId = builder.AddTranslation(expected.bytecodeOffset, expected.type, expected.inputs);
    EXPECT_EQ(deoptId, DeoptId{0});
    std::vector<uint8_t> encoded = builder.Encode();
    EXPECT_EQ(HexBytes(encoded), HexBytes(ComposeStream({expected})));

    DeoptTranslationReader reader(CopyToChunk(encoded), encoded.size());
    ASSERT_TRUE(reader.IsValid());
    ASSERT_EQ(reader.GetDeoptCount(), 1U);
    DeoptTranslation body;
    ASSERT_TRUE(reader.GetBody(0, &body));
    EXPECT_EQ(body.bytecodeOffset, expected.bytecodeOffset);
    ASSERT_EQ(body.inputs.size(), 2U);
    EXPECT_EQ(body.inputs[0], expected.inputs[0]);
    EXPECT_EQ(body.inputs[1], expected.inputs[1]);
    EXPECT_EQ(body.inputs[0].source, static_cast<int64_t>(firstLiteral));
    EXPECT_EQ(body.inputs[1].source, static_cast<int64_t>(secondLiteral));
    EXPECT_NE(body.inputs[0].source, static_cast<int64_t>(0x11U));
    EXPECT_EQ(OpcodeOfInput(body.inputs[0]), OP_TAGGED_HEAP_LITERAL);
}

/**
 * @tc.name: ReferenceStream_MatchesHandComputedDocumentedBytes
 * @tc.desc: Encode() reproduces the hand-computed reference byte stream of the documented format
 * @tc.type: FUNC
 */
HWTEST_F_L0(ArkSteedDeoptTranslationTest, ReferenceStream_MatchesHandComputedDocumentedBytes)
{
    std::vector<ExpectedTranslation> translations = {
        {REFERENCE_BYTECODE_OFFSET0,
         kungfu::DeoptType::LAZYDEOPT,
         {MakeInput(3, DeoptTranslationKind::TAGGED, DeoptSourceKind::STACK_SLOT, -16)}},
        {REFERENCE_BYTECODE_OFFSET1,
         kungfu::DeoptType::NOTINT1,
         {MakeInput(7, DeoptTranslationKind::INT32_TO_TAGGED, DeoptSourceKind::CONSTANT, 42)}},
    };

    DeoptTranslationBuilder builder;
    DeoptId first = builder.AddTranslation(translations[0].bytecodeOffset, translations[0].type,
                                           translations[0].inputs);
    DeoptId second = builder.AddTranslation(translations[1].bytecodeOffset, translations[1].type,
                                            translations[1].inputs);
    EXPECT_EQ(first, DeoptId{0});
    EXPECT_EQ(second, DeoptId{1});

    std::vector<uint8_t> encoded = builder.Encode();
    EXPECT_EQ(encoded.size(), REFERENCE_STREAM_SIZE);
    EXPECT_EQ(HexBytes(encoded), HexBytes(REFERENCE_STREAM, sizeof(REFERENCE_STREAM)));
    ASSERT_GE(encoded.size(), REF_BODY_STREAM_OFFSET);
    EXPECT_EQ(ReadU32LE(encoded, REF_DEOPT_COUNT_OFFSET), 2U);
    EXPECT_EQ(ReadU32LE(encoded, REF_BODY_COUNT_OFFSET), 2U);
    EXPECT_EQ(ReadU32LE(encoded, REF_OFFSET_TABLE_OFFSET), 0U);
    EXPECT_EQ(ReadU32LE(encoded, REF_OFFSET1_OFFSET), 6U);
    EXPECT_EQ(encoded[REF_HEADER0_OFFSET + U32_BYTES], 0x01);  // 0x01: deoptType byte of the first reference body
    EXPECT_EQ(encoded[REF_HEADER1_OFFSET + U32_BYTES], 0x02);  // 0x02: deoptType byte of the second reference body
}

/**
 * @tc.name: ReferenceStream_ReaderDecodesHandComputedBytes
 * @tc.desc: the reader decodes the hand-computed reference bytes into the documented translations
 * @tc.type: FUNC
 */
HWTEST_F_L0(ArkSteedDeoptTranslationTest, ReferenceStream_ReaderDecodesHandComputedBytes)
{
    const uint8_t *memory = CopyToChunk(ReferenceStreamBytes());
    DeoptTranslationReader reader(memory, REFERENCE_STREAM_SIZE);
    ASSERT_TRUE(reader.IsValid());
    EXPECT_EQ(reader.GetDeoptCount(), 2U);

    DeoptTranslationHeader header {};
    ASSERT_TRUE(reader.GetHeader(DeoptId{0}, &header));
    EXPECT_EQ(header.bodyId, 0U);
    EXPECT_EQ(static_cast<uint8_t>(header.type), static_cast<uint8_t>(kungfu::DeoptType::LAZYDEOPT));
    ASSERT_TRUE(reader.GetHeader(DeoptId{1}, &header));
    EXPECT_EQ(header.bodyId, 1U);
    EXPECT_EQ(static_cast<uint8_t>(header.type), static_cast<uint8_t>(kungfu::DeoptType::NOTINT1));

    DeoptTranslation body;
    ASSERT_TRUE(reader.GetBody(0, &body));
    EXPECT_EQ(body.bytecodeOffset, REFERENCE_BYTECODE_OFFSET0);
    ASSERT_EQ(body.inputs.size(), 1U);
    EXPECT_EQ(body.inputs[0],
              MakeInput(3, DeoptTranslationKind::TAGGED, DeoptSourceKind::STACK_SLOT, -16));
    ASSERT_TRUE(reader.GetBody(1, &body));
    EXPECT_EQ(body.bytecodeOffset, REFERENCE_BYTECODE_OFFSET1);
    ASSERT_EQ(body.inputs.size(), 1U);
    EXPECT_EQ(body.inputs[0],
              MakeInput(7, DeoptTranslationKind::INT32_TO_TAGGED, DeoptSourceKind::CONSTANT, 42));

    // Ids outside the header/body tables are rejected.
    EXPECT_FALSE(reader.GetHeader(DeoptId{2}, &header));
    EXPECT_FALSE(reader.GetBody(2, &body));
}

/**
 * @tc.name: RoundTrip_AllSourceKindsAndOpcodes
 * @tc.desc: one body covering all five DeoptSourceKind values, all four DeoptTranslationKind
 *           values and all fifteen explicit body opcodes round-trips exactly
 * @tc.type: FUNC
 */
HWTEST_F_L0(ArkSteedDeoptTranslationTest, RoundTrip_AllSourceKindsAndOpcodes)
{
    ASSERT_GE(ARKSTEED_DEOPT_GENERAL_REGISTER_CODES.size(), 3U);
    uint32_t gp0 = ARKSTEED_DEOPT_GENERAL_REGISTER_CODES[0];
    uint32_t gp1 = ARKSTEED_DEOPT_GENERAL_REGISTER_CODES[1];
    uint32_t gp2 = ARKSTEED_DEOPT_GENERAL_REGISTER_CODES[2];
    ASSERT_GE(ARKSTEED_DEOPT_FLOATING_REGISTER_COUNT, 1U);

    const std::vector<int64_t> specials = {
        static_cast<int64_t>(JSTaggedValue::Undefined().GetRawData()),
        static_cast<int64_t>(JSTaggedValue::Null().GetRawData()),
        static_cast<int64_t>(JSTaggedValue::True().GetRawData()),
        static_cast<int64_t>(JSTaggedValue::False().GetRawData()),
        static_cast<int64_t>(JSTaggedValue::Hole().GetRawData()),
        static_cast<int64_t>(JSTaggedValue::Exception().GetRawData()),
    };

    std::vector<DeoptTranslationInput> inputs;
    // TAGGED, one input per source kind and per tagged-constant opcode.
    inputs.push_back(MakeInput(0, DeoptTranslationKind::TAGGED, DeoptSourceKind::GP_REGISTER, gp0));
    inputs.push_back(MakeInput(1, DeoptTranslationKind::TAGGED, DeoptSourceKind::STACK_SLOT, -16));
    inputs.push_back(MakeInput(2, DeoptTranslationKind::TAGGED, DeoptSourceKind::HEAP_LITERAL, 0));
    for (size_t index = 0; index < specials.size(); ++index) {
        inputs.push_back(MakeInput(static_cast<int32_t>(3 + index), DeoptTranslationKind::TAGGED,
                                   DeoptSourceKind::CONSTANT, specials[index]));
    }
    inputs.push_back(MakeInput(9, DeoptTranslationKind::TAGGED, DeoptSourceKind::CONSTANT,
                               static_cast<int64_t>(JSTaggedValue(42).GetRawData())));
    inputs.push_back(MakeInput(10, DeoptTranslationKind::TAGGED, DeoptSourceKind::CONSTANT,
                               static_cast<int64_t>(JSTaggedValue(1.5).GetRawData())));
    // INT32_TO_TAGGED, all three source kinds.
    inputs.push_back(MakeInput(11, DeoptTranslationKind::INT32_TO_TAGGED,
                               DeoptSourceKind::GP_REGISTER, gp1));
    inputs.push_back(MakeInput(12, DeoptTranslationKind::INT32_TO_TAGGED,
                               DeoptSourceKind::STACK_SLOT, 24));
    inputs.push_back(MakeInput(13, DeoptTranslationKind::INT32_TO_TAGGED,
                               DeoptSourceKind::CONSTANT, -7));
    // FLOAT64_TO_TAGGED_DOUBLE, all three source kinds.
    inputs.push_back(MakeInput(14, DeoptTranslationKind::FLOAT64_TO_TAGGED_DOUBLE,
                               DeoptSourceKind::FP_REGISTER, 0));
    inputs.push_back(MakeInput(15, DeoptTranslationKind::FLOAT64_TO_TAGGED_DOUBLE,
                               DeoptSourceKind::STACK_SLOT, -32));
    inputs.push_back(MakeInput(16, DeoptTranslationKind::FLOAT64_TO_TAGGED_DOUBLE,
                               DeoptSourceKind::CONSTANT, GetFloat64RawBits(1.5)));
    // RAW_INT32, all three source kinds.
    inputs.push_back(MakeInput(17, DeoptTranslationKind::RAW_INT32, DeoptSourceKind::GP_REGISTER, gp2));
    inputs.push_back(MakeInput(18, DeoptTranslationKind::RAW_INT32, DeoptSourceKind::STACK_SLOT, 40));
    inputs.push_back(MakeInput(19, DeoptTranslationKind::RAW_INT32, DeoptSourceKind::CONSTANT,
                               std::numeric_limits<int32_t>::min()));
    ASSERT_EQ(inputs.size(), 20U);

    // The fixture covers every opcode of the body stream exactly once (BEGIN + 15 operands).
    std::set<uint8_t> opcodes;
    std::set<uint8_t> sourceKinds;
    std::set<uint8_t> valueKinds;
    for (const auto &input : inputs) {
        opcodes.insert(OpcodeOfInput(input));
        sourceKinds.insert(static_cast<uint8_t>(input.sourceKind));
        valueKinds.insert(static_cast<uint8_t>(input.valueKind));
    }
    ASSERT_EQ(opcodes.size(), static_cast<size_t>(OP_COUNT) - 1U);
    ASSERT_EQ(sourceKinds.size(), 5U);
    ASSERT_EQ(valueKinds.size(), 4U);

    ExpectedTranslation expected {64, kungfu::DeoptType::LAZYDEOPT, inputs};
    DeoptTranslationBuilder builder;
    EXPECT_EQ(builder.AddTranslation(expected.bytecodeOffset, expected.type, expected.inputs),
              DeoptId{0});
    std::vector<uint8_t> encoded = builder.Encode();
    EXPECT_EQ(HexBytes(encoded), HexBytes(ComposeStream({expected})));

    DeoptTranslationReader reader(CopyToChunk(encoded), encoded.size());
    ASSERT_TRUE(reader.IsValid());
    ASSERT_EQ(reader.GetDeoptCount(), 1U);
    DeoptTranslation body;
    ASSERT_TRUE(reader.GetBody(0, &body));
    EXPECT_EQ(body.bytecodeOffset, expected.bytecodeOffset);
    ASSERT_EQ(body.inputs.size(), inputs.size());
    for (size_t index = 0; index < inputs.size(); ++index) {
        SCOPED_TRACE(index);
        EXPECT_EQ(body.inputs[index], inputs[index]);
    }
    EXPECT_EQ(OpcodeOfInput(body.inputs[0]), OP_TAGGED_REGISTER);
    EXPECT_EQ(OpcodeOfInput(body.inputs[1]), OP_TAGGED_STACK_SLOT);
    EXPECT_EQ(OpcodeOfInput(body.inputs[2]), OP_TAGGED_HEAP_LITERAL);
    // specialKind 0..5 in documented order.
    for (size_t index = 0; index < specials.size(); ++index) {
        JSTaggedValue decoded(static_cast<JSTaggedType>(body.inputs[3 + index].source));
        SCOPED_TRACE(index);
        EXPECT_EQ(OpcodeOfInput(body.inputs[3 + index]), OP_TAGGED_SPECIAL);
        EXPECT_EQ(SpecialKindOf(body.inputs[3 + index].source), static_cast<uint8_t>(index));
        EXPECT_TRUE(decoded == JSTaggedValue(static_cast<JSTaggedType>(specials[index])));
    }
    EXPECT_TRUE(JSTaggedValue(static_cast<JSTaggedType>(body.inputs[3].source)).IsUndefined());
    EXPECT_TRUE(JSTaggedValue(static_cast<JSTaggedType>(body.inputs[4].source)).IsNull());
    EXPECT_TRUE(JSTaggedValue(static_cast<JSTaggedType>(body.inputs[5].source)).IsTrue());
    EXPECT_TRUE(JSTaggedValue(static_cast<JSTaggedType>(body.inputs[6].source)).IsFalse());
    EXPECT_TRUE(JSTaggedValue(static_cast<JSTaggedType>(body.inputs[7].source)).IsHole());
    EXPECT_TRUE(JSTaggedValue(static_cast<JSTaggedType>(body.inputs[8].source)).IsException());
    EXPECT_EQ(OpcodeOfInput(body.inputs[9]), OP_TAGGED_INT_CONSTANT);
    EXPECT_EQ(OpcodeOfInput(body.inputs[10]), OP_TAGGED_DOUBLE_BITS);
    EXPECT_TRUE(JSTaggedValue(static_cast<JSTaggedType>(body.inputs[10].source)).IsDouble());
    EXPECT_EQ(OpcodeOfInput(body.inputs[11]), OP_INT32_REGISTER);
    EXPECT_EQ(OpcodeOfInput(body.inputs[12]), OP_INT32_STACK_SLOT);
    EXPECT_EQ(OpcodeOfInput(body.inputs[13]), OP_INT32_CONSTANT);
    EXPECT_EQ(OpcodeOfInput(body.inputs[14]), OP_FLOAT64_REGISTER);
    EXPECT_EQ(OpcodeOfInput(body.inputs[15]), OP_FLOAT64_STACK_SLOT);
    EXPECT_EQ(OpcodeOfInput(body.inputs[16]), OP_FLOAT64_CONSTANT);
    EXPECT_EQ(OpcodeOfInput(body.inputs[17]), OP_RAW_INT32_REGISTER);
    EXPECT_EQ(OpcodeOfInput(body.inputs[18]), OP_RAW_INT32_STACK_SLOT);
    EXPECT_EQ(OpcodeOfInput(body.inputs[19]), OP_RAW_INT32_CONSTANT);
}

/**
 * @tc.name: RoundTrip_SpecialKindsZeroToFive
 * @tc.desc: each documented specialKind (0..5) is persisted with its documented number and read
 *           back as the corresponding special JSTaggedValue
 * @tc.type: FUNC
 */
HWTEST_F_L0(ArkSteedDeoptTranslationTest, RoundTrip_SpecialKindsZeroToFive)
{
    const std::vector<int64_t> specials = {
        static_cast<int64_t>(JSTaggedValue::Undefined().GetRawData()),
        static_cast<int64_t>(JSTaggedValue::Null().GetRawData()),
        static_cast<int64_t>(JSTaggedValue::True().GetRawData()),
        static_cast<int64_t>(JSTaggedValue::False().GetRawData()),
        static_cast<int64_t>(JSTaggedValue::Hole().GetRawData()),
        static_cast<int64_t>(JSTaggedValue::Exception().GetRawData()),
    };
    ASSERT_EQ(specials.size(), SPECIAL_KIND_COUNT);

    std::vector<ExpectedTranslation> translations;
    for (size_t index = 0; index < specials.size(); ++index) {
        translations.push_back({static_cast<uint32_t>(index + 1),
                                kungfu::DeoptType::LAZYDEOPT,
                                {MakeInput(0, DeoptTranslationKind::TAGGED,
                                           DeoptSourceKind::CONSTANT, specials[index])}});
    }

    DeoptTranslationBuilder builder;
    for (size_t index = 0; index < translations.size(); ++index) {
        DeoptId deoptId = builder.AddTranslation(translations[index].bytecodeOffset,
                                                 translations[index].type,
                                                 translations[index].inputs);
        EXPECT_EQ(deoptId.value, index);
    }
    std::vector<uint8_t> encoded = builder.Encode();
    // The composed bytes carry ULEB specialKind 0..5, so this comparison pins the numbering.
    EXPECT_EQ(HexBytes(encoded), HexBytes(ComposeStream(translations)));

    DeoptTranslationReader reader(CopyToChunk(encoded), encoded.size());
    ASSERT_TRUE(reader.IsValid());
    ASSERT_EQ(reader.GetDeoptCount(), SPECIAL_KIND_COUNT);
    std::set<uint8_t> observedKinds;
    for (size_t index = 0; index < specials.size(); ++index) {
        DeoptTranslation body;
        ASSERT_TRUE(reader.GetBody(static_cast<uint32_t>(index), &body));
        EXPECT_EQ(body.bytecodeOffset, static_cast<uint32_t>(index + 1));
        ASSERT_EQ(body.inputs.size(), 1U);
        EXPECT_EQ(OpcodeOfInput(body.inputs[0]), OP_TAGGED_SPECIAL);
        EXPECT_EQ(static_cast<uint8_t>(body.inputs[0].sourceKind),
                  static_cast<uint8_t>(DeoptSourceKind::CONSTANT));
        EXPECT_EQ(static_cast<uint8_t>(body.inputs[0].valueKind),
                  static_cast<uint8_t>(DeoptTranslationKind::TAGGED));
        EXPECT_EQ(body.inputs[0].source, specials[index]);
        EXPECT_EQ(body.inputs[0].vreg, 0);
        observedKinds.insert(SpecialKindOf(body.inputs[0].source));
    }
    EXPECT_EQ(observedKinds.size(), SPECIAL_KIND_COUNT);
    EXPECT_EQ(*observedKinds.begin(), SPECIAL_UNDEFINED);
    EXPECT_EQ(*observedKinds.rbegin(), SPECIAL_EXCEPTION);
    EXPECT_EQ(SpecialKindOf(specials[0]), SPECIAL_UNDEFINED);
    EXPECT_EQ(SpecialKindOf(specials[1]), SPECIAL_NULL);
    EXPECT_EQ(SpecialKindOf(specials[2]), SPECIAL_TRUE);
    EXPECT_EQ(SpecialKindOf(specials[3]), SPECIAL_FALSE);
    EXPECT_EQ(SpecialKindOf(specials[4]), SPECIAL_HOLE);
    EXPECT_EQ(SpecialKindOf(specials[5]), SPECIAL_EXCEPTION);
}

/**
 * @tc.name: RoundTrip_RegisterCodeCoverage
 * @tc.desc: every snapshot-addressable general register code (TAGGED) and every floating register
 *           code (FLOAT64) round-trips through the persisted ULEB register operand
 * @tc.type: FUNC
 */
HWTEST_F_L0(ArkSteedDeoptTranslationTest, RoundTrip_RegisterCodeCoverage)
{
    ASSERT_GT(ARKSTEED_DEOPT_GENERAL_REGISTER_CODES.size(), 0U);
    ASSERT_GT(ARKSTEED_DEOPT_FLOATING_REGISTER_COUNT, 0U);

    std::set<uint32_t> gpCodes;
    std::vector<DeoptTranslationInput> gpInputs;
    int32_t vreg = 0;
    for (uint32_t code : ARKSTEED_DEOPT_GENERAL_REGISTER_CODES) {
        EXPECT_TRUE(gpCodes.insert(code).second) << "duplicate general register code " << code;
        EXPECT_GE(GetArkSteedDeoptGeneralSnapshotOffset(code), 0) << "code " << code;
        gpInputs.push_back(MakeInput(vreg++, DeoptTranslationKind::TAGGED,
                                     DeoptSourceKind::GP_REGISTER, code));
    }
    EXPECT_EQ(gpCodes.size(), ARKSTEED_DEOPT_GENERAL_REGISTER_CODES.size());

    std::vector<DeoptTranslationInput> fpInputs;
    vreg = 0;
    for (uint32_t code = 0; code < ARKSTEED_DEOPT_FLOATING_REGISTER_COUNT; ++code) {
        EXPECT_GE(GetArkSteedDeoptFloatingSnapshotOffset(code), 0) << "code " << code;
        fpInputs.push_back(MakeInput(vreg++, DeoptTranslationKind::FLOAT64_TO_TAGGED_DOUBLE,
                                     DeoptSourceKind::FP_REGISTER, code));
    }

    std::vector<ExpectedTranslation> translations = {
        {11, kungfu::DeoptType::LAZYDEOPT, gpInputs},
        {22, kungfu::DeoptType::LAZYDEOPT, fpInputs},
    };
    DeoptTranslationBuilder builder;
    EXPECT_EQ(builder.AddTranslation(translations[0].bytecodeOffset, translations[0].type,
                                     translations[0].inputs).value, 0U);
    EXPECT_EQ(builder.AddTranslation(translations[1].bytecodeOffset, translations[1].type,
                                     translations[1].inputs).value, 1U);
    std::vector<uint8_t> encoded = builder.Encode();
    EXPECT_EQ(HexBytes(encoded), HexBytes(ComposeStream(translations)));

    DeoptTranslationReader reader(CopyToChunk(encoded), encoded.size());
    ASSERT_TRUE(reader.IsValid());
    ASSERT_EQ(reader.GetDeoptCount(), 2U);
    for (uint32_t bodyId = 0; bodyId < 2U; ++bodyId) {
        DeoptTranslation body;
        ASSERT_TRUE(reader.GetBody(bodyId, &body));
        EXPECT_EQ(body.bytecodeOffset, translations[bodyId].bytecodeOffset);
        ASSERT_EQ(body.inputs.size(), translations[bodyId].inputs.size());
        for (size_t index = 0; index < body.inputs.size(); ++index) {
            SCOPED_TRACE(index);
            EXPECT_EQ(body.inputs[index], translations[bodyId].inputs[index]);
            EXPECT_EQ(body.inputs[index].source, static_cast<int64_t>(translations[bodyId].inputs[index].source));
        }
    }
    EXPECT_EQ(OpcodeOfInput(gpInputs[0]), OP_TAGGED_REGISTER);
    EXPECT_EQ(OpcodeOfInput(fpInputs[0]), OP_FLOAT64_REGISTER);
}

/**
 * @tc.name: RoundTrip_DeduplicatesIdenticalBodiesAndHeaders
 * @tc.desc: identical bodies share a bodyId, a repeated (bodyId, deoptType) pair reuses its
 *           DeoptId, and different bodies stay distinct
 * @tc.type: FUNC
 */
HWTEST_F_L0(ArkSteedDeoptTranslationTest, RoundTrip_DeduplicatesIdenticalBodiesAndHeaders)
{
    const std::vector<DeoptTranslationInput> sharedBody = {
        MakeInput(0, DeoptTranslationKind::TAGGED, DeoptSourceKind::CONSTANT,
                  static_cast<int64_t>(JSTaggedValue(7).GetRawData())),
    };
    const std::vector<DeoptTranslationInput> otherBody = {
        MakeInput(1, DeoptTranslationKind::RAW_INT32, DeoptSourceKind::CONSTANT, 7),
    };

    DeoptTranslationBuilder builder;
    DeoptId first = builder.AddTranslation(10, kungfu::DeoptType::LAZYDEOPT, sharedBody);
    DeoptId second = builder.AddTranslation(10, kungfu::DeoptType::NOTINT1, sharedBody);
    DeoptId repeated = builder.AddTranslation(10, kungfu::DeoptType::LAZYDEOPT, sharedBody);
    DeoptId otherOffset = builder.AddTranslation(11, kungfu::DeoptType::LAZYDEOPT, sharedBody);
    DeoptId otherInputs = builder.AddTranslation(10, kungfu::DeoptType::LAZYDEOPT, otherBody);
    EXPECT_EQ(first, DeoptId{0});
    EXPECT_EQ(second, DeoptId{1});
    EXPECT_EQ(repeated, first) << "an identical (bodyId, deoptType) pair must reuse its DeoptId";
    EXPECT_EQ(otherOffset, DeoptId{2});
    EXPECT_EQ(otherInputs, DeoptId{3});

    std::vector<uint8_t> encoded = builder.Encode();
    ASSERT_GE(encoded.size(), 2 * U32_BYTES);
    // Four distinct headers over three distinct bodies.
    EXPECT_EQ(ReadU32LE(encoded, REF_DEOPT_COUNT_OFFSET), 4U);
    EXPECT_EQ(ReadU32LE(encoded, REF_BODY_COUNT_OFFSET), 3U);

    DeoptTranslationReader reader(CopyToChunk(encoded), encoded.size());
    ASSERT_TRUE(reader.IsValid());
    EXPECT_EQ(reader.GetDeoptCount(), 4U);
    const std::vector<uint32_t> expectedBodyIds = {0, 0, 1, 2};
    const std::vector<kungfu::DeoptType> expectedTypes = {
        kungfu::DeoptType::LAZYDEOPT,
        kungfu::DeoptType::NOTINT1,
        kungfu::DeoptType::LAZYDEOPT,
        kungfu::DeoptType::LAZYDEOPT,
    };
    for (size_t index = 0; index < expectedBodyIds.size(); ++index) {
        SCOPED_TRACE(index);
        DeoptTranslationHeader header {};
        ASSERT_TRUE(reader.GetHeader(DeoptId{static_cast<uint32_t>(index)}, &header));
        EXPECT_EQ(header.bodyId, expectedBodyIds[index]);
        EXPECT_EQ(static_cast<uint8_t>(header.type), static_cast<uint8_t>(expectedTypes[index]));
    }

    DeoptTranslation body0;
    DeoptTranslation body1;
    DeoptTranslation body2;
    ASSERT_TRUE(reader.GetBody(0, &body0));
    ASSERT_TRUE(reader.GetBody(1, &body1));
    ASSERT_TRUE(reader.GetBody(2, &body2));
    DeoptTranslation expectedBody0 {10, sharedBody};
    DeoptTranslation expectedBody1 {11, sharedBody};
    DeoptTranslation expectedBody2 {10, otherBody};
    EXPECT_TRUE(body0.BodyEquals(expectedBody0));
    EXPECT_TRUE(body1.BodyEquals(expectedBody1));
    EXPECT_TRUE(body2.BodyEquals(expectedBody2));
    EXPECT_TRUE(body0.BodyEquals(body1) == false) << "different bytecodeOffset is a different body";
    EXPECT_TRUE(body0.BodyEquals(body2) == false) << "different inputs are a different body";
    EXPECT_FALSE(reader.GetBody(3, &body2));
}

/**
 * @tc.name: RoundTrip_BytecodeOffsetAndDeoptTypeVariety
 * @tc.desc: distinct bytecodeOffset values and distinct deoptType values survive the round trip
 *           with their header/body mapping intact
 * @tc.type: FUNC
 */
HWTEST_F_L0(ArkSteedDeoptTranslationTest, RoundTrip_BytecodeOffsetAndDeoptTypeVariety)
{
    const std::vector<uint32_t> offsets = {0, 1, 127, 128, 300, 0xFFFFFFFFU};
    const std::vector<kungfu::DeoptType> types = {
        kungfu::DeoptType::LAZYDEOPT,
        kungfu::DeoptType::NOTINT1,
        kungfu::DeoptType::NOTINT2,
        kungfu::DeoptType::NOTINT3,
        kungfu::DeoptType::NOTINT4,
        kungfu::DeoptType::NOTINT5,
    };
    ASSERT_EQ(offsets.size(), types.size());

    std::vector<ExpectedTranslation> translations;
    for (size_t index = 0; index < offsets.size(); ++index) {
        translations.push_back({offsets[index],
                                types[index],
                                {MakeInput(static_cast<int32_t>(index),
                                           DeoptTranslationKind::RAW_INT32,
                                           DeoptSourceKind::CONSTANT,
                                           static_cast<int64_t>(index))}});
    }

    DeoptTranslationBuilder builder;
    for (size_t index = 0; index < translations.size(); ++index) {
        EXPECT_EQ(builder.AddTranslation(translations[index].bytecodeOffset,
                                         translations[index].type,
                                         translations[index].inputs).value, index);
    }
    std::vector<uint8_t> encoded = builder.Encode();
    EXPECT_EQ(HexBytes(encoded), HexBytes(ComposeStream(translations)));

    DeoptTranslationReader reader(CopyToChunk(encoded), encoded.size());
    ASSERT_TRUE(reader.IsValid());
    ASSERT_EQ(reader.GetDeoptCount(), offsets.size());
    for (size_t index = 0; index < offsets.size(); ++index) {
        SCOPED_TRACE(index);
        DeoptTranslationHeader header {};
        ASSERT_TRUE(reader.GetHeader(DeoptId{static_cast<uint32_t>(index)}, &header));
        EXPECT_EQ(header.bodyId, index);
        EXPECT_EQ(static_cast<uint8_t>(header.type), static_cast<uint8_t>(types[index]));
        DeoptTranslation body;
        ASSERT_TRUE(reader.GetBody(header.bodyId, &body));
        EXPECT_EQ(body.bytecodeOffset, offsets[index]);
        ASSERT_EQ(body.inputs.size(), 1U);
        EXPECT_EQ(body.inputs[0], translations[index].inputs[0]);
    }
    DeoptTranslationHeader header {};
    EXPECT_FALSE(reader.GetHeader(DeoptId{static_cast<uint32_t>(offsets.size())}, &header));
    DeoptTranslation body;
    EXPECT_FALSE(reader.GetBody(static_cast<uint32_t>(offsets.size()), &body));
}

/**
 * @tc.name: Encode_IsIdempotentAndDoesNotMutateBuilder
 * @tc.desc: repeated Encode() returns the same stream, an empty builder encodes to an empty
 *           stream, and the builder keeps accepting translations afterwards
 * @tc.type: FUNC
 */
HWTEST_F_L0(ArkSteedDeoptTranslationTest, Encode_IsIdempotentAndDoesNotMutateBuilder)
{
    DeoptTranslationBuilder builder;
    EXPECT_TRUE(builder.Encode().empty());

    const std::vector<DeoptTranslationInput> inputs = {
        MakeInput(0, DeoptTranslationKind::TAGGED, DeoptSourceKind::STACK_SLOT, -8),
    };
    EXPECT_EQ(builder.AddTranslation(3, kungfu::DeoptType::LAZYDEOPT, inputs).value, 0U);
    std::vector<uint8_t> first = builder.Encode();
    std::vector<uint8_t> second = builder.Encode();
    EXPECT_EQ(HexBytes(first), HexBytes(second));

    EXPECT_EQ(builder.AddTranslation(99, kungfu::DeoptType::LAZYDEOPT,
                                     {MakeInput(0, DeoptTranslationKind::RAW_INT32,
                                                DeoptSourceKind::CONSTANT, 5)}).value,
              1U);
    std::vector<uint8_t> third = builder.Encode();
    EXPECT_FALSE(first.empty());
    EXPECT_GT(third.size(), first.size());

    DeoptTranslationReader reader(CopyToChunk(third), third.size());
    ASSERT_TRUE(reader.IsValid());
    EXPECT_EQ(reader.GetDeoptCount(), 2U);
    DeoptTranslation body;
    ASSERT_TRUE(reader.GetBody(0, &body));
    EXPECT_EQ(body.bytecodeOffset, 3U);
    ASSERT_EQ(body.inputs.size(), 1U);
    EXPECT_EQ(body.inputs[0], inputs[0]);
    ASSERT_TRUE(reader.GetBody(1, &body));
    EXPECT_EQ(body.bytecodeOffset, 99U);
}

/**
 * @tc.name: Float64RawBits_MatchHandComputedPatterns
 * @tc.desc: GetFloat64RawBits returns the hand-computed IEEE-754 bit patterns used by the
 *           FLOAT64_CONSTANT and TAGGED_DOUBLE_BITS operands
 * @tc.type: FUNC
 */
HWTEST_F_L0(ArkSteedDeoptTranslationTest, Float64RawBits_MatchHandComputedPatterns)
{
    EXPECT_EQ(GetFloat64RawBits(0.0), static_cast<int64_t>(0x0000000000000000ULL));
    EXPECT_EQ(GetFloat64RawBits(-0.0), static_cast<int64_t>(0x8000000000000000ULL));
    EXPECT_EQ(GetFloat64RawBits(1.0), static_cast<int64_t>(0x3FF0000000000000ULL));
    // 0x3FF8000000000000: IEEE-754 binary64 bits of 1.5
    EXPECT_EQ(GetFloat64RawBits(1.5), static_cast<int64_t>(0x3FF8000000000000ULL));
    // 0x4014000000000000: IEEE-754 binary64 bits of 5.0
    EXPECT_EQ(GetFloat64RawBits(5.0), static_cast<int64_t>(0x4014000000000000ULL));
    // 0xC004000000000000: IEEE-754 binary64 bits of -2.5
    EXPECT_EQ(GetFloat64RawBits(-2.5), static_cast<int64_t>(0xC004000000000000ULL));
    EXPECT_EQ(GetFloat64RawBits(std::numeric_limits<double>::infinity()),
              static_cast<int64_t>(0x7FF0000000000000ULL));
    EXPECT_EQ(GetFloat64RawBits(-std::numeric_limits<double>::infinity()),
              static_cast<int64_t>(0xFFF0000000000000ULL));

    int64_t nanBits = GetFloat64RawBits(std::numeric_limits<double>::quiet_NaN());
    EXPECT_EQ(nanBits & 0x7FF0000000000000LL, 0x7FF0000000000000LL) << "NaN exponent must be all ones";
    EXPECT_NE(nanBits & 0x000FFFFFFFFFFFFFLL, 0) << "NaN mantissa must be non-zero";

    // Both persisted operands carry the same raw 64-bit pattern.
    std::vector<ExpectedTranslation> translations = {
        {4,
         kungfu::DeoptType::LAZYDEOPT,
         {MakeInput(0, DeoptTranslationKind::FLOAT64_TO_TAGGED_DOUBLE,
                    DeoptSourceKind::CONSTANT, GetFloat64RawBits(1.5))}},
        {8,
         kungfu::DeoptType::LAZYDEOPT,
         {MakeInput(0, DeoptTranslationKind::TAGGED, DeoptSourceKind::CONSTANT,
                    static_cast<int64_t>(JSTaggedValue(1.5).GetRawData()))}},
    };
    DeoptTranslationBuilder builder;
    for (const auto &translation : translations) {
        builder.AddTranslation(translation.bytecodeOffset, translation.type, translation.inputs);
    }
    std::vector<uint8_t> encoded = builder.Encode();
    EXPECT_EQ(HexBytes(encoded), HexBytes(ComposeStream(translations)));

    DeoptTranslationReader reader(CopyToChunk(encoded), encoded.size());
    ASSERT_TRUE(reader.IsValid());
    DeoptTranslation body;
    ASSERT_TRUE(reader.GetBody(0, &body));
    ASSERT_EQ(body.inputs.size(), 1U);
    EXPECT_EQ(body.inputs[0].source, GetFloat64RawBits(1.5));
    double untagged = 0.0;
    uint64_t rawBits = static_cast<uint64_t>(body.inputs[0].source);
    ASSERT_EQ(memcpy_s(&untagged, sizeof(untagged), &rawBits, sizeof(rawBits)), EOK);
    EXPECT_DOUBLE_EQ(untagged, 1.5);
    ASSERT_TRUE(reader.GetBody(1, &body));
    ASSERT_EQ(body.inputs.size(), 1U);
    EXPECT_EQ(body.inputs[0].source, static_cast<int64_t>(JSTaggedValue(1.5).GetRawData()));
    JSTaggedValue tagged(static_cast<JSTaggedType>(body.inputs[0].source));
    EXPECT_TRUE(tagged.IsDouble());
    EXPECT_DOUBLE_EQ(tagged.GetDouble(), 1.5);
}

/**
 * @tc.name: ConstantSource_Int32Float64AndTaggedVertices
 * @tc.desc: GetConstantSourceForDeoptTranslation applies the documented per-kind mapping for
 *           Int32Constant, Int64Constant, TaggedConstant and Float64Constant vertices
 * @tc.type: FUNC
 */
HWTEST_F_L0(ArkSteedDeoptTranslationTest, ConstantSource_Int32Float64AndTaggedVertices)
{
    constexpr int32_t INT32_VALUE = -123456;

    ValueVertex *int32Vertex = NewInt32Constant(INT32_VALUE);
    EXPECT_EQ(GetConstantSourceForDeoptTranslation(int32Vertex, DeoptTranslationKind::INT32_TO_TAGGED),
              static_cast<int64_t>(INT32_VALUE));
    EXPECT_EQ(GetConstantSourceForDeoptTranslation(int32Vertex, DeoptTranslationKind::RAW_INT32),
              static_cast<int64_t>(INT32_VALUE));
    EXPECT_EQ(GetConstantSourceForDeoptTranslation(int32Vertex, DeoptTranslationKind::TAGGED),
              static_cast<int64_t>(JSTaggedValue(INT32_VALUE).GetRawData()));

    constexpr int64_t INT64_VALUE = 0x1'2345'6789LL;
    ValueVertex *int64Vertex = NewInt64Constant(INT64_VALUE);
    EXPECT_EQ(GetConstantSourceForDeoptTranslation(int64Vertex, DeoptTranslationKind::RAW_INT32),
              INT64_VALUE);
    // The tagged mapping truncates an Int64Constant to its int32 view before tagging.
    EXPECT_EQ(GetConstantSourceForDeoptTranslation(int64Vertex, DeoptTranslationKind::TAGGED),
              static_cast<int64_t>(JSTaggedValue(static_cast<int>(INT64_VALUE)).GetRawData()));

    ValueVertex *taggedVertex = NewTaggedConstant(static_cast<int64_t>(JSTaggedValue(42).GetRawData()));
    EXPECT_EQ(GetConstantSourceForDeoptTranslation(taggedVertex, DeoptTranslationKind::TAGGED),
              static_cast<int64_t>(JSTaggedValue(42).GetRawData()));

    ValueVertex *doubleVertex = NewFloat64Constant(1.5);
    EXPECT_EQ(GetConstantSourceForDeoptTranslation(doubleVertex,
                                                   DeoptTranslationKind::FLOAT64_TO_TAGGED_DOUBLE),
              static_cast<int64_t>(0x3FF8000000000000ULL));

    // Special tagged constants pass through unchanged and keep the documented specialKind order.
    const std::vector<int64_t> specials = {
        static_cast<int64_t>(JSTaggedValue::Undefined().GetRawData()),
        static_cast<int64_t>(JSTaggedValue::Null().GetRawData()),
        static_cast<int64_t>(JSTaggedValue::True().GetRawData()),
        static_cast<int64_t>(JSTaggedValue::False().GetRawData()),
        static_cast<int64_t>(JSTaggedValue::Hole().GetRawData()),
        static_cast<int64_t>(JSTaggedValue::Exception().GetRawData()),
    };
    for (size_t index = 0; index < specials.size(); ++index) {
        SCOPED_TRACE(index);
        ValueVertex *specialVertex = NewTaggedConstant(specials[index]);
        EXPECT_EQ(GetConstantSourceForDeoptTranslation(specialVertex, DeoptTranslationKind::TAGGED),
                  specials[index]);
        EXPECT_EQ(SpecialKindOf(specials[index]), static_cast<uint8_t>(index));
    }

    // The produced sources are accepted by the builder and survive the round trip.
    std::vector<DeoptTranslationInput> inputs = {
        MakeInput(0, DeoptTranslationKind::INT32_TO_TAGGED, DeoptSourceKind::CONSTANT,
                  GetConstantSourceForDeoptTranslation(int32Vertex, DeoptTranslationKind::INT32_TO_TAGGED)),
        MakeInput(1, DeoptTranslationKind::TAGGED, DeoptSourceKind::CONSTANT,
                  GetConstantSourceForDeoptTranslation(taggedVertex, DeoptTranslationKind::TAGGED)),
        MakeInput(2, DeoptTranslationKind::FLOAT64_TO_TAGGED_DOUBLE, DeoptSourceKind::CONSTANT,
                  GetConstantSourceForDeoptTranslation(doubleVertex,
                                                       DeoptTranslationKind::FLOAT64_TO_TAGGED_DOUBLE)),
    };
    ExpectedTranslation expected {17, kungfu::DeoptType::LAZYDEOPT, inputs};
    DeoptTranslationBuilder builder;
    builder.AddTranslation(expected.bytecodeOffset, expected.type, expected.inputs);
    std::vector<uint8_t> encoded = builder.Encode();
    EXPECT_EQ(HexBytes(encoded), HexBytes(ComposeStream({expected})));
    DeoptTranslationReader reader(CopyToChunk(encoded), encoded.size());
    ASSERT_TRUE(reader.IsValid());
    DeoptTranslation body;
    ASSERT_TRUE(reader.GetBody(0, &body));
    EXPECT_EQ(body.inputs, inputs);
}

/**
 * @tc.name: Negative_InvalidStreamPrefixes
 * @tc.desc: null, short, zero-count and inconsistent headers are rejected without touching the
 *           output structures
 * @tc.type: FUNC
 */
HWTEST_F_L0(ArkSteedDeoptTranslationTest, Negative_InvalidStreamPrefixes)
{
    DeoptTranslationHeader header {};
    DeoptTranslation body;
    const uint32_t SENTINEL_OFFSET = 0x5A5A5A5AU;

    {
        DeoptTranslationReader reader(nullptr, 0);
        EXPECT_FALSE(reader.IsValid());
        EXPECT_EQ(reader.GetDeoptCount(), 0U);
        EXPECT_FALSE(reader.GetHeader(DeoptId{0}, &header));
        EXPECT_FALSE(reader.GetBody(0, &body));
    }
    {
        std::vector<uint8_t> empty;
        DeoptTranslationReader reader(empty.data(), 0);
        EXPECT_FALSE(reader.IsValid());
        EXPECT_EQ(reader.GetDeoptCount(), 0U);
    }
    {
        // 2: u32 header fields; one byte short of the minimum size
        std::vector<uint8_t> tooShort(2 * U32_BYTES - 1U, 0U);
        DeoptTranslationReader reader(tooShort.data(), tooShort.size());
        EXPECT_FALSE(reader.IsValid());
    }
    {
        // deoptCount == 0.
        std::vector<uint8_t> zeroCounts(2 * U32_BYTES, 0U);
        DeoptTranslationReader reader(zeroCounts.data(), zeroCounts.size());
        EXPECT_FALSE(reader.IsValid());
    }
    {
        // bodyCount == 0.
        std::vector<uint8_t> zeroBodies(2 * U32_BYTES, 0U);
        WriteU32LE(zeroBodies, REF_DEOPT_COUNT_OFFSET, 1U);
        DeoptTranslationReader reader(zeroBodies.data(), zeroBodies.size());
        EXPECT_FALSE(reader.IsValid());
    }
    {
        // bodyCount > deoptCount.
        std::vector<uint8_t> tooManyBodies(2 * U32_BYTES, 0U);
        WriteU32LE(tooManyBodies, REF_DEOPT_COUNT_OFFSET, 1U);
        WriteU32LE(tooManyBodies, REF_BODY_COUNT_OFFSET, 2U);
        DeoptTranslationReader reader(tooManyBodies.data(), tooManyBodies.size());
        EXPECT_FALSE(reader.IsValid());
    }
    {
        // Valid header and offset table, but an empty body stream.
        std::vector<uint8_t> noStream(2 * U32_BYTES + TRANSLATION_HEADER_BYTES + U32_BYTES, 0U);
        WriteU32LE(noStream, REF_DEOPT_COUNT_OFFSET, 1U);
        WriteU32LE(noStream, REF_BODY_COUNT_OFFSET, 1U);
        noStream[REF_HEADER0_OFFSET + U32_BYTES] = static_cast<uint8_t>(kungfu::DeoptType::LAZYDEOPT);
        DeoptTranslationReader reader(noStream.data(), noStream.size());
        EXPECT_FALSE(reader.IsValid());
    }
    {
        // An invalid reader must not write a partial result into the output structure.
        DeoptTranslationReader reader(nullptr, 0);
        body.bytecodeOffset = SENTINEL_OFFSET;
        body.inputs = {MakeInput(5, DeoptTranslationKind::RAW_INT32, DeoptSourceKind::CONSTANT, 1)};
        EXPECT_FALSE(reader.GetBody(0, &body));
        EXPECT_EQ(body.bytecodeOffset, SENTINEL_OFFSET);
        ASSERT_EQ(body.inputs.size(), 1U);
        EXPECT_EQ(body.inputs[0].vreg, 5);
    }
}

/**
 * @tc.name: Negative_TruncatedStreamIsRejectedAtEveryCut
 * @tc.desc: removing any suffix of the reference stream is rejected by the reader whenever the
 *           surviving prefix is structurally valid; it never reads outside the buffer
 * @tc.type: FUNC
 */
HWTEST_F_L0(ArkSteedDeoptTranslationTest, Negative_TruncatedStreamIsRejectedAtEveryCut)
{
    const std::vector<uint8_t> reference = ReferenceStreamBytes();
    const std::vector<DeoptTranslationInput> lastBodyInputs = {
        MakeInput(7, DeoptTranslationKind::INT32_TO_TAGGED, DeoptSourceKind::CONSTANT, 42),
    };
    const DeoptTranslation lastBodyExpected {REFERENCE_BYTECODE_OFFSET1, lastBodyInputs};

    // Sensitivity of the required criterion. The reference last body ends with the mandatory
    // one-byte value operand 0x2A (constant 42); the body cannot be decoded without it. A
    // successful decode of a truncated stream can nevertheless return a body whose content
    // merely differs from the reference -- here the same operand substituted with 0x07 is
    // accepted by the real reader and yields a different body. The historical "different
    // content is acceptable" check tolerated exactly that outcome, so a decoder regression
    // that substituted or dropped the removed operand would have passed it. Contract C9
    // requires the opposite: every successful decode of a truncated stream is a failure.
    {
        std::vector<uint8_t> substituted = reference;
        substituted[REFERENCE_STREAM_SIZE - 1U] = 0x07U;
        DeoptTranslationReader substitutedReader(CopyToChunk(substituted), substituted.size());
        ASSERT_TRUE(substitutedReader.IsValid());
        DeoptTranslation substitutedBody;
        ASSERT_TRUE(substitutedReader.GetBody(1, &substitutedBody));
        EXPECT_FALSE(substitutedBody.BodyEquals(lastBodyExpected));
    }

    size_t validPrefixCuts = 0;
    for (size_t cut = 1; cut <= reference.size(); ++cut) {
        SCOPED_TRACE(cut);
        std::vector<uint8_t> truncated(reference.begin(), reference.end() - cut);
        const uint8_t *memory = CopyToChunk(truncated);
        DeoptTranslationReader reader(memory, truncated.size());
        if (!reader.IsValid()) {
            // The surviving prefix is structurally incomplete: no translation is readable at
            // all.
            EXPECT_EQ(reader.GetDeoptCount(), 0U);
            continue;
        }
        ++validPrefixCuts;
        // The surviving prefix still parses, but the last body's byte range ends at the
        // truncated buffer end. The removed suffix contains operand bytes the decoder must
        // consume, so rejection is required unconditionally: a successful decode -- even one
        // whose content differs from the reference, as the substituted-operand control above
        // demonstrates -- must fail this case.
        DeoptTranslation lastBody;
        EXPECT_FALSE(reader.GetBody(1, &lastBody));
        // Body ids outside the surviving table stay rejected.
        DeoptTranslation body;
        EXPECT_FALSE(reader.GetBody(2, &body));
    }
    // The sweep must actually exercise structurally valid truncated prefixes; otherwise the
    // rejection assertions above would be vacuous.
    EXPECT_GT(validPrefixCuts, 0U);
    // Positive control: the untruncated stream still decodes the last body.
    const uint8_t *memory = CopyToChunk(reference);
    DeoptTranslationReader reader(memory, reference.size());
    ASSERT_TRUE(reader.IsValid());
    DeoptTranslation lastBody;
    ASSERT_TRUE(reader.GetBody(1, &lastBody));
    EXPECT_TRUE(lastBody.BodyEquals(lastBodyExpected));
}

/**
 * @tc.name: Negative_UnterminatedAndOverflowingLeb128
 * @tc.desc: unterminated, non-canonical and 64-bit-overflowing LEB128 operands are rejected
 * @tc.type: FUNC
 */
HWTEST_F_L0(ArkSteedDeoptTranslationTest, Negative_UnterminatedAndOverflowingLeb128)
{
    auto decodeFirstBody = [this](const std::vector<uint8_t> &body, DeoptTranslation *decoded) {
        std::vector<uint8_t> stream = WrapRawBodies({body});
        DeoptTranslationReader reader(CopyToChunk(stream), stream.size());
        if (!reader.IsValid()) {
            return false;
        }
        return reader.GetBody(0, decoded);
    };

    // Positive control for the wrapper: a well-formed single body decodes.
    {
        std::vector<uint8_t> goodBody = {OP_BEGIN, 0x05, 0x01, OP_RAW_INT32_CONSTANT, 0x00, 0x2A};
        DeoptTranslation body;
        ASSERT_TRUE(decodeFirstBody(goodBody, &body));
        EXPECT_EQ(body.bytecodeOffset, 5U);
        ASSERT_EQ(body.inputs.size(), 1U);
        EXPECT_EQ(body.inputs[0],
                  MakeInput(0, DeoptTranslationKind::RAW_INT32, DeoptSourceKind::CONSTANT, 42));
    }
    // Unterminated ULEB128 bytecodeOffset.
    {
        std::vector<uint8_t> body = {OP_BEGIN};
        body.insert(body.end(), 11U, 0x80U);
        DeoptTranslation decoded;
        EXPECT_FALSE(decodeFirstBody(body, &decoded));
    }
    // ULEB128 overflow: nine 0xFF payload bytes plus a payload byte with more than one bit at
    // shift 63.
    {
        std::vector<uint8_t> body = {OP_BEGIN};
        body.insert(body.end(), 9U, 0xFFU);
        body.push_back(0x03U);
        DeoptTranslation decoded;
        EXPECT_FALSE(decodeFirstBody(body, &decoded));
    }
    // Non-canonical (non-shortest) ULEB128 bytecodeOffset: 0x85 0x00.
    {
        std::vector<uint8_t> body = {OP_BEGIN, 0x85, 0x00, 0x01};
        DeoptTranslation decoded;
        EXPECT_FALSE(decodeFirstBody(body, &decoded));
    }
    // Unterminated SLEB128 vreg.
    {
        std::vector<uint8_t> body = {OP_BEGIN, 0x05, 0x01, OP_RAW_INT32_CONSTANT, 0x80, 0x80, 0x80};
        DeoptTranslation decoded;
        EXPECT_FALSE(decodeFirstBody(body, &decoded));
    }
    // SLEB128 vreg above the int32 range: 2^32 == 80 80 80 80 10.
    {
        std::vector<uint8_t> body = {OP_BEGIN, 0x05, 0x01, OP_RAW_INT32_CONSTANT,
                                     0x80, 0x80, 0x80, 0x80, 0x10, 0x00};
        DeoptTranslation decoded;
        EXPECT_FALSE(decodeFirstBody(body, &decoded));
    }
    // Unterminated SLEB128 source operand.
    {
        std::vector<uint8_t> body = {OP_BEGIN, 0x05, 0x01, OP_RAW_INT32_CONSTANT, 0x00,
                                     0x80, 0x80, 0x80};
        DeoptTranslation decoded;
        EXPECT_FALSE(decodeFirstBody(body, &decoded));
    }
    // SLEB128 source above the int32 range.
    {
        std::vector<uint8_t> body = {OP_BEGIN, 0x05, 0x01, OP_RAW_INT32_CONSTANT, 0x00,
                                     0x80, 0x80, 0x80, 0x80, 0x10};
        DeoptTranslation decoded;
        EXPECT_FALSE(decodeFirstBody(body, &decoded));
    }
}

/**
 * @tc.name: Negative_InputCountAndOpcodeCorruption
 * @tc.desc: a wrong inputCount, non-increasing vregs, reserved opcodes, an out-of-range register
 *           code and non-double TAGGED_DOUBLE_BITS bits are rejected
 * @tc.type: FUNC
 */
HWTEST_F_L0(ArkSteedDeoptTranslationTest, Negative_InputCountAndOpcodeCorruption)
{
    auto decodeFirstBody = [this](const std::vector<uint8_t> &body, DeoptTranslation *decoded) {
        std::vector<uint8_t> stream = WrapRawBodies({body});
        DeoptTranslationReader reader(CopyToChunk(stream), stream.size());
        if (!reader.IsValid()) {
            return false;
        }
        return reader.GetBody(0, decoded);
    };

    // inputCount claims three inputs, the body carries two.
    {
        std::vector<uint8_t> body = {OP_BEGIN, 0x01, 0x03,
                                     OP_RAW_INT32_CONSTANT, 0x00, 0x01,
                                     OP_RAW_INT32_CONSTANT, 0x01, 0x02};
        DeoptTranslation decoded;
        EXPECT_FALSE(decodeFirstBody(body, &decoded));
    }
    // inputCount far beyond the available bytes.
    {
        std::vector<uint8_t> body = {OP_BEGIN, 0x01, 0xE8, 0x07, OP_RAW_INT32_CONSTANT, 0x00, 0x01};
        DeoptTranslation decoded;
        EXPECT_FALSE(decodeFirstBody(body, &decoded));
    }
    // Repeated vreg.
    {
        std::vector<uint8_t> body = {OP_BEGIN, 0x01, 0x02,
                                     OP_RAW_INT32_CONSTANT, 0x05, 0x07,
                                     OP_RAW_INT32_CONSTANT, 0x05, 0x08};
        DeoptTranslation decoded;
        EXPECT_FALSE(decodeFirstBody(body, &decoded));
    }
    // Decreasing vregs.
    {
        std::vector<uint8_t> body = {OP_BEGIN, 0x01, 0x02,
                                     OP_RAW_INT32_CONSTANT, 0x05, 0x07,
                                     OP_RAW_INT32_CONSTANT, 0x03, 0x08};
        DeoptTranslation decoded;
        EXPECT_FALSE(decodeFirstBody(body, &decoded));
    }
    // BEGIN is not a valid input opcode.
    {
        std::vector<uint8_t> body = {OP_BEGIN, 0x01, 0x01, OP_BEGIN, 0x00, 0x00};
        DeoptTranslation decoded;
        EXPECT_FALSE(decodeFirstBody(body, &decoded));
    }
    // Reserved and out-of-range opcodes.
    {
        std::vector<uint8_t> body = {OP_BEGIN, 0x01, 0x01, OP_COUNT, 0x00, 0x00};
        DeoptTranslation decoded;
        EXPECT_FALSE(decodeFirstBody(body, &decoded));
    }
    {
        std::vector<uint8_t> body = {OP_BEGIN, 0x01, 0x01, 0xFF, 0x00, 0x00};
        DeoptTranslation decoded;
        EXPECT_FALSE(decodeFirstBody(body, &decoded));
    }
    // General register code without a snapshot slot.
    {
        ASSERT_GT(ARKSTEED_DEOPT_GENERAL_REGISTER_CODE_COUNT,
                  ARKSTEED_DEOPT_GENERAL_REGISTER_CODES.size());
        uint32_t invalidCode = 0;
        bool found = false;
        for (uint32_t code = 0; code < ARKSTEED_DEOPT_GENERAL_REGISTER_CODE_COUNT; ++code) {
            if (GetArkSteedDeoptGeneralSnapshotOffset(code) < 0) {
                invalidCode = code;
                found = true;
                break;
            }
        }
        ASSERT_TRUE(found);
        std::vector<uint8_t> body = {OP_BEGIN, 0x01, 0x01, OP_TAGGED_REGISTER, 0x00};
        PushULEB128(body, invalidCode);
        DeoptTranslation decoded;
        EXPECT_FALSE(decodeFirstBody(body, &decoded));
    }
    // Floating register code outside the snapshot.
    {
        std::vector<uint8_t> body = {OP_BEGIN, 0x01, 0x01, OP_FLOAT64_REGISTER, 0x00};
        PushULEB128(body, ARKSTEED_DEOPT_FLOATING_REGISTER_COUNT);
        DeoptTranslation decoded;
        EXPECT_FALSE(decodeFirstBody(body, &decoded));
    }
    // TAGGED_DOUBLE_BITS carrying a non-double tagged pattern.
    {
        std::vector<uint8_t> body = {OP_BEGIN, 0x01, 0x01, OP_TAGGED_DOUBLE_BITS, 0x00};
        PushU64LE(body, static_cast<uint64_t>(JSTaggedValue(42).GetRawData()));
        DeoptTranslation decoded;
        EXPECT_FALSE(decodeFirstBody(body, &decoded));
    }
    // Positive control: the same body shape with a valid double pattern decodes.
    {
        std::vector<uint8_t> body = {OP_BEGIN, 0x01, 0x01, OP_TAGGED_DOUBLE_BITS, 0x00};
        PushU64LE(body, static_cast<uint64_t>(JSTaggedValue(1.5).GetRawData()));
        DeoptTranslation decoded;
        ASSERT_TRUE(decodeFirstBody(body, &decoded));
        ASSERT_EQ(decoded.inputs.size(), 1U);
        EXPECT_TRUE(JSTaggedValue(static_cast<JSTaggedType>(decoded.inputs[0].source)).IsDouble());
    }
}

/**
 * @tc.name: Negative_BodyIdOffsetAndTypeCorruption
 * @tc.desc: a header bodyId outside the table, an invalid deoptType, a non-monotonic offset table
 *           and a body not starting with BEGIN are rejected
 * @tc.type: FUNC
 */
HWTEST_F_L0(ArkSteedDeoptTranslationTest, Negative_BodyIdOffsetAndTypeCorruption)
{
    const std::vector<uint8_t> reference = ReferenceStreamBytes();

    auto decode = [this](const std::vector<uint8_t> &stream, uint32_t bodyId, DeoptTranslation *body) {
        DeoptTranslationReader reader(CopyToChunk(stream), stream.size());
        if (!reader.IsValid()) {
            return false;
        }
        return reader.GetBody(bodyId, body);
    };

    // Header bodyId outside the offset table.
    {
        std::vector<uint8_t> mutated = reference;
        WriteU32LE(mutated, REF_HEADER1_OFFSET, 2U);
        DeoptTranslationReader reader(CopyToChunk(mutated), mutated.size());
        ASSERT_TRUE(reader.IsValid());
        DeoptTranslationHeader header {};
        EXPECT_FALSE(reader.GetHeader(DeoptId{1}, &header));
        ASSERT_TRUE(reader.GetHeader(DeoptId{0}, &header));
        EXPECT_EQ(header.bodyId, 0U);
        DeoptTranslation body;
        EXPECT_FALSE(reader.GetBody(2, &body));
    }
    // Header deoptType = NONE (below LAZYDEOPT) and = COUNT (one past the last valid type).
    {
        std::vector<uint8_t> mutated = reference;
        mutated[REF_HEADER0_OFFSET + U32_BYTES] = 0U;
        DeoptTranslationReader reader(CopyToChunk(mutated), mutated.size());
        ASSERT_TRUE(reader.IsValid());
        DeoptTranslationHeader header {};
        EXPECT_FALSE(reader.GetHeader(DeoptId{0}, &header));
    }
    {
        std::vector<uint8_t> mutated = reference;
        mutated[REF_HEADER1_OFFSET + U32_BYTES] = static_cast<uint8_t>(kungfu::DeoptType::COUNT);
        DeoptTranslationReader reader(CopyToChunk(mutated), mutated.size());
        ASSERT_TRUE(reader.IsValid());
        DeoptTranslationHeader header {};
        EXPECT_FALSE(reader.GetHeader(DeoptId{1}, &header));
    }
    // Offset table not strictly increasing: body 1 is rejected and the range of body 0, whose end
    // is derived from the corrupted offset, collapses as well.
    {
        std::vector<uint8_t> mutated = reference;
        WriteU32LE(mutated, REF_OFFSET1_OFFSET, 0U);
        DeoptTranslation body;
        EXPECT_FALSE(decode(mutated, 1, &body));
        EXPECT_FALSE(decode(mutated, 0, &body));
    }
    // Offset beyond the body stream.
    {
        std::vector<uint8_t> mutated = reference;
        WriteU32LE(mutated, REF_OFFSET_TABLE_OFFSET, 1000U);
        DeoptTranslation body;
        EXPECT_FALSE(decode(mutated, 0, &body));
    }
    // Body 0 does not start with the BEGIN opcode: its own range is rejected while the intact
    // second body still decodes.
    {
        std::vector<uint8_t> mutated = reference;
        mutated[REF_BODY_STREAM_OFFSET] = OP_TAGGED_REGISTER;
        DeoptTranslation body;
        EXPECT_FALSE(decode(mutated, 0, &body));
        EXPECT_TRUE(decode(mutated, 1, &body));
    }
    // The BEGIN opcode of the last body is also the end marker of the previous body range, so
    // corrupting it invalidates both bodies.
    {
        std::vector<uint8_t> mutated = reference;
        mutated[REF_BODY_STREAM_OFFSET + 6U] = OP_TAGGED_REGISTER;
        DeoptTranslation body;
        EXPECT_FALSE(decode(mutated, 0, &body));
        EXPECT_FALSE(decode(mutated, 1, &body));
    }
    // Truncated body stream of the last body: its range must stay inside the buffer.
    {
        std::vector<uint8_t> mutated = reference;
        mutated.resize(REFERENCE_STREAM_SIZE - 1U);
        DeoptTranslation body;
        EXPECT_FALSE(decode(mutated, 1, &body));
    }
}

/**
 * @tc.name: NegativeControl_MutationOfExpectedByteAndFieldIsDetected
 * @tc.desc: negative control (P0): a single mutated byte breaks the byte-exact expectation, and a
 *           mutated header field is observed through the reader
 * @tc.type: FUNC
 */
HWTEST_F_L0(ArkSteedDeoptTranslationTest, NegativeControl_MutationOfExpectedByteAndFieldIsDetected)
{
    const std::vector<uint8_t> expected = ReferenceStreamBytes();

    DeoptTranslationBuilder builder;
    builder.AddTranslation(REFERENCE_BYTECODE_OFFSET0, kungfu::DeoptType::LAZYDEOPT,
                           {MakeInput(3, DeoptTranslationKind::TAGGED, DeoptSourceKind::STACK_SLOT,
                                      -16)});
    builder.AddTranslation(REFERENCE_BYTECODE_OFFSET1, kungfu::DeoptType::NOTINT1,
                           {MakeInput(7, DeoptTranslationKind::INT32_TO_TAGGED,
                                      DeoptSourceKind::CONSTANT, 42)});
    const std::vector<uint8_t> actual = builder.Encode();
    ASSERT_EQ(HexBytes(actual), HexBytes(expected));

    // Mutating either side of the byte-exact expectation is detected for every structural byte
    // position: deoptCount, both header types, the offset table and the body stream.
    const std::vector<size_t> positions = {
        REF_DEOPT_COUNT_OFFSET,
        REF_BODY_COUNT_OFFSET,
        REF_HEADER0_OFFSET + U32_BYTES,
        REF_HEADER1_OFFSET + U32_BYTES,
        REF_OFFSET_TABLE_OFFSET,
        REF_OFFSET1_OFFSET,
        REF_BODY_STREAM_OFFSET,
        REFERENCE_STREAM_SIZE - 1U,
    };
    for (size_t position : positions) {
        SCOPED_TRACE(position);
        std::vector<uint8_t> mutatedExpected = expected;
        mutatedExpected[position] ^= 0x01U;
        EXPECT_NE(HexBytes(actual), HexBytes(mutatedExpected));
        std::vector<uint8_t> mutatedActual = actual;
        mutatedActual[position] ^= 0x01U;
        EXPECT_NE(HexBytes(mutatedActual), HexBytes(expected));
    }

    // Mutating the persisted deopt type (NOTINT1 -> NOTINT2) is observed as a different header
    // field, so a test asserting the original type would fail.
    {
        std::vector<uint8_t> typeMutated = actual;
        typeMutated[REF_HEADER1_OFFSET + U32_BYTES] = static_cast<uint8_t>(kungfu::DeoptType::NOTINT2);
        DeoptTranslationReader reader(CopyToChunk(typeMutated), typeMutated.size());
        ASSERT_TRUE(reader.IsValid());
        DeoptTranslationHeader header {};
        ASSERT_TRUE(reader.GetHeader(DeoptId{1}, &header));
        EXPECT_EQ(static_cast<uint8_t>(header.type), static_cast<uint8_t>(kungfu::DeoptType::NOTINT2));
        EXPECT_NE(static_cast<uint8_t>(header.type), static_cast<uint8_t>(kungfu::DeoptType::NOTINT1));
    }
    // Mutating the persisted deoptCount is observed as a different count.
    {
        std::vector<uint8_t> countMutated = actual;
        countMutated[REF_DEOPT_COUNT_OFFSET] = 3U;
        DeoptTranslationReader reader(CopyToChunk(countMutated), countMutated.size());
        EXPECT_EQ(reader.GetDeoptCount(), 3U);
        EXPECT_NE(reader.GetDeoptCount(), 2U);
    }
    // Mutating one byte of the first body's vreg is observed as a different decoded vreg.
    {
        std::vector<uint8_t> vregMutated = actual;
        vregMutated[REF_BODY_STREAM_OFFSET + 4U] = 0x04U;  // vreg 3 -> 4
        DeoptTranslationReader reader(CopyToChunk(vregMutated), vregMutated.size());
        ASSERT_TRUE(reader.IsValid());
        DeoptTranslation body;
        ASSERT_TRUE(reader.GetBody(0, &body));
        ASSERT_EQ(body.inputs.size(), 1U);
        EXPECT_EQ(body.inputs[0].vreg, 4);
        EXPECT_NE(body.inputs[0].vreg, 3);
    }
}

}  // namespace panda::test
