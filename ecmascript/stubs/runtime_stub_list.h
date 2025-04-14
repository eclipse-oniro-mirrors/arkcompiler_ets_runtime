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

#ifndef ECMASCRIPT_RUNTIME_STUB_LIST_H
#define ECMASCRIPT_RUNTIME_STUB_LIST_H

#include "ecmascript/stubs/test_runtime_stubs.h"

namespace panda::ecmascript {

#define RUNTIME_ASM_STUB_LIST(V)             \
    JS_CALL_TRAMPOLINE_LIST(V)               \
    FAST_CALL_TRAMPOLINE_LIST(V)             \
    ASM_INTERPRETER_TRAMPOLINE_LIST(V)       \
    BASELINE_TRAMPOLINE_LIST(V)

#define ASM_INTERPRETER_TRAMPOLINE_LIST(V)   \
    V(AsmInterpreterEntry)                   \
    V(GeneratorReEnterAsmInterp)             \
    V(PushCallArgsAndDispatchNative)         \
    V(PushCallArg0AndDispatch)               \
    V(PushCallArg1AndDispatch)               \
    V(PushCallArgs2AndDispatch)              \
    V(PushCallArgs3AndDispatch)              \
    V(PushCallThisArg0AndDispatch)           \
    V(PushCallThisArg1AndDispatch)           \
    V(PushCallThisArgs2AndDispatch)          \
    V(PushCallThisArgs3AndDispatch)          \
    V(PushCallRangeAndDispatch)              \
    V(PushCallNewAndDispatch)                \
    V(PushSuperCallAndDispatch)              \
    V(PushCallNewAndDispatchNative)          \
    V(PushNewTargetAndDispatchNative)        \
    V(PushCallRangeAndDispatchNative)        \
    V(PushCallThisRangeAndDispatch)          \
    V(ResumeRspAndDispatch)                  \
    V(ResumeRspAndReturn)                    \
    V(ResumeRspAndReturnBaseline)            \
    V(ResumeCaughtFrameAndDispatch)          \
    V(ResumeUncaughtFrameAndReturn)          \
    V(ResumeRspAndRollback)                  \
    V(CallSetter)                            \
    V(CallGetter)                            \
    V(CallContainersArgs2)                   \
    V(CallContainersArgs3)                   \
    V(CallReturnWithArgv)                    \
    V(CallGetterToBaseline)                  \
    V(CallSetterToBaseline)                  \
    V(CallContainersArgs2ToBaseline)         \
    V(CallContainersArgs3ToBaseline)         \
    V(CallReturnWithArgvToBaseline)          \
    V(ASMFastWriteBarrier)

#define BASELINE_TRAMPOLINE_LIST(V)                   \
    V(CallArg0AndCheckToBaseline)                     \
    V(CallArg1AndCheckToBaseline)                     \
    V(CallArgs2AndCheckToBaseline)                    \
    V(CallArgs3AndCheckToBaseline)                    \
    V(CallThisArg0AndCheckToBaseline)                 \
    V(CallThisArg1AndCheckToBaseline)                 \
    V(CallThisArgs2AndCheckToBaseline)                \
    V(CallThisArgs3AndCheckToBaseline)                \
    V(CallRangeAndCheckToBaseline)                    \
    V(CallNewAndCheckToBaseline)                      \
    V(SuperCallAndCheckToBaseline)                    \
    V(CallThisRangeAndCheckToBaseline)                \
    V(CallArg0AndDispatchFromBaseline)                \
    V(CallArg1AndDispatchFromBaseline)                \
    V(CallArgs2AndDispatchFromBaseline)               \
    V(CallArgs3AndDispatchFromBaseline)               \
    V(CallThisArg0AndDispatchFromBaseline)            \
    V(CallThisArg1AndDispatchFromBaseline)            \
    V(CallThisArgs2AndDispatchFromBaseline)           \
    V(CallThisArgs3AndDispatchFromBaseline)           \
    V(CallRangeAndDispatchFromBaseline)               \
    V(CallNewAndDispatchFromBaseline)                 \
    V(SuperCallAndDispatchFromBaseline)               \
    V(CallThisRangeAndDispatchFromBaseline)           \
    V(CallArg0AndCheckToBaselineFromBaseline)         \
    V(CallArg1AndCheckToBaselineFromBaseline)         \
    V(CallArgs2AndCheckToBaselineFromBaseline)        \
    V(CallArgs3AndCheckToBaselineFromBaseline)        \
    V(CallThisArg0AndCheckToBaselineFromBaseline)     \
    V(CallThisArg1AndCheckToBaselineFromBaseline)     \
    V(CallThisArgs2AndCheckToBaselineFromBaseline)    \
    V(CallThisArgs3AndCheckToBaselineFromBaseline)    \
    V(CallRangeAndCheckToBaselineFromBaseline)        \
    V(CallNewAndCheckToBaselineFromBaseline)          \
    V(SuperCallAndCheckToBaselineFromBaseline)        \
    V(CallThisRangeAndCheckToBaselineFromBaseline)    \
    V(GetBaselineBuiltinFp)

#define JS_CALL_TRAMPOLINE_LIST(V)           \
    V(CallRuntime)                           \
    V(CallRuntimeWithArgv)                   \
    V(JSFunctionEntry)                       \
    V(JSCall)                                \
    V(JSCallWithArgV)                        \
    V(JSCallWithArgVAndPushArgv)             \
    V(JSProxyCallInternalWithArgV)           \
    V(SuperCallWithArgV)                     \
    V(OptimizedCallAndPushArgv)              \
    V(DeoptHandlerAsm)                       \
    V(JSCallNew)                             \
    V(CallOptimized)                         \
    V(AOTCallToAsmInterBridge)               \
    V(FastCallToAsmInterBridge)

#define FAST_CALL_TRAMPOLINE_LIST(V)         \
    V(OptimizedFastCallEntry)                \
    V(OptimizedFastCallAndPushArgv)          \
    V(JSFastCallWithArgV)                    \
    V(JSFastCallWithArgVAndPushArgv)

#define RUNTIME_STUB_WITH_DFX(V)                \
    V(TraceLoadGetter)                          \
    V(TraceLoadSlowPath)                        \
    V(TraceLoadDetail)                          \
    V(TraceLoadEnd)

#define RUNTIME_STUB_WITHOUT_GC_LIST(V)        \
    V(Dump)                                    \
    V(DebugDump)                               \
    V(DumpWithHint)                            \
    V(DebugDumpWithHint)                       \
    V(DebugPrint)                              \
    V(DebugPrintCustom)                        \
    V(DebugPrintInstruction)                   \
    V(CollectingOpcodes)                       \
    V(DebugOsrEntry)                           \
    V(Comment)                                 \
    V(FatalPrint)                              \
    V(FatalPrintCustom)                        \
    V(GetActualArgvNoGC)                       \
    V(InsertOldToNewRSet)                      \
    V(InsertLocalToShareRSet)                  \
    V(SetBitAtomic)                            \
    V(MarkingBarrier)                          \
    V(SharedGCMarkingBarrier)                  \
    V(DoubleToInt)                             \
    V(SaturateTruncDoubleToInt32)           \
    V(FloatMod)                                \
    V(FloatAcos)                               \
    V(FloatAcosh)                              \
    V(FloatAsin)                               \
    V(FloatAsinh)                              \
    V(FloatAtan)                               \
    V(FloatAtan2)                              \
    V(FloatAtanh)                              \
    V(FloatCos)                                \
    V(FloatCosh)                               \
    V(FloatSin)                                \
    V(FloatSinh)                               \
    V(FloatTan)                                \
    V(FloatTanh)                               \
    V(FloatTrunc)                              \
    V(FloatLog)                                \
    V(FloatLog2)                               \
    V(FloatLog10)                              \
    V(FloatLog1p)                              \
    V(FloatExp)                                \
    V(FloatExpm1)                              \
    V(FloatCbrt)                               \
    V(FloatFloor)                              \
    V(FloatPow)                                \
    V(FloatCeil)                               \
    V(CallDateNow)                             \
    V(UpdateFieldType)                         \
    V(BigIntEquals)                            \
    V(TimeClip)                                \
    V(SetDateValues)                           \
    V(StartCallTimer)                          \
    V(EndCallTimer)                            \
    V(BigIntSameValueZero)                     \
    V(JSHClassFindProtoTransitions)            \
    V(FinishObjSizeTracking)                   \
    V(NumberHelperStringToDouble)              \
    V(GetStringToListCacheArray)               \
    V(IntLexicographicCompare)                 \
    V(DoubleLexicographicCompare)              \
    V(FastArraySortString)                     \
    V(StringToNumber)                          \
    V(StringGetStart)                          \
    V(StringGetEnd)                            \
    V(ArrayTrim)                               \
    V(SortTypedArray)                          \
    V(ReverseTypedArray)                       \
    V(CopyTypedArrayBuffer)                    \
    V(IsFastRegExp)                            \
    V(CreateLocalToShare)                      \
    V(CreateOldToNew)                          \
    V(ObjectCopy)                              \
    V(FillObject)                              \
    V(ReverseArray)                            \
    V(LrInt)                                   \
    V(FindPatchModule)                         \
    V(FatalPrintMisstakenResolvedBinding)      \
    V(LoadNativeModuleFailed)

#define RUNTIME_STUB_WITH_GC_LIST(V)            \
    V(HeapAlloc)                                \
    V(AllocateInYoung)                          \
    V(AllocateInOld)                            \
    V(AllocateInSOld)                           \
    V(AllocateInSNonMovable)                    \
    V(TypedArraySpeciesCreate)                  \
    V(TypedArrayCreateSameType)                 \
    V(CallInternalGetter)                       \
    V(CallInternalSetter)                       \
    V(CallGetPrototype)                         \
    V(RegularJSObjDeletePrototype)              \
    V(CallJSObjDeletePrototype)                 \
    V(ToPropertyKey)                            \
    V(NewJSPrimitiveRef)                        \
    V(ThrowTypeError)                           \
    V(MismatchError)                            \
    V(GetHash32)                                \
    V(ComputeHashcode)                          \
    V(NewInternalString)                        \
    V(NewTaggedArray)                           \
    V(NewCOWTaggedArray)                        \
    V(NewMutantTaggedArray)                     \
    V(NewCOWMutantTaggedArray)                  \
    V(CopyArray)                                \
    V(NumberToString)                           \
    V(IntToString)                              \
    V(RTSubstitution)                           \
    V(NameDictPutIfAbsent)                      \
    V(NameDictionaryGetAllEnumKeys)             \
    V(NumberDictionaryGetAllEnumKeys)           \
    V(PropertiesSetValue)                       \
    V(JSArrayReduceUnStable)                    \
    V(JSArrayFilterUnStable)                    \
    V(JSArrayMapUnStable)                       \
    V(CheckAndCopyArray)                        \
    V(NewEcmaHClass)                            \
    V(UpdateLayOutAndAddTransition)             \
    V(CopyAndUpdateObjLayout)                   \
    V(UpdateHClassForElementsKind)              \
    V(RuntimeDump)                              \
    V(ForceGC)                                  \
    V(NoticeThroughChainAndRefreshUser)         \
    V(JumpToCInterpreter)                       \
    V(StGlobalRecord)                           \
    V(SetFunctionNameNoPrefix)                  \
    V(StOwnByValueWithNameSet)                  \
    V(StOwnByName)                              \
    V(StOwnByNameWithNameSet)                   \
    V(SuspendGenerator)                         \
    V(UpFrame)                                  \
    V(Neg)                                      \
    V(Not)                                      \
    V(Inc)                                      \
    V(Dec)                                      \
    V(Shl2)                                     \
    V(Shr2)                                     \
    V(Ashr2)                                    \
    V(Or2)                                      \
    V(Xor2)                                     \
    V(And2)                                     \
    V(Exp)                                      \
    V(IsIn)                                     \
    V(InstanceOf)                               \
    V(CreateGeneratorObj)                       \
    V(ThrowConstAssignment)                     \
    V(GetTemplateObject)                        \
    V(CreateStringIterator)                     \
    V(NewJSArrayIterator)                       \
    V(NewJSTypedArrayIterator)                  \
    V(MapIteratorNext)                          \
    V(SetIteratorNext)                          \
    V(StringIteratorNext)                       \
    V(ArrayIteratorNext)                        \
    V(IteratorReturn)                           \
    V(GetNextPropName)                          \
    V(GetNextPropNameSlowpath)                  \
    V(ThrowIfNotObject)                         \
    V(IterNext)                                 \
    V(CloseIterator)                            \
    V(SuperCallSpread)                          \
    V(OptSuperCallSpread)                       \
    V(GetCallSpreadArgs)                        \
    V(DelObjProp)                               \
    V(NewObjApply)                              \
    V(CreateIterResultObj)                      \
    V(AsyncFunctionAwaitUncaught)               \
    V(AsyncFunctionResolveOrReject)             \
    V(ThrowUndefinedIfHole)                     \
    V(CopyDataProperties)                       \
    V(StArraySpread)                            \
    V(GetIteratorNext)                          \
    V(SetObjectWithProto)                       \
    V(LoadICByValue)                            \
    V(StoreICByValue)                           \
    V(StoreOwnICByValue)                        \
    V(StOwnByValue)                             \
    V(LdSuperByValue)                           \
    V(StSuperByValue)                           \
    V(StObjByValue)                             \
    V(LdObjByIndex)                             \
    V(StObjByIndex)                             \
    V(StOwnByIndex)                             \
    V(CreateClassWithBuffer)                    \
    V(CreateSharedClass)                        \
    V(LdSendableClass)                          \
    V(SetClassConstructorLength)                \
    V(LoadICByName)                             \
    V(StoreICByName)                            \
    V(StoreOwnICByName)                         \
    V(UpdateHotnessCounter)                     \
    V(CheckSafePoint)                           \
    V(PGODump)                                  \
    V(PGOPreDump)                               \
    V(JitCompile)                               \
    V(CountInterpExecFuncs)                     \
    V(BaselineJitCompile)                       \
    V(UpdateHotnessCounterWithProf)             \
    V(GetModuleNamespaceByIndex)                \
    V(GetModuleNamespaceByIndexOnJSFunc)        \
    V(GetModuleNamespace)                       \
    V(StModuleVarByIndex)                       \
    V(StModuleVarByIndexOnJSFunc)               \
    V(StModuleVar)                              \
    V(LdLocalModuleVarByIndex)                  \
    V(LdLocalModuleVarByIndexWithModule)        \
    V(LdExternalModuleVarByIndex)               \
    V(LdExternalModuleVarByIndexWithModule)     \
    V(LdSendableExternalModuleVarByIndex)       \
    V(LdSendableLocalModuleVarByIndex)          \
    V(LdLazyExternalModuleVarByIndex)           \
    V(LdLazySendableExternalModuleVarByIndex)   \
    V(LdLocalModuleVarByIndexOnJSFunc)          \
    V(LdExternalModuleVarByIndexOnJSFunc)       \
    V(LdModuleVar)                              \
    V(ProcessModuleLoadInfo)                    \
    V(GetModuleName)                            \
    V(ThrowExportsIsHole)                       \
    V(NewResolvedIndexBindingRecord)            \
    V(HandleResolutionIsNullOrString)           \
    V(CheckAndThrowModuleError)                 \
    V(GetResolvedRecordIndexBindingModule)      \
    V(GetResolvedRecordBindingModule)           \
    V(Throw)                                    \
    V(GetPropIterator)                          \
    V(GetPropIteratorSlowpath)                  \
    V(PrimitiveStringCreate)                    \
    V(AsyncFunctionEnter)                       \
    V(GetIterator)                              \
    V(GetAsyncIterator)                         \
    V(SetGeneratorState)                        \
    V(ThrowThrowNotExists)                      \
    V(ThrowPatternNonCoercible)                 \
    V(ThrowDeleteSuperProperty)                 \
    V(Eq)                                       \
    V(TryLdGlobalICByName)                      \
    V(LoadMiss)                                 \
    V(StoreMiss)                                \
    V(TryUpdateGlobalRecord)                    \
    V(ThrowReferenceError)                      \
    V(StGlobalVar)                              \
    V(LdGlobalICVar)                            \
    V(ToIndex)                                  \
    V(NewJSObjectByConstructor)                 \
    V(CloneHclass)                              \
    V(AllocateTypedArrayBuffer)                 \
    V(ToNumber)                                 \
    V(ToBoolean)                                \
    V(NotEq)                                    \
    V(Less)                                     \
    V(LessEq)                                   \
    V(Greater)                                  \
    V(GreaterEq)                                \
    V(Add2)                                     \
    V(Sub2)                                     \
    V(Mul2)                                     \
    V(Div2)                                     \
    V(Mod2)                                     \
    V(CreateEmptyObject)                        \
    V(CreateEmptyArray)                         \
    V(GetSymbolFunction)                        \
    V(GetUnmapedArgs)                           \
    V(CopyRestArgs)                             \
    V(CreateArrayWithBuffer)                    \
    V(CreateObjectWithBuffer)                   \
    V(NewThisObject)                            \
    V(NewObjRange)                              \
    V(DefineFunc)                               \
    V(CreateRegExpWithLiteral)                  \
    V(ThrowIfSuperNotCorrectCall)               \
    V(CreateObjectHavingMethod)                 \
    V(CreateObjectWithExcludedKeys)             \
    V(DefineMethod)                             \
    V(SetPatchModule)                           \
    V(ThrowSetterIsUndefinedException)          \
    V(ThrowNotCallableException)                \
    V(ThrowCallConstructorException)            \
    V(ThrowNonConstructorException)             \
    V(ThrowStackOverflowException)              \
    V(ThrowDerivedMustReturnException)          \
    V(CallSpread)                               \
    V(DefineGetterSetterByValue)                \
    V(SuperCall)                                \
    V(OptSuperCall)                             \
    V(LdBigInt)                                 \
    V(ToNumeric)                                \
    V(ToNumericConvertBigInt)                   \
    V(CallBigIntAsIntN)                         \
    V(CallBigIntAsUintN)                        \
    V(DynamicImport)                            \
    V(CreateAsyncGeneratorObj)                  \
    V(AsyncGeneratorResolve)                    \
    V(AsyncGeneratorReject)                     \
    V(NewLexicalEnvWithName)                    \
    V(NewSendableEnv)                           \
    V(OptGetUnmapedArgs)                        \
    V(OptCopyRestArgs)                          \
    V(NotifyBytecodePcChanged)                  \
    V(NotifyDebuggerStatement)                  \
    V(MethodEntry)                              \
    V(MethodExit)                               \
    V(OptNewLexicalEnvWithName)                 \
    V(OptSuspendGenerator)                      \
    V(OptAsyncGeneratorResolve)                 \
    V(OptCreateObjectWithExcludedKeys)          \
    V(OptNewObjRange)                           \
    V(GetTypeArrayPropertyByIndex)              \
    V(SetTypeArrayPropertyByIndex)              \
    V(FastCopyElementToArray)                   \
    V(GetPropertyByName)                        \
    V(JSObjectGetMethod)                        \
    V(DebugAOTPrint)                            \
    V(ProfileOptimizedCode)                     \
    V(ProfileTypedOp)                           \
    V(VerifyVTableLoading)                      \
    V(VerifyVTableStoring)                      \
    V(GetMethodFromCache)                       \
    V(GetArrayLiteralFromCache)                 \
    V(GetObjectLiteralFromCache)                \
    V(GetStringFromCache)                       \
    V(OptLdSuperByValue)                        \
    V(OptStSuperByValue)                        \
    V(BigIntEqual)                              \
    V(StringEqual)                              \
    V(StringIndexOf)                            \
    V(LdPatchVar)                               \
    V(StPatchVar)                               \
    V(DeoptHandler)                             \
    V(ContainerRBTreeForEach)                   \
    V(InsertStringToTable)                      \
    V(SlowFlattenString)                        \
    V(NotifyConcurrentResult)                   \
    V(DefineField)                              \
    V(CreatePrivateProperty)                    \
    V(DefinePrivateProperty)                    \
    V(LdPrivateProperty)                        \
    V(StPrivateProperty)                        \
    V(TestIn)                                   \
    V(UpdateAOTHClass)                          \
    V(AotInlineTrace)                           \
    V(AotInlineBuiltinTrace)                    \
    V(LocaleCompare)                            \
    V(ArraySort)                                \
    V(FastStringify)                            \
    V(ObjectSlowAssign)                         \
    V(GetLinkedHash)                            \
    V(LinkedHashMapComputeCapacity)             \
    V(LinkedHashSetComputeCapacity)             \
    V(JSObjectGrowElementsCapacity)             \
    V(HClassCloneWithAddProto)                  \
    V(LocaleCompareWithGc)                      \
    V(ParseInt)                                 \
    V(LocaleCompareCacheable)                   \
    V(ArrayForEachContinue)                     \
    V(NumberDictionaryPut)                      \
    V(ThrowRangeError)                          \
    V(InitializeGeneratorFunction)              \
    V(FunctionDefineOwnProperty)                \
    V(DefineOwnProperty)                        \
    V(AOTEnableProtoChangeMarker)               \
    V(CheckGetTrapResult)                       \
    V(CheckSetTrapResult)                       \
    V(JSProxyGetProperty)                       \
    V(JSProxySetProperty)                       \
    V(JSProxyHasProperty)                       \
    V(JSTypedArrayHasProperty)                  \
    V(ModuleNamespaceHasProperty)               \
    V(JSObjectHasProperty)                      \
    V(HasProperty)                              \
    V(DumpObject)                               \
    V(DumpHeapObjectAddress)                    \
    V(TryGetInternString)                       \
    V(FastCopyFromArrayToTypedArray)            \
    V(BigIntConstructor)                        \
    V(ObjectPrototypeHasOwnProperty)            \
    V(ReflectHas)                               \
    V(ReflectConstruct)                         \
    V(ReflectApply)                             \
    V(FunctionPrototypeApply)                   \
    V(FunctionPrototypeBind)                    \
    V(FunctionPrototypeCall)                    \
    V(SetPrototypeTransition)                   \
    V(GetSharedModule)                          \
    V(SuperCallForwardAllArgs)                  \
    V(OptSuperCallForwardAllArgs)               \
    V(GetCollationValueFromIcuCollator)         \
    V(DecodeURIComponent)                       \
    V(GetAllFlagsInternal)                      \
    V(SlowSharedObjectStoreBarrier)             \
    V(GetNativePcOfstForBaseline)               \
    V(AotCallBuiltinTrace)                      \
    V(NumberBigIntNativePointerToString)

#define RUNTIME_STUB_LIST(V)                     \
    RUNTIME_ASM_STUB_LIST(V)                     \
    RUNTIME_STUB_WITHOUT_GC_LIST(V)              \
    RUNTIME_STUB_WITH_GC_LIST(V)                 \
    RUNTIME_STUB_WITH_DFX(V)                     \
    TEST_RUNTIME_STUB_GC_LIST(V)

}  // namespace panda::ecmascript
#endif // ECMASCRIPT_RUNTIME_STUB_LIST_H
