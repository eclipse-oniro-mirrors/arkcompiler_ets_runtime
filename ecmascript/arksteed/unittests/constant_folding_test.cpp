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

// Unit domain G4.1u (matrix row G4.1u, contract C6 of
// `arksteed-cpp-unit-test-domains`): local constant folding over the public
// `TryFoldBinaryConstant` / `TryFoldUnaryConstant` / `TryFoldToBooleanConstant`
// seam. Expectations are computed from ECMA-262 / IEEE-754 and hand-evaluated
// constants (D2); the production expression itself is never used as an oracle.
//
// Fixture A (H4): allocator-backed chunk and graph with case lifetime; no VM is
// created. Only tagged constants are foldable at this seam
// (`PrimitiveConstant::FromVertex` accepts `TaggedConstantVertex` only), so
// `HeapConstantVertex` and the internal typed `Int32ConstantVertex` /
// `Float64ConstantVertex` are exercised as explicit rejections.

#include "ecmascript/arksteed/arksteed_constant_folding.h"

#include "ecmascript/arksteed/arksteed_graph.h"
#include "ecmascript/arksteed/arksteed_opcode.h"
#include "ecmascript/base/number_helper.h"
#include "ecmascript/mem/native_area_allocator.h"
#include "ecmascript/tests/test_helper.h"

#include <cmath>
#include <limits>

namespace panda::ecmascript::arksteed {
namespace {

struct ConstantFoldingFixture {
    NativeAreaAllocator native;
    Chunk chunk {&native};
    Graph graph {&chunk};
};

ValueVertex *MakeTagged(Chunk *chunk, JSTaggedValue value)
{
    return Vertex::New<TaggedConstantVertex>(chunk, static_cast<size_t>(0),
                                             static_cast<uint64_t>(value.GetRawData()));
}

ValueVertex *MakeInt32(Chunk *chunk, int32_t value)
{
    return Vertex::New<Int32ConstantVertex>(chunk, static_cast<size_t>(0), value);
}

ValueVertex *MakeFloat64(Chunk *chunk, double value)
{
    return Vertex::New<Float64ConstantVertex>(chunk, static_cast<size_t>(0), value);
}

ValueVertex *MakeHeap(Chunk *chunk)
{
    return Vertex::New<HeapConstantVertex>(chunk, static_cast<size_t>(0), 0U, static_cast<uint16_t>(0));
}

JSTaggedValue TInt(int32_t value)
{
    return JSTaggedValue(value);
}

JSTaggedValue TDouble(double value)
{
    return JSTaggedValue(value);
}

JSTaggedValue TNaN()
{
    return JSTaggedValue(base::NumberHelper::GetNaN());
}

JSTaggedValue TInfinity()
{
    return JSTaggedValue(std::numeric_limits<double>::infinity());
}

JSTaggedValue TNegInfinity()
{
    return JSTaggedValue(-std::numeric_limits<double>::infinity());
}

JSTaggedValue TPositiveZero()
{
    return JSTaggedValue(0.0);
}

JSTaggedValue TNegativeZero()
{
    return JSTaggedValue(-0.0);
}

struct FoldResult {
    bool folded = false;
    JSTaggedValue value {0};
};

FoldResult FoldBinary(Chunk *chunk, JSTaggedValue lhs, JSTaggedValue rhs, BinaryFoldOp op)
{
    FoldResult out;
    out.folded = TryFoldBinaryConstant(MakeTagged(chunk, lhs), MakeTagged(chunk, rhs), op, &out.value);
    return out;
}

FoldResult FoldUnary(Chunk *chunk, JSTaggedValue input, UnaryFoldOp op)
{
    FoldResult out;
    out.folded = TryFoldUnaryConstant(MakeTagged(chunk, input), op, &out.value);
    return out;
}

void ExpectFoldedInt(const FoldResult &result, int32_t expected)
{
    ASSERT_TRUE(result.folded) << "fold was rejected";
    ASSERT_TRUE(result.value.IsInt()) << "result is not a tagged int32";
    EXPECT_EQ(result.value.GetInt(), expected);
}

void ExpectFoldedDouble(const FoldResult &result, double expected)
{
    ASSERT_TRUE(result.folded) << "fold was rejected";
    ASSERT_TRUE(result.value.IsDouble()) << "result is not a tagged double";
    EXPECT_EQ(result.value.GetDouble(), expected);
}

void ExpectFoldedDoubleSign(const FoldResult &result, double expected)
{
    ASSERT_TRUE(result.folded) << "fold was rejected";
    ASSERT_TRUE(result.value.IsDouble()) << "result is not a tagged double";
    double actual = result.value.GetDouble();
    EXPECT_EQ(actual, expected);
    EXPECT_EQ(std::signbit(actual), std::signbit(expected)) << "zero sign was lost";
}

void ExpectFoldedNaN(const FoldResult &result)
{
    ASSERT_TRUE(result.folded) << "fold was rejected";
    ASSERT_TRUE(result.value.IsDouble()) << "result is not a tagged double";
    EXPECT_TRUE(std::isnan(result.value.GetDouble()));
}

void ExpectFoldedBool(const FoldResult &result, bool expected)
{
    ASSERT_TRUE(result.folded) << "fold was rejected";
    ASSERT_TRUE(result.value.IsTrue() || result.value.IsFalse()) << "result is not a tagged boolean";
    EXPECT_EQ(result.value.IsTrue(), expected);
}

void ExpectUnfolded(const FoldResult &result)
{
    EXPECT_FALSE(result.folded) << "input must not fold at this seam";
}
}  // namespace

class ArkSteedConstantFoldingTest : public testing::Test {
};

HWTEST_F_L0(ArkSteedConstantFoldingTest, BinaryArithmeticInt32)
{
    constexpr int32_t kMax = std::numeric_limits<int32_t>::max();
    constexpr int32_t kMin = std::numeric_limits<int32_t>::min();
    ConstantFoldingFixture f;

    // ADD produces an exact int32 only when the mathematical sum fits.
    ExpectFoldedInt(FoldBinary(&f.chunk, TInt(3), TInt(4), BinaryFoldOp::ADD), 7);
    ExpectFoldedInt(FoldBinary(&f.chunk, TInt(kMax), TInt(0), BinaryFoldOp::ADD), kMax);
    ExpectFoldedInt(FoldBinary(&f.chunk, TInt(kMin), TInt(kMax), BinaryFoldOp::ADD), -1);
    ExpectUnfolded(FoldBinary(&f.chunk, TInt(kMax), TInt(1), BinaryFoldOp::ADD));
    ExpectUnfolded(FoldBinary(&f.chunk, TInt(kMin), TInt(-1), BinaryFoldOp::ADD));

    // SUB
    ExpectFoldedInt(FoldBinary(&f.chunk, TInt(7), TInt(3), BinaryFoldOp::SUB), 4);
    ExpectFoldedInt(FoldBinary(&f.chunk, TInt(0), TInt(kMax), BinaryFoldOp::SUB), -kMax);
    ExpectFoldedInt(FoldBinary(&f.chunk, TInt(kMin), TInt(0), BinaryFoldOp::SUB), kMin);
    ExpectUnfolded(FoldBinary(&f.chunk, TInt(kMin), TInt(1), BinaryFoldOp::SUB));
    ExpectUnfolded(FoldBinary(&f.chunk, TInt(kMax), TInt(-1), BinaryFoldOp::SUB));

    // MUL: a negative factor times zero is left to the stub so that -0 is kept.
    ExpectFoldedInt(FoldBinary(&f.chunk, TInt(5), TInt(6), BinaryFoldOp::MUL), 30);
    ExpectFoldedInt(FoldBinary(&f.chunk, TInt(0), TInt(5), BinaryFoldOp::MUL), 0);
    ExpectFoldedInt(FoldBinary(&f.chunk, TInt(-3), TInt(4), BinaryFoldOp::MUL), -12);
    ExpectUnfolded(FoldBinary(&f.chunk, TInt(-1), TInt(0), BinaryFoldOp::MUL));
    ExpectUnfolded(FoldBinary(&f.chunk, TInt(0), TInt(-1), BinaryFoldOp::MUL));
    ExpectUnfolded(FoldBinary(&f.chunk, TInt(kMax), TInt(2), BinaryFoldOp::MUL));
    ExpectUnfolded(FoldBinary(&f.chunk, TInt(kMin), TInt(-1), BinaryFoldOp::MUL));

    // DIV never folds for two tagged int32 operands (JS division yields a double).
    ExpectUnfolded(FoldBinary(&f.chunk, TInt(6), TInt(3), BinaryFoldOp::DIV));
    ExpectUnfolded(FoldBinary(&f.chunk, TInt(1), TInt(0), BinaryFoldOp::DIV));
}

HWTEST_F_L0(ArkSteedConstantFoldingTest, BinaryArithmeticFloat64)
{
    constexpr double kMax = std::numeric_limits<double>::max();
    constexpr double kInf = std::numeric_limits<double>::infinity();
    ConstantFoldingFixture f;

    // ADD (IEEE-754 binary64).
    ExpectFoldedDouble(FoldBinary(&f.chunk, TDouble(1.5), TDouble(2.25), BinaryFoldOp::ADD), 3.75);
    ExpectFoldedDouble(FoldBinary(&f.chunk, TInfinity(), TDouble(1.0), BinaryFoldOp::ADD), kInf);
    ExpectFoldedNaN(FoldBinary(&f.chunk, TInfinity(), TNegInfinity(), BinaryFoldOp::ADD));
    ExpectFoldedDoubleSign(FoldBinary(&f.chunk, TPositiveZero(), TNegativeZero(), BinaryFoldOp::ADD), 0.0);
    ExpectFoldedDoubleSign(FoldBinary(&f.chunk, TNegativeZero(), TNegativeZero(), BinaryFoldOp::ADD), -0.0);
    ExpectFoldedDouble(FoldBinary(&f.chunk, TDouble(kMax), TDouble(kMax), BinaryFoldOp::ADD), kInf);

    // SUB
    ExpectFoldedDouble(FoldBinary(&f.chunk, TDouble(1.5), TDouble(0.25), BinaryFoldOp::SUB), 1.25);
    ExpectFoldedNaN(FoldBinary(&f.chunk, TInfinity(), TInfinity(), BinaryFoldOp::SUB));
    ExpectFoldedDoubleSign(FoldBinary(&f.chunk, TNegativeZero(), TPositiveZero(), BinaryFoldOp::SUB), -0.0);
    ExpectFoldedDoubleSign(FoldBinary(&f.chunk, TPositiveZero(), TNegativeZero(), BinaryFoldOp::SUB), 0.0);

    // MUL
    ExpectFoldedDouble(FoldBinary(&f.chunk, TDouble(1.5), TDouble(2.0), BinaryFoldOp::MUL), 3.0);
    ExpectFoldedNaN(FoldBinary(&f.chunk, TInfinity(), TPositiveZero(), BinaryFoldOp::MUL));
    ExpectFoldedDoubleSign(FoldBinary(&f.chunk, TNegativeZero(), TDouble(5.0), BinaryFoldOp::MUL), -0.0);
    ExpectFoldedDoubleSign(FoldBinary(&f.chunk, TPositiveZero(), TDouble(-3.0), BinaryFoldOp::MUL), -0.0);

    // DIV: signed infinity and NaN follow ECMA-262 numeric semantics.
    ExpectFoldedDouble(FoldBinary(&f.chunk, TDouble(6.0), TDouble(3.0), BinaryFoldOp::DIV), 2.0);
    ExpectFoldedDouble(FoldBinary(&f.chunk, TDouble(1.0), TPositiveZero(), BinaryFoldOp::DIV), kInf);
    ExpectFoldedDouble(FoldBinary(&f.chunk, TDouble(-1.0), TPositiveZero(), BinaryFoldOp::DIV), -kInf);
    ExpectFoldedDouble(FoldBinary(&f.chunk, TDouble(1.0), TNegativeZero(), BinaryFoldOp::DIV), -kInf);
    ExpectFoldedDouble(FoldBinary(&f.chunk, TDouble(-1.0), TNegativeZero(), BinaryFoldOp::DIV), kInf);
    ExpectFoldedNaN(FoldBinary(&f.chunk, TPositiveZero(), TPositiveZero(), BinaryFoldOp::DIV));
    ExpectFoldedNaN(FoldBinary(&f.chunk, TInfinity(), TInfinity(), BinaryFoldOp::DIV));
    ExpectFoldedNaN(FoldBinary(&f.chunk, TNaN(), TDouble(1.0), BinaryFoldOp::DIV));

    // Mixed int32/float64 promotes to the float64 path.
    ExpectFoldedDouble(FoldBinary(&f.chunk, TInt(1), TDouble(2.5), BinaryFoldOp::ADD), 3.5);
    ExpectFoldedDouble(FoldBinary(&f.chunk, TInt(3), TDouble(0.5), BinaryFoldOp::SUB), 2.5);
    ExpectFoldedDouble(FoldBinary(&f.chunk, TDouble(0.5), TInt(4), BinaryFoldOp::MUL), 2.0);
    ExpectFoldedDouble(FoldBinary(&f.chunk, TInt(6), TDouble(3.0), BinaryFoldOp::DIV), 2.0);
}

HWTEST_F_L0(ArkSteedConstantFoldingTest, BinaryBitwiseInt32)
{
    constexpr int32_t kMax = std::numeric_limits<int32_t>::max();
    constexpr int32_t kMin = std::numeric_limits<int32_t>::min();
    ConstantFoldingFixture f;

    // SHL applies a shift count modulo 32 (ECMA-262 13.8.1).
    ExpectFoldedInt(FoldBinary(&f.chunk, TInt(1), TInt(4), BinaryFoldOp::SHL), 16);
    ExpectFoldedInt(FoldBinary(&f.chunk, TInt(1), TInt(32), BinaryFoldOp::SHL), 1);
    ExpectFoldedInt(FoldBinary(&f.chunk, TInt(1), TInt(31), BinaryFoldOp::SHL), kMin);
    ExpectFoldedInt(FoldBinary(&f.chunk, TInt(kMin), TInt(1), BinaryFoldOp::SHL), 0);
    ExpectFoldedInt(FoldBinary(&f.chunk, TInt(kMax), TInt(1), BinaryFoldOp::SHL), -2);
    ExpectFoldedInt(FoldBinary(&f.chunk, TInt(-1), TInt(1), BinaryFoldOp::SHL), -2);

    // SHR is the unsigned (ToUint32) shift.
    ExpectFoldedInt(FoldBinary(&f.chunk, TInt(-1), TInt(1), BinaryFoldOp::SHR), kMax);
    ExpectFoldedInt(FoldBinary(&f.chunk, TInt(kMin), TInt(1), BinaryFoldOp::SHR), 1073741824);
    ExpectFoldedInt(FoldBinary(&f.chunk, TInt(16), TInt(2), BinaryFoldOp::SHR), 4);
    ExpectFoldedInt(FoldBinary(&f.chunk, TInt(1), TInt(33), BinaryFoldOp::SHR), 0);

    // ASHR is the arithmetic shift.
    ExpectFoldedInt(FoldBinary(&f.chunk, TInt(-1), TInt(1), BinaryFoldOp::ASHR), -1);
    ExpectFoldedInt(FoldBinary(&f.chunk, TInt(-8), TInt(1), BinaryFoldOp::ASHR), -4);
    ExpectFoldedInt(FoldBinary(&f.chunk, TInt(16), TInt(2), BinaryFoldOp::ASHR), 4);
    ExpectFoldedInt(FoldBinary(&f.chunk, TInt(kMin), TInt(31), BinaryFoldOp::ASHR), -1);
    ExpectFoldedInt(FoldBinary(&f.chunk, TInt(kMax), TInt(1), BinaryFoldOp::ASHR), 1073741823);

    ExpectFoldedInt(FoldBinary(&f.chunk, TInt(0xFF), TInt(0x0F), BinaryFoldOp::AND), 0x0F);
    ExpectFoldedInt(FoldBinary(&f.chunk, TInt(kMin), TInt(kMax), BinaryFoldOp::AND), 0);
    ExpectFoldedInt(FoldBinary(&f.chunk, TInt(-1), TInt(5), BinaryFoldOp::AND), 5);

    ExpectFoldedInt(FoldBinary(&f.chunk, TInt(0xF0), TInt(0x0F), BinaryFoldOp::OR), 0xFF);
    ExpectFoldedInt(FoldBinary(&f.chunk, TInt(kMin), TInt(0), BinaryFoldOp::OR), kMin);

    ExpectFoldedInt(FoldBinary(&f.chunk, TInt(0xFF), TInt(0x0F), BinaryFoldOp::XOR), 0xF0);
    ExpectFoldedInt(FoldBinary(&f.chunk, TInt(-1), TInt(5), BinaryFoldOp::XOR), -6);
    ExpectFoldedInt(FoldBinary(&f.chunk, TInt(kMin), TInt(kMin), BinaryFoldOp::XOR), 0);

    // Bitwise operations need two int32 operands at this seam.
    ExpectUnfolded(FoldBinary(&f.chunk, TDouble(1.0), TInt(1), BinaryFoldOp::SHL));
    ExpectUnfolded(FoldBinary(&f.chunk, TInt(1), TDouble(1.0), BinaryFoldOp::AND));
    ExpectUnfolded(FoldBinary(&f.chunk, JSTaggedValue::True(), TInt(1), BinaryFoldOp::OR));
    ExpectUnfolded(FoldBinary(&f.chunk, JSTaggedValue::Null(), TInt(1), BinaryFoldOp::XOR));
}

HWTEST_F_L0(ArkSteedConstantFoldingTest, BinaryCompareNumeric)
{
    constexpr int32_t kMax = std::numeric_limits<int32_t>::max();
    constexpr int32_t kMin = std::numeric_limits<int32_t>::min();
    ConstantFoldingFixture f;

    ExpectFoldedBool(FoldBinary(&f.chunk, TInt(3), TInt(5), BinaryFoldOp::LESS), true);
    ExpectFoldedBool(FoldBinary(&f.chunk, TInt(5), TInt(3), BinaryFoldOp::LESS), false);
    ExpectFoldedBool(FoldBinary(&f.chunk, TInt(kMin), TInt(kMax), BinaryFoldOp::LESS), true);
    ExpectFoldedBool(FoldBinary(&f.chunk, TInt(3), TInt(3), BinaryFoldOp::LESS_EQ), true);
    ExpectFoldedBool(FoldBinary(&f.chunk, TInt(4), TInt(3), BinaryFoldOp::LESS_EQ), false);
    ExpectFoldedBool(FoldBinary(&f.chunk, TInt(5), TInt(3), BinaryFoldOp::GREATER), true);
    ExpectFoldedBool(FoldBinary(&f.chunk, TInt(3), TInt(5), BinaryFoldOp::GREATER), false);
    ExpectFoldedBool(FoldBinary(&f.chunk, TInt(5), TInt(5), BinaryFoldOp::GREATER_EQ), true);
    ExpectFoldedBool(FoldBinary(&f.chunk, TInt(3), TInt(5), BinaryFoldOp::GREATER_EQ), false);

    // NaN compares false with everything; -0 and +0 are numerically equal.
    ExpectFoldedBool(FoldBinary(&f.chunk, TDouble(1.5), TDouble(2.5), BinaryFoldOp::LESS), true);
    ExpectFoldedBool(FoldBinary(&f.chunk, TDouble(2.5), TDouble(1.5), BinaryFoldOp::LESS), false);
    ExpectFoldedBool(FoldBinary(&f.chunk, TNaN(), TDouble(1.0), BinaryFoldOp::LESS), false);
    ExpectFoldedBool(FoldBinary(&f.chunk, TDouble(1.0), TNaN(), BinaryFoldOp::LESS), false);
    ExpectFoldedBool(FoldBinary(&f.chunk, TNaN(), TNaN(), BinaryFoldOp::LESS_EQ), false);
    ExpectFoldedBool(FoldBinary(&f.chunk, TInfinity(), TDouble(1.0), BinaryFoldOp::GREATER), true);
    ExpectFoldedBool(FoldBinary(&f.chunk, TNegInfinity(), TDouble(1.0), BinaryFoldOp::LESS), true);
    ExpectFoldedBool(FoldBinary(&f.chunk, TNegativeZero(), TPositiveZero(), BinaryFoldOp::LESS), false);
    ExpectFoldedBool(FoldBinary(&f.chunk, TNegativeZero(), TPositiveZero(), BinaryFoldOp::LESS_EQ), true);
    ExpectFoldedBool(FoldBinary(&f.chunk, TPositiveZero(), TNegativeZero(), BinaryFoldOp::GREATER), false);
    ExpectFoldedBool(FoldBinary(&f.chunk, TPositiveZero(), TNegativeZero(), BinaryFoldOp::GREATER_EQ), true);

    ExpectFoldedBool(FoldBinary(&f.chunk, TInt(1), TDouble(1.5), BinaryFoldOp::LESS), true);
    ExpectFoldedBool(FoldBinary(&f.chunk, TDouble(2.5), TInt(2), BinaryFoldOp::LESS_EQ), false);
    ExpectFoldedBool(FoldBinary(&f.chunk, TDouble(2.0), TInt(2), BinaryFoldOp::GREATER_EQ), true);

    // Relational comparison requires numeric operands; others stay unfolded.
    ExpectUnfolded(FoldBinary(&f.chunk, JSTaggedValue::True(), TInt(2), BinaryFoldOp::LESS));
    ExpectUnfolded(FoldBinary(&f.chunk, JSTaggedValue::Undefined(), TInt(1), BinaryFoldOp::GREATER));
    ExpectUnfolded(FoldBinary(&f.chunk, JSTaggedValue::Null(), TInt(1), BinaryFoldOp::LESS_EQ));
    ExpectUnfolded(FoldBinary(&f.chunk, JSTaggedValue::Hole(), TDouble(1.0), BinaryFoldOp::GREATER_EQ));
}

HWTEST_F_L0(ArkSteedConstantFoldingTest, BinaryEqualityOps)
{
    ConstantFoldingFixture f;

    // Loose equality (ECMA-262 7.2.14).
    ExpectFoldedBool(FoldBinary(&f.chunk, TInt(1), TInt(1), BinaryFoldOp::EQ), true);
    ExpectFoldedBool(FoldBinary(&f.chunk, TInt(1), TInt(2), BinaryFoldOp::EQ), false);
    ExpectFoldedBool(FoldBinary(&f.chunk, TInt(1), TInt(1), BinaryFoldOp::NOT_EQ), false);
    ExpectFoldedBool(FoldBinary(&f.chunk, TInt(1), TInt(2), BinaryFoldOp::NOT_EQ), true);
    ExpectFoldedBool(FoldBinary(&f.chunk, TDouble(1.5), TDouble(1.5), BinaryFoldOp::EQ), true);
    ExpectFoldedBool(FoldBinary(&f.chunk, TNaN(), TNaN(), BinaryFoldOp::EQ), false);
    ExpectFoldedBool(FoldBinary(&f.chunk, TNaN(), TInt(1), BinaryFoldOp::EQ), false);
    ExpectFoldedBool(FoldBinary(&f.chunk, TNaN(), TNaN(), BinaryFoldOp::NOT_EQ), true);
    ExpectFoldedBool(FoldBinary(&f.chunk, TPositiveZero(), TNegativeZero(), BinaryFoldOp::EQ), true);
    ExpectFoldedBool(FoldBinary(&f.chunk, TInt(1), TDouble(1.0), BinaryFoldOp::EQ), true);
    ExpectFoldedBool(FoldBinary(&f.chunk, TDouble(1.5), TInt(1), BinaryFoldOp::EQ), false);
    ExpectFoldedBool(FoldBinary(&f.chunk, JSTaggedValue::True(), TInt(1), BinaryFoldOp::EQ), true);
    ExpectFoldedBool(FoldBinary(&f.chunk, JSTaggedValue::False(), TInt(0), BinaryFoldOp::EQ), true);
    ExpectFoldedBool(FoldBinary(&f.chunk, JSTaggedValue::True(), TInt(2), BinaryFoldOp::EQ), false);
    ExpectFoldedBool(FoldBinary(&f.chunk, JSTaggedValue::True(), JSTaggedValue::True(), BinaryFoldOp::EQ), true);
    // null only equals undefined.
    ExpectFoldedBool(FoldBinary(&f.chunk, JSTaggedValue::Null(), JSTaggedValue::Undefined(), BinaryFoldOp::EQ), true);
    ExpectFoldedBool(FoldBinary(&f.chunk, JSTaggedValue::Null(), JSTaggedValue::Null(), BinaryFoldOp::EQ), true);
    ExpectFoldedBool(FoldBinary(&f.chunk, JSTaggedValue::Undefined(), JSTaggedValue::Undefined(), BinaryFoldOp::EQ),
                     true);
    ExpectFoldedBool(FoldBinary(&f.chunk, JSTaggedValue::Null(), TInt(0), BinaryFoldOp::EQ), false);
    ExpectFoldedBool(FoldBinary(&f.chunk, JSTaggedValue::Undefined(), TInt(0), BinaryFoldOp::EQ), false);
    ExpectFoldedBool(FoldBinary(&f.chunk, JSTaggedValue::Null(), JSTaggedValue::False(), BinaryFoldOp::EQ), false);
    ExpectFoldedBool(FoldBinary(&f.chunk, TInt(1), JSTaggedValue::Undefined(), BinaryFoldOp::EQ), false);
    // hole is not equality-foldable at this seam.
    ExpectUnfolded(FoldBinary(&f.chunk, JSTaggedValue::Hole(), JSTaggedValue::Hole(), BinaryFoldOp::EQ));
    ExpectUnfolded(FoldBinary(&f.chunk, JSTaggedValue::Hole(), JSTaggedValue::Null(), BinaryFoldOp::NOT_EQ));

    // Strict equality (ECMA-262 7.2.16).
    ExpectFoldedBool(FoldBinary(&f.chunk, TInt(1), TInt(1), BinaryFoldOp::STRICT_EQ), true);
    ExpectFoldedBool(FoldBinary(&f.chunk, TInt(1), TInt(2), BinaryFoldOp::STRICT_EQ), false);
    ExpectFoldedBool(FoldBinary(&f.chunk, TInt(1), TDouble(1.0), BinaryFoldOp::STRICT_EQ), true);
    ExpectFoldedBool(FoldBinary(&f.chunk, TInt(1), TDouble(1.5), BinaryFoldOp::STRICT_EQ), false);
    ExpectFoldedBool(FoldBinary(&f.chunk, TNaN(), TNaN(), BinaryFoldOp::STRICT_EQ), false);
    ExpectFoldedBool(FoldBinary(&f.chunk, TNaN(), TNaN(), BinaryFoldOp::STRICT_NOT_EQ), true);
    ExpectFoldedBool(FoldBinary(&f.chunk, TPositiveZero(), TNegativeZero(), BinaryFoldOp::STRICT_EQ), true);
    ExpectFoldedBool(FoldBinary(&f.chunk, JSTaggedValue::True(), JSTaggedValue::True(), BinaryFoldOp::STRICT_EQ), true);
    ExpectFoldedBool(FoldBinary(&f.chunk, JSTaggedValue::True(), JSTaggedValue::False(),
                                BinaryFoldOp::STRICT_EQ), false);
    ExpectFoldedBool(FoldBinary(&f.chunk, JSTaggedValue::Null(), JSTaggedValue::Undefined(),
                                BinaryFoldOp::STRICT_EQ), false);
    ExpectFoldedBool(FoldBinary(&f.chunk, JSTaggedValue::Undefined(), JSTaggedValue::Undefined(),
                                BinaryFoldOp::STRICT_EQ), true);
    ExpectFoldedBool(FoldBinary(&f.chunk, JSTaggedValue::Null(), JSTaggedValue::Null(), BinaryFoldOp::STRICT_EQ), true);
    ExpectFoldedBool(FoldBinary(&f.chunk, JSTaggedValue::True(), TInt(1), BinaryFoldOp::STRICT_EQ), false);
    ExpectFoldedBool(FoldBinary(&f.chunk, JSTaggedValue::False(), TInt(0), BinaryFoldOp::STRICT_NOT_EQ), true);
    ExpectFoldedBool(FoldBinary(&f.chunk, TInt(1), JSTaggedValue::Undefined(), BinaryFoldOp::STRICT_EQ), false);
}

HWTEST_F_L0(ArkSteedConstantFoldingTest, UnaryArithmetic)
{
    constexpr int32_t kMax = std::numeric_limits<int32_t>::max();
    constexpr int32_t kMin = std::numeric_limits<int32_t>::min();
    ConstantFoldingFixture f;

    // NEG
    ExpectFoldedInt(FoldUnary(&f.chunk, TInt(5), UnaryFoldOp::NEG), -5);
    ExpectFoldedInt(FoldUnary(&f.chunk, TInt(-5), UnaryFoldOp::NEG), 5);
    ExpectFoldedInt(FoldUnary(&f.chunk, TInt(kMax), UnaryFoldOp::NEG), -kMax);
    ExpectUnfolded(FoldUnary(&f.chunk, TInt(0), UnaryFoldOp::NEG));     // keeps -0
    ExpectUnfolded(FoldUnary(&f.chunk, TInt(kMin), UnaryFoldOp::NEG));  // overflow
    ExpectFoldedDoubleSign(FoldUnary(&f.chunk, TPositiveZero(), UnaryFoldOp::NEG), -0.0);
    ExpectFoldedDoubleSign(FoldUnary(&f.chunk, TNegativeZero(), UnaryFoldOp::NEG), 0.0);
    ExpectFoldedDouble(FoldUnary(&f.chunk, TDouble(1.5), UnaryFoldOp::NEG), -1.5);
    ExpectFoldedDouble(FoldUnary(&f.chunk, TInfinity(), UnaryFoldOp::NEG), -std::numeric_limits<double>::infinity());
    ExpectFoldedDouble(FoldUnary(&f.chunk, TNegInfinity(), UnaryFoldOp::NEG), std::numeric_limits<double>::infinity());
    ExpectFoldedNaN(FoldUnary(&f.chunk, TNaN(), UnaryFoldOp::NEG));
    ExpectUnfolded(FoldUnary(&f.chunk, JSTaggedValue::True(), UnaryFoldOp::NEG));
    ExpectUnfolded(FoldUnary(&f.chunk, JSTaggedValue::Null(), UnaryFoldOp::NEG));
    ExpectUnfolded(FoldUnary(&f.chunk, JSTaggedValue::Undefined(), UnaryFoldOp::NEG));
    ExpectUnfolded(FoldUnary(&f.chunk, JSTaggedValue::Hole(), UnaryFoldOp::NEG));

    // INC / DEC
    ExpectFoldedInt(FoldUnary(&f.chunk, TInt(5), UnaryFoldOp::INC), 6);
    ExpectFoldedInt(FoldUnary(&f.chunk, TInt(5), UnaryFoldOp::DEC), 4);
    ExpectFoldedInt(FoldUnary(&f.chunk, TInt(-1), UnaryFoldOp::INC), 0);
    ExpectFoldedInt(FoldUnary(&f.chunk, TInt(-1), UnaryFoldOp::DEC), -2);
    ExpectFoldedInt(FoldUnary(&f.chunk, TInt(kMin), UnaryFoldOp::INC), kMin + 1);
    ExpectFoldedInt(FoldUnary(&f.chunk, TInt(kMax), UnaryFoldOp::DEC), kMax - 1);
    ExpectUnfolded(FoldUnary(&f.chunk, TInt(kMax), UnaryFoldOp::INC));
    ExpectUnfolded(FoldUnary(&f.chunk, TInt(kMin), UnaryFoldOp::DEC));
    ExpectFoldedDouble(FoldUnary(&f.chunk, TDouble(1.5), UnaryFoldOp::INC), 2.5);
    ExpectFoldedDouble(FoldUnary(&f.chunk, TDouble(1.5), UnaryFoldOp::DEC), 0.5);
    ExpectFoldedDouble(FoldUnary(&f.chunk, TInfinity(), UnaryFoldOp::INC), std::numeric_limits<double>::infinity());
    ExpectFoldedDouble(FoldUnary(&f.chunk, TNegativeZero(), UnaryFoldOp::INC), 1.0);
    ExpectFoldedDouble(FoldUnary(&f.chunk, TNegativeZero(), UnaryFoldOp::DEC), -1.0);
    ExpectUnfolded(FoldUnary(&f.chunk, JSTaggedValue::True(), UnaryFoldOp::INC));
    ExpectUnfolded(FoldUnary(&f.chunk, JSTaggedValue::Undefined(), UnaryFoldOp::DEC));
}

HWTEST_F_L0(ArkSteedConstantFoldingTest, UnaryNotAndNumberConversions)
{
    constexpr int32_t kMax = std::numeric_limits<int32_t>::max();
    constexpr int32_t kMin = std::numeric_limits<int32_t>::min();
    ConstantFoldingFixture f;

    // NOT is ToInt32 followed by a bitwise complement (ECMA-262 13.4.2).
    ExpectFoldedInt(FoldUnary(&f.chunk, TInt(5), UnaryFoldOp::NOT), -6);
    ExpectFoldedInt(FoldUnary(&f.chunk, TInt(-1), UnaryFoldOp::NOT), 0);
    ExpectFoldedInt(FoldUnary(&f.chunk, TInt(0), UnaryFoldOp::NOT), -1);
    ExpectFoldedInt(FoldUnary(&f.chunk, TInt(kMax), UnaryFoldOp::NOT), kMin);
    ExpectFoldedInt(FoldUnary(&f.chunk, TInt(kMin), UnaryFoldOp::NOT), kMax);
    ExpectFoldedInt(FoldUnary(&f.chunk, TDouble(1.5), UnaryFoldOp::NOT), -2);
    ExpectFoldedInt(FoldUnary(&f.chunk, TDouble(3.0), UnaryFoldOp::NOT), -4);
    ExpectFoldedInt(FoldUnary(&f.chunk, TNegativeZero(), UnaryFoldOp::NOT), -1);
    ExpectFoldedInt(FoldUnary(&f.chunk, TNaN(), UnaryFoldOp::NOT), -1);
    ExpectFoldedInt(FoldUnary(&f.chunk, TInfinity(), UnaryFoldOp::NOT), -1);
    ExpectFoldedInt(FoldUnary(&f.chunk, TDouble(2147483648.0), UnaryFoldOp::NOT), kMax);
    ExpectFoldedInt(FoldUnary(&f.chunk, JSTaggedValue::True(), UnaryFoldOp::NOT), -2);
    ExpectFoldedInt(FoldUnary(&f.chunk, JSTaggedValue::False(), UnaryFoldOp::NOT), -1);
    ExpectFoldedInt(FoldUnary(&f.chunk, JSTaggedValue::Null(), UnaryFoldOp::NOT), -1);
    ExpectFoldedInt(FoldUnary(&f.chunk, JSTaggedValue::Undefined(), UnaryFoldOp::NOT), -1);
    ExpectUnfolded(FoldUnary(&f.chunk, JSTaggedValue::Hole(), UnaryFoldOp::NOT));

    // TO_NUMBER / TO_NUMERIC (ECMA-262 7.1.4).
    ExpectFoldedInt(FoldUnary(&f.chunk, TInt(5), UnaryFoldOp::TO_NUMBER), 5);
    ExpectFoldedInt(FoldUnary(&f.chunk, TInt(5), UnaryFoldOp::TO_NUMERIC), 5);
    ExpectFoldedDouble(FoldUnary(&f.chunk, TDouble(1.5), UnaryFoldOp::TO_NUMBER), 1.5);
    ExpectFoldedDoubleSign(FoldUnary(&f.chunk, TNegativeZero(), UnaryFoldOp::TO_NUMBER), -0.0);
    ExpectFoldedNaN(FoldUnary(&f.chunk, TNaN(), UnaryFoldOp::TO_NUMBER));
    ExpectFoldedInt(FoldUnary(&f.chunk, JSTaggedValue::True(), UnaryFoldOp::TO_NUMBER), 1);
    ExpectFoldedInt(FoldUnary(&f.chunk, JSTaggedValue::False(), UnaryFoldOp::TO_NUMERIC), 0);
    ExpectFoldedInt(FoldUnary(&f.chunk, JSTaggedValue::Null(), UnaryFoldOp::TO_NUMBER), 0);
    ExpectFoldedNaN(FoldUnary(&f.chunk, JSTaggedValue::Undefined(), UnaryFoldOp::TO_NUMERIC));
    ExpectUnfolded(FoldUnary(&f.chunk, JSTaggedValue::Hole(), UnaryFoldOp::TO_NUMBER));
    ExpectUnfolded(FoldUnary(&f.chunk, JSTaggedValue::Hole(), UnaryFoldOp::TO_NUMERIC));
}

HWTEST_F_L0(ArkSteedConstantFoldingTest, BooleanConversion)
{
    constexpr int32_t kMin = std::numeric_limits<int32_t>::min();
    ConstantFoldingFixture f;

    struct Case {
        JSTaggedValue input;
        bool expected;
    };
    const Case cases[] = {
        {TInt(0), false},
        {TInt(1), true},
        {TInt(-5), true},
        {TInt(kMin), true},
        {TPositiveZero(), false},
        {TNegativeZero(), false},
        {TNaN(), false},
        {TDouble(1.5), true},
        {TInfinity(), true},
        {TNegInfinity(), true},
        {JSTaggedValue::True(), true},
        {JSTaggedValue::False(), false},
        {JSTaggedValue::Undefined(), false},
        {JSTaggedValue::Null(), false},
        {JSTaggedValue::Hole(), false},
    };
    for (const Case &testCase : cases) {
        bool result = !testCase.expected;
        ASSERT_TRUE(TryFoldToBooleanConstant(MakeTagged(&f.chunk, testCase.input), &result));
        EXPECT_EQ(result, testCase.expected);
    }
}

HWTEST_F_L0(ArkSteedConstantFoldingTest, RejectNonFoldableInputsAndPreserveState)
{
    constexpr int32_t kMax = std::numeric_limits<int32_t>::max();
    ConstantFoldingFixture f;

    // HeapConstantVertex (e.g. a string literal) is rejected at this seam (D3).
    auto *heap = MakeHeap(&f.chunk);
    JSTaggedValue sentinel(TInt(12345));
    uint64_t sentinelRaw = sentinel.GetRawData();
    JSTaggedValue result(sentinel);
    EXPECT_FALSE(TryFoldBinaryConstant(heap, MakeTagged(&f.chunk, TInt(1)), BinaryFoldOp::ADD, &result));
    EXPECT_EQ(result.GetRawData(), sentinelRaw) << "rejected fold modified the output";
    EXPECT_FALSE(TryFoldBinaryConstant(MakeTagged(&f.chunk, TInt(1)), heap, BinaryFoldOp::STRICT_EQ, &result));
    EXPECT_EQ(result.GetRawData(), sentinelRaw) << "rejected fold modified the output";
    EXPECT_FALSE(TryFoldUnaryConstant(heap, UnaryFoldOp::TO_NUMBER, &result));
    EXPECT_EQ(result.GetRawData(), sentinelRaw) << "rejected fold modified the output";
    bool boolResult = true;
    EXPECT_FALSE(TryFoldToBooleanConstant(heap, &boolResult));
    EXPECT_TRUE(boolResult) << "rejected boolean fold modified the output";

    // Internal typed constants are not accepted either: only tagged constants fold.
    EXPECT_FALSE(TryFoldBinaryConstant(MakeInt32(&f.chunk, 3), MakeTagged(&f.chunk, TInt(4)),
                                       BinaryFoldOp::ADD, &result));
    EXPECT_EQ(result.GetRawData(), sentinelRaw);
    EXPECT_FALSE(TryFoldBinaryConstant(MakeTagged(&f.chunk, TInt(3)), MakeFloat64(&f.chunk, 4.0),
                                       BinaryFoldOp::ADD, &result));
    EXPECT_EQ(result.GetRawData(), sentinelRaw);
    EXPECT_FALSE(TryFoldUnaryConstant(MakeInt32(&f.chunk, 5), UnaryFoldOp::NEG, &result));
    EXPECT_EQ(result.GetRawData(), sentinelRaw);
    EXPECT_FALSE(TryFoldUnaryConstant(MakeFloat64(&f.chunk, 1.5), UnaryFoldOp::NEG, &result));
    EXPECT_EQ(result.GetRawData(), sentinelRaw);
    EXPECT_FALSE(TryFoldToBooleanConstant(MakeInt32(&f.chunk, 1), &boolResult));
    EXPECT_FALSE(TryFoldToBooleanConstant(MakeFloat64(&f.chunk, 1.0), &boolResult));

    // The same value folds when it is a tagged constant, so the rejection above
    // is about the vertex class, not about the numeric value.
    ExpectFoldedInt(FoldBinary(&f.chunk, TInt(3), TInt(4), BinaryFoldOp::ADD), 7);
    ExpectFoldedDouble(FoldBinary(&f.chunk, TDouble(3.0), TDouble(4.0), BinaryFoldOp::ADD), 7.0);

    // A non-constant vertex is never folded.
    auto *taggedMax = MakeTagged(&f.chunk, TInt(kMax));
    auto *taggedOne = MakeTagged(&f.chunk, TInt(1));
    auto *add = Vertex::New<I32AddVertex>(&f.chunk, static_cast<size_t>(2));
    add->SetInput(0, taggedMax);
    add->SetInput(1, taggedOne);
    EXPECT_FALSE(TryFoldBinaryConstant(add, MakeTagged(&f.chunk, TInt(1)), BinaryFoldOp::ADD, &result));
    EXPECT_EQ(result.GetRawData(), sentinelRaw);
    EXPECT_FALSE(TryFoldUnaryConstant(add, UnaryFoldOp::INC, &result));
    EXPECT_EQ(result.GetRawData(), sentinelRaw);
    EXPECT_FALSE(TryFoldToBooleanConstant(add, &boolResult));

    // Folding does not mutate its operands (same value and same inputs).
    auto *operand = Vertex::New<TaggedConstantVertex>(&f.chunk, static_cast<size_t>(0),
                                                      static_cast<uint64_t>(TInt(kMax).GetRawData()));
    uint64_t valueBefore = static_cast<uint64_t>(operand->GetValue());
    uint32_t inputCountBefore = add->GetInputCount();
    ValueVertex *input0Before = add->GetInput(0);
    ValueVertex *input1Before = add->GetInput(1);
    result = JSTaggedValue(0);
    EXPECT_FALSE(TryFoldBinaryConstant(operand, taggedOne, BinaryFoldOp::ADD, &result));
    EXPECT_EQ(static_cast<uint64_t>(operand->GetValue()), valueBefore);
    EXPECT_EQ(add->GetInputCount(), inputCountBefore);
    EXPECT_EQ(add->GetInput(0), input0Before);
    EXPECT_EQ(add->GetInput(1), input1Before);
    EXPECT_TRUE(add->TryCast<I32AddVertex>() != nullptr);
}

}  // namespace panda::ecmascript::arksteed
