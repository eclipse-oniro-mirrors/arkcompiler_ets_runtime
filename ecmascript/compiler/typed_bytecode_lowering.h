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

#ifndef ECMASCRIPT_COMPILER_TYPED_BYTECODE_LOWERING_H
#define ECMASCRIPT_COMPILER_TYPED_BYTECODE_LOWERING_H

#include "ecmascript/builtin_entries.h"
#include "ecmascript/compiler/argument_accessor.h"
#include "ecmascript/compiler/builtins/builtins_call_signature.h"
#include "ecmascript/compiler/bytecode_circuit_builder.h"
#include "ecmascript/compiler/circuit_builder-inl.h"
#include "ecmascript/compiler/object_access_helper.h"
#include "ecmascript/compiler/pass_manager.h"
#include "ecmascript/compiler/pgo_type/pgo_type_manager.h"
#include "ecmascript/compiler/type_info_accessors.h"
#include "ecmascript/enum_conversion.h"

namespace panda::ecmascript::kungfu {
class TypedBytecodeLowering {
public:
    TypedBytecodeLowering(Circuit* circuit,
                         PassContext* ctx,
                         Chunk* chunk,
                         bool enableLog,
                         bool enableTypeLog,
                         const std::string& name,
                         bool enableLoweringBuiltin,
                         const CString& recordName,
                         const std::string optBCRange)
        : circuit_(circuit),
          acc_(circuit),
          builder_(circuit, ctx->GetCompilerConfig()),
          dependEntry_(circuit->GetDependRoot()),
          tsManager_(ctx->GetTSManager()),
          chunk_(chunk),
          enableLog_(enableLog),
          enableTypeLog_(enableTypeLog),
          profiling_(ctx->GetCompilerConfig()->IsProfiling()),
          verifyVTable_(ctx->GetCompilerConfig()->IsVerifyVTbale()),
          traceBc_(ctx->GetCompilerConfig()->IsTraceBC()),
          methodName_(name),
          glue_(acc_.GetGlueFromArgList()),
          argAcc_(circuit),
          pgoTypeLog_(circuit),
          noCheck_(ctx->GetEcmaVM()->GetJSOptions().IsCompilerNoCheck()),
          thread_(ctx->GetEcmaVM()->GetJSThread()),
          enableLoweringBuiltin_(enableLoweringBuiltin),
          recordName_(recordName),
          optBCRange_(optBCRange)
    {
    }

    ~TypedBytecodeLowering() = default;

    bool RunTypedBytecodeLowering();

private:
    bool IsLogEnabled() const
    {
        return enableLog_;
    }

    bool IsTypeLogEnabled() const
    {
        return enableTypeLog_;
    }

    bool IsProfiling() const
    {
        return profiling_;
    }

    bool IsVerifyVTbale() const
    {
        return verifyVTable_;
    }

    bool IsTraceBC() const
    {
        return traceBc_;
    }

    const std::string& GetMethodName() const
    {
        return methodName_;
    }

    void Lower(GateRef gate);
    template<TypedBinOp Op>
    void LowerTypedBinOp(GateRef gate, bool convertNumberType = true);
    template<TypedUnOp Op>
    void LowerTypedUnOp(GateRef gate);
    template<TypedBinOp Op>
    void LowerTypedEqOrNotEq(GateRef gate);
    void LowerTypeToNumeric(GateRef gate);
    void LowerPrimitiveTypeToNumber(const UnOpTypeInfoAccessor &tacc);
    void LowerConditionJump(GateRef gate, bool flag);

    void LowerTypedTryLdGlobalByName(GateRef gate);

    void LowerTypedLdObjByName(GateRef gate);
    void LowerTypedStObjByName(GateRef gate);
    void LowerTypedStOwnByName(GateRef gate);
    GateRef BuildNamedPropertyAccess(GateRef hir, GateRef receiver, GateRef holder, PropertyLookupResult plr);
    GateRef BuildNamedPropertyAccess(GateRef hir, GateRef receiver, GateRef holder,
                                     GateRef value, PropertyLookupResult plr, uint32_t receiverHClassIndex = 0);
    using AccessMode = PGOObjectAccessHelper::AccessMode;
    bool TryLowerTypedLdObjByNameForBuiltin(GateRef gate);
    bool TryLowerTypedLdObjByNameForBuiltinsId(const LoadBulitinObjTypeInfoAccessor &tacc, BuiltinTypeId type);
    bool TryLowerTypedLdObjByNameForBuiltin(const LoadBulitinObjTypeInfoAccessor &tacc, BuiltinTypeId type);
    bool TryLowerTypedLdObjByNameForGlobalsId(const LoadBulitinObjTypeInfoAccessor &tacc, ConstantIndex globalsId);
    void LowerTypedLdArrayLength(const LoadBulitinObjTypeInfoAccessor &tacc);
    void LowerTypedLdTypedArrayLength(const LoadBulitinObjTypeInfoAccessor &tacc);
    void LowerTypedLdStringLength(const LoadBulitinObjTypeInfoAccessor &tacc);
    bool TryLowerTypedLdObjByNameForBuiltinMethod(const LoadBulitinObjTypeInfoAccessor &tacc, BuiltinTypeId type);

    void LowerTypedLdObjByIndex(GateRef gate);
    bool TryLowerTypedLdObjByIndexForBuiltin(GateRef gate);
    void LowerTypedStObjByIndex(GateRef gate);
    bool TryLowerTypedStObjByIndexForBuiltin(GateRef gate);

    void LowerTypedLdObjByValue(GateRef gate);
    bool TryLowerTypedLdObjByValueForBuiltin(GateRef gate);
    void LowerTypedStObjByValue(GateRef gate);
    void LowerTypedStOwnByValue(GateRef gate);
    bool TryLowerTypedStObjByValueForBuiltin(GateRef gate);

    void LowerTypedIsTrueOrFalse(GateRef gate, bool flag);

    void LowerTypedNewObjRange(GateRef gate);
    void LowerCreateEmptyObject(GateRef gate);
    void LowerCreateObjectWithBuffer(GateRef gate);
    void LowerTypedSuperCall(GateRef gate);

    void LowerTypedCallArg0(GateRef gate);
    void LowerTypedCallArg1(GateRef gate);
    void LowerTypedCallArg2(GateRef gate);
    void LowerTypedCallArg3(GateRef gate);
    void LowerTypedCallrange(GateRef gate);
    void LowerTypedCallthis0(GateRef gate);
    void LowerTypedCallthis1(GateRef gate);
    void LowerTypedCallthis2(GateRef gate);
    void LowerTypedCallthis3(GateRef gate);
    void LowerTypedCallthisrange(GateRef gate);
    void LowerTypedCallInit(GateRef gate);

    template<class TypeAccessor>
    void CheckFastCallThisCallTarget(const TypeAccessor &tacc);

    template<class TypeAccessor>
    void CheckCallThisCallTarget(const TypeAccessor &tacc);

    template<class TypeAccessor>
    void CheckThisCallTargetAndLowerCall(const TypeAccessor &tacc,
        const std::vector<GateRef> &args, const std::vector<GateRef> &argsFastCall);

    template<class TypeAccessor>
    void CheckCallTargetFromDefineFuncAndLowerCall(const TypeAccessor &tacc,
        const std::vector<GateRef> &args, const std::vector<GateRef> &argsFastCall, bool isNoGC);

    template<class TypeAccessor>
    void CheckCallTargetAndLowerCall(const TypeAccessor &tacc,
        const std::vector<GateRef> &args, const std::vector<GateRef> &argsFastCall);

    template<EcmaOpcode Op, class TypeAccessor>
    void LowerTypedCall(const TypeAccessor &tacc);

    template<EcmaOpcode Op, class TypeAccessor>
    void LowerTypedThisCall(const TypeAccessor &tacc);

    bool IsLoadVtable(GateRef func);
    void LowerFastCall(GateRef gate, GateRef func, const std::vector<GateRef> &argsFastCall, bool isNoGC);
    void LowerCall(GateRef gate, GateRef func, const std::vector<GateRef> &args, bool isNoGC);
    void LowerTypedTypeOf(GateRef gate);
    void LowerInstanceOf(GateRef gate);
    void LowerGetIterator(GateRef gate);
    GateRef LoadStringByIndex(const LoadBulitinObjTypeInfoAccessor &tacc);
    GateRef LoadJSArrayByIndex(const LoadBulitinObjTypeInfoAccessor &tacc);
    GateRef LoadTypedArrayByIndex(const LoadBulitinObjTypeInfoAccessor &tacc);
    void StoreJSArrayByIndex(const StoreBulitinObjTypeInfoAccessor &tacc);
    void StoreTypedArrayByIndex(const StoreBulitinObjTypeInfoAccessor &tacc);

    void AddBytecodeCount(EcmaOpcode op);
    void DeleteBytecodeCount(EcmaOpcode op);
    void AddHitBytecodeCount();

    template<TypedBinOp Op>
    void SpeculateStrings(GateRef gate);
    template<TypedBinOp Op>
    void SpeculateNumbers(GateRef gate);
    template<TypedUnOp Op>
    void SpeculateNumber(const UnOpTypeInfoAccessor& tacc);
    void SpeculateConditionJump(const ConditionJumpTypeInfoAccessor &tacc, bool flag);
    void SpeculateCallBuiltin(GateRef gate, GateRef func, const std::vector<GateRef> &args,
                              BuiltinsStubCSigns::ID id, bool isThrow, bool isSideEffect = false);
    void DeleteConstDataIfNoUser(GateRef gate);
    bool TryLowerNewBuiltinConstructor(GateRef gate);
    bool TryLowerTypedLdobjBynameFromGloablBuiltin(GateRef gate);
    bool CheckIsInOptBCIgnoreRange(int32_t index, EcmaOpcode ecmaOpcode);
    int32_t GetEcmaOpCodeListIndex(EcmaOpcode ecmaOpCode);
    void ParseOptBytecodeRange();
    void AddProfiling(GateRef gate);

    bool Uncheck() const
    {
        return noCheck_;
    }

    Circuit *circuit_ {nullptr};
    GateAccessor acc_;
    CircuitBuilder builder_;
    GateRef dependEntry_ {Gate::InvalidGateRef};
    TSManager *tsManager_ {nullptr};
    Chunk *chunk_ {nullptr};
    bool enableLog_ {false};
    bool enableTypeLog_ {false};
    bool profiling_ {false};
    bool verifyVTable_ {false};
    bool traceBc_ {false};
    size_t allJSBcCount_ {0};
    size_t allNonTypedOpCount_ {0};
    size_t hitTypedOpCount_ {0};
    std::string methodName_;
    GateRef glue_ {Circuit::NullGate()};
    ArgumentAccessor argAcc_;
    EcmaOpcode currentOp_ {static_cast<EcmaOpcode>(0xff)};
    PGOTypeLogList pgoTypeLog_;
    std::unordered_map<EcmaOpcode, uint32_t> bytecodeMap_;
    std::unordered_map<EcmaOpcode, uint32_t> bytecodeHitTimeMap_;
    bool noCheck_ {false};
    const JSThread *thread_ {nullptr};
    bool enableLoweringBuiltin_ {false};
    const CString &recordName_;
    std::string optBCRange_;
    std::vector<std::vector<int32_t>> optBCRangeList_;
};
}  // panda::ecmascript::kungfu
#endif  // ECMASCRIPT_COMPILER_TYPED_BYTECODE_LOWERING_H
