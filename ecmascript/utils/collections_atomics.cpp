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

#include "ecmascript/utils/collections_atomics.h"

#include "ecmascript/base/typed_array_helper-inl.h"
#include "ecmascript/checkpoint/thread_state_transition.h"
#include "ecmascript/containers/containers_errors.h"
#include "ecmascript/mem/barriers-inl.h"
#include "libpandabase/utils/time.h"

namespace panda::ecmascript::collections {
using AtomicHelper = base::AtomicHelper;
using NumberHelper = base::NumberHelper;
using BuiltinsBase = base::BuiltinsBase;
using BuiltinsArrayBuffer = builtins::BuiltinsArrayBuffer;
using BuiltinsSendableArrayBuffer = builtins::BuiltinsSendableArrayBuffer;
using ContainerError = containers::ContainerError;
using WaitResult = builtins::WaitResult;

namespace {
WaiterList *g_waitLists = Singleton<WaiterList>::GetInstance();
Mutex *g_mutex = Singleton<Mutex>::GetInstance();

void ValidateAtomicBuffer(JSThread *thread, const JSHandle<JSTaggedValue> &array)
{
    JSHandle<JSTaggedValue> backing(thread,
        JSHandle<JSTypedArray>::Cast(array)->GetViewedArrayBufferOrByteArray(thread));
    if (backing->IsSendableArrayBuffer() &&
        BuiltinsSendableArrayBuffer::IsDetachedBuffer(thread, backing.GetTaggedValue())) {
        auto error = ContainerError::ParamError(thread, "Parameter error. The SendableArrayBuffer is detached.");
        THROW_NEW_ERROR_AND_RETURN(thread, error);
    }
}
}  // namespace

JSTaggedValue CollectionsAtomics::Add(EcmaRuntimeCallInfo *argv)
{
    ASSERT(argv);
    JSThread *thread = argv->GetThread();
    BUILTINS_API_TRACE(thread, Atomics, Add);
    [[maybe_unused]] EcmaHandleScope handleScope(thread);
    JSHandle<JSTaggedValue> typedArray = GetCallArg(argv, 0);
    JSHandle<JSTaggedValue> index = GetCallArg(argv, 1);
    return AtomicReadModifyWrite(thread, typedArray, index, argv, AtomicHelper::AddFun());
}

JSTaggedValue CollectionsAtomics::Sub(EcmaRuntimeCallInfo *argv)
{
    ASSERT(argv);
    JSThread *thread = argv->GetThread();
    BUILTINS_API_TRACE(thread, Atomics, Sub);
    [[maybe_unused]] EcmaHandleScope handleScope(thread);
    JSHandle<JSTaggedValue> typedArray = GetCallArg(argv, 0);
    JSHandle<JSTaggedValue> index = GetCallArg(argv, 1);
    return AtomicReadModifyWrite(thread, typedArray, index, argv, AtomicHelper::SubFun());
}

JSTaggedValue CollectionsAtomics::And(EcmaRuntimeCallInfo *argv)
{
    ASSERT(argv);
    JSThread *thread = argv->GetThread();
    BUILTINS_API_TRACE(thread, Atomics, And);
    [[maybe_unused]] EcmaHandleScope handleScope(thread);
    JSHandle<JSTaggedValue> typedArray = GetCallArg(argv, 0);
    JSHandle<JSTaggedValue> index = GetCallArg(argv, 1);
    return AtomicReadModifyWrite(thread, typedArray, index, argv, AtomicHelper::AndFun());
}

JSTaggedValue CollectionsAtomics::Or(EcmaRuntimeCallInfo *argv)
{
    ASSERT(argv);
    JSThread *thread = argv->GetThread();
    BUILTINS_API_TRACE(thread, Atomics, Or);
    [[maybe_unused]] EcmaHandleScope handleScope(thread);
    JSHandle<JSTaggedValue> typedArray = GetCallArg(argv, 0);
    JSHandle<JSTaggedValue> index = GetCallArg(argv, 1);
    return AtomicReadModifyWrite(thread, typedArray, index, argv, AtomicHelper::OrFun());
}

JSTaggedValue CollectionsAtomics::Xor(EcmaRuntimeCallInfo *argv)
{
    ASSERT(argv);
    JSThread *thread = argv->GetThread();
    BUILTINS_API_TRACE(thread, Atomics, Xor);
    [[maybe_unused]] EcmaHandleScope handleScope(thread);
    JSHandle<JSTaggedValue> typedArray = GetCallArg(argv, 0);
    JSHandle<JSTaggedValue> index = GetCallArg(argv, 1);
    return AtomicReadModifyWrite(thread, typedArray, index, argv, AtomicHelper::XorFun());
}

JSTaggedValue CollectionsAtomics::Exchange(EcmaRuntimeCallInfo *argv)
{
    ASSERT(argv);
    JSThread *thread = argv->GetThread();
    BUILTINS_API_TRACE(thread, Atomics, Exchange);
    [[maybe_unused]] EcmaHandleScope handleScope(thread);
    JSHandle<JSTaggedValue> typedArray = GetCallArg(argv, 0);
    JSHandle<JSTaggedValue> index = GetCallArg(argv, 1);
    return AtomicReadModifyWrite(thread, typedArray, index, argv, AtomicHelper::ExchangeFun());
}

JSTaggedValue CollectionsAtomics::CompareExchange(EcmaRuntimeCallInfo *argv)
{
    ASSERT(argv);
    JSThread *thread = argv->GetThread();
    BUILTINS_API_TRACE(thread, Atomics, CompareExchange);
    [[maybe_unused]] EcmaHandleScope handleScope(thread);
    JSHandle<JSTaggedValue> typedArray = GetCallArg(argv, 0);
    JSHandle<JSTaggedValue> index = GetCallArg(argv, 1);
    return AtomicReadModifyWrite(thread, typedArray, index, argv, AtomicHelper::CompareExchangeFun());
}

JSTaggedValue CollectionsAtomics::Load(EcmaRuntimeCallInfo *argv)
{
    ASSERT(argv);
    JSThread *thread = argv->GetThread();
    BUILTINS_API_TRACE(thread, Atomics, Load);
    [[maybe_unused]] EcmaHandleScope handleScope(thread);
    if (!CollectionsAtomicHelper::ValidateArguments(argv, LOAD_LENGTH, LOAD_LENGTH)) {
        return JSTaggedValue::Exception();
    }
    JSHandle<JSTaggedValue> typedArray = GetCallArg(argv, 0);
    JSHandle<JSTaggedValue> index = GetCallArg(argv, 1);
    return CollectionsAtomicHelper::AtomicLoad(thread, typedArray, index);
}

JSTaggedValue CollectionsAtomics::Store(EcmaRuntimeCallInfo *argv)
{
    ASSERT(argv);
    JSThread *thread = argv->GetThread();
    BUILTINS_API_TRACE(thread, Atomics, Store);
    [[maybe_unused]] EcmaHandleScope handleScope(thread);
    if (!CollectionsAtomicHelper::ValidateArguments(argv, UPDATE_LENGTH, UPDATE_LENGTH)) {
        return JSTaggedValue::Exception();
    }
    JSHandle<JSTaggedValue> typedArray = GetCallArg(argv, 0);
    JSHandle<JSTaggedValue> index = GetCallArg(argv, 1);
    JSHandle<JSTaggedValue> value = GetCallArg(argv, BuiltinsBase::ArgsPosition::THIRD);
    return CollectionsAtomicHelper::AtomicStore(thread, typedArray, index, value);
}

template<typename T, typename Op>
JSTaggedValue CollectionsAtomics::ModifyInteger(JSThread *thread, const JSHandle<JSTaggedValue> &array,
    uint32_t offset, EcmaRuntimeCallInfo *argv, const Op &op)
{
    constexpr uint32_t operandCount = 2;
    T operands[operandCount] = {};
    auto value = GetCallArg(argv, BuiltinsBase::ArgsPosition::THIRD);
    operands[0] = static_cast<T>(JSTaggedValue::ToInt32(thread, value));
    RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
    ValidateAtomicBuffer(thread, array);
    RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
    if constexpr (std::is_same_v<Op, AtomicHelper::CompareExchangeFun>) {
        auto replacement = GetCallArg(argv, BuiltinsBase::ArgsPosition::FOURTH);
        operands[1] = static_cast<T>(JSTaggedValue::ToInt32(thread, replacement));
        RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
    }
    ValidateAtomicBuffer(thread, array);
    RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
    // All conversions have completed. Resolve the backing after their possible GC or buffer promotion.
    JSHandle<JSTaggedValue> backing(thread,
        JSHandle<JSTypedArray>::Cast(array)->GetViewedArrayBufferOrByteArray(thread));
    auto *data = static_cast<uint8_t *>(
        BuiltinsArrayBuffer::GetDataPointFromBuffer(thread, backing.GetTaggedValue()));
    T previous = op(reinterpret_cast<T *>(data + offset), operands, operandCount);
    if constexpr (std::is_same_v<T, uint32_t>) {
        return JSTaggedValue(previous);
    }
    return BuiltinsBase::GetTaggedInt(previous);
}

template<typename Op>
JSTaggedValue CollectionsAtomics::AtomicReadModifyWrite(JSThread *thread, const JSHandle<JSTaggedValue> &array,
    JSHandle<JSTaggedValue> &index, EcmaRuntimeCallInfo *argv, const Op &op)
{
    constexpr uint32_t requiredCount = std::is_same_v<Op, AtomicHelper::CompareExchangeFun> ?
        COMPARE_EXCHANGE_LENGTH : UPDATE_LENGTH;
    if (!CollectionsAtomicHelper::ValidateArguments(argv, requiredCount, requiredCount)) {
        return JSTaggedValue::Exception();
    }
    CollectionsAtomicHelper::AtomicTypedArrayWitness witness {};
    if (!CollectionsAtomicHelper::ValidateIntegerTypedArray(thread, array, witness)) {
        return JSTaggedValue::Exception();
    }
    uint32_t offset = CollectionsAtomicHelper::ValidateAtomicAccess(thread, witness, index);
    RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
    ValidateAtomicBuffer(thread, array);
    RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
    switch (witness.type) {
        case DataViewType::INT16:
            return ModifyInteger<int16_t>(thread, array, offset, argv, op);
        case DataViewType::INT32:
            return ModifyInteger<int32_t>(thread, array, offset, argv, op);
        case DataViewType::INT8:
            return ModifyInteger<int8_t>(thread, array, offset, argv, op);
        case DataViewType::UINT16:
            return ModifyInteger<uint16_t>(thread, array, offset, argv, op);
        case DataViewType::UINT32:
            return ModifyInteger<uint32_t>(thread, array, offset, argv, op);
        case DataViewType::UINT8:
            return ModifyInteger<uint8_t>(thread, array, offset, argv, op);
        default:
            UNREACHABLE();
    }
}

double CollectionsAtomics::ConvertTimeout(JSThread *thread, const JSHandle<JSTaggedValue> &timeout)
{
    if (timeout->IsUndefined()) {
        return base::POSITIVE_INFINITY;
    }
    JSTaggedNumber number = JSTaggedValue::ToNumber(thread, timeout);
    RETURN_VALUE_IF_ABRUPT_COMPLETION(thread, 0);
    double value = number.GetNumber();
    if (NumberHelper::IsNaN(number) || (!NumberHelper::IsFinite(number) && value > 0)) {
        return base::POSITIVE_INFINITY;
    }
    return value < 0 ? 0 : value;
}

JSTaggedValue CollectionsAtomics::Wait(EcmaRuntimeCallInfo *argv)
{
    ASSERT(argv);
    JSThread *thread = argv->GetThread();
    EcmaHandleScope scope(thread);
    if (!CollectionsAtomicHelper::ValidateArguments(argv, UPDATE_LENGTH, WAIT_LENGTH, true)) {
        return JSTaggedValue::Exception();
    }
    auto array = GetCallArg(argv, 0);
    auto index = GetCallArg(argv, 1);
    auto value = GetCallArg(argv, BuiltinsBase::ArgsPosition::THIRD);
    auto timeout = GetCallArg(argv, BuiltinsBase::ArgsPosition::FOURTH);
    CollectionsAtomicHelper::AtomicTypedArrayWitness witness {};
    if (!CollectionsAtomicHelper::ValidateIntegerTypedArray(thread, array, witness, true)) {
        return JSTaggedValue::Exception();
    }
    uint32_t offset = CollectionsAtomicHelper::ValidateAtomicAccess(thread, witness, index);
    RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
    int32_t expected = JSTaggedValue::ToInt32(thread, value);
    RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
    double duration = ConvertTimeout(thread, timeout);
    RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
    if (!thread->GetEcmaVM()->GetAgentCanSuspend()) {
        auto error = ContainerError::ParamError(thread, "Parameter error. vm does not allow wait to block.");
        THROW_NEW_ERROR_AND_RETURN_VALUE(thread, error, JSTaggedValue::Exception());
    }
    ValidateAtomicBuffer(thread, array);
    RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
    WaitResult result = DoWait(thread, array, offset, expected, duration);
    const auto *constants = thread->GlobalConstants();
    if (result == WaitResult::OK) {
        return constants->GetOkString();
    }
    return result == WaitResult::NOT_EQ ? constants->GetNotEqualString() : constants->GetTimeoutString();
}

JSTaggedValue CollectionsAtomics::Notify(EcmaRuntimeCallInfo *argv)
{
    ASSERT(argv);
    JSThread *thread = argv->GetThread();
    EcmaHandleScope scope(thread);
    if (!CollectionsAtomicHelper::ValidateArguments(argv, LOAD_LENGTH, UPDATE_LENGTH, true)) {
        return JSTaggedValue::Exception();
    }
    auto array = GetCallArg(argv, 0);
    auto index = GetCallArg(argv, 1);
    auto count = GetCallArg(argv, BuiltinsBase::ArgsPosition::THIRD);
    CollectionsAtomicHelper::AtomicTypedArrayWitness witness {};
    if (!CollectionsAtomicHelper::ValidateIntegerTypedArray(thread, array, witness, true)) {
        return JSTaggedValue::Exception();
    }
    uint32_t offset = CollectionsAtomicHelper::ValidateAtomicAccess(thread, witness, index);
    RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
    double wakeCount = base::POSITIVE_INFINITY;
    if (!count->IsUndefined()) {
        JSTaggedNumber number = JSTaggedValue::ToNumber(thread, count);
        RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
        wakeCount = NumberHelper::TruncateDouble(number.GetNumber());
        wakeCount = wakeCount < 0 ? 0 : wakeCount;
    }
    ValidateAtomicBuffer(thread, array);
    RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
    return JSTaggedValue(SignalShared(thread, array, offset, wakeCount));
}

JSTaggedValue CollectionsAtomics::IsLockFree(EcmaRuntimeCallInfo *argv)
{
    ASSERT(argv);
    auto *thread = argv->GetThread();
    EcmaHandleScope scope(thread);
    if (!CollectionsAtomicHelper::ValidateArguments(argv, IS_LOCK_FREE_LENGTH, IS_LOCK_FREE_LENGTH, false, false)) {
        return JSTaggedValue::Exception();
    }
    return builtins::BuiltinsAtomics::IsLockFree(argv);
}

WaitResult CollectionsAtomics::DoWait(JSThread *thread, const JSHandle<JSTaggedValue> &array,
    size_t index, int32_t expected, double timeout)
{
    MutexGuard guard(g_mutex);
    JSHandle<JSTaggedValue> backing(thread,
        JSHandle<JSTypedArray>::Cast(array)->GetViewedArrayBufferOrByteArray(thread));
    auto *data = static_cast<uint8_t *>(
        BuiltinsArrayBuffer::GetDataPointFromBuffer(thread, backing.GetTaggedValue()));
    auto *location = reinterpret_cast<int32_t *>(data + index);
    if (CollectionsAtomicHelper::AtomicLoadValue(location) != expected) {
        return WaitResult::NOT_EQ;
    }
    WaiterListNode *node = thread->GetEcmaVM()->GetWaiterListNode();
    node->index_ = index;
    node->arrayHandle_ = array.GetAddress();
    node->date_ = nullptr;
    node->waitPointer_ = nullptr;
    node->waiting_ = true;
    g_waitLists->AddNode(node);
    WaitResult result = WaitOnNode(thread, node, timeout);
    g_waitLists->DeleteNode(node);
    node->waiting_ = false;
    return result;
}

WaitResult CollectionsAtomics::WaitOnNode(JSThread *thread, WaiterListNode *node, double timeout)
{
    uint64_t currentTime = 0;
    uint64_t timeoutTime = 0;
    bool hasTimeout = timeout != base::POSITIVE_INFINITY;
    if (hasTimeout) {
        currentTime = time::GetCurrentTimeInMillis();
        timeoutTime = currentTime + static_cast<uint64_t>(timeout);
    }
    WaitResult res = WaitResult::OK;
    ThreadNativeScope nativeScope(thread);
    while (node->waiting_) {
        if (hasTimeout) {
            currentTime = time::GetCurrentTimeInMillis();
            if (currentTime >= timeoutTime) {
                res = WaitResult::TIME_OUT;
                break;
            }
            uint64_t untilTime = timeoutTime - currentTime;
            ASSERT(untilTime != 0);

            node->cond_.TimedWait(g_mutex, untilTime);
        } else {
            node->cond_.Wait(g_mutex);
        }
    }
    return res;
}

uint32_t CollectionsAtomics::SignalShared(JSThread *thread, const JSHandle<JSTaggedValue> &array,
    size_t index, double wakeCount)
{
    MutexGuard lockGuard(g_mutex);
    JSHandle<JSTaggedValue> backing(thread,
        JSHandle<JSTypedArray>::Cast(array)->GetViewedArrayBufferOrByteArray(thread));
    uint32_t wokenUpCount = 0;
    for (WaiterListNode *node : g_waitLists->sharedWaiters_) {
        if (!(wakeCount > 0)) {
            break;
        }
        if (!node->waiting_ || node->index_ != index) {
            continue;
        }
        EcmaHandleScope iterationScope(thread);
        JSHandle<JSTaggedValue> waitingValue(thread,
            JSTaggedValue(Barriers::GetTaggedValue(thread, node->arrayHandle_)));
        JSHandle<JSTaggedValue> waitingBacking(thread,
            JSHandle<JSTypedArray>::Cast(waitingValue)->GetViewedArrayBufferOrByteArray(thread));
        if (waitingBacking->IsSendableArrayBuffer() &&
            BuiltinsSendableArrayBuffer::IsDetachedBuffer(thread, waitingBacking.GetTaggedValue())) {
            continue;
        }
        // Resolve both addresses while managed; neither is kept across the blocking wait.
        if (BuiltinsArrayBuffer::GetDataPointFromBuffer(thread, waitingBacking.GetTaggedValue()) ==
            BuiltinsArrayBuffer::GetDataPointFromBuffer(thread, backing.GetTaggedValue())) {
            node->waiting_ = false;
            node->cond_.Signal();
            wakeCount--;
            wokenUpCount++;
        }
    }
    return wokenUpCount;
}

}  // namespace panda::ecmascript::collections
