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

#ifndef ECMASCRIPT_COMPILER_SLOWPATH_LOWERING_H
#define ECMASCRIPT_COMPILER_SLOWPATH_LOWERING_H

#include "ecmascript/compiler/argument_accessor.h"
#include "ecmascript/compiler/bytecode_circuit_builder.h"
#include "ecmascript/compiler/circuit.h"
#include "ecmascript/compiler/circuit_builder.h"
#include "ecmascript/compiler/circuit_builder-inl.h"
#include "ecmascript/compiler/gate_accessor.h"

namespace panda::ecmascript::kungfu {
// slowPath Lowering Process
// SW: state wire, DW: depend wire, VW: value wire
// Before lowering:
//                         SW        DW         VW
//                         |         |          |
//                         |         |          |
//                         v         v          v
//                     +-----------------------------+
//                     |            (HIR)            |
//                     |         JS_BYTECODE         |DW--------------------------------------
//                     |                             |                                       |
//                     +-----------------------------+                                       |
//                         SW                   SW                                           |
//                         |                     |                                           |
//                         |                     |                                           |
//                         |                     |                                           |
//                         v                     v                                           |
//                 +--------------+        +--------------+                                  |
//                 |  IF_SUCCESS  |        | IF_EXCEPTION |SW---------                       |
//                 +--------------+        +--------------+          |                       |
//                         SW                    SW                  |                       |
//                         |                     |                   |                       |
//                         v                     v                   |                       |
//     --------------------------------------------------------------|-----------------------|-------------------
//     catch processing                                              |                       |
//                                                                   |                       |
//                                                                   v                       V
//                                                            +--------------+       +-----------------+
//                                                            |    MERGE     |SW---->| DEPEND_SELECTOR |
//                                                            +--------------+       +-----------------+
//                                                                                          DW
//                                                                                          |
//                                                                                          v
//                                                                                   +-----------------+
//                                                                                   |  GET_EXCEPTION  |
//                                                                                   +-----------------+


// After lowering:
//         SW                                          DW      VW
//         |                                           |       |
//         |                                           |       |
//         |                                           v       v
//         |        +---------------------+         +------------------+
//         |        | CONSTANT(Exception) |         |       CALL       |DW---------------
//         |        +---------------------+         +------------------+                |
//         |                           VW            VW                                 |
//         |                           |             |                                  |
//         |                           |             |                                  |
//         |                           v             v                                  |
//         |                        +------------------+                                |
//         |                        |        EQ        |                                |
//         |                        +------------------+                                |
//         |                                VW                                          |
//         |                                |                                           |
//         |                                |                                           |
//         |                                v                                           |
//         |                        +------------------+                                |
//         ------------------------>|    IF_BRANCH     |                                |
//                                  +------------------+                                |
//                                   SW             SW                                  |
//                                   |              |                                   |
//                                   v              v                                   |
//                           +--------------+  +--------------+                         |
//                           |   IF_FALSE   |  |   IF_TRUE    |                         |
//                           |  (success)   |  |  (exception) |                         |
//                           +--------------+  +--------------+                         |
//                                 SW                SW   SW                            |
//                                 |                 |    |                             |
//                                 v                 v    |                             |
//     ---------------------------------------------------|-----------------------------|----------------------
//     catch processing                                   |                             |
//                                                        |                             |
//                                                        v                             v
//                                                 +--------------+             +-----------------+
//                                                 |    MERGE     |SW---------->| DEPEND_SELECTOR |
//                                                 +--------------+             +-----------------+
//                                                                                      DW
//                                                                                      |
//                                                                                      v
//                                                                              +-----------------+
//                                                                              |  GET_EXCEPTION  |
//                                                                              +-----------------+

class SlowPathLowering {
public:
    SlowPathLowering(BytecodeCircuitBuilder *bcBuilder, Circuit *circuit, CompilationConfig *cmpCfg,
                     bool enableLog)
        : bcBuilder_(bcBuilder), circuit_(circuit), acc_(circuit),
          argAcc_(circuit), builder_(circuit, cmpCfg),
          dependEntry_(Circuit::GetCircuitRoot(OpCode(OpCode::DEPEND_ENTRY))),
          enableLog_(enableLog) {}
    ~SlowPathLowering() = default;
    void CallRuntimeLowering();

    bool IsLogEnabled() const
    {
        return enableLog_;
    }

private:
    void ReplaceHirControlGate(GateAccessor::UsesIterator &useIt, GateRef newGate, bool noThrow = false);
    void ReplaceHirToSubCfg(GateRef hir, GateRef outir,
                       const std::vector<GateRef> &successControl,
                       const std::vector<GateRef> &exceptionControl,
                       bool noThrow = false);
    void ReplaceHirToCall(GateRef hirGate, GateRef callGate, bool noThrow = false);
    void ReplaceHirToThrowCall(GateRef hirGate, GateRef callGate);
    void LowerExceptionHandler(GateRef hirGate);
    // environment must be initialized
    GateRef GetConstPool(GateRef jsFunc);
    // environment must be initialized
    GateRef GetObjectFromConstPool(GateRef jsFunc, GateRef index);
    // environment must be initialized
    GateRef GetHomeObjectFromJSFunction(GateRef jsFunc);
    GateRef GetValueFromConstStringTable(GateRef glue, GateRef gate, uint32_t inIndex);
    void Lower(GateRef gate);
    void LowerAdd2Dyn(GateRef gate, GateRef glue);
    void LowerCreateIterResultObj(GateRef gate, GateRef glue);
    void SaveFrameToContext(GateRef gate, GateRef glue, GateRef jsFunc);
    void LowerSuspendGenerator(GateRef gate, GateRef glue, [[maybe_unused]]GateRef jsFunc);
    void LowerAsyncFunctionAwaitUncaught(GateRef gate, GateRef glue);
    void LowerAsyncFunctionResolve(GateRef gate, GateRef glue);
    void LowerAsyncFunctionReject(GateRef gate, GateRef glue);
    void LowerLoadStr(GateRef gate, GateRef glue);
    void LowerLexicalEnv(GateRef gate, GateRef glue);
    void LowerStGlobalVar(GateRef gate, GateRef glue);
    void LowerTryLdGlobalByName(GateRef gate, GateRef glue);
    void LowerGetIterator(GateRef gate, GateRef glue);
    void LowerToJSCall(GateRef gate, GateRef glue, const std::vector<GateRef> &args);
    void LowerCallArg0Dyn(GateRef gate, GateRef glue);
    void LowerCallArg1Dyn(GateRef gate, GateRef glue);
    void LowerCallArgs2Dyn(GateRef gate, GateRef glue);
    void LowerCallArgs3Dyn(GateRef gate, GateRef glue);
    void LowerCallIThisRangeDyn(GateRef gate, GateRef glue);
    void LowerCallSpreadDyn(GateRef gate, GateRef glue);
    void LowerCallIRangeDyn(GateRef gate, GateRef glue);
    void LowerNewObjSpreadDyn(GateRef gate, GateRef glue);
    void LowerThrowDyn(GateRef gate, GateRef glue);
    void LowerThrowConstAssignment(GateRef gate, GateRef glue);
    void LowerThrowThrowNotExists(GateRef gate, GateRef glue);
    void LowerThrowPatternNonCoercible(GateRef gate, GateRef glue);
    void LowerThrowIfNotObject(GateRef gate, GateRef glue);
    void LowerThrowUndefinedIfHole(GateRef gate, GateRef glue);
    void LowerThrowIfSuperNotCorrectCall(GateRef gate, GateRef glue);
    void LowerThrowDeleteSuperProperty(GateRef gate, GateRef glue);
    void LowerLdSymbol(GateRef gate, GateRef glue);
    void LowerLdGlobal(GateRef gate, GateRef glue);
    void LowerSub2Dyn(GateRef gate, GateRef glue);
    void LowerMul2Dyn(GateRef gate, GateRef glue);
    void LowerDiv2Dyn(GateRef gate, GateRef glue);
    void LowerMod2Dyn(GateRef gate, GateRef glue);
    void LowerEqDyn(GateRef gate, GateRef glue);
    void LowerNotEqDyn(GateRef gate, GateRef glue);
    void LowerLessDyn(GateRef gate, GateRef glue);
    void LowerLessEqDyn(GateRef gate, GateRef glue);
    void LowerGreaterDyn(GateRef gate, GateRef glue);
    void LowerGreaterEqDyn(GateRef gate, GateRef glue);
    void LowerGetPropIterator(GateRef gate, GateRef glue);
    void LowerIterNext(GateRef gate, GateRef glue);
    void LowerCloseIterator(GateRef gate, GateRef glue);
    void LowerIncDyn(GateRef gate, GateRef glue);
    void LowerDecDyn(GateRef gate, GateRef glue);
    void LowerToNumber(GateRef gate, GateRef glue);
    void LowerNegDyn(GateRef gate, GateRef glue);
    void LowerNotDyn(GateRef gate, GateRef glue);
    void LowerShl2Dyn(GateRef gate, GateRef glue);
    void LowerShr2Dyn(GateRef gate, GateRef glue);
    void LowerAshr2Dyn(GateRef gate, GateRef glue);
    void LowerAnd2Dyn(GateRef gate, GateRef glue);
    void LowerOr2Dyn(GateRef gate, GateRef glue);
    void LowerXor2Dyn(GateRef gate, GateRef glue);
    void LowerDelObjProp(GateRef gate, GateRef glue);
    void LowerExpDyn(GateRef gate, GateRef glue);
    void LowerIsInDyn(GateRef gate, GateRef glue);
    void LowerInstanceofDyn(GateRef gate, GateRef glue);
    void LowerFastStrictNotEqual(GateRef gate, GateRef glue);
    void LowerFastStrictEqual(GateRef gate, GateRef glue);
    void LowerCreateEmptyArray(GateRef gate, GateRef glue);
    void LowerCreateEmptyObject(GateRef gate, GateRef glue);
    void LowerCreateArrayWithBuffer(GateRef gate, GateRef glue, GateRef jsFunc);
    void LowerCreateObjectWithBuffer(GateRef gate, GateRef glue, GateRef jsFunc);
    void LowerStModuleVar(GateRef gate, GateRef glue);
    void LowerGetTemplateObject(GateRef gate, GateRef glue);
    void LowerSetObjectWithProto(GateRef gate, GateRef glue);
    void LowerLdBigInt(GateRef gate, GateRef glue);
    void LowerToNumeric(GateRef gate, GateRef glue);
    void LowerLdModuleVar(GateRef gate, GateRef glue);
    void LowerGetModuleNamespace(GateRef gate, GateRef glue);
    void LowerGetIteratorNext(GateRef gate, GateRef glue);
    void LowerSuperCall(GateRef gate, GateRef glue, GateRef newTarget);
    void LowerSuperCallSpread(GateRef gate, GateRef glue, GateRef newTarget);
    void LowerIsTrueOrFalse(GateRef gate, GateRef glue, bool flag);
    void LowerNewObjDynRange(GateRef gate, GateRef glue);
    void LowerConditionJump(GateRef gate, bool isEqualJump);
    void LowerGetNextPropName(GateRef gate, GateRef glue);
    void LowerCopyDataProperties(GateRef gate, GateRef glue);
    void LowerCreateObjectWithExcludedKeys(GateRef gate, GateRef glue);
    void LowerCreateRegExpWithLiteral(GateRef gate, GateRef glue);
    void LowerStOwnByValue(GateRef gate, GateRef glue);
    void LowerStOwnByIndex(GateRef gate, GateRef glue);
    void LowerStOwnByName(GateRef gate, GateRef glue);
    void LowerDefineFuncDyn(GateRef gate, GateRef glue, GateRef jsFunc);
    void LowerDefineGeneratorFunc(GateRef gate, GateRef glue, GateRef jsFunc);
    void LowerDefineAsyncFunc(GateRef gate, GateRef glue, GateRef jsFunc);
    void LowerNewLexicalEnvDyn(GateRef gate, GateRef glue);
    void LowerNewLexicalEnvWithNameDyn(GateRef gate, GateRef glue, GateRef jsFunc);
    void LowerPopLexicalEnv(GateRef gate, GateRef glue);
    void LowerLdSuperByValue(GateRef gate, GateRef glue, GateRef jsFunc);
    void LowerStSuperByValue(GateRef gate, GateRef glue, GateRef jsFunc);
    void LowerTryStGlobalByName(GateRef gate, GateRef glue);
    void LowerStConstToGlobalRecord(GateRef gate, GateRef glue);
    void LowerStLetToGlobalRecord(GateRef gate, GateRef glue);
    void LowerStClassToGlobalRecord(GateRef gate, GateRef glue);
    void LowerStOwnByValueWithNameSet(GateRef gate, GateRef glue);
    void LowerStOwnByNameWithNameSet(GateRef gate, GateRef glue);
    void LowerLdGlobalVar(GateRef gate, GateRef glue);
    void LowerLdObjByName(GateRef gate, GateRef glue);
    void LowerStObjByName(GateRef gate, GateRef glue);
    void LowerLdSuperByName(GateRef gate, GateRef glue);
    void LowerStSuperByName(GateRef gate, GateRef glue);
    void LowerDefineGetterSetterByValue(GateRef gate, GateRef glue);
    void LowerLdObjByIndex(GateRef gate, GateRef glue);
    void LowerStObjByIndex(GateRef gate, GateRef glue);
    void LowerLdObjByValue(GateRef gate, GateRef glue);
    void LowerStObjByValue(GateRef gate, GateRef glue);
    void LowerCreateGeneratorObj(GateRef gate, GateRef glue);
    void LowerStArraySpread(GateRef gate, GateRef glue);
    void LowerLdLexVarDyn(GateRef gate, GateRef glue);
    void LowerStLexVarDyn(GateRef gate, GateRef glue);
    void LowerCreateObjectHavingMethod(GateRef gate, GateRef glue, GateRef jsFunc);
    void LowerLdHomeObject(GateRef gate, GateRef jsFunc);
    void LowerDefineClassWithBuffer(GateRef gate, GateRef glue, GateRef jsFunc);
    void LowerAsyncFunctionEnter(GateRef gate, GateRef glue);
    void LowerTypeOfDyn(GateRef gate, GateRef glue);
    void LowerResumeGenerator(GateRef gate);
    void LowerGetResumeMode(GateRef gate);
    void LowerDefineNCFuncDyn(GateRef gate, GateRef glue, GateRef jsFunc);
    void LowerDefineMethod(GateRef gate, GateRef glue, GateRef jsFunc);
    void LowerGetUnmappedArgs(GateRef gate, GateRef glue, GateRef actualArgc);
    void LowerCopyRestArgs(GateRef gate, GateRef glue, GateRef actualArgc);
    GateRef LowerCallRuntime(GateRef glue, int index, const std::vector<GateRef> &args, bool useLabel = false);
    int32_t ComputeCallArgc(GateRef gate, EcmaOpcode op);
    GateRef GetValueFromTaggedArray(GateRef arrayGate, GateRef indexOffset);

    BytecodeCircuitBuilder *bcBuilder_;
    Circuit *circuit_;
    GateAccessor acc_;
    ArgumentAccessor argAcc_;
    CircuitBuilder builder_;
    GateRef dependEntry_;
    bool enableLog_ {false};
};
}  // panda::ecmascript::kungfu
#endif  // ECMASCRIPT_COMPILER_SLOWPATH_LOWERING_H
