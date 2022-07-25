/*
 * Copyright (c) 2021 Huawei Device Co., Ltd.
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

#ifndef ECMASCRIPT_INTERPRETER_INTERPRETER_H
#define ECMASCRIPT_INTERPRETER_INTERPRETER_H

#include "ecmascript/ecma_runtime_call_info.h"
#include "ecmascript/js_method.h"
#include "ecmascript/js_tagged_value.h"
#include "ecmascript/js_handle.h"
#include "ecmascript/js_thread.h"
#include "ecmascript/frames.h"
#include "ecmascript/require/js_cjs_module.h"

namespace panda::ecmascript {
class ConstantPool;
class ECMAObject;
class GeneratorContext;

class EcmaInterpreter {
public:
    static const int16_t METHOD_HOTNESS_THRESHOLD = 512;
    enum ActualNumArgsOfCall : uint8_t { CALLARG0 = 0, CALLARG1, CALLARGS2, CALLARGS3 };

    static inline JSTaggedValue Execute(EcmaRuntimeCallInfo *info);
    static inline JSTaggedValue ExecuteNative(EcmaRuntimeCallInfo *info);
    static EcmaRuntimeCallInfo* NewRuntimeCallInfo(
        JSThread *thread, JSHandle<JSTaggedValue> func, JSHandle<JSTaggedValue> thisObj,
        JSHandle<JSTaggedValue> newTarget, int32_t numArgs);
    static inline JSTaggedValue GeneratorReEnterInterpreter(JSThread *thread, JSHandle<GeneratorContext> context);
    static inline JSTaggedValue GeneratorReEnterAot(JSThread *thread, JSHandle<GeneratorContext> context);
    static inline void RunInternal(JSThread *thread, ConstantPool *constpool, const uint8_t *pc, JSTaggedType *sp);
    static inline void InitStackFrame(JSThread *thread);
    static inline uint32_t FindCatchBlock(JSMethod *caller, uint32_t pc);
    static inline size_t GetJumpSizeAfterCall(const uint8_t *prevPc);

    static inline JSTaggedValue GetRuntimeProfileTypeInfo(JSTaggedType *sp);
    static inline bool UpdateHotnessCounter(JSThread* thread, JSTaggedType *sp, JSTaggedValue acc, int32_t offset);
    static inline void NotifyBytecodePcChanged(JSThread *thread);
    static inline const JSPandaFile *GetNativeCallPandafile(JSThread *thread);
    static inline JSTaggedValue GetThisFunction(JSTaggedType *sp);
    static inline JSTaggedValue GetNewTarget(JSTaggedType *sp);
    static inline uint32_t GetNumArgs(JSTaggedType *sp, uint32_t restIdx, uint32_t &startIdx);
    static inline JSTaggedValue GetThisObjectFromFastNewFrame(JSTaggedType *sp);
    static inline bool IsFastNewFrameEnter(JSFunction *ctor, JSMethod *method);
    static inline bool IsFastNewFrameExit(JSTaggedType *sp);
};

enum EcmaOpcode {
    LDNAN_PREF,
    LDINFINITY_PREF,
    LDGLOBALTHIS_PREF,
    LDUNDEFINED_PREF,
    LDNULL_PREF,
    LDSYMBOL_PREF,
    LDGLOBAL_PREF,
    LDTRUE_PREF,
    LDFALSE_PREF,
    THROWDYN_PREF,
    TYPEOFDYN_PREF,
    LDLEXENVDYN_PREF,
    POPLEXENVDYN_PREF,
    GETUNMAPPEDARGS_PREF,
    GETPROPITERATOR_PREF,
    ASYNCFUNCTIONENTER_PREF,
    LDHOLE_PREF,
    RETURNUNDEFINED_PREF,
    CREATEEMPTYOBJECT_PREF,
    CREATEEMPTYARRAY_PREF,
    GETITERATOR_PREF,
    THROWTHROWNOTEXISTS_PREF,
    THROWPATTERNNONCOERCIBLE_PREF,
    LDHOMEOBJECT_PREF,
    THROWDELETESUPERPROPERTY_PREF,
    DEBUGGER_PREF,
    ADD2DYN_PREF_V8,
    SUB2DYN_PREF_V8,
    MUL2DYN_PREF_V8,
    DIV2DYN_PREF_V8,
    MOD2DYN_PREF_V8,
    EQDYN_PREF_V8,
    NOTEQDYN_PREF_V8,
    LESSDYN_PREF_V8,
    LESSEQDYN_PREF_V8,
    GREATERDYN_PREF_V8,
    GREATEREQDYN_PREF_V8,
    SHL2DYN_PREF_V8,
    ASHR2DYN_PREF_V8,
    SHR2DYN_PREF_V8,
    AND2DYN_PREF_V8,
    OR2DYN_PREF_V8,
    XOR2DYN_PREF_V8,
    TONUMBER_PREF_V8,
    NEGDYN_PREF_V8,
    NOTDYN_PREF_V8,
    INCDYN_PREF_V8,
    DECDYN_PREF_V8,
    EXPDYN_PREF_V8,
    ISINDYN_PREF_V8,
    INSTANCEOFDYN_PREF_V8,
    STRICTNOTEQDYN_PREF_V8,
    STRICTEQDYN_PREF_V8,
    RESUMEGENERATOR_PREF_V8,
    GETRESUMEMODE_PREF_V8,
    CREATEGENERATOROBJ_PREF_V8,
    THROWCONSTASSIGNMENT_PREF_V8,
    GETTEMPLATEOBJECT_PREF_V8,
    GETNEXTPROPNAME_PREF_V8,
    CALLARG0DYN_PREF_V8,
    THROWIFNOTOBJECT_PREF_V8,
    ITERNEXT_PREF_V8,
    CLOSEITERATOR_PREF_V8,
    COPYMODULE_PREF_V8,
    SUPERCALLSPREAD_PREF_V8,
    DELOBJPROP_PREF_V8_V8,
    NEWOBJSPREADDYN_PREF_V8_V8,
    CREATEITERRESULTOBJ_PREF_V8_V8,
    SUSPENDGENERATOR_PREF_V8_V8,
    ASYNCFUNCTIONAWAITUNCAUGHT_PREF_V8_V8,
    THROWUNDEFINEDIFHOLE_PREF_V8_V8,
    CALLARG1DYN_PREF_V8_V8,
    COPYDATAPROPERTIES_PREF_V8_V8,
    STARRAYSPREAD_PREF_V8_V8,
    GETITERATORNEXT_PREF_V8_V8,
    SETOBJECTWITHPROTO_PREF_V8_V8,
    LDOBJBYVALUE_PREF_V8_V8,
    STOBJBYVALUE_PREF_V8_V8,
    STOWNBYVALUE_PREF_V8_V8,
    LDSUPERBYVALUE_PREF_V8_V8,
    STSUPERBYVALUE_PREF_V8_V8,
    LDOBJBYINDEX_PREF_V8_IMM32,
    STOBJBYINDEX_PREF_V8_IMM32,
    STOWNBYINDEX_PREF_V8_IMM32,
    CALLSPREADDYN_PREF_V8_V8_V8,
    ASYNCFUNCTIONRESOLVE_PREF_V8_V8_V8,
    ASYNCFUNCTIONREJECT_PREF_V8_V8_V8,
    CALLARGS2DYN_PREF_V8_V8_V8,
    CALLARGS3DYN_PREF_V8_V8_V8_V8,
    DEFINEGETTERSETTERBYVALUE_PREF_V8_V8_V8_V8,
    NEWOBJDYNRANGE_PREF_IMM16_V8,
    CALLIRANGEDYN_PREF_IMM16_V8,
    CALLITHISRANGEDYN_PREF_IMM16_V8,
    SUPERCALL_PREF_IMM16_V8,
    CREATEOBJECTWITHEXCLUDEDKEYS_PREF_IMM16_V8_V8,
    DEFINEFUNCDYN_PREF_ID16_IMM16_V8,
    DEFINENCFUNCDYN_PREF_ID16_IMM16_V8,
    DEFINEGENERATORFUNC_PREF_ID16_IMM16_V8,
    DEFINEASYNCFUNC_PREF_ID16_IMM16_V8,
    DEFINEMETHOD_PREF_ID16_IMM16_V8,
    NEWLEXENVDYN_PREF_IMM16,
    COPYRESTARGS_PREF_IMM16,
    CREATEARRAYWITHBUFFER_PREF_IMM16,
    CREATEOBJECTHAVINGMETHOD_PREF_IMM16,
    THROWIFSUPERNOTCORRECTCALL_PREF_IMM16,
    CREATEOBJECTWITHBUFFER_PREF_IMM16,
    LDLEXVARDYN_PREF_IMM4_IMM4,
    LDLEXVARDYN_PREF_IMM8_IMM8,
    LDLEXVARDYN_PREF_IMM16_IMM16,
    STLEXVARDYN_PREF_IMM4_IMM4_V8,
    STLEXVARDYN_PREF_IMM8_IMM8_V8,
    STLEXVARDYN_PREF_IMM16_IMM16_V8,
    DEFINECLASSWITHBUFFER_PREF_ID16_IMM16_IMM16_V8_V8,
    GETMODULENAMESPACE_PREF_ID32,
    STMODULEVAR_PREF_ID32,
    TRYLDGLOBALBYNAME_PREF_ID32,
    TRYSTGLOBALBYNAME_PREF_ID32,
    LDGLOBALVAR_PREF_ID32,
    STGLOBALVAR_PREF_ID32,
    LDOBJBYNAME_PREF_ID32_V8,
    STOBJBYNAME_PREF_ID32_V8,
    STOWNBYNAME_PREF_ID32_V8,
    LDSUPERBYNAME_PREF_ID32_V8,
    STSUPERBYNAME_PREF_ID32_V8,
    LDMODULEVAR_PREF_ID32_IMM8,
    CREATEREGEXPWITHLITERAL_PREF_ID32_IMM8,
    ISTRUE_PREF,
    ISFALSE_PREF,
    STCONSTTOGLOBALRECORD_PREF_ID32,
    STLETTOGLOBALRECORD_PREF_ID32,
    STCLASSTOGLOBALRECORD_PREF_ID32,
    STOWNBYVALUEWITHNAMESET_PREF_V8_V8,
    STOWNBYNAMEWITHNAMESET_PREF_ID32_V8,
    LDFUNCTION_PREF,
    NEWLEXENVWITHNAMEDYN_PREF_IMM16_IMM16,
    LDBIGINT_PREF_ID32,
    TONUMERIC_PREF_V8,
    MOV_DYN_V8_V8,
    MOV_DYN_V16_V16,
    LDA_STR_ID32,
    LDAI_DYN_IMM32,
    FLDAI_DYN_IMM64,
    JMP_IMM8,
    JMP_IMM16,
    JMP_IMM32,
    JEQZ_IMM8,
    JEQZ_IMM16,
    LDA_DYN_V8,
    STA_DYN_V8,
    RETURN_DYN,
    MOV_V4_V4,
    JNEZ_IMM8,
    JNEZ_IMM16,
    LAST_OPCODE,
};

// if modify EcmaOpcode, please update GetEcmaOpcodeStr()
inline std::string GetEcmaOpcodeStr(EcmaOpcode opcode);
}  // namespace panda::ecmascript
#endif  // ECMASCRIPT_INTERPRETER_INTERPRETER_H
