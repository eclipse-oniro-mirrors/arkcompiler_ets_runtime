/*
 * Copyright (c) 2024 Huawei Device Co., Ltd.
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
#include "ecmascript/compiler/typed_native_inline_lowering.h"
#include "ecmascript/compiler/circuit_builder_helper.h"
#include "ecmascript/compiler/circuit_builder-inl.h"

namespace panda::ecmascript::kungfu {
GateRef TypedNativeInlineLowering::VisitGate(GateRef gate)
{
    auto op = acc_.GetOpCode(gate);
    switch (op) {
        case OpCode::MATH_COS:
            LowerGeneralUnaryMath(gate, RTSTUB_ID(FloatCos));
            break;
        case OpCode::MATH_COSH:
            LowerGeneralUnaryMath(gate, RTSTUB_ID(FloatCosh));
            break;
        case OpCode::MATH_SIN:
            LowerGeneralUnaryMath(gate, RTSTUB_ID(FloatSin));
            break;
        case OpCode::MATH_LOG:
            LowerGeneralUnaryMath(gate, RTSTUB_ID(FloatLog));
            break;
        case OpCode::MATH_LOG2:
            LowerGeneralUnaryMath(gate, RTSTUB_ID(FloatLog2));
            break;
        case OpCode::MATH_LOG10:
            LowerGeneralUnaryMath(gate, RTSTUB_ID(FloatLog10));
            break;
        case OpCode::MATH_LOG1P:
            LowerGeneralUnaryMath(gate, RTSTUB_ID(FloatLog1p));
            break;
        case OpCode::MATH_EXP:
            LowerMathExp(gate);
            break;
        case OpCode::MATH_EXPM1:
            LowerGeneralUnaryMath(gate, RTSTUB_ID(FloatExpm1));
            break;
        case OpCode::MATH_SINH:
            LowerGeneralUnaryMath(gate, RTSTUB_ID(FloatSinh));
            break;
        case OpCode::MATH_ASINH:
            LowerGeneralUnaryMath(gate, RTSTUB_ID(FloatAsinh));
            break;
        case OpCode::MATH_TAN:
            LowerGeneralUnaryMath(gate, RTSTUB_ID(FloatTan));
            break;
        case OpCode::MATH_ATAN:
            LowerGeneralUnaryMath(gate, RTSTUB_ID(FloatAtan));
            break;
        case OpCode::MATH_TANH:
            LowerGeneralUnaryMath(gate, RTSTUB_ID(FloatTanh));
            break;
        case OpCode::MATH_ACOS:
            LowerGeneralUnaryMath(gate, RTSTUB_ID(FloatAcos));
            break;
        case OpCode::MATH_ASIN:
            LowerGeneralUnaryMath(gate, RTSTUB_ID(FloatAsin));
            break;
        case OpCode::MATH_ATANH:
            LowerGeneralUnaryMath(gate, RTSTUB_ID(FloatAtanh));
            break;
        case OpCode::MATH_ACOSH:
            LowerGeneralUnaryMath(gate, RTSTUB_ID(FloatAcosh));
            break;
        case OpCode::MATH_ATAN2:
            LowerMathAtan2(gate);
            break;
        case OpCode::MATH_ABS:
            LowerAbs(gate);
            break;
        case OpCode::MATH_ABS_INT32:
            LowerIntAbs(gate);
            break;
        case OpCode::MATH_ABS_DOUBLE:
            LowerDoubleAbs(gate);
            break;
        case OpCode::MATH_TRUNC:
            LowerTrunc(gate);
            break;
        case OpCode::MATH_POW:
            LowerMathPow(gate);
            break;
        case OpCode::MATH_CBRT:
            LowerGeneralUnaryMath(gate, RTSTUB_ID(FloatCbrt));
            break;
        case OpCode::MATH_SIGN:
            LowerMathSignInt(gate);
            break;
        case OpCode::MATH_SIGN_TAGGED:
            LowerMathSignTagged(gate);
            break;
        case OpCode::MATH_MIN:
            LowerMinMax<false>(gate);
            break;
        case OpCode::MATH_MAX:
            LowerMinMax<true>(gate);
            break;
        case OpCode::MATH_MIN_INT32:
            LowerIntMinMax<false>(gate);
            break;
        case OpCode::MATH_MAX_INT32:
            LowerIntMinMax<true>(gate);
            break;
        case OpCode::MATH_MIN_DOUBLE:
            LowerDoubleMinMax<false>(gate);
            break;
        case OpCode::MATH_MAX_DOUBLE:
            LowerDoubleMinMax<true>(gate);
            break;
        case OpCode::MATH_CLZ32_DOUBLE:
            LowerClz32Float64(gate);
            break;
        case OpCode::MATH_CLZ32_INT32:
            LowerClz32Int32(gate);
            break;
        case OpCode::MATH_SQRT:
            LowerMathSqrt(gate);
            break;
        case OpCode::MATH_ROUND:
            LowerTaggedRounding(gate);
            break;
        case OpCode::MATH_ROUND_DOUBLE:
        case OpCode::MATH_FROUND:
            LowerDoubleRounding(gate);
            break;
        case OpCode::MATH_CEIL:
            LowerMathCeilFloor<true>(gate);
            break;
        case OpCode::MATH_FLOOR:
            LowerMathCeilFloor<false>(gate);
            break;
        case OpCode::MATH_IMUL:
            LowerMathImul(gate);
            break;
        case OpCode::GLOBAL_IS_FINITE:
            LowerGlobalIsFinite(gate);
            break;
        case OpCode::GLOBAL_IS_NAN:
            LowerGlobalIsNan(gate);
            break;
        default:
            break;
    }
    return Circuit::NullGate();
}

template <bool IS_CEIL>
void TypedNativeInlineLowering::LowerMathCeilFloor(GateRef gate)
{
    if (builder_.GetCompilationConfig()->IsAArch64()) {
        LowerMathCeilFloorWithIntrinsic<IS_CEIL>(gate);
    } else {
        LowerMathCeilFloorWithRuntimeCall<IS_CEIL>(gate);
    }
}

template <bool IS_CEIL>
void TypedNativeInlineLowering::LowerMathCeilFloorWithIntrinsic(GateRef gate)
{
    Environment env(gate, circuit_, &builder_);
    DEFVALUE(result, (&builder_), VariableType::FLOAT64(), builder_.NanValue());
    GateRef arg = acc_.GetValueIn(gate, 0);

    if constexpr (IS_CEIL) {
        result = builder_.DoubleCeil(arg);
    } else {
        result = builder_.DoubleFloor(arg);
    }
    acc_.ReplaceGate(gate, builder_.GetState(), builder_.GetDepend(), *result);
}

template <bool IS_CEIL>
void TypedNativeInlineLowering::LowerMathCeilFloorWithRuntimeCall(GateRef gate)
{
    Environment env(gate, circuit_, &builder_);
    DEFVALUE(result, (&builder_), VariableType::FLOAT64(), builder_.NanValue());
    GateRef arg = acc_.GetValueIn(gate, 0);
    GateRef glue = acc_.GetGlueFromArgList();

    if constexpr (IS_CEIL) {
        result = builder_.CallNGCRuntime(glue, RTSTUB_ID(FloatCeil), Gate::InvalidGateRef, {arg}, gate);
    } else {
        result = builder_.CallNGCRuntime(glue, RTSTUB_ID(FloatFloor), Gate::InvalidGateRef, {arg}, gate);
    }
    acc_.ReplaceGate(gate, builder_.GetState(), builder_.GetDepend(), *result);
}
GateRef TypedNativeInlineLowering::LowerGlobalDoubleIsFinite(GateRef value)
{
    // set the sign bit to 0 by shift left then right.
    auto temp = builder_.Int64LSL(builder_.CastDoubleToInt64(value), builder_.Int64(1));
    auto res = builder_.Int64LSR(temp, builder_.Int64(1));
    auto abs = builder_.CastInt64ToFloat64(res);
    auto result = builder_.DoubleLessThan(abs, builder_.Double(base::POSITIVE_INFINITY));
    return result;
}

GateRef TypedNativeInlineLowering::LowerGlobalTNumberIsFinite(GateRef value)
{
    ASSERT(!acc_.GetGateType(value).IsNJSValueType());
    DEFVALUE(result, (&builder_), VariableType::BOOL(), builder_.Boolean(true));

    Label isInt(&builder_);
    Label notInt(&builder_);
    Label exit(&builder_);
    builder_.Branch(builder_.TaggedIsInt(value), &isInt, &notInt);
    builder_.Bind(&isInt);
    {
        result = builder_.Boolean(true);
        builder_.Jump(&exit);
    }
    builder_.Bind(&notInt);
    {
        result = LowerGlobalDoubleIsFinite(value);
        builder_.Jump(&exit);
    }
    builder_.Bind(&exit);
    return *result;
}

void TypedNativeInlineLowering::LowerGlobalIsFinite(GateRef gate)
{
    GateRef value = acc_.GetValueIn(gate, 0);
    Environment env(gate, circuit_, &builder_);
    GateRef result = LowerGlobalDoubleIsFinite(value);

    acc_.ReplaceGate(gate, builder_.GetStateDepend(), result);
}

GateRef TypedNativeInlineLowering::LowerGlobalTNumberIsNan(GateRef value)
{
    ASSERT(!acc_.GetGateType(value).IsNJSValueType());
    DEFVALUE(result, (&builder_), VariableType::BOOL(), builder_.Boolean(false));

    Label isInt(&builder_);
    Label notInt(&builder_);
    Label exit(&builder_);
    builder_.Branch(builder_.TaggedIsInt(value), &isInt, &notInt);
    builder_.Bind(&isInt);
    {
        result = builder_.Boolean(false);
        builder_.Jump(&exit);
    }
    builder_.Bind(&notInt);
    {
        result = builder_.DoubleIsNAN(value);
        builder_.Jump(&exit);
    }
    builder_.Bind(&exit);
    return *result;
}

void TypedNativeInlineLowering::LowerGlobalIsNan(GateRef gate)
{
    GateRef value = acc_.GetValueIn(gate, 0);
    Environment env(gate, circuit_, &builder_);
    GateRef result = builder_.DoubleIsNAN(value);

    acc_.ReplaceGate(gate, builder_.GetStateDepend(), result);
}

void TypedNativeInlineLowering::LowerMathPow(GateRef gate)
{
    Environment env(gate, circuit_, &builder_);
    GateRef base = acc_.GetValueIn(gate, 0);
    GateRef exp = acc_.GetValueIn(gate, 1);
    Label exit(&builder_);
    Label notNan(&builder_);

    auto nanValue = builder_.NanValue();
    DEFVALUE(result, (&builder_), VariableType::FLOAT64(), nanValue);

    const double doubleOne = 1.0;
    GateRef baseIsOne = builder_.DoubleEqual(base, builder_.Double(doubleOne));
    // Base is 1.0, exponent is inf => NaN
    // Exponent is not finit, if is NaN or is Inf
    GateRef tempIsNan = builder_.BoolAnd(baseIsOne, builder_.DoubleIsINF(exp));
    GateRef resultIsNan = builder_.BoolOr(builder_.DoubleIsNAN(exp), tempIsNan);

    BRANCH_CIR(resultIsNan, &exit, &notNan);
    builder_.Bind(&notNan);
    {
        GateRef glue = acc_.GetGlueFromArgList();
        result = builder_.CallNGCRuntime(glue, RTSTUB_ID(FloatPow), Gate::InvalidGateRef, {base, exp}, gate);
        builder_.Jump(&exit);
    }

    builder_.Bind(&exit);
    acc_.ReplaceGate(gate, builder_.GetState(), builder_.GetDepend(), *result);
}

void TypedNativeInlineLowering::LowerMathExp(GateRef gate)
{
#ifdef SUPPORT_LLVM_INTRINSICS_WITH_CALLS
    Environment env(gate, circuit_, &builder_);
    constexpr double one = 1.0;
    GateRef base = builder_.Double(std::exp(one));
    GateRef power = acc_.GetValueIn(gate, 0U);

    GateRef exp = builder_.DoubleExp(base, power);
    acc_.ReplaceGate(gate, builder_.GetState(), builder_.GetDepend(), exp);
#else
    LowerGeneralUnaryMath(gate, RTSTUB_ID(FloatExp));
#endif
}

void TypedNativeInlineLowering::LowerMathImul(GateRef gate)
{
    Environment env(gate, circuit_, &builder_);
    GateRef val1 = acc_.GetValueIn(gate, 0);
    GateRef val2 = acc_.GetValueIn(gate, 1);
    ASSERT(acc_.GetGateType(val1).IsNJSValueType() && acc_.GetGateType(val2).IsNJSValueType());

    GateRef result = builder_.Int32Mul(val1, val2);
    acc_.ReplaceGate(gate, builder_.GetState(), builder_.GetDepend(), result);
}

void TypedNativeInlineLowering::LowerGeneralUnaryMath(GateRef gate, RuntimeStubCSigns::ID stubId)
{
    Environment env(gate, circuit_, &builder_);
    GateRef value = acc_.GetValueIn(gate, 0);
    GateRef glue = acc_.GetGlueFromArgList();
    GateRef result = builder_.CallNGCRuntime(glue, stubId, Gate::InvalidGateRef, {value}, gate);
    acc_.ReplaceGate(gate, builder_.GetState(), builder_.GetDepend(), result);
}

void TypedNativeInlineLowering::LowerMathAtan2(GateRef gate)
{
    Environment env(gate, circuit_, &builder_);
    GateRef y = acc_.GetValueIn(gate, 0);
    GateRef x = acc_.GetValueIn(gate, 1);
    GateRef glue = acc_.GetGlueFromArgList();
    GateRef result = builder_.CallNGCRuntime(glue, RTSTUB_ID(FloatAtan2), Gate::InvalidGateRef, {y, x}, gate);
    acc_.ReplaceGate(gate, builder_.GetState(), builder_.GetDepend(), result);
}

//  Int abs : The internal representation of an integer is inverse code,
//  The absolute value of a negative number can be found by inverting it by adding one.
GateRef TypedNativeInlineLowering::BuildIntAbs(GateRef value)
{
    ASSERT(acc_.GetMachineType(value) == MachineType::I32);
    if (isLiteCG_) {
        auto temp = builder_.Int32ASR(value, builder_.Int32(JSTaggedValue::INT_SIGN_BIT_OFFSET));
        auto res = builder_.Int32Xor(value, temp);
        return builder_.Int32Sub(res, temp);
    }
    return builder_.Abs(value);
}

//  Float abs : A floating-point number is composed of mantissa and exponent.
//  The length of mantissa will affect the precision of the number, and its sign will determine the sign of the number.
//  The absolute value of a floating-point number can be found by setting mantissa sign bit to 0.
GateRef TypedNativeInlineLowering::BuildDoubleAbs(GateRef value)
{
    ASSERT(acc_.GetMachineType(value) == MachineType::F64);
    if (isLiteCG_) {
        // set the sign bit to 0 by shift left then right.
        auto temp = builder_.Int64LSL(builder_.CastDoubleToInt64(value), builder_.Int64(1));
        auto res = builder_.Int64LSR(temp, builder_.Int64(1));
        return builder_.CastInt64ToFloat64(res);
    }
    return builder_.FAbs(value);
}

GateRef TypedNativeInlineLowering::BuildTNumberAbs(GateRef param)
{
    ASSERT(!acc_.GetGateType(param).IsNJSValueType());
    DEFVALUE(result, (&builder_), VariableType::JS_ANY(), builder_.HoleConstant());

    Label exit(&builder_);
    Label isInt(&builder_);
    Label notInt(&builder_);
    Label isIntMin(&builder_);
    Label isResultInt(&builder_);
    Label intExit(&builder_);
    BRANCH_CIR(builder_.TaggedIsInt(param), &isInt, &notInt);
    builder_.Bind(&isInt);
    {
        auto value = builder_.GetInt32OfTInt(param);
        BRANCH_CIR(builder_.Equal(value, builder_.Int32(INT32_MIN)), &isIntMin, &isResultInt);
        builder_.Bind(&isResultInt);
        {
            result = builder_.Int32ToTaggedPtr(BuildIntAbs(value));
            builder_.Jump(&intExit);
        }
        builder_.Bind(&isIntMin);
        {
            result = builder_.DoubleToTaggedDoublePtr(builder_.Double(-static_cast<double>(INT_MIN)));
            builder_.Jump(&intExit);
        }
        // Aot compiler fails without jump to intermediate label
        builder_.Bind(&intExit);
        builder_.Jump(&exit);
    }
    builder_.Bind(&notInt);
    {
        auto value = builder_.GetDoubleOfTDouble(param);
        result = builder_.DoubleToTaggedDoublePtr(BuildDoubleAbs(value));
        builder_.Jump(&exit);
    }
    builder_.Bind(&exit);
    return *result;
}

void TypedNativeInlineLowering::LowerAbs(GateRef gate)
{
    GateRef value = acc_.GetValueIn(gate, 0);
    Environment env(gate, circuit_, &builder_);
    GateRef res = BuildTNumberAbs(value);
    acc_.ReplaceGate(gate, builder_.GetStateDepend(), res);
}

GateRef TypedNativeInlineLowering::BuildRounding(GateRef gate, GateRef value, OpCode op)
{
    if (op == OpCode::MATH_ROUND || op == OpCode::MATH_ROUND_DOUBLE) {
        Label entry(&builder_);
        builder_.SubCfgEntry(&entry);
        const double diff = 0.5;
        GateRef diffValue = builder_.Double(diff);
        const double zero = 0.0;
        Label subOne(&builder_);
        Label exit(&builder_);
        Label retCeil(&builder_);
        Label nonZero(&builder_);
        DEFVALUE(result, (&builder_), VariableType::FLOAT64(), builder_.Double(zero));
        // 0 <= x < 0.5, return 0
        GateRef returnZero = builder_.BoolAnd(builder_.DoubleLessThan(value, diffValue),
            builder_.DoubleGreaterThan(value, builder_.Double(zero)));
        BRANCH_CIR(returnZero, &exit, &nonZero);
        builder_.Bind(&nonZero);
        {
            GateRef rounded;
            if (builder_.GetCompilationConfig()->IsAArch64() && !isLiteCG_) {
                rounded = builder_.DoubleCeil(value);
            } else {
                GateRef glue = acc_.GetGlueFromArgList();
                rounded = builder_.CallNGCRuntime(glue, RTSTUB_ID(FloatCeil), Gate::InvalidGateRef, {value}, gate);
            }
            // if ceil(x) - x > 0.5, return ceil(x) - 1
            // else return ceil(x)
            BRANCH_CIR(builder_.DoubleGreaterThan(builder_.DoubleSub(rounded, value), diffValue),
                &subOne, &retCeil);
            builder_.Bind(&subOne);
            {
                result = builder_.DoubleSub(rounded, builder_.Double(1U));
                builder_.Jump(&exit);
            }
            builder_.Bind(&retCeil);
            result = rounded;
            builder_.Jump(&exit);
        }
        builder_.Bind(&exit);
        GateRef res = *result;
        builder_.SubCfgExit();
        return res;
    } else if (op == OpCode::MATH_FROUND) {
        return builder_.ExtFloat32ToDouble(builder_.TruncDoubleToFloat32(value));
    } else {
        UNREACHABLE();
    }
}

void TypedNativeInlineLowering::LowerTaggedRounding(GateRef gate)
{
    Environment env(gate, circuit_, &builder_);

    DEFVALUE(result, (&builder_), VariableType::FLOAT64(), builder_.NanValue());

    GateRef in = acc_.GetValueIn(gate, 0);
    Label isInt(&builder_);
    Label isDouble(&builder_);
    Label exit(&builder_);

    builder_.Branch(builder_.TaggedIsInt(in), &isInt, &isDouble);
    builder_.Bind(&isInt);
    {
        result = builder_.GetDoubleOfTInt(in);
        builder_.Jump(&exit);
    }
    builder_.Bind(&isDouble);
    {
        GateRef value = builder_.GetDoubleOfTDouble(in);
        result = BuildRounding(gate, value, acc_.GetOpCode(gate));
        builder_.Jump(&exit);
    }

    builder_.Bind(&exit);
    acc_.ReplaceGate(gate, builder_.GetState(), builder_.GetDepend(), *result);
}

void TypedNativeInlineLowering::LowerDoubleRounding(GateRef gate)
{
    Environment env(gate, circuit_, &builder_);
    GateRef value = acc_.GetValueIn(gate, 0);
    GateRef res = BuildRounding(gate, value, acc_.GetOpCode(gate));
    acc_.ReplaceGate(gate, builder_.GetStateDepend(), res);
}

void TypedNativeInlineLowering::LowerIntAbs(GateRef gate)
{
    Environment env(gate, circuit_, &builder_);
    GateRef value = acc_.GetValueIn(gate, 0);
    auto frameState = FindFrameState(gate);
    builder_.DeoptCheck(builder_.NotEqual(value, builder_.Int32(INT32_MIN)), frameState, DeoptType::NOTINT3);
    GateRef res = BuildIntAbs(value);
    acc_.ReplaceGate(gate, builder_.GetStateDepend(), res);
}

void TypedNativeInlineLowering::LowerDoubleAbs(GateRef gate)
{
    GateRef value = acc_.GetValueIn(gate, 0);
    Environment env(gate, circuit_, &builder_);
    GateRef res = BuildDoubleAbs(value);
    acc_.ReplaceGate(gate, builder_.GetStateDepend(), res);
}

// for min select in1 if int1 < int2, in2 otherwise
template<bool IS_MAX>
GateRef TypedNativeInlineLowering::BuildIntMinMax(GateRef int1, GateRef int2, GateRef in1, GateRef in2)
{
    Label entry(&builder_);
    builder_.SubCfgEntry(&entry);
    // int or tagged
    VariableType type {acc_.GetMachineType(in1), acc_.GetGateType(in1)};
    DEFVALUE(result, (&builder_), type, (IS_MAX ? in1 : in2));
    Label left(&builder_);
    Label exit(&builder_);
    builder_.Branch(builder_.Int32LessThan(int1, int2), &left, &exit);
    builder_.Bind(&left);
    {
        result = IS_MAX ? in2 : in1;
        builder_.Jump(&exit);
    }
    builder_.Bind(&exit);
    GateRef res = *result;
    builder_.SubCfgExit();
    return res;
}

template<bool IS_MAX>
GateRef TypedNativeInlineLowering::BuildIntMinMax(GateRef in1, GateRef in2)
{
    ASSERT(acc_.GetMachineType(in1) == MachineType::I32);
    ASSERT(acc_.GetMachineType(in2) == MachineType::I32);
    if (isLiteCG_) {
        return BuildIntMinMax<IS_MAX>(in1, in2, in1, in2);
    }
    return IS_MAX ? builder_.Int32Max(in1, in2) : builder_.Int32Min(in1, in2);
}

/* for min select:
 * NaN if double1 or double2 is NaN
 * in1 if double1 and double2 are equal and in1 is negative zero
 * in1 if double1 < double2, in2 otherwise */
template<bool IS_MAX>
GateRef TypedNativeInlineLowering::BuildDoubleMinMax(GateRef double1, GateRef double2, GateRef in1, GateRef in2)
{
    Label entry(&builder_);
    builder_.SubCfgEntry(&entry);
    GateRef nanValue = builder_.NanValue();
    if (in1 != double1) { // case when in1 and in2 are tagged
        nanValue = builder_.DoubleToTaggedDoublePtr(nanValue);
    }
    // double or tagged
    VariableType type {acc_.GetMachineType(in1), acc_.GetGateType(in1)};
    DEFVALUE(result, (&builder_), type, nanValue);
    Label left(&builder_);
    Label rightOrZeroOrNan(&builder_);
    Label right(&builder_);
    Label exit(&builder_);
    Label equal(&builder_);
    Label equalOrNan(&builder_);
    builder_.Branch(builder_.DoubleLessThan(double1, double2), &left, &rightOrZeroOrNan);
    builder_.Bind(&rightOrZeroOrNan);
    {
        builder_.Branch(builder_.DoubleGreaterThan(double1, double2), &right, &equalOrNan);
        builder_.Bind(&equalOrNan);
        {
            builder_.Branch(builder_.DoubleEqual(double1, double2), &equal, &exit);
            builder_.Bind(&equal);
            {
                // Whether to return in1 or in2 matters only in case of 0, -0
                const double negZero = -0.0;
                GateRef negZeroValue = builder_.CastDoubleToInt64(builder_.Double(negZero));
                builder_.Branch(builder_.Equal(builder_.CastDoubleToInt64(double1), negZeroValue), &left, &right);
            }
        }
        builder_.Bind(&right);
        {
            result = IS_MAX ? in1 : in2;
            builder_.Jump(&exit);
        }
    }
    builder_.Bind(&left);
    {
        result = IS_MAX ? in2 : in1;
        builder_.Jump(&exit);
    }
    builder_.Bind(&exit);
    GateRef res = *result;
    builder_.SubCfgExit();
    return res;
}

template<bool IS_MAX>
GateRef TypedNativeInlineLowering::BuildDoubleMinMax(GateRef in1, GateRef in2)
{
    ASSERT(acc_.GetMachineType(in1) == MachineType::F64);
    ASSERT(acc_.GetMachineType(in2) == MachineType::F64);
    if (!isLiteCG_ && builder_.GetCompilationConfig()->IsAArch64()) {
        return IS_MAX ? builder_.DoubleMax(in1, in2) : builder_.DoubleMin(in1, in2);
    }
    return BuildDoubleMinMax<IS_MAX>(in1, in2, in1, in2);
}

template<bool IS_MAX>
void TypedNativeInlineLowering::LowerTNumberMinMax(GateRef gate)
{
    Environment env(gate, circuit_, &builder_);
    GateRef in1 = acc_.GetValueIn(gate, 0);
    GateRef in2 = acc_.GetValueIn(gate, 1);
    GateRef nanValue = builder_.DoubleToTaggedDoublePtr(builder_.NanValue());
    DEFVALUE(result, (&builder_), VariableType::JS_ANY(), nanValue);
    DEFVALUE(double1, (&builder_), VariableType::FLOAT64(), builder_.Double(0));
    DEFVALUE(double2, (&builder_), VariableType::FLOAT64(), builder_.Double(0));

    Label isInt1(&builder_);
    Label isInt2(&builder_);
    Label isDouble1(&builder_);
    Label isDouble2(&builder_);
    Label doubleExit(&builder_);
    Label exit(&builder_);
    builder_.Branch(builder_.TaggedIsInt(in1), &isInt1, &isDouble1);
    {
        builder_.Bind(&isInt1);
        GateRef int1 = builder_.GetInt32OfTInt(in1);
        builder_.Branch(builder_.TaggedIsInt(in2), &isInt2, &isDouble2);
        {
            builder_.Bind(&isInt2);
            GateRef int2 = builder_.GetInt32OfTInt(in2);
            result = BuildIntMinMax<IS_MAX>(int1, int2, in1, in2);
            builder_.Jump(&exit);
        }
        builder_.Bind(&isDouble2);
        double1 = builder_.ChangeInt32ToFloat64(int1);
        double2 = builder_.GetDoubleOfTDouble(in2);
        builder_.Jump(&doubleExit);
    }
    {
        builder_.Bind(&isDouble1);
        double1 = builder_.GetDoubleOfTDouble(in1);
        double2 = builder_.GetDoubleOfTNumber(in2);
        builder_.Jump(&doubleExit);
    }
    builder_.Bind(&doubleExit);
    result = BuildDoubleMinMax<IS_MAX>(*double1, *double2, in1, in2);
    builder_.Jump(&exit);

    builder_.Bind(&exit);
    acc_.ReplaceGate(gate, builder_.GetStateDepend(), *result);
}

template<bool IS_MAX>
void TypedNativeInlineLowering::LowerMathMinMaxWithIntrinsic(GateRef gate)
{
    Environment env(gate, circuit_, &builder_);
    GateRef in1 = acc_.GetValueIn(gate, 0);
    GateRef in2 = acc_.GetValueIn(gate, 1);
    GateRef nanValue = builder_.DoubleToTaggedDoublePtr(builder_.NanValue());
    DEFVALUE(result, (&builder_), VariableType::JS_ANY(), nanValue);

    Label intRes(&builder_);
    Label doubleRes(&builder_);
    Label exit(&builder_);

    builder_.Branch(builder_.BoolAnd(builder_.TaggedIsInt(in1), builder_.TaggedIsInt(in2)), &intRes, &doubleRes);
    builder_.Bind(&intRes);
    {
        GateRef int1 = builder_.GetInt32OfTInt(in1);
        GateRef int2 = builder_.GetInt32OfTInt(in2);
        GateRef intRet = BuildIntMinMax<IS_MAX>(int1, int2);
        result = builder_.Int32ToTaggedPtr(intRet);
        builder_.Jump(&exit);
    }
    builder_.Bind(&doubleRes);
    {
        GateRef double1 = builder_.GetDoubleOfTNumber(in1);
        GateRef double2 = builder_.GetDoubleOfTNumber(in2);
        // LLVM supports lowering of `minimum/maximum` intrinsics on X86 only since version 17
        // see https://github.com/llvm/llvm-project/commit/a82d27a9a6853c96f857ba0f514a78cd03bc5c35
        if (builder_.GetCompilationConfig()->IsAArch64()) {
            GateRef doubleRet = IS_MAX ? builder_.DoubleMax(double1, double2) : builder_.DoubleMin(double1, double2);
            result = builder_.DoubleToTaggedDoublePtr(doubleRet);
        } else {
            result = BuildDoubleMinMax<IS_MAX>(double1, double2, in1, in2);
        }
        builder_.Jump(&exit);
    }
    builder_.Bind(&exit);
    acc_.ReplaceGate(gate, builder_.GetStateDepend(), *result);
}

template<bool IS_MAX>
void TypedNativeInlineLowering::LowerMinMax(GateRef gate)
{
    if (isLiteCG_) {
        LowerTNumberMinMax<IS_MAX>(gate);
    } else {
        LowerMathMinMaxWithIntrinsic<IS_MAX>(gate);
    }
}

template<bool IS_MAX>
void TypedNativeInlineLowering::LowerIntMinMax(GateRef gate)
{
    GateRef in1 = acc_.GetValueIn(gate, 0);
    GateRef in2 = acc_.GetValueIn(gate, 1);
    Environment env(gate, circuit_, &builder_);
    GateRef res = BuildIntMinMax<IS_MAX>(in1, in2);
    acc_.ReplaceGate(gate, builder_.GetStateDepend(), res);
}

template<bool IS_MAX>
void TypedNativeInlineLowering::LowerDoubleMinMax(GateRef gate)
{
    GateRef in1 = acc_.GetValueIn(gate, 0);
    GateRef in2 = acc_.GetValueIn(gate, 1);
    Environment env(gate, circuit_, &builder_);
    GateRef res = BuildDoubleMinMax<IS_MAX>(in1, in2);
    acc_.ReplaceGate(gate, builder_.GetStateDepend(), res);
}

GateRef TypedNativeInlineLowering::FindFrameState(GateRef gate)
{
    while (!acc_.HasFrameState(gate)) {
        ASSERT(acc_.GetDependCount(gate) > 0);
        gate = acc_.GetDep(gate);
    }
    return acc_.GetFrameState(gate);
}

void TypedNativeInlineLowering::LowerClz32Float64(GateRef gate)
{
    Environment env(gate, circuit_, &builder_);
    Label exit(&builder_);
    Label isFinit(&builder_);

    GateRef param = acc_.GetValueIn(gate, 0);
    const int32_t defaultReturnValue = 32;
    DEFVALUE(result, (&builder_), VariableType::INT32(), builder_.Int32(defaultReturnValue));

    // NaN, Inf, -Inf after ToUint32 equal 0, so we in advance know result: Clz32(0) = 32
    auto paramCheck = builder_.BoolOr(builder_.DoubleIsNAN(param), builder_.DoubleIsINF(param));
    builder_.Branch(paramCheck, &exit, &isFinit);
    builder_.Bind(&isFinit);
    {
        auto truncedValue = builder_.TruncInt64ToInt32(builder_.TruncFloatToInt64(param));
        result = builder_.CountLeadingZeroes32(truncedValue);
        builder_.Jump(&exit);
    }

    builder_.Bind(&exit);
    acc_.ReplaceGate(gate, builder_.GetState(), builder_.GetDepend(), *result);
}

void TypedNativeInlineLowering::LowerClz32Int32(GateRef gate)
{
    Environment env(gate, circuit_, &builder_);
    GateRef param = acc_.GetValueIn(gate, 0);
    GateRef result = builder_.CountLeadingZeroes32(param);
    acc_.ReplaceGate(gate, builder_.GetState(), builder_.GetDepend(), result);
}

//  Int trunc(x) : return x
//  Float trunc(x) : return the integer part removing all fractional digits
void TypedNativeInlineLowering::LowerTrunc(GateRef gate)
{
    Environment env(gate, circuit_, &builder_);
    GateRef param = acc_.GetValueIn(gate, 0);
    DEFVALUE(result, (&builder_), VariableType::FLOAT64(), builder_.NanValue());

    Label isInt(&builder_);
    Label notInt(&builder_);
    Label isDouble(&builder_);
    Label exit(&builder_);

    BRANCH_CIR(builder_.TaggedIsInt(param), &isInt, &notInt);
    builder_.Bind(&isInt);
    {
        result = builder_.ChangeInt32ToFloat64(builder_.GetInt32OfTInt(param));
        builder_.Jump(&exit);
    }
    builder_.Bind(&notInt);
    {
        BRANCH_CIR(builder_.TaggedIsDouble(param), &isDouble, &exit);
        builder_.Bind(&isDouble);
        {
            GateRef input = builder_.GetDoubleOfTDouble(param);
            if (builder_.GetCompilationConfig()->IsAArch64()) {
                result = builder_.DoubleTrunc(input);
            } else {
                GateRef glue = acc_.GetGlueFromArgList();
                result = builder_.CallNGCRuntime(glue, RTSTUB_ID(FloatTrunc), Gate::InvalidGateRef, {input}, gate);
            }
            builder_.Jump(&exit);
        }
    }
    builder_.Bind(&exit);
    acc_.ReplaceGate(gate, builder_.GetState(), builder_.GetDepend(), *result);
}

void TypedNativeInlineLowering::LowerMathSqrt(GateRef gate)
{
    Environment env(gate, circuit_, &builder_);
    builder_.SetEnvironment(&env);
    GateRef param = acc_.GetValueIn(gate, 0);
    // 20.2.2.32
    // If value is NAN or negative, include -NaN and -Infinity but not -0.0, the result is NaN
    // Assembly instruction support NAN and negative
    auto ret = builder_.Sqrt(param);
    acc_.SetMachineType(ret, MachineType::F64);
    acc_.SetGateType(ret, GateType::NJSValue());
    acc_.ReplaceGate(gate, builder_.GetState(), builder_.GetDepend(), ret);
}

static void BuildMathSignDouble(Variable *resVarPtr, CircuitBuilder *builder, GateRef param,
                                std::vector<Label> *labelsForFloatCase)
{
    auto &taggedRes = *resVarPtr;
    auto &labelsForFloatCaseRef = *labelsForFloatCase;
    auto labelsIdx = 0;
    Label *isNan = &labelsForFloatCaseRef.at(labelsIdx++);
    Label *notNan = &labelsForFloatCaseRef.at(labelsIdx++);
    Label *exitNan = &labelsForFloatCaseRef.at(labelsIdx++);

    auto value = builder->GetDoubleOfTDouble(param);
    builder->Branch(builder->DoubleIsNAN(value), isNan, notNan);
    builder->Bind(isNan);
    {
        taggedRes = builder->DoubleToTaggedDoublePtr(builder->NanValue());
        builder->Jump(exitNan);
    }
    builder->Bind(notNan);
    {
        Label *isZero = &labelsForFloatCaseRef.at(labelsIdx++);
        Label *notZero = &labelsForFloatCaseRef.at(labelsIdx++);
        Label *exitZero = &labelsForFloatCaseRef.at(labelsIdx++);

        builder->Branch(builder->DoubleEqual(value, builder->Double(0)), isZero, notZero);
        builder->Bind(isZero);
        {
            taggedRes = param;
            builder->Jump(exitZero);
        }
        builder->Bind(notZero);
        {
            Label *isNegative = &labelsForFloatCaseRef.at(labelsIdx++);
            Label *notNegative = &labelsForFloatCaseRef.at(labelsIdx++);
            Label *exitNegative = &labelsForFloatCaseRef.at(labelsIdx);

            builder->Branch(builder->DoubleLessThan(value, builder->Double(0)), isNegative, notNegative);
            builder->Bind(isNegative);
            {
                taggedRes = builder->Int32ToTaggedPtr(builder->Int32(-1));
                builder->Jump(exitNegative);
            }
            builder->Bind(notNegative);
            {
                taggedRes = builder->Int32ToTaggedPtr(builder->Int32(1));
                builder->Jump(exitNegative);
            }
            builder->Bind(exitNegative);
            builder->Jump(exitZero);
        }
        builder->Bind(exitZero);
        builder->Jump(exitNan);
    }
    builder->Bind(exitNan);
}

static GateRef BuildMathSignInt(CircuitBuilder *builder, GateRef input)
{
    auto nz = builder->BooleanToInt32(builder->Int32NotEqual(input, builder->Int32(0)));
    auto valueShifted = builder->Int32ASR(input, builder->Int32(JSTaggedValue::INT_SIGN_BIT_OFFSET));
    return builder->Int32Or(valueShifted, nz);
}

void TypedNativeInlineLowering::LowerMathSignInt(GateRef gate)
{
    Environment env(gate, circuit_, &builder_);
    GateRef param = acc_.GetValueIn(gate, 0);

    auto res = BuildMathSignInt(&builder_, param);

    acc_.ReplaceGate(gate, builder_.GetState(), builder_.GetDepend(), res);
}

void TypedNativeInlineLowering::LowerMathSignTagged(GateRef gate)
{
    Environment env(gate, circuit_, &builder_);
    Label exit(&builder_);
    GateRef param = acc_.GetValueIn(gate, 0);
    DEFVALUE(taggedRes, (&builder_), VariableType::JS_ANY(), builder_.HoleConstant());

    std::vector<Label> labelsForFloatCase;
    constexpr auto FLOAT_CASE_LABELS_COUNT = 9;
    labelsForFloatCase.reserve(FLOAT_CASE_LABELS_COUNT);
    for (auto i = 0; i < FLOAT_CASE_LABELS_COUNT; i++) {
        labelsForFloatCase.emplace_back(&builder_);
    }

    Label isInt(&builder_);
    Label notInt(&builder_);
    builder_.Branch(builder_.TaggedIsInt(param), &isInt, &notInt);
    builder_.Bind(&isInt);
    {
        auto value = builder_.GetInt32OfTInt(param);

        auto res = BuildMathSignInt(&builder_, value);

        taggedRes = builder_.Int32ToTaggedPtr(res);
        builder_.Jump(&exit);
    }
    builder_.Bind(&notInt);
    {
        BuildMathSignDouble(&taggedRes, &builder_, param, &labelsForFloatCase);
        builder_.Jump(&exit);
    }
    builder_.Bind(&exit);
    acc_.ReplaceGate(gate, builder_.GetState(), builder_.GetDepend(), *taggedRes);
}
}
