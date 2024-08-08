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

#include "ecmascript/compiler/typed_hcr_lowering.h"

#include "ecmascript/compiler/builtins_lowering.h"
#include "ecmascript/compiler/builtins/builtins_string_stub_builder.h"
#include "ecmascript/compiler/mcr_gate_meta_data.h"
#include "ecmascript/compiler/new_object_stub_builder.h"
#include "ecmascript/compiler/pgo_type/pgo_type_manager.h"
#include "ecmascript/compiler/rt_call_signature.h"
#include "ecmascript/compiler/share_gate_meta_data.h"
#include "ecmascript/compiler/variable_type.h"
#include "ecmascript/deoptimizer/deoptimizer.h"
#include "ecmascript/elements.h"
#include "ecmascript/enum_conversion.h"
#include "ecmascript/js_arraybuffer.h"
#include "ecmascript/js_map.h"
#include "ecmascript/js_native_pointer.h"
#include "ecmascript/js_object.h"
#include "ecmascript/js_primitive_ref.h"
#include "ecmascript/linked_hash_table.h"
#include "ecmascript/message_string.h"
#include "ecmascript/vtable.h"

namespace panda::ecmascript::kungfu {
GateRef TypedHCRLowering::VisitGate(GateRef gate)
{
    GateRef glue = acc_.GetGlueFromArgList();
    auto op = acc_.GetOpCode(gate);
    [[maybe_unused]] auto scopedGate = circuit_->VisitGateBegin(gate);
    switch (op) {
        case OpCode::PRIMITIVE_TYPE_CHECK:
            LowerPrimitiveTypeCheck(gate);
            break;
        case OpCode::BUILTIN_PROTOTYPE_HCLASS_CHECK:
            LowerBuiltinPrototypeHClassCheck(gate);
            break;
        case OpCode::STABLE_ARRAY_CHECK:
            LowerStableArrayCheck(gate);
            break;
        case OpCode::TYPED_ARRAY_CHECK:
            LowerTypedArrayCheck(gate);
            break;
        case OpCode::ECMA_STRING_CHECK:
            LowerEcmaStringCheck(gate);
            break;
        case OpCode::ECMA_MAP_CHECK:
            LowerEcmaMapCheck(gate);
            break;
        case OpCode::FLATTEN_TREE_STRING_CHECK:
            LowerFlattenTreeStringCheck(gate, glue);
            break;
        case OpCode::LOAD_STRING_LENGTH:
            LowerStringLength(gate);
            break;
        case OpCode::LOAD_MAP_SIZE:
            LowerMapSize(gate);
            break;
        case OpCode::LOAD_TYPED_ARRAY_LENGTH:
            LowerLoadTypedArrayLength(gate);
            break;
        case OpCode::OBJECT_TYPE_CHECK:
            LowerObjectTypeCheck(gate);
            break;
        case OpCode::RANGE_CHECK_PREDICATE:
            LowerRangeCheckPredicate(gate);
            break;
        case OpCode::INDEX_CHECK:
            LowerIndexCheck(gate);
            break;
        case OpCode::TYPED_CALLTARGETCHECK_OP:
            LowerJSCallTargetCheck(gate);
            break;
        case OpCode::TYPED_CALL_CHECK:
            LowerCallTargetCheck(gate);
            break;
        case OpCode::JSINLINETARGET_TYPE_CHECK:
            LowerJSInlineTargetTypeCheck(gate);
            break;
        case OpCode::TYPE_CONVERT:
            LowerTypeConvert(gate);
            break;
        case OpCode::LOAD_PROPERTY:
            LowerLoadProperty(gate);
            break;
        case OpCode::CALL_PRIVATE_GETTER:
            LowerCallPrivateGetter(gate, glue);
            break;
        case OpCode::CALL_PRIVATE_SETTER:
            LowerCallPrivateSetter(gate, glue);
            break;
        case OpCode::CALL_GETTER:
            LowerCallGetter(gate, glue);
            break;
        case OpCode::STORE_PROPERTY:
        case OpCode::STORE_PROPERTY_NO_BARRIER:
            LowerStoreProperty(gate);
            break;
        case OpCode::CALL_SETTER:
            LowerCallSetter(gate, glue);
            break;
        case OpCode::LOAD_ARRAY_LENGTH:
            LowerLoadArrayLength(gate);
            break;
        case OpCode::LOAD_ELEMENT:
            LowerLoadElement(gate);
            break;
        case OpCode::STORE_ELEMENT:
            LowerStoreElement(gate, glue);
            break;
        case OpCode::TYPED_CALL_BUILTIN:
        case OpCode::TYPED_CALL_BUILTIN_SIDE_EFFECT:
            LowerTypedCallBuitin(gate);
            break;
        case OpCode::TYPED_NEW_ALLOCATE_THIS:
            LowerTypedNewAllocateThis(gate, glue);
            break;
        case OpCode::TYPED_SUPER_ALLOCATE_THIS:
            LowerTypedSuperAllocateThis(gate, glue);
            break;
        case OpCode::GET_SUPER_CONSTRUCTOR:
            LowerGetSuperConstructor(gate);
            break;
        case OpCode::COW_ARRAY_CHECK:
            LowerCowArrayCheck(gate, glue);
            break;
        case OpCode::LOOK_UP_HOLDER:
            LowerLookupHolder(gate);
            break;
        case OpCode::LOAD_GETTER:
            LowerLoadGetter(gate);
            break;
        case OpCode::LOAD_SETTER:
            LowerLoadSetter(gate);
            break;
        case OpCode::PROTOTYPE_CHECK:
            LowerPrototypeCheck(gate);
            break;
        case OpCode::STRING_EQUAL:
            LowerStringEqual(gate, glue);
            break;
        case OpCode::STRING_ADD:
            LowerStringAdd(gate, glue);
            break;
        case OpCode::TYPE_OF_CHECK:
            LowerTypeOfCheck(gate);
            break;
        case OpCode::TYPE_OF:
            LowerTypeOf(gate, glue);
            break;
        case OpCode::ARRAY_CONSTRUCTOR_CHECK:
            LowerArrayConstructorCheck(gate, glue);
            break;
        case OpCode::ARRAY_CONSTRUCTOR:
            LowerArrayConstructor(gate, glue);
            break;
        case OpCode::FLOAT32_ARRAY_CONSTRUCTOR_CHECK:
            LowerFloat32ArrayConstructorCheck(gate, glue);
            break;
        case OpCode::FLOAT32_ARRAY_CONSTRUCTOR:
            LowerFloat32ArrayConstructor(gate, glue);
            break;
        case OpCode::LOAD_BUILTIN_OBJECT:
            LowerLoadBuiltinObject(gate);
            break;
        case OpCode::OBJECT_CONSTRUCTOR_CHECK:
            LowerObjectConstructorCheck(gate, glue);
            break;
        case OpCode::OBJECT_CONSTRUCTOR:
            LowerObjectConstructor(gate, glue);
            break;
        case OpCode::BOOLEAN_CONSTRUCTOR_CHECK:
            LowerBooleanConstructorCheck(gate, glue);
            break;
        case OpCode::BOOLEAN_CONSTRUCTOR:
            LowerBooleanConstructor(gate, glue);
            break;
        case OpCode::ORDINARY_HAS_INSTANCE:
            LowerOrdinaryHasInstance(gate, glue);
            break;
        case OpCode::PROTO_CHANGE_MARKER_CHECK:
            LowerProtoChangeMarkerCheck(gate);
            break;
        case OpCode::MONO_CALL_GETTER_ON_PROTO:
            LowerMonoCallGetterOnProto(gate, glue);
            break;
        case OpCode::MONO_LOAD_PROPERTY_ON_PROTO:
            LowerMonoLoadPropertyOnProto(gate);
            break;
        case OpCode::MONO_STORE_PROPERTY_LOOK_UP_PROTO:
            LowerMonoStorePropertyLookUpProto(gate, glue);
            break;
        case OpCode::MONO_STORE_PROPERTY:
            LowerMonoStoreProperty(gate, glue);
            break;
        case OpCode::TYPED_CREATE_OBJ_WITH_BUFFER:
            LowerTypedCreateObjWithBuffer(gate, glue);
            break;
        case OpCode::STRING_FROM_SINGLE_CHAR_CODE:
            LowerStringFromSingleCharCode(gate, glue);
            break;
        case OpCode::MIGRATE_ARRAY_WITH_KIND:
            LowerMigrateArrayWithKind(gate);
            break;
        case OpCode::NUMBER_TO_STRING:
            LowerNumberToString(gate, glue);
            break;
        case OpCode::ECMA_OBJECT_CHECK:
            LowerEcmaObjectCheck(gate);
            break;
        default:
            break;
    }
    return Circuit::NullGate();
}

void TypedHCRLowering::LowerJSCallTargetCheck(GateRef gate)
{
    TypedCallTargetCheckOp Op = acc_.GetTypedCallTargetCheckOp(gate);
    switch (Op) {
        case TypedCallTargetCheckOp::JSCALL: {
            LowerJSCallTargetTypeCheck(gate);
            break;
        }
        case TypedCallTargetCheckOp::JSCALL_FAST: {
            LowerJSFastCallTargetTypeCheck(gate);
            break;
        }
        case TypedCallTargetCheckOp::JSCALLTHIS: {
            LowerJSCallThisTargetTypeCheck(gate);
            break;
        }
        case TypedCallTargetCheckOp::JSCALLTHIS_FAST: {
            LowerJSFastCallThisTargetTypeCheck(gate);
            break;
        }
        case TypedCallTargetCheckOp::JSCALLTHIS_NOGC: {
            LowerJSNoGCCallThisTargetTypeCheck(gate);
            break;
        }
        case TypedCallTargetCheckOp::JSCALLTHIS_FAST_NOGC: {
            LowerJSNoGCFastCallThisTargetTypeCheck(gate);
            break;
        }
        case TypedCallTargetCheckOp::JS_NEWOBJRANGE: {
            LowerJSNewObjRangeCallTargetCheck(gate);
            break;
        }
        default:
            LOG_ECMA(FATAL) << "this branch is unreachable";
            UNREACHABLE();
    }
}

void TypedHCRLowering::LowerPrimitiveTypeCheck(GateRef gate)
{
    Environment env(gate, circuit_, &builder_);
    auto type = acc_.GetParamGateType(gate);
    if (type.IsIntType()) {
        LowerIntCheck(gate);
    } else if (type.IsDoubleType()) {
        LowerDoubleCheck(gate);
    } else if (type.IsNumberType()) {
        LowerNumberCheck(gate);
    } else if (type.IsBooleanType()) {
        LowerBooleanCheck(gate);
    } else {
        LOG_ECMA(FATAL) << "this branch is unreachable";
        UNREACHABLE();
    }
}

void TypedHCRLowering::LowerIntCheck(GateRef gate)
{
    GateRef frameState = GetFrameState(gate);

    GateRef value = acc_.GetValueIn(gate, 0);
    GateRef typeCheck = builder_.TaggedIsInt(value);
    builder_.DeoptCheck(typeCheck, frameState, DeoptType::NOTINT6);

    acc_.ReplaceGate(gate, builder_.GetState(), builder_.GetDepend(), Circuit::NullGate());
}

void TypedHCRLowering::LowerDoubleCheck(GateRef gate)
{
    GateRef frameState = GetFrameState(gate);

    GateRef value = acc_.GetValueIn(gate, 0);
    GateRef typeCheck = builder_.TaggedIsDouble(value);
    builder_.DeoptCheck(typeCheck, frameState, DeoptType::NOTDOUBLE3);

    acc_.ReplaceGate(gate, builder_.GetState(), builder_.GetDepend(), Circuit::NullGate());
}

void TypedHCRLowering::LowerNumberCheck(GateRef gate)
{
    GateRef frameState = GetFrameState(gate);

    GateRef value = acc_.GetValueIn(gate, 0);
    GateRef typeCheck = builder_.TaggedIsNumber(value);
    builder_.DeoptCheck(typeCheck, frameState, DeoptType::NOTNUMBER2);

    acc_.ReplaceGate(gate, builder_.GetState(), builder_.GetDepend(), Circuit::NullGate());
}

void TypedHCRLowering::LowerBooleanCheck(GateRef gate)
{
    GateRef frameState = GetFrameState(gate);

    GateRef value = acc_.GetValueIn(gate, 0);
    GateRef typeCheck = builder_.TaggedIsBoolean(value);
    builder_.DeoptCheck(typeCheck, frameState, DeoptType::NOTBOOL2);

    acc_.ReplaceGate(gate, builder_.GetState(), builder_.GetDepend(), Circuit::NullGate());
}

void TypedHCRLowering::LowerStableArrayCheck(GateRef gate)
{
    Environment env(gate, circuit_, &builder_);
    GateRef frameState = GetFrameState(gate);

    GateRef receiver = acc_.GetValueIn(gate, 0);
    builder_.HeapObjectCheck(receiver, frameState);

    GateRef receiverHClass = builder_.LoadConstOffset(
        VariableType::JS_POINTER(), receiver, TaggedObject::HCLASS_OFFSET);
    ArrayMetaDataAccessor accessor = acc_.GetArrayMetaDataAccessor(gate);
    builder_.HClassStableArrayCheck(receiverHClass, frameState, accessor);
    builder_.ArrayGuardianCheck(frameState);

    acc_.ReplaceGate(gate, builder_.GetState(), builder_.GetDepend(), Circuit::NullGate());
}

void TypedHCRLowering::SetDeoptTypeInfo(JSType jstype, DeoptType &type, size_t &typedArrayRootHclassIndex,
    size_t &typedArrayRootHclassOnHeapIndex)
{
    type = DeoptType::NOTARRAY1;
    switch (jstype) {
        case JSType::JS_INT8_ARRAY:
            typedArrayRootHclassIndex = GlobalEnv::INT8_ARRAY_ROOT_HCLASS_INDEX;
            typedArrayRootHclassOnHeapIndex = GlobalEnv::INT8_ARRAY_ROOT_HCLASS_ON_HEAP_INDEX;
            break;
        case JSType::JS_UINT8_ARRAY:
            typedArrayRootHclassIndex = GlobalEnv::UINT8_ARRAY_ROOT_HCLASS_INDEX;
            typedArrayRootHclassOnHeapIndex = GlobalEnv::UINT8_ARRAY_ROOT_HCLASS_ON_HEAP_INDEX;
            break;
        case JSType::JS_UINT8_CLAMPED_ARRAY:
            typedArrayRootHclassIndex = GlobalEnv::UINT8_CLAMPED_ARRAY_ROOT_HCLASS_INDEX;
            typedArrayRootHclassOnHeapIndex = GlobalEnv::UINT8_CLAMPED_ARRAY_ROOT_HCLASS_ON_HEAP_INDEX;
            break;
        case JSType::JS_INT16_ARRAY:
            typedArrayRootHclassIndex = GlobalEnv::INT16_ARRAY_ROOT_HCLASS_INDEX;
            typedArrayRootHclassOnHeapIndex = GlobalEnv::INT16_ARRAY_ROOT_HCLASS_ON_HEAP_INDEX;
            break;
        case JSType::JS_UINT16_ARRAY:
            typedArrayRootHclassIndex = GlobalEnv::UINT16_ARRAY_ROOT_HCLASS_INDEX;
            typedArrayRootHclassOnHeapIndex = GlobalEnv::UINT16_ARRAY_ROOT_HCLASS_ON_HEAP_INDEX;
            break;
        case JSType::JS_INT32_ARRAY:
            typedArrayRootHclassIndex = GlobalEnv::INT32_ARRAY_ROOT_HCLASS_INDEX;
            typedArrayRootHclassOnHeapIndex = GlobalEnv::INT32_ARRAY_ROOT_HCLASS_ON_HEAP_INDEX;
            break;
        case JSType::JS_UINT32_ARRAY:
            typedArrayRootHclassIndex = GlobalEnv::UINT32_ARRAY_ROOT_HCLASS_INDEX;
            typedArrayRootHclassOnHeapIndex = GlobalEnv::UINT32_ARRAY_ROOT_HCLASS_ON_HEAP_INDEX;
            break;
        case JSType::JS_FLOAT32_ARRAY:
            typedArrayRootHclassIndex = GlobalEnv::FLOAT32_ARRAY_ROOT_HCLASS_INDEX;
            typedArrayRootHclassOnHeapIndex = GlobalEnv::FLOAT32_ARRAY_ROOT_HCLASS_ON_HEAP_INDEX;
            break;
        case JSType::JS_FLOAT64_ARRAY:
            typedArrayRootHclassIndex = GlobalEnv::FLOAT64_ARRAY_ROOT_HCLASS_INDEX;
            typedArrayRootHclassOnHeapIndex = GlobalEnv::FLOAT64_ARRAY_ROOT_HCLASS_ON_HEAP_INDEX;
            break;
        case JSType::JS_BIGINT64_ARRAY:
            typedArrayRootHclassIndex = GlobalEnv::BIGINT64_ARRAY_ROOT_HCLASS_INDEX;
            typedArrayRootHclassOnHeapIndex = GlobalEnv::BIGINT64_ARRAY_ROOT_HCLASS_ON_HEAP_INDEX;
            break;
        case JSType::JS_BIGUINT64_ARRAY:
            typedArrayRootHclassIndex = GlobalEnv::BIGUINT64_ARRAY_ROOT_HCLASS_INDEX;
            typedArrayRootHclassOnHeapIndex = GlobalEnv::BIGUINT64_ARRAY_ROOT_HCLASS_ON_HEAP_INDEX;
            break;
        default:
            LOG_ECMA(FATAL) << "this branch is unreachable";
            UNREACHABLE();
    }
}

void TypedHCRLowering::LowerTypedArrayCheck(GateRef gate)
{
    Environment env(gate, circuit_, &builder_);
    TypedArrayMetaDataAccessor accessor = acc_.GetTypedArrayMetaDataAccessor(gate);
    size_t typedArrayRootHclassIndex = GlobalEnv::INT8_ARRAY_ROOT_HCLASS_INDEX;
    size_t typedArrayRootHclassOnHeapIndex = GlobalEnv::INT8_ARRAY_ROOT_HCLASS_ON_HEAP_INDEX;
    auto deoptType = DeoptType::NONE;
    ParamType paramType = accessor.GetParamType();
    ASSERT(paramType.IsBuiltinType());
    auto builtinType = paramType.GetBuiltinType();
    SetDeoptTypeInfo(builtinType, deoptType, typedArrayRootHclassIndex, typedArrayRootHclassOnHeapIndex);

    GateRef frameState = GetFrameState(gate);
    GateRef glueGlobalEnv = builder_.GetGlobalEnv();
    GateRef receiver = acc_.GetValueIn(gate, 0);
    builder_.HeapObjectCheck(receiver, frameState);
    GateRef receiverHClass = builder_.LoadHClass(receiver);
    GateRef rootHclass = builder_.GetGlobalEnvObj(glueGlobalEnv, typedArrayRootHclassIndex);
    GateRef rootOnHeapHclass = builder_.GetGlobalEnvObj(glueGlobalEnv, typedArrayRootHclassOnHeapIndex);
    GateRef check1 = builder_.Equal(receiverHClass, rootHclass);
    GateRef check2 = builder_.Equal(receiverHClass, rootOnHeapHclass);
    builder_.DeoptCheck(builder_.BoolOr(check1, check2), frameState, deoptType);

    OnHeapMode onHeapMode = accessor.GetOnHeapMode();
    if (accessor.IsAccessElement() && !OnHeap::IsNone(onHeapMode)) {
        GateRef profilingOnHeap = builder_.Boolean(OnHeap::ToBoolean(onHeapMode));
        GateRef runtimeOnHeap = builder_.IsOnHeap(receiverHClass);
        GateRef onHeapCheck = builder_.Equal(profilingOnHeap, runtimeOnHeap);
        builder_.DeoptCheck(onHeapCheck, frameState, DeoptType::INCONSISTENTONHEAP1);
    }

    acc_.ReplaceGate(gate, builder_.GetState(), builder_.GetDepend(), Circuit::NullGate());
}

void TypedHCRLowering::LowerEcmaStringCheck(GateRef gate)
{
    Environment env(gate, circuit_, &builder_);
    GateRef frameState = GetFrameState(gate);
    GateRef receiver = acc_.GetValueIn(gate, 0);
    builder_.HeapObjectCheck(receiver, frameState);
    GateRef isString = builder_.TaggedObjectIsString(receiver);
    builder_.DeoptCheck(isString, frameState, DeoptType::NOTSTRING1);

    acc_.ReplaceGate(gate, builder_.GetState(), builder_.GetDepend(), Circuit::NullGate());
}

void TypedHCRLowering::LowerEcmaMapCheck(GateRef gate)
{
    Environment env(gate, circuit_, &builder_);
    GateRef frameState = GetFrameState(gate);
    GateRef receiver = acc_.GetValueIn(gate, 0);
    builder_.HeapObjectCheck(receiver, frameState);

    GateRef hclass = builder_.LoadHClass(receiver);

    size_t mapHclassIndex = GlobalEnv::MAP_CLASS_INDEX;
    GateRef glueGlobalEnv = builder_.GetGlobalEnv();
    GateRef mapHclass = builder_.GetGlobalEnvObj(glueGlobalEnv, mapHclassIndex);
    GateRef isMap = builder_.Equal(hclass, mapHclass, "Check HClass");

    builder_.DeoptCheck(isMap, frameState, DeoptType::ISNOTMAP);
    acc_.ReplaceGate(gate, builder_.GetState(), builder_.GetDepend(), Circuit::NullGate());
}

void TypedHCRLowering::LowerFlattenTreeStringCheck(GateRef gate, GateRef glue)
{
    Environment env(gate, circuit_, &builder_);
    GateRef str = acc_.GetValueIn(gate, 0);
    DEFVALUE(result, (&builder_), VariableType::JS_POINTER(), str);
    Label isTreeString(&builder_);
    Label exit(&builder_);

    BRANCH_CIR(builder_.IsTreeString(str), &isTreeString, &exit);
    builder_.Bind(&isTreeString);
    {
        Label isFlat(&builder_);
        Label needFlat(&builder_);
        BRANCH_CIR(builder_.TreeStringIsFlat(str), &isFlat, &needFlat);
        builder_.Bind(&isFlat);
        {
            result = builder_.GetFirstFromTreeString(str);
            builder_.Jump(&exit);
        }
        builder_.Bind(&needFlat);
        {
            result = LowerCallRuntime(glue, gate, RTSTUB_ID(SlowFlattenString), { str }, true);
            builder_.Jump(&exit);
        }
    }

    builder_.Bind(&exit);
    acc_.ReplaceGate(gate, builder_.GetState(), builder_.GetDepend(), *result);
}

GateRef TypedHCRLowering::GetLengthFromString(GateRef gate)
{
    GateRef shiftCount = builder_.Int32(EcmaString::STRING_LENGTH_SHIFT_COUNT);
    return builder_.Int32LSR(
        builder_.LoadConstOffset(VariableType::INT32(), gate, EcmaString::MIX_LENGTH_OFFSET), shiftCount);
}

void TypedHCRLowering::LowerStringLength(GateRef gate)
{
    Environment env(gate, circuit_, &builder_);
    GateRef receiver = acc_.GetValueIn(gate, 0);
    GateRef length = GetLengthFromString(receiver);

    acc_.ReplaceGate(gate, builder_.GetState(), builder_.GetDepend(), length);
}

void TypedHCRLowering::LowerMapSize(GateRef gate)
{
    Environment env(gate, circuit_, &builder_);
    GateRef receiver = acc_.GetValueIn(gate, 0);

    GateRef linkedMap = builder_.LoadConstOffset(VariableType::JS_ANY(), receiver, JSMap::LINKED_MAP_OFFSET);
    GateRef mapSizeTagged = builder_.LoadFromTaggedArray(linkedMap, LinkedHashMap::NUMBER_OF_ELEMENTS_INDEX);
    GateRef mapSize = builder_.TaggedGetInt(mapSizeTagged);

    acc_.ReplaceGate(gate, builder_.GetState(), builder_.GetDepend(), mapSize);
}

void TypedHCRLowering::LowerLoadTypedArrayLength(GateRef gate)
{
    Environment env(gate, circuit_, &builder_);
    GateRef receiver = acc_.GetValueIn(gate, 0);
    GateRef length = builder_.LoadConstOffset(VariableType::INT32(), receiver, JSTypedArray::ARRAY_LENGTH_OFFSET);
    acc_.ReplaceGate(gate, builder_.GetState(), builder_.GetDepend(), length);
}

void TypedHCRLowering::LowerObjectTypeCheck(GateRef gate)
{
    Environment env(gate, circuit_, &builder_);
    LowerSimpleHClassCheck(gate);
}

void TypedHCRLowering::LowerSimpleHClassCheck(GateRef gate)
{
    GateRef frameState = GetFrameState(gate);
    GateRef compare = BuildCompareHClass(gate, frameState);
    builder_.DeoptCheck(compare, frameState, DeoptType::INCONSISTENTHCLASS6);
    acc_.ReplaceGate(gate, builder_.GetState(), builder_.GetDepend(), Circuit::NullGate());
}

GateRef TypedHCRLowering::BuildCompareHClass(GateRef gate, GateRef frameState)
{
    GateRef receiver = acc_.GetValueIn(gate, 0);
    bool isHeapObject = acc_.GetObjectTypeAccessor(gate).IsHeapObject();
    if (!isHeapObject) {
        builder_.HeapObjectCheck(receiver, frameState);
    }
    GateRef aotHCIndex = acc_.GetValueIn(gate, 1);
    auto hclassIndex = acc_.GetConstantValue(aotHCIndex);
    ArgumentAccessor argAcc(circuit_);
    GateRef unsharedConstPool = argAcc.GetFrameArgsIn(frameState, FrameArgIdx::UNSHARED_CONST_POOL);
    GateRef aotHCGate = builder_.LoadHClassFromConstpool(unsharedConstPool, hclassIndex);
    GateRef receiverHClass = builder_.LoadConstOffset(
        VariableType::JS_POINTER(), receiver, TaggedObject::HCLASS_OFFSET);
    return builder_.Equal(aotHCGate, receiverHClass, "checkHClass");
}

void TypedHCRLowering::LowerRangeCheckPredicate(GateRef gate)
{
    Environment env(gate, circuit_, &builder_);
    auto deoptType = DeoptType::NOTARRAY1;
    GateRef frameState = GetFrameState(gate);
    GateRef x = acc_.GetValueIn(gate, 0);
    GateRef y = acc_.GetValueIn(gate, 1);
    TypedBinaryAccessor accessor = acc_.GetTypedBinaryAccessor(gate);
    TypedBinOp cond = accessor.GetTypedBinOp();
    GateRef check = Circuit::NullGate();
    // check the condition
    switch (cond) {
        case TypedBinOp::TYPED_GREATER:
            check = builder_.Int32GreaterThan(x, y);
            break;
        case TypedBinOp::TYPED_GREATEREQ:
            check = builder_.Int32GreaterThanOrEqual(x, y);
            break;
        case TypedBinOp::TYPED_LESS:
            check = builder_.Int32LessThan(x, y);
            break;
        case TypedBinOp::TYPED_LESSEQ:
            check = builder_.Int32LessThanOrEqual(x, y);
            break;
        default:
            UNREACHABLE();
            break;
    }
    builder_.DeoptCheck(check, frameState, deoptType);
    acc_.ReplaceGate(gate, builder_.GetState(), builder_.GetDepend(), Circuit::NullGate());
}

void TypedHCRLowering::BuiltinInstanceHClassCheck(Environment *env, GateRef gate)
{
    BuiltinPrototypeHClassAccessor accessor = acc_.GetBuiltinHClassAccessor(gate);
    BuiltinTypeId type = accessor.GetBuiltinTypeId();
    ElementsKind kind = accessor.GetElementsKind();
    GateRef frameState = GetFrameState(gate);
    GateRef glue = acc_.GetGlueFromArgList();
    GateRef receiver = acc_.GetValueIn(gate, 0);
    GateRef ihcMatches = Circuit::NullGate();
    if (type == BuiltinTypeId::ARRAY) {
        if (Elements::IsGeneric(kind)) {
            auto arrayHClassIndexMap = compilationEnv_->GetArrayHClassIndexMap();
            auto iter = arrayHClassIndexMap.find(kind);
            ASSERT(iter != arrayHClassIndexMap.end());
            GateRef initialIhcAddress = builder_.GetGlobalConstantValue(iter->second.first);
            GateRef initialIhcWithProtoAddress = builder_.GetGlobalConstantValue(iter->second.second);
            GateRef receiverHClass = builder_.LoadHClassByConstOffset(receiver);
            GateRef tryIhcMatches = builder_.Equal(receiverHClass, initialIhcAddress);
            GateRef tryIhcWithProtoMatches = builder_.Equal(receiverHClass, initialIhcWithProtoAddress);
            ihcMatches = builder_.BoolOr(tryIhcMatches, tryIhcWithProtoMatches);
        } else {
            GateRef receiverHClass = builder_.LoadHClassByConstOffset(receiver);
            GateRef elementsKind = builder_.GetElementsKindByHClass(receiverHClass);
            ihcMatches =
                builder_.NotEqual(elementsKind, builder_.Int32(static_cast<size_t>(ElementsKind::GENERIC)));
        }
    } else {
        size_t ihcOffset = JSThread::GlueData::GetBuiltinInstanceHClassOffset(type, env->IsArch32Bit());
        GateRef initialIhcAddress = builder_.LoadConstOffset(VariableType::JS_POINTER(), glue, ihcOffset);
        GateRef receiverHClass = builder_.LoadHClassByConstOffset(receiver);
        if (IsTypedArrayType(type)) {
             // check IHC onHeap hclass
            size_t ihcOnHeapOffset = JSThread::GlueData::GetBuiltinExtraHClassOffset(type, env->IsArch32Bit());
            GateRef initialIhcOnHeapAddress = builder_.LoadConstOffset(VariableType::JS_POINTER(),
                                                                       glue, ihcOnHeapOffset);
            ihcMatches = builder_.Int64Or(builder_.Equal(receiverHClass, initialIhcAddress),
                                          builder_.Equal(receiverHClass, initialIhcOnHeapAddress));
        } else {
            ihcMatches = builder_.Equal(receiverHClass, initialIhcAddress);
        }
    }
    // De-opt if HClass of x changed where X is the current builtin object.
    builder_.DeoptCheck(ihcMatches, frameState, DeoptType::BUILTININSTANCEHCLASSMISMATCH);
}

void TypedHCRLowering::BuiltinPrototypeHClassCheck(Environment *env, GateRef gate)
{
    BuiltinPrototypeHClassAccessor accessor = acc_.GetBuiltinHClassAccessor(gate);
    BuiltinTypeId type = accessor.GetBuiltinTypeId();
    bool isPrototypeOfPrototype = accessor.IsPrototypeOfPrototype();
    GateRef frameState = GetFrameState(gate);
    GateRef glue = acc_.GetGlueFromArgList();
    GateRef receiver = acc_.GetValueIn(gate, 0);
    // Only HClasses recorded in the JSThread during builtin initialization are available
    [[maybe_unused]] JSHClass *initialPrototypeHClass = compilationEnv_->GetBuiltinPrototypeHClass(type);
    ASSERT(initialPrototypeHClass != nullptr);

    // Phc = PrototypeHClass
    size_t phcOffset = JSThread::GlueData::GetBuiltinPrototypeHClassOffset(type, env->IsArch32Bit());
    GateRef receiverPhcAddress = builder_.LoadPrototypeHClass(receiver);
    GateRef initialPhcAddress = builder_.LoadConstOffset(VariableType::JS_POINTER(), glue, phcOffset);
    GateRef phcMatches = builder_.Equal(receiverPhcAddress, initialPhcAddress);
    // De-opt if HClass of X.prototype changed where X is the current builtin object.
    builder_.DeoptCheck(phcMatches, frameState, DeoptType::BUILTINPROTOHCLASSMISMATCH1);

    // array.Iterator should compare PrototypeOfPrototypeHClass.
    if (isPrototypeOfPrototype) {
        size_t pphcOffset = JSThread::GlueData::GetBuiltinPrototypeOfPrototypeHClassOffset(type, env->IsArch32Bit());
        GateRef receiverPPhcAddress = builder_.LoadPrototypeOfPrototypeHClass(receiver);
        GateRef initialPPhcAddress = builder_.LoadConstOffset(VariableType::JS_POINTER(), glue, pphcOffset);
        GateRef pphcMatches = builder_.Equal(receiverPPhcAddress, initialPPhcAddress);
        // De-opt if HClass of X.prototype.prototype changed where X is the current builtin object.
        builder_.DeoptCheck(pphcMatches, frameState, DeoptType::BUILTINPROTOHCLASSMISMATCH2);
    }
}

void TypedHCRLowering::BuiltinInstanceStringTypeCheck(GateRef gate)
{
    BuiltinPrototypeHClassAccessor accessor = acc_.GetBuiltinHClassAccessor(gate);
    [[maybe_unused]] BuiltinTypeId type = accessor.GetBuiltinTypeId();
    ASSERT(type == BuiltinTypeId::STRING);
    GateRef frameState = GetFrameState(gate);
    GateRef receiver = acc_.GetValueIn(gate, 0);
    GateRef typeCheck = builder_.TaggedObjectIsString(receiver);
    builder_.DeoptCheck(typeCheck, frameState, DeoptType::BUILTININSTANCEHCLASSMISMATCH2);
}

void TypedHCRLowering::LowerBuiltinPrototypeHClassCheck(GateRef gate)
{
    Environment env(gate, circuit_, &builder_);
    GateRef frameState = GetFrameState(gate);
    GateRef receiver = acc_.GetValueIn(gate, 0);
    builder_.HeapObjectCheck(receiver, frameState);
    BuiltinPrototypeHClassAccessor accessor = acc_.GetBuiltinHClassAccessor(gate);
    BuiltinTypeId type = accessor.GetBuiltinTypeId();
    // BuiltinTypeId::STRING represents primitive string, only need to check the type of hclass here.
    if (type == BuiltinTypeId::STRING) {
        BuiltinInstanceStringTypeCheck(gate);
    } else {
        BuiltinInstanceHClassCheck(&env, gate); // check IHC
        BuiltinPrototypeHClassCheck(&env, gate); // check PHC
    }
    acc_.ReplaceGate(gate, builder_.GetState(), builder_.GetDepend(), Circuit::NullGate());
}

void TypedHCRLowering::LowerIndexCheck(GateRef gate)
{
    Environment env(gate, circuit_, &builder_);
    auto deoptType = DeoptType::NOTLEGALIDX1;

    GateRef frameState = GetFrameState(gate);
    GateRef length = acc_.GetValueIn(gate, 0);
    GateRef index = acc_.GetValueIn(gate, 1);
    ASSERT(acc_.GetGateType(length).IsNJSValueType());
    // UnsignedLessThan can check both lower and upper bounds
    GateRef lengthCheck = builder_.Int32UnsignedLessThan(index, length);
    builder_.DeoptCheck(lengthCheck, frameState, deoptType);
    acc_.ReplaceGate(gate, builder_.GetState(), builder_.GetDepend(), index);
}

GateRef TypedHCRLowering::LowerCallRuntime(GateRef glue, GateRef hirGate, int index, const std::vector<GateRef> &args,
                                           bool useLabel)
{
    const std::string name = RuntimeStubCSigns::GetRTName(index);
    if (useLabel) {
        GateRef result = builder_.CallRuntime(glue, index, Gate::InvalidGateRef, args, hirGate, name.c_str());
        return result;
    } else {
        const CallSignature *cs = RuntimeStubCSigns::Get(RTSTUB_ID(CallRuntime));
        GateRef target = builder_.IntPtr(index);
        GateRef result = builder_.Call(cs, glue, target, dependEntry_, args, hirGate, name.c_str());
        return result;
    }
}

void TypedHCRLowering::LowerTypeConvert(GateRef gate)
{
    Environment env(gate, circuit_, &builder_);

    TypeConvertAccessor accessor(acc_.TryGetValue(gate));
    ParamType leftType = accessor.GetLeftType();
    GateType rightType = accessor.GetRightType();
    if (rightType.IsNumberType()) {
        GateRef value = acc_.GetValueIn(gate, 0);
        // NOTICE-PGO: wx support undefined/null/boolean type:
        if (leftType.HasNumberType()) {
            LowerPrimitiveToNumber(gate, value, leftType);
        }
        return;
    }
}

void TypedHCRLowering::LowerPrimitiveToNumber(GateRef dst, GateRef src, ParamType srcType)
{
    DEFVALUE(result, (&builder_), VariableType::JS_ANY(), builder_.HoleConstant());
    if (srcType.IsBooleanType()) {
        Label exit(&builder_);
        Label isTrue(&builder_);
        Label isFalse(&builder_);
        BRANCH_CIR(builder_.TaggedIsTrue(src), &isTrue, &isFalse);
        builder_.Bind(&isTrue);
        {
            result = IntToTaggedIntPtr(builder_.Int32(1));
            builder_.Jump(&exit);
        }
        builder_.Bind(&isFalse);
        {
            result = IntToTaggedIntPtr(builder_.Int32(0));
            builder_.Jump(&exit);
        }
        builder_.Bind(&exit);
    } else if (srcType.IsUndefinedType()) {
        result = DoubleToTaggedDoublePtr(builder_.Double(base::NAN_VALUE));
    } else if (srcType.IsBigIntType() || srcType.HasNumberType()) {
        ASSERT(!srcType.IsIntOverflowType());
        result = src;
    } else if (srcType.IsNullType()) {
        result = IntToTaggedIntPtr(builder_.Int32(0));
    } else {
        LOG_ECMA(FATAL) << "this branch is unreachable";
        UNREACHABLE();
    }
    acc_.ReplaceGate(dst, builder_.GetState(), builder_.GetDepend(), *result);
}

GateRef TypedHCRLowering::LoadFromConstPool(GateRef unsharedConstPool, size_t index, size_t valVecType)
{
    GateRef constPoolSize = builder_.GetLengthOfTaggedArray(unsharedConstPool);
    GateRef valVecIndex = builder_.Int32Sub(constPoolSize, builder_.Int32(valVecType));
    GateRef valVec = builder_.GetValueFromTaggedArray(unsharedConstPool, valVecIndex);
    return builder_.LoadFromTaggedArray(valVec, index);
}

void TypedHCRLowering::LowerLoadProperty(GateRef gate)
{
    Environment env(gate, circuit_, &builder_);
    ASSERT(acc_.GetNumValueIn(gate) == 2);  // 2: receiver, plr
    GateRef receiver = acc_.GetValueIn(gate, 0);
    GateRef propertyLookupResult = acc_.GetValueIn(gate, 1);
    PropertyLookupResult plr(acc_.TryGetValue(propertyLookupResult));
    ASSERT(plr.IsLocal() || plr.IsFunction());

    GateRef result = LoadPropertyFromHolder(receiver, plr);

    acc_.ReplaceGate(gate, builder_.GetState(), builder_.GetDepend(), result);
}

void TypedHCRLowering::LowerCallPrivateGetter(GateRef gate, GateRef glue)
{
    Environment env(gate, circuit_, &builder_);
    ASSERT(acc_.GetNumValueIn(gate) == 2); // 2: receiver, accessor
    GateRef receiver = acc_.GetValueIn(gate, 0);
    GateRef accessor = acc_.GetValueIn(gate, 1);

    DEFVALUE(result, (&builder_), VariableType::JS_ANY(), builder_.UndefineConstant());
    result = CallAccessor(glue, gate, accessor, receiver, AccessorMode::GETTER);
    ReplaceHirWithPendingException(gate, glue, builder_.GetState(), builder_.GetDepend(), *result);
}

void TypedHCRLowering::LowerCallPrivateSetter(GateRef gate, GateRef glue)
{
    Environment env(gate, circuit_, &builder_);
    ASSERT(acc_.GetNumValueIn(gate) == 3); // 3: receiver, accessor, value
    GateRef receiver = acc_.GetValueIn(gate, 0);
    GateRef accessor = acc_.GetValueIn(gate, 1);
    GateRef value = acc_.GetValueIn(gate, 2);

    CallAccessor(glue, gate, accessor, receiver, AccessorMode::SETTER, value);
    ReplaceHirWithPendingException(gate, glue, builder_.GetState(), builder_.GetDepend(), Circuit::NullGate());
}

void TypedHCRLowering::LowerCallGetter(GateRef gate, GateRef glue)
{
    Environment env(gate, circuit_, &builder_);
    ASSERT(acc_.GetNumValueIn(gate) == 3);  // 3: receiver, holder, plr
    GateRef receiver = acc_.GetValueIn(gate, 0);
    GateRef propertyLookupResult = acc_.GetValueIn(gate, 1);
    GateRef holder = acc_.GetValueIn(gate, 2);
    PropertyLookupResult plr(acc_.TryGetValue(propertyLookupResult));

    GateRef accessor = LoadPropertyFromHolder(holder, plr);

    DEFVALUE(result, (&builder_), VariableType::JS_ANY(), builder_.UndefineConstant());
    Label isInternalAccessor(&builder_);
    Label notInternalAccessor(&builder_);
    Label callGetter(&builder_);
    Label exit(&builder_);
    BRANCH_CIR(builder_.IsAccessorInternal(accessor), &isInternalAccessor, &notInternalAccessor);
    {
        builder_.Bind(&isInternalAccessor);
        {
            result = builder_.CallRuntime(glue, RTSTUB_ID(CallInternalGetter),
                Gate::InvalidGateRef, { accessor, holder }, gate);
            builder_.Jump(&exit);
        }
        builder_.Bind(&notInternalAccessor);
        {
            GateRef getter = builder_.LoadConstOffset(VariableType::JS_ANY(), accessor, AccessorData::GETTER_OFFSET);
            BRANCH_CIR(builder_.IsSpecial(getter, JSTaggedValue::VALUE_UNDEFINED), &exit, &callGetter);
            builder_.Bind(&callGetter);
            {
                result = CallAccessor(glue, gate, getter, receiver, AccessorMode::GETTER);
                builder_.Jump(&exit);
            }
        }
    }
    builder_.Bind(&exit);
    ReplaceHirWithPendingException(gate, glue, builder_.GetState(), builder_.GetDepend(), *result);
}

void TypedHCRLowering::LowerStoreProperty(GateRef gate)
{
    Environment env(gate, circuit_, &builder_);
    ASSERT(acc_.GetNumValueIn(gate) == 3);  // 3: receiver, plr, value
    GateRef receiver = acc_.GetValueIn(gate, 0);
    GateRef propertyLookupResult = acc_.GetValueIn(gate, 1);
    GateRef value = acc_.GetValueIn(gate, 2); // 2: value
    PropertyLookupResult plr(acc_.TryGetValue(propertyLookupResult));
    ASSERT(plr.IsLocal());
    auto op = OpCode(acc_.GetOpCode(gate));
    if (op == OpCode::STORE_PROPERTY) {
        if (plr.IsInlinedProps()) {
            builder_.StoreConstOffset(VariableType::JS_ANY(), receiver, plr.GetOffset(), value);
        } else {
            auto properties = builder_.LoadConstOffset(VariableType::JS_ANY(), receiver, JSObject::PROPERTIES_OFFSET);
            builder_.SetValueToTaggedArray(
                VariableType::JS_ANY(), acc_.GetGlueFromArgList(), properties, builder_.Int32(plr.GetOffset()), value);
        }
    } else if (op == OpCode::STORE_PROPERTY_NO_BARRIER) {
        if (plr.IsInlinedProps()) {
            builder_.StoreConstOffset(GetVarType(plr), receiver, plr.GetOffset(), value);
        } else {
            auto properties = builder_.LoadConstOffset(VariableType::JS_ANY(), receiver, JSObject::PROPERTIES_OFFSET);
            builder_.SetValueToTaggedArray(
                GetVarType(plr), acc_.GetGlueFromArgList(), properties, builder_.Int32(plr.GetOffset()), value);
        }
    } else {
        UNREACHABLE();
    }
    acc_.ReplaceGate(gate, builder_.GetState(), builder_.GetDepend(), Circuit::NullGate());
}

void TypedHCRLowering::LowerCallSetter(GateRef gate, GateRef glue)
{
    Environment env(gate, circuit_, &builder_);
    ASSERT(acc_.GetNumValueIn(gate) == 4);  // 4: receiver, holder, plr, value
    GateRef receiver = acc_.GetValueIn(gate, 0);
    GateRef propertyLookupResult = acc_.GetValueIn(gate, 1);
    GateRef holder = acc_.GetValueIn(gate, 2);
    GateRef value = acc_.GetValueIn(gate, 3);

    PropertyLookupResult plr(acc_.TryGetValue(propertyLookupResult));
    ASSERT(plr.IsAccessor());
    GateRef accessor = Circuit::NullGate();
    if (plr.IsNotHole()) {
        ASSERT(plr.IsLocal());
        if (plr.IsInlinedProps()) {
            accessor = builder_.LoadConstOffset(VariableType::JS_ANY(), holder, plr.GetOffset());
        } else {
            auto properties = builder_.LoadConstOffset(VariableType::JS_ANY(), holder, JSObject::PROPERTIES_OFFSET);
            accessor = builder_.GetValueFromTaggedArray(properties, builder_.Int32(plr.GetOffset()));
        }
    } else if (plr.IsLocal()) {
        if (plr.IsInlinedProps()) {
            accessor = builder_.LoadConstOffset(VariableType::JS_ANY(), holder, plr.GetOffset());
        } else {
            auto properties = builder_.LoadConstOffset(VariableType::JS_ANY(), holder, JSObject::PROPERTIES_OFFSET);
            accessor = builder_.GetValueFromTaggedArray(properties, builder_.Int32(plr.GetOffset()));
        }
        accessor = builder_.ConvertHoleAsUndefined(accessor);
    } else {
        UNREACHABLE();
    }
    Label isInternalAccessor(&builder_);
    Label notInternalAccessor(&builder_);
    Label callSetter(&builder_);
    Label exit(&builder_);
    BRANCH_CIR(builder_.IsAccessorInternal(accessor), &isInternalAccessor, &notInternalAccessor);
    {
        builder_.Bind(&isInternalAccessor);
        {
            builder_.CallRuntime(glue, RTSTUB_ID(CallInternalSetter),
                Gate::InvalidGateRef, { receiver, accessor, value }, gate);
            builder_.Jump(&exit);
        }
        builder_.Bind(&notInternalAccessor);
        {
            GateRef setter = builder_.LoadConstOffset(VariableType::JS_ANY(), accessor, AccessorData::SETTER_OFFSET);
            BRANCH_CIR(builder_.IsSpecial(setter, JSTaggedValue::VALUE_UNDEFINED), &exit, &callSetter);
            builder_.Bind(&callSetter);
            {
                CallAccessor(glue, gate, setter, receiver, AccessorMode::SETTER, value);
                builder_.Jump(&exit);
            }
        }
    }
    builder_.Bind(&exit);
    ReplaceHirWithPendingException(gate, glue, builder_.GetState(), builder_.GetDepend(), Circuit::NullGate());
}

void TypedHCRLowering::LowerLoadArrayLength(GateRef gate)
{
    Environment env(gate, circuit_, &builder_);
    GateRef array = acc_.GetValueIn(gate, 0);
    GateRef result = builder_.LoadConstOffset(VariableType::INT32(), array, JSArray::LENGTH_OFFSET);
    acc_.SetGateType(gate, GateType::NJSValue());
    acc_.ReplaceGate(gate, builder_.GetState(), builder_.GetDepend(), result);
}

GateRef TypedHCRLowering::GetElementSize(BuiltinTypeId id)
{
    GateRef elementSize = Circuit::NullGate();
    switch (id) {
        case BuiltinTypeId::INT8_ARRAY:
        case BuiltinTypeId::UINT8_ARRAY:
        case BuiltinTypeId::UINT8_CLAMPED_ARRAY:
            elementSize = builder_.Int32(sizeof(uint8_t));
            break;
        case BuiltinTypeId::INT16_ARRAY:
        case BuiltinTypeId::UINT16_ARRAY:
            elementSize = builder_.Int32(sizeof(uint16_t));
            break;
        case BuiltinTypeId::INT32_ARRAY:
        case BuiltinTypeId::UINT32_ARRAY:
        case BuiltinTypeId::FLOAT32_ARRAY:
            elementSize = builder_.Int32(sizeof(uint32_t));
            break;
        case BuiltinTypeId::FLOAT64_ARRAY:
            elementSize = builder_.Int32(sizeof(double));
            break;
        default:
            LOG_ECMA(FATAL) << "this branch is unreachable";
            UNREACHABLE();
    }
    return elementSize;
}

VariableType TypedHCRLowering::GetVariableType(BuiltinTypeId id)
{
    VariableType type = VariableType::JS_ANY();
    switch (id) {
        case BuiltinTypeId::INT8_ARRAY:
        case BuiltinTypeId::UINT8_ARRAY:
        case BuiltinTypeId::UINT8_CLAMPED_ARRAY:
            type = VariableType::INT8();
            break;
        case BuiltinTypeId::INT16_ARRAY:
        case BuiltinTypeId::UINT16_ARRAY:
            type = VariableType::INT16();
            break;
        case BuiltinTypeId::INT32_ARRAY:
        case BuiltinTypeId::UINT32_ARRAY:
            type = VariableType::INT32();
            break;
        case BuiltinTypeId::FLOAT32_ARRAY:
            type = VariableType::FLOAT32();
            break;
        case BuiltinTypeId::FLOAT64_ARRAY:
            type = VariableType::FLOAT64();
            break;
        default:
            LOG_ECMA(FATAL) << "this branch is unreachable";
            UNREACHABLE();
    }
    return type;
}

void TypedHCRLowering::LowerLoadElement(GateRef gate)
{
    Environment env(gate, circuit_, &builder_);
    LoadElementAccessor accessor = acc_.GetLoadElementAccessor(gate);
    TypedLoadOp op = accessor.GetTypedLoadOp();
    switch (op) {
        case TypedLoadOp::ARRAY_LOAD_INT_ELEMENT:
        case TypedLoadOp::ARRAY_LOAD_DOUBLE_ELEMENT:
        case TypedLoadOp::ARRAY_LOAD_OBJECT_ELEMENT:
        case TypedLoadOp::ARRAY_LOAD_TAGGED_ELEMENT:
            LowerArrayLoadElement(gate, ArrayState::PACKED, op);
            break;
        case TypedLoadOp::ARRAY_LOAD_HOLE_TAGGED_ELEMENT:
        case TypedLoadOp::ARRAY_LOAD_HOLE_INT_ELEMENT:
        case TypedLoadOp::ARRAY_LOAD_HOLE_DOUBLE_ELEMENT:
            LowerArrayLoadElement(gate, ArrayState::HOLEY, op);
            break;
        case TypedLoadOp::INT8ARRAY_LOAD_ELEMENT:
            LowerTypedArrayLoadElement(gate, BuiltinTypeId::INT8_ARRAY);
            break;
        case TypedLoadOp::UINT8ARRAY_LOAD_ELEMENT:
            LowerTypedArrayLoadElement(gate, BuiltinTypeId::UINT8_ARRAY);
            break;
        case TypedLoadOp::UINT8CLAMPEDARRAY_LOAD_ELEMENT:
            LowerTypedArrayLoadElement(gate, BuiltinTypeId::UINT8_CLAMPED_ARRAY);
            break;
        case TypedLoadOp::INT16ARRAY_LOAD_ELEMENT:
            LowerTypedArrayLoadElement(gate, BuiltinTypeId::INT16_ARRAY);
            break;
        case TypedLoadOp::UINT16ARRAY_LOAD_ELEMENT:
            LowerTypedArrayLoadElement(gate, BuiltinTypeId::UINT16_ARRAY);
            break;
        case TypedLoadOp::INT32ARRAY_LOAD_ELEMENT:
            LowerTypedArrayLoadElement(gate, BuiltinTypeId::INT32_ARRAY);
            break;
        case TypedLoadOp::UINT32ARRAY_LOAD_ELEMENT:
            LowerTypedArrayLoadElement(gate, BuiltinTypeId::UINT32_ARRAY);
            break;
        case TypedLoadOp::FLOAT32ARRAY_LOAD_ELEMENT:
            LowerTypedArrayLoadElement(gate, BuiltinTypeId::FLOAT32_ARRAY);
            break;
        case TypedLoadOp::FLOAT64ARRAY_LOAD_ELEMENT:
            LowerTypedArrayLoadElement(gate, BuiltinTypeId::FLOAT64_ARRAY);
            break;
        case TypedLoadOp::STRING_LOAD_ELEMENT:
            LowerStringLoadElement(gate);
            break;
        default:
            LOG_ECMA(FATAL) << "this branch is unreachable";
            UNREACHABLE();
    }
}

void TypedHCRLowering::LowerCowArrayCheck(GateRef gate, GateRef glue)
{
    Environment env(gate, circuit_, &builder_);
    GateRef receiver = acc_.GetValueIn(gate, 0);
    Label notCOWArray(&builder_);
    Label isCOWArray(&builder_);
    BRANCH_CIR(builder_.IsJsCOWArray(receiver), &isCOWArray, &notCOWArray);
    builder_.Bind(&isCOWArray);
    {
        LowerCallRuntime(glue, gate, RTSTUB_ID(CheckAndCopyArray), {receiver}, true);
        builder_.Jump(&notCOWArray);
    }
    builder_.Bind(&notCOWArray);

    acc_.ReplaceGate(gate, builder_.GetState(), builder_.GetDepend(), Circuit::NullGate());
}

// for JSArray
void TypedHCRLowering::LowerArrayLoadElement(GateRef gate, ArrayState arrayState, TypedLoadOp op)
{
    Environment env(gate, circuit_, &builder_);
    GateRef receiver = acc_.GetValueIn(gate, 0);
    GateRef index = acc_.GetValueIn(gate, 1);
    GateRef element = builder_.LoadConstOffset(VariableType::JS_POINTER(), receiver, JSObject::ELEMENTS_OFFSET);
    GateRef result = Circuit::NullGate();
    if (arrayState == ArrayState::HOLEY) {
        if (op == TypedLoadOp::ARRAY_LOAD_HOLE_INT_ELEMENT ||
            op == TypedLoadOp::ARRAY_LOAD_HOLE_DOUBLE_ELEMENT) {
            result = builder_.GetValueFromJSArrayWithElementsKind(VariableType::INT64(), element, index);
        } else {
            result = builder_.GetValueFromTaggedArray(element, index);
            result = builder_.ConvertHoleAsUndefined(result);
        }
    } else {
        // When elementsKind swith on, we should get corresponding raw value for Int and Double kind.
        result = builder_.GetValueFromTaggedArray(element, index);
    }
    acc_.ReplaceGate(gate, builder_.GetState(), builder_.GetDepend(), result);
}

void TypedHCRLowering::LowerTypedArrayLoadElement(GateRef gate, BuiltinTypeId id)
{
    Environment env(gate, circuit_, &builder_);
    GateRef receiver = acc_.GetValueIn(gate, 0);
    GateRef index = acc_.GetValueIn(gate, 1);
    GateRef elementSize = GetElementSize(id);
    GateRef offset = builder_.PtrMul(index, elementSize);
    VariableType type = GetVariableType(id);

    GateRef result = Circuit::NullGate();
    LoadElementAccessor accessor = acc_.GetLoadElementAccessor(gate);
    OnHeapMode onHeapMode = accessor.GetOnHeapMode();

    switch (onHeapMode) {
        case OnHeapMode::ON_HEAP: {
            result = BuildOnHeapTypedArrayLoadElement(receiver, offset, type);
            break;
        }
        case OnHeapMode::NOT_ON_HEAP: {
            result = BuildNotOnHeapTypedArrayLoadElement(receiver, offset, type);
            break;
        }
        default: {
            Label isByteArray(&builder_);
            Label isArrayBuffer(&builder_);
            Label exit(&builder_);
            result = BuildTypedArrayLoadElement(receiver, offset, type, &isByteArray, &isArrayBuffer, &exit);
            break;
        }
    }

    switch (id) {
        case BuiltinTypeId::INT8_ARRAY:
            result = builder_.SExtInt8ToInt32(result);
            break;
        case BuiltinTypeId::UINT8_ARRAY:
        case BuiltinTypeId::UINT8_CLAMPED_ARRAY:
            result = builder_.ZExtInt8ToInt32(result);
            break;
        case BuiltinTypeId::INT16_ARRAY:
            result = builder_.SExtInt16ToInt32(result);
            break;
        case BuiltinTypeId::UINT16_ARRAY:
            result = builder_.ZExtInt16ToInt32(result);
            break;
        case BuiltinTypeId::FLOAT32_ARRAY:
            result = builder_.ExtFloat32ToDouble(result);
            break;
        default:
            break;
    }
    acc_.ReplaceGate(gate, builder_.GetState(), builder_.GetDepend(), result);
}

GateRef TypedHCRLowering::BuildOnHeapTypedArrayLoadElement(GateRef receiver, GateRef offset, VariableType type)
{
    GateRef byteArray =
        builder_.LoadConstOffset(VariableType::JS_POINTER(), receiver, JSTypedArray::VIEWED_ARRAY_BUFFER_OFFSET);
    GateRef data = builder_.PtrAdd(byteArray, builder_.IntPtr(ByteArray::DATA_OFFSET));
    GateRef result = builder_.Load(type, data, offset);
    return result;
}

GateRef TypedHCRLowering::BuildNotOnHeapTypedArrayLoadElement(GateRef receiver, GateRef offset, VariableType type)
{
    GateRef arrayBuffer =
        builder_.LoadConstOffset(VariableType::JS_POINTER(), receiver, JSTypedArray::VIEWED_ARRAY_BUFFER_OFFSET);

    GateRef data = builder_.Load(VariableType::JS_POINTER(), arrayBuffer, builder_.IntPtr(JSArrayBuffer::DATA_OFFSET));
    GateRef block = builder_.Load(VariableType::JS_ANY(), data, builder_.IntPtr(JSNativePointer::POINTER_OFFSET));
    GateRef byteOffset =
        builder_.Load(VariableType::INT32(), receiver, builder_.IntPtr(JSTypedArray::BYTE_OFFSET_OFFSET));
    GateRef result = builder_.Load(type, block, builder_.PtrAdd(offset, byteOffset));
    return result;
}

GateRef TypedHCRLowering::BuildTypedArrayLoadElement(GateRef receiver, GateRef offset, VariableType type,
                                                     Label *isByteArray, Label *isArrayBuffer, Label *exit)
{
    GateRef byteArrayOrArrayBuffer =
        builder_.LoadConstOffset(VariableType::JS_POINTER(), receiver, JSTypedArray::VIEWED_ARRAY_BUFFER_OFFSET);
    DEFVALUE(data, (&builder_), VariableType::JS_ANY(), builder_.Undefined());
    DEFVALUE(result, (&builder_), type, builder_.Double(0));

    GateRef isOnHeap = builder_.IsOnHeap(builder_.LoadHClass(receiver));
    BRANCH_CIR(isOnHeap, isByteArray, isArrayBuffer);
    builder_.Bind(isByteArray);
    {
        data = builder_.PtrAdd(byteArrayOrArrayBuffer, builder_.IntPtr(ByteArray::DATA_OFFSET));
        result = builder_.Load(type, *data, offset);
        builder_.Jump(exit);
    }
    builder_.Bind(isArrayBuffer);
    {
        data = builder_.Load(VariableType::JS_POINTER(), byteArrayOrArrayBuffer,
                             builder_.IntPtr(JSArrayBuffer::DATA_OFFSET));
        GateRef block = builder_.Load(VariableType::JS_ANY(), *data, builder_.IntPtr(JSNativePointer::POINTER_OFFSET));
        GateRef byteOffset =
            builder_.Load(VariableType::INT32(), receiver, builder_.IntPtr(JSTypedArray::BYTE_OFFSET_OFFSET));
        result = builder_.Load(type, block, builder_.PtrAdd(offset, byteOffset));
        builder_.Jump(exit);
    }
    builder_.Bind(exit);

    return *result;
}

void TypedHCRLowering::LowerStringLoadElement(GateRef gate)
{
    Environment env(gate, circuit_, &builder_);
    GateRef glue = acc_.GetGlueFromArgList();
    GateRef receiver = acc_.GetValueIn(gate, 0);
    GateRef index = acc_.GetValueIn(gate, 1);

    GateRef result = builder_.CallStub(glue, gate, CommonStubCSigns::GetSingleCharCodeByIndex,
                                       { glue, receiver, index });
    acc_.ReplaceGate(gate, builder_.GetState(), builder_.GetDepend(), result);
}

void TypedHCRLowering::LowerStoreElement(GateRef gate, GateRef glue)
{
    Environment env(gate, circuit_, &builder_);
    StoreElementAccessor accessor = acc_.GetStoreElementAccessor(gate);
    TypedStoreOp op = accessor.GetTypedStoreOp();
    switch (op) {
        case TypedStoreOp::ARRAY_STORE_ELEMENT:
        case TypedStoreOp::ARRAY_STORE_INT_ELEMENT:
        case TypedStoreOp::ARRAY_STORE_DOUBLE_ELEMENT:
            LowerArrayStoreElement(gate, glue, op);
            break;
        case TypedStoreOp::INT8ARRAY_STORE_ELEMENT:
            LowerTypedArrayStoreElement(gate, BuiltinTypeId::INT8_ARRAY);
            break;
        case TypedStoreOp::UINT8ARRAY_STORE_ELEMENT:
            LowerTypedArrayStoreElement(gate, BuiltinTypeId::UINT8_ARRAY);
            break;
        case TypedStoreOp::UINT8CLAMPEDARRAY_STORE_ELEMENT:
            LowerUInt8ClampedArrayStoreElement(gate);
            break;
        case TypedStoreOp::INT16ARRAY_STORE_ELEMENT:
            LowerTypedArrayStoreElement(gate, BuiltinTypeId::INT16_ARRAY);
            break;
        case TypedStoreOp::UINT16ARRAY_STORE_ELEMENT:
            LowerTypedArrayStoreElement(gate, BuiltinTypeId::UINT16_ARRAY);
            break;
        case TypedStoreOp::INT32ARRAY_STORE_ELEMENT:
            LowerTypedArrayStoreElement(gate, BuiltinTypeId::INT32_ARRAY);
            break;
        case TypedStoreOp::UINT32ARRAY_STORE_ELEMENT:
            LowerTypedArrayStoreElement(gate, BuiltinTypeId::UINT32_ARRAY);
            break;
        case TypedStoreOp::FLOAT32ARRAY_STORE_ELEMENT:
            LowerTypedArrayStoreElement(gate, BuiltinTypeId::FLOAT32_ARRAY);
            break;
        case TypedStoreOp::FLOAT64ARRAY_STORE_ELEMENT:
            LowerTypedArrayStoreElement(gate, BuiltinTypeId::FLOAT64_ARRAY);
            break;
        default:
            LOG_ECMA(FATAL) << "this branch is unreachable";
            UNREACHABLE();
    }
}

// for JSArray
void TypedHCRLowering::LowerArrayStoreElement(GateRef gate, GateRef glue, TypedStoreOp op)
{
    Environment env(gate, circuit_, &builder_);
    GateRef receiver = acc_.GetValueIn(gate, 0);  // 0: receiver
    GateRef index = acc_.GetValueIn(gate, 1);     // 1: index
    GateRef value = acc_.GetValueIn(gate, 2);     // 2: value

    GateRef element = builder_.LoadConstOffset(VariableType::JS_POINTER(), receiver, JSObject::ELEMENTS_OFFSET);
    // Because at retype stage, we have set output according to op
    // there is not need to consider about the convertion here.
    if (op == TypedStoreOp::ARRAY_STORE_INT_ELEMENT) {
        GateRef convertedValue = builder_.ZExtInt32ToInt64(value);
        builder_.SetValueToTaggedArray(VariableType::INT64(), glue, element, index, convertedValue);
    } else if (op == TypedStoreOp::ARRAY_STORE_DOUBLE_ELEMENT) {
        builder_.SetValueToTaggedArray(VariableType::FLOAT64(), glue, element, index, value);
    } else {
        builder_.SetValueToTaggedArray(VariableType::JS_ANY(), glue, element, index, value);
    }

    acc_.ReplaceGate(gate, builder_.GetState(), builder_.GetDepend(), Circuit::NullGate());
}

// for JSTypedArray
void TypedHCRLowering::LowerTypedArrayStoreElement(GateRef gate, BuiltinTypeId id)
{
    Environment env(gate, circuit_, &builder_);
    GateRef receiver = acc_.GetValueIn(gate, 0);
    GateRef index = acc_.GetValueIn(gate, 1);
    GateRef value = acc_.GetValueIn(gate, 2);

    GateRef elementSize = GetElementSize(id);
    GateRef offset = builder_.PtrMul(index, elementSize);
    switch (id) {
        case BuiltinTypeId::INT8_ARRAY:
        case BuiltinTypeId::UINT8_ARRAY:
            value = builder_.TruncInt32ToInt8(value);
            break;
        case BuiltinTypeId::INT16_ARRAY:
        case BuiltinTypeId::UINT16_ARRAY:
            value = builder_.TruncInt32ToInt16(value);
            break;
        case BuiltinTypeId::FLOAT32_ARRAY:
            value = builder_.TruncDoubleToFloat32(value);
            break;
        default:
            break;
    }

    Label isByteArray(&builder_);
    Label isArrayBuffer(&builder_);
    Label exit(&builder_);
    OptStoreElementByOnHeapMode(gate, receiver, offset, value, &isByteArray, &isArrayBuffer, &exit);
    acc_.ReplaceGate(gate, builder_.GetState(), builder_.GetDepend(), Circuit::NullGate());
}

void TypedHCRLowering::OptStoreElementByOnHeapMode(GateRef gate, GateRef receiver, GateRef offset, GateRef value,
                                                   Label *isByteArray, Label *isArrayBuffer, Label *exit)
{
    StoreElementAccessor accessor = acc_.GetStoreElementAccessor(gate);
    OnHeapMode onHeapMode = accessor.GetOnHeapMode();
    switch (onHeapMode) {
        case OnHeapMode::ON_HEAP: {
            BuildOnHeapTypedArrayStoreElement(receiver, offset, value);
            break;
        }
        case OnHeapMode::NOT_ON_HEAP: {
            BuildNotOnHeapTypedArrayStoreElement(receiver, offset, value);
            break;
        }
        default: {
            BuildTypedArrayStoreElement(receiver, offset, value, isByteArray, isArrayBuffer, exit);
            break;
        }
    }
}

void TypedHCRLowering::BuildOnHeapTypedArrayStoreElement(GateRef receiver, GateRef offset, GateRef value)
{
    GateRef byteArray = builder_.LoadConstOffset(VariableType::JS_POINTER(), receiver,
                                                 JSTypedArray::VIEWED_ARRAY_BUFFER_OFFSET);
    GateRef data = builder_.PtrAdd(byteArray, builder_.IntPtr(ByteArray::DATA_OFFSET));

    builder_.StoreMemory(MemoryType::ELEMENT_TYPE, VariableType::VOID(), data, offset, value);
}

void TypedHCRLowering::BuildNotOnHeapTypedArrayStoreElement(GateRef receiver, GateRef offset, GateRef value)
{
    GateRef arrayBuffer = builder_.LoadConstOffset(VariableType::JS_POINTER(), receiver,
                                                   JSTypedArray::VIEWED_ARRAY_BUFFER_OFFSET);
    GateRef data = builder_.Load(VariableType::JS_POINTER(), arrayBuffer, builder_.IntPtr(JSArrayBuffer::DATA_OFFSET));
    GateRef block = builder_.Load(VariableType::JS_ANY(), data, builder_.IntPtr(JSNativePointer::POINTER_OFFSET));
    GateRef byteOffset =
        builder_.Load(VariableType::INT32(), receiver, builder_.IntPtr(JSTypedArray::BYTE_OFFSET_OFFSET));
    builder_.StoreMemory(MemoryType::ELEMENT_TYPE, VariableType::VOID(), block,
                         builder_.PtrAdd(offset, byteOffset), value);
}

void TypedHCRLowering::BuildTypedArrayStoreElement(GateRef receiver, GateRef offset, GateRef value,
                                                   Label *isByteArray, Label *isArrayBuffer, Label *exit)
{
    GateRef byteArrayOrArrayBuffer = builder_.LoadConstOffset(VariableType::JS_POINTER(), receiver,
                                                              JSTypedArray::VIEWED_ARRAY_BUFFER_OFFSET);
    GateRef isOnHeap = builder_.IsOnHeap(builder_.LoadHClass(receiver));
    DEFVALUE(data, (&builder_), VariableType::JS_ANY(), builder_.Undefined());
    BRANCH_CIR(isOnHeap, isByteArray, isArrayBuffer);
    builder_.Bind(isByteArray);
    {
        data = builder_.PtrAdd(byteArrayOrArrayBuffer, builder_.IntPtr(ByteArray::DATA_OFFSET));
        builder_.StoreMemory(MemoryType::ELEMENT_TYPE, VariableType::VOID(), *data, offset, value);
        builder_.Jump(exit);
    }
    builder_.Bind(isArrayBuffer);
    {
        data = builder_.Load(VariableType::JS_POINTER(), byteArrayOrArrayBuffer,
                             builder_.IntPtr(JSArrayBuffer::DATA_OFFSET));
        GateRef block = builder_.Load(VariableType::JS_ANY(), *data, builder_.IntPtr(JSNativePointer::POINTER_OFFSET));
        GateRef byteOffset =
            builder_.Load(VariableType::INT32(), receiver, builder_.IntPtr(JSTypedArray::BYTE_OFFSET_OFFSET));
        builder_.StoreMemory(MemoryType::ELEMENT_TYPE, VariableType::VOID(), block,
                             builder_.PtrAdd(offset, byteOffset), value);
        builder_.Jump(exit);
    }
    builder_.Bind(exit);
}

// for UInt8ClampedArray
void TypedHCRLowering::LowerUInt8ClampedArrayStoreElement(GateRef gate)
{
    Environment env(gate, circuit_, &builder_);

    GateRef receiver = acc_.GetValueIn(gate, 0);
    GateRef index = acc_.GetValueIn(gate, 1);
    GateRef elementSize = builder_.Int32(sizeof(uint8_t));
    GateRef offset = builder_.PtrMul(index, elementSize);
    GateRef value = acc_.GetValueIn(gate, 2);

    DEFVALUE(result, (&builder_), VariableType::INT32(), value);
    GateRef topValue = builder_.Int32(static_cast<uint32_t>(UINT8_MAX));
    GateRef bottomValue = builder_.Int32(static_cast<uint32_t>(0));
    Label isOverFlow(&builder_);
    Label notOverFlow(&builder_);
    Label exit(&builder_);
    Label isByteArray(&builder_);
    Label isArrayBuffer(&builder_);
    Label quit(&builder_);
    BRANCH_CIR(builder_.Int32GreaterThan(value, topValue), &isOverFlow, &notOverFlow);
    builder_.Bind(&isOverFlow);
    {
        result = topValue;
        builder_.Jump(&exit);
    }
    builder_.Bind(&notOverFlow);
    {
        Label isUnderSpill(&builder_);
        BRANCH_CIR(builder_.Int32LessThan(value, bottomValue), &isUnderSpill, &exit);
        builder_.Bind(&isUnderSpill);
        {
            result = bottomValue;
            builder_.Jump(&exit);
        }
    }
    builder_.Bind(&exit);
    value = builder_.TruncInt32ToInt8(*result);
    OptStoreElementByOnHeapMode(gate, receiver, offset, value, &isByteArray, &isArrayBuffer, &quit);
    acc_.ReplaceGate(gate, builder_.GetState(), builder_.GetDepend(), Circuit::NullGate());
}

GateRef TypedHCRLowering::DoubleToTaggedDoublePtr(GateRef gate)
{
    return builder_.DoubleToTaggedDoublePtr(gate);
}

GateRef TypedHCRLowering::ChangeInt32ToFloat64(GateRef gate)
{
    return builder_.ChangeInt32ToFloat64(gate);
}

GateRef TypedHCRLowering::TruncDoubleToInt(GateRef gate)
{
    return builder_.TruncInt64ToInt32(builder_.TruncFloatToInt64(gate));
}

GateRef TypedHCRLowering::IntToTaggedIntPtr(GateRef x)
{
    GateRef val = builder_.SExtInt32ToInt64(x);
    return builder_.ToTaggedIntPtr(val);
}

void TypedHCRLowering::LowerTypedCallBuitin(GateRef gate)
{
    BuiltinLowering lowering(circuit_);
    lowering.LowerTypedCallBuitin(gate);
}

void TypedHCRLowering::LowerJSCallTargetTypeCheck(GateRef gate)
{
    Environment env(gate, circuit_, &builder_);
    ArgumentAccessor argAcc(circuit_);
    GateRef frameState = GetFrameState(gate);
    GateRef sharedConstPool = argAcc.GetFrameArgsIn(frameState, FrameArgIdx::SHARED_CONST_POOL);
    auto func = acc_.GetValueIn(gate, 0);
    auto methodIndex = acc_.GetValueIn(gate, 1);
    builder_.HeapObjectCheck(func, frameState);
    GateRef funcMethodTarget = builder_.GetMethodFromFunction(func);
    GateRef methodTarget = builder_.GetValueFromTaggedArray(sharedConstPool, methodIndex);
    GateRef check = builder_.Equal(funcMethodTarget, methodTarget);
    builder_.DeoptCheck(check, frameState, DeoptType::NOTJSCALLTGT2);
    acc_.ReplaceGate(gate, builder_.GetState(), builder_.GetDepend(), Circuit::NullGate());
}

void TypedHCRLowering::LowerJSFastCallTargetTypeCheck(GateRef gate)
{
    Environment env(gate, circuit_, &builder_);
    ArgumentAccessor argAcc(circuit_);
    GateRef frameState = GetFrameState(gate);
    GateRef sharedConstPool = argAcc.GetFrameArgsIn(frameState, FrameArgIdx::SHARED_CONST_POOL);
    auto func = acc_.GetValueIn(gate, 0);
    auto methodIndex = acc_.GetValueIn(gate, 1);
    builder_.HeapObjectCheck(func, frameState);
    GateRef funcMethodTarget = builder_.GetMethodFromFunction(func);
    GateRef methodTarget = builder_.GetValueFromTaggedArray(sharedConstPool, methodIndex);
    GateRef check = builder_.Equal(funcMethodTarget, methodTarget);
    builder_.DeoptCheck(check, frameState, DeoptType::NOTJSFASTCALLTGT1);
    acc_.ReplaceGate(gate, builder_.GetState(), builder_.GetDepend(), Circuit::NullGate());
}

void TypedHCRLowering::LowerJSCallThisTargetTypeCheck(GateRef gate)
{
    Environment env(gate, circuit_, &builder_);
    GateRef frameState = GetFrameState(gate);
    auto func = acc_.GetValueIn(gate, 0);
    builder_.HeapObjectCheck(func, frameState);
    GateRef methodId = builder_.GetMethodId(func);
    GateRef check = builder_.Equal(methodId, acc_.GetValueIn(gate, 1));
    builder_.DeoptCheck(check, frameState, DeoptType::NOTJSCALLTGT3);
    acc_.ReplaceGate(gate, builder_.GetState(), builder_.GetDepend(), Circuit::NullGate());
}

void TypedHCRLowering::LowerJSNoGCCallThisTargetTypeCheck(GateRef gate)
{
    Environment env(gate, circuit_, &builder_);
    GateRef frameState = GetFrameState(gate);
    auto func = acc_.GetValueIn(gate, 0);
    builder_.HeapObjectCheck(func, frameState);
    GateRef methodId = builder_.GetMethodId(func);
    GateRef check = builder_.Equal(methodId, acc_.GetValueIn(gate, 1));
    builder_.DeoptCheck(check, frameState, DeoptType::NOTJSCALLTGT4);
    acc_.ReplaceGate(gate, builder_.GetState(), builder_.GetDepend(), Circuit::NullGate());
}

void TypedHCRLowering::LowerJSFastCallThisTargetTypeCheck(GateRef gate)
{
    Environment env(gate, circuit_, &builder_);
    GateRef frameState = GetFrameState(gate);
    auto func = acc_.GetValueIn(gate, 0);
    builder_.HeapObjectCheck(func, frameState);
    GateRef methodId = builder_.GetMethodId(func);
    GateRef check = builder_.Equal(methodId, acc_.GetValueIn(gate, 1));
    builder_.DeoptCheck(check, frameState, DeoptType::NOTJSFASTCALLTGT2);
    acc_.ReplaceGate(gate, builder_.GetState(), builder_.GetDepend(), Circuit::NullGate());
}

void TypedHCRLowering::LowerJSNoGCFastCallThisTargetTypeCheck(GateRef gate)
{
    Environment env(gate, circuit_, &builder_);
    GateRef frameState = GetFrameState(gate);
    auto func = acc_.GetValueIn(gate, 0);
    builder_.HeapObjectCheck(func, frameState);
    GateRef methodId = builder_.GetMethodId(func);
    GateRef check = builder_.Equal(methodId, acc_.GetValueIn(gate, 1));
    builder_.DeoptCheck(check, frameState, DeoptType::NOTJSFASTCALLTGT3);
    acc_.ReplaceGate(gate, builder_.GetState(), builder_.GetDepend(), Circuit::NullGate());
}

void TypedHCRLowering::LowerJSNewObjRangeCallTargetCheck(GateRef gate)
{
    Environment env(gate, circuit_, &builder_);
    GateRef frameState = GetFrameState(gate);
    auto ctor = acc_.GetValueIn(gate, 0);
    builder_.HeapObjectCheck(ctor, frameState);
    GateRef isJsFunc = builder_.IsJSFunction(ctor);
    builder_.DeoptCheck(isJsFunc, frameState, DeoptType::NOTJSNEWCALLTGT);
    acc_.ReplaceGate(gate, builder_.GetState(), builder_.GetDepend(), Circuit::NullGate());
}

void TypedHCRLowering::LowerCallTargetCheck(GateRef gate)
{
    Environment env(gate, circuit_, &builder_);
    GateRef frameState = GetFrameState(gate);

    BuiltinLowering lowering(circuit_);
    GateRef funcheck = lowering.LowerCallTargetCheck(&env, gate);
    GateRef check = lowering.CheckPara(gate, funcheck);
    builder_.DeoptCheck(check, frameState, DeoptType::NOTCALLTGT1);

    acc_.ReplaceGate(gate, builder_.GetState(), builder_.GetDepend(), Circuit::NullGate());
}

void TypedHCRLowering::LowerJSInlineTargetTypeCheck(GateRef gate)
{
    Environment env(gate, circuit_, &builder_);
    GateRef frameState = GetFrameState(gate);
    auto func = acc_.GetValueIn(gate, 0);
    builder_.HeapObjectCheck(func, frameState);
    GateRef isJsFunc = builder_.IsJSFunction(func);
    GateRef GetMethodId = builder_.GetMethodId(func);
    GateRef check = builder_.BoolAnd(isJsFunc, builder_.Equal(GetMethodId, acc_.GetValueIn(gate, 1)));
    builder_.DeoptCheck(check, frameState, DeoptType::INLINEFAIL1);
    acc_.ReplaceGate(gate, builder_.GetState(), builder_.GetDepend(), Circuit::NullGate());
}

void TypedHCRLowering::LowerTypedNewAllocateThis(GateRef gate, GateRef glue)
{
    Environment env(gate, circuit_, &builder_);
    ArgumentAccessor argAcc(circuit_);
    GateRef ctor = acc_.GetValueIn(gate, 0); // 0: 1st argument
    GateRef ihclass = acc_.GetValueIn(gate, 1); // 1: 2nd argument
    GateRef size = acc_.GetValueIn(gate, 2); // 2: 3rd argument
    Label isBase(&builder_);
    Label exit(&builder_);
    DEFVALUE(thisObj, (&builder_), VariableType::JS_ANY(), builder_.Undefined());
    BRANCH_CIR(builder_.IsBase(ctor), &isBase, &exit);
    builder_.Bind(&isBase);
    NewObjectStubBuilder newBuilder(builder_.GetCurrentEnvironment());
    newBuilder.SetParameters(glue, 0);
    newBuilder.NewJSObject(&thisObj, &exit, ihclass, size);
    builder_.Bind(&exit);
    acc_.ReplaceGate(gate, builder_.GetState(), builder_.GetDepend(), *thisObj);
}

void TypedHCRLowering::LowerTypedSuperAllocateThis(GateRef gate, GateRef glue)
{
    Environment env(gate, circuit_, &builder_);
    GateRef superCtor = acc_.GetValueIn(gate, 0);
    GateRef newTarget = acc_.GetValueIn(gate, 1);

    DEFVALUE(thisObj, (&builder_), VariableType::JS_ANY(), builder_.Undefined());
    Label allocate(&builder_);
    Label exit(&builder_);

    GateRef isBase = builder_.IsBase(superCtor);
    BRANCH_CIR(isBase, &allocate, &exit);
    builder_.Bind(&allocate);
    {
        thisObj = builder_.CallStub(glue, gate, CommonStubCSigns::NewJSObject, { glue, newTarget });
        builder_.Jump(&exit);
    }
    builder_.Bind(&exit);
    acc_.ReplaceGate(gate, builder_.GetState(), builder_.GetDepend(), *thisObj);
}

void TypedHCRLowering::LowerGetSuperConstructor(GateRef gate)
{
    Environment env(gate, circuit_, &builder_);
    GateRef ctor = acc_.GetValueIn(gate, 0);
    GateRef hclass = builder_.LoadHClass(ctor);
    GateRef superCtor = builder_.LoadConstOffset(VariableType::JS_ANY(), hclass, JSHClass::PROTOTYPE_OFFSET);
    acc_.ReplaceGate(gate, builder_.GetState(), builder_.GetDepend(), superCtor);
}

VariableType TypedHCRLowering::GetVarType(PropertyLookupResult plr)
{
    if (plr.GetRepresentation() == Representation::DOUBLE) {
        return kungfu::VariableType::FLOAT64();
    } else if (plr.GetRepresentation() == Representation::INT) {
        return kungfu::VariableType::INT32();
    } else {
        return kungfu::VariableType::INT64();
    }
}

GateRef TypedHCRLowering::GetLengthFromSupers(GateRef supers)
{
    return builder_.LoadConstOffset(VariableType::INT32(), supers, TaggedArray::EXTRA_LENGTH_OFFSET);
}

GateRef TypedHCRLowering::GetValueFromSupers(GateRef supers, size_t index)
{
    GateRef val = builder_.LoadFromTaggedArray(supers, index);
    return builder_.LoadObjectFromWeakRef(val);
}

GateRef TypedHCRLowering::CallAccessor(GateRef glue, GateRef gate, GateRef function, GateRef receiver,
    AccessorMode mode, GateRef value)
{
    const CallSignature *cs = RuntimeStubCSigns::Get(RTSTUB_ID(JSCall));
    GateRef target = builder_.IntPtr(RTSTUB_ID(JSCall));
    GateRef newTarget = builder_.Undefined();
    GateRef argc = builder_.Int64(NUM_MANDATORY_JSFUNC_ARGS + (mode == AccessorMode::SETTER ? 1 : 0));  // 1: value
    GateRef argv = builder_.IntPtr(0);
    std::vector<GateRef> args { glue, argc, argv, function, newTarget, receiver };
    if (mode == AccessorMode::SETTER) {
        args.emplace_back(value);
    }

    return builder_.Call(cs, glue, target, builder_.GetDepend(), args, gate);
}

void TypedHCRLowering::ReplaceHirWithPendingException(GateRef hirGate, GateRef glue, GateRef state, GateRef depend,
                                                      GateRef value)
{
    auto condition = builder_.HasPendingException(glue);
    GateRef ifBranch = builder_.Branch(state, condition, 1, BranchWeight::DEOPT_WEIGHT, "checkException");
    GateRef ifTrue = builder_.IfTrue(ifBranch);
    GateRef ifFalse = builder_.IfFalse(ifBranch);
    GateRef eDepend = builder_.DependRelay(ifTrue, depend);
    GateRef sDepend = builder_.DependRelay(ifFalse, depend);

    StateDepend success(ifFalse, sDepend);
    StateDepend exception(ifTrue, eDepend);
    acc_.ReplaceHirWithIfBranch(hirGate, success, exception, value);
}

void TypedHCRLowering::LowerLookupHolder(GateRef gate)
{
    Environment env(gate, circuit_, &builder_);
    ASSERT(acc_.GetNumValueIn(gate) == 3);  // 3: receiver, holderHC, jsFunc
    GateRef receiver = acc_.GetValueIn(gate, 0);
    GateRef holderHCIndex = acc_.GetValueIn(gate, 1);
    GateRef unsharedConstPool = acc_.GetValueIn(gate, 2);  // 2: constpool
    GateRef holderHC = builder_.LoadHClassFromConstpool(unsharedConstPool,
        acc_.GetConstantValue(holderHCIndex));
    GateRef frameState = acc_.GetFrameState(gate);
    DEFVALUE(holder, (&builder_), VariableType::JS_ANY(), receiver);
    Label loopHead(&builder_);
    Label exit(&builder_);
    Label lookUpProto(&builder_);
    builder_.Jump(&loopHead);

    builder_.LoopBegin(&loopHead);
    builder_.DeoptCheck(builder_.TaggedIsNotNull(*holder), frameState, DeoptType::INCONSISTENTHCLASS13);
    auto curHC = builder_.LoadHClass(*holder);
    BRANCH_CIR(builder_.Equal(curHC, holderHC), &exit, &lookUpProto);

    builder_.Bind(&lookUpProto);
    holder = builder_.LoadConstOffset(VariableType::JS_ANY(), curHC, JSHClass::PROTOTYPE_OFFSET);
    builder_.LoopEnd(&loopHead);

    builder_.Bind(&exit);
    acc_.ReplaceGate(gate, builder_.GetState(), builder_.GetDepend(), *holder);
}

void TypedHCRLowering::LowerLoadGetter(GateRef gate)
{
    Environment env(gate, circuit_, &builder_);
    ASSERT(acc_.GetNumValueIn(gate) == 2);  // 2: holder, plr
    GateRef holder = acc_.GetValueIn(gate, 0);
    GateRef propertyLookupResult = acc_.GetValueIn(gate, 1);
    PropertyLookupResult plr(acc_.TryGetValue(propertyLookupResult));
    ASSERT(plr.IsAccessor());

    GateRef getter;
    if (plr.IsInlinedProps()) {
        auto acceessorData = builder_.LoadConstOffset(VariableType::JS_ANY(), holder, plr.GetOffset());
        getter = builder_.LoadConstOffset(VariableType::JS_ANY(), acceessorData, AccessorData::GETTER_OFFSET);
    } else {
        auto properties = builder_.LoadConstOffset(VariableType::JS_ANY(), holder, JSObject::PROPERTIES_OFFSET);
        auto acceessorData = builder_.GetValueFromTaggedArray(properties, builder_.Int32(plr.GetOffset()));
        getter = builder_.LoadConstOffset(VariableType::JS_ANY(), acceessorData, AccessorData::GETTER_OFFSET);
    }
    acc_.ReplaceGate(gate, builder_.GetState(), builder_.GetDepend(), getter);
}

void TypedHCRLowering::LowerLoadSetter(GateRef gate)
{
    Environment env(gate, circuit_, &builder_);
    ASSERT(acc_.GetNumValueIn(gate) == 2);  // 2: holder, plr
    GateRef holder = acc_.GetValueIn(gate, 0);
    GateRef propertyLookupResult = acc_.GetValueIn(gate, 1);
    PropertyLookupResult plr(acc_.TryGetValue(propertyLookupResult));
    ASSERT(plr.IsAccessor());

    GateRef setter;
    if (plr.IsInlinedProps()) {
        auto acceessorData = builder_.LoadConstOffset(VariableType::JS_ANY(), holder, plr.GetOffset());
        setter = builder_.LoadConstOffset(VariableType::JS_ANY(), acceessorData, AccessorData::SETTER_OFFSET);
    } else {
        auto properties = builder_.LoadConstOffset(VariableType::JS_ANY(), holder, JSObject::PROPERTIES_OFFSET);
        auto acceessorData = builder_.GetValueFromTaggedArray(properties, builder_.Int32(plr.GetOffset()));
        setter = builder_.LoadConstOffset(VariableType::JS_ANY(), acceessorData, AccessorData::SETTER_OFFSET);
    }
    acc_.ReplaceGate(gate, builder_.GetState(), builder_.GetDepend(), setter);
}

void TypedHCRLowering::LowerPrototypeCheck(GateRef gate)
{
    Environment env(gate, circuit_, &builder_);
    GateRef unsharedConstPool = acc_.GetValueIn(gate, 0);
    GateRef frameState = acc_.GetFrameState(gate);

    uint32_t hclassIndex = acc_.GetHClassIndex(gate);
    auto expectedReceiverHC = builder_.LoadHClassFromConstpool(unsharedConstPool, hclassIndex);

    auto prototype = builder_.LoadConstOffset(VariableType::JS_ANY(), expectedReceiverHC, JSHClass::PROTOTYPE_OFFSET);
    auto protoHClass = builder_.LoadHClass(prototype);
    auto marker = builder_.LoadConstOffset(VariableType::JS_ANY(), protoHClass, JSHClass::PROTO_CHANGE_MARKER_OFFSET);
    builder_.DeoptCheck(builder_.TaggedIsNotNull(marker), frameState, DeoptType::PROTOTYPECHANGED1);
    auto prototypeHasChanged = builder_.GetHasChanged(marker);
    auto accessorHasChanged = builder_.GetAccessorHasChanged(marker);
    auto check = builder_.BoolAnd(builder_.BoolNot(prototypeHasChanged), builder_.BoolNot(accessorHasChanged));

    builder_.DeoptCheck(check, frameState, DeoptType::INLINEFAIL2);
    acc_.ReplaceGate(gate, builder_.GetState(), builder_.GetDepend(), Circuit::NullGate());
    return;
}

void TypedHCRLowering::LowerStringEqual(GateRef gate, GateRef glue)
{
    Environment env(gate, circuit_, &builder_);
    GateRef left = acc_.GetValueIn(gate, 0);
    GateRef right = acc_.GetValueIn(gate, 1);
    GateRef leftLength = GetLengthFromString(left);
    GateRef rightLength = GetLengthFromString(right);

    DEFVALUE(result, (&builder_), VariableType::BOOL(), builder_.False());
    Label lenEqual(&builder_);
    Label exit(&builder_);
    BRANCH_CIR(builder_.Equal(leftLength, rightLength), &lenEqual, &exit);
    builder_.Bind(&lenEqual);
    {
        result = builder_.CallStub(glue, gate, CommonStubCSigns::FastStringEqual, { glue, left, right });
        builder_.Jump(&exit);
    }
    builder_.Bind(&exit);
    acc_.ReplaceGate(gate, builder_.GetState(), builder_.GetDepend(), *result);
}

void TypedHCRLowering::LowerStringAdd(GateRef gate, GateRef glue)
{
    Environment env(gate, circuit_, &builder_);
    GateRef left = acc_.GetValueIn(gate, 0);
    GateRef right = acc_.GetValueIn(gate, 1);
    GateRef leftLength = builder_.GetLengthFromString(left);

    Label isFirstConcat(&builder_);
    Label isNotFirstConcat(&builder_);
    Label leftEmpty(&builder_);
    Label leftNotEmpty(&builder_);
    Label slowPath(&builder_);
    Label exit(&builder_);

    DEFVALUE(lineString, (&builder_), VariableType::JS_POINTER(), builder_.Undefined());
    DEFVALUE(slicedString, (&builder_), VariableType::JS_POINTER(), builder_.Undefined());
    DEFVALUE(newLeft, (&builder_), VariableType::JS_POINTER(), builder_.Undefined());
    DEFVALUE(result, (&builder_), VariableType::JS_POINTER(), builder_.Undefined());

    BRANCH_CIR(builder_.Equal(leftLength, builder_.Int32(0)), &leftEmpty, &leftNotEmpty);
    builder_.Bind(&leftEmpty);
    {
        result = right;
        builder_.Jump(&exit);
    }
    builder_.Bind(&leftNotEmpty);
    {
        Label rightEmpty(&builder_);
        Label rightNotEmpty(&builder_);
        GateRef rightLength = builder_.GetLengthFromString(right);
        BRANCH_CIR(builder_.Equal(rightLength, builder_.Int32(0)), &rightEmpty, &rightNotEmpty);
        builder_.Bind(&rightEmpty);
        {
            result = left;
            builder_.Jump(&exit);
        }
        builder_.Bind(&rightNotEmpty);
        {
            Label stringConcatOpt(&builder_);
            GateRef newLength = builder_.Int32Add(leftLength, rightLength);
            BRANCH_CIR(builder_.Int32LessThan(newLength,
                builder_.Int32(SlicedString::MIN_SLICED_ECMASTRING_LENGTH)), &slowPath, &stringConcatOpt);
            builder_.Bind(&stringConcatOpt);
            {
                GateRef backStoreLength =
                    builder_.Int32Mul(newLength, builder_.Int32(LineEcmaString::INIT_LENGTH_TIMES));
                GateRef leftIsUtf8 = builder_.IsUtf8String(left);
                GateRef rightIsUtf8 = builder_.IsUtf8String(right);
                GateRef canBeCompressed = builder_.BoolAnd(leftIsUtf8, rightIsUtf8);
                GateRef isValidFirstOpt = builder_.Equal(leftIsUtf8, rightIsUtf8);
                GateRef isValidOpt = builder_.Equal(leftIsUtf8, rightIsUtf8);
                if (!IsFirstConcatInStringAdd(gate)) {
                    isValidFirstOpt = builder_.False();
                }
                if (!ConcatIsInStringAdd(gate)) {
                    isValidOpt = builder_.False();
                }

                BRANCH_CIR(builder_.IsSpecialSlicedString(left), &isNotFirstConcat, &isFirstConcat);
                builder_.Bind(&isFirstConcat);
                {
                    Label fastPath(&builder_);
                    Label canBeConcat(&builder_);
                    Label canBeCompress(&builder_);
                    Label canNotBeCompress(&builder_);
                    Label newSlicedStr(&builder_);
                    BRANCH_CIR(builder_.Int32LessThan(builder_.Int32(LineEcmaString::MAX_LENGTH),
                        backStoreLength), &slowPath, &fastPath);
                    builder_.Bind(&fastPath);
                    {
                        BRANCH_CIR(builder_.CanBeConcat(left, right, isValidFirstOpt),
                            &canBeConcat, &slowPath);
                        builder_.Bind(&canBeConcat);
                        {
                            lineString = AllocateLineString(glue, backStoreLength, canBeCompressed);
                            BRANCH_CIR(canBeCompressed, &canBeCompress, &canNotBeCompress);
                            builder_.Bind(&canBeCompress);
                            {
                                GateRef leftSource = builder_.GetStringDataFromLineOrConstantString(left);
                                GateRef rightSource = builder_.GetStringDataFromLineOrConstantString(right);
                                GateRef leftDst = builder_.TaggedPointerToInt64(
                                    builder_.PtrAdd(*lineString, builder_.IntPtr(LineEcmaString::DATA_OFFSET)));
                                GateRef rightDst = builder_.TaggedPointerToInt64(builder_.PtrAdd(leftDst,
                                    builder_.ZExtInt32ToPtr(leftLength)));
                                builder_.CopyChars(glue, leftDst, leftSource, leftLength,
                                                   builder_.IntPtr(sizeof(uint8_t)), VariableType::INT8());
                                builder_.CopyChars(glue, rightDst, rightSource, rightLength,
                                                   builder_.IntPtr(sizeof(uint8_t)), VariableType::INT8());
                                builder_.Jump(&newSlicedStr);
                            }
                            builder_.Bind(&canNotBeCompress);
                            {
                                Label leftIsUtf8L(&builder_);
                                Label leftIsUtf16L(&builder_);
                                Label rightIsUtf8L(&builder_);
                                Label rightIsUtf16L(&builder_);
                                GateRef leftSource = builder_.GetStringDataFromLineOrConstantString(left);
                                GateRef rightSource = builder_.GetStringDataFromLineOrConstantString(right);
                                GateRef leftDst = builder_.TaggedPointerToInt64(
                                    builder_.PtrAdd(*lineString, builder_.IntPtr(LineEcmaString::DATA_OFFSET)));
                                GateRef rightDst = builder_.TaggedPointerToInt64(builder_.PtrAdd(leftDst,
                                    builder_.PtrMul(builder_.ZExtInt32ToPtr(leftLength),
                                    builder_.IntPtr(sizeof(uint16_t)))));
                                BRANCH_CIR(leftIsUtf8, &leftIsUtf8L, &leftIsUtf16L);
                                builder_.Bind(&leftIsUtf8L);
                                {
                                    // left is utf8, right string must utf16
                                    builder_.CopyUtf8AsUtf16(glue, leftDst, leftSource, leftLength);
                                    builder_.CopyChars(glue, rightDst, rightSource, rightLength,
                                        builder_.IntPtr(sizeof(uint16_t)), VariableType::INT16());
                                    builder_.Jump(&newSlicedStr);
                                }
                                builder_.Bind(&leftIsUtf16L);
                                {
                                    builder_.CopyChars(glue, leftDst, leftSource, leftLength,
                                        builder_.IntPtr(sizeof(uint16_t)), VariableType::INT16());
                                    BRANCH_CIR(rightIsUtf8, &rightIsUtf8L, &rightIsUtf16L);
                                    builder_.Bind(&rightIsUtf8L);
                                    builder_.CopyUtf8AsUtf16(glue, rightDst, rightSource, rightLength);
                                    builder_.Jump(&newSlicedStr);
                                    builder_.Bind(&rightIsUtf16L);
                                    builder_.CopyChars(glue, rightDst, rightSource, rightLength,
                                        builder_.IntPtr(sizeof(uint16_t)), VariableType::INT16());
                                    builder_.Jump(&newSlicedStr);
                                }
                            }
                            builder_.Bind(&newSlicedStr);
                            slicedString = AllocateSlicedString(glue, *lineString, newLength, canBeCompressed);
                            result = *slicedString;
                            builder_.Jump(&exit);
                        }
                    }
                }
                builder_.Bind(&isNotFirstConcat);
                {
                    Label fastPath(&builder_);
                    BRANCH_CIR(builder_.CanBackStore(right, isValidOpt), &fastPath, &slowPath);
                    builder_.Bind(&fastPath);
                    {
                        // left string length means current length,
                        // max length means the field which was already initialized.
                        lineString = builder_.LoadConstOffset(VariableType::JS_POINTER(),
                                                              left, SlicedString::PARENT_OFFSET);
                        GateRef maxLength = builder_.GetLengthFromString(*lineString);
                        Label needsRealloc(&builder_);
                        Label backingStore(&builder_);
                        BRANCH_CIR(builder_.Int32LessThan(maxLength, newLength), &needsRealloc, &backingStore);
                        builder_.Bind(&needsRealloc);
                        {
                            Label newLineStr(&builder_);
                            Label canBeCompress(&builder_);
                            Label canNotBeCompress(&builder_);
                            // The new backing store will have a length of min(2*length, LineEcmaString::MAX_LENGTH).
                            GateRef newBackStoreLength = builder_.Int32Mul(newLength, builder_.Int32(2));
                            BRANCH_CIR(builder_.Int32LessThan(newBackStoreLength,
                                builder_.Int32(LineEcmaString::MAX_LENGTH)), &newLineStr, &slowPath);
                            builder_.Bind(&newLineStr);
                            {
                                BRANCH_CIR(canBeCompressed, &canBeCompress, &canNotBeCompress);
                                builder_.Bind(&canBeCompress);
                                {
                                    GateRef newBackingStore = AllocateLineString(glue, newBackStoreLength,
                                                                                 canBeCompressed);
                                    GateRef len = builder_.Int32LSL(newLength,
                                        builder_.Int32(EcmaString::STRING_LENGTH_SHIFT_COUNT));
                                    GateRef mixLength = builder_.Int32Or(len,
                                        builder_.Int32(EcmaString::STRING_COMPRESSED));
                                    GateRef leftSource = builder_.GetStringDataFromLineOrConstantString(*lineString);
                                    GateRef rightSource = builder_.GetStringDataFromLineOrConstantString(right);
                                    GateRef leftDst = builder_.TaggedPointerToInt64(
                                        builder_.PtrAdd(newBackingStore,
                                        builder_.IntPtr(LineEcmaString::DATA_OFFSET)));
                                    GateRef rightDst = builder_.TaggedPointerToInt64(
                                        builder_.PtrAdd(leftDst, builder_.ZExtInt32ToPtr(leftLength)));
                                    builder_.CopyChars(glue, leftDst, leftSource, leftLength,
                                        builder_.IntPtr(sizeof(uint8_t)), VariableType::INT8());
                                    builder_.CopyChars(glue, rightDst, rightSource, rightLength,
                                        builder_.IntPtr(sizeof(uint8_t)), VariableType::INT8());
                                    newLeft = left;
                                    builder_.StoreConstOffset(VariableType::JS_POINTER(), *newLeft,
                                        SlicedString::PARENT_OFFSET, newBackingStore);
                                    builder_.StoreConstOffset(VariableType::INT32(), *newLeft,
                                        EcmaString::MIX_LENGTH_OFFSET, mixLength);
                                    result = *newLeft;
                                    builder_.Jump(&exit);
                                }
                                builder_.Bind(&canNotBeCompress);
                                {
                                    GateRef newBackingStore = AllocateLineString(glue, newBackStoreLength,
                                                                                 canBeCompressed);
                                    GateRef len = builder_.Int32LSL(newLength,
                                        builder_.Int32(EcmaString::STRING_LENGTH_SHIFT_COUNT));
                                    GateRef mixLength = builder_.Int32Or(len,
                                        builder_.Int32(EcmaString::STRING_UNCOMPRESSED));
                                    GateRef leftSource = builder_.GetStringDataFromLineOrConstantString(*lineString);
                                    GateRef rightSource = builder_.GetStringDataFromLineOrConstantString(right);
                                    GateRef leftDst = builder_.TaggedPointerToInt64(
                                        builder_.PtrAdd(newBackingStore,
                                        builder_.IntPtr(LineEcmaString::DATA_OFFSET)));
                                    GateRef rightDst = builder_.TaggedPointerToInt64(builder_.PtrAdd(leftDst,
                                        builder_.PtrMul(builder_.ZExtInt32ToPtr(leftLength),
                                        builder_.IntPtr(sizeof(uint16_t)))));
                                    builder_.CopyChars(glue, leftDst, leftSource, leftLength,
                                        builder_.IntPtr(sizeof(uint16_t)), VariableType::INT16());
                                    builder_.CopyChars(glue, rightDst, rightSource, rightLength,
                                        builder_.IntPtr(sizeof(uint16_t)), VariableType::INT16());
                                    newLeft = left;
                                    builder_.StoreConstOffset(VariableType::JS_POINTER(), *newLeft,
                                        SlicedString::PARENT_OFFSET, newBackingStore);
                                    builder_.StoreConstOffset(VariableType::INT32(), *newLeft,
                                        EcmaString::MIX_LENGTH_OFFSET, mixLength);
                                    result = *newLeft;
                                    builder_.Jump(&exit);
                                }
                            }
                        }
                        builder_.Bind(&backingStore);
                        {
                            Label canBeCompress(&builder_);
                            Label canNotBeCompress(&builder_);
                            BRANCH_CIR(canBeCompressed, &canBeCompress, &canNotBeCompress);
                            builder_.Bind(&canBeCompress);
                            {
                                GateRef len = builder_.Int32LSL(newLength,
                                    builder_.Int32(EcmaString::STRING_LENGTH_SHIFT_COUNT));
                                GateRef mixLength = builder_.Int32Or(len,
                                    builder_.Int32(EcmaString::STRING_COMPRESSED));
                                GateRef rightSource = builder_.GetStringDataFromLineOrConstantString(right);
                                GateRef leftDst = builder_.TaggedPointerToInt64(
                                    builder_.PtrAdd(*lineString, builder_.IntPtr(LineEcmaString::DATA_OFFSET)));
                                GateRef rightDst = builder_.TaggedPointerToInt64(builder_.PtrAdd(leftDst,
                                    builder_.ZExtInt32ToPtr(leftLength)));
                                builder_.CopyChars(glue, rightDst, rightSource, rightLength,
                                    builder_.IntPtr(sizeof(uint8_t)), VariableType::INT8());
                                newLeft = left;
                                builder_.StoreConstOffset(VariableType::JS_POINTER(), *newLeft,
                                    SlicedString::PARENT_OFFSET, *lineString);
                                builder_.StoreConstOffset(VariableType::INT32(), *newLeft,
                                    EcmaString::MIX_LENGTH_OFFSET, mixLength);
                                result = *newLeft;
                                builder_.Jump(&exit);
                            }
                            builder_.Bind(&canNotBeCompress);
                            {
                                GateRef len = builder_.Int32LSL(newLength,
                                    builder_.Int32(EcmaString::STRING_LENGTH_SHIFT_COUNT));
                                GateRef mixLength = builder_.Int32Or(len,
                                    builder_.Int32(EcmaString::STRING_UNCOMPRESSED));
                                GateRef rightSource = builder_.GetStringDataFromLineOrConstantString(right);
                                GateRef leftDst = builder_.TaggedPointerToInt64(
                                    builder_.PtrAdd(*lineString, builder_.IntPtr(LineEcmaString::DATA_OFFSET)));
                                GateRef rightDst = builder_.TaggedPointerToInt64(builder_.PtrAdd(leftDst,
                                    builder_.PtrMul(builder_.ZExtInt32ToPtr(leftLength),
                                    builder_.IntPtr(sizeof(uint16_t)))));
                                builder_.CopyChars(glue, rightDst, rightSource, rightLength,
                                    builder_.IntPtr(sizeof(uint16_t)), VariableType::INT16());
                                newLeft = left;
                                builder_.StoreConstOffset(VariableType::JS_POINTER(), *newLeft,
                                    SlicedString::PARENT_OFFSET, *lineString);
                                builder_.StoreConstOffset(VariableType::INT32(), *newLeft,
                                    EcmaString::MIX_LENGTH_OFFSET, mixLength);
                                result = *newLeft;
                                builder_.Jump(&exit);
                            }
                        }
                    }
                }
                builder_.Bind(&slowPath);
                {
                    result = builder_.CallStub(glue, gate, CommonStubCSigns::FastStringAdd, { glue, left, right });
                    builder_.Jump(&exit);
                }
            }
        }
    }
    builder_.Bind(&exit);
    acc_.ReplaceGate(gate, builder_.GetState(), builder_.GetDepend(), *result);
}

GateRef TypedHCRLowering::AllocateLineString(GateRef glue, GateRef length, GateRef canBeCompressed)
{
    Label subentry(&builder_);
    builder_.SubCfgEntry(&subentry);
    Label isUtf8(&builder_);
    Label isUtf16(&builder_);
    Label exit(&builder_);
    DEFVALUE(mixLength, (&builder_), VariableType::INT32(), builder_.Int32(0));
    DEFVALUE(size, (&builder_), VariableType::INT64(), builder_.Int64(0));

    GateRef len = builder_.Int32LSL(length, builder_.Int32(EcmaString::STRING_LENGTH_SHIFT_COUNT));

    BRANCH_CIR(canBeCompressed, &isUtf8, &isUtf16);
    builder_.Bind(&isUtf8);
    {
        size = builder_.AlignUp(builder_.ComputeSizeUtf8(builder_.ZExtInt32ToPtr(length)),
            builder_.IntPtr(static_cast<size_t>(MemAlignment::MEM_ALIGN_OBJECT)));
        mixLength = builder_.Int32Or(len, builder_.Int32(EcmaString::STRING_COMPRESSED));
        builder_.Jump(&exit);
    }
    builder_.Bind(&isUtf16);
    {
        size = builder_.AlignUp(builder_.ComputeSizeUtf16(builder_.ZExtInt32ToPtr(length)),
            builder_.IntPtr(static_cast<size_t>(MemAlignment::MEM_ALIGN_OBJECT)));
        mixLength = builder_.Int32Or(len, builder_.Int32(EcmaString::STRING_UNCOMPRESSED));
        builder_.Jump(&exit);
    }
    builder_.Bind(&exit);

    GateRef stringClass = builder_.GetGlobalConstantValue(ConstantIndex::LINE_STRING_CLASS_INDEX);

    builder_.StartAllocate();
    GateRef lineString =
        builder_.HeapAlloc(glue, *size, GateType::TaggedValue(), RegionSpaceFlag::IN_SHARED_OLD_SPACE);
    builder_.StoreConstOffset(VariableType::JS_POINTER(), lineString, 0, stringClass,
                              MemoryAttribute::NeedBarrierAndAtomic());
    builder_.StoreConstOffset(VariableType::INT32(), lineString, EcmaString::MIX_LENGTH_OFFSET, *mixLength);
    builder_.StoreConstOffset(VariableType::INT32(), lineString, EcmaString::MIX_HASHCODE_OFFSET, builder_.Int32(0));
    auto ret = builder_.FinishAllocate(lineString);
    builder_.SubCfgExit();
    return ret;
}

GateRef TypedHCRLowering::AllocateSlicedString(GateRef glue, GateRef flatString, GateRef length,
                                               GateRef canBeCompressed)
{
    Label subentry(&builder_);
    builder_.SubCfgEntry(&subentry);
    Label isUtf8(&builder_);
    Label isUtf16(&builder_);
    Label exit(&builder_);
    DEFVALUE(mixLength, (&builder_), VariableType::INT32(), builder_.Int32(0));

    GateRef len = builder_.Int32LSL(length, builder_.Int32(EcmaString::STRING_LENGTH_SHIFT_COUNT));

    BRANCH_CIR(canBeCompressed, &isUtf8, &isUtf16);
    builder_.Bind(&isUtf8);
    {
        mixLength = builder_.Int32Or(len, builder_.Int32(EcmaString::STRING_COMPRESSED));
        builder_.Jump(&exit);
    }
    builder_.Bind(&isUtf16);
    {
        mixLength = builder_.Int32Or(len, builder_.Int32(EcmaString::STRING_UNCOMPRESSED));
        builder_.Jump(&exit);
    }
    builder_.Bind(&exit);

    GateRef stringClass = builder_.GetGlobalConstantValue(ConstantIndex::SLICED_STRING_CLASS_INDEX);
    GateRef size = builder_.IntPtr(AlignUp(SlicedString::SIZE, static_cast<size_t>(MemAlignment::MEM_ALIGN_OBJECT)));

    builder_.StartAllocate();
    GateRef slicedString = builder_.HeapAlloc(glue, size, GateType::TaggedValue(),
        RegionSpaceFlag::IN_SHARED_OLD_SPACE);
    builder_.StoreConstOffset(VariableType::JS_POINTER(), slicedString, 0, stringClass,
                              MemoryAttribute::NeedBarrierAndAtomic());
    builder_.StoreConstOffset(VariableType::INT32(), slicedString, EcmaString::MIX_LENGTH_OFFSET, *mixLength);
    builder_.StoreConstOffset(VariableType::INT32(), slicedString, EcmaString::MIX_HASHCODE_OFFSET, builder_.Int32(0));
    builder_.StoreConstOffset(VariableType::JS_POINTER(), slicedString, SlicedString::PARENT_OFFSET, flatString);
    builder_.StoreConstOffset(VariableType::INT32(), slicedString, SlicedString::STARTINDEX_OFFSET, builder_.Int32(0));
    builder_.StoreConstOffset(VariableType::INT32(), slicedString,
        SlicedString::BACKING_STORE_FLAG, builder_.Int32(EcmaString::HAS_BACKING_STORE));
    auto ret = builder_.FinishAllocate(slicedString);
    builder_.SubCfgExit();
    return ret;
}

bool TypedHCRLowering::IsFirstConcatInStringAdd(GateRef gate) const
{
    if (!ConcatIsInStringAdd(gate)) {
        return false;
    }
    auto status = acc_.GetStringStatus(gate);
    return status == EcmaString::BEGIN_STRING_ADD || status == EcmaString::IN_STRING_ADD;
}

bool TypedHCRLowering::ConcatIsInStringAdd(GateRef gate) const
{
    auto status = acc_.GetStringStatus(gate);
    return status == EcmaString::CONFIRMED_IN_STRING_ADD ||
           status == EcmaString::IN_STRING_ADD ||
           status == EcmaString::BEGIN_STRING_ADD;
}

void TypedHCRLowering::LowerTypeOfCheck(GateRef gate)
{
    Environment env(gate, circuit_, &builder_);
    GateRef frameState = GetFrameState(gate);
    GateRef value = acc_.GetValueIn(gate, 0);
    GateTypeAccessor accessor(acc_.TryGetValue(gate));
    ParamType type = accessor.GetParamType();
    GateRef check = Circuit::NullGate();
    if (type.IsNumberType()) {
        check = builder_.TaggedIsNumber(value);
    } else if (type.IsBooleanType()) {
        check = builder_.TaggedIsBoolean(value);
    } else if (type.IsNullType()) {
        check = builder_.TaggedIsNull(value);
    } else if (type.IsUndefinedType()) {
        check = builder_.TaggedIsUndefined(value);
    } else {
        // NOTICE-PGO: wx add support for builtin(Function Object ArrayKind)
        builder_.DeoptCheck(builder_.TaggedIsHeapObject(value), frameState, DeoptType::INCONSISTENTTYPE1);
        if (type.IsStringType()) {
            check = builder_.TaggedIsString(value);
        } else if (type.IsBigIntType()) {
            check = builder_.IsJsType(value, JSType::BIGINT);
        } else if (type.IsSymbolType()) {
            check = builder_.IsJsType(value, JSType::SYMBOL);
        } else {
            UNREACHABLE();
        }
    }

    builder_.DeoptCheck(check, frameState, DeoptType::INCONSISTENTTYPE1);
    acc_.ReplaceGate(gate, builder_.GetState(), builder_.GetDepend(), Circuit::NullGate());
}

void TypedHCRLowering::LowerTypeOf(GateRef gate, GateRef glue)
{
    Environment env(gate, circuit_, &builder_);
    GateTypeAccessor accessor(acc_.TryGetValue(gate));
    ParamType type = accessor.GetParamType();
    GateRef gConstAddr = builder_.Load(VariableType::JS_POINTER(), glue,
        builder_.IntPtr(JSThread::GlueData::GetGlobalConstOffset(builder_.GetCompilationConfig()->Is32Bit())));
    ConstantIndex index;
    // NOTICE-PGO: wx add support for builtin(Function Object ArrayKind)
    if (type.IsNumberType()) {
        index = ConstantIndex::NUMBER_STRING_INDEX;
    } else if (type.IsBooleanType()) {
        index = ConstantIndex::BOOLEAN_STRING_INDEX;
    } else if (type.IsNullType()) {
        index = ConstantIndex::OBJECT_STRING_INDEX;
    } else if (type.IsUndefinedType()) {
        index = ConstantIndex::UNDEFINED_STRING_INDEX;
    } else if (type.IsStringType()) {
        index = ConstantIndex::STRING_STRING_INDEX;
    } else if (type.IsBigIntType()) {
        index = ConstantIndex::BIGINT_STRING_INDEX;
    } else if (type.IsSymbolType()) {
        index = ConstantIndex::SYMBOL_STRING_INDEX;
    } else {
        UNREACHABLE();
    }

    GateRef result = builder_.Load(VariableType::JS_POINTER(), gConstAddr, builder_.GetGlobalConstantOffset(index));
    acc_.ReplaceGate(gate, builder_.GetState(), builder_.GetDepend(), result);
}

void TypedHCRLowering::LowerArrayConstructorCheck(GateRef gate, GateRef glue)
{
    Environment env(gate, circuit_, &builder_);
    GateRef frameState = GetFrameState(gate);
    GateRef newTarget = acc_.GetValueIn(gate, 0);
    Label isHeapObject(&builder_);
    Label exit(&builder_);
    DEFVALUE(check, (&builder_), VariableType::BOOL(), builder_.True());
    check = builder_.TaggedIsHeapObject(newTarget);
    BRANCH_CIR(*check, &isHeapObject, &exit);
    builder_.Bind(&isHeapObject);
    {
        Label isJSFunction(&builder_);
        check = builder_.IsJSFunction(newTarget);
        BRANCH_CIR(*check, &isJSFunction, &exit);
        builder_.Bind(&isJSFunction);
        {
            Label getHclass(&builder_);
            GateRef glueGlobalEnvOffset = builder_.IntPtr(
                JSThread::GlueData::GetGlueGlobalEnvOffset(builder_.GetCurrentEnvironment()->Is32Bit()));
            GateRef glueGlobalEnv = builder_.Load(VariableType::NATIVE_POINTER(), glue, glueGlobalEnvOffset);
            GateRef arrayFunc =
                builder_.GetGlobalEnvValue(VariableType::JS_ANY(), glueGlobalEnv, GlobalEnv::ARRAY_FUNCTION_INDEX);
            check = builder_.Equal(arrayFunc, newTarget);
            BRANCH_CIR(*check, &getHclass, &exit);
            builder_.Bind(&getHclass);
            {
                GateRef intialHClass = builder_.Load(VariableType::JS_ANY(), newTarget,
                                                     builder_.IntPtr(JSFunction::PROTO_OR_DYNCLASS_OFFSET));
                check = builder_.IsJSHClass(intialHClass);
                builder_.Jump(&exit);
            }
        }
    }
    builder_.Bind(&exit);
    builder_.DeoptCheck(*check, frameState, DeoptType::NEWBUILTINCTORARRAY);
    acc_.ReplaceGate(gate, builder_.GetState(), builder_.GetDepend(), Circuit::NullGate());
}

void TypedHCRLowering::LowerArrayConstructor(GateRef gate, GateRef glue)
{
    Environment env(gate, circuit_, &builder_);
    if (acc_.GetNumValueIn(gate) == 1) {
        NewArrayConstructorWithNoArgs(gate, glue);
        return;
    }
    ASSERT(acc_.GetNumValueIn(gate) == 2); // 2: new target and arg0
    DEFVALUE(res, (&builder_), VariableType::JS_ANY(), builder_.Undefined());
    Label slowPath(&builder_);
    Label exit(&builder_);
    GateRef newTarget = acc_.GetValueIn(gate, 0);
    GateRef arg0 = acc_.GetValueIn(gate, 1);
    GateRef intialHClass =
        builder_.Load(VariableType::JS_ANY(), newTarget, builder_.IntPtr(JSFunction::PROTO_OR_DYNCLASS_OFFSET));
    DEFVALUE(arrayLength, (&builder_), VariableType::INT64(), builder_.Int64(0));
    Label argIsNumber(&builder_);
    Label arrayCreate(&builder_);
    BRANCH_CIR(builder_.TaggedIsNumber(arg0), &argIsNumber, &slowPath);
    builder_.Bind(&argIsNumber);
    {
        Label argIsInt(&builder_);
        Label argIsDouble(&builder_);
        BRANCH_CIR(builder_.TaggedIsInt(arg0), &argIsInt, &argIsDouble);
        builder_.Bind(&argIsInt);
        {
            Label validIntLength(&builder_);
            GateRef intLen = builder_.GetInt64OfTInt(arg0);
            GateRef isGEZero = builder_.Int64GreaterThanOrEqual(intLen, builder_.Int64(0));
            GateRef isLEMaxLen = builder_.Int64LessThanOrEqual(intLen, builder_.Int64(JSArray::MAX_ARRAY_INDEX));
            BRANCH_CIR(builder_.BoolAnd(isGEZero, isLEMaxLen), &validIntLength, &slowPath);
            builder_.Bind(&validIntLength);
            {
                arrayLength = intLen;
                builder_.Jump(&arrayCreate);
            }
        }
        builder_.Bind(&argIsDouble);
        {
            Label validDoubleLength(&builder_);
            Label GetDoubleToIntValue(&builder_);
            GateRef doubleLength = builder_.GetDoubleOfTDouble(arg0);
            GateRef doubleToInt = builder_.DoubleToInt(doubleLength, &GetDoubleToIntValue);
            GateRef intToDouble = builder_.CastInt64ToFloat64(builder_.SExtInt32ToInt64(doubleToInt));
            GateRef doubleEqual = builder_.DoubleEqual(doubleLength, intToDouble);
            GateRef doubleLEMaxLen =
                builder_.DoubleLessThanOrEqual(doubleLength, builder_.Double(JSArray::MAX_ARRAY_INDEX));
            BRANCH_CIR(builder_.BoolAnd(doubleEqual, doubleLEMaxLen), &validDoubleLength, &slowPath);
            builder_.Bind(&validDoubleLength);
            {
                arrayLength = builder_.SExtInt32ToInt64(doubleToInt);
                builder_.Jump(&arrayCreate);
            }
        }
    }
    builder_.Bind(&arrayCreate);
    {
        Label lengthValid(&builder_);
        BRANCH_CIR(
            builder_.Int64GreaterThan(*arrayLength, builder_.Int64(JSObject::MAX_GAP)), &slowPath, &lengthValid);
        builder_.Bind(&lengthValid);
        {
            NewObjectStubBuilder newBuilder(builder_.GetCurrentEnvironment());
            newBuilder.SetParameters(glue, 0);
            res = newBuilder.NewJSArrayWithSize(intialHClass, *arrayLength);
            GateRef lengthOffset = builder_.IntPtr(JSArray::LENGTH_OFFSET);
            builder_.Store(VariableType::INT32(), glue, *res, lengthOffset, builder_.TruncInt64ToInt32(*arrayLength));
            GateRef accessor = builder_.GetGlobalConstantValue(ConstantIndex::ARRAY_LENGTH_ACCESSOR);
            builder_.SetPropertyInlinedProps(glue, *res, intialHClass, accessor,
                builder_.Int32(JSArray::LENGTH_INLINE_PROPERTY_INDEX), VariableType::JS_ANY());
            builder_.SetExtensibleToBitfield(glue, *res, true);
            builder_.Jump(&exit);
        }
    }
    builder_.Bind(&slowPath);
    {
        size_t range = acc_.GetNumValueIn(gate);
        std::vector<GateRef> args(range);
        for (size_t i = 0; i < range; ++i) {
            args[i] = acc_.GetValueIn(gate, i);
        }
        res = LowerCallRuntime(glue, gate, RTSTUB_ID(OptNewObjRange), args, true);
        builder_.Jump(&exit);
    }
    builder_.Bind(&exit);
    ReplaceGateWithPendingException(glue, gate, builder_.GetState(), builder_.GetDepend(), *res);
}

void TypedHCRLowering::LowerFloat32ArrayConstructorCheck(GateRef gate, GateRef glue)
{
    Environment env(gate, circuit_, &builder_);
    GateRef frameState = GetFrameState(gate);
    GateRef newTarget = acc_.GetValueIn(gate, 0);
    GateRef glueGlobalEnvOffset = builder_.IntPtr(
        JSThread::GlueData::GetGlueGlobalEnvOffset(builder_.GetCurrentEnvironment()->Is32Bit()));
    GateRef glueGlobalEnv = builder_.Load(VariableType::NATIVE_POINTER(), glue, glueGlobalEnvOffset);
    GateRef arrayFunc =
        builder_.GetGlobalEnvValue(VariableType::JS_ANY(), glueGlobalEnv, GlobalEnv::FLOAT32_ARRAY_FUNCTION_INDEX);
    GateRef check = builder_.Equal(arrayFunc, newTarget);
    builder_.DeoptCheck(check, frameState, DeoptType::NEWBUILTINCTORFLOAT32ARRAY);
    acc_.ReplaceGate(gate, builder_.GetState(), builder_.GetDepend(), Circuit::NullGate());
}

void TypedHCRLowering::NewFloat32ArrayConstructorWithNoArgs(GateRef gate, GateRef glue)
{
    NewObjectStubBuilder newBuilder(builder_.GetCurrentEnvironment());
    newBuilder.SetParameters(glue, 0);
    GateRef res = newBuilder.NewFloat32ArrayWithSize(glue, builder_.Int32(0));
    ReplaceGateWithPendingException(glue, gate, builder_.GetState(), builder_.GetDepend(), res);
}

void TypedHCRLowering::ConvertFloat32ArrayConstructorLength(GateRef len, Variable *arrayLength,
                                                            Label *arrayCreate, Label *slowPath)
{
    Label argIsNumber(&builder_);
    BRANCH_CIR(builder_.TaggedIsNumber(len), &argIsNumber, slowPath);
    builder_.Bind(&argIsNumber);
    {
        Label argIsInt(&builder_);
        Label argIsDouble(&builder_);
        BRANCH_CIR(builder_.TaggedIsInt(len), &argIsInt, &argIsDouble);
        builder_.Bind(&argIsInt);
        {
            Label validIntLength(&builder_);
            GateRef intLen = builder_.GetInt64OfTInt(len);
            GateRef isGEZero = builder_.Int64GreaterThanOrEqual(intLen, builder_.Int64(0));
            GateRef isLEMaxLen = builder_.Int64LessThanOrEqual(intLen, builder_.Int64(JSObject::MAX_GAP));
            BRANCH_CIR(builder_.BoolAnd(isGEZero, isLEMaxLen), &validIntLength, slowPath);
            builder_.Bind(&validIntLength);
            {
                *arrayLength = intLen;
                builder_.Jump(arrayCreate);
            }
        }
        builder_.Bind(&argIsDouble);
        {
            Label validDoubleLength(&builder_);
            Label GetDoubleToIntValue(&builder_);
            GateRef doubleLength = builder_.GetDoubleOfTDouble(len);
            GateRef doubleToInt = builder_.DoubleToInt(doubleLength, &GetDoubleToIntValue);
            GateRef intToDouble = builder_.CastInt64ToFloat64(builder_.SExtInt32ToInt64(doubleToInt));
            GateRef doubleEqual = builder_.DoubleEqual(doubleLength, intToDouble);
            GateRef doubleLEMaxLen =
                builder_.DoubleLessThanOrEqual(doubleLength, builder_.Double(JSObject::MAX_GAP));
            BRANCH_CIR(builder_.BoolAnd(doubleEqual, doubleLEMaxLen), &validDoubleLength, slowPath);
            builder_.Bind(&validDoubleLength);
            {
                *arrayLength = builder_.SExtInt32ToInt64(doubleToInt);
                builder_.Jump(arrayCreate);
            }
        }
    }
}

void TypedHCRLowering::LowerFloat32ArrayConstructor(GateRef gate, GateRef glue)
{
    Environment env(gate, circuit_, &builder_);
    if (acc_.GetNumValueIn(gate) == 1) {
        NewFloat32ArrayConstructorWithNoArgs(gate, glue);
        return;
    }
    ASSERT(acc_.GetNumValueIn(gate) == 2); // 2: new target and arg0
    DEFVALUE(res, (&builder_), VariableType::JS_ANY(), builder_.Undefined());
    Label slowPath(&builder_);
    Label exit(&builder_);
    GateRef arg0 = acc_.GetValueIn(gate, 1);
    DEFVALUE(arrayLength, (&builder_), VariableType::INT64(), builder_.Int64(0));
    Label arrayCreate(&builder_);
    ConvertFloat32ArrayConstructorLength(arg0, &arrayLength, &arrayCreate, &slowPath);
    builder_.Bind(&arrayCreate);
    {
        GateRef truncedLength = builder_.TruncInt64ToInt32(*arrayLength);
        NewObjectStubBuilder newBuilder(builder_.GetCurrentEnvironment());
        newBuilder.SetParameters(glue, 0);
        res = newBuilder.NewFloat32ArrayWithSize(glue, truncedLength);
        builder_.Jump(&exit);
    }
    builder_.Bind(&slowPath);
    {
        size_t range = acc_.GetNumValueIn(gate);
        std::vector<GateRef> args(range);
        for (size_t i = 0; i < range; ++i) {
            args[i] = acc_.GetValueIn(gate, i);
        }
        res = LowerCallRuntime(glue, gate, RTSTUB_ID(OptNewObjRange), args, true);
        builder_.Jump(&exit);
    }
    builder_.Bind(&exit);
    ReplaceGateWithPendingException(glue, gate, builder_.GetState(), builder_.GetDepend(), *res);
}

void TypedHCRLowering::NewArrayConstructorWithNoArgs(GateRef gate, GateRef glue)
{
    GateRef newTarget = acc_.GetValueIn(gate, 0);
    GateRef intialHClass =
        builder_.Load(VariableType::JS_ANY(), newTarget, builder_.IntPtr(JSFunction::PROTO_OR_DYNCLASS_OFFSET));
    GateRef arrayLength = builder_.Int64(0);
    NewObjectStubBuilder newBuilder(builder_.GetCurrentEnvironment());
    newBuilder.SetParameters(glue, 0);
    GateRef res = newBuilder.NewJSArrayWithSize(intialHClass, arrayLength);
    GateRef lengthOffset = builder_.IntPtr(JSArray::LENGTH_OFFSET);
    builder_.Store(VariableType::INT32(), glue, res, lengthOffset, builder_.TruncInt64ToInt32(arrayLength));
    GateRef accessor = builder_.GetGlobalConstantValue(ConstantIndex::ARRAY_LENGTH_ACCESSOR);
    builder_.SetPropertyInlinedProps(glue, res, intialHClass, accessor,
                                     builder_.Int32(JSArray::LENGTH_INLINE_PROPERTY_INDEX), VariableType::JS_ANY());
    builder_.SetExtensibleToBitfield(glue, res, true);
    ReplaceGateWithPendingException(glue, gate, builder_.GetState(), builder_.GetDepend(), res);
}

void TypedHCRLowering::LowerObjectConstructorCheck(GateRef gate, GateRef glue)
{
    Environment env(gate, circuit_, &builder_);
    GateRef frameState = GetFrameState(gate);
    GateRef newTarget = acc_.GetValueIn(gate, 0);
    Label isHeapObject(&builder_);
    Label exit(&builder_);
    DEFVALUE(check, (&builder_), VariableType::BOOL(), builder_.True());
    check = builder_.TaggedIsHeapObject(newTarget);
    BRANCH_CIR(*check, &isHeapObject, &exit);
    builder_.Bind(&isHeapObject);
    {
        Label isJSFunction(&builder_);
        check = builder_.IsJSFunction(newTarget);
        BRANCH_CIR(*check, &isJSFunction, &exit);
        builder_.Bind(&isJSFunction);
        {
            Label getHclass(&builder_);
            GateRef glueGlobalEnvOffset = builder_.IntPtr(
                JSThread::GlueData::GetGlueGlobalEnvOffset(builder_.GetCurrentEnvironment()->Is32Bit()));
            GateRef glueGlobalEnv = builder_.Load(VariableType::NATIVE_POINTER(), glue, glueGlobalEnvOffset);
            GateRef targetFunc =
                builder_.GetGlobalEnvValue(VariableType::JS_ANY(), glueGlobalEnv, GlobalEnv::OBJECT_FUNCTION_INDEX);
            check = builder_.Equal(targetFunc, newTarget);
            BRANCH_CIR(*check, &getHclass, &exit);
            builder_.Bind(&getHclass);
            {
                GateRef intialHClass = builder_.Load(VariableType::JS_ANY(), newTarget,
                                                     builder_.IntPtr(JSFunction::PROTO_OR_DYNCLASS_OFFSET));
                check = builder_.IsJSHClass(intialHClass);
                builder_.Jump(&exit);
            }
        }
    }
    builder_.Bind(&exit);
    builder_.DeoptCheck(*check, frameState, DeoptType::NEWBUILTINCTOROBJECT);
    acc_.ReplaceGate(gate, builder_.GetState(), builder_.GetDepend(), Circuit::NullGate());
}

void TypedHCRLowering::LowerObjectConstructor(GateRef gate, GateRef glue)
{
    Environment env(gate, circuit_, &builder_);
    GateRef value = builder_.Undefined();
    ASSERT(acc_.GetNumValueIn(gate) <= 2); // 2: new target and arg0
    if (acc_.GetNumValueIn(gate) > 1) {
        value = acc_.GetValueIn(gate, 1);
    }
    DEFVALUE(res, (&builder_), VariableType::JS_ANY(), builder_.Undefined());
    Label slowPath(&builder_);
    Label exit(&builder_);

    Label isHeapObj(&builder_);
    Label notHeapObj(&builder_);
    BRANCH_CIR(builder_.TaggedIsHeapObject(value), &isHeapObj, &notHeapObj);
    builder_.Bind(&isHeapObj);
    {
        Label isEcmaObj(&builder_);
        Label notEcmaObj(&builder_);
        BRANCH_CIR(builder_.TaggedObjectIsEcmaObject(value), &isEcmaObj, &notEcmaObj);
        builder_.Bind(&isEcmaObj);
        {
            res = value;
            builder_.Jump(&exit);
        }
        builder_.Bind(&notEcmaObj);
        {
            Label isSymbol(&builder_);
            Label notSymbol(&builder_);
            BRANCH_CIR(builder_.TaggedIsSymbol(value), &isSymbol, &notSymbol);
            builder_.Bind(&isSymbol);
            {
                res = NewJSPrimitiveRef(PrimitiveType::PRIMITIVE_SYMBOL, glue, value);
                builder_.Jump(&exit);
            }
            builder_.Bind(&notSymbol);
            {
                Label isBigInt(&builder_);
                BRANCH_CIR(builder_.TaggedIsBigInt(value), &isBigInt, &slowPath);
                builder_.Bind(&isBigInt);
                {
                    res = NewJSPrimitiveRef(PrimitiveType::PRIMITIVE_BIGINT, glue, value);
                    builder_.Jump(&exit);
                }
            }
        }
    }
    builder_.Bind(&notHeapObj);
    {
        Label isNumber(&builder_);
        Label notNumber(&builder_);
        BRANCH_CIR(builder_.TaggedIsNumber(value), &isNumber, &notNumber);
        builder_.Bind(&isNumber);
        {
            res = NewJSPrimitiveRef(PrimitiveType::PRIMITIVE_NUMBER, glue, value);
            builder_.Jump(&exit);
        }
        builder_.Bind(&notNumber);
        {
            Label isBoolean(&builder_);
            Label notBoolean(&builder_);
            BRANCH_CIR(builder_.TaggedIsBoolean(value), &isBoolean, &notBoolean);
            builder_.Bind(&isBoolean);
            {
                res = NewJSPrimitiveRef(PrimitiveType::PRIMITIVE_BOOLEAN, glue, value);
                builder_.Jump(&exit);
            }
            builder_.Bind(&notBoolean);
            {
                Label isNullOrUndefined(&builder_);
                BRANCH_CIR(builder_.TaggedIsUndefinedOrNull(value), &isNullOrUndefined, &slowPath);
                builder_.Bind(&isNullOrUndefined);
                {
                    GateRef glueGlobalEnvOffset = builder_.IntPtr(
                        JSThread::GlueData::GetGlueGlobalEnvOffset(builder_.GetCurrentEnvironment()->Is32Bit()));
                    GateRef glueGlobalEnv = builder_.Load(VariableType::NATIVE_POINTER(), glue, glueGlobalEnvOffset);
                    GateRef objectFunctionPrototype = builder_.GetGlobalEnvValue(VariableType::JS_ANY(),
                        glueGlobalEnv, GlobalEnv::OBJECT_FUNCTION_PROTOTYPE_INDEX);
                    res = builder_.OrdinaryNewJSObjectCreate(glue, objectFunctionPrototype);
                    builder_.Jump(&exit);
                }
            }
        }
    }
    builder_.Bind(&slowPath);
    {
        size_t range = acc_.GetNumValueIn(gate);
        std::vector<GateRef> args(range);
        for (size_t i = 0; i < range; ++i) {
            args[i] = acc_.GetValueIn(gate, i);
        }
        res = LowerCallRuntime(glue, gate, RTSTUB_ID(OptNewObjRange), args, true);
        builder_.Jump(&exit);
    }
    builder_.Bind(&exit);
    ReplaceGateWithPendingException(glue, gate, builder_.GetState(), builder_.GetDepend(), *res);
}

void TypedHCRLowering::LowerBooleanConstructorCheck(GateRef gate, GateRef glue)
{
    Environment env(gate, circuit_, &builder_);
    GateRef frameState = GetFrameState(gate);
    GateRef newTarget = acc_.GetValueIn(gate, 0);
    Label isHeapObject(&builder_);
    Label exit(&builder_);
    DEFVALUE(check, (&builder_), VariableType::BOOL(), builder_.True());
    check = builder_.TaggedIsHeapObject(newTarget);
    BRANCH_CIR(*check, &isHeapObject, &exit);
    builder_.Bind(&isHeapObject);
    {
        Label isJSFunction(&builder_);
        check = builder_.IsJSFunction(newTarget);
        BRANCH_CIR(*check, &isJSFunction, &exit);
        builder_.Bind(&isJSFunction);
        {
            Label getHclass(&builder_);
            GateRef glueGlobalEnvOffset = builder_.IntPtr(
                JSThread::GlueData::GetGlueGlobalEnvOffset(builder_.GetCurrentEnvironment()->Is32Bit()));
            GateRef glueGlobalEnv = builder_.Load(VariableType::NATIVE_POINTER(), glue, glueGlobalEnvOffset);
            GateRef booleanFunc =
                builder_.GetGlobalEnvValue(VariableType::JS_ANY(), glueGlobalEnv, GlobalEnv::BOOLEAN_FUNCTION_INDEX);
            check = builder_.Equal(booleanFunc, newTarget);
            BRANCH_CIR(*check, &getHclass, &exit);
            builder_.Bind(&getHclass);
            {
                GateRef intialHClass = builder_.Load(VariableType::JS_ANY(), newTarget,
                                                     builder_.IntPtr(JSFunction::PROTO_OR_DYNCLASS_OFFSET));
                check = builder_.IsJSHClass(intialHClass);
                builder_.Jump(&exit);
            }
        }
    }
    builder_.Bind(&exit);
    builder_.DeoptCheck(*check, frameState, DeoptType::NEWBUILTINCTORBOOLEAN);
    acc_.ReplaceGate(gate, builder_.GetState(), builder_.GetDepend(), Circuit::NullGate());
}

void TypedHCRLowering::LowerBooleanConstructor(GateRef gate, GateRef glue)
{
    Environment env(gate, circuit_, &builder_);
    GateRef value = builder_.Undefined();
    ASSERT(acc_.GetNumValueIn(gate) <= 2); // 2: new target and arg0
    if (acc_.GetNumValueIn(gate) > 1) {
        value = acc_.GetValueIn(gate, 1);
    }
    DEFVALUE(res, (&builder_), VariableType::JS_ANY(), builder_.Undefined());
    Label isBoolean(&builder_);
    Label slowPath(&builder_);
    Label exit(&builder_);
    BRANCH_CIR(builder_.TaggedIsBoolean(value), &isBoolean, &slowPath);
    builder_.Bind(&isBoolean);
    {
        res = NewJSPrimitiveRef(PrimitiveType::PRIMITIVE_BOOLEAN, glue, value);
        builder_.Jump(&exit);
    }
    builder_.Bind(&slowPath);
    {
        size_t range = acc_.GetNumValueIn(gate);
        std::vector<GateRef> args(range);
        for (size_t i = 0; i < range; ++i) {
            args[i] = acc_.GetValueIn(gate, i);
        }
        res = LowerCallRuntime(glue, gate, RTSTUB_ID(OptNewObjRange), args, true);
        builder_.Jump(&exit);
    }
    builder_.Bind(&exit);
    ReplaceGateWithPendingException(glue, gate, builder_.GetState(), builder_.GetDepend(), *res);
}

GateRef TypedHCRLowering::NewJSPrimitiveRef(PrimitiveType type, GateRef glue, GateRef value)
{
    GateRef glueGlobalEnvOffset = builder_.IntPtr(
        JSThread::GlueData::GetGlueGlobalEnvOffset(builder_.GetCurrentEnvironment()->Is32Bit()));
    GateRef globalEnv = builder_.Load(VariableType::NATIVE_POINTER(), glue, glueGlobalEnvOffset);
    GateRef ctor = Circuit::NullGate();
    switch (type) {
        case PrimitiveType::PRIMITIVE_NUMBER: {
            ctor = builder_.GetGlobalEnvValue(VariableType::JS_ANY(), globalEnv, GlobalEnv::NUMBER_FUNCTION_INDEX);
            break;
        }
        case PrimitiveType::PRIMITIVE_SYMBOL: {
            ctor = builder_.GetGlobalEnvValue(VariableType::JS_ANY(), globalEnv, GlobalEnv::SYMBOL_FUNCTION_INDEX);
            break;
        }
        case PrimitiveType::PRIMITIVE_BOOLEAN: {
            ctor = builder_.GetGlobalEnvValue(VariableType::JS_ANY(), globalEnv, GlobalEnv::BOOLEAN_FUNCTION_INDEX);
            break;
        }
        case PrimitiveType::PRIMITIVE_BIGINT: {
            ctor = builder_.GetGlobalEnvValue(VariableType::JS_ANY(), globalEnv, GlobalEnv::BIGINT_FUNCTION_INDEX);
            break;
        }
        default: {
            LOG_ECMA(FATAL) << "this branch is unreachable " << static_cast<int>(type);
            UNREACHABLE();
        }
    }
    GateRef hclass =
        builder_.Load(VariableType::JS_ANY(), ctor, builder_.IntPtr(JSFunction::PROTO_OR_DYNCLASS_OFFSET));
    NewObjectStubBuilder newBuilder(builder_.GetCurrentEnvironment());
    GateRef res = newBuilder.NewJSObject(glue, hclass);
    GateRef valueOffset = builder_.IntPtr(JSPrimitiveRef::VALUE_OFFSET);
    builder_.Store(VariableType::JS_ANY(), glue, res, valueOffset, value);
    return res;
}

void TypedHCRLowering::ReplaceGateWithPendingException(GateRef glue, GateRef gate, GateRef state, GateRef depend,
                                                       GateRef value)
{
    auto condition = builder_.HasPendingException(glue);
    GateRef ifBranch = builder_.Branch(state, condition, 1, BranchWeight::DEOPT_WEIGHT, "checkException");
    GateRef ifTrue = builder_.IfTrue(ifBranch);
    GateRef ifFalse = builder_.IfFalse(ifBranch);
    GateRef eDepend = builder_.DependRelay(ifTrue, depend);
    GateRef sDepend = builder_.DependRelay(ifFalse, depend);

    StateDepend success(ifFalse, sDepend);
    StateDepend exception(ifTrue, eDepend);
    acc_.ReplaceHirWithIfBranch(gate, success, exception, value);
}

void TypedHCRLowering::LowerLoadBuiltinObject(GateRef gate)
{
    if (!enableLoweringBuiltin_) {
        return;
    }
    Environment env(gate, circuit_, &builder_);
    auto frameState = GetFrameState(gate);
    GateRef glue = acc_.GetGlueFromArgList();
    auto builtinEntriesOffset = JSThread::GlueData::GetBuiltinEntriesOffset(false);
    size_t index = acc_.GetIndex(gate);
    auto boxOffset = builtinEntriesOffset + BuiltinIndex::GetInstance().GetBuiltinBoxOffset(index);
    GateRef box = builder_.LoadConstOffset(VariableType::JS_POINTER(), glue, boxOffset);
    GateRef builtin = builder_.LoadConstOffset(VariableType::JS_POINTER(), box, PropertyBox::VALUE_OFFSET);
    auto builtinIsNotHole = builder_.TaggedIsNotHole(builtin);
    // attributes on globalThis may change, it will cause renew a PropertyBox, the old box will be abandoned
    // so we need deopt
    builder_.DeoptCheck(builtinIsNotHole, frameState, DeoptType::BUILTINISHOLE1);
    acc_.ReplaceGate(gate, builder_.GetState(), builder_.GetDepend(), builtin);
}

void TypedHCRLowering::LowerOrdinaryHasInstance(GateRef gate, GateRef glue)
{
    Environment env(gate, circuit_, &builder_);
    GateRef obj = acc_.GetValueIn(gate, 0);
    GateRef target = acc_.GetValueIn(gate, 1);

    DEFVALUE(result, (&builder_), VariableType::JS_ANY(), builder_.TaggedFalse());
    DEFVALUE(object, (&builder_), VariableType::JS_ANY(), obj);
    Label exit(&builder_);

    Label targetIsBoundFunction(&builder_);
    Label targetNotBoundFunction(&builder_);
    // 2. If C has a [[BoundTargetFunction]] internal slot, then
    //    a. Let BC be the value of C's [[BoundTargetFunction]] internal slot.
    //    b. Return InstanceofOperator(O,BC)  (see 12.9.4).
    BRANCH_CIR(builder_.TaggedIsBoundFunction(target), &targetIsBoundFunction, &targetNotBoundFunction);
    builder_.Bind(&targetIsBoundFunction);
    {
        GateRef boundTarget = builder_.LoadConstOffset(VariableType::JS_ANY(), target,
                                                       JSBoundFunction::BOUND_TARGET_OFFSET);
        result = builder_.CallRuntime(glue, RTSTUB_ID(InstanceOf), Gate::InvalidGateRef,
                                      { obj, boundTarget }, gate);
        builder_.Jump(&exit);
    }
    builder_.Bind(&targetNotBoundFunction);

    // 3. If Type(O) is not Object, return false
    Label objIsHeapObject(&builder_);
    Label objIsEcmaObject(&builder_);
    Label objNotEcmaObject(&builder_);
    BRANCH_CIR(builder_.TaggedIsHeapObject(obj), &objIsHeapObject, &objNotEcmaObject);
    builder_.Bind(&objIsHeapObject);
    BRANCH_CIR(builder_.TaggedObjectIsEcmaObject(obj), &objIsEcmaObject, &objNotEcmaObject);
    builder_.Bind(&objNotEcmaObject);
    {
        result = builder_.TaggedFalse();
        builder_.Jump(&exit);
    }
    builder_.Bind(&objIsEcmaObject);
    {
        // 4. Let P be Get(C, "prototype").
        Label getCtorProtoSlowPath(&builder_);
        Label ctorIsJSFunction(&builder_);
        Label gotCtorPrototype(&builder_);
        Label isHeapObject(&builder_);
        DEFVALUE(constructorPrototype, (&builder_), VariableType::JS_ANY(), builder_.Undefined());
        BRANCH_CIR(builder_.IsJSFunction(target), &ctorIsJSFunction, &getCtorProtoSlowPath);
        builder_.Bind(&ctorIsJSFunction);
        {
            Label getCtorProtoFastPath(&builder_);
            GateRef ctorProtoOrHC = builder_.LoadConstOffset(VariableType::JS_POINTER(), target,
                                                             JSFunction::PROTO_OR_DYNCLASS_OFFSET);

            BRANCH_CIR(builder_.TaggedIsHole(ctorProtoOrHC), &getCtorProtoSlowPath, &getCtorProtoFastPath);
            builder_.Bind(&getCtorProtoFastPath);
            {
                Label isHClass(&builder_);
                Label isPrototype(&builder_);
                BRANCH_CIR(builder_.TaggedIsHeapObject(ctorProtoOrHC), &isHeapObject, &getCtorProtoSlowPath);
                builder_.Bind(&isHeapObject);
                BRANCH_CIR(builder_.IsJSHClass(ctorProtoOrHC), &isHClass, &isPrototype);
                builder_.Bind(&isHClass);
                {
                    constructorPrototype = builder_.LoadConstOffset(VariableType::JS_POINTER(), ctorProtoOrHC,
                                                                    JSHClass::PROTOTYPE_OFFSET);
                    builder_.Jump(&gotCtorPrototype);
                }
                builder_.Bind(&isPrototype);
                {
                    constructorPrototype = ctorProtoOrHC;
                    builder_.Jump(&gotCtorPrototype);
                }
            }
        }
        builder_.Bind(&getCtorProtoSlowPath);
        {
            auto prototypeString = builder_.GetGlobalConstantValue(ConstantIndex::PROTOTYPE_STRING_INDEX);
            constructorPrototype = builder_.CallRuntime(glue, RTSTUB_ID(GetPropertyByName), Gate::InvalidGateRef,
                                                        { target, prototypeString }, gate);
            builder_.Jump(&gotCtorPrototype);
        }
        // 6. If Type(P) is not Object, throw a TypeError exception.
        Label prototypeIsEcmaObj(&builder_);
        Label prototypeNotEcmaObj(&builder_);
        builder_.Bind(&gotCtorPrototype);
        {
            BRANCH_CIR(builder_.IsEcmaObject(*constructorPrototype), &prototypeIsEcmaObj, &prototypeNotEcmaObj);
            builder_.Bind(&prototypeNotEcmaObj);
            {
                GateRef taggedId = builder_.Int32(GET_MESSAGE_STRING_ID(TargetTypeNotObject));
                builder_.CallRuntime(glue, RTSTUB_ID(ThrowTypeError), Gate::InvalidGateRef,
                                     { builder_.Int32ToTaggedInt(taggedId) }, gate);
                result = builder_.ExceptionConstant();
                builder_.Jump(&exit);
            }
        }
        builder_.Bind(&prototypeIsEcmaObj);
        // 7. Repeat
        //    a.Let O be O.[[GetPrototypeOf]]().
        //    b.ReturnIfAbrupt(O).
        //    c.If O is null, return false.
        //    d.If SameValue(P, O) is true, return true.
        Label loopHead(&builder_);
        Label loopEnd(&builder_);
        Label afterLoop(&builder_);
        Label strictEqual1(&builder_);
        Label notStrictEqual1(&builder_);
        Label shouldReturn(&builder_);
        Label shouldContinue(&builder_);

        BRANCH_CIR(builder_.TaggedIsNull(*object), &afterLoop, &loopHead);
        builder_.LoopBegin(&loopHead);
        {
            GateRef isEqual = builder_.Equal(*object, *constructorPrototype);

            BRANCH_CIR(isEqual, &strictEqual1, &notStrictEqual1);
            builder_.Bind(&strictEqual1);
            {
                result = builder_.TaggedTrue();
                builder_.Jump(&exit);
            }
            builder_.Bind(&notStrictEqual1);
            {
                Label objectIsHeapObject(&builder_);
                Label objectIsEcmaObject(&builder_);
                Label objectNotEcmaObject(&builder_);

                BRANCH_CIR(builder_.TaggedIsHeapObject(*object), &objectIsHeapObject, &objectNotEcmaObject);
                builder_.Bind(&objectIsHeapObject);
                BRANCH_CIR(builder_.TaggedObjectIsEcmaObject(*object), &objectIsEcmaObject, &objectNotEcmaObject);
                builder_.Bind(&objectNotEcmaObject);
                {
                    GateRef taggedId = builder_.Int32(GET_MESSAGE_STRING_ID(CanNotGetNotEcmaObject));
                    builder_.CallRuntime(glue, RTSTUB_ID(ThrowTypeError), Gate::InvalidGateRef,
                                         { builder_.Int32ToTaggedInt(taggedId) }, gate);
                    result = builder_.ExceptionConstant();
                    builder_.Jump(&exit);
                }
                builder_.Bind(&objectIsEcmaObject);
                {
                    Label objectIsJsProxy(&builder_);
                    Label objectNotIsJsProxy(&builder_);
                    BRANCH_CIR(builder_.IsJsProxy(*object), &objectIsJsProxy, &objectNotIsJsProxy);
                    builder_.Bind(&objectIsJsProxy);
                    {
                        object = builder_.CallRuntime(glue, RTSTUB_ID(CallGetPrototype), Gate::InvalidGateRef,
                                                      { *object }, gate);
                        builder_.Jump(&shouldContinue);
                    }
                    builder_.Bind(&objectNotIsJsProxy);
                    {
                        GateRef objHClass = builder_.LoadHClass(*object);
                        object = builder_.LoadPrototype(objHClass);
                        builder_.Jump(&shouldContinue);
                    }
                }
            }
            builder_.Bind(&shouldContinue);
            BRANCH_CIR(builder_.TaggedIsNull(*object), &afterLoop, &loopEnd);
        }
        builder_.Bind(&loopEnd);
        builder_.LoopEnd(&loopHead);
        builder_.Bind(&afterLoop);
        {
            result = builder_.TaggedFalse();
            builder_.Jump(&exit);
        }
    }
    builder_.Bind(&exit);
    ReplaceGateWithPendingException(glue, gate, builder_.GetState(), builder_.GetDepend(), *result);
}

void TypedHCRLowering::LowerProtoChangeMarkerCheck(GateRef gate)
{
    Environment env(gate, circuit_, &builder_);
    GateRef frameState = acc_.GetFrameState(gate);
    GateRef receiver = acc_.GetValueIn(gate, 0);
    auto hclass = builder_.LoadConstOffset(VariableType::JS_POINTER(), receiver, TaggedObject::HCLASS_OFFSET);
    auto prototype = builder_.LoadConstOffset(VariableType::JS_ANY(), hclass, JSHClass::PROTOTYPE_OFFSET);
    auto protoHClass = builder_.LoadConstOffset(VariableType::JS_POINTER(), prototype, TaggedObject::HCLASS_OFFSET);
    auto marker = builder_.LoadConstOffset(VariableType::JS_ANY(), protoHClass, JSHClass::PROTO_CHANGE_MARKER_OFFSET);
    auto notNull = builder_.TaggedIsNotNull(marker);
    builder_.DeoptCheck(notNull, frameState, DeoptType::PROTOTYPECHANGED2);
    auto hasChanged = builder_.GetHasChanged(marker);
    builder_.DeoptCheck(builder_.BoolNot(hasChanged), frameState, DeoptType::PROTOTYPECHANGED2);
    acc_.ReplaceGate(gate, builder_.GetState(), builder_.GetDepend(), Circuit::NullGate());
}

void TypedHCRLowering::LowerMonoLoadPropertyOnProto(GateRef gate)
{
    Environment env(gate, circuit_, &builder_);
    GateRef frameState = acc_.GetFrameState(gate);
    GateRef receiver = acc_.GetValueIn(gate, 0);
    GateRef propertyLookupResult = acc_.GetValueIn(gate, 1); // 1: propertyLookupResult
    GateRef hclassIndex = acc_.GetValueIn(gate, 2); // 2: hclassIndex
    GateRef unsharedConstPool = acc_.GetValueIn(gate, 3); // 3: constPool
    PropertyLookupResult plr(acc_.TryGetValue(propertyLookupResult));
    GateRef result = Circuit::NullGate();
    ASSERT(plr.IsLocal() || plr.IsFunction());

    auto receiverHC = builder_.LoadConstOffset(VariableType::JS_POINTER(), receiver, TaggedObject::HCLASS_OFFSET);
    auto prototype = builder_.LoadConstOffset(VariableType::JS_ANY(), receiverHC, JSHClass::PROTOTYPE_OFFSET);

    // lookup from receiver for holder
    auto holderHC = builder_.LoadHClassFromConstpool(unsharedConstPool, acc_.GetConstantValue(hclassIndex));
    DEFVALUE(current, (&builder_), VariableType::JS_ANY(), prototype);
    Label exit(&builder_);
    Label loopHead(&builder_);
    Label loadHolder(&builder_);
    Label lookUpProto(&builder_);
    builder_.Jump(&loopHead);

    builder_.LoopBegin(&loopHead);
    builder_.DeoptCheck(builder_.TaggedIsNotNull(*current), frameState, DeoptType::INCONSISTENTHCLASS8);
    auto curHC = builder_.LoadConstOffset(VariableType::JS_POINTER(), *current, TaggedObject::HCLASS_OFFSET);
    BRANCH_CIR(builder_.Equal(curHC, holderHC), &loadHolder, &lookUpProto);

    builder_.Bind(&lookUpProto);
    current = builder_.LoadConstOffset(VariableType::JS_ANY(), curHC, JSHClass::PROTOTYPE_OFFSET);
    builder_.LoopEnd(&loopHead);

    builder_.Bind(&loadHolder);
    result = LoadPropertyFromHolder(*current, plr);
    builder_.Jump(&exit);
    builder_.Bind(&exit);
    acc_.ReplaceGate(gate, builder_.GetState(), builder_.GetDepend(), result);
}

void TypedHCRLowering::LowerMonoCallGetterOnProto(GateRef gate, GateRef glue)
{
    Environment env(gate, circuit_, &builder_);
    GateRef frameState = acc_.GetFrameState(gate);
    GateRef receiver = acc_.GetValueIn(gate, 0);
    GateRef propertyLookupResult = acc_.GetValueIn(gate, 1); // 1: propertyLookupResult
    GateRef hclassIndex = acc_.GetValueIn(gate, 2); // 2: hclassIndex
    GateRef unsharedConstPool = acc_.GetValueIn(gate, 3); // 3: constPool
    PropertyLookupResult plr(acc_.TryGetValue(propertyLookupResult));
    GateRef accessor = Circuit::NullGate();
    GateRef holder = Circuit::NullGate();

    auto receiverHC = builder_.LoadConstOffset(VariableType::JS_POINTER(), receiver, TaggedObject::HCLASS_OFFSET);
    auto prototype = builder_.LoadConstOffset(VariableType::JS_ANY(), receiverHC, JSHClass::PROTOTYPE_OFFSET);

    // lookup from receiver for holder
    auto holderHC = builder_.LoadHClassFromConstpool(unsharedConstPool, acc_.GetConstantValue(hclassIndex));
    DEFVALUE(current, (&builder_), VariableType::JS_ANY(), prototype);
    Label exitLoad(&builder_);
    Label loopHead(&builder_);
    Label loadHolder(&builder_);
    Label lookUpProto(&builder_);
    builder_.Jump(&loopHead);

    builder_.LoopBegin(&loopHead);
    builder_.DeoptCheck(builder_.TaggedIsNotNull(*current), frameState, DeoptType::INCONSISTENTHCLASS9);
    auto curHC = builder_.LoadConstOffset(VariableType::JS_POINTER(), *current, TaggedObject::HCLASS_OFFSET);
    BRANCH_CIR(builder_.Equal(curHC, holderHC), &loadHolder, &lookUpProto);

    builder_.Bind(&lookUpProto);
    current = builder_.LoadConstOffset(VariableType::JS_ANY(), curHC, JSHClass::PROTOTYPE_OFFSET);
    builder_.LoopEnd(&loopHead);

    builder_.Bind(&loadHolder);
    holder = *current;
    accessor = LoadPropertyFromHolder(holder, plr);
    builder_.Jump(&exitLoad);
    builder_.Bind(&exitLoad);
    DEFVALUE(result, (&builder_), VariableType::JS_ANY(), builder_.UndefineConstant());
    Label isInternalAccessor(&builder_);
    Label notInternalAccessor(&builder_);
    Label callGetter(&builder_);
    Label exit(&builder_);
    BRANCH_CIR(builder_.IsAccessorInternal(accessor), &isInternalAccessor, &notInternalAccessor);
    {
        builder_.Bind(&isInternalAccessor);
        {
            result = builder_.CallRuntime(glue, RTSTUB_ID(CallInternalGetter),
                Gate::InvalidGateRef, { accessor, holder }, gate);
            builder_.Jump(&exit);
        }
        builder_.Bind(&notInternalAccessor);
        {
            GateRef getter = builder_.LoadConstOffset(VariableType::JS_ANY(), accessor, AccessorData::GETTER_OFFSET);
            BRANCH_CIR(builder_.IsSpecial(getter, JSTaggedValue::VALUE_UNDEFINED), &exit, &callGetter);
            builder_.Bind(&callGetter);
            {
                result = CallAccessor(glue, gate, getter, receiver, AccessorMode::GETTER);
                builder_.Jump(&exit);
            }
        }
    }
    builder_.Bind(&exit);
    ReplaceHirWithPendingException(gate, glue, builder_.GetState(), builder_.GetDepend(), *result);
}

GateRef TypedHCRLowering::LoadPropertyFromHolder(GateRef holder, PropertyLookupResult plr)
{
    GateRef result = Circuit::NullGate();
    if (plr.IsNotHole()) {
        ASSERT(plr.IsLocal());
        if (plr.IsInlinedProps()) {
            result = builder_.LoadConstOffset(VariableType::JS_ANY(), holder, plr.GetOffset());
        } else {
            auto properties = builder_.LoadConstOffset(VariableType::JS_ANY(), holder, JSObject::PROPERTIES_OFFSET);
            result = builder_.GetValueFromTaggedArray(properties, builder_.Int32(plr.GetOffset()));
        }
    } else if (plr.IsLocal()) {
        if (plr.IsInlinedProps()) {
            result = builder_.LoadConstOffset(VariableType::JS_ANY(), holder, plr.GetOffset());
        } else {
            auto properties = builder_.LoadConstOffset(VariableType::JS_ANY(), holder, JSObject::PROPERTIES_OFFSET);
            result = builder_.GetValueFromTaggedArray(properties, builder_.Int32(plr.GetOffset()));
        }
        result = builder_.ConvertHoleAsUndefined(result);
    } else {
        UNREACHABLE();
    }
    return result;
}

void TypedHCRLowering::LowerMonoStorePropertyLookUpProto(GateRef gate, GateRef glue)
{
    Environment env(gate, circuit_, &builder_);
    GateRef frameState = acc_.GetFrameState(gate);
    GateRef receiver = acc_.GetValueIn(gate, 0);
    GateRef propertyLookupResult = acc_.GetValueIn(gate, 1); // 1: propertyLookupResult
    GateRef hclassIndex = acc_.GetValueIn(gate, 2); // 2: hclassIndex
    GateRef unsharedConstPool = acc_.GetValueIn(gate, 3); // 3: constpool
    GateRef value = acc_.GetValueIn(gate, 4); // 4: value
    PropertyLookupResult plr(acc_.TryGetValue(propertyLookupResult));
    bool noBarrier = acc_.IsNoBarrier(gate);

    auto receiverHC = builder_.LoadConstOffset(VariableType::JS_POINTER(), receiver, TaggedObject::HCLASS_OFFSET);
    auto prototype = builder_.LoadConstOffset(VariableType::JS_ANY(), receiverHC, JSHClass::PROTOTYPE_OFFSET);
    // lookup from receiver for holder
    auto holderHC = builder_.LoadHClassFromConstpool(unsharedConstPool, acc_.GetConstantValue(hclassIndex));
    DEFVALUE(current, (&builder_), VariableType::JS_ANY(), prototype);
    Label exit(&builder_);
    Label loopHead(&builder_);
    Label loadHolder(&builder_);
    Label lookUpProto(&builder_);
    builder_.Jump(&loopHead);

    builder_.LoopBegin(&loopHead);
    builder_.DeoptCheck(builder_.TaggedIsNotNull(*current), frameState, DeoptType::INCONSISTENTHCLASS10);
    auto curHC = builder_.LoadConstOffset(VariableType::JS_POINTER(), *current,
                                          TaggedObject::HCLASS_OFFSET);
    BRANCH_CIR(builder_.Equal(curHC, holderHC), &loadHolder, &lookUpProto);

    builder_.Bind(&lookUpProto);
    current = builder_.LoadConstOffset(VariableType::JS_ANY(), curHC, JSHClass::PROTOTYPE_OFFSET);
    builder_.LoopEnd(&loopHead);

    builder_.Bind(&loadHolder);
    if (!plr.IsAccessor()) {
        StorePropertyOnHolder(*current, value, plr, noBarrier);
        builder_.Jump(&exit);
    } else {
        GateRef accessor = LoadPropertyFromHolder(*current, plr);
        Label isInternalAccessor(&builder_);
        Label notInternalAccessor(&builder_);
        Label callSetter(&builder_);
        BRANCH_CIR(builder_.IsAccessorInternal(accessor), &isInternalAccessor, &notInternalAccessor);
        {
            builder_.Bind(&isInternalAccessor);
            {
                builder_.CallRuntime(glue, RTSTUB_ID(CallInternalSetter),
                    Gate::InvalidGateRef, { receiver, accessor, value }, gate);
                builder_.Jump(&exit);
            }
            builder_.Bind(&notInternalAccessor);
            {
                GateRef setter =
                    builder_.LoadConstOffset(VariableType::JS_ANY(), accessor, AccessorData::SETTER_OFFSET);
                BRANCH_CIR(builder_.IsSpecial(setter, JSTaggedValue::VALUE_UNDEFINED), &exit, &callSetter);
                builder_.Bind(&callSetter);
                {
                    CallAccessor(glue, gate, setter, receiver, AccessorMode::SETTER, value);
                    builder_.Jump(&exit);
                }
            }
        }
    }
    builder_.Bind(&exit);
    acc_.ReplaceGate(gate, builder_.GetState(), builder_.GetDepend(), Circuit::NullGate());
}

void TypedHCRLowering::LowerMonoStoreProperty(GateRef gate, GateRef glue)
{
    Environment env(gate, circuit_, &builder_);
    GateRef receiver = acc_.GetValueIn(gate, 0);
    GateRef propertyLookupResult = acc_.GetValueIn(gate, 1); // 1: propertyLookupResult
    GateRef hclassIndex = acc_.GetValueIn(gate, 2); // 2: hclassIndex
    GateRef unsharedConstPool = acc_.GetValueIn(gate, 3); // 3: constPool
    GateRef value = acc_.GetValueIn(gate, 4); // 4: value
    GateRef key = acc_.GetValueIn(gate, 5); // 5: key
    PropertyLookupResult plr(acc_.TryGetValue(propertyLookupResult));
    bool noBarrier = acc_.IsNoBarrier(gate);
    auto receiverHC = builder_.LoadConstOffset(VariableType::JS_POINTER(), receiver, TaggedObject::HCLASS_OFFSET);
    auto prototype = builder_.LoadConstOffset(VariableType::JS_ANY(), receiverHC, JSHClass::PROTOTYPE_OFFSET);
    // transition happened
    Label exit(&builder_);
    Label notProto(&builder_);
    Label isProto(&builder_);
    auto newHolderHC = builder_.LoadHClassFromConstpool(unsharedConstPool, acc_.GetConstantValue(hclassIndex));
    builder_.StoreConstOffset(VariableType::JS_ANY(), newHolderHC, JSHClass::PROTOTYPE_OFFSET, prototype);
    builder_.Branch(builder_.IsProtoTypeHClass(receiverHC), &isProto, &notProto,
        BranchWeight::ONE_WEIGHT, BranchWeight::DEOPT_WEIGHT, "isProtoTypeHClass");
    builder_.Bind(&isProto);
    builder_.CallRuntime(glue, RTSTUB_ID(UpdateAOTHClass), Gate::InvalidGateRef,
        { receiverHC, newHolderHC, key }, gate);
    builder_.Jump(&notProto);
    builder_.Bind(&notProto);
    MemoryAttribute mAttr = MemoryAttribute::NeedBarrierAndAtomic();
    builder_.StoreConstOffset(VariableType::JS_ANY(), receiver, TaggedObject::HCLASS_OFFSET, newHolderHC, mAttr);
    if (!plr.IsInlinedProps()) {
        auto properties =
            builder_.LoadConstOffset(VariableType::JS_ANY(), receiver, JSObject::PROPERTIES_OFFSET);
        auto capacity = builder_.LoadConstOffset(VariableType::INT32(), properties, TaggedArray::LENGTH_OFFSET);
        auto index = builder_.Int32(plr.GetOffset());
        Label needExtend(&builder_);
        Label notExtend(&builder_);
        BRANCH_CIR(builder_.Int32UnsignedLessThan(index, capacity), &notExtend, &needExtend);
        builder_.Bind(&notExtend);
        {
            if (!plr.IsAccessor()) {
                StorePropertyOnHolder(receiver, value, plr, noBarrier);
                builder_.Jump(&exit);
            } else {
                GateRef accessor = LoadPropertyFromHolder(receiver, plr);
                Label isInternalAccessor(&builder_);
                Label notInternalAccessor(&builder_);
                Label callSetter(&builder_);
                BRANCH_CIR(builder_.IsAccessorInternal(accessor), &isInternalAccessor, &notInternalAccessor);
                {
                    builder_.Bind(&isInternalAccessor);
                    {
                        builder_.CallRuntime(glue, RTSTUB_ID(CallInternalSetter),
                            Gate::InvalidGateRef, { receiver, accessor, value }, gate);
                        builder_.Jump(&exit);
                    }
                    builder_.Bind(&notInternalAccessor);
                    {
                        GateRef setter =
                            builder_.LoadConstOffset(VariableType::JS_ANY(), accessor, AccessorData::SETTER_OFFSET);
                        BRANCH_CIR(builder_.IsSpecial(setter, JSTaggedValue::VALUE_UNDEFINED), &exit, &callSetter);
                        builder_.Bind(&callSetter);
                        {
                            CallAccessor(glue, gate, setter, receiver, AccessorMode::SETTER, value);
                            builder_.Jump(&exit);
                        }
                    }
                }
            }
        }
        builder_.Bind(&needExtend);
        {
            builder_.CallRuntime(glue,
                RTSTUB_ID(PropertiesSetValue),
                Gate::InvalidGateRef,
                { receiver, value, properties, builder_.Int32ToTaggedInt(capacity),
                builder_.Int32ToTaggedInt(index) }, gate);
            builder_.Jump(&exit);
        }
    } else {
        if (!plr.IsAccessor()) {
            StorePropertyOnHolder(receiver, value, plr, noBarrier);
            builder_.Jump(&exit);
        } else {
            GateRef accessor = LoadPropertyFromHolder(receiver, plr);
            Label isInternalAccessor(&builder_);
            Label notInternalAccessor(&builder_);
            Label callSetter(&builder_);
            BRANCH_CIR(builder_.IsAccessorInternal(accessor), &isInternalAccessor, &notInternalAccessor);
            {
                builder_.Bind(&isInternalAccessor);
                {
                    builder_.CallRuntime(glue, RTSTUB_ID(CallInternalSetter),
                        Gate::InvalidGateRef, { receiver, accessor, value }, gate);
                    builder_.Jump(&exit);
                }
                builder_.Bind(&notInternalAccessor);
                {
                    GateRef setter =
                        builder_.LoadConstOffset(VariableType::JS_ANY(), accessor, AccessorData::SETTER_OFFSET);
                    BRANCH_CIR(builder_.IsSpecial(setter, JSTaggedValue::VALUE_UNDEFINED), &exit, &callSetter);
                    builder_.Bind(&callSetter);
                    {
                        CallAccessor(glue, gate, setter, receiver, AccessorMode::SETTER, value);
                        builder_.Jump(&exit);
                    }
                }
            }
        }
    }
    builder_.Bind(&exit);
    acc_.ReplaceGate(gate, builder_.GetState(), builder_.GetDepend(), Circuit::NullGate());
}

void TypedHCRLowering::StorePropertyOnHolder(GateRef holder, GateRef value, PropertyLookupResult plr, bool noBarrier)
{
    if (!noBarrier) {
        if (plr.IsInlinedProps()) {
            builder_.StoreConstOffset(VariableType::JS_ANY(), holder, plr.GetOffset(), value);
        } else {
            auto properties = builder_.LoadConstOffset(VariableType::JS_ANY(), holder, JSObject::PROPERTIES_OFFSET);
            builder_.SetValueToTaggedArray(
                VariableType::JS_ANY(), acc_.GetGlueFromArgList(), properties, builder_.Int32(plr.GetOffset()), value);
        }
    } else {
        if (plr.IsInlinedProps()) {
            builder_.StoreConstOffset(GetVarType(plr), holder, plr.GetOffset(), value);
        } else {
            auto properties = builder_.LoadConstOffset(VariableType::JS_ANY(), holder, JSObject::PROPERTIES_OFFSET);
            builder_.SetValueToTaggedArray(
                GetVarType(plr), acc_.GetGlueFromArgList(), properties, builder_.Int32(plr.GetOffset()), value);
        }
    }
}

void TypedHCRLowering::LowerTypedCreateObjWithBuffer(GateRef gate, GateRef glue)
{
    Environment env(gate, circuit_, &builder_);
    DEFVALUE(ret, (&builder_), VariableType::JS_ANY(), builder_.Undefined());
    Label equal(&builder_);
    Label notEqual(&builder_);
    Label exit(&builder_);
    GateRef objSize = acc_.GetValueIn(gate, 0); // 0: objSize
    GateRef index = acc_.GetValueIn(gate, 1); // 1: index
    GateRef lexEnv = acc_.GetValueIn(gate, 3); // 3: lexenv
    size_t numValueIn = acc_.GetNumValueIn(gate);
    ArgumentAccessor argAcc(circuit_);
    GateRef frameState = acc_.GetFrameState(gate);
    GateRef jsFunc = argAcc.GetFrameArgsIn(frameState, FrameArgIdx::FUNC);
    GateRef module = builder_.GetModuleFromFunction(jsFunc);
    GateRef sharedConstpool = argAcc.GetFrameArgsIn(frameState, FrameArgIdx::SHARED_CONST_POOL);
    GateRef unsharedConstPool = argAcc.GetFrameArgsIn(frameState, FrameArgIdx::UNSHARED_CONST_POOL);
    GateRef oldObj = builder_.GetObjectFromConstPool(glue, gate, sharedConstpool, unsharedConstPool, module,
        builder_.TruncInt64ToInt32(index), ConstPoolType::OBJECT_LITERAL);
    GateRef hclass = builder_.LoadConstOffset(VariableType::JS_POINTER(), oldObj, JSObject::HCLASS_OFFSET);
    GateRef emptyArray = builder_.GetGlobalConstantValue(ConstantIndex::EMPTY_ARRAY_OBJECT_INDEX);
    GateRef objectSize = builder_.GetObjectSizeFromHClass(hclass);
    BRANCH_CIR(builder_.Equal(objectSize, objSize), &equal, &notEqual);
    builder_.Bind(&equal);
    {
        builder_.StartAllocate();
        GateRef newObj = builder_.HeapAlloc(glue, objSize, GateType::TaggedValue(), RegionSpaceFlag::IN_YOUNG_SPACE);
        builder_.StoreConstOffset(VariableType::JS_POINTER(), newObj, JSObject::HCLASS_OFFSET, hclass,
                                  MemoryAttribute::NeedBarrierAndAtomic());
        builder_.StoreConstOffset(VariableType::INT64(), newObj,
            JSObject::HASH_OFFSET, builder_.Int64(JSTaggedValue(0).GetRawData()));
        builder_.StoreConstOffset(VariableType::INT64(), newObj, JSObject::PROPERTIES_OFFSET, emptyArray);
        builder_.StoreConstOffset(VariableType::INT64(), newObj, JSObject::ELEMENTS_OFFSET, emptyArray);
        size_t fixedNumValueIn = 4; // objSize, index, hclass, lexEnv
        for (uint32_t i = 0; i < numValueIn - fixedNumValueIn; i += 2) { // 2 : value, offset
            builder_.StoreConstOffset(VariableType::INT64(), newObj,
                acc_.GetConstantValue(acc_.GetValueIn(gate, i + fixedNumValueIn + 1)),
                acc_.GetValueIn(gate, i + fixedNumValueIn));
        }
        ret = builder_.FinishAllocate(newObj);
        builder_.Jump(&exit);
    }
    builder_.Bind(&notEqual);
    {
        ret = builder_.CallRuntime(glue, RTSTUB_ID(CreateObjectHavingMethod),
            Gate::InvalidGateRef, { oldObj, lexEnv }, gate);
        builder_.Jump(&exit);
    }
    builder_.Bind(&exit);
    acc_.ReplaceGate(gate, builder_.GetState(), builder_.GetDepend(), *ret);
}

void TypedHCRLowering::LowerStringFromSingleCharCode(GateRef gate, GateRef glue)
{
    Environment env(gate, circuit_, &builder_);
    DEFVALUE(value, (&builder_), VariableType::INT16(), builder_.Int16(0));
    DEFVALUE(res, (&builder_), VariableType::JS_ANY(), builder_.Undefined());
    Label isPendingException(&builder_);
    Label noPendingException(&builder_);
    Label exit(&builder_);
    Label isInt(&builder_);
    Label notInt(&builder_);
    Label newObj(&builder_);
    Label canBeCompress(&builder_);
    Label canNotBeCompress(&builder_);
    GateRef codePointTag = acc_.GetValueIn(gate);
    GateRef codePointValue = builder_.ToNumber(gate, codePointTag, glue);
    BRANCH_CIR(builder_.HasPendingException(glue), &isPendingException, &noPendingException);
    builder_.Bind(&isPendingException);
    {
        res = builder_.ExceptionConstant();
        builder_.Jump(&exit);
    }
    builder_.Bind(&noPendingException);
    {
        BRANCH_CIR(builder_.TaggedIsInt(codePointValue), &isInt, &notInt);
        builder_.Bind(&isInt);
        {
            value = builder_.TruncInt32ToInt16(builder_.GetInt32OfTInt(codePointValue));
            builder_.Jump(&newObj);
        }
        builder_.Bind(&notInt);
        {
            value = builder_.TruncInt32ToInt16(
                builder_.DoubleToInt(glue, builder_.GetDoubleOfTDouble(codePointValue), base::INT16_BITS));
            builder_.Jump(&newObj);
        }
        builder_.Bind(&newObj);
        BRANCH_CIR(builder_.IsASCIICharacter(builder_.ZExtInt16ToInt32(*value)), &canBeCompress, &canNotBeCompress);
        NewObjectStubBuilder newBuilder(&env);
        newBuilder.SetParameters(glue, 0);
        builder_.Bind(&canBeCompress);
        {
            GateRef singleCharTable = builder_.GetGlobalConstantValue(ConstantIndex::SINGLE_CHAR_TABLE_INDEX);
            res = builder_.GetValueFromTaggedArray(singleCharTable, builder_.ZExtInt16ToInt32(*value));
            builder_.Jump(&exit);
        }
        builder_.Bind(&canNotBeCompress);
        {
            Label afterNew1(&builder_);
            newBuilder.AllocLineStringObject(&res, &afterNew1, builder_.Int32(1), false);
            builder_.Bind(&afterNew1);
            {
                GateRef dst = builder_.ChangeTaggedPointerToInt64(
                    builder_.PtrAdd(*res, builder_.IntPtr(LineEcmaString::DATA_OFFSET)));
                builder_.Store(VariableType::INT16(), glue, dst, builder_.IntPtr(0), *value);
                builder_.Jump(&exit);
            }
        }
    }
    builder_.Bind(&exit);
    ReplaceGateWithPendingException(glue, gate, builder_.GetState(), builder_.GetDepend(), *res);
}

void TypedHCRLowering::LowerMigrateArrayWithKind(GateRef gate)
{
    Environment env(gate, circuit_, &builder_);
    Label exit(&builder_);
    GateRef object = acc_.GetValueIn(gate, 0);
    GateRef oldKind = acc_.GetValueIn(gate, 1); // 1: objSize
    GateRef newKind = acc_.GetValueIn(gate, 2); // 2: index

    DEFVALUE(newElements, (&builder_), VariableType::JS_ANY(), builder_.Undefined());
    Label doMigration(&builder_);
    Label migrateFromInt(&builder_);
    Label migrateOtherKinds(&builder_);
    GateRef oldKindIsInt = builder_.Int32Equal(oldKind, builder_.Int32(static_cast<uint32_t>(ElementsKind::INT)));
    GateRef newKindIsHoleInt = builder_.Int32Equal(newKind,
                                                   builder_.Int32(static_cast<uint32_t>(ElementsKind::HOLE_INT)));
    GateRef oldKindIsNum = builder_.Int32Equal(oldKind, builder_.Int32(static_cast<uint32_t>(ElementsKind::NUMBER)));
    GateRef newKindIsHoleNum = builder_.Int32Equal(newKind,
                                                   builder_.Int32(static_cast<uint32_t>(ElementsKind::HOLE_NUMBER)));
    GateRef isTransitToHoleKind = builder_.BoolOr(builder_.BoolAnd(oldKindIsInt, newKindIsHoleInt),
                                                  builder_.BoolAnd(oldKindIsNum, newKindIsHoleNum));
    GateRef sameKinds = builder_.Int32Equal(oldKind, newKind);
    GateRef noNeedMigration = builder_.BoolOr(sameKinds, isTransitToHoleKind);
    BRANCH_CIR(noNeedMigration, &exit, &doMigration);
    builder_.Bind(&doMigration);

    GateRef needCOW = builder_.IsJsCOWArray(object);
    BRANCH_CIR(builder_.ElementsKindIsIntOrHoleInt(oldKind), &migrateFromInt, &migrateOtherKinds);
    builder_.Bind(&migrateFromInt);
    {
        Label migrateToHeapValuesFromInt(&builder_);
        Label migrateToRawValuesFromInt(&builder_);
        Label migrateToNumbersFromInt(&builder_);
        BRANCH_CIR(builder_.ElementsKindIsHeapKind(newKind),
                   &migrateToHeapValuesFromInt, &migrateToRawValuesFromInt);
        builder_.Bind(&migrateToHeapValuesFromInt);
        {
            newElements = builder_.MigrateFromRawValueToHeapValues(object, needCOW, builder_.True());
            builder_.StoreConstOffset(VariableType::JS_ANY(), object, JSObject::ELEMENTS_OFFSET, *newElements);
            builder_.Jump(&exit);
        }
        builder_.Bind(&migrateToRawValuesFromInt);
        {
            BRANCH_CIR(builder_.ElementsKindIsNumOrHoleNum(newKind), &migrateToNumbersFromInt, &exit);
            builder_.Bind(&migrateToNumbersFromInt);
            {
                builder_.MigrateFromHoleIntToHoleNumber(object);
                builder_.Jump(&exit);
            }
        }
    }
    builder_.Bind(&migrateOtherKinds);
    {
        Label migrateFromNumber(&builder_);
        Label migrateToHeapValuesFromNum(&builder_);
        Label migrateToRawValuesFromNum(&builder_);
        Label migrateToIntFromNum(&builder_);
        Label migrateToRawValueFromTagged(&builder_);
        BRANCH_CIR(builder_.ElementsKindIsNumOrHoleNum(oldKind), &migrateFromNumber, &migrateToRawValueFromTagged);
        builder_.Bind(&migrateFromNumber);
        {
            BRANCH_CIR(builder_.ElementsKindIsHeapKind(newKind),
                       &migrateToHeapValuesFromNum, &migrateToRawValuesFromNum);
            builder_.Bind(&migrateToHeapValuesFromNum);
            {
                Label migrateToTaggedFromNum(&builder_);
                BRANCH_CIR(builder_.ElementsKindIsHeapKind(newKind), &migrateToTaggedFromNum, &exit);
                builder_.Bind(&migrateToTaggedFromNum);
                {
                    newElements = builder_.MigrateFromRawValueToHeapValues(object, needCOW, builder_.False());
                    builder_.StoreConstOffset(VariableType::JS_ANY(), object, JSObject::ELEMENTS_OFFSET, *newElements);
                    builder_.Jump(&exit);
                }
            }
            builder_.Bind(&migrateToRawValuesFromNum);
            {
                BRANCH_CIR(builder_.ElementsKindIsIntOrHoleInt(newKind), &migrateToIntFromNum, &exit);
                builder_.Bind(&migrateToIntFromNum);
                {
                    builder_.MigrateFromHoleNumberToHoleInt(object);
                    builder_.Jump(&exit);
                }
            }
        }
        builder_.Bind(&migrateToRawValueFromTagged);
        {
            Label migrateToIntFromTagged(&builder_);
            Label migrateToOthersFromTagged(&builder_);
            BRANCH_CIR(builder_.ElementsKindIsIntOrHoleInt(newKind),
                       &migrateToIntFromTagged, &migrateToOthersFromTagged);
            builder_.Bind(&migrateToIntFromTagged);
            {
                newElements = builder_.MigrateFromHeapValueToRawValue(object, needCOW, builder_.True());
                builder_.StoreConstOffset(VariableType::JS_ANY(), object, JSObject::ELEMENTS_OFFSET, *newElements);
                builder_.Jump(&exit);
            }
            builder_.Bind(&migrateToOthersFromTagged);
            {
                Label migrateToNumFromTagged(&builder_);
                BRANCH_CIR(builder_.ElementsKindIsNumOrHoleNum(newKind), &migrateToNumFromTagged, &exit);
                builder_.Bind(&migrateToNumFromTagged);
                {
                    newElements = builder_.MigrateFromHeapValueToRawValue(object, needCOW, builder_.False());
                    builder_.StoreConstOffset(VariableType::JS_ANY(), object, JSObject::ELEMENTS_OFFSET, *newElements);
                    builder_.Jump(&exit);
                }
            }
        }
    }
    builder_.Bind(&exit);
    acc_.ReplaceGate(gate, builder_.GetState(), builder_.GetDepend(), Circuit::NullGate());
}

void TypedHCRLowering::LowerNumberToString(GateRef gate, GateRef glue)
{
    Environment env(gate, circuit_, &builder_);
    DEFVALUE(result, (&builder_), VariableType::JS_ANY(), builder_.Undefined());
    GateRef number = acc_.GetValueIn(gate, 0);
    result = builder_.CallRuntime(glue, RTSTUB_ID(NumberToString),
        Gate::InvalidGateRef, { number }, gate);
    acc_.ReplaceGate(gate, builder_.GetState(), builder_.GetDepend(), *result);
}

void TypedHCRLowering::LowerEcmaObjectCheck(GateRef gate)
{
    Environment env(gate, circuit_, &builder_);
    GateRef value = acc_.GetValueIn(gate, 0);
    GateRef frameState = acc_.GetFrameState(gate);
    builder_.HeapObjectCheck(value, frameState);
    builder_.HeapObjectIsEcmaObjectCheck(value, frameState);
    acc_.ReplaceGate(gate, builder_.GetState(), builder_.GetDepend(), Circuit::NullGate());
}
}  // namespace panda::ecmascript::kungfu
