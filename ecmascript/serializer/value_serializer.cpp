/*
 * Copyright (c) 2023 Huawei Device Co., Ltd.
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

#include "ecmascript/serializer/value_serializer.h"

#include "ecmascript/base/array_helper.h"
#include "ecmascript/js_serializer.h"
#include "ecmascript/shared_mm/shared_mm.h"

namespace panda::ecmascript {

bool ValueSerializer::CheckObjectCanSerialize(TaggedObject *object, bool &findSharedObject)
{
    JSType type = object->GetClass()->GetObjectType();
    if (IsInternalJSType(type)) {
        return true;
    }
    switch (type) {
        case JSType::JS_ERROR:
        case JSType::JS_EVAL_ERROR:
        case JSType::JS_RANGE_ERROR:
        case JSType::JS_REFERENCE_ERROR:
        case JSType::JS_TYPE_ERROR:
        case JSType::JS_AGGREGATE_ERROR:
        case JSType::JS_URI_ERROR:
        case JSType::JS_SYNTAX_ERROR:
        case JSType::JS_OOM_ERROR:
        case JSType::JS_TERMINATION_ERROR:
        case JSType::JS_DATE:
        case JSType::JS_ARRAY:
        case JSType::JS_MAP:
        case JSType::JS_SET:
        case JSType::JS_REG_EXP:
        case JSType::JS_INT8_ARRAY:
        case JSType::JS_UINT8_ARRAY:
        case JSType::JS_UINT8_CLAMPED_ARRAY:
        case JSType::JS_INT16_ARRAY:
        case JSType::JS_UINT16_ARRAY:
        case JSType::JS_INT32_ARRAY:
        case JSType::JS_UINT32_ARRAY:
        case JSType::JS_FLOAT32_ARRAY:
        case JSType::JS_FLOAT64_ARRAY:
        case JSType::JS_BIGINT64_ARRAY:
        case JSType::JS_BIGUINT64_ARRAY:
        case JSType::JS_ARRAY_BUFFER:
        case JSType::JS_SHARED_ARRAY_BUFFER:
        case JSType::LINE_STRING:
        case JSType::CONSTANT_STRING:
        case JSType::TREE_STRING:
        case JSType::SLICED_STRING:
        case JSType::JS_OBJECT:
        case JSType::JS_ASYNC_FUNCTION:  // means CONCURRENT_FUNCTION
            return true;
        case JSType::JS_SHARED_OBJECT: {
            if (serializeSharedEvent_ > 0) {
                return true;
            }
            if (defaultCloneShared_ || cloneSharedSet_.find(ToUintPtr(object)) != cloneSharedSet_.end()) {
                findSharedObject = true;
                serializeSharedEvent_++;
                return true;
            }
            break;
        }
        case JSType::JS_SHARED_FUNCTION: {
            if (serializeSharedEvent_ > 0) {
                return true;
            }
            break;
        }
        default:
            break;
    }
    LOG_ECMA(ERROR) << "Unsupport serialize object type: " << JSHClass::DumpJSType(type);
    return false;
}

void ValueSerializer::InitTransferSet(CUnorderedSet<uintptr_t> transferDataSet)
{
    transferDataSet_ = std::move(transferDataSet);
}

bool ValueSerializer::WriteValue(JSThread *thread,
                                 const JSHandle<JSTaggedValue> &value,
                                 const JSHandle<JSTaggedValue> &transfer,
                                 const JSHandle<JSTaggedValue> &cloneList)
{
    ECMA_BYTRACE_NAME(HITRACE_TAG_ARK, "ValueSerializer::WriteValue");
    ASSERT(!value->IsWeak());
    if (!defaultTransfer_ && !PrepareTransfer(thread, transfer)) {
        LOG_ECMA(ERROR) << "ValueSerialize: PrepareTransfer fail";
        data_->SetIncompleteData(true);
        return false;
    }
    if (!defaultCloneShared_ && !PrepareClone(thread, cloneList)) {
        LOG_ECMA(ERROR) << "ValueSerialize: PrepareClone fail";
        data_->SetIncompleteData(true);
        return false;
    }
    if (value->IsHeapObject()) {
        // Add fast path for string
        if (value->IsString()) {
            vm_->GetSnapshotEnv()->InitializeStringClass();
        } else {
            vm_->GetSnapshotEnv()->Initialize();
        }
    }
    SerializeJSTaggedValue(value.GetTaggedValue());
    if (value->IsHeapObject()) {
        vm_->GetSnapshotEnv()->ClearEnvMap();
    }
    if (notSupport_) {
        LOG_ECMA(ERROR) << "ValueSerialize: serialize data is incomplete";
        data_->SetIncompleteData(true);
        return false;
    }
    size_t maxSerializerSize = vm_->GetEcmaParamConfiguration().GetMaxJSSerializerSize();
    if (data_->Size() > maxSerializerSize) {
        LOG_ECMA(ERROR) << "The serialization data size has exceed limit Size, current size is: " << data_->Size()
                        << " max size is: " << maxSerializerSize;
        return false;
    }
    return true;
}

void ValueSerializer::SerializeObjectImpl(TaggedObject *object, bool isWeak)
{
    if (notSupport_) {
        return;
    }
    bool cloneSharedObject = false;
    if (!CheckObjectCanSerialize(object, cloneSharedObject)) {
        notSupport_ = true;
        return;
    }
    if (isWeak) {
        data_->WriteEncodeFlag(EncodeFlag::WEAK);
    }
    if (SerializeReference(object) || SerializeRootObject(object)) {
        return;
    }
    if (object->GetClass()->IsNativeBindingObject()) {
        SerializeNativeBindingObject(object);
        return;
    }
    if (object->GetClass()->IsJSError()) {
        SerializeJSError(object);
        return;
    }
    bool arrayBufferDeferDetach = false;
    JSTaggedValue trackInfo;
    JSTaggedType hashfield = JSTaggedValue::VALUE_ZERO;
    JSType type = object->GetClass()->GetObjectType();
    // serialize prologue
    switch (type) {
        case JSType::JS_ARRAY_BUFFER:
            arrayBufferDeferDetach = SerializeJSArrayBufferPrologue(object);
            break;
        case JSType::JS_SHARED_ARRAY_BUFFER:
            SerializeJSSharedArrayBufferPrologue(object);
            break;
        case JSType::METHOD:
            SerializeMethodPrologue(reinterpret_cast<Method *>(object));
            break;
        case JSType::JS_ARRAY: {
            JSArray *array = reinterpret_cast<JSArray *>(object);
            trackInfo = array->GetTrackInfo();
            array->SetTrackInfo(thread_, JSTaggedValue::Undefined());
            break;
        }
        case JSType::TREE_STRING:
        case JSType::SLICED_STRING:
            object = EcmaStringAccessor::FlattenNoGC(vm_, EcmaString::Cast(object));
            break;
        case JSType::JS_REG_EXP:
            SerializeJSRegExpPrologue(reinterpret_cast<JSRegExp *>(object));
            break;
        case JSType::JS_SHARED_FUNCTION: {
            if (serializeSharedEvent_ > 0) {
                data_->WriteEncodeFlag(EncodeFlag::JS_FUNCTION_IN_SHARED);
            }
            break;
        }
        case JSType::JS_OBJECT:
            hashfield = Barriers::GetValue<JSTaggedType>(object, JSObject::HASH_OFFSET);
            Barriers::SetPrimitive<JSTaggedType>(object, JSObject::HASH_OFFSET, JSTaggedValue::VALUE_ZERO);
            break;
        default:
            break;
    }

    // serialize object here
    SerializeTaggedObject<SerializeType::VALUE_SERIALIZE>(object);

    // serialize epilogue
    if (type == JSType::JS_ARRAY) {
        JSArray *array = reinterpret_cast<JSArray *>(object);
        array->SetTrackInfo(thread_, trackInfo);
    } else if (type == JSType::JS_OBJECT) {
        if (JSTaggedValue(hashfield).IsHeapObject()) {
            Barriers::SetObject<true>(thread_, object, JSObject::HASH_OFFSET, hashfield);
        } else {
            Barriers::SetPrimitive<JSTaggedType>(object, JSObject::HASH_OFFSET, hashfield);
        }
    }
    if (cloneSharedObject) {
        serializeSharedEvent_--;
    }
    if (arrayBufferDeferDetach) {
        ASSERT(object->GetClass()->IsArrayBuffer());
        JSArrayBuffer *arrayBuffer = reinterpret_cast<JSArrayBuffer *>(object);
        arrayBuffer->Detach(thread_, arrayBuffer->GetWithNativeAreaAllocator());
    }
}

void ValueSerializer::SerializeJSError(TaggedObject *object)
{
    [[maybe_unused]] EcmaHandleScope scope(thread_);
    data_->WriteEncodeFlag(EncodeFlag::JS_ERROR);
    JSType type = object->GetClass()->GetObjectType();
    ASSERT(type >= JSType::JS_ERROR_FIRST && type <= JSType::JS_ERROR_LAST);
    data_->WriteUint8(static_cast<uint8_t>(type));
    auto globalConst = thread_->GlobalConstants();
    JSHandle<JSTaggedValue> handleMsg = globalConst->GetHandledMessageString();
    JSHandle<JSTaggedValue> msg =
        JSObject::GetProperty(thread_, JSHandle<JSTaggedValue>(thread_, object), handleMsg).GetValue();
    if (msg->IsString()) {
        EcmaString *str = EcmaStringAccessor::FlattenNoGC(vm_, EcmaString::Cast(msg->GetTaggedObject()));
        data_->WriteUint8(1); // 1: msg is string
        SerializeTaggedObject<SerializeType::VALUE_SERIALIZE>(str);
    } else {
        data_->WriteUint8(0); // 0: msg is undefined
    }
}

void ValueSerializer::SerializeNativeBindingObject(TaggedObject *object)
{
    [[maybe_unused]] EcmaHandleScope scope(thread_);
    JSHandle<GlobalEnv> env = vm_->GetGlobalEnv();
    JSHandle<JSTaggedValue> nativeBindingSymbol = env->GetNativeBindingSymbol();
    JSHandle<JSTaggedValue> nativeBindingValue =
        JSObject::GetProperty(thread_, JSHandle<JSObject>(thread_, object), nativeBindingSymbol).GetRawValue();
    if (!nativeBindingValue->IsJSNativePointer()) {
        LOG_ECMA(ERROR) << "ValueSerialize: SerializeNativeBindingObject nativeBindingValue is not JSNativePointer";
        notSupport_ = true;
        return;
    }
    auto info = reinterpret_cast<panda::JSNApi::NativeBindingInfo *>(
        JSNativePointer::Cast(nativeBindingValue->GetTaggedObject())->GetExternalPointer());
    if (info == nullptr) {
        LOG_ECMA(ERROR) << "ValueSerialize: SerializeNativeBindingObject NativeBindingInfo is nullptr";
        notSupport_ = true;
        return;
    }
    void *enginePointer = info->env;
    void *objPointer = info->nativeValue;
    void *hint = info->hint;
    void *detachData = info->detachData;
    void *attachData = info->attachData;
    DetachFunc detachNative = reinterpret_cast<DetachFunc>(info->detachFunc);
    if (detachNative == nullptr) {
        LOG_ECMA(ERROR) << "ValueSerialize: SerializeNativeBindingObject detachNative == nullptr";
        notSupport_ = true;
        return;
    }
    void *buffer = detachNative(enginePointer, objPointer, hint, detachData);
    AttachFunc attachNative = reinterpret_cast<AttachFunc>(info->attachFunc);
    data_->WriteEncodeFlag(EncodeFlag::NATIVE_BINDING_OBJECT);
    data_->WriteJSTaggedType(reinterpret_cast<JSTaggedType>(attachNative));
    data_->WriteJSTaggedType(reinterpret_cast<JSTaggedType>(buffer));
    data_->WriteJSTaggedType(reinterpret_cast<JSTaggedType>(hint));
    data_->WriteJSTaggedType(reinterpret_cast<JSTaggedType>(attachData));
}

bool ValueSerializer::SerializeJSArrayBufferPrologue(TaggedObject *object)
{
    ASSERT(object->GetClass()->IsArrayBuffer());
    JSArrayBuffer *arrayBuffer = reinterpret_cast<JSArrayBuffer *>(object);
    if (arrayBuffer->IsDetach()) {
        LOG_ECMA(ERROR) << "ValueSerialize: don't support serialize detached array buffer";
        notSupport_ = true;
        return false;
    }
    bool transfer = transferDataSet_.find(ToUintPtr(object)) != transferDataSet_.end();
    bool clone = cloneArrayBufferSet_.find(ToUintPtr(object)) != cloneArrayBufferSet_.end();
    size_t arrayLength = arrayBuffer->GetArrayBufferByteLength();
    if (arrayLength > 0) {
        if (transfer) {
            if (clone) {
                notSupport_ = true;
                LOG_ECMA(ERROR) << "ValueSerialize: can't put arraybuffer in both transfer list and clone list";
                return false;
            }
            data_->WriteEncodeFlag(EncodeFlag::TRANSFER_ARRAY_BUFFER);
            return true;
        } else if (clone || !defaultTransfer_) {
            data_->WriteEncodeFlag(EncodeFlag::ARRAY_BUFFER);
            data_->WriteUint32(arrayLength);
            JSNativePointer *np =
                reinterpret_cast<JSNativePointer *>(arrayBuffer->GetArrayBufferData().GetTaggedObject());
            data_->WriteRawData(static_cast<uint8_t *>(np->GetExternalPointer()), arrayLength);
            return false;
        } else {
            data_->WriteEncodeFlag(EncodeFlag::TRANSFER_ARRAY_BUFFER);
            return true;
        }
    }
    return false;
}

void ValueSerializer::SerializeJSSharedArrayBufferPrologue(TaggedObject *object)
{
    ASSERT(object->GetClass()->IsSharedArrayBuffer());
    JSArrayBuffer *arrayBuffer = reinterpret_cast<JSArrayBuffer *>(object);
    bool transfer = transferDataSet_.find(ToUintPtr(object)) != transferDataSet_.end();
    if (arrayBuffer->IsDetach() || transfer) {
        LOG_ECMA(ERROR) << "ValueSerialize: don't support serialize detached or transfer shared array buffer";
        notSupport_ = true;
        return;
    }
    size_t arrayLength = arrayBuffer->GetArrayBufferByteLength();
    if (arrayLength > 0) {
        JSNativePointer *np = reinterpret_cast<JSNativePointer *>(arrayBuffer->GetArrayBufferData().GetTaggedObject());
        void *buffer = np->GetExternalPointer();
        if (JSSharedMemoryManager::GetInstance()->CreateOrLoad(&buffer, arrayLength)) {
            LOG_ECMA(ERROR) << "ValueSerialize: can't find buffer form shared memory pool";
            notSupport_ = true;
            return;
        }
    }
}

void ValueSerializer::SerializeMethodPrologue(Method *method)
{
    JSTaggedValue constPoolVal = method->GetConstantPool();
    if (!constPoolVal.IsHeapObject()) {
        return;
    }
    ConstantPool *constPool = reinterpret_cast<ConstantPool *>(constPoolVal.GetTaggedObject());
    const JSPandaFile *jsPandaFile = constPool->GetJSPandaFile();
    data_->WriteEncodeFlag(EncodeFlag::METHOD);
    data_->WriteJSTaggedType(method->GetLiteralInfo());
    data_->WriteJSTaggedType(reinterpret_cast<JSTaggedType>(jsPandaFile));
}

void ValueSerializer::SerializeJSRegExpPrologue(JSRegExp *jsRegExp)
{
    uint32_t bufferSize = jsRegExp->GetLength();
    if (bufferSize == 0) {
        LOG_ECMA(ERROR) << "ValueSerialize: JSRegExp buffer size is 0";
        notSupport_ = true;
        return;
    }

    data_->WriteEncodeFlag(EncodeFlag::JS_REG_EXP);
    data_->WriteUint32(bufferSize);
    JSNativePointer *np =
        reinterpret_cast<JSNativePointer *>(jsRegExp->GetByteCodeBuffer().GetTaggedObject());
    data_->WriteRawData(static_cast<uint8_t *>(np->GetExternalPointer()), bufferSize);
}

bool ValueSerializer::PrepareTransfer(JSThread *thread, const JSHandle<JSTaggedValue> &transfer)
{
    if (transfer->IsUndefined()) {
        return true;
    }
    if (!transfer->IsJSArray()) {
        return false;
    }
    int len = base::ArrayHelper::GetArrayLength(thread, transfer);
    int k = 0;
    while (k < len) {
        bool exists = JSTaggedValue::HasProperty(thread, transfer, k);
        if (exists) {
            JSHandle<JSTaggedValue> element = JSArray::FastGetPropertyByValue(thread, transfer, k);
            if (!element->IsArrayBuffer()) {
                return false;
            }
            transferDataSet_.insert(static_cast<uintptr_t>(element.GetTaggedType()));
        }
        k++;
    }
    return true;
}

bool ValueSerializer::PrepareClone(JSThread *thread, const JSHandle<JSTaggedValue> &cloneList)
{
    if (cloneList->IsUndefined()) {
        return true;
    }
    if (!cloneList->IsJSArray()) {
        return false;
    }
    int len = base::ArrayHelper::GetArrayLength(thread, cloneList);
    int index = 0;
    while (index < len) {
        bool exists = JSTaggedValue::HasProperty(thread, cloneList, index);
        if (exists) {
            JSHandle<JSTaggedValue> element = JSArray::FastGetPropertyByValue(thread, cloneList, index);
            if (element->IsArrayBuffer()) {
                cloneArrayBufferSet_.insert(static_cast<uintptr_t>(element.GetTaggedType()));
            } else if (element->IsJSSharedObject()) {
                cloneSharedSet_.insert(static_cast<uintptr_t>(element.GetTaggedType()));
            } else {
                return false;
            }
        }
        index++;
    }
    return true;
}
}  // namespace panda::ecmascript

