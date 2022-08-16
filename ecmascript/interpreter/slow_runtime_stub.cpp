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

#include "ecmascript/interpreter/slow_runtime_stub.h"

#include "ecmascript/base/number_helper.h"
#include "ecmascript/builtins/builtins_regexp.h"
#include "ecmascript/global_dictionary-inl.h"
#include "ecmascript/ic/profile_type_info.h"
#include "ecmascript/interpreter/frame_handler.h"
#include "ecmascript/interpreter/interpreter-inl.h"
#include "ecmascript/interpreter/slow_runtime_helper.h"
#include "ecmascript/jspandafile/program_object.h"
#include "ecmascript/jspandafile/scope_info_extractor.h"
#include "ecmascript/js_arguments.h"
#include "ecmascript/js_array.h"
#include "ecmascript/js_array_iterator.h"
#include "ecmascript/js_async_function.h"
#include "ecmascript/js_async_generator_object.h"
#include "ecmascript/js_for_in_iterator.h"
#include "ecmascript/js_function.h"
#include "ecmascript/js_generator_object.h"
#include "ecmascript/js_hclass-inl.h"
#include "ecmascript/js_iterator.h"
#include "ecmascript/js_promise.h"
#include "ecmascript/js_proxy.h"
#include "ecmascript/js_tagged_value-inl.h"
#include "ecmascript/js_thread.h"
#include "ecmascript/module/js_module_manager.h"
#include "ecmascript/tagged_dictionary.h"
#include "ecmascript/runtime_call_id.h"
#include "ecmascript/template_string.h"
#include "ecmascript/stubs/runtime_stubs-inl.h"

namespace panda::ecmascript {
JSTaggedValue SlowRuntimeStub::CallSpreadDyn(JSThread *thread, JSTaggedValue func, JSTaggedValue obj,
                                             JSTaggedValue array)
{
    INTERPRETER_TRACE(thread, CallSpreadDyn);
    [[maybe_unused]] EcmaHandleScope handleScope(thread);

    JSHandle<JSTaggedValue> jsFunc(thread, func);
    JSHandle<JSTaggedValue> jsArray(thread, array);
    JSHandle<JSTaggedValue> taggedObj(thread, obj);
    return RuntimeStubs::RuntimeCallSpreadDyn(thread, jsFunc, taggedObj, jsArray);
}

JSTaggedValue SlowRuntimeStub::NegDyn(JSThread *thread, JSTaggedValue value)
{
    INTERPRETER_TRACE(thread, NegDyn);
    [[maybe_unused]] EcmaHandleScope handleScope(thread);

    JSHandle<JSTaggedValue> inputTag(thread, value);
    return RuntimeStubs::RuntimeNegDyn(thread, inputTag);
}

JSTaggedValue SlowRuntimeStub::AsyncFunctionEnter(JSThread *thread)
{
    INTERPRETER_TRACE(thread, AsyncFunctionEnter);
    [[maybe_unused]] EcmaHandleScope handleScope(thread);

    return RuntimeStubs::RuntimeAsyncFunctionEnter(thread);
}

JSTaggedValue SlowRuntimeStub::ToNumber(JSThread *thread, JSTaggedValue value)
{
    INTERPRETER_TRACE(thread, Tonumber);
    [[maybe_unused]] EcmaHandleScope handleScope(thread);

    JSHandle<JSTaggedValue> number(thread, value);
    // may return exception
    return RuntimeStubs::RuntimeToNumber(thread, number);
}

JSTaggedValue SlowRuntimeStub::ToNumeric(JSThread *thread, JSTaggedValue value)
{
    INTERPRETER_TRACE(thread, Tonumeric);
    [[maybe_unused]] EcmaHandleScope handleScope(thread);

    JSHandle<JSTaggedValue> numeric(thread, value);
    // may return exception
    return RuntimeStubs::RuntimeToNumeric(thread, numeric);
}

JSTaggedValue SlowRuntimeStub::NotDyn(JSThread *thread, JSTaggedValue value)
{
    INTERPRETER_TRACE(thread, NotDyn);
    [[maybe_unused]] EcmaHandleScope handleScope(thread);

    JSHandle<JSTaggedValue> inputTag(thread, value);
    return RuntimeStubs::RuntimeNotDyn(thread, inputTag);
}

JSTaggedValue SlowRuntimeStub::IncDyn(JSThread *thread, JSTaggedValue value)
{
    INTERPRETER_TRACE(thread, IncDyn);
    [[maybe_unused]] EcmaHandleScope handleScope(thread);

    JSHandle<JSTaggedValue> inputTag(thread, value);
    return RuntimeStubs::RuntimeIncDyn(thread, inputTag);
}

JSTaggedValue SlowRuntimeStub::DecDyn(JSThread *thread, JSTaggedValue value)
{
    INTERPRETER_TRACE(thread, DecDyn);
    [[maybe_unused]] EcmaHandleScope handleScope(thread);

    JSHandle<JSTaggedValue> inputTag(thread, value);
    return RuntimeStubs::RuntimeDecDyn(thread, inputTag);
}

void SlowRuntimeStub::ThrowDyn(JSThread *thread, JSTaggedValue value)
{
    INTERPRETER_TRACE(thread, ThrowDyn);
    RuntimeStubs::RuntimeThrowDyn(thread, value);
}

JSTaggedValue SlowRuntimeStub::GetPropIterator(JSThread *thread, JSTaggedValue value)
{
    INTERPRETER_TRACE(thread, GetPropIterator);
    [[maybe_unused]] EcmaHandleScope handleScope(thread);

    JSHandle<JSTaggedValue> objHandle(thread, value);
    return RuntimeStubs::RuntimeGetPropIterator(thread, objHandle);
}

void SlowRuntimeStub::ThrowConstAssignment(JSThread *thread, JSTaggedValue value)
{
    INTERPRETER_TRACE(thread, ThrowConstAssignment);
    [[maybe_unused]] EcmaHandleScope handleScope(thread);

    JSHandle<EcmaString> name(thread, value.GetTaggedObject());
    return RuntimeStubs::RuntimeThrowConstAssignment(thread, name);
}

JSTaggedValue SlowRuntimeStub::Add2Dyn(JSThread *thread, JSTaggedValue left, JSTaggedValue right)
{
    INTERPRETER_TRACE(thread, Add2Dyn);
    [[maybe_unused]] EcmaHandleScope handleScope(thread);

    JSHandle<JSTaggedValue> leftValue(thread, left);
    JSHandle<JSTaggedValue> rightValue(thread, right);
    return RuntimeStubs::RuntimeAdd2Dyn(thread, leftValue, rightValue);
}

JSTaggedValue SlowRuntimeStub::Sub2Dyn(JSThread *thread, JSTaggedValue left, JSTaggedValue right)
{
    INTERPRETER_TRACE(thread, Sub2Dyn);
    [[maybe_unused]] EcmaHandleScope handleScope(thread);

    JSHandle<JSTaggedValue> leftTag(thread, left);
    JSHandle<JSTaggedValue> rightTag(thread, right);
    return RuntimeStubs::RuntimeSub2Dyn(thread, leftTag, rightTag);
}

JSTaggedValue SlowRuntimeStub::Mul2Dyn(JSThread *thread, JSTaggedValue left, JSTaggedValue right)
{
    INTERPRETER_TRACE(thread, Mul2Dyn);
    [[maybe_unused]] EcmaHandleScope handleScope(thread);

    JSHandle<JSTaggedValue> leftTag(thread, left);
    JSHandle<JSTaggedValue> rightTag(thread, right);
    return RuntimeStubs::RuntimeMul2Dyn(thread, leftTag, rightTag);
}

JSTaggedValue SlowRuntimeStub::Div2Dyn(JSThread *thread, JSTaggedValue left, JSTaggedValue right)
{
    INTERPRETER_TRACE(thread, Div2Dyn);
    [[maybe_unused]] EcmaHandleScope handleScope(thread);

    JSHandle<JSTaggedValue> leftTag(thread, left);
    JSHandle<JSTaggedValue> rightTag(thread, right);
    return RuntimeStubs::RuntimeDiv2Dyn(thread, leftTag, rightTag);
}

JSTaggedValue SlowRuntimeStub::Mod2Dyn(JSThread *thread, JSTaggedValue left, JSTaggedValue right)
{
    INTERPRETER_TRACE(thread, Mod2Dyn);
    [[maybe_unused]] EcmaHandleScope handleScope(thread);

    JSHandle<JSTaggedValue> leftTag(thread, left);
    JSHandle<JSTaggedValue> rightTag(thread, right);
    return RuntimeStubs::RuntimeMod2Dyn(thread, leftTag, rightTag);
}

JSTaggedValue SlowRuntimeStub::EqDyn(JSThread *thread, JSTaggedValue left, JSTaggedValue right)
{
    INTERPRETER_TRACE(thread, EqDyn);
    [[maybe_unused]] EcmaHandleScope handleScope(thread);

    JSHandle<JSTaggedValue> leftValue(thread, left);
    JSHandle<JSTaggedValue> rightValue(thread, right);
    return RuntimeStubs::RuntimeEqDyn(thread, leftValue, rightValue);
}

JSTaggedValue SlowRuntimeStub::NotEqDyn(JSThread *thread, JSTaggedValue left, JSTaggedValue right)
{
    INTERPRETER_TRACE(thread, NotEqDyn);
    [[maybe_unused]] EcmaHandleScope handleScope(thread);

    JSHandle<JSTaggedValue> leftValue(thread, left);
    JSHandle<JSTaggedValue> rightValue(thread, right);
    return RuntimeStubs::RuntimeNotEqDyn(thread, leftValue, rightValue);
}

JSTaggedValue SlowRuntimeStub::LessDyn(JSThread *thread, JSTaggedValue left, JSTaggedValue right)
{
    INTERPRETER_TRACE(thread, LessDyn);
    [[maybe_unused]] EcmaHandleScope handleScope(thread);

    JSHandle<JSTaggedValue> leftValue(thread, left);
    JSHandle<JSTaggedValue> rightValue(thread, right);
    return RuntimeStubs::RuntimeLessDyn(thread, leftValue, rightValue);
}

JSTaggedValue SlowRuntimeStub::LessEqDyn(JSThread *thread, JSTaggedValue left, JSTaggedValue right)
{
    INTERPRETER_TRACE(thread, LessEqDyn);
    [[maybe_unused]] EcmaHandleScope handleScope(thread);

    JSHandle<JSTaggedValue> leftValue(thread, left);
    JSHandle<JSTaggedValue> rightValue(thread, right);
    return RuntimeStubs::RuntimeLessEqDyn(thread, leftValue, rightValue);
}

JSTaggedValue SlowRuntimeStub::GreaterDyn(JSThread *thread, JSTaggedValue left, JSTaggedValue right)
{
    INTERPRETER_TRACE(thread, GreaterDyn);
    [[maybe_unused]] EcmaHandleScope handleScope(thread);

    JSHandle<JSTaggedValue> leftValue(thread, left);
    JSHandle<JSTaggedValue> rightValue(thread, right);
    return RuntimeStubs::RuntimeGreaterDyn(thread, leftValue, rightValue);
}

JSTaggedValue SlowRuntimeStub::GreaterEqDyn(JSThread *thread, JSTaggedValue left, JSTaggedValue right)
{
    INTERPRETER_TRACE(thread, GreaterEqDyn);
    [[maybe_unused]] EcmaHandleScope handleScope(thread);

    JSHandle<JSTaggedValue> leftValue(thread, left);
    JSHandle<JSTaggedValue> rightValue(thread, right);
    return RuntimeStubs::RuntimeGreaterEqDyn(thread, leftValue, rightValue);
}

JSTaggedValue SlowRuntimeStub::Shl2Dyn(JSThread *thread, JSTaggedValue left, JSTaggedValue right)
{
    INTERPRETER_TRACE(thread, Shl2Dyn);
    [[maybe_unused]] EcmaHandleScope handleScope(thread);

    JSHandle<JSTaggedValue> leftTag(thread, left);
    JSHandle<JSTaggedValue> rightTag(thread, right);
    return RuntimeStubs::RuntimeShl2Dyn(thread, leftTag, rightTag);
}

JSTaggedValue SlowRuntimeStub::Shr2Dyn(JSThread *thread, JSTaggedValue left, JSTaggedValue right)
{
    INTERPRETER_TRACE(thread, Shr2Dyn);
    [[maybe_unused]] EcmaHandleScope handleScope(thread);

    JSHandle<JSTaggedValue> leftTag(thread, left);
    JSHandle<JSTaggedValue> rightTag(thread, right);
    return RuntimeStubs::RuntimeShr2Dyn(thread, leftTag, rightTag);
}

JSTaggedValue SlowRuntimeStub::Ashr2Dyn(JSThread *thread, JSTaggedValue left, JSTaggedValue right)
{
    INTERPRETER_TRACE(thread, Ashr2Dyn);
    [[maybe_unused]] EcmaHandleScope handleScope(thread);

    JSHandle<JSTaggedValue> leftTag(thread, left);
    JSHandle<JSTaggedValue> rightTag(thread, right);
    return RuntimeStubs::RuntimeAshr2Dyn(thread, leftTag, rightTag);
}

JSTaggedValue SlowRuntimeStub::And2Dyn(JSThread *thread, JSTaggedValue left, JSTaggedValue right)
{
    INTERPRETER_TRACE(thread, And2Dyn);
    [[maybe_unused]] EcmaHandleScope handleScope(thread);
    JSHandle<JSTaggedValue> leftTag(thread, left);
    JSHandle<JSTaggedValue> rightTag(thread, right);
    return RuntimeStubs::RuntimeAnd2Dyn(thread, leftTag, rightTag);
}

JSTaggedValue SlowRuntimeStub::Or2Dyn(JSThread *thread, JSTaggedValue left, JSTaggedValue right)
{
    INTERPRETER_TRACE(thread, Or2Dyn);
    [[maybe_unused]] EcmaHandleScope handleScope(thread);

    JSHandle<JSTaggedValue> leftTag(thread, left);
    JSHandle<JSTaggedValue> rightTag(thread, right);
    return RuntimeStubs::RuntimeOr2Dyn(thread, leftTag, rightTag);
}

JSTaggedValue SlowRuntimeStub::Xor2Dyn(JSThread *thread, JSTaggedValue left, JSTaggedValue right)
{
    INTERPRETER_TRACE(thread, Xor2Dyn);
    [[maybe_unused]] EcmaHandleScope handleScope(thread);

    JSHandle<JSTaggedValue> leftTag(thread, left);
    JSHandle<JSTaggedValue> rightTag(thread, right);
    return RuntimeStubs::RuntimeXor2Dyn(thread, leftTag, rightTag);
}

JSTaggedValue SlowRuntimeStub::ToJSTaggedValueWithInt32(JSThread *thread, JSTaggedValue value)
{
    INTERPRETER_TRACE(thread, ToJSTaggedValueWithInt32);
    [[maybe_unused]] EcmaHandleScope handleScope(thread);
    JSHandle<JSTaggedValue> valueHandle(thread, value);
    return RuntimeStubs::RuntimeToJSTaggedValueWithInt32(thread, valueHandle);
}

JSTaggedValue SlowRuntimeStub::ToJSTaggedValueWithUint32(JSThread *thread, JSTaggedValue value)
{
    INTERPRETER_TRACE(thread, ToJSTaggedValueWithUint32);
    [[maybe_unused]] EcmaHandleScope handleScope(thread);
    JSHandle<JSTaggedValue> valueHandle(thread, value);
    return RuntimeStubs::RuntimeToJSTaggedValueWithUint32(thread, valueHandle);
}

JSTaggedValue SlowRuntimeStub::DelObjProp(JSThread *thread, JSTaggedValue obj, JSTaggedValue prop)
{
    INTERPRETER_TRACE(thread, Delobjprop);
    [[maybe_unused]] EcmaHandleScope handleScope(thread);

    JSHandle<JSTaggedValue> objHandle(thread, obj);
    JSHandle<JSTaggedValue> propHandle(thread, prop);
    return RuntimeStubs::RuntimeDelObjProp(thread, objHandle, propHandle);
}

JSTaggedValue SlowRuntimeStub::NewObjDynRange(JSThread *thread, JSTaggedValue func, JSTaggedValue newTarget,
                                              uint16_t firstArgIdx, uint16_t length)
{
    INTERPRETER_TRACE(thread, NewobjDynrange);
    [[maybe_unused]] EcmaHandleScope handleScope(thread);

    JSHandle<JSTaggedValue> funcHandle(thread, func);
    JSHandle<JSTaggedValue> newTargetHandle(thread, newTarget);
    return RuntimeStubs::RuntimeNewObjDynRange(thread, funcHandle, newTargetHandle, firstArgIdx, length);
}

JSTaggedValue SlowRuntimeStub::CreateObjectWithExcludedKeys(JSThread *thread, uint16_t numKeys, JSTaggedValue objVal,
                                                            uint16_t firstArgRegIdx)
{
    INTERPRETER_TRACE(thread, CreateObjectWithExcludedKeys);
    [[maybe_unused]] EcmaHandleScope handleScope(thread);

    JSHandle<JSTaggedValue> obj(thread, objVal);
    return RuntimeStubs::RuntimeCreateObjectWithExcludedKeys(thread, numKeys, obj, firstArgRegIdx);
}

JSTaggedValue SlowRuntimeStub::ExpDyn(JSThread *thread, JSTaggedValue base, JSTaggedValue exponent)
{
    INTERPRETER_TRACE(thread, ExpDyn);
    [[maybe_unused]] EcmaHandleScope handleScope(thread);

    return RuntimeStubs::RuntimeExpDyn(thread, base, exponent);
}

JSTaggedValue SlowRuntimeStub::IsInDyn(JSThread *thread, JSTaggedValue prop, JSTaggedValue obj)
{
    INTERPRETER_TRACE(thread, IsInDyn);
    [[maybe_unused]] EcmaHandleScope handleScope(thread);

    JSHandle<JSTaggedValue> propHandle(thread, prop);
    JSHandle<JSTaggedValue> objHandle(thread, obj);
    return RuntimeStubs::RuntimeIsInDyn(thread, propHandle, objHandle);
}

JSTaggedValue SlowRuntimeStub::InstanceofDyn(JSThread *thread, JSTaggedValue obj, JSTaggedValue target)
{
    INTERPRETER_TRACE(thread, InstanceofDyn);
    [[maybe_unused]] EcmaHandleScope handleScope(thread);

    JSHandle<JSTaggedValue> objHandle(thread, obj);
    JSHandle<JSTaggedValue> targetHandle(thread, target);
    return RuntimeStubs::RuntimeInstanceofDyn(thread, objHandle, targetHandle);
}

JSTaggedValue SlowRuntimeStub::NewLexicalEnvDyn(JSThread *thread, uint16_t numVars)
{
    INTERPRETER_TRACE(thread, NewlexenvDyn);
    [[maybe_unused]] EcmaHandleScope handleScope(thread);

    return RuntimeStubs::RuntimeNewLexicalEnvDyn(thread, numVars);
}

JSTaggedValue SlowRuntimeStub::NewLexicalEnvWithNameDyn(JSThread *thread, uint16_t numVars, uint16_t scopeId)
{
    INTERPRETER_TRACE(thread, NewlexenvwithNameDyn);
    [[maybe_unused]] EcmaHandleScope handleScope(thread);

    return RuntimeStubs::RuntimeNewLexicalEnvWithNameDyn(thread, numVars, scopeId);
}

JSTaggedValue SlowRuntimeStub::CreateIterResultObj(JSThread *thread, JSTaggedValue value, JSTaggedValue flag)
{
    INTERPRETER_TRACE(thread, CreateIterResultObj);
    [[maybe_unused]] EcmaHandleScope handleScope(thread);

    JSHandle<JSTaggedValue> valueHandle(thread, value);
    return RuntimeStubs::RuntimeCreateIterResultObj(thread, valueHandle, flag);
}

JSTaggedValue SlowRuntimeStub::CreateGeneratorObj(JSThread *thread, JSTaggedValue genFunc)
{
    INTERPRETER_TRACE(thread, CreateGeneratorObj);
    [[maybe_unused]] EcmaHandleScope handleScope(thread);

    JSHandle<JSTaggedValue> generatorFunction(thread, genFunc);
    return RuntimeStubs::RuntimeCreateGeneratorObj(thread, generatorFunction);
}

JSTaggedValue SlowRuntimeStub::CreateAsyncGeneratorObj(JSThread *thread, JSTaggedValue genFunc)
{
    INTERPRETER_TRACE(thread, CreateAsyncGeneratorObj);
    [[maybe_unused]] EcmaHandleScope handleScope(thread);

    ObjectFactory *factory = thread->GetEcmaVM()->GetFactory();
    JSHandle<JSTaggedValue> asyncGeneratorFunction(thread, genFunc);
    JSHandle<JSAsyncGeneratorObject> obj = factory->NewJSAsyncGeneratorObject(asyncGeneratorFunction);
    JSHandle<GeneratorContext> context = factory->NewGeneratorContext();
    context->SetGeneratorObject(thread, obj.GetTaggedValue());
    // change state to SUSPENDED_START
    obj->SetAsyncGeneratorState(JSAsyncGeneratorState::SUSPENDED_START);
    obj->SetGeneratorContext(thread, context);
    return obj.GetTaggedValue();
}

JSTaggedValue SlowRuntimeStub::SuspendGenerator(JSThread *thread, JSTaggedValue genObj, JSTaggedValue value)
{
    INTERPRETER_TRACE(thread, SuspendGenerator);
    [[maybe_unused]] EcmaHandleScope handleScope(thread);

    JSHandle<JSTaggedValue> genObjHandle(thread, genObj);
    if (genObjHandle->IsGeneratorObject()) {
        JSHandle<JSObject> obj = JSTaggedValue::ToObject(thread, genObjHandle);
        RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
        JSHandle<JSGeneratorObject> generatorObjectHandle = JSHandle<JSGeneratorObject>::Cast(obj);
        JSHandle<GeneratorContext> genContextHandle(thread, generatorObjectHandle->GetGeneratorContext());
        return SuspendGeneratorHelper(thread, generatorObjectHandle, genContextHandle, value);
    }
    if (genObjHandle->IsAsyncGeneratorObject()) {
        JSHandle<JSObject> obj = JSTaggedValue::ToObject(thread, genObjHandle);
        RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
        JSHandle<JSAsyncGeneratorObject> generatorObjectHandle = JSHandle<JSAsyncGeneratorObject>::Cast(obj);
        JSHandle<GeneratorContext> genContextHandle(thread, generatorObjectHandle->GetGeneratorContext());
        return SuspendAsyncGeneratorHelper(thread, generatorObjectHandle, genContextHandle, value);
    }
    return JSTaggedValue::Undefined();
}

JSTaggedValue SlowRuntimeStub::SuspendGeneratorHelper(JSThread *thread,
                                                      JSHandle<JSGeneratorObject> generatorObjectHandle,
                                                      JSHandle<GeneratorContext> genContextHandle, JSTaggedValue value)
{
    JSHandle<JSTaggedValue> valueHandle(thread, value);
    // save stack, should copy cur_frame, function execute over will free cur_frame
    SlowRuntimeHelper::SaveFrameToContext(thread, genContextHandle);
    RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
    // change state to SuspendedYield
    if (generatorObjectHandle->IsExecuting()) {
        generatorObjectHandle->SetGeneratorState(JSGeneratorState::SUSPENDED_YIELD);
        return valueHandle.GetTaggedValue();
    }
    return generatorObjectHandle.GetTaggedValue();
}

JSTaggedValue SlowRuntimeStub::SuspendAsyncGeneratorHelper(JSThread *thread,
                                                           JSHandle<JSAsyncGeneratorObject> generatorObjectHandle,
                                                           JSHandle<GeneratorContext> genContextHandle,
                                                           JSTaggedValue value)
{
    JSHandle<JSTaggedValue> valueHandle(thread, value);
    // save stack, should copy cur_frame, function execute over will free cur_frame
    SlowRuntimeHelper::SaveFrameToContext(thread, genContextHandle);
    RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
    // change state to SuspendedYield
    if (generatorObjectHandle->IsExecuting()) {
        generatorObjectHandle->SetAsyncGeneratorState(JSAsyncGeneratorState::SUSPENDED_YIELD);
        return valueHandle.GetTaggedValue();
    }
    return generatorObjectHandle.GetTaggedValue();
}

JSTaggedValue SlowRuntimeStub::AsyncFunctionAwaitUncaught(JSThread *thread, JSTaggedValue asyncFuncObj,
                                                          JSTaggedValue value)
{
    INTERPRETER_TRACE(thread, AsyncFunctionAwaitUncaught);
    [[maybe_unused]] EcmaHandleScope handleScope(thread);
    JSHandle<JSTaggedValue> asyncFuncObjHandle(thread, asyncFuncObj);
    JSHandle<JSTaggedValue> valueHandle(thread, value);

    JSAsyncFunction::AsyncFunctionAwait(thread, asyncFuncObjHandle, valueHandle);
    if (asyncFuncObjHandle->IsAsyncGeneratorObject()) {
        JSHandle<JSObject> obj = JSTaggedValue::ToObject(thread, asyncFuncObjHandle);
        RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
        JSHandle<JSAsyncGeneratorObject> generator = JSHandle<JSAsyncGeneratorObject>::Cast(obj);
        JSHandle<TaggedQueue> queue(thread, generator->GetAsyncGeneratorQueue());
        if (queue->Empty()) {
            return JSTaggedValue::Undefined();
        }
        JSHandle<AsyncGeneratorRequest> next(thread, queue->Front());
        JSHandle<PromiseCapability> completion(thread, next->GetCapability());
        JSHandle<JSPromise> promise(thread, completion->GetPromise());
        return promise.GetTaggedValue();
    }
    JSHandle<JSAsyncFuncObject> asyncFunc(asyncFuncObjHandle);
    JSHandle<JSPromise> promise(thread, asyncFunc->GetPromise());
    RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
    return promise.GetTaggedValue();
}

JSTaggedValue SlowRuntimeStub::AsyncFunctionResolveOrReject(JSThread *thread, JSTaggedValue asyncFuncObj,
                                                            JSTaggedValue value, bool is_resolve)
{
    INTERPRETER_TRACE(thread, AsyncFunctionResolveOrReject);
    [[maybe_unused]] EcmaHandleScope handleScope(thread);

    JSHandle<JSTaggedValue> asyncFuncObjHandle(thread, asyncFuncObj);
    JSHandle<JSTaggedValue> valueHandle(thread, value);
    return RuntimeStubs::RuntimeAsyncFunctionResolveOrReject(thread, asyncFuncObjHandle, valueHandle, is_resolve);
}

JSTaggedValue SlowRuntimeStub::NewObjSpreadDyn(JSThread *thread, JSTaggedValue func, JSTaggedValue newTarget,
                                               JSTaggedValue array)
{
    INTERPRETER_TRACE(thread, NewobjspreadDyn);
    [[maybe_unused]] EcmaHandleScope handleScope(thread);

    JSHandle<JSTaggedValue> funcHandle(thread, func);
    JSHandle<JSTaggedValue> newTargetHandle(thread, newTarget);
    JSHandle<JSTaggedValue> jsArray(thread, array);
    return RuntimeStubs::RuntimeNewObjSpreadDyn(thread, funcHandle, newTargetHandle, jsArray);
}

void SlowRuntimeStub::ThrowUndefinedIfHole(JSThread *thread, JSTaggedValue obj)
{
    INTERPRETER_TRACE(thread, ThrowUndefinedIfHole);
    [[maybe_unused]] EcmaHandleScope handleScope(thread);

    JSHandle<EcmaString> name(thread, obj);
    return RuntimeStubs::RuntimeThrowUndefinedIfHole(thread, name);
}

JSTaggedValue SlowRuntimeStub::ThrowIfSuperNotCorrectCall(JSThread *thread, uint16_t index, JSTaggedValue thisValue)
{
    INTERPRETER_TRACE(thread, ThrowIfSuperNotCorrectCall);
    [[maybe_unused]] EcmaHandleScope handleScope(thread);

    return RuntimeStubs::RuntimeThrowIfSuperNotCorrectCall(thread, index, thisValue);
}

void SlowRuntimeStub::ThrowIfNotObject(JSThread *thread)
{
    INTERPRETER_TRACE(thread, ThrowIfNotObject);
    [[maybe_unused]] EcmaHandleScope handleScope(thread);

    THROW_TYPE_ERROR(thread, "Inner return result is not object");
}

void SlowRuntimeStub::ThrowThrowNotExists(JSThread *thread)
{
    INTERPRETER_TRACE(thread, ThrowThrowNotExists);
    [[maybe_unused]] EcmaHandleScope handleScope(thread);

    return RuntimeStubs::RuntimeThrowIfNotObject(thread);
}

void SlowRuntimeStub::ThrowPatternNonCoercible(JSThread *thread)
{
    INTERPRETER_TRACE(thread, ThrowPatternNonCoercible);
    [[maybe_unused]] EcmaHandleScope handleScope(thread);

    return RuntimeStubs::RuntimeThrowPatternNonCoercible(thread);
}

JSTaggedValue SlowRuntimeStub::StOwnByName(JSThread *thread, JSTaggedValue obj, JSTaggedValue prop, JSTaggedValue value)
{
    INTERPRETER_TRACE(thread, StOwnByNameDyn);
    [[maybe_unused]] EcmaHandleScope handleScope(thread);

    JSHandle<JSTaggedValue> objHandle(thread, obj);
    JSHandle<JSTaggedValue> propHandle(thread, prop);
    JSHandle<JSTaggedValue> valueHandle(thread, value);
    return RuntimeStubs::RuntimeStOwnByName(thread, objHandle, propHandle, valueHandle);
}

JSTaggedValue SlowRuntimeStub::StOwnByNameWithNameSet(JSThread *thread, JSTaggedValue obj, JSTaggedValue prop,
                                                      JSTaggedValue value)
{
    INTERPRETER_TRACE(thread, StOwnByNameDyn);
    [[maybe_unused]] EcmaHandleScope handleScope(thread);

    JSHandle<JSTaggedValue> objHandle(thread, obj);
    JSHandle<JSTaggedValue> propHandle(thread, prop);
    JSHandle<JSTaggedValue> valueHandle(thread, value);
    return RuntimeStubs::RuntimeStOwnByNameWithNameSet(thread, objHandle, propHandle, valueHandle);
}

JSTaggedValue SlowRuntimeStub::StOwnByIndex(JSThread *thread, JSTaggedValue obj, uint32_t idx, JSTaggedValue value)
{
    INTERPRETER_TRACE(thread, StOwnByIdDyn);
    [[maybe_unused]] EcmaHandleScope handleScope(thread);

    JSHandle<JSTaggedValue> objHandle(thread, obj);
    JSHandle<JSTaggedValue> idxHandle(thread, JSTaggedValue(idx));
    JSHandle<JSTaggedValue> valueHandle(thread, value);
    return RuntimeStubs::RuntimeStOwnByIndex(thread, objHandle, idxHandle, valueHandle);
}

JSTaggedValue SlowRuntimeStub::StOwnByValue(JSThread *thread, JSTaggedValue obj, JSTaggedValue key, JSTaggedValue value)
{
    INTERPRETER_TRACE(thread, StOwnByValueDyn);
    [[maybe_unused]] EcmaHandleScope handleScope(thread);

    JSHandle<JSTaggedValue> objHandle(thread, obj);
    JSHandle<JSTaggedValue> keyHandle(thread, key);
    JSHandle<JSTaggedValue> valueHandle(thread, value);
    return RuntimeStubs::RuntimeStOwnByValue(thread, objHandle, keyHandle, valueHandle);
}

JSTaggedValue SlowRuntimeStub::StOwnByValueWithNameSet(JSThread *thread, JSTaggedValue obj, JSTaggedValue key,
                                                       JSTaggedValue value)
{
    INTERPRETER_TRACE(thread, StOwnByValueDyn);
    [[maybe_unused]] EcmaHandleScope handleScope(thread);

    JSHandle<JSTaggedValue> objHandle(thread, obj);
    JSHandle<JSTaggedValue> keyHandle(thread, key);
    JSHandle<JSTaggedValue> valueHandle(thread, value);
    return RuntimeStubs::RuntimeStOwnByValueWithNameSet(thread, objHandle, keyHandle, valueHandle);
}

JSTaggedValue SlowRuntimeStub::CreateEmptyArray(JSThread *thread, ObjectFactory *factory, JSHandle<GlobalEnv> globalEnv)
{
    INTERPRETER_TRACE(thread, CreateEmptyArray);
    [[maybe_unused]] EcmaHandleScope handleScope(thread);
    return RuntimeStubs::RuntimeCreateEmptyArray(thread, factory, globalEnv);
}

JSTaggedValue SlowRuntimeStub::CreateEmptyObject(JSThread *thread, ObjectFactory *factory,
                                                 JSHandle<GlobalEnv> globalEnv)
{
    INTERPRETER_TRACE(thread, CreateEmptyObject);
    [[maybe_unused]] EcmaHandleScope handleScope(thread);

    return RuntimeStubs::RuntimeCreateEmptyObject(thread, factory, globalEnv);
}

JSTaggedValue SlowRuntimeStub::CreateObjectWithBuffer(JSThread *thread, ObjectFactory *factory, JSObject *literal)
{
    INTERPRETER_TRACE(thread, CreateObjectWithBuffer);
    [[maybe_unused]] EcmaHandleScope handleScope(thread);

    JSHandle<JSObject> obj(thread, literal);
    return RuntimeStubs::RuntimeCreateObjectWithBuffer(thread, factory, obj);
}

JSTaggedValue SlowRuntimeStub::CreateObjectHavingMethod(JSThread *thread, ObjectFactory *factory, JSObject *literal,
                                                        JSTaggedValue env, ConstantPool *constpool)
{
    INTERPRETER_TRACE(thread, CreateObjectHavingMethod);
    [[maybe_unused]] EcmaHandleScope handleScope(thread);

    JSHandle<JSObject> obj(thread, literal);
    JSHandle<JSTaggedValue> envi(thread, env);
    JSHandle<JSTaggedValue> pool(thread, constpool);
    return RuntimeStubs::RuntimeCreateObjectHavingMethod(thread, factory, obj, envi, pool);
}

JSTaggedValue SlowRuntimeStub::SetObjectWithProto(JSThread *thread, JSTaggedValue proto, JSTaggedValue obj)
{
    INTERPRETER_TRACE(thread, SetObjectWithProto);
    [[maybe_unused]] EcmaHandleScope handleScope(thread);

    JSHandle<JSTaggedValue> protoHandle(thread, proto);
    JSHandle<JSObject> objHandle(thread, obj);
    return RuntimeStubs::RuntimeSetObjectWithProto(thread, protoHandle, objHandle);
}

JSTaggedValue SlowRuntimeStub::IterNext(JSThread *thread, JSTaggedValue iter)
{
    INTERPRETER_TRACE(thread, IterNext);
    [[maybe_unused]] EcmaHandleScope handleScope(thread);

    JSHandle<JSTaggedValue> iterHandle(thread, iter);
    return RuntimeStubs::RuntimeIterNext(thread, iterHandle);
}

JSTaggedValue SlowRuntimeStub::CloseIterator(JSThread *thread, JSTaggedValue iter)
{
    INTERPRETER_TRACE(thread, CloseIterator);
    [[maybe_unused]] EcmaHandleScope handleScope(thread);

    JSHandle<JSTaggedValue> iterHandle(thread, iter);
    return RuntimeStubs::RuntimeCloseIterator(thread, iterHandle);
}

void SlowRuntimeStub::StModuleVar(JSThread *thread, JSTaggedValue key, JSTaggedValue value)
{
    INTERPRETER_TRACE(thread, StModuleVar);
    [[maybe_unused]] EcmaHandleScope scope(thread);

    return RuntimeStubs::RuntimeStModuleVar(thread, key, value);
}

JSTaggedValue SlowRuntimeStub::LdModuleVar(JSThread *thread, JSTaggedValue key, bool inner)
{
    INTERPRETER_TRACE(thread, LdModuleVar);
    [[maybe_unused]] EcmaHandleScope scope(thread);

    return RuntimeStubs::RuntimeLdModuleVar(thread, key, inner);
}

JSTaggedValue SlowRuntimeStub::CreateRegExpWithLiteral(JSThread *thread, JSTaggedValue pattern, uint8_t flags)
{
    INTERPRETER_TRACE(thread, CreateRegExpWithLiteral);
    [[maybe_unused]] EcmaHandleScope handleScope(thread);

    JSHandle<JSTaggedValue> patternHandle(thread, pattern);
    return RuntimeStubs::RuntimeCreateRegExpWithLiteral(thread, patternHandle, flags);
}

JSTaggedValue SlowRuntimeStub::CreateArrayWithBuffer(JSThread *thread, ObjectFactory *factory, JSArray *literal)
{
    INTERPRETER_TRACE(thread, CreateArrayWithBuffer);
    [[maybe_unused]] EcmaHandleScope handleScope(thread);

    JSHandle<JSTaggedValue> array(thread, literal);
    return RuntimeStubs::RuntimeCreateArrayWithBuffer(thread, factory, array);
}

JSTaggedValue SlowRuntimeStub::GetTemplateObject(JSThread *thread, JSTaggedValue literal)
{
    INTERPRETER_TRACE(thread, GetTemplateObject);
    [[maybe_unused]] EcmaHandleScope handleScope(thread);

    JSHandle<JSTaggedValue> templateLiteral(thread, literal);
    return RuntimeStubs::RuntimeGetTemplateObject(thread, templateLiteral);
}

JSTaggedValue SlowRuntimeStub::GetNextPropName(JSThread *thread, JSTaggedValue iter)
{
    INTERPRETER_TRACE(thread, GetNextPropName);
    [[maybe_unused]] EcmaHandleScope handleScope(thread);

    JSHandle<JSTaggedValue> iterator(thread, iter);
    return RuntimeStubs::RuntimeGetNextPropName(thread, iterator);
}

JSTaggedValue SlowRuntimeStub::CopyDataProperties(JSThread *thread, JSTaggedValue dst, JSTaggedValue src)
{
    INTERPRETER_TRACE(thread, CopyDataProperties);
    [[maybe_unused]] EcmaHandleScope handleScope(thread);

    JSHandle<JSTaggedValue> dstHandle(thread, dst);
    JSHandle<JSTaggedValue> srcHandle(thread, src);
    return RuntimeStubs::RuntimeCopyDataProperties(thread, dstHandle, srcHandle);
}

JSTaggedValue SlowRuntimeStub::GetIteratorNext(JSThread *thread, JSTaggedValue obj, JSTaggedValue method)
{
    INTERPRETER_TRACE(thread, GetIteratorNext);
    [[maybe_unused]] EcmaHandleScope handleScope(thread);

    JSHandle<JSTaggedValue> iter(thread, obj);
    JSHandle<JSTaggedValue> next(thread, method);
    return RuntimeStubs::RuntimeGetIteratorNext(thread, iter, next);
}

JSTaggedValue SlowRuntimeStub::GetUnmapedArgs(JSThread *thread, JSTaggedType *sp, uint32_t actualNumArgs,
                                              uint32_t startIdx)
{
    INTERPRETER_TRACE(thread, GetUnmapedArgs);
    [[maybe_unused]] EcmaHandleScope handleScope(thread);
    return RuntimeStubs::RuntimeGetUnmapedArgs(thread, sp, actualNumArgs, startIdx);
}

JSTaggedValue SlowRuntimeStub::CopyRestArgs(JSThread *thread, JSTaggedType *sp, uint32_t restNumArgs, uint32_t startIdx)
{
    INTERPRETER_TRACE(thread, Copyrestargs);
    [[maybe_unused]] EcmaHandleScope handleScope(thread);
    return RuntimeStubs::RuntimeCopyRestArgs(thread, sp, restNumArgs, startIdx);
}

JSTaggedValue SlowRuntimeStub::GetIterator(JSThread *thread, JSTaggedValue obj)
{
    INTERPRETER_TRACE(thread, GetIterator);
    [[maybe_unused]] EcmaHandleScope handleScope(thread);

    JSHandle<JSTaggedValue> objHandle(thread, obj);
    return RuntimeStubs::RuntimeGetIterator(thread, objHandle);
}

JSTaggedValue SlowRuntimeStub::DefineGetterSetterByValue(JSThread *thread, JSTaggedValue obj, JSTaggedValue prop,
                                                         JSTaggedValue getter, JSTaggedValue setter, bool flag)
{
    INTERPRETER_TRACE(thread, DefineGetterSetterByValue);
    [[maybe_unused]] EcmaHandleScope handleScope(thread);

    JSHandle<JSObject> objHandle(thread, obj);
    JSHandle<JSTaggedValue> propHandle(thread, prop);
    JSHandle<JSTaggedValue> getterHandle(thread, getter);
    JSHandle<JSTaggedValue> setterHandle(thread, setter);
    return RuntimeStubs::RuntimeDefineGetterSetterByValue(thread, objHandle, propHandle,
                                                          getterHandle, setterHandle, flag);
}

JSTaggedValue SlowRuntimeStub::LdObjByIndex(JSThread *thread, JSTaggedValue obj, uint32_t idx, bool callGetter,
                                            JSTaggedValue receiver)
{
    INTERPRETER_TRACE(thread, LdObjByIndexDyn);
    [[maybe_unused]] EcmaHandleScope handleScope(thread);

    JSHandle<JSTaggedValue> objHandle(thread, obj);
    return RuntimeStubs::RuntimeLdObjByIndex(thread, objHandle, idx, callGetter, receiver);
}

JSTaggedValue SlowRuntimeStub::StObjByIndex(JSThread *thread, JSTaggedValue obj, uint32_t idx, JSTaggedValue value)
{
    INTERPRETER_TRACE(thread, StObjByIndexDyn);
    [[maybe_unused]] EcmaHandleScope handleScope(thread);

    JSHandle<JSTaggedValue> objHandle(thread, obj);
    JSHandle<JSTaggedValue> valueHandle(thread, value);
    return RuntimeStubs::RuntimeStObjByIndex(thread, objHandle, idx, valueHandle);
}

JSTaggedValue SlowRuntimeStub::LdObjByName(JSThread *thread, JSTaggedValue obj, JSTaggedValue prop, bool callGetter,
                                           JSTaggedValue receiver)
{
    INTERPRETER_TRACE(thread, LdObjByNameDyn);
    [[maybe_unused]] EcmaHandleScope handleScope(thread);
    return RuntimeStubs::RuntimeLdObjByName(thread, obj, prop, callGetter, receiver);
}

JSTaggedValue SlowRuntimeStub::StObjByName(JSThread *thread, JSTaggedValue obj, JSTaggedValue prop, JSTaggedValue value)
{
    INTERPRETER_TRACE(thread, StObjByNameDyn);
    [[maybe_unused]] EcmaHandleScope handleScope(thread);

    JSHandle<JSTaggedValue> objHandle(thread, obj);
    JSHandle<JSTaggedValue> propHandle(thread, prop);
    JSHandle<JSTaggedValue> valueHandle(thread, value);
    return RuntimeStubs::RuntimeStObjByName(thread, objHandle, propHandle, valueHandle);
}

JSTaggedValue SlowRuntimeStub::LdObjByValue(JSThread *thread, JSTaggedValue obj, JSTaggedValue prop, bool callGetter,
                                            JSTaggedValue receiver)
{
    INTERPRETER_TRACE(thread, LdObjByValueDyn);
    [[maybe_unused]] EcmaHandleScope handleScope(thread);

    JSHandle<JSTaggedValue> objHandle(thread, obj);
    JSHandle<JSTaggedValue> propHandle(thread, prop);
    return RuntimeStubs::RuntimeLdObjByValue(thread, objHandle, propHandle, callGetter, receiver);
}

JSTaggedValue SlowRuntimeStub::StObjByValue(JSThread *thread, JSTaggedValue obj, JSTaggedValue prop,
                                            JSTaggedValue value)
{
    INTERPRETER_TRACE(thread, StObjByValueDyn);
    [[maybe_unused]] EcmaHandleScope handleScope(thread);

    JSHandle<JSTaggedValue> objHandle(thread, obj);
    JSHandle<JSTaggedValue> propHandle(thread, prop);
    JSHandle<JSTaggedValue> valueHandle(thread, value);
    return RuntimeStubs::RuntimeStObjByValue(thread, objHandle, propHandle, valueHandle);
}

JSTaggedValue SlowRuntimeStub::TryLdGlobalByNameFromGlobalProto(JSThread *thread, JSTaggedValue global,
                                                                JSTaggedValue prop)
{
    INTERPRETER_TRACE(thread, Trygetobjprop);
    [[maybe_unused]] EcmaHandleScope handleScope(thread);

    JSHandle<JSTaggedValue> obj(thread, global.GetTaggedObject()->GetClass()->GetPrototype());
    JSHandle<JSTaggedValue> propHandle(thread, prop);
    return RuntimeStubs::RuntimeTryLdGlobalByName(thread, obj, propHandle);
}

JSTaggedValue SlowRuntimeStub::TryStGlobalByName(JSThread *thread, JSTaggedValue prop)
{
    INTERPRETER_TRACE(thread, TryStGlobalByName);
    // If fast path is fail, not need slow path, just throw error.
    return ThrowReferenceError(thread, prop, " is not defined");
}

JSTaggedValue SlowRuntimeStub::LdGlobalVarFromGlobalProto(JSThread *thread, JSTaggedValue global, JSTaggedValue prop)
{
    INTERPRETER_TRACE(thread, LdGlobalVar);
    [[maybe_unused]] EcmaHandleScope handleScope(thread);

    JSHandle<JSTaggedValue> objHandle(thread, global);
    JSHandle<JSTaggedValue> propHandle(thread, prop);
    return RuntimeStubs::RuntimeLdGlobalVarFromProto(thread, objHandle, propHandle); // After checked global itself.
}

JSTaggedValue SlowRuntimeStub::StGlobalVar(JSThread *thread, JSTaggedValue prop, JSTaggedValue value)
{
    INTERPRETER_TRACE(thread, StGlobalVar);
    [[maybe_unused]] EcmaHandleScope handleScope(thread);

    JSHandle<JSTaggedValue> propHandle(thread, prop);
    JSHandle<JSTaggedValue> valueHandle(thread, value);
    return RuntimeStubs::RuntimeStGlobalVar(thread, propHandle, valueHandle);
}

JSTaggedValue SlowRuntimeStub::TryUpdateGlobalRecord(JSThread *thread, JSTaggedValue prop, JSTaggedValue value)
{
    INTERPRETER_TRACE(thread, TryUpdateGlobalRecord);
    [[maybe_unused]] EcmaHandleScope handleScope(thread);

    return RuntimeStubs::RuntimeTryUpdateGlobalRecord(thread, prop, value);
}

// return box
JSTaggedValue SlowRuntimeStub::LdGlobalRecord(JSThread *thread, JSTaggedValue key)
{
    INTERPRETER_TRACE(thread, LdGlobalRecord);
    [[maybe_unused]] EcmaHandleScope handleScope(thread);

    return RuntimeStubs::RuntimeLdGlobalRecord(thread, key);
}

JSTaggedValue SlowRuntimeStub::StGlobalRecord(JSThread *thread, JSTaggedValue prop, JSTaggedValue value, bool isConst)
{
    INTERPRETER_TRACE(thread, StGlobalRecord);
    [[maybe_unused]] EcmaHandleScope handleScope(thread);

    JSHandle<JSTaggedValue> propHandle(thread, prop);
    JSHandle<JSTaggedValue> valueHandle(thread, value);
    return RuntimeStubs::RuntimeStGlobalRecord(thread, propHandle, valueHandle, isConst);
}

JSTaggedValue SlowRuntimeStub::ThrowReferenceError(JSThread *thread, JSTaggedValue prop, const char *desc)
{
    INTERPRETER_TRACE(thread, ThrowReferenceError);
    [[maybe_unused]] EcmaHandleScope handleScope(thread);

    JSHandle<JSTaggedValue> propHandle(thread, prop);
    return RuntimeStubs::RuntimeThrowReferenceError(thread, propHandle, desc);
}

JSTaggedValue SlowRuntimeStub::ThrowTypeError(JSThread *thread, const char *message)
{
    INTERPRETER_TRACE(thread, ThrowTypeError);
    [[maybe_unused]] EcmaHandleScope handleScope(thread);

    return RuntimeStubs::RuntimeThrowTypeError(thread, message);
}

JSTaggedValue SlowRuntimeStub::ThrowSyntaxError(JSThread *thread, const char *message)
{
    INTERPRETER_TRACE(thread, ThrowSyntaxError);
    [[maybe_unused]] EcmaHandleScope handleScope(thread);

    return RuntimeStubs::RuntimeThrowSyntaxError(thread, message);
}

JSTaggedValue SlowRuntimeStub::StArraySpread(JSThread *thread, JSTaggedValue dst, JSTaggedValue index,
                                             JSTaggedValue src)
{
    INTERPRETER_TRACE(thread, StArraySpread);
    [[maybe_unused]] EcmaHandleScope handleScope(thread);

    JSHandle<JSTaggedValue> dstHandle(thread, dst);
    JSHandle<JSTaggedValue> srcHandle(thread, src);
    return RuntimeStubs::RuntimeStArraySpread(thread, dstHandle, index, srcHandle);
}

JSTaggedValue SlowRuntimeStub::DefineGeneratorFunc(JSThread *thread, JSFunction *func)
{
    INTERPRETER_TRACE(thread, DefineGeneratorFunc);
    [[maybe_unused]] EcmaHandleScope handleScope(thread);
    JSHandle<JSFunction> funcHandle(thread, func);

    return RuntimeStubs::RuntimeDefineGeneratorFunc(thread, funcHandle);
}

JSTaggedValue SlowRuntimeStub::DefineAsyncFunc(JSThread *thread, JSFunction *func)
{
    INTERPRETER_TRACE(thread, DefineAsyncFunc);
    [[maybe_unused]] EcmaHandleScope handleScope(thread);
    JSHandle<JSFunction> funcHandle(thread, func);
    return RuntimeStubs::RuntimeDefineAsyncFunc(thread, funcHandle);
}

JSTaggedValue SlowRuntimeStub::DefineNCFuncDyn(JSThread *thread, JSFunction *func)
{
    INTERPRETER_TRACE(thread, DefineNCFuncDyn);
    [[maybe_unused]] EcmaHandleScope handleScope(thread);
    JSHandle<JSFunction> funcHandle(thread, func);
    return RuntimeStubs::RuntimeDefineNCFuncDyn(thread, funcHandle);
}

JSTaggedValue SlowRuntimeStub::DefinefuncDyn(JSThread *thread, JSFunction *func)
{
    INTERPRETER_TRACE(thread, DefinefuncDyn);
    [[maybe_unused]] EcmaHandleScope handleScope(thread);
    JSHandle<JSFunction> funcHandle(thread, func);
    return RuntimeStubs::RuntimeDefinefuncDyn(thread, funcHandle);
}

JSTaggedValue SlowRuntimeStub::GetSuperConstructor(JSThread *thread, JSTaggedValue ctor)
{
    INTERPRETER_TRACE(thread, GetSuperConstructor);
    JSHandle<JSTaggedValue> ctorHandle(thread, ctor);
    JSHandle<JSTaggedValue> superConstructor(thread, JSTaggedValue::GetPrototype(thread, ctorHandle));
    RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
    return superConstructor.GetTaggedValue();
}

JSTaggedValue SlowRuntimeStub::SuperCall(JSThread *thread, JSTaggedValue func, JSTaggedValue newTarget,
                                         uint16_t firstVRegIdx, uint16_t length)
{
    INTERPRETER_TRACE(thread, SuperCall);
    [[maybe_unused]] EcmaHandleScope handleScope(thread);
    FrameHandler frameHandler(thread);

    JSHandle<JSTaggedValue> funcHandle(thread, func);
    JSHandle<JSTaggedValue> newTargetHandle(thread, newTarget);
    return RuntimeStubs::RuntimeSuperCall(thread, funcHandle, newTargetHandle, firstVRegIdx, length);
}

JSTaggedValue SlowRuntimeStub::SuperCallSpread(JSThread *thread, JSTaggedValue func, JSTaggedValue newTarget,
                                               JSTaggedValue array)
{
    INTERPRETER_TRACE(thread, SuperCallSpread);
    [[maybe_unused]] EcmaHandleScope handleScope(thread);

    JSHandle<JSTaggedValue> funcHandle(thread, func);
    JSHandle<JSTaggedValue> newTargetHandle(thread, newTarget);
    JSHandle<JSTaggedValue> jsArray(thread, array);
    return RuntimeStubs::RuntimeSuperCallSpread(thread, funcHandle, newTargetHandle, jsArray);
}

JSTaggedValue SlowRuntimeStub::DefineMethod(JSThread *thread, JSFunction *func, JSTaggedValue homeObject)
{
    INTERPRETER_TRACE(thread, DefineMethod);
    [[maybe_unused]] EcmaHandleScope handleScope(thread);
    JSHandle<JSFunction> funcHandle(thread, func);
    JSHandle<JSTaggedValue> homeObjectHandle(thread, homeObject);
    return RuntimeStubs::RuntimeDefineMethod(thread, funcHandle, homeObjectHandle);
}

JSTaggedValue SlowRuntimeStub::LdSuperByValue(JSThread *thread, JSTaggedValue obj, JSTaggedValue key,
                                              JSTaggedValue thisFunc)
{
    INTERPRETER_TRACE(thread, LdSuperByValue);
    [[maybe_unused]] EcmaHandleScope handleScope(thread);

    JSHandle<JSTaggedValue> objHandle(thread, obj);
    JSHandle<JSTaggedValue> propHandle(thread, key);
    return RuntimeStubs::RuntimeLdSuperByValue(thread, objHandle, propHandle, thisFunc);
}

JSTaggedValue SlowRuntimeStub::StSuperByValue(JSThread *thread, JSTaggedValue obj, JSTaggedValue key,
                                              JSTaggedValue value, JSTaggedValue thisFunc)
{
    INTERPRETER_TRACE(thread, StSuperByValue);
    [[maybe_unused]] EcmaHandleScope handleScope(thread);

    JSHandle<JSTaggedValue> objHandle(thread, obj);
    JSHandle<JSTaggedValue> propHandle(thread, key);
    JSHandle<JSTaggedValue> valueHandle(thread, value);
    return RuntimeStubs::RuntimeStSuperByValue(thread, objHandle, propHandle, valueHandle, thisFunc);
}

JSTaggedValue SlowRuntimeStub::GetCallSpreadArgs(JSThread *thread, JSTaggedValue array)
{
    INTERPRETER_TRACE(thread, GetCallSpreadArgs);
    [[maybe_unused]] EcmaHandleScope handleScope(thread);

    JSHandle<JSTaggedValue> jsArray(thread, array);
    return RuntimeStubs::RuntimeGetCallSpreadArgs(thread, jsArray);
}

void SlowRuntimeStub::ThrowDeleteSuperProperty(JSThread *thread)
{
    INTERPRETER_TRACE(thread, ThrowDeleteSuperProperty);
    [[maybe_unused]] EcmaHandleScope handleScope(thread);

    return RuntimeStubs::RuntimeThrowDeleteSuperProperty(thread);
}

JSTaggedValue SlowRuntimeStub::NotifyInlineCache(JSThread *thread, JSFunction *func, JSMethod *method)
{
    INTERPRETER_TRACE(thread, NotifyInlineCache);
    [[maybe_unused]] EcmaHandleScope handleScope(thread);

    JSHandle<JSFunction> funcHandle(thread, func);
    return RuntimeStubs::RuntimeNotifyInlineCache(thread, funcHandle, method);
}

JSTaggedValue SlowRuntimeStub::ResolveClass(JSThread *thread, JSTaggedValue ctor, TaggedArray *literal,
                                            JSTaggedValue base, JSTaggedValue lexenv, ConstantPool *constpool)
{
    JSHandle<JSFunction> cls(thread, ctor);
    JSHandle<TaggedArray> literalBuffer(thread, literal);
    JSHandle<JSTaggedValue> baseHandle(thread, base);
    JSHandle<JSTaggedValue> lexicalEnv(thread, lexenv);
    JSHandle<ConstantPool> constpoolHandle(thread, constpool);
    return RuntimeStubs::RuntimeResolveClass(thread, cls, literalBuffer, baseHandle, lexicalEnv, constpoolHandle);
}

// clone class may need re-set inheritance relationship due to extends may be a variable.
JSTaggedValue SlowRuntimeStub::CloneClassFromTemplate(JSThread *thread, JSTaggedValue ctor, JSTaggedValue base,
                                                      JSTaggedValue lexenv, ConstantPool *constpool)
{
    [[maybe_unused]] EcmaHandleScope handleScope(thread);

    JSHandle<JSTaggedValue> lexenvHandle(thread, lexenv);
    JSHandle<JSTaggedValue> constpoolHandle(thread, JSTaggedValue(constpool));
    JSHandle<JSTaggedValue> baseHandle(thread, base);
    JSHandle<JSFunction> cls(thread, ctor);
    return RuntimeStubs::RuntimeCloneClassFromTemplate(thread, cls, baseHandle, lexenvHandle, constpoolHandle);
}

JSTaggedValue SlowRuntimeStub::SetClassInheritanceRelationship(JSThread *thread, JSTaggedValue ctor, JSTaggedValue base)
{
    [[maybe_unused]] EcmaHandleScope handleScope(thread);

    JSHandle<JSTaggedValue> cls(thread, ctor);
    JSHandle<JSTaggedValue> parent(thread, base);
    return RuntimeStubs::RuntimeSetClassInheritanceRelationship(thread, cls, parent);
}

JSTaggedValue SlowRuntimeStub::SetClassConstructorLength(JSThread *thread, JSTaggedValue ctor, JSTaggedValue length)
{
    return RuntimeStubs::RuntimeSetClassConstructorLength(thread, ctor, length);
}

JSTaggedValue SlowRuntimeStub::GetModuleNamespace(JSThread *thread, JSTaggedValue localName)
{
    return RuntimeStubs::RuntimeGetModuleNamespace(thread, localName);
}

JSTaggedValue SlowRuntimeStub::LdBigInt(JSThread *thread, JSTaggedValue numberBigInt)
{
    INTERPRETER_TRACE(thread, LdBigInt);
    [[maybe_unused]] EcmaHandleScope handleScope(thread);
    JSHandle<JSTaggedValue> bigint(thread, numberBigInt);
    return RuntimeStubs::RuntimeLdBigInt(thread, bigint);
}
JSTaggedValue SlowRuntimeStub::AsyncGeneratorResolve(JSThread *thread, JSTaggedValue asyncFuncObj,
                                                     const JSTaggedValue value, JSTaggedValue flag)
{
    INTERPRETER_TRACE(thread, AsyncGeneratorResolve);
    [[maybe_unused]] EcmaHandleScope handleScope(thread);

    JSHandle<JSAsyncGeneratorObject> asyncFuncObjHandle(thread, asyncFuncObj);
    JSHandle<JSTaggedValue> valueHandle(thread, value);
    ASSERT(flag.IsBoolean());
    bool done = flag.IsTrue();
    return JSAsyncGeneratorObject::AsyncGeneratorResolve(thread, asyncFuncObjHandle, valueHandle, done);
}

JSTaggedValue SlowRuntimeStub::DefineAsyncGeneratorFunc(JSThread *thread, JSFunction *func)
{
    INTERPRETER_TRACE(thread, DefineAsyncGeneratorFunc);
    [[maybe_unused]] EcmaHandleScope handleScope(thread);
    auto method = func->GetCallTarget();

    JSHandle<GlobalEnv> env = thread->GetEcmaVM()->GetGlobalEnv();
    ObjectFactory *factory = thread->GetEcmaVM()->GetFactory();
    JSHandle<JSFunction> jsFunc = factory->NewJSAsyncGeneratorFunction(method);
    ASSERT_NO_ABRUPT_COMPLETION(thread);
    // 26.3.4.3 prototype
    // Whenever a GeneratorFunction instance is created another ordinary object is also created and
    // is the initial value of the generator function's "prototype" property.
    JSHandle<JSFunction> objFun(env->GetObjectFunction());
    JSHandle<JSObject> initialAsyncGeneratorFuncPrototype = factory->NewJSObjectByConstructor(objFun);
    JSObject::SetPrototype(thread, initialAsyncGeneratorFuncPrototype, env->GetAsyncGeneratorPrototype());
    ASSERT_NO_ABRUPT_COMPLETION(thread);
    jsFunc->SetProtoOrDynClass(thread, initialAsyncGeneratorFuncPrototype);

    return jsFunc.GetTaggedValue();
}
}  // namespace panda::ecmascript
