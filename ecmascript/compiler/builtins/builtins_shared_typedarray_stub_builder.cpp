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

#include "ecmascript/compiler/builtins/builtins_shared_typedarray_stub_builder.h"

#include "ecmascript/base/typed_array_helper-inl.h"
#include "ecmascript/compiler/new_object_stub_builder.h"
#include "ecmascript/js_array.h"
#include "ecmascript/js_typed_array.h"
#include "ecmascript/shared_objects/js_shared_typed_array.h"

namespace panda::ecmascript::kungfu {

// @ref panda::ecmascript::base::TypedArrayHelper::SharedTypedArrayConstructor(), all argument forms.
void BuiltinsSharedTypedArrayStubBuilder::GenSharedTypedArrayConstructor(GateRef glue,
    GateRef nativeCode, GateRef func, GateRef newTarget, GateRef thisValue, GateRef numArgs,
    size_t ctorFuncField, size_t rootHClassField, size_t onHeapHClassField,
    const DataViewType arrayType)
{
    auto env = GetEnvironment();
    DEFVARIABLE(returnValue, VariableType::JS_ANY(), Undefined());

    Label newTargetObject(env);
    Label newTargetFunction(env);
    Label slowPath(env);
    Label slowPath1(env);
    Label hasException(env);
    Label exit(env);

    // 1. If NewTarget is undefined, throw exception in slowPath
    BRANCH(TaggedIsHeapObject(newTarget), &newTargetObject, &slowPath);
    Bind(&newTargetObject);
    BRANCH(IsJSFunction(glue, newTarget), &newTargetFunction, &slowPath);
    Bind(&newTargetFunction);
    {
        Label fastGetHClass(env);
        GateRef globalEnv = GetCurrentGlobalEnv();
        auto nativeCtorFunc = GetGlobalEnvValue(VariableType::JS_ANY(), glue, globalEnv,
            ctorFuncField);
        GateRef rootHClass = GetGlobalEnvValue(VariableType::JS_ANY(), glue, globalEnv,
            rootHClassField);
        GateRef isHClass = IsJSHClass(glue, rootHClass);
        GateRef isNativeCtor = Equal(nativeCtorFunc, newTarget);
        // Native-constructor-only fast path
        BRANCH(LogicAndBuilder(env).And(isNativeCtor).And(isHClass).Done(), &fastGetHClass, &slowPath1);
        Bind(&fastGetHClass);
        {
            GateRef arg0 = GetArgFromArgv(glue, IntPtr(0), numArgs, true);

            // Constructor input forms:
            //   1. constructor()                          -> Form A (arg0 undefined -> length 0)
            //   2. constructor(length: number)            -> Form A (ToIndex; input guard keeps
            //      only undefined / non-negative Smi in IR, everything else re-enters C++)
            //   3. constructor(elements: Iterable<number>) -> object dispatch: ordinary
            //      object (not StableJSArray) -> C++ slowpath
            //   4. constructor(array: ArrayLike<number>)   -> object dispatch: StableJSArray
            //      -> Form C (IR copy); other ArrayLike -> C++ slowpath
            //   5. constructor(buffer: SendableArrayBuffer, byteOffset?, length?) -> Form B
            //   TypedArray/SharedTypedArray/ArrayBuffer/SharedArrayBuffer/OrdinaryObject arg0 -> C++ slowpath
            Label isEcmaObj(env);
            Label notEcmaObj(env);
            BRANCH(IsEcmaObject(glue, arg0), &isEcmaObj, &notEcmaObj);

            // ---- Form A: length (non-ECMAObject arg0) ----
            // Constructor forms 1 (no args -> undefined -> length 0) and 2 (length).
            Bind(&notEcmaObj);
            {
                Label lenGuardPass(env);
                BRANCH(IsUndefOrNonNegativeSmi(arg0), &lenGuardPass, &slowPath1);
                Bind(&lenGuardPass);
                {
                    DEFVARIABLE(elementLength, VariableType::INT32(), Int32(0));
                    DEFVARIABLE(elementLengthTagged, VariableType::JS_ANY(), IntToTaggedPtr(Int32(0)));
                    Label isUndef(env);
                    Label notUndef(env);
                    Label lenDone(env);
                    BRANCH(TaggedIsUndefined(arg0), &isUndef, &notUndef);
                    Bind(&notUndef);
                    {
                        // @ref SharedTypedArrayConstructor: ToIndex: under the input
                        // guard arg0 is a non-negative Smi, so ToIndex is the identity —
                        // read it directly (no exception path).
                        elementLength = TaggedGetInt(arg0);
                        elementLengthTagged = arg0;
                        Jump(&isUndef);
                    }
                    Bind(&isUndef);
                    {
                        GateRef obj = AllocateSharedTypedArray(glue, rootHClass, *elementLength,
                            *elementLengthTagged, onHeapHClassField, arrayType);
                        BRANCH(HasPendingException(glue), &hasException, &lenDone);
                        Bind(&lenDone);
                        {
                            returnValue = obj;
                            Jump(&exit);
                        }
                    }
                }
            }

            // ---- ECMAObject arg0: dispatch between the buffer / array forms ----
            Bind(&isEcmaObj);
            {
                Label isSendableBuffer(env);
                Label notSendableBuffer(env);
                // @ref SharedTypedArrayConstructor: firstArg->IsSendableArrayBuffer().
                BRANCH(IsJSObjectType(glue, arg0, JSType::JS_SENDABLE_ARRAY_BUFFER),
                    &isSendableBuffer, &notSendableBuffer);

                // ---- Form B: constructor(buffer: SendableArrayBuffer [, byteOffset [, length ] ]) ----
                // Constructor form 5
                Bind(&isSendableBuffer);
                {
                    Label bShellDone(env);
                    returnValue = AllocateSharedTypedArray(glue, rootHClass, arrayType);
                    BRANCH(HasPendingException(glue), &hasException, &bShellDone);
                    Bind(&bShellDone);
                    {
                        CreateFromSendableArrayBuffer(&returnValue, glue, numArgs, arg0, &exit,
                            &slowPath1, arrayType);
                    }
                }

                // ---- Form C: constructor(array: StableJSArray) — the fast sub-case of form 4 ----
                // Constructor form 4 (ArrayLike input) when arg0 passes the FULL C++
                // IsStableJSArray(thread) guard.
                Bind(&notSendableBuffer);
                {
                    Label isStable(env);
                    BRANCH(IsStableJSArray(glue, arg0), &isStable, &slowPath1);
                    Bind(&isStable);
                    {
                        Label cShellDone(env);
                        returnValue = AllocateSharedTypedArray(glue, rootHClass, arrayType);
                        BRANCH(HasPendingException(glue), &hasException, &cShellDone);
                        Bind(&cShellDone);
                        {
                            FastCopyElementFromArray(&returnValue, glue, arg0, rootHClass,
                                onHeapHClassField, &exit, &slowPath1, arrayType);
                        }
                    }
                }
            }
        }
    }

    Bind(&hasException);
    {
        returnValue = Exception();
        Jump(&exit);
    }

    // Slow path with newTarget: re-enter the C++ constructor entry.
    Bind(&slowPath1);
    {
        returnValue = CallBuiltinRuntimeWithNewTarget(glue,
            {glue, nativeCode, func, thisValue, numArgs, GetArgv(), newTarget});
        Jump(&exit);
    }
    // Slow path without newTarget: re-enter the C++ entry.
    Bind(&slowPath);
    {
        returnValue = CallBuiltinRuntime(glue,
            {glue, nativeCode, func, thisValue, numArgs, GetArgv()}, true);
        Jump(&exit);
    }

    Bind(&exit);
    Return(*returnValue);
}

// @ref TypedArrayHelper::CreateFromSendableArrayBuffer
void BuiltinsSharedTypedArrayStubBuilder::CreateFromSendableArrayBuffer(Variable *result,
    GateRef glue, GateRef numArgs, GateRef buffer, Label *exit, Label *slowPath,
    const DataViewType arrayType)
{
    auto env = GetEnvironment();
    GateRef elementSize = Int32(base::TypedArrayHelper::GetSizeFromType(arrayType));
    GateRef byteOffset = GetArgFromArgv(glue, IntPtr(1), numArgs, true); // 1: byteOffset
    Label offsetGuardPass(env);
    BRANCH(IsUndefOrNonNegativeSmi(byteOffset), &offsetGuardPass, slowPath);
    Bind(&offsetGuardPass);
    {
        // @ref :offset = ToIndex(byteOffset)
        // under the IsUndefOrNonNegativeSmi guard: ToIndex = undefined keeps the 0 init + a Smi reads directly.
        DEFVARIABLE(offset, VariableType::INT32(), Int32(0));
        Label offsetSetValue(env);
        Label offsetOk(env);
        BRANCH(TaggedIsUndefined(byteOffset), &offsetOk, &offsetSetValue);
        Bind(&offsetSetValue);
        {
            offset = TaggedGetInt(byteOffset);
            Jump(&offsetOk);
        }
        Bind(&offsetOk);
        {
            // @ref :offset modulo elementSize != 0 -> RangeError.
            Label offsetAligned(env);
            BRANCH(Int32Equal(Int32Mod(*offset, elementSize), Int32(0)), &offsetAligned, slowPath);
            Bind(&offsetAligned);
            {
                // @ref :newLength = ToIndex(length)
                GateRef length = GetArgFromArgv(glue, IntPtr(2), numArgs, true); // 2: length
                Label lenGuardPass(env);
                BRANCH(IsUndefOrNonNegativeSmi(length), &lenGuardPass, slowPath);
                Bind(&lenGuardPass);
                {
                    // @ref :IsDetachedBuffer
                    Label notDetached(env);
                    BRANCH(IsSendableArrayBufferDetached(glue, buffer), slowPath, &notDetached);
                    Bind(&notDetached);
                    {
                        // @ref :bufferByteLength is read after the detached check.
                        GateRef bufferByteLength = GetSendableArrayBufferByteLength(buffer);
                        DEFVARIABLE(newByteLength, VariableType::INT64(), Int64(0));
                        Label lenUndef(env);
                        Label lenNotUndef(env);
                        Label setProps(env);
                        BRANCH(TaggedIsUndefined(length), &lenUndef, &lenNotUndef);
                        Bind(&lenNotUndef);
                        {
                            // @ref :newByteLength = newLength * elementSize;
                            // offset + newByteLength > bufferByteLength -> RangeError.
                            GateRef newLength = TaggedGetInt(length);
                            newByteLength = Int64Mul(ZExtInt32ToInt64(newLength),
                                ZExtInt32ToInt64(elementSize));
                            BRANCH(Int64UnsignedGreaterThan(
                                Int64Add(ZExtInt32ToInt64(*offset), *newByteLength),
                                ZExtInt32ToInt64(bufferByteLength)),
                                slowPath, &setProps);
                        }
                        Bind(&lenUndef);
                        {
                            // @ref :bufferByteLength % elementSize != 0 ->
                            // RangeError; bufferByteLength < offset -> RangeError.
                            Label aligned(env);
                            Label boundsOk(env);
                            BRANCH(Int32Equal(Int32Mod(bufferByteLength, elementSize), Int32(0)),
                                &aligned, slowPath);
                            Bind(&aligned);
                            BRANCH(Int32UnsignedLessThan(bufferByteLength, *offset),
                                slowPath, &boundsOk);
                            Bind(&boundsOk);
                            newByteLength = ZExtInt32ToInt64(Int32Sub(bufferByteLength, *offset));
                            Jump(&setProps);
                        }
                        // @ref TypedArrayHelper::SetArrayBufferProperties.
                        Bind(&setProps);
                        {
                            SetViewedArrayBufferOrByteArray(glue, result->ReadVariable(), buffer);
                            SetByteLength(glue, result->ReadVariable(),
                                TruncInt64ToInt32(*newByteLength));
                            SetByteOffset(glue, result->ReadVariable(), *offset);
                            SetTypedArrayLength(glue, result->ReadVariable(), TruncInt64ToInt32(
                                Int64Div(*newByteLength, ZExtInt32ToInt64(elementSize))));
                            Jump(exit);
                        }
                    }
                }
            }
        }
    }
}

// @ref TypedArrayHelper::FastCopyElementFromArrayForCtor<TypedArrayKind::SHARED>
void BuiltinsSharedTypedArrayStubBuilder::FastCopyElementFromArray(Variable *result,
    GateRef glue, GateRef array, GateRef rootHClass, size_t onHeapHClassField,
    Label *exit, Label *slowPath, const DataViewType arrayType)
{
    auto env = GetEnvironment();
    Label lenInRange(env);
    Label hasException(env);
    Label elementsOk(env);
    Label copyNotEx(env);

    GateRef len = LoadPrimitive(VariableType::INT32(), array,
        IntPtr(panda::ecmascript::JSArray::LENGTH_OFFSET));
    // 2^31 upper bound: dispatch oversized lengths to the C++ entry.
    BRANCH(Int32GreaterThanOrEqual(len, Int32(0)), &lenInRange, slowPath);
    Bind(&lenInRange);
    // @ref :load-on-demand check — elements shorter than length re-enters
    // C++ (CreateFromOrdinaryObject path).
    GateRef elements = GetElementsArray(glue, array);
    BRANCH(Int32UnsignedLessThan(GetLengthOfTaggedArray(elements), len), slowPath, &elementsOk);
    Bind(&elementsOk);
    {
        // @ref :AllocateTypedArrayBuffer<SHARED> on the pre-allocated shell.
        AllocateSharedTypedArrayBuffer(glue, result->ReadVariable(), len, IntToTaggedInt(len),
            rootHClass, onHeapHClassField, arrayType);
        // @ref :RETURN_EXCEPTION_IF_ABRUPT
        BRANCH(HasPendingException(glue), &hasException, &copyNotEx);
        Bind(&copyNotEx);
        {
            // @ref CopyArrayToTypedArrayForCtor<SHARED>.
            CallRuntimeWithGlobalEnv(glue, GetCurrentGlobalEnv(),
                RTSTUB_ID(CopyArrayToSharedTypedArrayForCtor),
                { result->ReadVariable(), IntToTaggedInt(len), array });
            BRANCH(HasPendingException(glue), &hasException, exit);
        }
    }
    Bind(&hasException);
    {
        result->WriteVariable(Exception());
        Jump(exit);
    }
}

// Fast-path input guard: everything else re-enters the C++ constructor.
GateRef BuiltinsSharedTypedArrayStubBuilder::IsUndefOrNonNegativeSmi(GateRef value)
{
    auto env = GetEnvironment();
    Label entry(env);
    env->SubCfgEntry(&entry);
    DEFVARIABLE(result, VariableType::BOOL(), False());
    Label exit(env);
    Label isUndefined(env);
    Label checkInt(env);
    Label checkNonNeg(env);
    BRANCH(TaggedIsUndefined(value), &isUndefined, &checkInt);
    Bind(&isUndefined);
    {
        result = True();
        Jump(&exit);
    }
    Bind(&checkInt);
    {
        Label nonNegSmi(env);
        BRANCH(TaggedIsInt(value), &checkNonNeg, &exit);
        Bind(&checkNonNeg);
        BRANCH(Int32GreaterThanOrEqual(TaggedGetInt(value), Int32(0)), &nonNegSmi, &exit);
        Bind(&nonNegSmi);
        {
            result = True();
            Jump(&exit);
        }
    }
    Bind(&exit);
    auto ret = *result;
    env->SubCfgExit();
    return ret;
}

// @ref TypedArrayHelper::AllocateSharedTypedArray(thread, newTarget,
// arrayType) — the 3-arg overload.
GateRef BuiltinsSharedTypedArrayStubBuilder::AllocateSharedTypedArray(GateRef glue,
    GateRef rootHClass, const DataViewType arrayType)
{
    auto env = GetEnvironment();
    Label entry(env);
    env->SubCfgEntry(&entry);
    DEFVARIABLE(result, VariableType::JS_ANY(), Undefined());
    Label exit(env);
    Label hasException(env);
    Label notException(env);

    NewObjectStubBuilder newBuilder(this);
    newBuilder.SetParameters(glue, IntPtr(0));
    result = newBuilder.NewSObject(glue, rootHClass);
    // The exception check precedes every field store.
    BRANCH(HasPendingException(glue), &hasException, &notException);
    Bind(&hasException);
    {
        result = Exception();
        Jump(&exit);
    }
    Bind(&notException);
    {
        // initialize SharedTypedArray
        SetViewedArrayBufferOrByteArray(glue, *result, Undefined(), MemoryAttribute::NoBarrier());
        SetContentType(glue, *result, Int8(static_cast<uint8_t>(arrayType)));
        SetByteLength(glue, *result, Int32(0));
        SetByteOffset(glue, *result, Int32(0));
        SetTypedArrayLength(glue, *result, Int32(0));
        Store(VariableType::INT32(), glue, *result, IntPtr(JSSharedTypedArray::MOD_RECORD_OFFSET),
            Int32(0));
        Jump(&exit);
    }
    Bind(&exit);
    auto ret = *result;
    env->SubCfgExit();
    return ret;
}

// @ref TypedArrayHelper::AllocateSharedTypedArray(thread, newTarget,
// length, arrayType) — the 4-arg overload.
GateRef BuiltinsSharedTypedArrayStubBuilder::AllocateSharedTypedArray(GateRef glue,
    GateRef rootHClass, GateRef length, GateRef lengthTagged, size_t onHeapHClassField,
    const DataViewType arrayType)
{
    auto env = GetEnvironment();
    Label entry(env);
    env->SubCfgEntry(&entry);
    DEFVARIABLE(result, VariableType::JS_ANY(), Undefined());
    Label exit(env);
    Label hasException(env);
    Label shellDone(env);

    GateRef obj = AllocateSharedTypedArray(glue, rootHClass, arrayType);
    BRANCH(HasPendingException(glue), &hasException, &shellDone);
    Bind(&shellDone);
    {
        result = AllocateSharedTypedArrayBuffer(glue, obj, length, lengthTagged,
            rootHClass, onHeapHClassField, arrayType);
        BRANCH(HasPendingException(glue), &hasException, &exit);
    }
    Bind(&hasException);
    {
        result = Exception();
        Jump(&exit);
    }
    Bind(&exit);
    auto ret = *result;
    env->SubCfgExit();
    return ret;
}

// @ref TypedArrayHelper::AllocateTypedArrayBuffer<SHARED>.
GateRef BuiltinsSharedTypedArrayStubBuilder::AllocateSharedTypedArrayBuffer(GateRef glue,
    GateRef typedArray, GateRef length, GateRef lengthTagged, GateRef rootHClass,
    size_t onHeapHClassField, const DataViewType arrayType)
{
    auto env = GetEnvironment();
    Label subentry(env);
    env->SubCfgEntry(&subentry);
    DEFVARIABLE(result, VariableType::JS_ANY(), typedArray);
    Label exit(env);
    Label slowPath(env);
    Label next(env);

    // No IR mirror of the C++ length > MAX check: callers pass guard-derived lengths in [0, 2^31)
    GateRef elementSize = Int32(base::TypedArrayHelper::GetSizeFromType(arrayType));
    GateRef byteLength = Int64Mul(ZExtInt32ToInt64(elementSize), ZExtInt32ToInt64(length));
    BRANCH(Int64UnsignedLessThanOrEqual(byteLength, Int64(JSTypedArray::MAX_ONHEAP_LENGTH)),
        &next, &slowPath);
    Bind(&next);
    {
        // On-heap path: shared-heap ByteArray.
        NewObjectStubBuilder newObjectStubBuilder(this);
        newObjectStubBuilder.SetParameters(glue, 0);
        GateRef byteArray = newObjectStubBuilder.NewSByteArray(glue, elementSize, length);
        // The shell's hclass is the root hclass by native-construction,
        // so the on-heap slot is used directly.
        GateRef onHeapHclass = GetGlobalEnvValue(VariableType::JS_ANY(), glue,
            GetCurrentGlobalEnv(), onHeapHClassField);
#if ECMASCRIPT_ENABLE_IC
        // IC-gated like the C++ JSHClass::NotifyHclassChanged.
        NotifyHClassChanged(glue, rootHClass, onHeapHclass);
#endif
        TransitionHClass(glue, typedArray, onHeapHclass, MemoryAttribute::NeedBarrierAndAtomic());
        // The check sits after Notify/Transition and before the field stores,
        // like the C++ — the failure path also performs the transition.
        Label hasException(env);
        Label notException(env);
        BRANCH(HasPendingException(glue), &hasException, &notException);
        Bind(&hasException);
        {
            result = Exception();
            Jump(&exit);
        }
        Bind(&notException);
        {
            SetContentType(glue, typedArray, Int8(static_cast<uint8_t>(arrayType)));
            SetViewedArrayBufferOrByteArray(glue, typedArray, byteArray);
            SetByteLength(glue, typedArray, TruncInt64ToInt32(byteLength));
            SetByteOffset(glue, typedArray, Int32(0));
            SetTypedArrayLength(glue, typedArray, length);
            Jump(&exit);
        }
    }
    Bind(&slowPath);
    {
        // Off-heap path: the RTSTUB allocates the SendableArrayBuffer
        result = CallRuntimeWithGlobalEnv(glue, GetCurrentGlobalEnv(),
            RTSTUB_ID(AllocateSharedTypedArrayBuffer), { typedArray, lengthTagged });
        Jump(&exit);
    }
    Bind(&exit);
    auto ret = *result;
    env->SubCfgExit();
    return ret;
}

}  // namespace panda::ecmascript::kungfu
