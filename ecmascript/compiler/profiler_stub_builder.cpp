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

#include "ecmascript/compiler/profiler_stub_builder.h"

#include "ecmascript/compiler/interpreter_stub-inl.h"

namespace panda::ecmascript::kungfu {
void ProfilerStubBuilder::PGOProfiler(GateRef glue, GateRef pc, GateRef func, GateRef profileTypeInfo,
    const std::vector<GateRef> &values, SlotIDFormat format, OperationType type)
{
    if (type == OperationType::TRUE_BRANCH ||
        type == OperationType::FALSE_BRANCH) {
        SlotIDInfo slotIdInfo(pc, SlotIDInfo::SlotIDInfoType::PC);
        PGOProfiler(glue, func, profileTypeInfo, slotIdInfo, values, type);
    } else {
        SlotIDInfo slotIdInfo(pc, format);
        PGOProfiler(glue, func, profileTypeInfo, slotIdInfo, values, type);
    }
}

void ProfilerStubBuilder::PGOProfiler(GateRef glue, GateRef func, GateRef profileTypeInfo,
    GateRef slotId, const std::vector<GateRef> &values, OperationType type)
{
    SlotIDInfo slotIdInfo(slotId, SlotIDInfo::SlotIDInfoType::SLOT_ID);
    PGOProfiler(glue, func, profileTypeInfo, slotIdInfo, values, type);
}

void ProfilerStubBuilder::TryDump(GateRef glue, GateRef func, GateRef profileTypeInfo)
{
    auto env = GetEnvironment();
    Label subEntry(env);
    env->SubCfgEntry(&subEntry);

    Label updatePeriodCounter(env);
    Label exit(env);
    Label needDump(env);

    BRANCH(IsProfileTypeInfoWithBigMethod(profileTypeInfo), &exit, &needDump);
    Bind(&needDump);
    BRANCH(IsProfileTypeInfoDumped(profileTypeInfo), &exit, &updatePeriodCounter);
    Bind(&updatePeriodCounter);
    {
        SetDumpPeriodIndex(glue, profileTypeInfo);
        CallRuntime(glue, RTSTUB_ID(PGODump), { func });
        Jump(&exit);
    }
    Bind(&exit);
    env->SubCfgExit();
}

void ProfilerStubBuilder::TryPreDump(GateRef glue, GateRef func, GateRef profileTypeInfo)
{
    auto env = GetEnvironment();
    Label subEntry(env);
    env->SubCfgEntry(&subEntry);
    Label exit(env);
    Label profiler(env);
    BRANCH(IsProfileTypeInfoHotAndValid(profileTypeInfo), &profiler, &exit);
    Bind(&profiler);
    {
        TryPreDumpInner(glue, func, profileTypeInfo);
        Jump(&exit);
    }
    Bind(&exit);
    env->SubCfgExit();
}

void ProfilerStubBuilder::ProfileOpType(
    GateRef glue, SlotIDInfo slotInfo, GateRef func, GateRef profileTypeInfo, GateRef type)
{
    auto env = GetEnvironment();
    Label subEntry(env);
    env->SubCfgEntry(&subEntry);

    Label exit(env);
    Label profiler(env);
    BRANCH(IsProfileTypeInfoHotAndValid(profileTypeInfo), &profiler, &exit);
    Bind(&profiler);
    {
        Label icSlotValid(env);
        Label uninitialized(env);
        Label compareLabel(env);
        Label updateSlot(env);

        GateRef slotId = GetSlotID(slotInfo);
        GateRef length = GetICLength(profileTypeInfo);
        BRANCH(Int32LessThan(slotId, length), &icSlotValid, &exit);
        Bind(&icSlotValid);
        GateRef slotValue = GetICSlot(glue, profileTypeInfo, slotId);
        DEFVARIABLE(curTaggedSlotValue, VariableType::INT64(), type);
        BRANCH(TaggedIsInt(slotValue), &compareLabel, &uninitialized);
        Bind(&compareLabel);
        {
            GateRef oldTaggedSlotValue = ChangeTaggedPointerToInt64(slotValue);
            curTaggedSlotValue = Int64Or(oldTaggedSlotValue, type);
            BRANCH(Int64Equal(oldTaggedSlotValue, *curTaggedSlotValue), &exit, &updateSlot);
        }
        Bind(&uninitialized);
        {
            // Only when slot value is undefined, it means uninitialized, so we need to update the slot.
            // When the slot value is hole, it means slot is overflow (0xff). Otherwise, do nothing.
            BRANCH(TaggedIsUndefined(slotValue), &updateSlot, &exit);
        }
        Bind(&updateSlot);
        {
            SetICSlot(VariableType::JS_ANY(), glue, profileTypeInfo, slotId, *curTaggedSlotValue);
            TryPreDumpInner(glue, func, profileTypeInfo);
            Jump(&exit);
        }
    }
    Bind(&exit);
    env->SubCfgExit();
}

void ProfilerStubBuilder::ProfileDefineClass(
    GateRef glue, SlotIDInfo slotInfo, GateRef func, GateRef constructor, GateRef profileTypeInfo)
{
    auto env = GetEnvironment();
    Label subEntry(env);
    env->SubCfgEntry(&subEntry);

    Label exit(env);
    Label profiler(env);
    BRANCH(IsProfileTypeInfoHotAndValid(profileTypeInfo), &profiler, &exit);
    Bind(&profiler);
    {
        Label icSlotValid(env);
        Label updateSlot(env);
        Label isHeapObject(env);
        Label isProfileTypeInfoCell0(env);

        GateRef slotId = GetSlotID(slotInfo);
        GateRef length = GetICLength(profileTypeInfo);
        BRANCH(Int32LessThan(slotId, length), &icSlotValid, &exit);
        Bind(&icSlotValid);
        GateRef slotValue = GetICSlot(glue, profileTypeInfo, slotId);
        Branch(TaggedIsHeapObject(slotValue), &isHeapObject, &exit);
        Bind(&isHeapObject);
        Branch(IsProfileTypeInfoCell0(glue, slotValue), &isProfileTypeInfoCell0, &exit);
        Bind(&isProfileTypeInfoCell0);
        GateRef handleOffset = IntPtr(ProfileTypeInfoCell::HANDLE_OFFSET);
        GateRef handle = Load(VariableType::JS_ANY(), glue, slotValue, handleOffset);
        BRANCH(TaggedIsUndefined(handle), &updateSlot, &exit);
        Bind(&updateSlot);
        auto weakCtor = env->GetBuilder()->CreateWeakRef(constructor);
        Store(VariableType::JS_POINTER(), glue, slotValue, handleOffset, weakCtor);
        TryPreDumpInner(glue, func, profileTypeInfo);
        Jump(&exit);
    }
    Bind(&exit);
    env->SubCfgExit();
}

void ProfilerStubBuilder::ProfileCreateObject(
    GateRef glue, SlotIDInfo slotInfo, GateRef func, GateRef newObj, GateRef profileTypeInfo)
{
    auto env = GetEnvironment();
    Label subEntry(env);
    env->SubCfgEntry(&subEntry);
    Label exit(env);

    Label profiler(env);
    BRANCH(IsProfileTypeInfoHotAndValid(profileTypeInfo), &profiler, &exit);
    Bind(&profiler);
    {
        Label icSlotValid(env);
        Label isHeapObject(env);
        Label isWeak(env);
        Label uninitialized(env);
        Label updateSlot(env);

        GateRef slotId = GetSlotID(slotInfo);
        GateRef length = GetICLength(profileTypeInfo);
        BRANCH(Int32LessThan(slotId, length), &icSlotValid, &exit);
        Bind(&icSlotValid);
        auto hclass = LoadHClass(glue, newObj);
        GateRef slotValue = GetICSlot(glue, profileTypeInfo, slotId);
        BRANCH(TaggedIsHeapObject(slotValue), &isHeapObject, &uninitialized);
        Bind(&isHeapObject);
        {
            BRANCH(TaggedIsWeak(slotValue), &isWeak, &updateSlot);
        }
        Bind(&isWeak);
        {
            auto cachedHClass = LoadObjectFromWeakRef(slotValue);
            BRANCH(Equal(cachedHClass, hclass), &exit, &updateSlot);
        }
        Bind(&uninitialized);
        {
            // Only when slot value is undefined, it means uninitialized, so we need to update the slot.
            // When the slot value is hole, it means slot is overflow (0xff). Otherwise, do nothing.
            BRANCH(TaggedIsUndefined(slotValue), &updateSlot, &exit);
        }
        Bind(&updateSlot);
        {
            auto weakCtor = env->GetBuilder()->CreateWeakRef(hclass);
            SetICSlot(VariableType::JS_ANY(), glue, profileTypeInfo, slotId, weakCtor);
            TryPreDumpInner(glue, func, profileTypeInfo);
            Jump(&exit);
        }
    }
    Bind(&exit);
    env->SubCfgExit();
}

void ProfilerStubBuilder::ProfileCall(
    GateRef glue, SlotIDInfo slotInfo, GateRef func, GateRef target, GateRef profileTypeInfo)
{
    auto env = GetEnvironment();
    Label subEntry(env);
    env->SubCfgEntry(&subEntry);

    StartTraceCallDetail(glue, profileTypeInfo, IntToTaggedInt(GetSlotID(slotInfo)));
    IR_IF (IsJSFunction(glue, target)) {
        GateRef targetProfileInfo = GetProfileTypeInfo(glue, target);
        IR_IF (BoolNot(IsProfileTypeInfoHotAndValid(targetProfileInfo))) {
            CallRuntime(glue, RTSTUB_ID(UpdateHotnessCounterWithProf), { target });
        }
        // Deferred compilation
        GateRef tieringDecision = GetTieringDecisionFromFunction(glue, target);
        IR_IF (Int32Equal(tieringDecision,
            Int32(static_cast<int32_t>(TieringDecision::ARKSTEED_REQUESTED)))) {
            CallRuntime(glue, RTSTUB_ID(SubmitJitTask), { target });
        }
        IR_IF (IsProfileTypeInfoHotAndValid(profileTypeInfo)) {
            GateRef slotId = GetSlotID(slotInfo);
            GateRef slotIdNext = Int32Add(slotId, Int32(1));
            GateRef length = GetICLength(profileTypeInfo);
            IR_IF (Int32LessThan(slotIdNext, length)) {
                GateRef slotValue = GetICSlot(glue, profileTypeInfo, slotId);
                IR_IF (TaggedIsHeapObject(slotValue)) {
                    IR_IF (Int64Equal(slotValue, target)) {
                        GateRef callCnt = TaggedGetInt(GetICSlot(glue, profileTypeInfo, slotIdNext));
                        GateRef newCallCnt = Int32Add(callCnt, Int32(1));
                        SetICSlot(VariableType::JS_ANY(), glue, profileTypeInfo, slotIdNext,
                                  IntToTaggedInt(newCallCnt));
                    } IR_ELSE {
                        GateRef nonType = TaggedInt(PGO_BUILTINS_STUB_ID(NONE));
                        SetICSlot(VariableType::JS_ANY(), glue, profileTypeInfo, slotId, nonType);
                        TryPreDumpInner(glue, func, profileTypeInfo);
                    }
                } IR_ELSE {
                    // Only when slot value is undefined, it means uninitialized, so we need to update the slot.
                    // When the slot value is hole, it means slot is overflow (0xff). Otherwise, do nothing.
                    IR_IF (TaggedIsUndefined(slotValue)) {
                        SetICSlot(VariableType::JS_ANY(), glue, profileTypeInfo, slotId, target);
                        SetICSlot(VariableType::JS_ANY(), glue, profileTypeInfo, slotIdNext, IntToTaggedInt(Int32(1)));
                        TryPreDumpInner(glue, func, profileTypeInfo);
                    }
                }
            }
        }
    }
    EndTraceCall(glue);
    env->SubCfgExit();
}

void ProfilerStubBuilder::ProfileGetterSetterCall(GateRef glue, GateRef target)
{
    auto env = GetEnvironment();
    Label subEntry(env);
    env->SubCfgEntry(&subEntry);

    Label exit(env);

    Label targetIsFunction(env);
    BRANCH(IsJSFunction(glue, target), &targetIsFunction, &exit);
    Bind(&targetIsFunction);
    {
        // Deferred compilation
        Label methodIsRequested(env);
        Label notRequested(env);
        GateRef tieringDecision = GetTieringDecisionFromFunction(glue, target);
        BRANCH(Int32Equal(tieringDecision,
            Int32(static_cast<int32_t>(TieringDecision::ARKSTEED_REQUESTED))),
            &methodIsRequested, &notRequested);
        Bind(&methodIsRequested);
        {
            CallRuntime(glue, RTSTUB_ID(SubmitJitTask), { target });
            Jump(&notRequested);
        }
        Bind(&notRequested);
        GateRef targetProfileInfo = GetProfileTypeInfo(glue, target);
        Label targetNonHotness(env);
        BRANCH(TaggedIsUndefined(targetProfileInfo), &targetNonHotness, &exit);
        Bind(&targetNonHotness);
        {
            CallRuntime(glue, RTSTUB_ID(UpdateHotnessCounterWithProf), { target });
            Jump(&exit);
        }
    }
    Bind(&exit);
    env->SubCfgExit();
}

GateRef ProfilerStubBuilder::TryGetBuiltinFunctionId(GateRef glue, GateRef target)
{
    auto env = GetEnvironment();
    Label subEntry(env);
    env->SubCfgEntry(&subEntry);
    Label targetIsFunction(env);
    Label exit(env);

    DEFVARIABLE(functionId, VariableType::INT32(), Int32(PGO_BUILTINS_STUB_ID(NONE)));

    BRANCH(IsJSFunction(glue, target), &targetIsFunction, &exit);
    Bind(&targetIsFunction);
    {
        auto builtinsId = env->GetBuilder()->GetBuiltinsId(glue, target);
        functionId = Int32Mul(TruncInt64ToInt32(builtinsId), Int32(-1));
        Jump(&exit);
    }
    Bind(&exit);
    auto ret = *functionId;
    env->SubCfgExit();
    return ret;
}

void ProfilerStubBuilder::ProfileNativeCall(
    GateRef glue, SlotIDInfo slotInfo, GateRef func, GateRef target, GateRef profileTypeInfo)
{
    auto env = GetEnvironment();
    Label subEntry(env);
    env->SubCfgEntry(&subEntry);

    Label exit(env);
    Label currentIsHot(env);

    BRANCH(IsProfileTypeInfoHotAndValid(profileTypeInfo), &currentIsHot, &exit);
    Bind(&currentIsHot);
    {
        Label icSlotValid(env);
        Label updateSlot(env);
        Label initSlot(env);
        Label sameValueCheck(env);
        Label invalidate(env);
        Label notOverflow(env);

        GateRef slotId = GetSlotID(slotInfo);
        GateRef length = GetICLength(profileTypeInfo);
        BRANCH(Int32LessThan(slotId, length), &icSlotValid, &exit);
        Bind(&icSlotValid);
        GateRef slotValue = GetICSlot(glue, profileTypeInfo, slotId);
        BRANCH(TaggedIsHole(slotValue), &exit, &notOverflow); // hole -- slot is overflow
        Bind(&notOverflow);
        BRANCH(TaggedIsInt(slotValue), &updateSlot, &initSlot);
        Bind(&updateSlot);
        GateRef oldId = TaggedGetInt(slotValue);
        BRANCH(Int32Equal(oldId, Int32(PGO_BUILTINS_STUB_ID(NONE))), &exit, &sameValueCheck);
        Bind(&sameValueCheck);
        {
            GateRef newId = TryGetBuiltinFunctionId(glue, target);
            BRANCH(Int32Equal(oldId, newId), &exit, &invalidate);
        }
        Bind(&invalidate);
        {
            GateRef invalidId = Int32(PGO_BUILTINS_STUB_ID(NONE));
            SetICSlot(VariableType::JS_ANY(), glue, profileTypeInfo, slotId, IntToTaggedInt(invalidId));
            TryPreDumpInner(glue, func, profileTypeInfo);
            Jump(&exit);
        }
        Bind(&initSlot);
        {
            GateRef newId = TryGetBuiltinFunctionId(glue, target);
            SetICSlot(VariableType::JS_ANY(), glue, profileTypeInfo, slotId, IntToTaggedInt(newId));
            TryPreDumpInner(glue, func, profileTypeInfo);
            Jump(&exit);
        }
    }
    Bind(&exit);
    env->SubCfgExit();
}

GateRef ProfilerStubBuilder::IsProfileTypeInfoDumped(GateRef profileTypeInfo, ProfileOperation callback)
{
    if (callback.IsEmpty()) {
        return Boolean(true);
    }
    return IsProfileTypeInfoDumped(profileTypeInfo);
}

GateRef ProfilerStubBuilder::UpdateTrackTypeInPropAttr(GateRef attr, GateRef value, ProfileOperation callback)
{
    if (callback.IsEmpty()) {
        return attr;
    }
    auto env = GetEnvironment();
    Label entry(env);
    env->SubCfgEntry(&entry);

    GateRef oldTrackType = GetTrackTypeInPropAttr(attr);
    DEFVARIABLE(newTrackType, VariableType::INT32(), Int32(static_cast<int32_t>(TrackType::TAGGED)));
    DEFVARIABLE(result, VariableType::INT64(), attr);

    Label exit(env);
    Label judgeValue(env);
    BRANCH(Equal(oldTrackType, Int32(static_cast<int32_t>(TrackType::TAGGED))), &exit, &judgeValue);
    Bind(&judgeValue);
    {
        newTrackType = TaggedToTrackType(value);
        Label update(env);
        Label merge(env);
        BRANCH(Int32Equal(*newTrackType, Int32(static_cast<int32_t>(TrackType::TAGGED))), &update, &merge);
        Bind(&merge);
        {
            newTrackType = Int32Or(oldTrackType, *newTrackType);
            BRANCH(Int32Equal(oldTrackType, *newTrackType), &exit, &update);
        }
        Bind(&update);
        {
            result = SetTrackTypeInPropAttr(attr, *newTrackType);
            Jump(&exit);
        }
    }
    Bind(&exit);
    auto ret = *result;
    env->SubCfgExit();
    return ret;
}

void ProfilerStubBuilder::UpdatePropAttrIC(
    GateRef glue, GateRef receiver, GateRef value, GateRef handler, ProfileOperation callback)
{
    if (callback.IsEmpty()) {
        return;
    }
    auto env = GetEnvironment();
    Label entry(env);
    env->SubCfgEntry(&entry);
    Label exit(env);
    Label handleUnShared(env);
    Label updateLayout(env);

    GateRef attrIndex = HandlerBaseGetAttrIndex(handler);
    GateRef hclass = LoadHClass(glue, receiver);
    GateRef layout = GetLayoutFromHClass(glue, hclass);
    GateRef attr = GetPropAttrFromLayoutInfo(glue, layout, attrIndex);
    GateRef newAttr = UpdateTrackTypeInPropAttr(attr, value, callback);
    BRANCH(IsJSShared(glue, receiver), &exit, &handleUnShared);
    Bind(&handleUnShared);
    {
        BRANCH(Equal(attr, newAttr), &exit, &updateLayout);
        Bind(&updateLayout);
        {
            UpdateFieldType(glue, LoadHClass(glue, receiver), newAttr);
            callback.TryPreDump();
            Jump(&exit);
        }
    }
    Bind(&exit);
    env->SubCfgExit();
}

void ProfilerStubBuilder::UpdatePropAttrWithValue(GateRef glue, GateRef receiver, GateRef attr,
                                                  GateRef value, ProfileOperation callback)
{
    if (callback.IsEmpty()) {
        return;
    }
    auto env = GetEnvironment();
    Label entry(env);
    env->SubCfgEntry(&entry);
    Label exit(env);
    Label updateLayout(env);
    Label isNotJSShared(env);
    BRANCH(IsJSShared(glue, receiver), &exit, &isNotJSShared);
    Bind(&isNotJSShared);
    GateRef newAttr = UpdateTrackTypeInPropAttr(attr, value, callback);
    BRANCH(Equal(attr, newAttr), &exit, &updateLayout);
    Bind(&updateLayout);
    {
        UpdateFieldType(glue, LoadHClass(glue, receiver), newAttr);
        Jump(&exit);
    }
    Bind(&exit);
    env->SubCfgExit();
}

GateRef ProfilerStubBuilder::TaggedToTrackType(GateRef value)
{
    auto env = GetEnvironment();
    Label entry(env);
    env->SubCfgEntry(&entry);

    DEFVARIABLE(newTrackType, VariableType::INT32(), Int32(static_cast<int32_t>(TrackType::TAGGED)));
    Label exit(env);
    Label isInt(env);
    Label notInt(env);
    BRANCH(TaggedIsInt(value), &isInt, &notInt);
    Bind(&isInt);
    {
        newTrackType = Int32(static_cast<int32_t>(TrackType::INT));
        Jump(&exit);
    }
    Bind(&notInt);
    {
        Label isObject(env);
        Label isDouble(env);
        BRANCH(TaggedIsObject(value), &isObject, &isDouble);
        Bind(&isObject);
        {
            newTrackType = Int32(static_cast<int32_t>(TrackType::TAGGED));
            Jump(&exit);
        }
        Bind(&isDouble);
        {
            newTrackType = Int32(static_cast<int32_t>(TrackType::DOUBLE));
            Jump(&exit);
        }
    }
    Bind(&exit);
    auto ret = *newTrackType;
    env->SubCfgExit();
    return ret;
}

void ProfilerStubBuilder::ProfileBranch(
    GateRef glue, SlotIDInfo slotInfo, GateRef func, GateRef profileTypeInfo, bool isTrue)
{
    auto env = GetEnvironment();
    Label subEntry(env);
    env->SubCfgEntry(&subEntry);
    Label profiler(env);
    Label icSlotValid(env);
    Label hasSlot(env);
    Label compareLabel(env);
    Label updateSlot(env);
    Label preProfile(env);
    Label needUpdate(env);
    Label exit(env);
    DEFVARIABLE(oldPrama, VariableType::INT32(), Int32(PGOSampleType::None()));
    DEFVARIABLE(newTrue, VariableType::INT32(), isTrue ? Int32(1) : Int32(0));
    DEFVARIABLE(newFalse, VariableType::INT32(), isTrue ? Int32(0) : Int32(1));

    BRANCH(IsProfileTypeInfoHotAndValid(profileTypeInfo), &profiler, &exit);
    Bind(&profiler);
    {
        GateRef slotId = GetSlotID(slotInfo);
        GateRef length = GetICLength(profileTypeInfo);
        BRANCH(Int32LessThan(slotId, length), &icSlotValid, &exit);
        Bind(&icSlotValid);
        GateRef slotValue = GetICSlot(glue, profileTypeInfo, slotId);
        BRANCH(TaggedIsHole(slotValue), &exit, &hasSlot); // ishole -- isundefined
        Bind(&hasSlot);
        {
            Label uninitialized(env);
            BRANCH(TaggedIsInt(slotValue), &compareLabel, &uninitialized);
            Bind(&compareLabel);
            {
                GateRef oldSlotValue = TaggedGetInt(slotValue);
                GateRef oldTrue = Int32LSR(oldSlotValue, Int32(PGOSampleType::WEIGHT_TRUE_START_BIT));
                GateRef oldFalse = Int32LSR(oldSlotValue, Int32(PGOSampleType::WEIGHT_START_BIT));
                oldFalse = Int32And(oldFalse, Int32(PGOSampleType::WEIGHT_MASK));
                oldPrama = Int32And(oldSlotValue, Int32(PGOSampleType::AnyType()));
                auto condition = BitAnd(Int32LessThan(oldTrue, Int32(PGOSampleType::WEIGHT_THRESHOLD)),
                                        Int32LessThan(oldFalse, Int32(PGOSampleType::WEIGHT_THRESHOLD)));
                BRANCH(condition, &needUpdate, &exit); // WEIGHT_THRESHOLD: 2047 limit
                Bind(&needUpdate);
                {
                    newTrue = Int32Add(*newTrue, oldTrue);
                    newFalse = Int32Add(*newFalse, oldFalse);
                    Jump(&updateSlot);
                }
            }
            Bind(&uninitialized);
            {
                // Only when slot value is undefined, it means uninitialized, so we need to update the slot.
                // When the slot value is hole, it means slot is overflow (0xff). Otherwise, do nothing.
                BRANCH(TaggedIsUndefined(slotValue), &updateSlot, &exit);
            }
            Bind(&updateSlot);
            {
                GateRef newSlotValue =
                    Int32Or(*oldPrama, Int32LSL(*newTrue, Int32(PGOSampleType::WEIGHT_TRUE_START_BIT)));
                newSlotValue = Int32Or(newSlotValue, Int32LSL(*newFalse, Int32(PGOSampleType::WEIGHT_START_BIT)));
                SetICSlot(VariableType::JS_ANY(), glue, profileTypeInfo, slotId, IntToTaggedInt(newSlotValue));
                auto isFinal = BitOr(Int32Equal(*newTrue, Int32(PGOSampleType::WEIGHT_THRESHOLD)),
                                     Int32Equal(*newFalse, Int32(PGOSampleType::WEIGHT_THRESHOLD)));
                BRANCH(isFinal, &preProfile, &exit);
            }
            Bind(&preProfile);
            {
                TryPreDumpInner(glue, func, profileTypeInfo);
                Jump(&exit);
            }
        }
    }
    Bind(&exit);
    env->SubCfgExit();
}

void ProfilerStubBuilder::TryPreDumpInner(GateRef glue, GateRef func, GateRef profileTypeInfo)
{
    auto env = GetEnvironment();
    Label subEntry(env);
    env->SubCfgEntry(&subEntry);
    Label setPreDump(env);
    Label setPreDumpPeriodIndex(env);
    Label setAndAddPredumpWorkList(env);
    Label exit(env);
    BRANCH(IsProfileTypeInfoPreDumped(profileTypeInfo), &exit, &setPreDump);
    Bind(&setPreDump);
    {
        BRANCH(IsProfileTypeInfoInit(profileTypeInfo), &setAndAddPredumpWorkList, &setPreDumpPeriodIndex);
    }
    Bind(&setPreDumpPeriodIndex);
    {
        SetPreDumpPeriodIndex(glue, profileTypeInfo);
        Jump(&exit);
    }
    Bind(&setAndAddPredumpWorkList);
    {
        SetPreDumpPeriodIndex(glue, profileTypeInfo);
        CallRuntime(glue, RTSTUB_ID(PGOPreDump), { func });
        Jump(&exit);
    }
    Bind(&exit);
    env->SubCfgExit();
}

GateRef ProfilerStubBuilder::GetIterationFunctionId(GateRef glue, GateRef iterator)
{
    auto env = GetEnvironment();
    Label subEntry(env);
    env->SubCfgEntry(&subEntry);
    Label exit(env);

    DEFVARIABLE(functionId, VariableType::INT32(), Int32(PGO_BUILTINS_STUB_ID(NONE)));
    DEFVARIABLE(maybeFunc, VariableType::JS_ANY(), Undefined());
    Label isArrayProtoValues(env);
    Label notArrayProtoValues(env);
    Label isSetProtoValues(env);
    Label notSetProtoValues(env);
    Label isMapProtoEntries(env);
    Label notMapProtoEntries(env);
    Label isStringProtoIter(env);
    Label notStringProtoIter(env);
    Label isTypedArrayProtoValues(env);

    GateRef globalEnv = GetCurrentGlobalEnv();
    maybeFunc = GetGlobalEnvValue(VariableType::JS_ANY(), glue, globalEnv,
                                  GlobalEnv::ARRAY_PROTO_VALUES_FUNCTION_INDEX);
    BRANCH(Int64Equal(iterator, *maybeFunc), &isArrayProtoValues, &notArrayProtoValues);
    Bind(&isArrayProtoValues);
    {
        functionId = Int32(PGO_BUILTINS_STUB_ID(ArrayProtoIterator));
        Jump(&exit);
    }
    Bind(&notArrayProtoValues);
    maybeFunc = GetGlobalEnvValue(VariableType::JS_ANY(), glue, globalEnv,
                                  GlobalEnv::SET_PROTO_VALUES_FUNCTION_INDEX);
    BRANCH(Int64Equal(iterator, *maybeFunc), &isSetProtoValues, &notSetProtoValues);
    Bind(&isSetProtoValues);
    {
        functionId = Int32(PGO_BUILTINS_STUB_ID(SetProtoIterator));
        Jump(&exit);
    }
    Bind(&notSetProtoValues);
    maybeFunc = GetGlobalEnvValue(VariableType::JS_ANY(), glue, globalEnv,
                                  GlobalEnv::MAP_PROTO_ENTRIES_FUNCTION_INDEX);
    BRANCH(Int64Equal(iterator, *maybeFunc), &isMapProtoEntries, &notMapProtoEntries);
    Bind(&isMapProtoEntries);
    {
        functionId = Int32(PGO_BUILTINS_STUB_ID(MapProtoIterator));
        Jump(&exit);
    }
    Bind(&notMapProtoEntries);
    maybeFunc = GetGlobalEnvValue(VariableType::JS_ANY(), glue, globalEnv,
                                  GlobalEnv::STRING_PROTO_ITER_FUNCTION_INDEX);
    BRANCH(Int64Equal(iterator, *maybeFunc), &isStringProtoIter, &notStringProtoIter);
    Bind(&isStringProtoIter);
    {
        functionId = Int32(PGO_BUILTINS_STUB_ID(StringProtoIterator));
        Jump(&exit);
    }
    Bind(&notStringProtoIter);
    maybeFunc = GetGlobalEnvValue(VariableType::JS_ANY(), glue, globalEnv,
                                  GlobalEnv::TYPED_ARRAY_PROTO_VALUES_FUNCTION_INDEX);
    BRANCH(Int64Equal(iterator, *maybeFunc), &isTypedArrayProtoValues, &exit);
    Bind(&isTypedArrayProtoValues);
    {
        functionId = Int32(PGO_BUILTINS_STUB_ID(TypedArrayProtoIterator));
        Jump(&exit);
    }
    Bind(&exit);
    auto ret = *functionId;
    env->SubCfgExit();
    return ret;
}

void ProfilerStubBuilder::ProfileGetIterator(
    GateRef glue, SlotIDInfo slotInfo, GateRef func, GateRef iterator, GateRef profileTypeInfo)
{
    auto env = GetEnvironment();
    Label subEntry(env);
    env->SubCfgEntry(&subEntry);

    Label exit(env);
    Label profiler(env);
    BRANCH(IsProfileTypeInfoHotAndValid(profileTypeInfo), &profiler, &exit);
    Bind(&profiler);
    {
        Label icSlotValid(env);
        Label updateSlot(env);
        Label initSlot(env);
        Label sameValueCheck(env);
        Label invalidate(env);
        Label notOverflow(env);

        GateRef slotId = GetSlotID(slotInfo);
        GateRef length = GetICLength(profileTypeInfo);
        BRANCH(Int32LessThan(slotId, length), &icSlotValid, &exit);
        Bind(&icSlotValid);
        GateRef slotValue = GetICSlot(glue, profileTypeInfo, slotId);
        BRANCH(TaggedIsHole(slotValue), &exit, &notOverflow); // hole -- slot is overflow
        Bind(&notOverflow);
        BRANCH(TaggedIsInt(slotValue), &updateSlot, &initSlot);
        Bind(&updateSlot);
        GateRef oldIterKind = TaggedGetInt(slotValue);
        BRANCH(Int32Equal(oldIterKind, Int32(PGO_BUILTINS_STUB_ID(NONE))),
            &exit, &sameValueCheck);
        Bind(&sameValueCheck);
        {
            GateRef newIterKind = GetIterationFunctionId(glue, iterator);
            BRANCH(Int32Equal(oldIterKind, newIterKind), &exit, &invalidate);
        }
        Bind(&invalidate);
        {
            GateRef invalidKind = Int32(PGO_BUILTINS_STUB_ID(NONE));
            SetICSlot(VariableType::JS_ANY(), glue, profileTypeInfo, slotId, IntToTaggedInt(invalidKind));
            TryPreDumpInner(glue, func, profileTypeInfo);
            Jump(&exit);
        }
        Bind(&initSlot);
        {
            GateRef newIterKind = GetIterationFunctionId(glue, iterator);
            SetICSlot(VariableType::JS_ANY(), glue, profileTypeInfo, slotId, IntToTaggedInt(newIterKind));
            TryPreDumpInner(glue, func, profileTypeInfo);
            Jump(&exit);
        }
    }
    Bind(&exit);
    env->SubCfgExit();
}

GateRef ProfilerStubBuilder::GetSlotID(const SlotIDInfo &slotInfo)
{
    auto slotType = slotInfo.GetSlotType();
    if (slotType == SlotIDInfo::SlotIDInfoType::SLOT_ID) {
        return slotInfo.GetSlotID();
    }
    if (slotType == SlotIDInfo::SlotIDInfoType::PC) {
        // for PROFILE_BRANCH
        return ZExtInt8ToInt32(LoadPrimitive(VariableType::INT8(), slotInfo.GetPC(), IntPtr(1)));
    }
    ASSERT(slotType == SlotIDInfo::SlotIDInfoType::PC_FORMAT);
    auto format = slotInfo.GetFormat();
    auto pc = slotInfo.GetPC();
    if (format == SlotIDFormat::IMM16) {
        auto hight = LoadPrimitive(VariableType::INT8(), pc, IntPtr(2)); // 2 : skip 1 byte of bytecode
        hight = Int16LSL(ZExtInt8ToInt16(hight), Int16(8)); // 8 : set as high 8 bits
        auto low = LoadPrimitive(VariableType::INT8(), pc, IntPtr(1));
        auto result = Int16Add(hight, ZExtInt8ToInt16(low));
        return ZExtInt16ToInt32(result);
    } else if (format == SlotIDFormat::PREF_IMM8) {
        return ZExtInt8ToInt32(LoadPrimitive(VariableType::INT8(), pc, IntPtr(2))); // 2 : skip 1 byte of bytecode
    }
    return ZExtInt8ToInt32(LoadPrimitive(VariableType::INT8(), pc, IntPtr(1)));
}

GateRef ProfilerStubBuilder::IsProfileTypeInfoInit(GateRef profileTypeInfo)
{
    GateRef count = LoadPrimitive(VariableType::INT32(), profileTypeInfo, IntPtr(ProfileTypeInfo::PERIOD_INDEX_OFFSET));
    return Int32Equal(count, Int32(ProfileTypeInfo::INITIAL_PERIOD_INDEX));
}

GateRef ProfilerStubBuilder::IsProfileTypeInfoDumped(GateRef profileTypeInfo)
{
    GateRef count = LoadPrimitive(VariableType::INT32(), profileTypeInfo, IntPtr(ProfileTypeInfo::PERIOD_INDEX_OFFSET));
    return Int32Equal(count, Int32(ProfileTypeInfo::DUMP_PERIOD_INDEX));
}

GateRef ProfilerStubBuilder::IsProfileTypeInfoPreDumped(GateRef profileTypeInfo)
{
    GateRef count = LoadPrimitive(VariableType::INT32(), profileTypeInfo, IntPtr(ProfileTypeInfo::PERIOD_INDEX_OFFSET));
    return Int32Equal(count, Int32(ProfileTypeInfo::PRE_DUMP_PERIOD_INDEX));
}

GateRef ProfilerStubBuilder::IsProfileTypeInfoWithBigMethod(GateRef profileTypeInfo)
{
    GateRef count = LoadPrimitive(VariableType::INT32(), profileTypeInfo, IntPtr(ProfileTypeInfo::PERIOD_INDEX_OFFSET));
    return Int32Equal(count, Int32(ProfileTypeInfo::BIG_METHOD_PERIOD_INDEX));
}

GateRef ProfilerStubBuilder::IsProfileTypeInfoHotAndValid(GateRef profileTypeInfo)
{
    auto env = GetEnvironment();
    Label subEntry(env);
    env->SubCfgEntry(&subEntry);
    Label exit(env);
    Label isHot(env);
    DEFVARIABLE(res, VariableType::BOOL(), Boolean(false));
    BRANCH(TaggedIsUndefined(profileTypeInfo), &exit, &isHot);
    Bind(&isHot);
    {
        res = BoolNot(IsProfileTypeInfoWithBigMethod(profileTypeInfo));
        Jump(&exit);
    }
    Bind(&exit);
    auto ret = *res;
    env->SubCfgExit();
    return ret;
}

void ProfilerStubBuilder::SetDumpPeriodIndex(GateRef glue, GateRef profileTypeInfo)
{
    Store(VariableType::INT32(), glue, profileTypeInfo, IntPtr(ProfileTypeInfo::PERIOD_INDEX_OFFSET),
          Int32(ProfileTypeInfo::DUMP_PERIOD_INDEX));
}

void ProfilerStubBuilder::SetPreDumpPeriodIndex(GateRef glue, GateRef profileTypeInfo)
{
    Store(VariableType::INT32(), glue, profileTypeInfo, IntPtr(ProfileTypeInfo::PERIOD_INDEX_OFFSET),
          Int32(ProfileTypeInfo::PRE_DUMP_PERIOD_INDEX));
}

GateRef ProfilerStubBuilder::GetTieringDecisionFromFunction(GateRef glue, GateRef func)
{
    // Matches Method::GetTieringDecision: the tiering state machine reuses the
    // deopt count field.
    GateRef method = GetMethodFromJSFunctionOrProxy(glue, func);
    GateRef extraLiteralInfo = LoadPrimitive(VariableType::INT64(), method,
        IntPtr(Method::EXTRA_LITERAL_INFO_OFFSET));
    GateRef decision = Int64And(
        Int64LSR(extraLiteralInfo, Int64(Method::DeoptCountBits::START_BIT)),
        Int64((1ULL << Method::DEOPT_THRESHOLD_BITS) - 1));
    return TruncInt64ToInt32(decision);
}

void ProfilerStubBuilder::SetTieringDecisionFromFunction(GateRef glue, GateRef func, GateRef decision)
{
    GateRef method = GetMethodFromJSFunctionOrProxy(glue, func);
    GateRef extraLiteralInfo = LoadPrimitive(VariableType::INT64(), method,
        IntPtr(Method::EXTRA_LITERAL_INFO_OFFSET));
    GateRef clearedBits = Int64And(extraLiteralInfo,
        Int64(static_cast<int64_t>(~Method::DeoptCountBits::Mask())));
    GateRef newBits = Int64LSL(ZExtInt32ToInt64(decision),
        Int64(static_cast<int64_t>(Method::DeoptCountBits::START_BIT)));
    Store(VariableType::INT64(), glue, method, IntPtr(Method::EXTRA_LITERAL_INFO_OFFSET),
        Int64Or(clearedBits, newBits));
}

void ProfilerStubBuilder::ResetTieringForRecompile(GateRef glue, GateRef func, GateRef profileTypeInfoCell)
{
    Store(VariableType::JS_POINTER(), glue, profileTypeInfoCell,
        IntPtr(ProfileTypeInfoCell::MACHINE_CODE_OFFSET), Hole());
    SetTieringDecisionFromFunction(glue, func, Int32(static_cast<int32_t>(TieringDecision::PREHEATED)));
    CallRuntime(glue, RTSTUB_ID(RefreshHotnessCounter), { func });
}

GateRef ProfilerStubBuilder::IsCompiledOrTryCompile(GateRef glue, GateRef func, GateRef profileTypeInfo,
                                                    ProfileOperation callback, GateRef pc)
{
    if (callback.IsEmpty() && callback.IsJitEmpty()) {
        return Boolean(true);
    }
    return IsCompiledOrTryCompile(glue, func, profileTypeInfo, pc);
}

void ProfilerStubBuilder::SetInvocationCnt(GateRef glue, GateRef profileTypeInfo, GateRef invocationCnt)
{
    Store(VariableType::INT32(), glue, profileTypeInfo, IntPtr(ProfileTypeInfo::INVOCATION_COUNT_OFFSET),
          invocationCnt);
}

GateRef ProfilerStubBuilder::GetInvocationCnt(GateRef profileTypeInfo)
{
    return LoadPrimitive(VariableType::INT32(), profileTypeInfo, IntPtr(ProfileTypeInfo::INVOCATION_COUNT_OFFSET));
}

GateRef ProfilerStubBuilder::IsCompiledOrTryCompile(GateRef glue, GateRef func, GateRef profileTypeInfo,
                                                    [[maybe_unused]] GateRef pc)
{
    GateRef decision = GetTieringDecisionFromFunction(glue, func);

    GateRef invocationCnt = GetInvocationCnt(profileTypeInfo);
    GateRef newInvocationCnt = Int32Add(invocationCnt, Int32(1));
    SetInvocationCnt(glue, profileTypeInfo, newInvocationCnt);

    return Int32Equal(decision, Int32(static_cast<int32_t>(TieringDecision::DISABLED)));
}

void ProfilerStubBuilder::PGOProfiler(GateRef glue, GateRef func, GateRef profileTypeInfo,
    SlotIDInfo slotIdInfo, const std::vector<GateRef> &values, OperationType type)
{
    switch (type) {
        case OperationType::CALL:
            ProfileCall(glue, slotIdInfo, func, values[0], profileTypeInfo);
            break;
        case OperationType::NATIVE_CALL:
            ProfileNativeCall(glue, slotIdInfo, func, values[0], profileTypeInfo);
            break;
        case OperationType::GETTER_SETTER_CALL:
            ProfileGetterSetterCall(glue, values[0]);
            break;
        case OperationType::OPERATION_TYPE:
            ProfileOpType(glue, slotIdInfo, func, profileTypeInfo, values[0]);
            break;
        case OperationType::DEFINE_CLASS:
            ProfileDefineClass(glue, slotIdInfo, func, values[0], profileTypeInfo);
            break;
        case OperationType::CREATE_OBJECT:
            ProfileCreateObject(glue, slotIdInfo, func, values[0], profileTypeInfo);
            break;
        case OperationType::TRY_DUMP:
            TryDump(glue, func, profileTypeInfo);
            break;
        case OperationType::TRY_PREDUMP:
            TryPreDump(glue, func, profileTypeInfo);
            break;
        case OperationType::TRUE_BRANCH:
            ProfileBranch(glue, slotIdInfo, func, profileTypeInfo, true);
            break;
        case OperationType::FALSE_BRANCH:
            ProfileBranch(glue, slotIdInfo, func, profileTypeInfo, false);
            break;
        case OperationType::ITERATOR_FUNC_KIND:
            ProfileGetIterator(glue, slotIdInfo, func, values[0], profileTypeInfo);
            break;
        default:
            break;
    }
}
} // namespace panda::ecmascript::kungfu
