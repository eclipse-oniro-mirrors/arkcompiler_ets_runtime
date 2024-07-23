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

#ifndef ECMASCRIPT_SERIALIZER_VALUE_SERIALIZER_H
#define ECMASCRIPT_SERIALIZER_VALUE_SERIALIZER_H

#include "ecmascript/serializer/base_serializer-inl.h"

namespace panda::ecmascript {

class ValueSerializer : public BaseSerializer {
public:
    explicit ValueSerializer(JSThread *thread, bool defaultTransfer = false, bool defaultCloneShared = false)
        : BaseSerializer(thread),  defaultTransfer_(defaultTransfer), defaultCloneShared_(defaultCloneShared) {}
    ~ValueSerializer()
    {
        // clear transfer obj set after serialization
        transferDataSet_.clear();
        cloneArrayBufferSet_.clear();
        cloneSharedSet_.clear();
    }
    NO_COPY_SEMANTIC(ValueSerializer);
    NO_MOVE_SEMANTIC(ValueSerializer);

    bool WriteValue(JSThread *thread, const JSHandle<JSTaggedValue> &value, const JSHandle<JSTaggedValue> &transfer,
                    const JSHandle<JSTaggedValue> &cloneList);

private:
    void SerializeObjectImpl(TaggedObject *object, bool isWeak = false) override;
    void SerializeJSError(TaggedObject *object);
    void SerializeNativeBindingObject(TaggedObject *object);
    bool SerializeJSArrayBufferPrologue(TaggedObject *object);
    void SerializeJSSharedArrayBufferPrologue(TaggedObject *object);
    void SerializeJSSendableArrayBufferPrologue(TaggedObject *object);
    void SerializeJSRegExpPrologue(JSRegExp *jsRegExp);
    void InitTransferSet(CUnorderedSet<uintptr_t> transferDataSet);
    bool PrepareTransfer(JSThread *thread, const JSHandle<JSTaggedValue> &transfer);
    bool PrepareClone(JSThread *thread, const JSHandle<JSTaggedValue> &cloneList);
    bool CheckObjectCanSerialize(TaggedObject *object, bool &findSharedObject);
    bool IsInternalJSType(JSType type)
    {
        if ((type >= JSType::JS_RECORD_FIRST && type <= JSType::JS_RECORD_LAST) ||
            (type == JSType::JS_NATIVE_POINTER && !supportJSNativePointer_)) {
            return false;
        }
        return type >= JSType::HCLASS && type <= JSType::TYPE_LAST && type != JSType::SYMBOL;
    }

private:
    bool defaultTransfer_ {false};
    bool defaultCloneShared_ {false};
    bool notSupport_ {false};
    bool supportJSNativePointer_ {false};
    std::vector<std::pair<ssize_t, panda::JSNApi::NativeBindingInfo *>> detachCallbackInfo_;
    CUnorderedSet<uintptr_t> transferDataSet_;
    CUnorderedSet<uintptr_t> cloneArrayBufferSet_;
    CUnorderedSet<uintptr_t> cloneSharedSet_;
};
}

#endif  // ECMASCRIPT_SERIALIZER_BASE_SERIALIZER_H