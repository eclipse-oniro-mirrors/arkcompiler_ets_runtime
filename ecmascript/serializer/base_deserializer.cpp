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

#include "ecmascript/serializer/base_deserializer.h"

#include "ecmascript/ecma_string_table.h"
#include "ecmascript/ecma_vm.h"
#include "ecmascript/global_env.h"
#include "ecmascript/js_arraybuffer.h"
#include "ecmascript/js_regexp.h"
#include "ecmascript/js_thread.h"
#include "ecmascript/mem/mem.h"
#include "ecmascript/mem/sparse_space.h"
#include "ecmascript/platform/mutex.h"
#include "ecmascript/runtime.h"
#include "ecmascript/runtime_lock.h"
#include "ecmascript/shared_mm/shared_mm.h"

namespace panda::ecmascript {

#define NEW_OBJECT_ALL_SPACES()                                           \
    (uint8_t)SerializedObjectSpace::OLD_SPACE:                            \
    case (uint8_t)SerializedObjectSpace::NON_MOVABLE_SPACE:               \
    case (uint8_t)SerializedObjectSpace::MACHINE_CODE_SPACE:              \
    case (uint8_t)SerializedObjectSpace::HUGE_SPACE:                      \
    case (uint8_t)SerializedObjectSpace::SHARED_OLD_SPACE:                \
    case (uint8_t)SerializedObjectSpace::SHARED_NON_MOVABLE_SPACE:        \
    case (uint8_t)SerializedObjectSpace::SHARED_HUGE_SPACE

JSHandle<JSTaggedValue> BaseDeserializer::ReadValue()
{
    ECMA_BYTRACE_NAME(HITRACE_TAG_ARK, "Deserialize dataSize: " + std::to_string(data_->Size()));
    AllocateToDifferentSpaces();
    JSHandle<JSTaggedValue> res = DeserializeJSTaggedValue();
    return res;
}

JSHandle<JSTaggedValue> BaseDeserializer::DeserializeJSTaggedValue()
{
    if (data_->IsIncompleteData()) {
        LOG_ECMA(ERROR) << "The serialization data is incomplete";
        return JSHandle<JSTaggedValue>();
    }

    // stop gc during deserialize
    heap_->SetOnSerializeEvent(true);

    uint8_t encodeFlag = data_->ReadUint8(position_);
    JSHandle<JSTaggedValue> resHandle(thread_, JSTaggedValue::Undefined());
    while (ReadSingleEncodeData(encodeFlag, resHandle.GetAddress(), 0, true) == 0) { // 0: root object offset
        encodeFlag = data_->ReadUint8(position_);
    }
    // now new constpool here if newConstPoolInfos_ is not empty
    for (auto newConstpoolInfo : newConstPoolInfos_) {
        DeserializeConstPool(newConstpoolInfo);
        delete newConstpoolInfo;
    }
    newConstPoolInfos_.clear();

    // initialize concurrent func here after constpool is set
    for (auto func : concurrentFunctions_) {
        func->InitializeForConcurrentFunction(thread_);
    }
    concurrentFunctions_.clear();

    // new native binding object here
    for (auto nativeBindingInfo : nativeBindingInfos_) {
        DeserializeNativeBindingObject(nativeBindingInfo);
        delete nativeBindingInfo;
    }
    nativeBindingInfos_.clear();

    // new js error here
    for (auto jsErrorInfo : jsErrorInfos_) {
        DeserializeJSError(jsErrorInfo);
        delete jsErrorInfo;
    }
    jsErrorInfos_.clear();

    // recovery gc after serialize
    heap_->SetOnSerializeEvent(false);

    // If is on concurrent mark, mark push root object to stack for mark
    if (resHandle->IsHeapObject() && thread_->IsConcurrentMarkingOrFinished()) {
        Barriers::MarkAndPushForDeserialize(thread_, resHandle->GetHeapObject());
    }

    return resHandle;
}

uintptr_t BaseDeserializer::DeserializeTaggedObject(SerializedObjectSpace space)
{
    size_t objSize = data_->ReadUint32(position_);
    uintptr_t res = RelocateObjectAddr(space, objSize);
    objectVector_.push_back(res);
    DeserializeObjectField(res, res + objSize);
    return res;
}

void BaseDeserializer::DeserializeObjectField(uintptr_t start, uintptr_t end)
{
    size_t offset = 0; // 0: initial offset
    while (start + offset < end) {
        uint8_t encodeFlag = data_->ReadUint8(position_);
        offset += ReadSingleEncodeData(encodeFlag, start, offset);
    }
}

void BaseDeserializer::DeserializeConstPool(NewConstPoolInfo *info)
{
    [[maybe_unused]] EcmaHandleScope scope(thread_);
    JSPandaFile *jsPandaFile = info->jsPandaFile_;
    panda_file::File::EntityId methodId = info->methodId_;
    ObjectSlot slot = info->GetSlot();
    EcmaContext *context = thread_->GetCurrentEcmaContext();
    JSHandle<ConstantPool> constpool = context->CreateConstpoolPair(jsPandaFile, methodId);

    slot.Update(constpool.GetTaggedType());
    WriteBarrier(thread_, reinterpret_cast<void *>(info->GetObjAddr()), info->GetFieldOffset(),
                 constpool.GetTaggedType());
}

void BaseDeserializer::DeserializeNativeBindingObject(NativeBindingInfo *info)
{
    [[maybe_unused]] EcmaHandleScope scope(thread_);
    AttachFunc af = info->af_;
    void *bufferPointer = info->bufferPointer_;
    void *hint = info->hint_;
    void *attachData = info->attachData_;
    bool root = info->root_;
    Local<JSValueRef> attachVal = af(engine_, bufferPointer, hint, attachData);
    if (attachVal.IsEmpty()) {
        LOG_ECMA(ERROR) << "NativeBindingObject is empty";
        attachVal = JSValueRef::Undefined(thread_->GetEcmaVM());
    }
    JSTaggedType res = JSNApiHelper::ToJSHandle(attachVal).GetTaggedType();
    ObjectSlot slot = info->GetSlot();
    slot.Update(res);
    if (!root && !JSTaggedValue(res).IsInvalidValue()) {
        WriteBarrier(thread_, reinterpret_cast<void *>(info->GetObjAddr()), info->GetFieldOffset(), res);
    }
}

void BaseDeserializer::DeserializeJSError(JSErrorInfo *info)
{
    [[maybe_unused]] EcmaHandleScope scope(thread_);
    uint8_t type = info->errorType_;
    base::ErrorType errorType = base::ErrorType(type - static_cast<uint8_t>(JSType::JS_ERROR_FIRST));
    JSTaggedValue errorMsg = info->errorMsg_;
    bool root = info->root_;
    ObjectFactory *factory = thread_->GetEcmaVM()->GetFactory();
    JSHandle<JSObject> errorTag = factory->NewJSError(errorType, JSHandle<EcmaString>(thread_, errorMsg));
    ObjectSlot slot = info->GetSlot();
    slot.Update(errorTag.GetTaggedType());
    if (!root && !errorTag.GetTaggedValue().IsInvalidValue()) {
        WriteBarrier(thread_, reinterpret_cast<void *>(info->GetObjAddr()), info->GetFieldOffset(),
                     errorTag.GetTaggedType());
    }
}

void BaseDeserializer::HandleNewObjectEncodeFlag(SerializedObjectSpace space,  uintptr_t objAddr, size_t fieldOffset,
                                                 bool isRoot)
{
    // deserialize object prologue
    bool isWeak = GetAndResetWeak();
    bool isTransferBuffer = GetAndResetTransferBuffer();
    bool isSharedArrayBuffer = GetAndResetSharedArrayBuffer();
    void *bufferPointer = GetAndResetBufferPointer();
    ConstantPool *constpool = GetAndResetConstantPool();
    bool needNewConstPool = GetAndResetNeedNewConstPool();
    // deserialize object here
    uintptr_t addr = DeserializeTaggedObject(space);

    // deserialize object epilogue
    if (isTransferBuffer) {
        TransferArrayBufferAttach(addr);
    } else if (isSharedArrayBuffer) {
        IncreaseSharedArrayBufferReference(addr);
    } else if (bufferPointer != nullptr) {
        ResetNativePointerBuffer(addr, bufferPointer);
    } else if (constpool != nullptr) {
        ResetMethodConstantPool(addr, constpool);
    } else if (needNewConstPool) {
        // defer new constpool
        newConstPoolInfos_.back()->objAddr_ = addr;
        newConstPoolInfos_.back()->offset_ = Method::CONSTANT_POOL_OFFSET;
    }
    TaggedObject *object = reinterpret_cast<TaggedObject *>(addr);
    if (object->GetClass()->IsJSNativePointer()) {
        JSNativePointer *nativePointer = reinterpret_cast<JSNativePointer *>(object);
        if (nativePointer->GetDeleter() != nullptr) {
            thread_->GetEcmaVM()->PushToNativePointerList(nativePointer);
        }
    } else if (object->GetClass()->IsJSFunction()) {
        JSFunction* func = reinterpret_cast<JSFunction *>(object);
        FunctionKind funcKind = func->GetFunctionKind();
        if (funcKind == FunctionKind::CONCURRENT_FUNCTION) {
            // defer initialize concurrent function until constpool is set
            concurrentFunctions_.push_back(reinterpret_cast<JSFunction *>(object));
        }
    }
    UpdateMaybeWeak(ObjectSlot(objAddr + fieldOffset), addr, isWeak);
    if (!isRoot) {
        WriteBarrier<WriteBarrierType::DESERIALIZE>(thread_, reinterpret_cast<void *>(objAddr), fieldOffset,
                                                    static_cast<JSTaggedType>(addr));
    }
}

void BaseDeserializer::HandleMethodEncodeFlag()
{
    panda_file::File::EntityId methodId = MethodLiteral::GetMethodId(data_->ReadJSTaggedType(position_));
    JSPandaFile *jsPandaFile = reinterpret_cast<JSPandaFile *>(data_->ReadJSTaggedType(position_));
    panda_file::IndexAccessor indexAccessor(*jsPandaFile->GetPandaFile(), methodId);
    int32_t index = static_cast<int32_t>(indexAccessor.GetHeaderIndex());
    JSTaggedValue constpool = thread_->GetCurrentEcmaContext()->FindConstpoolWithAOT(jsPandaFile, index);
    if (constpool.IsHole()) {
        LOG_ECMA(INFO) << "ValueDeserialize: function deserialize can't find constpool from panda file: "
            << jsPandaFile;
        // defer new constpool until deserialize finish
        needNewConstPool_ = true;
        newConstPoolInfos_.push_back(new NewConstPoolInfo(jsPandaFile, methodId));
    } else {
        constpool_ = reinterpret_cast<ConstantPool *>(constpool.GetTaggedObject());
    }
}

void BaseDeserializer::TransferArrayBufferAttach(uintptr_t objAddr)
{
    ASSERT(JSTaggedValue(static_cast<JSTaggedType>(objAddr)).IsArrayBuffer());
    JSArrayBuffer *arrayBuffer = reinterpret_cast<JSArrayBuffer *>(objAddr);
    size_t arrayLength = arrayBuffer->GetArrayBufferByteLength();
    bool withNativeAreaAllocator = arrayBuffer->GetWithNativeAreaAllocator();
    JSNativePointer *np = reinterpret_cast<JSNativePointer *>(arrayBuffer->GetArrayBufferData().GetTaggedObject());
    arrayBuffer->Attach(thread_, arrayLength, JSTaggedValue(np), withNativeAreaAllocator);
}

void BaseDeserializer::IncreaseSharedArrayBufferReference(uintptr_t objAddr)
{
    ASSERT(JSTaggedValue(static_cast<JSTaggedType>(objAddr)).IsSharedArrayBuffer());
    JSArrayBuffer *arrayBuffer = reinterpret_cast<JSArrayBuffer *>(objAddr);
    size_t arrayLength = arrayBuffer->GetArrayBufferByteLength();
    JSNativePointer *np = reinterpret_cast<JSNativePointer *>(arrayBuffer->GetArrayBufferData().GetTaggedObject());
    void *buffer = np->GetExternalPointer();
    if (JSSharedMemoryManager::GetInstance()->CreateOrLoad(&buffer, arrayLength)) {
        LOG_ECMA(FATAL) << "BaseDeserializer::IncreaseSharedArrayBufferReference failed";
    }
}

void BaseDeserializer::ResetNativePointerBuffer(uintptr_t objAddr, void *bufferPointer)
{
    JSTaggedValue obj = JSTaggedValue(static_cast<JSTaggedType>(objAddr));
    ASSERT(obj.IsArrayBuffer() || obj.IsJSRegExp());
    auto nativeAreaAllocator = thread_->GetEcmaVM()->GetNativeAreaAllocator();
    JSNativePointer *np = nullptr;
    if (obj.IsArrayBuffer()) {
        JSArrayBuffer *arrayBuffer = reinterpret_cast<JSArrayBuffer *>(objAddr);
        arrayBuffer->SetWithNativeAreaAllocator(true);
        np = reinterpret_cast<JSNativePointer *>(arrayBuffer->GetArrayBufferData().GetTaggedObject());
        nativeAreaAllocator->IncreaseNativeSizeStats(arrayBuffer->GetArrayBufferByteLength(), NativeFlag::ARRAY_BUFFER);
    } else {
        JSRegExp *jsRegExp = reinterpret_cast<JSRegExp *>(objAddr);
        np = reinterpret_cast<JSNativePointer *>(jsRegExp->GetByteCodeBuffer().GetTaggedObject());
        nativeAreaAllocator->IncreaseNativeSizeStats(jsRegExp->GetLength(), NativeFlag::REGEXP_BTYECODE);
    }

    np->SetExternalPointer(bufferPointer);
    np->SetDeleter(NativeAreaAllocator::FreeBufferFunc);
    np->SetData(thread_->GetEcmaVM()->GetNativeAreaAllocator());
}

void BaseDeserializer::ResetMethodConstantPool(uintptr_t objAddr, ConstantPool *constpool)
{
    ASSERT(JSTaggedValue(static_cast<JSTaggedType>(objAddr)).IsMethod());
    Method *method = reinterpret_cast<Method *>(objAddr);
    method->SetConstantPool(thread_, JSTaggedValue(constpool), BarrierMode::SKIP_BARRIER);
}

size_t BaseDeserializer::ReadSingleEncodeData(uint8_t encodeFlag, uintptr_t objAddr, size_t fieldOffset, bool isRoot)
{
    size_t handledFieldSize = sizeof(JSTaggedType);
    ObjectSlot slot(objAddr + fieldOffset);
    switch (encodeFlag) {
        case NEW_OBJECT_ALL_SPACES(): {
            SerializedObjectSpace space = SerializeData::DecodeSpace(encodeFlag);
            HandleNewObjectEncodeFlag(space, objAddr, fieldOffset, isRoot);
            break;
        }
        case (uint8_t)EncodeFlag::REFERENCE: {
            uint32_t valueIndex = data_->ReadUint32(position_);
            uintptr_t valueAddr = objectVector_[valueIndex];
            UpdateMaybeWeak(slot, valueAddr, GetAndResetWeak());
            WriteBarrier<WriteBarrierType::DESERIALIZE>(thread_, reinterpret_cast<void *>(objAddr), fieldOffset,
                                                        static_cast<JSTaggedType>(valueAddr));
            break;
        }
        case (uint8_t)EncodeFlag::WEAK: {
            ASSERT(!isWeak_);
            isWeak_ = true;
            handledFieldSize = 0;
            break;
        }
        case (uint8_t)EncodeFlag::PRIMITIVE: {
            JSTaggedType value = data_->ReadJSTaggedType(position_);
            slot.Update(value);
            break;
        }
        case (uint8_t)EncodeFlag::MULTI_RAW_DATA: {
            uint32_t size = data_->ReadUint32(position_);
            data_->ReadRawData(objAddr + fieldOffset, size, position_);
            handledFieldSize = size;
            break;
        }
        case (uint8_t)EncodeFlag::ROOT_OBJECT: {
            uint32_t index = data_->ReadUint32(position_);
            uintptr_t valueAddr = thread_->GetEcmaVM()->GetSnapshotEnv()->RelocateRootObjectAddr(index);
            if (!isRoot && valueAddr > JSTaggedValue::INVALID_VALUE_LIMIT) {
                WriteBarrier<WriteBarrierType::DESERIALIZE>(thread_, reinterpret_cast<void *>(objAddr), fieldOffset,
                                                            static_cast<JSTaggedType>(valueAddr));
            }
            UpdateMaybeWeak(slot, valueAddr, GetAndResetWeak());
            break;
        }
        case (uint8_t)EncodeFlag::OBJECT_PROTO: {
            uint8_t type = data_->ReadUint8(position_);
            uintptr_t protoAddr = RelocateObjectProtoAddr(type);
            if (!isRoot && protoAddr > JSTaggedValue::INVALID_VALUE_LIMIT) {
                WriteBarrier<WriteBarrierType::DESERIALIZE>(thread_, reinterpret_cast<void *>(objAddr), fieldOffset,
                                                            static_cast<JSTaggedType>(protoAddr));
            }
            UpdateMaybeWeak(slot, protoAddr, GetAndResetWeak());
            break;
        }
        case (uint8_t)EncodeFlag::TRANSFER_ARRAY_BUFFER: {
            isTransferArrayBuffer_ = true;
            handledFieldSize = 0;
            break;
        }
        case (uint8_t)EncodeFlag::SHARED_ARRAY_BUFFER: {
            isSharedArrayBuffer_ = true;
            handledFieldSize = 0;
            break;
        }
        case (uint8_t)EncodeFlag::ARRAY_BUFFER:
        case (uint8_t)EncodeFlag::JS_REG_EXP: {
            size_t bufferLength = data_->ReadUint32(position_);
            auto nativeAreaAllocator = thread_->GetEcmaVM()->GetNativeAreaAllocator();
            bufferPointer_ = nativeAreaAllocator->AllocateBuffer(bufferLength);
            data_->ReadRawData(ToUintPtr(bufferPointer_), bufferLength, position_);
            heap_->IncreaseNativeBindingSize(bufferLength);
            handledFieldSize = 0;
            break;
        }
        case (uint8_t)EncodeFlag::METHOD: {
            HandleMethodEncodeFlag();
            handledFieldSize = 0;
            break;
        }
        case (uint8_t)EncodeFlag::NATIVE_BINDING_OBJECT: {
            slot.Update(JSTaggedValue::Undefined().GetRawData());
            AttachFunc af = reinterpret_cast<AttachFunc>(data_->ReadJSTaggedType(position_));
            void *bufferPointer = reinterpret_cast<void *>(data_->ReadJSTaggedType(position_));
            void *hint = reinterpret_cast<void *>(data_->ReadJSTaggedType(position_));
            void *attachData = reinterpret_cast<void *>(data_->ReadJSTaggedType(position_));
            // defer new native binding object until deserialize finish
            nativeBindingInfos_.push_back(new NativeBindingInfo(af, bufferPointer, hint, attachData,
                                                                objAddr, fieldOffset, isRoot));
            break;
        }
        case (uint8_t)EncodeFlag::JS_ERROR: {
            slot.Update(JSTaggedValue::Undefined().GetRawData());
            uint8_t type = data_->ReadUint8(position_);
            ASSERT(type >= static_cast<uint8_t>(JSType::JS_ERROR_FIRST)
                && type <= static_cast<uint8_t>(JSType::JS_ERROR_LAST));
            jsErrorInfos_.push_back(new JSErrorInfo(type, JSTaggedValue::Undefined(), objAddr, fieldOffset, isRoot));
            uint8_t flag = data_->ReadUint8(position_);
            if (flag == 1) { // error msg is string
                isErrorMsg_ = true;
                handledFieldSize = 0;
            }
            break;
        }
        case (uint8_t)EncodeFlag::SHARED_OBJECT: {
            JSTaggedType value = data_->ReadJSTaggedType(position_);
            objectVector_.push_back(value);
            bool isErrorMsg = GetAndResetIsErrorMsg();
            if (isErrorMsg) {
                // defer new js error
                jsErrorInfos_.back()->errorMsg_ = JSTaggedValue(value);
                break;
            }
            if (!isRoot) {
                WriteBarrier<WriteBarrierType::DESERIALIZE>(thread_, reinterpret_cast<void *>(objAddr), fieldOffset,
                                                            value);
            }
            UpdateMaybeWeak(slot, value, GetAndResetWeak());
            break;
        }
        default:
            LOG_ECMA(FATAL) << "this branch is unreachable";
            UNREACHABLE();
            break;
    }
    return handledFieldSize;
}

uintptr_t BaseDeserializer::RelocateObjectAddr(SerializedObjectSpace space, size_t objSize)
{
    uintptr_t res = 0U;
    switch (space) {
        case SerializedObjectSpace::OLD_SPACE: {
            if (oldSpaceBeginAddr_ + objSize > AlignUp(oldSpaceBeginAddr_, DEFAULT_REGION_SIZE)) {
                ASSERT(oldRegionIndex_ < regionVector_.size());
                oldSpaceBeginAddr_ = regionVector_[oldRegionIndex_++]->GetBegin();
            }
            res = oldSpaceBeginAddr_;
            oldSpaceBeginAddr_ += objSize;
            break;
        }
        case SerializedObjectSpace::NON_MOVABLE_SPACE: {
            if (nonMovableSpaceBeginAddr_ + objSize > AlignUp(nonMovableSpaceBeginAddr_, DEFAULT_REGION_SIZE)) {
                ASSERT(nonMovableRegionIndex_ < regionVector_.size());
                nonMovableSpaceBeginAddr_ = regionVector_[nonMovableRegionIndex_++]->GetBegin();
            }
            res = nonMovableSpaceBeginAddr_;
            nonMovableSpaceBeginAddr_ += objSize;
            break;
        }
        case SerializedObjectSpace::MACHINE_CODE_SPACE: {
            if (machineCodeSpaceBeginAddr_ + objSize > AlignUp(machineCodeSpaceBeginAddr_, DEFAULT_REGION_SIZE)) {
                ASSERT(machineCodeRegionIndex_ < regionVector_.size());
                machineCodeSpaceBeginAddr_ = regionVector_[machineCodeRegionIndex_++]->GetBegin();
            }
            res = machineCodeSpaceBeginAddr_;
            machineCodeSpaceBeginAddr_ += objSize;
            break;
        }
        case SerializedObjectSpace::HUGE_SPACE: {
            // no gc for this allocate
            res = heap_->GetHugeObjectSpace()->Allocate(objSize, thread_);
            break;
        }
        case SerializedObjectSpace::SHARED_OLD_SPACE: {
            if (sOldSpaceBeginAddr_ + objSize > AlignUp(sOldSpaceBeginAddr_, DEFAULT_REGION_SIZE)) {
                ASSERT(sOldRegionIndex_ < regionVector_.size());
                sOldSpaceBeginAddr_ = regionVector_[sOldRegionIndex_++]->GetBegin();
            }
            res = sOldSpaceBeginAddr_;
            sOldSpaceBeginAddr_ += objSize;
            break;
        }
        case SerializedObjectSpace::SHARED_NON_MOVABLE_SPACE: {
            if (sNonMovableSpaceBeginAddr_ + objSize > AlignUp(sNonMovableSpaceBeginAddr_, DEFAULT_REGION_SIZE)) {
                ASSERT(sNonMovableRegionIndex_ < regionVector_.size());
                sNonMovableSpaceBeginAddr_ = regionVector_[sNonMovableRegionIndex_++]->GetBegin();
            }
            res = sNonMovableSpaceBeginAddr_;
            sNonMovableSpaceBeginAddr_ += objSize;
            break;
        }
        case SerializedObjectSpace::SHARED_HUGE_SPACE: {
            // no gc for this allocate
            res = sheap_->GetHugeObjectSpace()->Allocate(thread_, objSize);
            break;
        }
        default:
            LOG_ECMA(FATAL) << "this branch is unreachable";
            UNREACHABLE();
    }
    return res;
}

JSTaggedType BaseDeserializer::RelocateObjectProtoAddr(uint8_t objectType)
{
    auto env = thread_->GetEcmaVM()->GetGlobalEnv();
    switch (objectType) {
        case (uint8_t)JSType::JS_OBJECT:
            return env->GetObjectFunctionPrototype().GetTaggedType();
        case (uint8_t)JSType::JS_ERROR:
            return JSHandle<JSFunction>(env->GetErrorFunction())->GetFunctionPrototype().GetRawData();
        case (uint8_t)JSType::JS_EVAL_ERROR:
            return JSHandle<JSFunction>(env->GetEvalErrorFunction())->GetFunctionPrototype().GetRawData();
        case (uint8_t)JSType::JS_RANGE_ERROR:
            return JSHandle<JSFunction>(env->GetRangeErrorFunction())->GetFunctionPrototype().GetRawData();
        case (uint8_t)JSType::JS_REFERENCE_ERROR:
            return JSHandle<JSFunction>(env->GetReferenceErrorFunction())->GetFunctionPrototype().GetRawData();
        case (uint8_t)JSType::JS_TYPE_ERROR:
            return JSHandle<JSFunction>(env->GetTypeErrorFunction())->GetFunctionPrototype().GetRawData();
        case (uint8_t)JSType::JS_AGGREGATE_ERROR:
            return JSHandle<JSFunction>(env->GetAggregateErrorFunction())->GetFunctionPrototype().GetRawData();
        case (uint8_t)JSType::JS_URI_ERROR:
            return JSHandle<JSFunction>(env->GetURIErrorFunction())->GetFunctionPrototype().GetRawData();
        case (uint8_t)JSType::JS_SYNTAX_ERROR:
            return JSHandle<JSFunction>(env->GetSyntaxErrorFunction())->GetFunctionPrototype().GetRawData();
        case (uint8_t)JSType::JS_OOM_ERROR:
            return JSHandle<JSFunction>(env->GetOOMErrorFunction())->GetFunctionPrototype().GetRawData();
        case (uint8_t)JSType::JS_TERMINATION_ERROR:
            return JSHandle<JSFunction>(env->GetTerminationErrorFunction())->GetFunctionPrototype().GetRawData();
        case (uint8_t)JSType::JS_DATE:
            return env->GetDatePrototype().GetTaggedType();
        case (uint8_t)JSType::JS_ARRAY:
            return env->GetArrayPrototype().GetTaggedType();
        case (uint8_t)JSType::JS_MAP:
            return env->GetMapPrototype().GetTaggedType();
        case (uint8_t)JSType::JS_SET:
            return env->GetSetPrototype().GetTaggedType();
        case (uint8_t)JSType::JS_REG_EXP:
            return env->GetRegExpPrototype().GetTaggedType();
        case (uint8_t)JSType::JS_INT8_ARRAY:
            return env->GetInt8ArrayFunctionPrototype().GetTaggedType();
        case (uint8_t)JSType::JS_UINT8_ARRAY:
            return env->GetUint8ArrayFunctionPrototype().GetTaggedType();
        case (uint8_t)JSType::JS_UINT8_CLAMPED_ARRAY:
            return env->GetUint8ClampedArrayFunctionPrototype().GetTaggedType();
        case (uint8_t)JSType::JS_INT16_ARRAY:
            return env->GetInt16ArrayFunctionPrototype().GetTaggedType();
        case (uint8_t)JSType::JS_UINT16_ARRAY:
            return env->GetUint16ArrayFunctionPrototype().GetTaggedType();
        case (uint8_t)JSType::JS_INT32_ARRAY:
            return env->GetInt32ArrayFunctionPrototype().GetTaggedType();
        case (uint8_t)JSType::JS_UINT32_ARRAY:
            return env->GetUint32ArrayFunctionPrototype().GetTaggedType();
        case (uint8_t)JSType::JS_FLOAT32_ARRAY:
            return env->GetFloat32ArrayFunctionPrototype().GetTaggedType();
        case (uint8_t)JSType::JS_FLOAT64_ARRAY:
            return env->GetFloat64ArrayFunctionPrototype().GetTaggedType();
        case (uint8_t)JSType::JS_BIGINT64_ARRAY:
            return env->GetBigInt64ArrayFunctionPrototype().GetTaggedType();
        case (uint8_t)JSType::JS_BIGUINT64_ARRAY:
            return env->GetBigUint64ArrayFunctionPrototype().GetTaggedType();
        case (uint8_t)JSType::JS_ARRAY_BUFFER:
            return JSHandle<JSFunction>(env->GetArrayBufferFunction())->GetFunctionPrototype().GetRawData();
        case (uint8_t)JSType::JS_SHARED_ARRAY_BUFFER:
            return JSHandle<JSFunction>(env->GetSharedArrayBufferFunction())->GetFunctionPrototype().GetRawData();
        case (uint8_t)JSType::JS_ASYNC_FUNCTION:
            return env->GetAsyncFunctionPrototype().GetTaggedType();
        case (uint8_t)JSType::BIGINT:
            return JSHandle<JSFunction>(env->GetBigIntFunction())->GetFunctionPrototype().GetRawData();
        default:
            LOG_ECMA(ERROR) << "Relocate unsupported JSType: " << JSHClass::DumpJSType(static_cast<JSType>(objectType));
            LOG_ECMA(FATAL) << "this branch is unreachable";
            UNREACHABLE();
            break;
    }
}

void BaseDeserializer::AllocateToDifferentSpaces()
{
    size_t oldSpaceSize = data_->GetOldSpaceSize();
    if (oldSpaceSize > 0) {
        heap_->GetOldSpace()->IncreaseLiveObjectSize(oldSpaceSize);
        AllocateToOldSpace(oldSpaceSize);
    }
    size_t nonMovableSpaceSize = data_->GetNonMovableSpaceSize();
    if (nonMovableSpaceSize > 0) {
        heap_->GetNonMovableSpace()->IncreaseLiveObjectSize(nonMovableSpaceSize);
        AllocateToNonMovableSpace(nonMovableSpaceSize);
    }
    size_t machineCodeSpaceSize = data_->GetMachineCodeSpaceSize();
    if (machineCodeSpaceSize > 0) {
        heap_->GetMachineCodeSpace()->IncreaseLiveObjectSize(machineCodeSpaceSize);
        AllocateToMachineCodeSpace(machineCodeSpaceSize);
    }
    size_t sOldSpaceSize = data_->GetSharedOldSpaceSize();
    if (sOldSpaceSize > 0) {
        sheap_->GetOldSpace()->IncreaseLiveObjectSize(sOldSpaceSize);
        AllocateToSharedOldSpace(sOldSpaceSize);
    }
    size_t sNonMovableSpaceSize = data_->GetSharedNonMovableSpaceSize();
    if (sNonMovableSpaceSize > 0) {
        sheap_->GetNonMovableSpace()->IncreaseLiveObjectSize(sNonMovableSpaceSize);
        AllocateToSharedNonMovableSpace(sNonMovableSpaceSize);
    }
}

void BaseDeserializer::AllocateMultiRegion(SparseSpace *space, size_t spaceObjSize, size_t &regionIndex)
{
    regionIndex = regionVector_.size();
    size_t regionAlignedSize = SerializeData::AlignUpRegionAvailableSize(spaceObjSize);
    size_t regionNum = regionAlignedSize / Region::GetRegionAvailableSize();
    while (regionNum > 1) { // 1: one region have allocated before
        std::vector<size_t> regionRemainSizeVector = data_->GetRegionRemainSizeVector();
        space->ResetTopPointer(space->GetCurrentRegion()->GetEnd() - regionRemainSizeVector[regionRemainSizeIndex_++]);
        if (!space->Expand()) {
            LOG_ECMA(FATAL) << "BaseDeserializer::OutOfMemory when deserialize";
        }
        regionVector_.push_back(space->GetCurrentRegion());
        regionNum--;
    }
    size_t lastRegionRemainSize = regionAlignedSize - spaceObjSize;
    space->ResetTopPointer(space->GetCurrentRegion()->GetEnd() - lastRegionRemainSize);
}

Region *BaseDeserializer::AllocateMultiSharedRegion(SharedSparseSpace *space, size_t spaceObjSize, size_t &regionIndex)
{
    regionIndex = regionVector_.size();
    size_t regionAlignedSize = SerializeData::AlignUpRegionAvailableSize(spaceObjSize);
    size_t regionNum = regionAlignedSize / Region::GetRegionAvailableSize();
    std::vector<size_t> regionRemainSizeVector = data_->GetRegionRemainSizeVector();
    std::vector<Region *> allocateRegions;
    while (regionNum > 0) {
        if (space->CommittedSizeExceed()) {
            LOG_ECMA(FATAL) << "BaseDeserializer::OutOfMemory when deserialize";
        }
        Region *region = space->AllocateDeserializeRegion(thread_);
        if (regionNum == 1) { // 1: Last allocate region
            size_t lastRegionRemainSize = regionAlignedSize - spaceObjSize;
            region->SetHighWaterMark(region->GetEnd() - lastRegionRemainSize);
        } else {
            region->SetHighWaterMark(region->GetEnd() - regionRemainSizeVector[regionRemainSizeIndex_++]);
        }
        regionVector_.push_back(region);
        allocateRegions.push_back(region);
        regionNum--;
    }
    space->MergeDeserializeAllocateRegions(allocateRegions);
    return allocateRegions.front();
}

void BaseDeserializer::AllocateToOldSpace(size_t oldSpaceSize)
{
    SparseSpace *space = heap_->GetOldSpace();
    uintptr_t object = space->Allocate(oldSpaceSize, false);
    if (UNLIKELY(object == 0U)) {
        if (space->CommittedSizeExceed()) {
            LOG_ECMA(FATAL) << "BaseDeserializer::OutOfMemory when deserialize";
        }
        oldSpaceBeginAddr_ = space->GetCurrentRegion()->GetBegin();
        AllocateMultiRegion(space, oldSpaceSize, oldRegionIndex_);
    } else {
        oldSpaceBeginAddr_ = object;
    }
}

void BaseDeserializer::AllocateToNonMovableSpace(size_t nonMovableSpaceSize)
{
    SparseSpace *space = heap_->GetNonMovableSpace();
    uintptr_t object = space->Allocate(nonMovableSpaceSize, false);
    if (UNLIKELY(object == 0U)) {
        if (space->CommittedSizeExceed()) {
            LOG_ECMA(FATAL) << "BaseDeserializer::OutOfMemory when deserialize";
        }
        nonMovableSpaceBeginAddr_ = space->GetCurrentRegion()->GetBegin();
        AllocateMultiRegion(space, nonMovableSpaceSize, nonMovableRegionIndex_);
    } else {
        nonMovableSpaceBeginAddr_ = object;
    }
}

void BaseDeserializer::AllocateToMachineCodeSpace(size_t machineCodeSpaceSize)
{
    SparseSpace *space = heap_->GetMachineCodeSpace();
    uintptr_t object = space->Allocate(machineCodeSpaceSize, false);
    if (UNLIKELY(object == 0U)) {
        if (space->CommittedSizeExceed()) {
            LOG_ECMA(FATAL) << "BaseDeserializer::OutOfMemory when deserialize";
        }
        machineCodeSpaceBeginAddr_ = space->GetCurrentRegion()->GetBegin();
        AllocateMultiRegion(space, machineCodeSpaceSize, machineCodeRegionIndex_);
    } else {
        machineCodeSpaceBeginAddr_ = object;
    }
}

void BaseDeserializer::AllocateToSharedOldSpace(size_t sOldSpaceSize)
{
    SharedSparseSpace *space = sheap_->GetOldSpace();
    uintptr_t object = space->AllocateNoGCAndExpand(thread_, sOldSpaceSize);
    if (UNLIKELY(object == 0U)) {
        Region *region = AllocateMultiSharedRegion(space, sOldSpaceSize, sOldRegionIndex_);
        sOldSpaceBeginAddr_ = region->GetBegin();
    } else {
        sOldSpaceBeginAddr_ = object;
    }
}

void BaseDeserializer::AllocateToSharedNonMovableSpace(size_t sNonMovableSpaceSize)
{
    SharedNonMovableSpace *space = sheap_->GetNonMovableSpace();
    uintptr_t object = space->AllocateNoGCAndExpand(thread_, sNonMovableSpaceSize);
    if (UNLIKELY(object == 0U)) {
        Region *region = AllocateMultiSharedRegion(space, sNonMovableSpaceSize, sNonMovableRegionIndex_);
        sNonMovableSpaceBeginAddr_ = region->GetBegin();
    } else {
        sNonMovableSpaceBeginAddr_ = object;
    }
}

}  // namespace panda::ecmascript

