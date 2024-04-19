/*
 * Copyright (c) 2022 Huawei Device Co., Ltd.
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

#include "ecmascript/compiler/builtins_lowering.h"
#include "ecmascript/global_env.h"

namespace panda::ecmascript::kungfu {
void BuiltinLowering::LowerTypedCallBuitin(GateRef gate)
{
    Environment env(gate, circuit_, &builder_);
    auto valuesIn = acc_.GetNumValueIn(gate);
    auto idGate = acc_.GetValueIn(gate, valuesIn - 1);
    auto id = static_cast<BuiltinsStubCSigns::ID>(acc_.GetConstantValue(idGate));
    switch (id) {
        case BUILTINS_STUB_ID(StringLocaleCompare):
            LowerTypedLocaleCompare(gate);
            break;
        case BUILTINS_STUB_ID(ArraySort):
            LowerTypedArraySort(gate);
            break;
        case BUILTINS_STUB_ID(JsonStringify):
            LowerTypedStringify(gate);
            break;
        case BUILTINS_STUB_ID(MapProtoIterator):
        case BUILTINS_STUB_ID(SetProtoIterator):
        case BUILTINS_STUB_ID(StringProtoIterator):
        case BUILTINS_STUB_ID(ArrayProtoIterator):
        case BUILTINS_STUB_ID(TypeArrayProtoIterator):
            LowerBuiltinIterator(gate, id);
            break;
        case BUILTINS_STUB_ID(MapIteratorProtoNext):
        case BUILTINS_STUB_ID(SetIteratorProtoNext):
        case BUILTINS_STUB_ID(StringIteratorProtoNext):
        case BUILTINS_STUB_ID(ArrayIteratorProtoNext):
            LowerIteratorNext(gate, id);
            break;
        case BUILTINS_STUB_ID(IteratorProtoReturn):
            LowerIteratorReturn(gate, id);
            break;
        case BUILTINS_STUB_ID(NumberConstructor):
            LowerNumberConstructor(gate);
            break;
        default:
            break;
    }
}

void BuiltinLowering::LowerTypedFloor(GateRef gate)
{
    auto ret = TypedFloor(gate);
    acc_.ReplaceGate(gate, builder_.GetState(), builder_.GetDepend(), ret);
}

GateRef BuiltinLowering::TypedFloor(GateRef gate)
{
    auto env = builder_.GetCurrentEnvironment();
    Label entry(&builder_);
    env->SubCfgEntry(&entry);

    Label numberBranch(&builder_);
    Label notNumberBranch(&builder_);
    Label exit(&builder_);

    GateRef para1 = acc_.GetValueIn(gate, 0);
    DEFVALUE(result, (&builder_), VariableType::JS_ANY(), builder_.HoleConstant());

    BRANCH_CIR(builder_.TaggedIsNumber(para1), &numberBranch, &notNumberBranch);
    builder_.Bind(&numberBranch);
    {
        GateRef value = builder_.GetDoubleOfTNumber(para1);
        Label IsNan(&builder_);
        Label NotNan(&builder_);
        GateRef condition = builder_.DoubleIsNAN(value);
        BRANCH_CIR(condition, &IsNan, &NotNan);
        builder_.Bind(&NotNan);
        {
            GateRef glue = acc_.GetGlueFromArgList();
            int index = RTSTUB_ID(FloatFloor);
            GateRef floor = builder_.CallNGCRuntime(glue, index, Gate::InvalidGateRef, {value}, gate);
            result = builder_.DoubleToTaggedDoublePtr(floor);
            builder_.Jump(&exit);
        }
        builder_.Bind(&IsNan);
        {
            result = builder_.DoubleToTaggedDoublePtr(builder_.Double(base::NAN_VALUE));
            builder_.Jump(&exit);
        }
    }
    builder_.Bind(&notNumberBranch);
    {
        builder_.Jump(&exit);
    }

    builder_.Bind(&exit);
    auto ret = *result;
    env->SubCfgExit();
    return ret;
}

GateRef BuiltinLowering::IntToTaggedIntPtr(GateRef x)
{
    GateRef val = builder_.SExtInt32ToInt64(x);
    return builder_.ToTaggedIntPtr(val);
}

GateRef BuiltinLowering::LowerCallRuntime(GateRef glue, GateRef gate, int index, const std::vector<GateRef> &args,
                                          bool useLabel)
{
    const std::string name = RuntimeStubCSigns::GetRTName(index);
    if (useLabel) {
        GateRef result = builder_.CallRuntime(glue, index, Gate::InvalidGateRef, args, gate, name.c_str());
        return result;
    } else {
        const CallSignature *cs = RuntimeStubCSigns::Get(RTSTUB_ID(CallRuntime));
        GateRef target = builder_.IntPtr(index);
        GateRef result = builder_.Call(cs, glue, target, builder_.GetDepend(), args, gate, name.c_str());
        return result;
    }
}

void BuiltinLowering::ReplaceHirWithValue(GateRef hirGate, GateRef value, bool noThrow)
{
    if (!noThrow) {
        GateRef state = builder_.GetState();
        // copy depend-wire of hirGate to value
        GateRef depend = builder_.GetDepend();
        // exception value
        GateRef exceptionVal = builder_.ExceptionConstant();
        // compare with trampolines result
        GateRef equal = builder_.Equal(value, exceptionVal);
        auto ifBranch = builder_.Branch(state, equal, 1, BranchWeight::DEOPT_WEIGHT, "checkException");

        GateRef ifTrue = builder_.IfTrue(ifBranch);
        GateRef ifFalse = builder_.IfFalse(ifBranch);
        GateRef eDepend = builder_.DependRelay(ifTrue, depend);
        GateRef sDepend = builder_.DependRelay(ifFalse, depend);
        StateDepend success(ifFalse, sDepend);
        StateDepend exception(ifTrue, eDepend);
        acc_.ReplaceHirWithIfBranch(hirGate, success, exception, value);
    } else {
        acc_.ReplaceHirDirectly(hirGate, builder_.GetStateDepend(), value);
    }
}

void BuiltinLowering::LowerTypedLocaleCompare(GateRef gate)
{
    GateRef glue = acc_.GetGlueFromArgList();
    uint32_t index = 0;
    GateRef thisObj = acc_.GetValueIn(gate, index++);
    GateRef a0 = acc_.GetValueIn(gate, index++);
    GateRef a1 = acc_.GetValueIn(gate, index++);
    GateRef a2 = acc_.GetValueIn(gate, index++);

    std::vector<GateRef> args;
    args.reserve(index);
    args.emplace_back(thisObj);
    args.emplace_back(a0);
    args.emplace_back(a1);
    args.emplace_back(a2);
    GateRef result = LowerCallRuntime(glue, gate, RTSTUB_ID(LocaleCompare), args);
    ReplaceHirWithValue(gate, result);
}

void BuiltinLowering::LowerTypedArraySort(GateRef gate)
{
    GateRef glue = acc_.GetGlueFromArgList();
    GateRef thisObj = acc_.GetValueIn(gate, 0);
    GateRef result = LowerCallRuntime(glue, gate, RTSTUB_ID(ArraySort), { thisObj });
    ReplaceHirWithValue(gate, result);
}

GateRef BuiltinLowering::LowerCallTargetCheck(Environment *env, GateRef gate)
{
    builder_.SetEnvironment(env);
    GateRef idGate = acc_.GetValueIn(gate, 1);
    BuiltinsStubCSigns::ID id = static_cast<BuiltinsStubCSigns::ID>(acc_.GetConstantValue(idGate));
    switch (id) {
        case BuiltinsStubCSigns::ID::MapProtoIterator:
        case BuiltinsStubCSigns::ID::SetProtoIterator:
        case BuiltinsStubCSigns::ID::StringProtoIterator:
        case BuiltinsStubCSigns::ID::ArrayProtoIterator:
        case BuiltinsStubCSigns::ID::TypeArrayProtoIterator: {
            return LowerCallTargetCheckWithDetector(gate, id);
        }
        case BuiltinsStubCSigns::ID::NumberConstructor: {
            return LowerCallTargetCheckWithGlobalEnv(gate, id);
        }
        default: {
            return LowerCallTargetCheckDefault(gate, id);
        }
    }
}

GateRef BuiltinLowering::LowerCallTargetCheckDefault(GateRef gate, BuiltinsStubCSigns::ID id)
{
    GateRef constantFunction = builder_.GetGlobalConstantValue(GET_TYPED_CONSTANT_INDEX(id));
    GateRef function = acc_.GetValueIn(gate, 0); // 0: function
    return builder_.Equal(function, constantFunction);
}

GateRef BuiltinLowering::LowerCallTargetCheckWithGlobalEnv(GateRef gate, BuiltinsStubCSigns::ID id)
{
    GateRef glueGlobalEnv = builder_.GetGlobalEnv();
    GateRef globalFunction =
        builder_.GetGlobalEnvObj(glueGlobalEnv, GET_TYPED_GLOBAL_ENV_INDEX(id));
    GateRef target = acc_.GetValueIn(gate, 0); // 0:target
    return builder_.Equal(target, globalFunction);
}

GateRef BuiltinLowering::LowerCallTargetCheckWithDetector(GateRef gate, BuiltinsStubCSigns::ID id)
{
    JSType expectType = JSType::INVALID;
    uint16_t detectorIndex = 0;
    switch (id) {
        case BuiltinsStubCSigns::ID::MapProtoIterator: {
            expectType = JSType::JS_MAP;
            detectorIndex = GlobalEnv::MAP_ITERATOR_DETECTOR_INDEX;
            break;
        }
        case BuiltinsStubCSigns::ID::SetProtoIterator: {
            expectType = JSType::JS_SET;
            detectorIndex = GlobalEnv::SET_ITERATOR_DETECTOR_INDEX;
            break;
        }
        case BuiltinsStubCSigns::ID::StringProtoIterator: {
            expectType = JSType::STRING_FIRST;
            detectorIndex = GlobalEnv::STRING_ITERATOR_DETECTOR_INDEX;
            break;
        }
        case BuiltinsStubCSigns::ID::ArrayProtoIterator: {
            expectType = JSType::JS_ARRAY;
            detectorIndex = GlobalEnv::ARRAY_ITERATOR_DETECTOR_INDEX;
            break;
        }
        case BuiltinsStubCSigns::ID::TypeArrayProtoIterator: {
            expectType = JSType::JS_TYPED_ARRAY_FIRST;
            detectorIndex = GlobalEnv::TYPED_ARRAY_ITERATOR_DETECTOR_INDEX;
            break;
        }
        default: {
            LOG_COMPILER(FATAL) << "this branch is unreachable";
            UNREACHABLE();
        }
    }
    GateRef obj = acc_.GetValueIn(gate, 2);  // 2: iterator obj
    GateRef check1 =  builder_.BoolAnd(
        builder_.TaggedIsHeapObjectOp(obj), builder_.IsSpecificObjectType(obj, expectType));
    GateRef glueGlobalEnv = builder_.GetGlobalEnv();
    GateRef markerCell = builder_.GetGlobalEnvObj(glueGlobalEnv, detectorIndex);
    GateRef check2 = builder_.BoolAnd(check1, builder_.IsMarkerCellValid(markerCell));
    return check2;
}

GateRef BuiltinLowering::CheckPara(GateRef gate, GateRef funcCheck)
{
    GateRef idGate = acc_.GetValueIn(gate, 1);
    BuiltinsStubCSigns::ID id = static_cast<BuiltinsStubCSigns::ID>(acc_.GetConstantValue(idGate));
    if (IS_TYPED_INLINE_BUILTINS_ID(id)) {
        // Don't need check param. Param was checked before
        return funcCheck;
    }
    switch (id) {
        case BuiltinsStubCSigns::ID::StringLocaleCompare:
        case BuiltinsStubCSigns::ID::ArraySort:
        case BuiltinsStubCSigns::ID::JsonStringify:
        case BuiltinsStubCSigns::ID::MapProtoIterator:
        case BuiltinsStubCSigns::ID::SetProtoIterator:
        case BuiltinsStubCSigns::ID::StringProtoIterator:
        case BuiltinsStubCSigns::ID::ArrayProtoIterator:
        case BuiltinsStubCSigns::ID::TypeArrayProtoIterator:
        case BuiltinsStubCSigns::ID::MapIteratorProtoNext:
        case BuiltinsStubCSigns::ID::SetIteratorProtoNext:
        case BuiltinsStubCSigns::ID::StringIteratorProtoNext:
        case BuiltinsStubCSigns::ID::ArrayIteratorProtoNext:
        case BuiltinsStubCSigns::ID::IteratorProtoReturn:
        case BuiltinsStubCSigns::ID::NumberConstructor:
            // Don't need check para
            return funcCheck;
        default: {
            LOG_COMPILER(FATAL) << "this branch is unreachable";
            UNREACHABLE();
        }
    }
}

void BuiltinLowering::LowerTypedStringify(GateRef gate)
{
    GateRef glue = acc_.GetGlueFromArgList();
    GateRef value = acc_.GetValueIn(gate, 0);
    std::vector<GateRef> args;
    args.emplace_back(value);
    GateRef result = LowerCallRuntime(glue, gate, RTSTUB_ID(FastStringify), args);
    ReplaceHirWithValue(gate, result);
}

void BuiltinLowering::LowerBuiltinIterator(GateRef gate, BuiltinsStubCSigns::ID id)
{
    GateRef glue = acc_.GetGlueFromArgList();
    GateRef obj = acc_.GetValueIn(gate, 0);
    GateRef result = Circuit::NullGate();
    switch (id) {
        case BUILTINS_STUB_ID(MapProtoIterator): {
            result = builder_.CallStub(glue, gate, CommonStubCSigns::CreateJSMapIterator, { glue, obj });
            break;
        }
        case BUILTINS_STUB_ID(SetProtoIterator): {
            result = builder_.CallStub(glue, gate, CommonStubCSigns::CreateJSSetIterator, { glue, obj });
            break;
        }
        case BUILTINS_STUB_ID(StringProtoIterator): {
            result = LowerCallRuntime(glue, gate, RTSTUB_ID(CreateStringIterator), { obj }, true);
            break;
        }
        case BUILTINS_STUB_ID(ArrayProtoIterator): {
            result = LowerCallRuntime(glue, gate, RTSTUB_ID(NewJSArrayIterator), { obj }, true);
            break;
        }
        case BUILTINS_STUB_ID(TypeArrayProtoIterator): {
            result = LowerCallRuntime(glue, gate, RTSTUB_ID(NewJSTypedArrayIterator), { obj }, true);
            break;
        }
        default:
            UNREACHABLE();
    }
    ReplaceHirWithValue(gate, result);
}

void BuiltinLowering::LowerIteratorNext(GateRef gate, BuiltinsStubCSigns::ID id)
{
    GateRef glue = acc_.GetGlueFromArgList();
    GateRef thisObj = acc_.GetValueIn(gate, 0);
    GateRef result = Circuit::NullGate();
    switch (id) {
        case BUILTINS_STUB_ID(MapIteratorProtoNext): {
            result = LowerCallRuntime(glue, gate, RTSTUB_ID(MapIteratorNext), { thisObj }, true);
            break;
        }
        case BUILTINS_STUB_ID(SetIteratorProtoNext): {
            result = LowerCallRuntime(glue, gate, RTSTUB_ID(SetIteratorNext), { thisObj }, true);
            break;
        }
        case BUILTINS_STUB_ID(StringIteratorProtoNext): {
            result = LowerCallRuntime(glue, gate, RTSTUB_ID(StringIteratorNext), { thisObj }, true);
            break;
        }
        case BUILTINS_STUB_ID(ArrayIteratorProtoNext): {
            result = LowerCallRuntime(glue, gate, RTSTUB_ID(ArrayIteratorNext), { thisObj }, true);
            break;
        }
        default:
            UNREACHABLE();
    }
    ReplaceHirWithValue(gate, result);
}

void BuiltinLowering::LowerIteratorReturn(GateRef gate, BuiltinsStubCSigns::ID id)
{
    GateRef glue = acc_.GetGlueFromArgList();
    GateRef thisObj = acc_.GetValueIn(gate, 0);
    GateRef result = Circuit::NullGate();
    switch (id) {
        case BUILTINS_STUB_ID(IteratorProtoReturn): {
            result = LowerCallRuntime(glue, gate, RTSTUB_ID(IteratorReturn), { thisObj }, true);
            break;
        }
        default:
            UNREACHABLE();
    }
    ReplaceHirWithValue(gate, result);
}

void BuiltinLowering::LowerNumberConstructor(GateRef gate)
{
    auto env = builder_.GetCurrentEnvironment();

    DEFVALUE(result, (&builder_), VariableType::JS_ANY(), IntToTaggedIntPtr(builder_.Int32(0)));
    GateRef param = acc_.GetValueIn(gate, 0);
    Label exit(env);
    Label isNumber(env);
    Label notNumber(env);
    BRANCH_CIR(builder_.TaggedIsNumber(param), &isNumber, &notNumber);
    builder_.Bind(&isNumber);
    {
        result = param;
        builder_.Jump(&exit);
    }
    builder_.Bind(&notNumber);
    {
        Label isString(env);
        Label notString(env);
        BRANCH_CIR(builder_.TaggedIsString(param), &isString, &notString);
        builder_.Bind(&isString);
        {
            Label nonZeroLength(env);
            auto length = builder_.GetLengthFromString(param);
            BRANCH_CIR(builder_.Equal(length, builder_.Int32(0)), &exit, &nonZeroLength);
            builder_.Bind(&nonZeroLength);
            Label isInteger(env);
            BRANCH_CIR(builder_.IsIntegerString(param), &isInteger, &notString);
            builder_.Bind(&isInteger);
            {
                result = IntToTaggedIntPtr(builder_.GetRawHashFromString(param));
                builder_.Jump(&exit);
            }
        }
        builder_.Bind(&notString);
        {
            GateRef glue = acc_.GetGlueFromArgList();
            result = LowerCallRuntime(glue, gate, RTSTUB_ID(ToNumericConvertBigInt), { param }, true);
            builder_.Jump(&exit);
        }
    }
    builder_.Bind(&exit);
    ReplaceHirWithValue(gate, *result);
}
}  // namespace panda::ecmascript::kungfu
