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

#include "ecmascript/utils/collections_atomic_helper.h"

#include <string>

#include "ecmascript/base/typed_array_helper-inl.h"
#include "ecmascript/containers/containers_errors.h"

namespace panda::ecmascript::collections {
using TypedArrayHelper = base::TypedArrayHelper;
using BuiltinsBase = base::BuiltinsBase;
using BuiltinsArrayBuffer = builtins::BuiltinsArrayBuffer;
using BuiltinsSendableArrayBuffer = builtins::BuiltinsSendableArrayBuffer;
using ContainerError = containers::ContainerError;

namespace {
std::string GetActualType(JSThread *thread, const JSHandle<JSTaggedValue> &value)
{
    if (value->IsTypedArray() || value->IsSharedTypedArray()) {
        auto type = JSHandle<JSTypedArray>::Cast(value)->GetContentType();
        auto name = TypedArrayHelper::GetConstructorNameFromType(thread, type);
        return (value->IsSharedTypedArray() ? "collections." : "") +
            EcmaStringAccessor(JSHandle<EcmaString>::Cast(name)).ToStdString(thread);
    }
    if (value->IsUndefined()) {
        return "undefined";
    }
    if (value->IsNull()) {
        return "null";
    }
    if (value->IsNumber()) {
        return "number";
    }
    if (value->IsString()) {
        return "string";
    }
    if (value->IsBoolean()) {
        return "boolean";
    }
    if (value->IsBigInt()) {
        return "bigint";
    }
    if (value->IsSymbol()) {
        return "symbol";
    }
    return value->IsCallable() ? "function" : "object";
}

JSTaggedValue CreateRequiredError(JSThread *thread, uint32_t index)
{
    std::string message = "Parameter error. Param[" + std::to_string(index) +
        "] is required, cannot be null or undefined";
    return ContainerError::ParamError(thread, message.c_str());
}

JSTaggedValue CreateTypeError(JSThread *thread, const JSHandle<JSTaggedValue> &value, uint32_t index,
    const char *expected)
{
    std::string message = "Parameter error. Param[" + std::to_string(index) +
        "] type mismatch. Expected: " + expected + ", Actual: " + GetActualType(thread, value);
    return ContainerError::ParamError(thread, message.c_str());
}

bool ValidateArgumentCount(EcmaRuntimeCallInfo *argv, uint32_t requiredCount, uint32_t maximumCount)
{
    uint32_t actualCount = argv->GetArgsNumber();
    if (actualCount >= requiredCount && actualCount <= maximumCount) {
        return true;
    }
    std::string expected = std::to_string(requiredCount);
    if (requiredCount != maximumCount) {
        expected += "-" + std::to_string(maximumCount);
    }
    std::string message = "Parameter error. Incorrect parameter count. Expected: " + expected +
        ", Actual: " + std::to_string(actualCount);
    auto *thread = argv->GetThread();
    auto error = ContainerError::ParamError(thread, message.c_str());
    THROW_NEW_ERROR_AND_RETURN_VALUE(thread, error, false);
}

bool ValidateNumberArgument(JSThread *thread, const JSHandle<JSTaggedValue> &value, uint32_t index, bool optional)
{
    if (optional && value->IsUndefined()) {
        return true;
    }
    if (!optional && value->IsUndefinedOrNull()) {
        auto error = CreateRequiredError(thread, index);
        THROW_NEW_ERROR_AND_RETURN_VALUE(thread, error, false);
    }
    if (!value->IsNumber()) {
        auto error = CreateTypeError(thread, value, index, "number");
        THROW_NEW_ERROR_AND_RETURN_VALUE(thread, error, false);
    }
    return true;
}

template<typename T>
JSTaggedValue LoadInteger([[maybe_unused]] JSThread *thread, uint8_t *address)
{
    T value = CollectionsAtomicHelper::AtomicLoadValue(reinterpret_cast<T *>(address));
    if constexpr (std::is_same_v<T, uint32_t>) {
        return JSTaggedValue(value);
    }
    return BuiltinsBase::GetTaggedInt(value);
}

void StoreInteger(JSThread *thread, uint8_t *address, DataViewType type,
    const JSHandle<JSTaggedValue> &bufferTag)
{
    switch (type) {
        case DataViewType::INT16:
            CollectionsAtomicHelper::AtomicStoreValue(reinterpret_cast<int16_t *>(address),
                JSTaggedValue::ToInt16(thread, bufferTag));
            break;
        case DataViewType::INT32:
            CollectionsAtomicHelper::AtomicStoreValue(reinterpret_cast<int32_t *>(address),
                JSTaggedValue::ToInt32(thread, bufferTag));
            break;
        case DataViewType::INT8:
            CollectionsAtomicHelper::AtomicStoreValue(reinterpret_cast<int8_t *>(address),
                JSTaggedValue::ToInt8(thread, bufferTag));
            break;
        case DataViewType::UINT16:
            CollectionsAtomicHelper::AtomicStoreValue(reinterpret_cast<uint16_t *>(address),
                static_cast<uint16_t>(JSTaggedValue::ToInt32(thread, bufferTag)));
            break;
        case DataViewType::UINT32:
            CollectionsAtomicHelper::AtomicStoreValue(reinterpret_cast<uint32_t *>(address),
                JSTaggedValue::ToUint32(thread, bufferTag));
            break;
        case DataViewType::UINT8:
            CollectionsAtomicHelper::AtomicStoreValue(reinterpret_cast<uint8_t *>(address),
                JSTaggedValue::ToUint8(thread, bufferTag));
            break;
        default:
            UNREACHABLE();
    }
}
}  // namespace

bool CollectionsAtomicHelper::ValidateArguments(EcmaRuntimeCallInfo *argv, uint32_t requiredCount,
    uint32_t maximumCount, bool waitable, bool hasArray)
{
    if (!ValidateArgumentCount(argv, requiredCount, maximumCount)) {
        return false;
    }
    auto *thread = argv->GetThread();
    if (hasArray) {
        auto array = BuiltinsBase::GetCallArg(argv, 0);
        AtomicTypedArrayWitness witness {};
        if (!ValidateIntegerTypedArray(thread, array, witness, waitable)) {
            return false;
        }
    }
    for (uint32_t index = hasArray ? 1 : 0; index < argv->GetArgsNumber(); index++) {
        auto value = BuiltinsBase::GetCallArg(argv, index);
        if (!ValidateNumberArgument(thread, value, index, index >= requiredCount)) {
            return false;
        }
    }
    return true;
}

JSTaggedValue CollectionsAtomicHelper::ValidateIntegerTypedArray(JSThread *thread, JSHandle<JSTaggedValue> typedArray,
    bool waitable)
{
    AtomicTypedArrayWitness witness {};
    if (!ValidateIntegerTypedArray(thread, typedArray, witness, waitable)) {
        return JSTaggedValue::Exception();
    }
    return witness.buffer;
}

bool CollectionsAtomicHelper::ValidateIntegerTypedArray(JSThread *thread, const JSHandle<JSTaggedValue> &typedArray,
    AtomicTypedArrayWitness &witness, bool waitable)
{
    if (typedArray->IsUndefinedOrNull()) {
        auto error = CreateRequiredError(thread, 0);
        THROW_NEW_ERROR_AND_RETURN_VALUE(thread, error, false);
    }
    if (!typedArray->IsSharedTypedArray()) {
        auto error = CreateTypeError(thread, typedArray, 0, waitable ? "collections.Int32Array" : "AtomicIntegerArray");
        THROW_NEW_ERROR_AND_RETURN_VALUE(thread, error, false);
    }
    // Validate the backing before checking the supported element types.
    JSHandle<JSTaggedValue> buffer(thread,
        JSHandle<JSTypedArray>::Cast(typedArray)->GetViewedArrayBufferOrByteArray(thread));
    if (thread->HasPendingException()) {
        return false;
    }
    if (buffer->IsSendableArrayBuffer() &&
        BuiltinsSendableArrayBuffer::IsDetachedBuffer(thread, buffer.GetTaggedValue())) {
        auto error = ContainerError::ParamError(thread, "Parameter error. The SendableArrayBuffer is detached.");
        THROW_NEW_ERROR_AND_RETURN_VALUE(thread, error, false);
    }

    // Resolve the element type from typedArray.[[TypedArrayName]].
    DataViewType type = JSTypedArray::Cast(typedArray->GetTaggedObject())->GetContentType();

    // Collections supports six unclamped integer types; only Int32 is waitable.
    const bool validType = waitable ? type == DataViewType::INT32 :
        BuiltinsArrayBuffer::IsUnclampedIntegerElementType(type);
    if (!validType) {
        auto error = CreateTypeError(thread, typedArray, 0, waitable ? "collections.Int32Array" : "AtomicIntegerArray");
        THROW_NEW_ERROR_AND_RETURN_VALUE(thread, error, false);
    }
    JSTypedArray *array = JSTypedArray::Cast(typedArray->GetTaggedObject());
    witness = {buffer.GetTaggedValue(), type, array->GetArrayLength(), array->GetByteOffset(), true};
    return true;
}

uint32_t CollectionsAtomicHelper::ValidateAtomicAccess(JSThread *thread, const JSHandle<JSTaggedValue> typedArray,
    JSHandle<JSTaggedValue> requestIndex)
{
    // Assert that typedArray is an Object with a [[ViewedArrayBuffer]] internal slot.
    ASSERT(typedArray->IsSharedTypedArray());
    JSTypedArray *array = JSTypedArray::Cast(typedArray->GetTaggedObject());
    AtomicTypedArrayWitness witness = {array->GetViewedArrayBufferOrByteArray(thread), array->GetContentType(),
        array->GetArrayLength(), array->GetByteOffset(), true};
    return ValidateAtomicAccess(thread, witness, requestIndex);
}

uint32_t CollectionsAtomicHelper::ValidateAtomicAccess(JSThread *thread, const AtomicTypedArrayWitness &witness,
    JSHandle<JSTaggedValue> requestIndex)
{
    // Convert requestIndex to an access index.
    JSTaggedNumber accessIndex = JSTaggedValue::ToIndex(thread, requestIndex);
    RETURN_VALUE_IF_ABRUPT_COMPLETION(thread, 0);
    uint64_t index = base::NumberHelper::DoubleToUInt64(accessIndex.GetNumber());
    // Throw a BusinessError when the access index is outside the array.
    if (index >= witness.length) {
        auto error = ContainerError::BusinessError(thread, containers::ErrorFlag::RANGE_ERROR,
            "Parameter error. Index is overflow.");
        THROW_NEW_ERROR_AND_RETURN_VALUE(thread, error, 0);
    }

    // Resolve the element size and byte offset from the witness.
    uint32_t elementSize = TypedArrayHelper::GetSizeFromType(witness.type);
    // Return (accessIndex × elementSize) + offset.
    ASSERT(index * elementSize + witness.byteOffset <= UINT32_MAX);
    uint32_t allOffset = static_cast<uint32_t>(index * elementSize + witness.byteOffset);
    return allOffset;
}

JSTaggedValue CollectionsAtomicHelper::AtomicStore(JSThread *thread, const JSHandle<JSTaggedValue> &typedArray,
    JSHandle<JSTaggedValue> index, JSHandle<JSTaggedValue> &value)
{
    AtomicTypedArrayWitness witness {};
    if (!ValidateIntegerTypedArray(thread, typedArray, witness)) {
        return JSTaggedValue::Exception();
    }
    uint32_t indexedPosition = ValidateAtomicAccess(thread, witness, index);
    RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
    JSTaggedNumber integerValue = JSTaggedValue::ToInteger(thread, value);
    RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
    JSHandle<JSTaggedValue> bufferTag(thread, integerValue);
    // Conversion may promote an on-heap ByteArray to a SendableArrayBuffer; resolve the current backing.
    JSHandle<JSTaggedValue> buffer(thread,
        JSHandle<JSTypedArray>::Cast(typedArray)->GetViewedArrayBufferOrByteArray(thread));
    if (buffer->IsSendableArrayBuffer() &&
        BuiltinsSendableArrayBuffer::IsDetachedBuffer(thread, buffer.GetTaggedValue())) {
        auto error = ContainerError::ParamError(thread, "Parameter error. The SendableArrayBuffer is detached.");
        THROW_NEW_ERROR_AND_RETURN_VALUE(thread, error, JSTaggedValue::Exception());
    }
    auto *block = reinterpret_cast<uint8_t *>(
        BuiltinsArrayBuffer::GetDataPointFromBuffer(thread, buffer.GetTaggedValue()));
    uint8_t *address = block + indexedPosition;
    StoreInteger(thread, address, witness.type, bufferTag);
    return bufferTag.GetTaggedValue();
}

JSTaggedValue CollectionsAtomicHelper::AtomicLoad(JSThread *thread, const JSHandle<JSTaggedValue> &typedArray,
    JSHandle<JSTaggedValue> index)
{
    AtomicTypedArrayWitness witness {};
    if (!ValidateIntegerTypedArray(thread, typedArray, witness)) {
        return JSTaggedValue::Exception();
    }
    uint32_t indexedPosition = ValidateAtomicAccess(thread, witness, index);
    RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
    // Conversion may promote an on-heap ByteArray to a SendableArrayBuffer; resolve the current backing.
    JSHandle<JSTaggedValue> buffer(thread,
        JSHandle<JSTypedArray>::Cast(typedArray)->GetViewedArrayBufferOrByteArray(thread));
    if (buffer->IsSendableArrayBuffer() &&
        BuiltinsSendableArrayBuffer::IsDetachedBuffer(thread, buffer.GetTaggedValue())) {
        auto error = ContainerError::ParamError(thread, "Parameter error. The SendableArrayBuffer is detached.");
        THROW_NEW_ERROR_AND_RETURN_VALUE(thread, error, JSTaggedValue::Exception());
    }
    auto *block = reinterpret_cast<uint8_t *>(
        BuiltinsArrayBuffer::GetDataPointFromBuffer(thread, buffer.GetTaggedValue()));
    uint8_t *address = block + indexedPosition;
    switch (witness.type) {
        case DataViewType::INT16:
            return LoadInteger<int16_t>(thread, address);
        case DataViewType::INT32:
            return LoadInteger<int32_t>(thread, address);
        case DataViewType::INT8:
            return LoadInteger<int8_t>(thread, address);
        case DataViewType::UINT16:
            return LoadInteger<uint16_t>(thread, address);
        case DataViewType::UINT32:
            return LoadInteger<uint32_t>(thread, address);
        case DataViewType::UINT8:
            return LoadInteger<uint8_t>(thread, address);
        default:
            UNREACHABLE();
    }
}
}  // namespace panda::ecmascript::collections
