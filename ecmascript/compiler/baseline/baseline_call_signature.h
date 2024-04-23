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

#ifndef ECMASCRIPT_BASELINE_COMPILER_CALL_SIGNATURE_H
#define ECMASCRIPT_BASELINE_COMPILER_CALL_SIGNATURE_H

#include "ecmascript/compiler/call_signature.h"
#include "ecmascript/compiler/baseline/baseline_compiler_builtins.h"

namespace panda::ecmascript::kungfu {
enum class BaselineCallInputs : size_t {
    GLUE = 0,
    SP,
    OTHERS,
};

#define DEFINE_PARAMETER_INDEX(name, ...)                          \
enum class name##CallSignature::ParameterIndex : uint8_t {         \
    __VA_ARGS__,                                                   \
    PARAMETER_COUNT,                                               \
};

#define DECL_BASELINE_CALL_SIGNATURE(name)                         \
class name##CallSignature final {                                  \
public:                                                            \
    enum class ParameterIndex : uint8_t;                           \
    static void Initialize(CallSignature *descriptor);             \
};

#define BASELINE_STUB_CALL_SIGNATURE_COMMON_SET();                         \
    callSign->SetParameters(params.data());                                \
    callSign->SetTargetKind(CallSignature::TargetKind::BASELINE_STUB);     \
    callSign->SetCallConv(CallSignature::CallConv::CCallConv);

static constexpr size_t FIRST_PARAMETER = 0;
static constexpr size_t SECOND_PARAMETER = 1;
static constexpr size_t THIRD_PARAMETER = 2;
static constexpr size_t FOURTH_PARAMETER = 3;
static constexpr size_t FIFTH_PARAMETER = 4;
static constexpr size_t SIXTH_PARAMETER = 5;
static constexpr size_t SEVENTH_PARAMETER = 6;
static constexpr size_t EGIHTH_PARAMETER = 7;
static constexpr size_t NINTH_PARAMETER = 8;
BASELINE_COMPILER_BUILTIN_LIST(DECL_BASELINE_CALL_SIGNATURE)

// describe the parameters of the baseline builtins
DEFINE_PARAMETER_INDEX(BaselineLdObjByName,
    GLUE, SP, ACC, PROFILE_TYPE_INFO, STRING_ID, SLOT_ID)

DEFINE_PARAMETER_INDEX(BaselineTryLdGLobalByNameImm8ID16,
    GLUE, SP, ACC, PROFILE_TYPE_INFO, STRING_ID, SLOT_ID)

DEFINE_PARAMETER_INDEX(BaselineStToGlobalRecordImm16ID16,
    GLUE, SP, ACC, STRING_ID)

DEFINE_PARAMETER_INDEX(BaselineLdaStrID16,
    GLUE, SP, ACC, STRING_ID)

DEFINE_PARAMETER_INDEX(BaselineCallArg1Imm8V8,
    GLUE, SP, ACC, ARG0, HOTNESS_COUNTER)

DEFINE_PARAMETER_INDEX(BaselineJmpImm16,
    GLUE, FUNC, PROFILE_TYPE_INFO, HOTNESS_COUNTER, OFFSET)
DEFINE_PARAMETER_INDEX(BaselineLdsymbol,
    GLUE, ACC)
DEFINE_PARAMETER_INDEX(BaselineLdglobal,
    GLUE, ACC)
DEFINE_PARAMETER_INDEX(BaselinePoplexenv,
    GLUE, SP)
DEFINE_PARAMETER_INDEX(BaselineGetunmappedargs,
    GLUE, ACC, SP)
DEFINE_PARAMETER_INDEX(BaselineAsyncfunctionenter,
    GLUE, ACC)
DEFINE_PARAMETER_INDEX(BaselineCreateasyncgeneratorobjV8,
    GLUE, GEN_FUNC, ACC)
DEFINE_PARAMETER_INDEX(BaselineDebugger,
    GLUE)
DEFINE_PARAMETER_INDEX(BaselineGetpropiterator,
    GLUE, ACC)
DEFINE_PARAMETER_INDEX(BaselineGetiteratorImm8,
    GLUE, ACC)
DEFINE_PARAMETER_INDEX(BaselineGetiteratorImm16,
    GLUE, ACC)
DEFINE_PARAMETER_INDEX(BaselineCloseiteratorImm8V8,
    GLUE, ITER)
DEFINE_PARAMETER_INDEX(BaselineCloseiteratorImm16V8,
    GLUE, ITER)
DEFINE_PARAMETER_INDEX(BaselineAsyncgeneratorresolveV8V8V8,
    GLUE, SP, OFFSET, V0, V1, V2)
DEFINE_PARAMETER_INDEX(BaselineCreateemptyobject,
    GLUE, ACC)
DEFINE_PARAMETER_INDEX(BaselineCreateemptyarrayImm8,
    GLUE, SP, TRACE_ID, PROFILE_TYPE_INFO, SLOTID)
DEFINE_PARAMETER_INDEX(BaselineCreateemptyarrayImm16,
    GLUE, SP, TRACE_ID, PROFILE_TYPE_INFO, SLOTID)
DEFINE_PARAMETER_INDEX(BaselineCreategeneratorobjV8,
    GLUE, GEN_FUNC)
DEFINE_PARAMETER_INDEX(BaselineCreateiterresultobjV8V8,
    GLUE, VALUE, FLAG)
DEFINE_PARAMETER_INDEX(BaselineCreateobjectwithexcludedkeysImm8V8V8,
    GLUE, NUMKEYS, OBJ, FIRST_ARG_REG_IDX)
DEFINE_PARAMETER_INDEX(BaselineCallthis0Imm8V8,
    GLUE, SP, ACC, THIS_VALUE, HOTNESS_COUNTER)
DEFINE_PARAMETER_INDEX(BaselineCreatearraywithbufferImm8Id16,
    GLUE, SP, TRACE_ID, PROFILE_TYPE_INFO, IMM, SLOTID)
DEFINE_PARAMETER_INDEX(BaselineCreatearraywithbufferImm16Id16,
    GLUE, SP, TRACE_ID, PROFILE_TYPE_INFO, IMM, SLOTID)
DEFINE_PARAMETER_INDEX(BaselineCallthis1Imm8V8V8,
    GLUE, SP, ACC, THIS_VALUE, A0_VALUE, HOTNESS_COUNTER)
DEFINE_PARAMETER_INDEX(BaselineCallthis2Imm8V8V8V8,
    GLUE, SP, THIS_VALUE, A0_VALUE, A1_VALUE)
DEFINE_PARAMETER_INDEX(BaselineCreateobjectwithbufferImm8Id16,
    GLUE, SP, IMM)
DEFINE_PARAMETER_INDEX(BaselineCreateobjectwithbufferImm16Id16,
    GLUE, SP, IMM)
DEFINE_PARAMETER_INDEX(BaselineCreateregexpwithliteralImm8Id16Imm8,
    GLUE, SP, STRING_ID, FLAGS)
DEFINE_PARAMETER_INDEX(BaselineCreateregexpwithliteralImm16Id16Imm8,
    GLUE, SP, STRING_ID, FLAGS)
DEFINE_PARAMETER_INDEX(BaselineNewobjapplyImm8V8,
    GLUE, ACC, FUNC)
DEFINE_PARAMETER_INDEX(BaselineNewobjapplyImm16V8,
    GLUE, ACC, FUNC)
DEFINE_PARAMETER_INDEX(BaselineNewlexenvImm8,
    GLUE, ACC, NUM_VARS, SP)
DEFINE_PARAMETER_INDEX(BaselineNewlexenvwithnameImm8Id16,
    GLUE, SP, ACC, NUM_VARS, SCOPEID)
DEFINE_PARAMETER_INDEX(BaselineAdd2Imm8V8,
    GLUE, ACC, LEFT)
DEFINE_PARAMETER_INDEX(BaselineSub2Imm8V8,
    GLUE, ACC, LEFT)
DEFINE_PARAMETER_INDEX(BaselineMul2Imm8V8,
    GLUE, ACC, LEFT)
DEFINE_PARAMETER_INDEX(BaselineDiv2Imm8V8,
    GLUE, ACC, LEFT)
DEFINE_PARAMETER_INDEX(BaselineMod2Imm8V8,
    GLUE, ACC, LEFT)
DEFINE_PARAMETER_INDEX(BaselineEqImm8V8,
    GLUE, ACC, LEFT)
DEFINE_PARAMETER_INDEX(BaselineNoteqImm8V8,
    GLUE, ACC, LEFT)
DEFINE_PARAMETER_INDEX(BaselineLessImm8V8,
    GLUE, ACC, LEFT)
DEFINE_PARAMETER_INDEX(BaselineLesseqImm8V8,
    GLUE, ACC, LEFT)
DEFINE_PARAMETER_INDEX(BaselineGreaterImm8V8,
    GLUE, ACC, LEFT)
DEFINE_PARAMETER_INDEX(BaselineGreatereqImm8V8,
    GLUE, ACC, LEFT)
DEFINE_PARAMETER_INDEX(BaselineShl2Imm8V8,
    GLUE, ACC, LEFT)
DEFINE_PARAMETER_INDEX(BaselineShr2Imm8V8,
    GLUE, ACC, LEFT)
DEFINE_PARAMETER_INDEX(BaselineAshr2Imm8V8,
    GLUE, ACC, LEFT)
DEFINE_PARAMETER_INDEX(BaselineAnd2Imm8V8,
    GLUE, ACC, LEFT)
DEFINE_PARAMETER_INDEX(BaselineOr2Imm8V8,
    GLUE, ACC, LEFT)
DEFINE_PARAMETER_INDEX(BaselineXor2Imm8V8,
    GLUE, ACC, LEFT)
DEFINE_PARAMETER_INDEX(BaselineExpImm8V8,
    GLUE, ACC, BASE)
DEFINE_PARAMETER_INDEX(BaselineTypeofImm8,
    GLUE, ACC)
DEFINE_PARAMETER_INDEX(BaselineTypeofImm16,
    GLUE, ACC)
DEFINE_PARAMETER_INDEX(BaselineTonumberImm8,
    GLUE, ACC)
DEFINE_PARAMETER_INDEX(BaselineTonumericImm8,
    GLUE, ACC)
DEFINE_PARAMETER_INDEX(BaselineNegImm8,
    GLUE, ACC)
DEFINE_PARAMETER_INDEX(BaselineNotImm8,
    GLUE, ACC)
DEFINE_PARAMETER_INDEX(BaselineIncImm8,
    GLUE, ACC)
DEFINE_PARAMETER_INDEX(BaselineDecImm8,
    GLUE, ACC)
DEFINE_PARAMETER_INDEX(BaselineIsinImm8V8,
    GLUE, ACC, PROP)
DEFINE_PARAMETER_INDEX(BaselineInstanceofImm8V8,
    GLUE, PROFILE_TYPE_INFO, ACC, OBJ, SLOTID)
DEFINE_PARAMETER_INDEX(BaselineStrictnoteqImm8V8,
    GLUE, ACC, LEFT)
DEFINE_PARAMETER_INDEX(BaselineStricteqImm8V8,
    GLUE, ACC, LEFT)
DEFINE_PARAMETER_INDEX(BaselineIstrue,
    ACC)
DEFINE_PARAMETER_INDEX(BaselineIsfalse,
    ACC)
DEFINE_PARAMETER_INDEX(BaselineCallthis3Imm8V8V8V8V8,
    GLUE, SP, THIS_VALUE, ARG0, ARG1, ARG2)
DEFINE_PARAMETER_INDEX(BaselineCallthisrangeImm8Imm8V8,
    GLUE, SP, ACC, ACTUAL_NUM_ARGS, THIS_REG, HOTNESS_COUNTER)
DEFINE_PARAMETER_INDEX(BaselineSupercallthisrangeImm8Imm8V8,
    GLUE, SP, ACC, RANGE, V0, HOTNESS_COUNTER)
DEFINE_PARAMETER_INDEX(BaselineSupercallarrowrangeImm8Imm8V8,
    GLUE, ACC, RANGE, V0)
DEFINE_PARAMETER_INDEX(BaselineDefinefuncImm8Id16Imm8,
    GLUE, ACC, METHODID, LENGTH, SP)
DEFINE_PARAMETER_INDEX(BaselineDefinefuncImm16Id16Imm8,
    GLUE, ACC, METHODID, LENGTH, SP)
DEFINE_PARAMETER_INDEX(BaselineDefinemethodImm8Id16Imm8,
    GLUE, ACC, METHODID, LENGTH, SP)
DEFINE_PARAMETER_INDEX(BaselineDefinemethodImm16Id16Imm8,
    GLUE, ACC, METHODID, LENGTH, SP)
DEFINE_PARAMETER_INDEX(BaselineCallarg0Imm8,
    GLUE, SP, ACC, HOTNESS_COUNTER)
DEFINE_PARAMETER_INDEX(BaselineSupercallspreadImm8V8,
    GLUE, SP, ACC, ARRARY, HOTNESS_COUNTER)
DEFINE_PARAMETER_INDEX(BaselineApplyImm8V8V8,
    GLUE, ACC, OBJ, ARRARY)
DEFINE_PARAMETER_INDEX(BaselineCallargs2Imm8V8V8,
    GLUE, SP, ACC, HOTNESS_COUNTER, A0_VALUE, A1_VALUE)
DEFINE_PARAMETER_INDEX(BaselineCallargs3Imm8V8V8V8,
    GLUE, SP, A0_VALUE, A1_VALUE, A2_VALUE)
DEFINE_PARAMETER_INDEX(BaselineCallrangeImm8Imm8V8,
    GLUE, SP, ACC, HOTNESS_COUNTER, ACTUAL_NUM_ARGS, ARG_START)
DEFINE_PARAMETER_INDEX(BaselineLdexternalmodulevarImm8,
    GLUE, ACC, INDEX)
DEFINE_PARAMETER_INDEX(BaselineLdthisbynameImm8Id16,
    GLUE, SP, STRING_ID, SLOT_ID)
DEFINE_PARAMETER_INDEX(BaselineDefinegettersetterbyvalueV8V8V8V8,
    GLUE, SP, PROP, GETTER, SETTER, OFFSET, ACC, OBJ)
DEFINE_PARAMETER_INDEX(BaselineLdthisbynameImm16Id16,
    GLUE, SP, STRING_ID, SLOT_ID)
DEFINE_PARAMETER_INDEX(BaselineStthisbynameImm8Id16,
    GLUE, SP, STRING_ID, SLOT_ID)
DEFINE_PARAMETER_INDEX(BaselineStthisbynameImm16Id16,
    GLUE, SP, STRING_ID, SLOT_ID)
DEFINE_PARAMETER_INDEX(BaselineLdthisbyvalueImm8,
    GLUE, SP, PROFILE_TYPE_INFO, ACC, SLOT_ID)
DEFINE_PARAMETER_INDEX(BaselineLdthisbyvalueImm16,
    GLUE, SP, PROFILE_TYPE_INFO, ACC, SLOT_ID)
DEFINE_PARAMETER_INDEX(BaselineStthisbyvalueImm8V8,
    GLUE, SP, PROFILE_TYPE_INFO, ACC, SLOT_ID, PROP_KEY)
DEFINE_PARAMETER_INDEX(BaselineStthisbyvalueImm16V8,
    GLUE, SP, PROFILE_TYPE_INFO, ACC, SLOT_ID, PROP_KEY)
DEFINE_PARAMETER_INDEX(BaselineDynamicimport,
    GLUE, SP, ACC)
DEFINE_PARAMETER_INDEX(BaselineDefineclasswithbufferImm8Id16Id16Imm16V8,
    GLUE, SP, METHOD_ID, LITERRAL_ID, LENGTH, V0)
DEFINE_PARAMETER_INDEX(BaselineDefineclasswithbufferImm16Id16Id16Imm16V8,
    GLUE, SP, METHOD_ID, LITERRAL_ID, LENGTH, PROTO)
DEFINE_PARAMETER_INDEX(BaselineResumegenerator,
    GLUE, SP, ACC)
DEFINE_PARAMETER_INDEX(BaselineGetresumemod,
    ACC)
DEFINE_PARAMETER_INDEX(BaselineGettemplateobjectImm8,
    GLUE, ACC)
DEFINE_PARAMETER_INDEX(BaselineGettemplateobjectImm16,
    GLUE, ACC)
DEFINE_PARAMETER_INDEX(BaselineGetnextpropnameV8,
    GLUE, ITER)
DEFINE_PARAMETER_INDEX(BaselineJeqzImm8,
    ACC, OFFSET, PROFILE_TYPE_INFO, HOTNESS_COUNTER)
DEFINE_PARAMETER_INDEX(BaselineJeqzImm16,
    ACC, OFFSET, PROFILE_TYPE_INFO, HOTNESS_COUNTER)
DEFINE_PARAMETER_INDEX(BaselineSetobjectwithprotoImm8V8,
    GLUE, ACC, PROTO)
DEFINE_PARAMETER_INDEX(BaselineDelobjpropV8,
    GLUE, ACC, OBJ)
DEFINE_PARAMETER_INDEX(BaselineAsyncfunctionawaituncaughtV8,
    GLUE, ACC, ASYNC_FUNC_OBJ)
DEFINE_PARAMETER_INDEX(BaselineCopydatapropertiesV8,
    GLUE, ACC, DST)
DEFINE_PARAMETER_INDEX(BaselineStarrayspreadV8V8,
    GLUE, ACC, DST, INDEX)
DEFINE_PARAMETER_INDEX(BaselineSetobjectwithprotoImm16V8,
    GLUE, ACC, PROTO)
DEFINE_PARAMETER_INDEX(BaselineLdobjbyvalueImm8V8,
    GLUE, PROFILE_TYPE_INFO, ACC, RECEIVER, SLOTID)
DEFINE_PARAMETER_INDEX(BaselineLdobjbyvalueImm16V8,
    GLUE, PROFILE_TYPE_INFO, ACC, RECEIVER, SLOTID)
DEFINE_PARAMETER_INDEX(BaselineStobjbyvalueImm8V8V8,
    GLUE, PROFILE_TYPE_INFO, ACC, RECEIVER, SLOTID, PROP_KEY)
DEFINE_PARAMETER_INDEX(BaselineStobjbyvalueImm16V8V8,
    GLUE, PROFILE_TYPE_INFO, ACC, RECEIVER, SLOTID, PROP_KEY)
DEFINE_PARAMETER_INDEX(BaselineStownbyvalueImm8V8V8,
    GLUE, ACC, RECEIVER, PROP_KEY)
DEFINE_PARAMETER_INDEX(BaselineStownbyvalueImm16V8V8,
    GLUE, ACC, RECEIVER, PROP_KEY)
DEFINE_PARAMETER_INDEX(BaselineLdsuperbyvalueImm8V8,
    GLUE, ACC, RECEIVER)
DEFINE_PARAMETER_INDEX(BaselineLdsuperbyvalueImm16V8,
    GLUE, ACC, RECEIVER)
DEFINE_PARAMETER_INDEX(BaselineStsuperbyvalueImm8V8V8,
    GLUE, ACC, RECEIVER, PROP_KEY)
DEFINE_PARAMETER_INDEX(BaselineStsuperbyvalueImm16V8V8,
    GLUE, ACC, RECEIVER, PROP_KEY)
DEFINE_PARAMETER_INDEX(BaselineLdobjbyindexImm8Imm16,
    GLUE, ACC, INDEX)
DEFINE_PARAMETER_INDEX(BaselineLdobjbyindexImm16Imm16,
    GLUE, ACC, INDEX)
DEFINE_PARAMETER_INDEX(BaselineStobjbyindexImm8V8Imm16,
    GLUE, ACC, RECEIVER, INDEX)
DEFINE_PARAMETER_INDEX(BaselineStobjbyindexImm16V8Imm16,
    GLUE, ACC, RECEIVER, INDEX)
DEFINE_PARAMETER_INDEX(BaselineStownbyindexImm8V8Imm16,
    GLUE, PROFILE_TYPE_INFO, ACC, RECEIVER, INDEX, SLOTID)
DEFINE_PARAMETER_INDEX(BaselineStownbyindexImm16V8Imm16,
    GLUE, PROFILE_TYPE_INFO, ACC, RECEIVER, INDEX, SLOTID)
DEFINE_PARAMETER_INDEX(BaselineAsyncfunctionresolveV8,
    GLUE, ACC, ASYNC_FUNC_OBJ)
DEFINE_PARAMETER_INDEX(BaselineAsyncfunctionrejectV8,
    GLUE, ACC, ASYNC_FUNC_OBJ)
DEFINE_PARAMETER_INDEX(BaselineCopyrestargsImm8,
    GLUE, SP, ACC, REST_IDX)
DEFINE_PARAMETER_INDEX(BaselineLdlexvarImm4Imm4,
    SP, ACC, LEVEL, SLOT)
DEFINE_PARAMETER_INDEX(BaselineStlexvarImm4Imm4,
    GLUE, SP, ACC, LEVEL, SLOT)
DEFINE_PARAMETER_INDEX(BaselineGetmodulenamespaceImm8,
    GLUE, ACC, INDEX)
DEFINE_PARAMETER_INDEX(BaselineStmodulevarImm8,
    GLUE, ACC, INDEX)
DEFINE_PARAMETER_INDEX(BaselineTryldglobalbynameImm16Id16,
    GLUE, SP, PROFILE_TYPE_INFO, ACC, SLOTID, STRING_ID)
DEFINE_PARAMETER_INDEX(BaselineTrystglobalbynameImm8Id16,
    GLUE, SP, PROFILE_TYPE_INFO, ACC, SLOTID, STRING_ID)
DEFINE_PARAMETER_INDEX(BaselineTrystglobalbynameImm16Id16,
    GLUE, SP, PROFILE_TYPE_INFO, ACC, SLOTID, STRING_ID)
DEFINE_PARAMETER_INDEX(BaselineLdglobalvarImm16Id16,
    GLUE, SP, PROFILE_TYPE_INFO, ACC, SLOTID, STRING_ID)
DEFINE_PARAMETER_INDEX(BaselineStglobalvarImm16Id16,
    GLUE, SP, PROFILE_TYPE_INFO, ACC, SLOTID, STRING_ID)
DEFINE_PARAMETER_INDEX(BaselineLdobjbynameImm8Id16,
    GLUE, SP, PROFILE_TYPE_INFO, ACC, SLOTID, STRING_ID)
DEFINE_PARAMETER_INDEX(BaselineLdobjbynameImm16Id16,
    GLUE, SP, PROFILE_TYPE_INFO, ACC, SLOTID, STRING_ID)
DEFINE_PARAMETER_INDEX(BaselineStobjbynameImm8Id16V8,
    GLUE, SP, SLOTID, STRING_ID, RECEIVER)
DEFINE_PARAMETER_INDEX(BaselineStobjbynameImm16Id16V8,
    GLUE, SP, SLOTID, STRING_ID, RECEIVER)
DEFINE_PARAMETER_INDEX(BaselineStownbynameImm8Id16V8,
    GLUE, SP, ACC, RECEIVER, STRING_ID)
DEFINE_PARAMETER_INDEX(BaselineStownbynameImm16Id16V8,
    GLUE, SP, ACC, RECEIVER, STRING_ID)
DEFINE_PARAMETER_INDEX(BaselineLdsuperbynameImm8Id16,
    GLUE, SP, ACC, STRING_ID)
DEFINE_PARAMETER_INDEX(BaselineLdsuperbynameImm16Id16,
    GLUE, SP, ACC, STRING_ID)
DEFINE_PARAMETER_INDEX(BaselineStsuperbynameImm8Id16V8,
    GLUE, SP, ACC, RECEIVER, STRING_ID)
DEFINE_PARAMETER_INDEX(BaselineStsuperbynameImm16Id16V8,
    GLUE, SP, ACC, RECEIVER, STRING_ID)
DEFINE_PARAMETER_INDEX(BaselineLdlocalmodulevarImm8,
    GLUE, ACC, INDEX)
DEFINE_PARAMETER_INDEX(BaselineStconsttoglobalrecordImm16Id16,
    GLUE, SP, ACC, STRING_ID)


DEFINE_PARAMETER_INDEX(BaselineJeqzImm32,
    GLUE, ACC, FUNC, PROFILE_TYPE_INFO, HOTNESS_COUNTER, OFFSET)
DEFINE_PARAMETER_INDEX(BaselineJnezImm8,
    GLUE, ACC, FUNC, PROFILE_TYPE_INFO, HOTNESS_COUNTER, OFFSET)
DEFINE_PARAMETER_INDEX(BaselineJnezImm16,
    GLUE, ACC, FUNC, PROFILE_TYPE_INFO, HOTNESS_COUNTER, OFFSET)
DEFINE_PARAMETER_INDEX(BaselineJnezImm32,
    GLUE, ACC, FUNC, PROFILE_TYPE_INFO, HOTNESS_COUNTER, OFFSET)
DEFINE_PARAMETER_INDEX(BaselineStownbyvaluewithnamesetImm8V8V8,
    GLUE, ACC, RECEIVER, PROP_KEY)
DEFINE_PARAMETER_INDEX(BaselineStownbyvaluewithnamesetImm16V8V8,
    GLUE, ACC, RECEIVER, PROP_KEY)
DEFINE_PARAMETER_INDEX(BaselineStownbynamewithnamesetImm8Id16V8,
    GLUE, ACC, SP, STRING_ID, RECEIVER)
DEFINE_PARAMETER_INDEX(BaselineStownbynamewithnamesetImm16Id16V8,
    GLUE, ACC, SP, STRING_ID, RECEIVER)
DEFINE_PARAMETER_INDEX(BaselineLdbigintId16,
    GLUE, SP, STRING_ID)
DEFINE_PARAMETER_INDEX(BaselineJmpImm8,
    GLUE, FUNC, PROFILE_TYPE_INFO, HOTNESS_COUNTER, OFFSET)
DEFINE_PARAMETER_INDEX(BaselineJmpImm32,
    GLUE, FUNC, PROFILE_TYPE_INFO, HOTNESS_COUNTER, OFFSET)
DEFINE_PARAMETER_INDEX(BaselineFldaiImm64,
    ACC, IMM)
DEFINE_PARAMETER_INDEX(BaselineReturn, GLUE, ACC, SP)
DEFINE_PARAMETER_INDEX(BaselineLdlexvarImm8Imm8,
    ACC, LEVEL, SLOT, SP)
DEFINE_PARAMETER_INDEX(BaselineStlexvarImm8Imm8,
    GLUE, ACC, LEVEL, SLOT, SP)
DEFINE_PARAMETER_INDEX(BaselineJnstricteqV8Imm16,
    GLUE)
DEFINE_PARAMETER_INDEX(BaselineMovV4V4,
    GLUE, SP, VDST, VSRC)
DEFINE_PARAMETER_INDEX(BaselineMovV8V8,
    GLUE, SP, VDST, VSRC)
DEFINE_PARAMETER_INDEX(BaselineMovV16V16,
    GLUE, SP, VDST, VSRC)
DEFINE_PARAMETER_INDEX(BaselineAsyncgeneratorrejectV8,
    GLUE, SP, ACC, V0)
DEFINE_PARAMETER_INDEX(BaselineNop,
    GLUE)
DEFINE_PARAMETER_INDEX(BaselineSetgeneratorstateImm8,
    GLUE, ACC, INDEX)
DEFINE_PARAMETER_INDEX(BaselineGetasynciteratorImm8,
    GLUE, ACC)
DEFINE_PARAMETER_INDEX(BaselineLdPrivatePropertyImm8Imm16Imm16,
    GLUE, ACC, INDEX0, INDEX1, ENV)
DEFINE_PARAMETER_INDEX(BaselineStPrivatePropertyImm8Imm16Imm16V8,
    GLUE, SP, INDEX0, INDEX1, INDEX2)
DEFINE_PARAMETER_INDEX(BaselineTestInImm8Imm16Imm16,
    GLUE, ACC, INDEX0, INDEX1, ENV)
DEFINE_PARAMETER_INDEX(BaselineDeprecatedLdlexenvPrefNone,
    ACC, SP)
DEFINE_PARAMETER_INDEX(BaselineWideCreateobjectwithexcludedkeysPrefImm16V8V8,
    GLUE, SP, V0, V1, V2)
DEFINE_PARAMETER_INDEX(BaselineThrowPrefNone,
    GLUE, ACC)
DEFINE_PARAMETER_INDEX(BaselineDeprecatedPoplexenvPrefNone,
    GLUE, SP)
DEFINE_PARAMETER_INDEX(BaselineWideNewobjrangePrefImm16V8,
    GLUE, SP, ACC, NUM_ARGS, IDX, HOTNESS_COUNTER)
DEFINE_PARAMETER_INDEX(BaselineThrowNotexistsPrefNone,
    GLUE)
DEFINE_PARAMETER_INDEX(BaselineDeprecatedGetiteratornextPrefV8V8,
   GLUE, SP, V0, V1)
DEFINE_PARAMETER_INDEX(BaselineWideNewlexenvPrefImm16,
    GLUE, SP, ACC, INDEX)
DEFINE_PARAMETER_INDEX(BaselineThrowPatternnoncoerciblePrefNone,
    GLUE)
DEFINE_PARAMETER_INDEX(BaselineDeprecatedCreatearraywithbufferPrefImm16,
    GLUE, SP, IMM_I16, SLOT_ID_I8, PROFILE_TYPE_INFO, PC)
DEFINE_PARAMETER_INDEX(BaselineWideNewlexenvwithnamePrefImm16Id16,
    GLUE, SP, ACC, INDEX0, INDEX1)
DEFINE_PARAMETER_INDEX(BaselineThrowDeletesuperpropertyPrefNone,
    GLUE)
DEFINE_PARAMETER_INDEX(BaselineDeprecatedCreateobjectwithbufferPrefImm16,
    GLUE, IMM_I16, SP)
DEFINE_PARAMETER_INDEX(BaselineNewobjrangeImm8Imm8V8,
    GLUE, SP, V0, V1)
DEFINE_PARAMETER_INDEX(BaselineNewobjrangeImm16Imm8V8,
    GLUE, SP, V0, V1)
DEFINE_PARAMETER_INDEX(BaselineWideCallrangePrefImm16V8,
    GLUE, SP, ACC, V0, V1, HOTNESS_COUNTER)
DEFINE_PARAMETER_INDEX(BaselineThrowConstassignmentPrefV8,
    GLUE, SP, V0)
DEFINE_PARAMETER_INDEX(BaselineDeprecatedTonumberPrefV8,
    GLUE, SP, ACC, V0)
DEFINE_PARAMETER_INDEX(BaselineWideCallthisrangePrefImm16V8,
    GLUE, SP, ACC, V0, V1, HOTNESS_COUNTER)
DEFINE_PARAMETER_INDEX(BaselineThrowIfnotobjectPrefV8,
    GLUE, SP, V0)
DEFINE_PARAMETER_INDEX(BaselineDeprecatedTonumericPrefV8,
    GLUE, SP, ACC, V0)
DEFINE_PARAMETER_INDEX(BaselineWideSupercallthisrangePrefImm16V8,
    GLUE, SP, INDEX0, INDEX1)
DEFINE_PARAMETER_INDEX(BaselineThrowUndefinedifholePrefV8V8,
    GLUE, SP, V0, V1)
DEFINE_PARAMETER_INDEX(BaselineThrowUndefinedifholewithnamePrefId16,
    GLUE, ACC, SP, STRING_ID)
DEFINE_PARAMETER_INDEX(BaselineDeprecatedNegPrefV8,
    GLUE, SP, V0)
DEFINE_PARAMETER_INDEX(BaselineWideSupercallarrowrangePrefImm16V8,
    GLUE, ACC, RANGE, V0_I8
)
DEFINE_PARAMETER_INDEX(BaselineThrowIfsupernotcorrectcallPrefImm8,
    GLUE, ACC, IMM)
DEFINE_PARAMETER_INDEX(BaselineDeprecatedNotPrefV8,
    GLUE, SP, INDEX)
DEFINE_PARAMETER_INDEX(BaselineWideLdobjbyindexPrefImm32,
    GLUE, ACC, INDEX)
DEFINE_PARAMETER_INDEX(BaselineThrowIfsupernotcorrectcallPrefImm16,
    GLUE, ACC, IMM)
DEFINE_PARAMETER_INDEX(BaselineDeprecatedIncPrefV8,
    GLUE, SP, V0)
DEFINE_PARAMETER_INDEX(BaselineWideStobjbyindexPrefV8Imm32,
    GLUE, SP, ACC, V0, INDEX)
DEFINE_PARAMETER_INDEX(BaselineDeprecatedDecPrefV8,
    GLUE, SP, INDEX)
DEFINE_PARAMETER_INDEX(BaselineWideStownbyindexPrefV8Imm32,
    GLUE, SP, ACC, V0, INDEX)
DEFINE_PARAMETER_INDEX(BaselineDeprecatedCallarg0PrefV8,
    GLUE, SP, FUNC_REG, HOTNESS_COUNTER)
DEFINE_PARAMETER_INDEX(BaselineWideCopyrestargsPrefImm16,
    GLUE, INDEX)
DEFINE_PARAMETER_INDEX(BaselineDeprecatedCallarg1PrefV8V8,
    GLUE, SP, FUNC_REG, A0, HOTNESS_COUNTER)
DEFINE_PARAMETER_INDEX(BaselineWideLdlexvarPrefImm16Imm16,
    GLUE, SP, ACC, LEVEL_I16, SLOT_I16)
DEFINE_PARAMETER_INDEX(BaselineDeprecatedCallargs2PrefV8V8V8,
    GLUE, SP, FUNC_REG, A0, A1, HOTNESS_COUNTER)
DEFINE_PARAMETER_INDEX(BaselineWideStlexvarPrefImm16Imm16,
    GLUE, SP, ACC, LEVEL_I16, SLOT_I16)
DEFINE_PARAMETER_INDEX(BaselineDeprecatedCallargs3PrefV8V8V8V8,
    GLUE, SP, FUNC_REG, A0, A1, A2)
DEFINE_PARAMETER_INDEX(BaselineWideGetmodulenamespacePrefImm16,
    GLUE, ACC, INDEX)
DEFINE_PARAMETER_INDEX(BaselineDeprecatedCallrangePrefImm16V8,
    GLUE, SP, INDEX, FUNC_REG, HOTNESS_COUNTER)
DEFINE_PARAMETER_INDEX(BaselineWideStmodulevarPrefImm16,
    GLUE, ACC, INDEX)
DEFINE_PARAMETER_INDEX(BaselineDeprecatedCallspreadPrefV8V8V8,
    GLUE, SP, V0, V1, V2)
DEFINE_PARAMETER_INDEX(BaselineWideLdlocalmodulevarPrefImm16,
    GLUE, ACC, INDEX)
DEFINE_PARAMETER_INDEX(BaselineDeprecatedCallthisrangePrefImm16V8,
    GLUE, SP, INDEX, FUNC_REG, HOTNESS_COUNTER)
DEFINE_PARAMETER_INDEX(BaselineWideLdexternalmodulevarPrefImm16,
    GLUE, ACC, INDEX)
DEFINE_PARAMETER_INDEX(BaselineDeprecatedDefineclasswithbufferPrefId16Imm16Imm16V8V8,
    SP, METHOD_ID, LITERAL_ID, LENGTH, V0, V1, GLUE)
DEFINE_PARAMETER_INDEX(BaselineWideLdpatchvarPrefImm16,
    GLUE, ACC, INDEX)
DEFINE_PARAMETER_INDEX(BaselineDeprecatedResumegeneratorPrefV8,
    GLUE, SP, ACC, V0)
DEFINE_PARAMETER_INDEX(BaselineWideStpatchvarPrefImm16,
    GLUE, ACC, INDEX)
DEFINE_PARAMETER_INDEX(BaselineDeprecatedGetresumemodePrefV8,
    SP, ACC, V0)
DEFINE_PARAMETER_INDEX(BaselineDeprecatedGettemplateobjectPrefV8,
    GLUE, SP, V0)
DEFINE_PARAMETER_INDEX(BaselineDeprecatedDelobjpropPrefV8V8,
    GLUE, SP, V0, V1)
DEFINE_PARAMETER_INDEX(BaselineDeprecatedSuspendgeneratorPrefV8V8,
    GLUE, SP, PC, V0, V1)
DEFINE_PARAMETER_INDEX(BaselineDeprecatedAsyncfunctionawaituncaughtPrefV8V8,
    GLUE, SP, V0, V1)
DEFINE_PARAMETER_INDEX(BaselineDeprecatedCopydatapropertiesPrefV8V8,
    GLUE, SP, V0, V1)
DEFINE_PARAMETER_INDEX(BaselineDeprecatedSetobjectwithprotoPrefV8V8,
    GLUE, SP, ACC, V0, V1)
DEFINE_PARAMETER_INDEX(BaselineDeprecatedLdobjbyvaluePrefV8V8,
    GLUE, SP, ACC, V0, V1)
DEFINE_PARAMETER_INDEX(BaselineDeprecatedLdsuperbyvaluePrefV8V8,
    GLUE, SP, V0, V1)
DEFINE_PARAMETER_INDEX(BaselineDeprecatedLdobjbyindexPrefV8Imm32,
    GLUE, SP, V0, INDEX)
DEFINE_PARAMETER_INDEX(BaselineDeprecatedAsyncfunctionresolvePrefV8V8V8,
    GLUE, SP, V0, V1)
DEFINE_PARAMETER_INDEX(BaselineDeprecatedAsyncfunctionrejectPrefV8V8V8,
    GLUE, SP, V0, V1)
DEFINE_PARAMETER_INDEX(BaselineDeprecatedStlexvarPrefImm4Imm4V8,
    GLUE, SP, LEVEL_I4, SLOT_I4, V0)
DEFINE_PARAMETER_INDEX(BaselineDeprecatedStlexvarPrefImm8Imm8V8,
    GLUE, SP, LEVEL_I8, SLOT_I8, V0)
DEFINE_PARAMETER_INDEX(BaselineDeprecatedStlexvarPrefImm16Imm16V8,
    GLUE, SP, LEVEL_I16, SLOT_I16, V0)
DEFINE_PARAMETER_INDEX(BaselineDeprecatedGetmodulenamespacePrefId32,
    GLUE, ACC, STRING_ID, SP)
DEFINE_PARAMETER_INDEX(BaselineDeprecatedStmodulevarPrefId32,
    GLUE, ACC, STRING_ID, SP)
DEFINE_PARAMETER_INDEX(BaselineDeprecatedLdobjbynamePrefId32V8,
    GLUE, SP, ACC, V0, STRING_ID)
DEFINE_PARAMETER_INDEX(BaselineDeprecatedLdsuperbynamePrefId32V8,
    GLUE, SP, ACC, STRING_ID, V0)
DEFINE_PARAMETER_INDEX(BaselineDeprecatedLdmodulevarPrefId32Imm8,
    GLUE, ACC, STRING_ID, FLAG_I8, SP)
DEFINE_PARAMETER_INDEX(BaselineDeprecatedStconsttoglobalrecordPrefId32,
    GLUE, ACC, STRING_ID, SP)
DEFINE_PARAMETER_INDEX(BaselineDeprecatedStlettoglobalrecordPrefId32,
    GLUE, ACC, STRING_ID, SP)
DEFINE_PARAMETER_INDEX(BaselineDeprecatedStclasstoglobalrecordPrefId32,
    GLUE, ACC, STRING_ID, SP)
DEFINE_PARAMETER_INDEX(BaselineDeprecatedLdhomeobjectPrefNone,
    ACC, SP)
DEFINE_PARAMETER_INDEX(BaselineDeprecatedCreateobjecthavingmethodPrefImm16,
    GLUE, ACC, SP, IMM_I16)
DEFINE_PARAMETER_INDEX(BaselineDeprecatedDynamicimportPrefV8,
    GLUE, SP, ACC, VREG)
DEFINE_PARAMETER_INDEX(BaselineCallRuntimeNotifyConcurrentResultPrefNone,
    GLUE, SP, ACC)
DEFINE_PARAMETER_INDEX(BaselineDefineFieldByNameImm8Id16V8,
    GLUE, SP, SLOT_ID_I8, STRING_ID, V0)
DEFINE_PARAMETER_INDEX(BaselineCallRuntimeDefineFieldByValuePrefImm8V8V8,
    GLUE, SP, ACC, V0, V1)
DEFINE_PARAMETER_INDEX(BaselineCallRuntimeDefineFieldByIndexPrefImm8Imm32V8,
    GLUE, SP, ACC, INDEX, V0)
DEFINE_PARAMETER_INDEX(BaselineCallRuntimeToPropertyKeyPrefNone,
    GLUE, ACC)
DEFINE_PARAMETER_INDEX(BaselineCallRuntimeCreatePrivatePropertyPrefImm16Id16,
    GLUE, SP, COUNT, LITERAL_ID)
DEFINE_PARAMETER_INDEX(BaselineCallRuntimeDefinePrivatePropertyPrefImm8Imm16Imm16V8,
    GLUE, SP, ACC, LEVEL_INDEX, SLOT_INDEX, V0)
DEFINE_PARAMETER_INDEX(BaselineCallRuntimeCallInitPrefImm8V8,
    GLUE, SP, ACC, V0, HOTNESS_COUNTER)
DEFINE_PARAMETER_INDEX(BaselineCallRuntimeDefineSendableClassPrefImm16Id16Id16Imm16V8,
    GLUE, SP, METHOD_ID, LITERAL_ID, LENGTH, V0)
DEFINE_PARAMETER_INDEX(BaselineCallRuntimeLdSendableClassPrefImm16,
    GLUE, SP, ACC, LEVEL)
DEFINE_PARAMETER_INDEX(BaselineReturnundefined, GLUE, ACC, SP)
DEFINE_PARAMETER_INDEX(BaselineExceptionHandler,
    GLUE, ACC, SP, PROFILE_TYPE_INFO, HOTNESS_COUNTER, FRAME)
DEFINE_PARAMETER_INDEX(BaselineUpdateHotness,
    GLUE, SP, OFFSET)
}  // namespace panda::ecmascript::kungfu
#endif  // ECMASCRIPT_BASELINE_COMPILER_CALL_SIGNATURE_H