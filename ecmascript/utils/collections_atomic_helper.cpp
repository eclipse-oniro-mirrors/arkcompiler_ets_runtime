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

#include "ecmascript/base/typed_array_helper-inl.h"

namespace panda::ecmascript::collections {
using TypedArrayHelper = base::TypedArrayHelper;
using BuiltinsBase = base::BuiltinsBase;
using BuiltinsArrayBuffer = builtins::BuiltinsArrayBuffer;
using BuiltinsSendableArrayBuffer = builtins::BuiltinsSendableArrayBuffer;

namespace {
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
    if (!typedArray->IsSharedTypedArray()) {
        THROW_TYPE_ERROR_AND_RETURN(thread, "The typedArray must be a collections TypedArray.", false);
    }
    // Validate the backing before checking the supported element types.
    JSHandle<JSTaggedValue> buffer(thread, TypedArrayHelper::ValidateTypedArray(thread, typedArray));
    if (thread->HasPendingException()) {
        return false;
    }

    // Resolve the element type from typedArray.[[TypedArrayName]].
    DataViewType type = JSTypedArray::Cast(typedArray->GetTaggedObject())->GetContentType();

    // Collections supports six unclamped integer types; only Int32 is waitable.
    if (waitable) {
        const bool validWaitType = type == DataViewType::INT32;
        if (!validWaitType) {
            THROW_TYPE_ERROR_AND_RETURN(thread, "The typedArray must be a collections Int32Array.", false);
        }
    } else {
        const bool validIntegerType = BuiltinsArrayBuffer::IsUnclampedIntegerElementType(type);
        if (!validIntegerType) {
            THROW_TYPE_ERROR_AND_RETURN(thread,
                "The collections TypedArray element type is not an unclamped integer.", false);
        }
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
    // Throw a RangeError when the access index is outside the array.
    if (index >= witness.length) {
        THROW_RANGE_ERROR_AND_RETURN(thread, "Index is overflow.", 0);
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
        THROW_TYPE_ERROR_AND_RETURN(thread, "The SendableArrayBuffer is detached.", JSTaggedValue::Exception());
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
        THROW_TYPE_ERROR_AND_RETURN(thread, "The SendableArrayBuffer is detached.", JSTaggedValue::Exception());
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
