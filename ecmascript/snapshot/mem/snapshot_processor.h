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

#ifndef ECMASCRIPT_SNAPSHOT_MEM_SNAPSHOT_PROCESSOR_H
#define ECMASCRIPT_SNAPSHOT_MEM_SNAPSHOT_PROCESSOR_H

#include <iostream>
#include <fstream>
#include <sstream>

#include "ecmascript/snapshot/mem/encode_bit.h"
#include "ecmascript/jspandafile/method_literal.h"
#include "ecmascript/js_tagged_value.h"
#include "ecmascript/mem/object_xray.h"

#include "libpandabase/macros.h"

namespace panda::ecmascript {
class EcmaVM;
class JSPandaFile;

enum class SnapshotType {
    VM_ROOT,
    BUILTINS,
    ETSO
};

using ObjectEncode = std::pair<uint64_t, ecmascript::EncodeBit>;

class SnapshotProcessor final {
public:
    explicit SnapshotProcessor(EcmaVM *vm) : vm_(vm), objXRay_(vm) {}
    ~SnapshotProcessor();

    void Initialize();
    void StopAllocate();
    void WriteObjectToFile(std::fstream &write);
    std::vector<uint32_t> StatisticsObjectSize();
    void ProcessObjectQueue(CQueue<TaggedObject *> *queue, std::unordered_map<uint64_t, ObjectEncode> *data);
    void SerializeObject(TaggedObject *objectHeader, CQueue<TaggedObject *> *queue,
                         std::unordered_map<uint64_t, ObjectEncode> *data);
    void Relocate(SnapshotType type, const JSPandaFile *jsPandaFile, uint64_t rootObjSize);
    void RelocateSpaceObject(Space* space, SnapshotType type, MethodLiteral* methods,
                             size_t methodNums, size_t rootObjSize);
    void SerializePandaFileMethod();
    EncodeBit EncodeTaggedObject(TaggedObject *objectHeader, CQueue<TaggedObject *> *queue,
                                 std::unordered_map<uint64_t, ObjectEncode> *data);
    void EncodeTaggedObjectRange(ObjectSlot start, ObjectSlot end, CQueue<TaggedObject *> *queue,
                                 std::unordered_map<uint64_t, ObjectEncode> *data);
    void DeserializeObjectExcludeString(uintptr_t oldSpaceBegin, size_t oldSpaceObjSize, size_t nonMovableObjSize,
                                        size_t machineCodeObjSize, size_t snapshotObjSize);
    void DeserializeString(uintptr_t stringBegin, uintptr_t stringEnd);

    void SetProgramSerializeStart()
    {
        programSerialize_ = true;
    }

    void SetBuiltinsSerializeStart()
    {
        builtinsSerialize_ = true;
    }

    void SetBuiltinsDeserializeStart()
    {
        builtinsDeserialize_ = true;
    }

    const CVector<uintptr_t> GetStringVector() const
    {
        return stringVector_;
    }

    LocalSpace* GetOldLocalSpace() const
    {
        return oldLocalSpace_;
    }

    size_t GetNativeTableSize() const;

private:
    void SetObjectEncodeField(uintptr_t obj, size_t offset, uint64_t value);

    EncodeBit SerializeObjectHeader(TaggedObject *objectHeader, size_t objectType, CQueue<TaggedObject *> *queue,
                                    std::unordered_map<uint64_t, ObjectEncode> *data);
    uint64_t SerializeTaggedField(JSTaggedType *tagged, CQueue<TaggedObject *> *queue,
                                  std::unordered_map<uint64_t, ObjectEncode> *data);
    void DeserializeField(TaggedObject *objectHeader);
    void DeserializeTaggedField(uint64_t *value);
    void DeserializeNativePointer(uint64_t *value);
    void DeserializeClassWord(TaggedObject *object);
    void DeserializePandaMethod(uintptr_t begin, uintptr_t end, MethodLiteral *methods,
                                size_t &methodNums, size_t &others);
    void DeserializeSpaceObject(uintptr_t beginAddr, Space* space, size_t spaceObjSize);
    void HandleRootObject(SnapshotType type, uintptr_t rootObjectAddr, size_t objType, size_t &constSpecialIndex);

    EncodeBit NativePointerToEncodeBit(void *nativePointer);
    size_t SearchNativeMethodIndex(void *nativePointer);
    uintptr_t TaggedObjectEncodeBitToAddr(EncodeBit taggedBit);
    void WriteSpaceObjectToFile(Space* space, std::fstream &write);
    uint32_t StatisticsSpaceObjectSize(Space* space);
    uintptr_t AllocateObjectToLocalSpace(Space *space, size_t objectSize);

    EcmaVM *vm_ {nullptr};
    LocalSpace *oldLocalSpace_ {nullptr};
    LocalSpace *nonMovableLocalSpace_ {nullptr};
    LocalSpace *machineCodeLocalSpace_ {nullptr};
    SnapshotSpace *snapshotLocalSpace_ {nullptr};
    ObjectXRay objXRay_;
    bool programSerialize_ {false};
    bool builtinsSerialize_ {false};
    bool builtinsDeserialize_ {false};
    CVector<uintptr_t> pandaMethod_;
    CVector<uintptr_t> stringVector_;
    std::unordered_map<uint32_t, Region *> regionIndexMap_;
    size_t regionIndex_ {0};

    NO_COPY_SEMANTIC(SnapshotProcessor);
    NO_MOVE_SEMANTIC(SnapshotProcessor);
};

class PUBLIC_API ConstantPoolProcessor {
public:
    ConstantPoolProcessor(EcmaVM *vm) : vm_(vm), index_(0) {}

    JSTaggedValue GetConstantPoolInfos(size_t nums);

    void CollectConstantPoolInfo(const JSPandaFile* pf, JSHandle<JSTaggedValue> constantPool);

    static void RestoreConstantPoolInfo(JSThread *thread, JSHandle<TaggedArray> constPoolInfos,
                                        const JSPandaFile* pf, JSHandle<ConstantPool> constPool);

private:
    JSTaggedValue GenerateConstantPoolInfo(JSHandle<ConstantPool> constantPool);

    static int BinarySearch(JSHandle<TaggedArray> constPoolInfos, uint32_t target, bool findLeftBound = true);

    static bool EcmaStringsAreEqual(const JSTaggedValue &x, const JSTaggedValue &y);

    EcmaVM *vm_ {nullptr};
    size_t index_ {0};
    static const int ITEM_SIZE = 2;
};
}  // namespace panda::ecmascript

#endif  // ECMASCRIPT_SNAPSHOT_MEM_SNAPSHOT_PROCESSOR_H
